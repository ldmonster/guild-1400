# Wave-12 hardening — src/world history + statistics cluster (W12-HIST)

MCP DOWN: hardening only, no new 1:1 reconstruction. ASAN+UBSAN build of the
cluster's test targets; added malformed/truncated/empty/boundary tests; fixed the
real OOB reads found. Goldens + valid-input paths kept byte-identical.

## Cluster
`src/world/`: history*.{h,cpp} (history, history_parse, history_scan, history_full,
history_second_pass, history_text_pass, history_commandline_pass, history_chronicle,
history_cart_table, history_mission), world_history2, statistics{,_full,_report},
statistic_recon_{chart,dump}, lookup_table_save_recon, wire_history_amt.

## Build / run
```
cmake -S . -B build-asan-hist -DCMAKE_BUILD_TYPE=Debug -DGUILD_BACKEND=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -g"
cmake --build build-asan-hist --target <cluster test targets> -j
```
(build-asan-hist removed at end of the wave.)

## Bugs found + fixed (all genuine OOB; the original did not corrupt memory)

### 1. `HistoryCommandIndex` global-buffer-overflow (CONFIRMED by ASAN)
`src/world/history_commandline_pass.cpp`. The command-keyword match did
`std::memcmp(token, name, strlen(name))`. The original's tokens live in a 6080-byte
NUL-padded scratch, so the `memcmp` read stayed in-bounds; our callers pass
exact-length C strings, so a token shorter than the keyword over-read past the token
end. ASAN flagged it in the pre-existing `world_history_cmdline_pass_test`
(`HistCmdline.CommandIndexMatch`, "STADTKASSE+500" vs the 16-byte "BELAGERUNG_START").
Fix: `std::strncmp` — behaviour-identical (a token shorter than `name` has its NUL
where `name` has a non-NUL char, so the comparison fails there — never a prefix match)
and bounds the read at the token's NUL.

### 2. `HistoryClassifyGroupRef` over-read on a short head
`src/world/history_commandline_pass.cpp`. Loaded a 4-byte prefix dword at `head+1`
(via `memcmp(.., 4)`) and the slot digit at `head[6]` regardless of head length. On a
head shorter than 5 chars the 4-byte prefix compare over-read; on an exactly-5-char
"_SET"-prefixed head the `head[6]` slot-digit read was OOB. Fix: `strncmp` for the
prefix (NUL-bounded, identical result), and read `head[6]` only when `head[5]` is
non-NUL (mirroring the original's NUL-padded scratch, where head[6] would be NUL ->
slot 0). Valid 9-char "X_SET 2 rest" inputs are byte-identical.

### 3. `Chronicle::FormatDate` / `Chronicle::At` unbounded index
`src/world/history_chronicle.{cpp,h}`. The in-memory chronicle models the engine's
loaded-entry list; `FormatDate(i)` and `At(i)` indexed `entries_[i]` with no bound.
The engine never reads an entry it did not load, so a faithful guard bounds the index
to the live `count_` range: `FormatDate` returns a zero date "00.00.0000" for an
out-of-range index; `At` clamps into `[0, count_-1]` (well-defined — the array is a
fixed member). `ScanNextForward` was already bounded by `count_`.

## Reviewed, NOT changed (faithful; bound already present / caller contract)
- `TableFindEntrySlotById` (lookup_table_save_recon): walks `table[result]` for
  result = 0,67,…,4221, breaking once the running index reaches 4288 BEFORE the read,
  so the last read is index 4221. The 64-entry (4288-dword) table contract is the
  original's global; with a correctly-sized table there is no OOB. Pinned with a new
  exact-size miss-scan test.
- `StatisticsCategoryTotal(accum, k)` reads up to `accum[k+15]`; valid for k in 0..3
  over the documented 20-float window. All callers pass 0..3. Boundary test added.
- `CompareSliderColor` clamps the index to [0,7] before the 8-entry palette lookup —
  already safe; added an all-inputs bounds test.
- `statistic_recon_dump` reads fixed offsets up to +0x1A4 / +361 of the 536-byte
  person record and passes raw bytes (0..255) to env table accessors; faithful to the
  original (the table bound, if any, is the env's responsibility). Max-byte (255)
  pass-through tests added; the record reads stay within 536 bytes.
- `HistoryRoleNameIndex` / `HistoryClassifyTokenMode` already use `strncmp` (safe).
- `HistoryCollapseLabel` uses `std::strcpy(out, label)` into a caller scratch — the
  original 6096-byte scratch contract; not an OOB in the reconstruction (callers size
  `out`). Unbalanced/empty/nested-bracket tests added.

## BEHAVIORAL — needs MCP (documented, NOT changed)
- None found in this cluster. The fixes above are pure memory-safety guards that do
  not change observable output or control flow on any valid input.

## Tests added
- `tests/unit/world_history_cmdline_pass_test.cpp`: `CommandIndexShortTokenNoOverread`,
  `GroupRefShortHeadNoOverread`, `SecondPassOverlongTokenNoOverread` (pin fixes 1 & 2;
  NUL-less / short / overlong / truncated-prefix tokens).
- `tests/unit/lookup_table_save_recon_test.cpp`: `MissScanStaysInBoundsOnExactTable`.
- `tests/unit/statistic_recon_test.cpp`: `MaxIndexBytesTraits`, `MaxIndexBytesIdentity`,
  `SliderColorBoundsAllInputs`.
- NEW `tests/unit/world_history_blob_harden_test.cpp` (316 checks): truncated/empty/
  NUL-less date parse+format+roundtrip; chronicle with 0/1/many entries + out-of-range
  FormatDate/At + full-capacity reject (pins fix 3); bracket-collapse on unbalanced/
  empty/nested labels; role-name scan on short/NUL-less/unknown tokens; first-pass
  validity on empty/unbalanced; statistics category-total column boundary.

## Status: all cluster test targets pass under ASAN+UBSAN and in the normal build
unit: world_history_cmdline_pass_test (74), lookup_table_save_recon_test (15),
statistic_recon_test (2642), world_history_blob_harden_test (316),
history_cart_table_test (117), world_history2_test (53), history_mission_test (27),
world_history_full_test (106), history_parse_test (88), wire_history_amt_test (27),
gui_statistics_window_test (32).
integration/e2e (ASAN): world_history_cmdline_pass_{itest,e2e} (20/22),
history_cart_table_e2e (13), history_mission_{itest,e2e} (20/19),
world_history2_{itest,e2e} (9/27), world_history_full_e2e (35). 0 failures.

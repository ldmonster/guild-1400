# Wave-12 hardening — W12-CITY cluster (family / city / relation / world setup)

MCP was DOWN for this wave: NO new 1:1 reconstruction. This pass HARDENS the
world family-tree / city / relation / world-bootstrap cluster with an ASAN+UBSAN
build plus boundary / malformed-input tests, and FIXES the memory-safety / UB it
surfaced. Every golden value and every valid-input path stays BYTE-IDENTICAL; only
out-of-envelope (malformed / out-of-range) inputs are made fail-safe.

## Cluster owned

Sources (edited only these + the test files + this doc):
- `src/world/stammbaum.{h,cpp}` (FamilyTree / parent-child-sibling / heirs / ancestor)
- `src/world/stammbaum_query.{h,cpp}` (gen-distance, *AtGen, inheritance, blood-related)
- `src/world/stammbaum_tree.{h,cpp}` (StammbaumGatherTree, GetFamilyRecord)
- `src/world/family_query.{h,cpp}` (descendants, heir-line)
- `src/world/city.{h,cpp}` (UtilParseInt, CSV field list, InitParameterTable, INI mapper)
- `src/world/city_load.{h,cpp}` (district-coord accessor, definition loader)
- `src/world/city_population_census.{h,cpp}` (aggregate / wealth grid / snapshot)
- `src/world/city_satisfaction_grid.{h,cpp}` (verified clean — already well covered)
- `src/world/relation.{h,cpp}` (768x768 relation matrix — verified clean, wave-2 fixups)
- `src/world/world_setup.{h,cpp}` (WorldCountActiveObjects, WorldSetupInit)
- `src/world/world_io_save_recon.{h,cpp}` (write-side object/building serializers)
- `src/world/groundplan_recon.{h,cpp}` (building-state / Wappen label math)
- `src/world/money_format.{h,cpp}` (thousands-grouped Gulden formatter)

NOT TOUCHED: `src/world/data_load.cpp` (kindWorth OOB already fixed by the
orchestrator). `relation.{h,cpp}` was verified clean (its raw `768*a + b` addressing
is a faithful 1:1 port with no recon array bug — see the "engine envelope" note).

## ASAN+UBSAN build

```
cmake -S . -B build-asan-city -DCMAKE_BUILD_TYPE=Debug -DGUILD_BACKEND=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -g"
cmake --build build-asan-city --target <test targets> -j$(nproc)
```

(build-asan-city removed at the end of the wave.)

## Bugs found & FIXED (memory-safety / UB — faithful, the original did not corrupt)

1. **`groundplan_recon.cpp` — `Groundplan_GetWappenLabelId` OOB read (ASan SEGV).**
   The case-0 plot-grid walk can fall through without selecting a slot (no marker
   == 6 and no terminator == 7); `v1` then walks to the end of the span. The
   following `typeWords[134 * v1]` read had NO bound against the caller's typeWords
   span. In the binary the markers (byte_12CE912) and the type-word details
   (unk_12CEA71) are two columns of ONE contiguous 768-slot person array, so a
   fall-through `v1` still lands in adjacent in-bounds BSS; here the caller passes
   two tightly-sized, co-sized spans, so the read ran off the end.
   FIX: clamp the typeWords read to `[0, markerCount)`; an unselected fall-through
   uses group code 0 (the v0-default path == "no plot selected"). Confirmed via a
   standalone ASan repro (SEGV at groundplan_recon.cpp:140) → returns 1241 after.
   Pinned: `GroundplanReconWappen.GridWalkFallthroughNoSelectionNoOob`,
   `…GridWalkEmptySpanNoOob`. (Selecting a valid slot is byte-identical.)

2. **`world_io_save_recon.cpp` — integer-overflow bypass of the sink bounds check.**
   `RawWrite`'s `s->pos + n > s->cap` and `BioWriteArray`'s `count * stride` were
   computed in 32-bit. A malformed building blob carries the array element count
   `n` at `[edi]`; the payload size is `n*n` (and `4*n*n`), which can wrap u32 to a
   small/zero value and slip a truncated or out-of-bounds memcpy past the buffer
   (e.g. `n == 0x10000` → `n*n` wraps to 0). The original streams to a file (bounded
   by the file boundary); the byte-cursor reconstruction must bound it.
   FIX: compute the capacity check and the array payload size in 64-bit (`u64`) and
   fail the sink cleanly. Valid (non-overflowing) writes are unchanged.
   Pinned: `SaveReconWorldIo.WriteBuildingDataArrayCountOverflowFailsClean`,
   `…WriteBuildingDataArrayTooLargeFailsClean`.

3. **`city.cpp` — `UtilParseInt` signed-integer-overflow UB (UBSan).**
   `v = (u8)*s + 10*v; v -= 48;` accumulates in `int`; an over-long digit run
   overflows the signed multiply (`777777777 * 10`). The binary's `lea`/`imul`-based
   accumulation wraps in 2's complement (no machine-level fault), so the 1:1
   behaviour is wrapping. FIX: accumulate in `u32` (defined wraparound, bit-identical
   to the register value) and reinterpret back to `i32`; the trailing sign negation
   uses `0u - v` (x86 `neg`, wraps for INT_MIN). Observable output for all
   in-range inputs is unchanged. Pinned: `CityLoadHarden.ParseIntEdges` +
   `…CsvFieldListBounds` (drives a 1000-digit token through it under UBSan).

4. **`city_population_census.cpp` — out-of-range district tile → OOB.**
   `CityComputeWealthGrid` bins residents into tightly-sized 8x8 stack arrays
   (`worth[8][8]` / `count[8][8]`); `CityAggregateDistrictStats` loop-1 bins into
   `g_cityGrid` via `GridSatCount(tileX, tileY)`. Neither bounded the tile. A
   placed (onMap) resident's tile is a 0..7 district index in normal operation
   (WorldToCityTile output), but a malformed `WealthResident` / `CensusResident`
   tile (8, negative, 100, …) would corrupt the stack / index g_cityGrid OOB.
   FIX: bound both binning sites to the 8x8 grid (`(unsigned)x < 8 && (unsigned)y
   < 8`) before the write; valid 0..7 tiles are byte-identical. Confirmed via a
   standalone ASan repro (tileX/tileY = 8) → no fault after.
   Pinned: `CityCensusHarden.WealthGridOutOfRangeTilesRejected`,
   `…AggregateRejectsOutOfRangeResidentTiles`.

## Tests added (boundary / malformed / oversized / empty)

New unit test files:
- `tests/unit/world_family_harden_test.cpp` — unknown / out-of-range person ids,
  null & empty & single-element trees, cyclic parent chain, cyclic child chain,
  self-parent, deep 200-node chain past the 64-wide *AtGen frontier, zero/tiny out
  buffers, heir cap (kMaxHeirs == 4), inheritance sibling fallback,
  FamilyAreBloodRelated over none/unknown/cycle ids. (covers stammbaum +
  stammbaum_query + family_query.)
- `tests/unit/world_relation_harden_test.cpp` — the 768x768 grid at the in-bounds
  boundary cells (0,0)/(0,767)/(766,767)/(767,0)/(767,766), self-sentinel, signed
  round-trip (INT8_MIN/MAX), low-byte truncation, asymmetry, reset.
- `tests/unit/world_city_census_harden_test.cpp` — wealth-grid corner tiles (0,0)
  & (7,7), out-of-range/negative tiles rejected, unplaced/off-map skipped, empty
  input, snapshot block+scalars+cap-divisor copy, aggregate with out-of-range
  resident tiles + empty (degenerate-divisor) inputs.
- `tests/unit/world_city_load_harden_test.cpp` — ParseInt edges, CSV bounds
  (maxFields<=0, oversized 1000-char token, fewer/more fields), malformed/empty
  INI, load-from-truncated-INI defaults, overlong NUL-less Stadtname (capped at
  31), district-coord in/boundary/out-of-range, loader slot+file bounds.

Extended existing files:
- `tests/unit/world_money_format_test.cpp` — INT_MAX, INT_MIN (abs widened to
  long long, no UB), INT_MIN with rate, full u32 range grouping core.
- `tests/unit/world_io_save_recon_test.cpp` — the two array-count overflow cases.
- `tests/unit/groundplan_recon_test.cpp` — the two grid-walk fall-through cases.

## ASAN+UBSAN result (owned cluster, all clean)

```
groundplan_recon_test           37  checks, 0 failures
world_io_save_recon_test        13  checks, 0 failures
world_money_format_test         32  checks, 0 failures
world_family_harden_test        49  checks, 0 failures
world_relation_harden_test      19  checks, 0 failures
world_city_census_harden_test   81  checks, 0 failures
world_city_load_harden_test     40  checks, 0 failures
world_stammbaum_tree_test       33  checks, 0 failures
world_economy_test             145  checks, 0 failures   (city.cpp goldens intact)
world_bootstrap_test            79  checks, 0 failures
city_satisfaction_grid_test    203  checks, 0 failures
city_satisfaction_grid_itest   105  checks, 0 failures
```
Normal (non-sanitizer) build of the same targets is green; goldens byte-identical.

## BEHAVIORAL — needs MCP (NOT changed; documented per the brief)

- **`city.cpp` 0x50704c — `CityParseCsvFieldList` trailing empty token.** With
  fewer commas than `maxFields`, the loop parses ONE extra empty token
  (`ParseInt("") == 0`) before the "last" flag stops it: e.g. `"10,20"` with
  maxFields 4 returns n==3 with out[2]==0 (the header comment claims the remaining
  slots keep their prior value). This is observable behaviour on valid input, NOT
  a memory-safety issue, so it is pinned as-is in `CsvFieldListBounds` and flagged
  here for a 1:1 cross-check against the binary's v16/"last field" handling.

- **`relation.cpp` 0x5942fc — no range guard on (a,b).** `RelationGet/Set` are a
  1:1 port of the binary's raw `dword_123D6CD[192*a] + b` pointer arithmetic, which
  carries NO bound on a/b. An out-of-range index faults in BOTH the binary and the
  reconstruction (it is the engine's own envelope), so NO guard was added — adding
  a bound the original lacks would be a behavioural change. The hardening tests stay
  strictly inside the 768x768 grid (max valid byte offset 589823 of 589824).

- **`city_population_census.cpp` 0x578abc — degenerate divisors.** With a zero live
  slot count / zero building sums, the aggregate divides by zero in DOUBLE
  arithmetic (v3, v44, v50, v52), producing inf/nan exactly as the original FPU
  path would on the same degenerate input. No integer div-by-zero exists. Left
  as-is (engine envelope); `AggregateEmptyInputsNoCrash` only asserts no fault/OOB.
```

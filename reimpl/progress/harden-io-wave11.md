# Wave-11 hardening — io cluster (W11-IO)

MCP was DOWN: **no new 1:1 reconstruction**, hardening only. ASAN+UBSAN
(`-fsanitize=address,undefined -fno-sanitize-recover=all`) build of the io test
targets, malformed/truncated/oversized input tests, and memory-safety fixes.

Faithfulness rule applied throughout: the valid-asset / in-bounds path is
byte-identical; only genuine OOB / UB / leaks were fixed. Anything that would
change observable output on VALID input is flagged **BEHAVIORAL — needs MCP**, not
changed.

Build dir: `build-asan-io` (dedicated, to avoid clobbering other agents' shared
`build-asan`). Normal `build/` kept green for all owned targets.

---

## Bugs found + fixed (all confirmed by an ASAN/UBSAN abort before, clean after)

### 1. `src/io/vfs_tree.cpp:353` — strcpy-param-overlap (UB)
`NormalizeDirPath` did `std::strcpy(comp, sep + 1)` where `sep` points *into*
`comp` — an in-place left-shift of the tail with overlapping src/dst (UB; ASAN
`strcpy-param-overlap`). Replaced with `std::memmove(comp, sep+1, strlen+1)`,
which is byte-identical for a left shift and well-defined.
Pinned by: `io_vfs_tree` existing `NormalizeDirPath`/dir-walk tests (now clean).

### 2. `src/io/vfs_tree.cpp` + `src/io/vfs_recon_mutate.cpp` — array LEAK on empty-dir
When `CloseFileEntry` removes the *last* file in a directory, it nulls the dir's
`+0x104` array slot but the registry-backed heap block (allocated in
`BuildFinishedFileList`) was never freed — the default `g_freeArray` hook was an
inert no-op. Genuine leak (the original frees the block here).
Fix: added `FreeNodeArray(VfsNode*)` to `vfs_tree.{h,cpp}` (frees the block via the
private array registry and nulls the slot) and wired `vfs_recon_mutate`'s default
free hook to call it. Observable behavior unchanged (slot still nulled, disk
delete still fires).
Pinned by: `io_vfs_recon_mutate.CloseFileEntryEmptiesArray` (now leak-free).

### 3. `src/io/vfs_recon3_tempdir.cpp:92` — global buffer OOB write (1 byte)
`g_cache` was `0x104` bytes, but the documented boundary path accepts a strlen
`0x103` value then appends a separator + NUL, writing offsets `0x103` and `0x104`
(needs `0x105` bytes). In the binary the trailing NUL lands in the adjacent global
(`byte_64AC10` adjacency); here it ran 1 byte past the array (UBSAN: index 260 OOB
for `char[260]`). Sized `g_cache` to `0x105` — all visible output (string content,
strlen) is byte-identical; only the 1-byte trailing NUL is now in-buffer.
Pinned by: `vfs_recon3_tempdir.LengthGateAccepts0x103`.

### 4. `src/io/vfs_recon3_tempdir.cpp` + `src/io/file_ops3.{h,cpp}` — working-dir LEAK
`VfsGetTempDir`'s working-dir fallback called `VfsGetWorkingDir(nullptr,0)`, which
returns an **owned** heap buffer (allocated via the FileOps3 `allocMem` hook), then
copied its bytes into `g_cache` and dropped the pointer — a leak (the original
frees it after copying). Fix: added a partner `freeMem` hook to `FileOps3Hooks`
(+ `VfsFreeMem`) — same OS-leaf pattern as `allocMem` — and free `wd` after the
copy. Default hook is an inert no-op (portable build). Test installs a real
`free`. Observable result (the computed temp-dir string) unchanged.
Pinned by: `vfs_recon3_tempdir.WorkingDirFallback` + friends (now leak-free).

### 5. `src/io/save.cpp:297` — misaligned `u32&` reference (UB)
The relink table has a 10-byte stride, so the pointer/id slot at `off+6` is
naturally unaligned. The code bound a `u32&` to it (UBSAN: misaligned reference).
Replaced the `RelinkPtr_` reference helper with memcpy-based
`RelinkPtrGet_`/`RelinkPtrSet_`. Byte-identical little-endian read/write of the
same slot (matches the original `*(DWORD*)(table+n+6)` raw access), no UB.
Pinned by: `io_save.relink_table_dispatch`.

### 6. `src/io/bio_codec.cpp:178` (`ZipTellCurrentFile`) — misaligned ptr/int (UB)
`unz_s +124` (pointer) and file-info `+24` (int) are read at offsets that are not
naturally aligned in the byte-modeled struct (a 32-bit-struct reconstruction
artifact). UBSAN flagged the misaligned load. Switched both reads to memcpy —
byte-identical. (Also fixed the matching misaligned *stores* in
`bio_codec_test.cpp:200/202`.)
Pinned by: `BioCodec.ZipTellCurrentFile_Pure`.

### 7. TEST BUG — `tests/unit/save_serial3_test.cpp:37/57` — undersized source buffer
`SaveWriteBuildingSlotTables` strides the slot tables by the **in-memory** stride
`kBst_CitySlotTableStride` (7952), but the test allocated `slot` by the **emitted**
size (2496) → heap-buffer-overflow READ while writing. Source is correct (faithful
stride); fixed the test to size `slot` by `kBst_CitySlotTableStride`. Goldens
(emitted byte counts) unchanged.

---

## Edge / malformed / truncated / oversized tests added

### `io_zip_archive_test.cpp` (+10 tests, all clean under ASAN)
- `malformed_empty_archive` (0-byte), `malformed_tiny_archives` (1..8 bytes)
- `malformed_header_only` (local sig only, no central dir/EOCD)
- `malformed_truncated_eocd` (EOCD sig but record cut short)
- `malformed_bad_central_dir_offset` (off+size past EOF → rejected; Seek guards)
- `malformed_count_too_large` (entries = 0xFFFFFFFF; walk terminates, no OOB)
- `malformed_member_size_exceeds_archive` (compressedSize patched huge → extract
  fails at the `dataOff+compSize > file size` guard)
- `malformed_truncated_deflate` (archive cut mid-deflate-stream)
- `malformed_overlong_locate_name` (name ≥256 and null → PARAMERROR)

### `io_save_world_test.cpp` (+9 tests, all clean under ASAN)
- `person_index_empty_count`, `person_index_truncated_midrecord`,
  `person_index_zero_byte`, `person_index_capacity_boundary` (writes the last
  in-range slot, ASAN watches the upper bound)
- `global_counters_truncated`
- `city_records_empty_count`, `city_records_truncated_midrecord`
- `building_slot_tables_truncated`
- `character_slot_truncated_and_null`

### `io_save_test.cpp` (+5 tests, all clean under ASAN)
- `malformed_zero_byte_header`, `malformed_tiny_header` (1 & 4 bytes)
- `malformed_truncated_thumbnail` (cut mid-0xE100-thumbnail)
- `malformed_version_below_floor` (version < 0x10025 floor → reject)
- `malformed_truncated_scalar`

---

## Verified-clean (no fix needed)

- `src/io/zip_archive.cpp` — all reads go through `Seek` (bounds-checked vs
  `file_.size()`) and `cursor_ + n > file_.size()` guards; filename copy clamps to
  `nameCap`; `ExtractCurrentFile` guards `dataOff+compSize > file size`. Robust to
  bad central-dir offsets/counts, oversized member sizes, truncation.
- `src/io/archive_mount.cpp` — std::string/std::vector only; rides on the hardened
  `ZipArchive`.
- `src/io/bio_codec.cpp` — `stride*count` u32 overflow is matched in BOTH the alloc
  and the read (`expectStride==stride`), and `VfsReadStream` clamps to the stream's
  remaining bytes, so the destination is never overrun. Faithful to the original's
  arithmetic.
- `src/io/save.cpp` header/scalar loaders — all `ReadExact`, fixed-size fields,
  bounded thumbnail skip scratch.
- `src/io/vfs.cpp` `VfsReadStream`/`VfsWriteStream` — clamp to available / write
  remaining before memcpy.

## Real-asset e2e (ASAN+UBSAN, `GUILD_GAME_DIR` set) — ALL CLEAN
`io_save_world_real_e2e_test` (full Augsburg .cty/.SAV load through every table
loader), `io_zip_archive_e2e_test`, `io_vfs_e2e_test`, `gfx_archive_e2e_test`
(real .BIN extraction through compress::InflateRaw), `bio_codec_e2e_test` — no OOB,
no leak, no UB. This confirms the real save's count/marker fields stay within the
fixed capacities (the clamp question below only affects *malformed* saves).

---

## BEHAVIORAL — needs MCP (do NOT change without confirming the binary)

1. **save_world_load: unbounded count/marker scatter.**
   `LoadPersonIndexTable` (`@0x5a7ffc`) trusts the file `count` and indexes
   `tileBase + i*67` for `i < count` with no check against `kSceneTileCapacity`
   (8192). `LoadCityRecords` (`@0x5a8d3c`) trusts each record's leading `marker`
   (u16, ≤65535) as a direct slot index into `personBase + marker*536` with no
   check against `kCityRecCapacity` (768). A malformed save with `count > 8192` or
   `marker ≥ 768` scatters past the fixed buffer — exactly as the original's
   unbounded write into `dword_13CE290` / `word_12CE910`. Whether the binary
   clamps the index is a 1:1 question (it may rely on global adjacency / trust the
   file). Tests deliberately stay at/below capacity and exercise the truncation
   guards instead. **Decision needed: does the original bound count/marker?** If
   yes, add the same clamp; if no, this is the engine envelope (document only).

2. **vfs_recon3_tempdir: uncapped working-dir copy loop.**
   The working-dir fallback copy loop (`vfs_recon3_tempdir.cpp` ~line 82) copies the
   working directory verbatim with no length cap (the env path IS capped at 0x103
   by the `getFullPath` hook). A working dir > 0x104 chars would overflow even the
   enlarged `g_cache[0x105]`. Matches the binary's verbatim copy (which would run
   into adjacent globals). The exact free address for `wd` (~0x5fc960) and any cap
   need MCP confirmation. Not reachable from the inert default (no working dir of
   that length in practice); fix #4 plugged the leak on the in-range path.

## Cross-cluster notes
- The "compress::Inflater-rooted leak via zip extract" flagged by other agents does
  **not** root in io's own allocation: the real-asset `gfx_archive_e2e` /
  `io_zip_archive_e2e` deflate-extraction runs leak-clean under ASAN here. If a leak
  remains it roots in `compress/` (InflateRaw internal state) — that owner's call.
  No io-side change made.

## Files touched
Source: `src/io/vfs_tree.{h,cpp}`, `src/io/vfs_recon_mutate.cpp`,
`src/io/vfs_recon3_tempdir.cpp`, `src/io/file_ops3.{h,cpp}`, `src/io/save.cpp`,
`src/io/bio_codec.cpp`.
Tests: `tests/unit/io_zip_archive_test.cpp`, `tests/unit/io_save_world_test.cpp`,
`tests/unit/io_save_test.cpp`, `tests/unit/save_serial3_test.cpp`,
`tests/unit/bio_codec_test.cpp`, `tests/unit/vfs_recon3_tempdir_test.cpp`.

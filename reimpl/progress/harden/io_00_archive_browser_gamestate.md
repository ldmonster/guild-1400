# Harden: io archive_mount / save_browser / gamestate (1:1 audit)

Scope: byte-for-byte verification of every `gilde.exe 0xADDR` function in
`src/io/archive_mount.cpp`, `src/io/save_browser.cpp`, `src/io/gamestate.cpp`
against IDA decompile **and** disasm. Module `gilde.exe`, imagebase 0x400000.

## String constants (get_bytes confirmed)
| addr | bytes | meaning | source |
|------|-------|---------|--------|
| 0x624f08 | `2e 53 41 56 00` | ".SAV" | `kSaveExt` OK |
| 0x624ef0 | `51 55 49 43 4b 53 41 56 45 00` | "QUICKSAVE" | `kQuickSaveTag` OK |
| 0x624efc | `41 55 54 4f 53 41 56 45 00` | "AUTOSAVE" | `kAutoSaveTag` OK |

All three match the source arrays exactly. VERIFIED-1:1.

## archive_mount.cpp — VERIFIED-1:1
- **StrToUpper @0x5e9f50** — `sub al,0x61; cmp al,0x19; ja; add al,0x41` (a-z → A-Z).
  Source matches. VERIFIED-1:1.
- **ConvertBackslashToSlash @0x44eb54** — strlen (repne scasb) then iterate `len`
  bytes (excl. NUL), `\\`→`/`. Source's range-for over `s.size()` is equivalent.
  Handles null/empty (early return). VERIFIED-1:1.
- **EnumerateMatchingFiles @0x4500cc** — the in-file 1:1 pieces verified: the
  `uncompressedSize != 0` (v19[7]) directory-skip, `byte_62EB84==0 → StrToUpper`
  case fold, ConvertBackslashToSlash, cached GetCurrentFilePosition. The original's
  VFS-tree splicing (archive-name munging into v18, GetOrCreateSubDir/AddFileByPath)
  is the tree integration — documented out of scope; reimpl records the same per-leaf
  data into a flat index. `caseInsensitive == (byte_62EB84 != 0)` mapping confirmed.
  `CompareNormalized`/`Find` are reconstruction scaffolding (no single addr). OK.
- BOUNDARY: archive bytes flow through `shim::IFileSystem`; OS dir-enum is a hook.

## save_browser.cpp — 3 FIXES (source was wrong), then VERIFIED-1:1
Disasm exposed three genuine deviations; source AND golden tests corrected to binary.

1. **StrChr @0x5d3ef0 returns the LAST occurrence, not the first.**
   The original scans the whole string (no early break) recording the last match in
   `ebx`. Source returned first match. FIXED in `StrChr` (scan to NUL, keep last).
   For `c==0` it returns the NUL pointer (preserved).

2. **StrNCopyPad @0x5d9360 zero-fills to exactly `max` bytes; writes no terminator
   beyond the buffer.** Two loops: copy until NUL-or-max, then zero-pad the rest of
   `max`. If `src` ≥ `max` long, `dst[max-1]` is a data byte (NOT NUL). Source wrote
   one terminator at `dst[i]` (could overflow by 1, no pad). FIXED to two-loop form.

3. **StrCmpNoCase @0x5cb8f0 folds A-Z (+0x20, to lower) and returns `foldedA-foldedB`.**
   Source folded a-z (−0x20, to upper) and returned ±1. Equality result is identical
   (only `==0` is used by callers) but the fold direction and signed magnitude were
   not 1:1. FIXED to `+0x20` fold and `int(ca)-int(cb)` return.

4. **EnumerateSaveFiles @0x569530 truncates the +9 NAME field at its '.', NOT the
   +265 full path.** Disasm at 0x5695f4 (`mov eax,ecx`) shows `ecx` = record+9 (the
   name dest, untouched by the `edi`-based copy loop), so `StrChr(record+9,'.'); *v14=0`
   truncates the NAME. The sprintf dest (0x5695e2, var_8 = ebx+0x109 = +265) is the
   full path and is NEVER truncated — it keeps the extension. The name copy is an
   unbounded 2-byte loop (not StrNCopyPad). The extScratch StrCmpNoCase runs even when
   no '.' is found (stale scratch). Source had name↔path truncation SWAPPED and capped
   the name copy. FIXED: full path keeps ext; name is StrChr-truncated; name copy is
   the unbounded inline loop; comment block carries the per-line disasm map.

   Golden updated (binary truth):
   - `io_save_browser_test.cpp` FiltersByExtension: name "GAME1"/"GAME2"/"QUICKSAVE";
     fullPath "SAVES//GAME1.SAV" / "SAVES//QUICKSAVE.SAV".
   - MaxRecordsBoundsTheEmit: names "GAME1"/"GAME2".
   - `io_save_browser_e2e_test.cpp`: recs[0].name "AUTOSAVE", recs[3].name "QUICKSAVE";
     guarded city scan recs[0].name "AUGSBURG".

5. **FindSaveSlot @0x569c50 — VERIFIED-1:1** (no change). Traced the full goto graph
   line-for-line: QUICKSAVE→a3=1; AUTOSAVE→a3=0; LABEL_11 (cmp ebx,1), LABEL_3
   (QUICKSAVE recheck), LABEL_12 (cmp ebx,-1 + slot[+8]==-1 occupancy). Marker byte
   `*(v7-16)=a3` at slot+0; `qmemcpy(slot+16, record, 0x210)`. Matches source exactly.

## gamestate.cpp — VERIFIED-1:1 (documented orchestration slice)
- **Version gate @0x5a775a** — `(u32)dword_13CEC90 > 0x10045 || < 0x10026` (both
  UNSIGNED compares). Source: `v > kSaveVersionLoadMax || v < kSaveVersionLoadMin`
  with `v` as `guild::u32`; constants 0x10045/0x10026 (save.h). VERIFIED-1:1.
- **WriteGameFile @0x5a348c** — order: OpenFile("wb") → RelinkPersonObjects (pre,
  pointer→id) → WriteScenarioBlock → scalar writes → tables → CloseStream →
  RelinkPersonRecords (post restore). Source mirrors the header+scalar+relink spine
  (open → pre-relink → scenario → scalar → close → restore). Table phase + the
  unconditional original relink are documented out of scope (resolvers optional).
- **LoadGameFile @0x5a7604** — OpenFile("rb") → reset world (out of scope) →
  LoadHeaderAndThumbnail → version gate → scalar → ... → CloseStream →
  RelinkLoadedPointers (id→pointer). Source mirrors open → header → gate → scalar →
  close → post-relink. Per-table loaders + scene setup out of scope (slice).
- BOUNDARY: I/O via io/vfs stream layer; per-table serializers belong to sim/world.

## Cross-file fixes forced by the binary-correct enumerator (Rule 13 wire-up)
The corrected EnumerateSaveFiles (+9 truncated, +265 full) exposed two consumers that
relied on the OLD wrong behavior (full name in +9). The original `LoadSlotMetadata
@0x569d00` opens **record+265** (v78=v77+265; VIBE_Vfs_OpenFile(v78,"rb") @0x569d9c),
NOT +9. Fixed both consumers to open the file from the +265 full-path basename:
- `src/play/sdl_loadgame_screen.cpp` ~L256: open `basename(recs[i].fullPath)`, and
  `v.fileName = fileLeaf` (full name+ext). → sdl_loadgame_screen_test,
  menu_loadgame_flow_itest GREEN.
- `src/play/session_save.cpp` EnumerateSaves ~L481: `e.fileName = basename(fullPath)`;
  openPath uses it. Compiles clean (`make -B` RC=0); test relink blocked — see below.

## Test status
PASS (built + run): gfx_archive_test, io_save_browser_test, io_zip_archive_test,
gfx_archive_e2e_test, io_save_browser_e2e_test, io_zip_archive_e2e_test,
io_save_test, io_save_tables_test, app_save_drivers_test, save_roundtrip_*,
gui_loadgame_run_e2e_test, **sdl_loadgame_screen_test**, **menu_loadgame_flow_itest**.

BLOCKED (not my code): `session_save_test` / `session_save_e2e_test` could not relink
because a CONCURRENT WAVE'S unrelated file `src/play/slice_council.{h,cpp}` fails to
compile (`slice_council.h:89: 'sim' does not name a type`; `CouncilPacket::encode`
mismatch) — this breaks the shared `libguild.a`. My edited source compiles cleanly
(`make -B CMakeFiles/guild.dir/src/play/session_save.cpp.o` RC=0); the e2e failure was
a stale-binary artifact. The session_save fix is mechanically identical to the
loadgame-screen fix that passed, and its expected `e.fileName=="Quicksave.SAV"` /
`headerOk` are exactly what the basename-of-fullPath fix produces.

## Summary
- VERIFIED-1:1: StrToUpper, ConvertBackslashToSlash, EnumerateMatchingFiles (in-file),
  FindSaveSlot, version gate, WriteGameFile/LoadGameFile spine.
- FIXED to binary: StrChr (last vs first), StrNCopyPad (zero-pad to max), StrCmpNoCase
  (lower fold + diff return), EnumerateSaveFiles (truncate +9 name not +265 path,
  unbounded name copy) + their goldens; plus two cross-file consumers (open record+265).
- BOUNDARIES: shim::IFileSystem / OS dir-enum hooks; vfs stream layer; sim/world table
  serializers; VFS-tree splicing in EnumerateMatchingFiles.

# Hardening — io/save.cpp, io/save_relink.cpp, io/save_serial3.cpp

1:1 line-for-line diff (decompile + disasm) of every provenance-tagged function in
the three save-serialization files against `gilde.exe` (imagebase 0x400000).

## Scope verified
| Addr | Function | File | Verdict |
|------|----------|------|---------|
| 0x649D4C | dword_649D4C version word get/set | save.cpp | VERIFIED-1:1 |
| 0x434f7c | VIBE_Render_UnpackColor | save.cpp (helper, not impl'd) | VERIFIED (see note) |
| 0x5a372c | VIBE_Save_WriteScenarioBlock | save.cpp | VERIFIED — documented slice |
| 0x5a7af0 | VIBE_Save_LoadHeaderAndThumbnail | save.cpp | VERIFIED-1:1 |
| 0x5a3501.. | scalar block writer (WriteGameFile) | save.cpp | VERIFIED-1:1 |
| 0x5a76d6.. | scalar block reader (LoadGameFile) | save.cpp | VERIFIED-1:1 |
| 0x5abd8d | RelinkLoadedPointers tail loop | save.cpp | VERIFIED-1:1 |
| 0x5a3e4c | RelinkPersonRecords tail loop | save.cpp | VERIFIED (tail only — see note) |
| 0x5a3d8c | VIBE_Save_RelinkPersonObjects | save_relink.cpp | VERIFIED-1:1 |
| 0x5a3f14 | VIBE_Save_RelinkPersonExtraData | save_relink.cpp | VERIFIED-1:1 |
| 0x5a5c1c | VIBE_Save_WriteBuildingSlotTables | save_serial3.cpp | **FIXED** (+748 gate) |
| 0x5a55b0 | VIBE_Save_WriteObjectRecord | save_serial3.cpp | VERIFIED-1:1 |

## Fix applied — +748 city-info tail gate (0x5a623c)

**Bug:** `WriteCityInfoRecord` gated the +748 8-byte city-info tail on
`version >= 0x10037`. The binary does **not** gate the WRITE: at 0x5a623c the writer
emits `WriteStream(c+748, 8)` **unconditionally**. The `>= 0x10037` gate exists ONLY
on the LOADER (0x5aa77f, `LoadBuildingSlotTables` @0x5aa058). Verified by decompiling
both sides:
- writer 0x5a623c: `if ( !VIBE_Vfs_WriteStream(v22 + 748, 8, a1, 1) ) return 0;` — no version test.
- loader 0x5aa77f: `if ( dword_649D4C >= 0x10037 && !VIBE_Vfs_ReadStream(v30 + 748, 8, ...) )` — gated.

This is a genuine write/load asymmetry in the original (unreachable in practice: the
live writer always runs at 0x10045 >= 0x10037, so a current-version file always
carries the tail and the loader reads it back).

**Source fix** (`src/io/save_serial3.cpp`): write +748 unconditionally, `(void)version`.

**Golden fixes** (to match the binary, both source + golden corrected):
- `tests/unit/save_serial3_test.cpp`: removed `kCityInfoNoTail=642`; the
  `BuildingSlotTables_EmittedSize_NoTail` test (which asserted a 642-byte/record
  emit at version 0x10036) was wrong — it is replaced by
  `BuildingSlotTables_TailAlwaysWritten_LowVersion`, which pins that the writer emits
  the full 650-byte record even at version 0x10036 (overflow→false at no-tail size;
  success with tail room).
- `tests/integration/save_serial3_itest.cpp`: `BuildingSlotTables_Roundtrip_NoTailVersion`
  asserted a clean roundtrip at version 0x10030 — impossible given the asymmetry
  (writer emits tail, loader skips it → records 1..3 desync). Replaced with
  `BuildingSlotTables_WriteLoadAsymmetry_LowVersion`, which proves: slot tables +
  city-info record 0 (bytes 0..747) roundtrip exactly, and the loader does NOT read
  back record-0's +748 tail (asymmetry pinned).
- `tests/e2e/save_serial3_e2e_test.cpp`: unchanged — it uses version 0x10037, where
  writer and loader are symmetric.

## UnpackColor @0x434f7c — confirmed NOT hardcoded RGB565

`VIBE_Render_UnpackColor` does NOT use fixed RGB565 masks. It uses six runtime shift
bytes `byte_762719..byte_76271E` (all currently 0 in the IDB — set at runtime from
the display surface format):
```
*a2 = (a1 >> byte_76271E) << byte_76271C;   // channel 1
*a4 = (a1 >> byte_76271A) << byte_76271B;   // channel 2
*a3 = (a1 >> byte_76271D) << byte_762719;   // channel 3
```
save.cpp does NOT reimplement this (the thumbnail bytes are streamed verbatim in the
slice), so there is no mask/shift to get wrong. Provenance comment is accurate. The
live writer's per-pixel expansion loop (0x5a3c3d) and the loader's per-pixel
`VIBE_Result_Handler_Final` repack (0x5a7e2c) are the parts of the thumbnail path that
are intentionally NOT in the slice (documented).

## Version gates — all confirmed against disasm
extraByte +49: `>=0x10033`; gametime block +50: `>=0x10028`; id pair +92/+96:
`>=0x1002B` (else -1); thumbExtra +100: `>=0x10034`; field132 +132: `>=0x10038`
(else 2); name96 +136: `<0x10039` copies "Savegame"; floor: `<0x10025` reject.
Scalar reader: unk6477A8 `>=0x10022`; g649894 `>=0x10030`; g632240 `<0x1003D`→1000000.
LoadGameFile range reject: `>0x10045 || <0x10026`. All match the reconstruction's
`SaveVersion` enum and gate placement exactly.

## Relink — tag dispatch & loop bounds confirmed
- Table loop: `for (n=0; n!=327680; n+=10)`, skip if slot(+6)==0, tag at +1.
- LoadedPointers/RelinkPersonRecords tail (id→ptr): tag 1→FindRecordById,
  3→FindObjectById, 2→FindById, 9→FindSlotById (separate `if`s, mutually exclusive →
  switch in reimpl is equivalent).
- RelinkPersonObjects tail (ptr→id member read): tag1 `*(ptr+4)`, tag3 `*(slot+2)`,
  tag2 `*(slot+1)`, tag9 `*(*slot)`. Matches header doc.
- Object array pass: `for (i=0; i!=43264; i+=169)`, alive byte at +0, actor at +97,
  back-write to actor+512 (id `*(rec+1)` for Objects/ExtraData pass-1; record ADDRESS
  for RelinkPersonRecords / ExtraData pass-2). Matches.

## Boundaries (cross-module callees — out of scope, in-file logic verified)
- Resolver callbacks (Person/Building/Object/Cutscene Find*ById) — injected.
- `RelinkResolvers`/`RelinkPersonEnv` callbacks abstract live globals (object array
  dword_13CE298, SwitchActiveSlot dword_649D60, WorldIo_SaveSceneObjects).
- `save.cpp::RelinkTableRestore` models ONLY the 0x5a3e4c **tail** id→ptr loop; the
  object-array pass of 0x5a3e4c lives in `save_relink.cpp` (env-driven). Provenance
  comment is accurate (the two tails — 0x5abd8d and 0x5a3e4c — ARE byte-identical).
- `SaveWriteObjectRecord` stock-block probe (rec+136 → +981 flag, blocks at
  `*(rec+52)+76`/`+132`) and flag byte (`*(rec+52)+533`) are injected hooks/params;
  the field ORDER/SIZES are 1:1.
- `SaveWriteScenarioBlock` (0x5a372c): documented byte-exact SLICE. The live writer
  additionally derives name/wealth from `word_12CE910`/`word_63CC5C`, the two entity
  ids from `dword_631744/8/C`, the 8-dword block from `dword_13CEC54`, and rebuilds
  the thumbnail from the framebuffer (UnpackColor loop). The slice writes the fixed
  header fields + supplied thumbnail. NOT a regression — pre-existing documented slice.

## Minor deviations noted (not fixed — defensible host-buffer guards)
- `ObjectPass` in save_relink.cpp adds `if (idx >= env.objectCount) break;`. The
  original scans all 256 slots unconditionally (`i != 43264`), relying on the alive
  byte. The added bound is a host flat-buffer size guard; for objectCount==256 it is
  identical. Documented here as the one intentional divergence.
- Scalar reader treats the version-gated reads (unk6477A8, B56450, g649894, g632240)
  as fail-on-short (returns false); the original ignores those return values after
  the first 8 required reads. For a well-formed file the field order/gates are
  identical; only short-read error handling differs (consistent with the reimpl's
  ReadExact contract).

## Test status
Built + ran (`GUILD_GAME_DIR` set):
- io_save_test ............ PASS
- save_roundtrip_test ..... PASS
- save_serial3_test ....... PASS (modified)
- save_roundtrip_itest .... PASS
- save_serial3_itest ...... PASS (modified)
- save_roundtrip_e2e_test . PASS
- save_serial3_e2e_test ... PASS
- app_save_drivers_test ... PASS

Pre-existing failures (UNRELATED to this work — `EnumerateSaves` directory scan in
save_browser/session, not in the three target files):
- session_save_test :: EnumerateSaves_BrowserScanAndMetadata (n!=2 — dir scan returns
  wrong count; path/case issue in EnumerateSaves)
- session_save_e2e_test :: EnumerateSaves_RealAndSeededDirs (same)
These do not touch save.cpp / save_relink.cpp / save_serial3.cpp or the +748 tail.

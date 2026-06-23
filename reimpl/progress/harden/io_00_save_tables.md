# Harden: io_00 save_tables — 1:1 verification

Scope: `src/io/save_tables.cpp`, `src/io/save_world_tables_save_recon.cpp` (+ headers).
Method: per provenance address, decompile + diff field order / sizes / strides /
version gates against Hex-Rays. Module `gilde.exe`, imagebase 0x400000.

## save_tables.cpp — VERIFIED-1:1 (all 12 functions)

| Addr | Function | Verdict | Key checks |
|------|----------|---------|-----------|
| 0x5a3fa4 | WriteMapTileTable | VERIFIED-1:1 | count loop `!=548864`, stride 67; fields +0(2)+2(4)+6(4)+10(4)+14(4)+18(1)+19(1)+28(0x1F) |
| 0x5a8d3c | LoadCityRecords (read-order ref) | VERIFIED (ref) | maptile load mirror reads count then count records; field order matches writer |
| 0x5a7520 | WriteHotkeyTable | VERIFIED-1:1 | header(4) then 24x {038,098,100,160} each 4 |
| 0x5aba98 | LoadHotkeyTable | VERIFIED-1:1 | same on-disk order (038,098,100,160) |
| 0x5aa7a8 | LoadMapTiles | VERIFIED-1:1 | side: `<0x1003A`→256, `>=0x10042`→768, else 512 (UNSIGNED cmp). overlay gate `<0x10036`. dest row stride 768 |
| 0x5a6258 | WriteMapTiles | VERIFIED-1:1 | both grids 768x768, row stride 768; height (dword_123D6CD+3) then overlay (byte_1333110) |
| 0x5a6ff4 | WriteHistoryAndCarts | VERIFIED-1:1 | flag(1), d1=cur1-base1(4), d2=cur2-base2(4), 16 flag bytes, 4 carts stride 68: +0(4) then 8x{+0(4),+4(1)} step 8 |
| 0x5ab59c | LoadHistoryAndCarts | VERIFIED-1:1 | cur=base+delta; cart layout identical |
| 0x5a6e1c | WriteAmtTable | VERIFIED-1:1 | count `!=-1`, stride 276; +0(4)+8(1)+20(4)+38(1)+48(1)+52(0x40,cnt16)+116(1)+124(0x98)+24(0xE)+12(4) |
| 0x5ab3ac | LoadAmtTable | VERIFIED-1:1 | +24 14-byte gametime read only `>=0x1003F` else default; +12 read only `>=0x10040` else -1 |
| 0x5a6324 | WriteGameGlobals | VERIFIED-1:1 | 26-field prefix incl qword_1235262 split 4/2/4; matrix 16x5 stride 20; 5x0x44 blocks; grid 8x8 stride 24 rec +0(2)+2(2)+4(1)+8(4) |
| 0x5aa8dc | LoadGameGlobals | VERIFIED-1:1 | byte-identical to writer |

### Constants confirmed (save_tables.h)
- kMapTileStride 67, kMapTileScanBytes 548864 (67*8192) ✓
- kHotkeyCount 24 ✓
- kMapRowStride 768 ✓
- kCartCount 4, kCartStride 68, kCartEntryCount 8, kHistoryFlagsLen 16 ✓
- kAmtStride 276 (69 dwords), kAmtScanBytes 6624 ✓
- Version gates 0x1003A/0x10042/0x10036/0x1003F/0x10040 all UNSIGNED (`(unsigned int)dword_649D4C` cmp) ✓

## save_world_tables_save_recon.cpp — VERIFIED-1:1

| Addr | Function | Verdict |
|------|----------|---------|
| 0x5a57f4 | WriteCityAndPersonTables | VERIFIED-1:1 |

Diffed against decompile incl. the disasm-recovered staging/copy-path region:
- PERSON: stride 536 (134 dwords), span 411648 (102912 dwords); count nonzero ptr;
  WriteObjectRecord(personPtr[i], personLink[i]=dword_12CE914 at same index). ✓
- CITY: index stride 67 span 548864; record stride 65, live tag 29 at record[0];
  recPtr at idx+0x3B, link at idx+0x02. ✓
- CHAR slab: stride 404 span 517120.
  - PRE-PASS: live (`+9!=0`): +0x24/+0x28 ptr→index via `(ptr-base)/404` or -1; dead→memset 0. ✓
  - WRITE LOOP: leading byte (=rec[9] on copy-path else 0), then +12(4) +20(4) +48(32)
    +80(32) +240(160) +400(1) +36(4) +40(4) — **field order matches exactly**. ✓
  - POST-PASS: index→offset `index*404` or 0 when -1. ✓
- Constants (header) all confirmed: 0x218/0x64800/0x43/0x86000/0x41/0x1D/0x194/0x7E400. ✓

### Documented boundaries (cross-module; intentionally modeled, not approximated)
1. `WriteObjectRecord` @0x5a55b0 — routed through `WriteObjectRecordFn` hook (lives in
   save_serial3). On-disk record body & count/order ARE reconstructed; the per-object
   emit is the hooked callee. (rule-13 wiring point.)
2. CHAR slab `+0x14` handle slot: original PRE-PASS does `slot=*(DWORD*)slot` (one deref)
   and POST-PASS does `slot=handleTable[slot]` (relookup). The id-model reconstruction
   treats +0x14 as the handle index directly (identity); on-disk bytes are byte-exact.
   Documented in header "Pointer-slot width caveat".
3. Copy-path probe `*(*(handle+0x34)+0x200)!=0` — routed through `CharProbeHook`.
4. `VIBE_GameTime_Advance` @0x583150 — in LoadAmtTable old-version (`<0x1003F`) else-branch
   the original calls GameTime_Advance(+24, count+1, 0, 0) after stamping the default
   gametime block. The reconstruction stamps the 14-byte default (qword_13CE852 8B +
   unk_13CE85A 4B + unk_13CE85E 2B) but does NOT invoke GameTime_Advance — that is a
   cross-module side-effect outside this serializer's scope. Caller must supply the
   assembled 14-byte default. (Stream bytes unaffected; in-memory side effect deferred.)
5. `VIBE_History_LoadChronicleText` @0x4fced0 — LoadHistoryAndCarts calls it after the
   flag byte; cross-module callee, not part of the stream layout. Out of scope here.
6. `ConvertX` @0x5c6b08 — NOT referenced by either target file (these are pure byte-copy
   serializers, no float→int sites). Out of scope.

## No source/golden fixes required
Every diffed function was already byte-faithful; no discrepancies found in field order,
strides, version bounds (all unsigned), tile dimensions (768x768), or the +24 gametime
block layout. No edits made to source or goldens.

## Test status (GUILD_GAME_DIR set)
- io_save_tables_test: the suite's checks for the 12 in-scope functions PASS. The single
  failing check is `building_record_old_version_gates` (line 366) which exercises
  `SaveWriteBuildingRecord`/`SaveLoadBuildingRecord` in `src/io/save_building.cpp` —
  OUT OF EDIT SCOPE (a different module). Pre-existing, not touched.
- save_world_tables_save_recon_test: PASS
- world_io_save_recon_test: PASS
- io_save_tables_e2e_test: PASS

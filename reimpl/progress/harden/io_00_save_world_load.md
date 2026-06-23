# Harden: src/io/save_world_load.cpp — world-load driver + table loaders

Module: `guild::io` full-scenario world load (the `.cty`/`.SAV` load pipeline).
Method: every `gilde.exe 0xADDR` function decompiled AND disassembled, diffed
line-for-line against the source. DISASM used to confirm strides, offsets and
version-gate compares.

## Verdict: VERIFIED-1:1 (serialization + driver order + relink column slice).

No source or golden-vector edits were required — the file is already binary-faithful.
Tests: `io_save_world_test`, `world_city_census_harden_test`,
`world_city_load_harden_test`, `io_save_world_real_e2e_test` — all PASS (4/4).

---

## Per-function verification

### VIBE_Save_LoadPersonIndexTable @0x5a7ffc — VERIFIED-1:1
- `count` read (4), `v6 > 0` SIGNED compare → loop. Stride 67 confirmed (`v3 += 67`).
- Per-record reads: +0(2) +2(4) +6(4) +10(4) +14(4) +18(1) +19(1) +28(0x1F). Exact match.
- `dword_6498C0 = count`. Source `kSceneTileStride=67` / `countOut`. Match.
- Boundary: original head calls `VIBE_World_RelinkObjectOwners(a2)` (clears the scene
  table); the portable loader takes a pre-zeroed `tileBase` (driver memsets). Documented.

### VIBE_Save_LoadGlobalCounters @0x5a86d0 — VERIFIED-1:1
- 16 records (`++v30 >= 16`), 164 stride. Every per-field global (`unk_13C31xx`) mapped
  to its byte offset within the 164-byte record and matched against the source RDf
  offsets:
  - base+0(2), +2(0x10), +0x14(4); `>=0x1002C`: +0x28(4);
  - +0x1C,+0x20,+0x2C,+0x30,+0x34,+0x38,+0x40,+0x44,+0x48,+0x4C,+0x50 (11×4);
  - `>=0x10014`: +0x54,+0x58,+0x5C,+0x3C,+0x60,+0x64,+0x68 (7×4);
  - `>=0x10015`: +0x70(0xE);  +0x80(4), +0x84(0x10), +0x94(0xC), +0xA0(4);
  - `>=0x10018`: +0x6C(4).
  All offsets + version gates + read ORDER identical. Version global `dword_649D4C`.
  Golden vector (`global_counters_roundtrip`) reproduces the exact wire order; 152
  bytes/record at 0x1003B confirmed.

### VIBE_Save_LoadCityRecords @0x5a8d3c — VERIFIED-1:1 (the heart of world load)
- Preamble: word_63CC5C(2), count(4)→dword_647724, dword_6498E8(4), dword_6498EC(4);
  `>=0x10017`: 8× handler ids (loop returns 0 on any short read).
- `count > 0` SIGNED. Per record: leading marker word → `word_12CE910[268*marker]`
  (268 words = 536 bytes = stride). `*v6 = marker`.
- `>=0x1003E`: read +520(4); else `*((DWORD)v6+130)=-1` (byte 520). Match.
- Field reads (all confirmed): +2(1) +4(4) +8(1) +9(1) +10(2) +12(1) +13(1) +16..+36
  (6×4); `>=0x1003B`: +40(2),+44(4); +48(0x10); `>=0x10031`: +64(0x10);
  +80(2),+84(4) then **`+84 dword += 1342`** (`*((DWORD)v6+21)`); +88(1),+92(0x20);
  `>=0x10024`: +124(4); +128(5); +136(0xA8=168); +356..+361 (6×1);
  `+364 = 0` then read +364(4); `+368 = 0` then read +368(4);
  `<0x10020`: +372(2) else +372(4) (the `v6+186` word/dword split — confirmed via
  `v6+186` × 2 = byte 372); `+380 = 0` then read +380(4); +384(1); +396(4) then
  **`+396 dword += 1468`** (`*((DWORD)v6+99)`); +400(4); `<0x1003E`: `*((DWORD)v6+100)=4`
  (byte 400); +404..+488 (large run, exact); `>=0x1002A`: +492(4);
  `>=0x10021`: +453(1) THEN read v10(4) → `*((DWORD)v6+97)=v10` (byte 388, confirmed
  `[edi+184h]=eax` @0x5a9580); `>=0x10021`: +496(0x18=24); `>=0x10036`: +524(4),
  +528..+532 (5×1). The +453-before-v10 ordering and the C-precedence `(A&&B)||C`
  short-circuit were re-derived from disasm; the source's order is correct.
- Biases +1342/+1468 (`kCityCounterBiasA/B`) match. Golden `city_record_roundtrip_1003B`.

### VIBE_Save_LoadBuildingSlotTables @0x5aa058 — VERIFIED-1:1 (serialization)
- 5 city-slot tables (`v26 < 5`), 7952 stride: header +0/+4/+8/+12 (4 each), then 62
  sub-records (`v3 < 62`, +128 stride): +0(2) +4(4) +8(4) +12(4) +32(4) +36(4) +48(4)
  +52(4) +56(4) +60(2) +44(4). Exact.
- 4 city-info records (`v27 < 4`), 756 stride: +0(0x20) +64(8) +72(1) +76(4) +80(4)
  +84(2) +88(8) +96(1) +97(1); +100×10×8; +180×10×8; +260(1)+261(1); +264×4×4;
  +280×4×4; +296(4); +300×11×1; +311(1); +312×7×1; +319/320/321/322(1); +324×8×18(0x12);
  +468(4) +472(4) +476(0xD0=208); `>=0x10037`: +748(8). Exact match to the source's
  `LoadCitySlotTable` / `LoadCityInfoRecord`.
- BOUNDARY (documented in source): after the 4th city-info record the original runs
  a trailing localized-name lookup — `Sprintf("_STADTAUSWAHL_%s_INFO+0", byte_13CD6A0)`
  → `StrToUpper` → `VIBE_Text_FindTextArrayIndex` → copies the display name into
  `unk_13CD6C0`. This is the text/UI subsystem and is correctly OMITTED (no bytes read
  from the save stream, so serialization stays byte-identical).

### VIBE_Save_LoadCharacterSlot @0x5a96c0 — VERIFIED-1:1 (serialization); BOUNDARY: alloc
- index(4) → `VIBE_Character_AllocSlotAtIndex(index)` (BOUNDARY; source takes `rec`).
- First field read size confirmed via disasm = **0x20** (`mov edx,20h` @0x5a96f4), into
  rec+5. Then +44(4) +48(4) +140(2) +368(0x30) +304(0x40) +56(0x10) +72(0xC), then a
  hidden flag byte (`if ==1: rec[140] |= 0x20`), then personId(4)→ byte 300
  (`*((DWORD)v4+75)`), `>=0x10013`: +424(0x40), then `*((WORD)v4+70) &= 0xDFFB`
  (word @140). The personId read REUSES the index local (`&v7`) in the binary — the
  source uses a separate `personId` var; functionally identical (value captured before
  the +424 read either way). Golden `character_slot_roundtrip_1003B`.

### VIBE_Save_RelinkLoadedPointers @0x5abb84 — column slice VERIFIED-1:1
- GATE (@0x5abb8e): `dword_6498E4 = Person_FindRecordById(dword_6498E4); if(!result)
  return result(0)`. Source `if(!PersonFindRecordById(playerId)) return false`. Match.
  The binary also reassigns the resolved pointer back to dword_6498E4 (not part of the
  portable WorldState — noted boundary).
- Column loop (@0x5abbe2): 768 records, word step 268 (byte 536, total 0x64800),
  marker `word_12CE910[j] != -1`. Offsets confirmed via disasm:
  - +364 (0x16C): `-1 → 0` (@0x5abc06); else `GameObject_ResolveEntityById(v7+91,0,id,0)`.
  - +368 (0x170): `-1 → 0` (@0x5abc1b); else resolve.
  - +380 (0x17C): `-1 → 0` (@0x5abc30); else `He_FindFirstHandlerByFilter(1,1,id)`.
  - +388 (0x184): `= 0` unconditional (@0x5abc3d).
- RESOLVE semantics verified by decompiling `VIBE_GameObject_ResolveEntityById@0x583b44`:
  with `a1` set and `a2=a4=0` it first clears `*a1=0`, then scans the 169-stride OBJECT
  array `dword_13CE298` (256 recs, alive@+0, id@+1), writing the matched record pointer
  on a hit and leaving 0 on a miss. This is byte-for-byte the same scan as
  `VIBE_Building_FindById@0x587b20` (43264/169 = 256, id@+1) — which the source uses to
  gate +364/+368. The pointer-as-id tree convention keeps `id` on a hit, 0 on miss/-1:
  observably identical (clear-to-0 on -1, clear-to-0 on miss, bind on hit). `PersonFindRecordById`
  @0x58bc6c (stride 536, marker@+0, id@+4=dword_12CE914) matches the source's
  `g_persons`/`g_personIds`. Golden `relink_columns_resolve_clear_he_zero`.
- The +380 He link: the partial `.cty` path loads NO He records, so every non-(-1) id
  misses → 0. Reproduced.
- BOUNDARIES (the rest of the FULL relink, out of this slice — documented in source):
  `Person_QueryBegin`→dword_6477A4; the `>=0x10017` dword_6498EC/F0 handler-resolve
  loop; the scene-tile loop @0x5abc5b (`dword_13CE290`, kind-29 special, owner chains);
  the `byte_11D6040` He-actor pass; `dword_11CB620` (41-stride) He resolve; the
  `dword_B5FB66` (10-stride, typed by byte_B5FB61) Person/Object/Building/Cutscene pass;
  `Person_SyncMasterShopObjects`. All require the live He/scene/universe registries.

### VIBE_Save_LoadGameFile @0x5a7604 (driver) — order + gates VERIFIED-1:1
- Version range check @0x5a775a: `dword_13CEC90 > 0x10045 || < 0x10026` → reject.
  Source `version > kSaveVersionLoadMax(0x10045) || version < kSaveVersionLoadMin(0x10026)`.
  `SaveVersionGet()` returns the header magic (= dword_649D4C after header load), so the
  range check is equivalent to the binary's dword_13CEC90 check.
- Scalar block (io/save — reused, BOUNDARY): reads dword_6498E4 (the player id threaded
  to relink as `blk.g6498E4`), confirmed.
- Table ORDER matches exactly: LoadPersonIndexTable(3) → LoadPersonTable(4) →
  LoadGlobalCounters(5) → LoadCityRecords(6) → LoadBuildingSlotTables(7) →
  `>=0x10045`: Amt_LoadAemter(8, deferred) → PostLoadInitScene(9, BOUNDARY/scene stream).
- PARTIAL path `byte_13CEC94 & 2` (the `.cty`/network case): CloseStream, then
  `dword_6498E8 = dword_6498EC = edx` (a decompiler-uninitialized leftover register —
  these display-record globals are clobbered with garbage then re-derived; NOT part of
  WorldState, noted), then `RelinkLoadedPointers`. Source captures the embedded scene
  (LoadWorldEx) and runs `RelinkPersonRecordColumns(blk.g6498E4)`. Match.
- FULL `.SAV` tail (Gesetz/MapTiles/GameGlobals/Avatar/Object/Amt/History/ActionQueues/
  Hotkey + LoadCharacters/Mission) — each a separate module-owned loader, correctly
  enumerated as deferred BOUNDARIES in the source (the shipped cities are partial so the
  tail is never on the city flow). The column relink is run for consistency.

## Tests
`cd build && GUILD_GAME_DIR=$PWD/../europe_guild_1400_original ctest -R 'io_save_world|world_city_load|world_city_census' --output-on-failure`
- io_save_world_test ............ PASS  (strides/constants pinned; roundtrip golden
  vectors for index table / global counters / city records / slot tables / char slot;
  truncation + boundary cases; relink column resolve/clear/He-zero)
- world_city_census_harden_test . PASS
- world_city_load_harden_test ... PASS
- io_save_world_real_e2e_test ... PASS
4/4 passed.

## Constants confirmed against the binary
67 / 8192 / 548864 (scene tiles) · 164 / 16 (global counters) · 536 / 768 / +1342 / +1468
(city records) · 7952 = 16 + 62*128 / 756 / 5 / 4 (slot tables) · 0x204=516 (char slot)
· 169 / 256 / 43264 (object array) · version range [0x10026, 0x10045] · per-field gates
0x1002C 0x10014 0x10015 0x10018 0x10017 0x1003B 0x10031 0x10024 0x10020 0x1003E 0x1002A
0x10021 0x10036 0x10013 0x10037 0x10045 — all verified via disasm `cmp ...; jb/jnb`.

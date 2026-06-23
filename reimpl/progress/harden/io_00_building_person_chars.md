# Harden pass — io: building / person / recon3 characters

Scope: `src/io/save_building.cpp`, `src/io/save_person.cpp`, `src/io/save_recon3_characters.cpp`
(+ `tests/unit/io_save_tables_test.cpp`). Every `gilde.exe 0xADDR` provenance function was
decompiled AND its serialization path diffed line-for-line against the binary.

## Functions audited (decompile + disasm)

| addr | role | result |
|------|------|--------|
| 0x5a45bc | VIBE_Save_WriteBuildingTable | **FIXED** (write gates were wrong) |
| 0x5a86d0 | VIBE_Save_LoadGlobalCounters (building load mirror) | VERIFIED-1:1 |
| 0x5a6854 | VIBE_Save_WriteObjectTable (main/light/marks) | VERIFIED-1:1 (per-record body) |
| 0x5aae40 | VIBE_Save_LoadObjectTable | VERIFIED-1:1 (stream path) |
| 0x5a714c | VIBE_Save_WriteActionQueues | BOUNDARY (multi-pool — see below) |
| 0x5ab704 | VIBE_Save_LoadActionQueues | BOUNDARY (multi-pool — see below) |
| 0x5a4134 | VIBE_Save_WritePersonTable | VERIFIED-1:1 |
| 0x5a8190 | VIBE_Save_LoadPersonTable | VERIFIED-1:1 |
| 0x5a4938 | VIBE_Save_WriteGameStateHeader (scene record) | VERIFIED-1:1 |
| 0x5a986c | VIBE_Save_LoadCharacters | VERIFIED-1:1 (stream path) |
| 0x5a96c0 | VIBE_Save_LoadCharacterSlot | BOUNDARY (lives in save_world_load.cpp) |

## FIX — building writer was mirroring the loader's version gates (deviation)

`SaveWriteBuildingRecord` (0x5a45bc): the source previously gated `+40` (`>=0x1002C`),
`+112` (`>=0x10015`) and `+108` (`>=0x10018`) on the WRITE side, with a comment claiming it
"mirrors the loader's gates." The disassembly proves the writer emits those three fields
**unconditionally** — the ONLY version gate in the writer is the `>=0x10014` seven-dword
group (+84,+88,+92,+60,+96,+100,+104). Evidence: `0x5a45bc` decompile has no `dword_649D4C`
compare except `>= 0x10014`; disasm shows `+0x28`(+40) written right after `+0x14`(+20) with
no branch, and `+0x70`(+112) / `+0x6C`(+108) are emitted with no compare. Only the LOADER
(0x5a86d0) gates +40 (`>=0x1002C`), +112 (`>=0x10015`), +108 (`>=0x10018`).

Fixed `SaveWriteBuildingRecord` to write all four unconditionally save the `>=0x10014` group,
and updated the `building_record_old_version_gates` golden to the binary-accurate behavior:
at a pre-0x10014 write version the writer still emits +40 / +112(0xE) / +108, so the writer
stream size = full record minus only the 28-byte +84 group, and the loader (which gates them
out at that version) reads fewer bytes than were written — the original's latent old-version
write/read asymmetry. The shipping version (0x10045) round-trips cleanly (all gates taken on
both sides); the test now also asserts that clean current-version round-trip.

## VERIFIED-1:1 details

- Building LOAD (0x5a86d0): unk_ globals map exactly to record offsets; field/gate order
  matches `SaveLoadBuildingRecord` byte-for-byte (stride 164, 16 slots).
- Object main (0x5a6854 / 0x5aae40): field order +0(1)+4(4)+8(2)+12(4)+16(4)+20(0x30)+68(0xE)
  +82(0xE)+96(0xE)+112(4)+120(1), 8-dword @+140 group (load gated `>=0x1002F`), var-length
  blob (+124 ptr / +128 len; write len+blob else 0-dword; load zeroes +124/+128 then reads
  +128 len, reads blob iff len), then +172 of header `blobChunk`(==160) bytes. The loader's
  kind-12/13 dispatch reads the SAME +172 chunk in all three branches — identical stream
  consumption; the extra +188 alloc/-1 and +208 zero are engine pointer state (boundary).
- Object light (stride 164): +0(4)+4(1)+8(4)+12(4)+16(4)+20(0xE)+34(0x80). Marks (stride 80):
  +0(1)+4(4)+8(4)+12(4)+16(0x40). Both match.
- Person table (0x5a4134 / 0x5a8190): field order +0(1)+1(4)+5(0x20)+37/39/41(2)+43(4)+47(1)
  +52(1)+53/57/61/65/69(4), `*(r+65)>4 -> 2` (signed), [+73 if `>=0x10028`], [discard dword
  if `<0x10032`], +90(2)+92(1); kind-30 plantmap 64×24 (sub +0/4/8/9/10/16/12/13, +20 zeroed);
  else +101(0x30); +153(0x10 if `>=0x10043` else zero16 | r[153]|=1, r+165=-1); +48=5000,
  +149=-1. Extra-96 table: count then per-rec +0(0x40)+64(0x10)+80(0x10). All match.
- Scene record (0x5a4938 per-record body): full ~90-field order verified including the two
  counter biases — cA = `*(r+84)-1342` (kCounterBiasA), cB = `*(r+396)-1468` (kCounterBiasB) —
  and the four embedded link ids at +364(idAt91)/+368(idAt92)/+380(idAt95)/+388(idAt97).
- LoadCharacters (0x5a986c): two index counts + per-slot relink (slot+300 link, `+75` dwords),
  step1 person / step2 object resolve; slab read loop reads lead->rec[9], +12(4),+20(4),+48(32),
  +80(32),+240(160),+400(1),+36(4),+40(4); slab count 1280 if `>=0x10044` else 768 (unsigned);
  relink iterates full 1280 capacity, handle resolve at +20 (0->zero record), +36/+40 `-1->0`
  else offset, vtable at `*(rec+6) >> 24` (signed sar 24), +12=0. All stream reads match.

## Boundaries (in-file logic verified; cross-file / engine state noted)

- **Action queues (0x5a714c / 0x5ab704):** the binary serializes THREE pools — pool A
  (byte_1078360, 0x2000 records), head pointers dword_11AA498/49C, pool B (unk_BAFB60), a
  +145/+149 pointer<->index transform (`(ptr-base)/0x99`, `-1` sentinel; back `base+153*idx`),
  pool C (byte_B5FB60, 0x8000 × 10 bytes), and trailing globals (11AA46C/494/484/47C,
  631288/28C/290). The source models a SINGLE generic 153-byte pool whose link fields are
  native indices (identity transform) plus the documented `ActionPtrToIndex/IndexToPtr`
  helpers (stride/divisor 153 = 0x99, `-1` sentinel — both VERIFIED correct against the
  binary). The multi-pool wiring, head/trailing globals, and pool B/C depend on globals
  outside the edit scope and are NOT reconstructed here — flagged as a known gap, not faked.
- **LoadCharacterSlot (0x5a96c0):** the sparse 516-byte slot reader is reconstructed in
  io/save_world_load.cpp; the recon3 file defaults to an inert slot reader and exercises the
  real one through `CharLoadHooks.loadSlot`. slot+300 link offset confirmed (`+75` dwords).
- **Object-main writer count:** binary iterates 1024 slots writing only ALIVE records; source
  `SaveWriteObjectMain` writes a caller-compacted `count`-length array (compaction is the
  caller's contract). Live-list side effect (`v19[120]&8 -> dword_11D5A1C`) is in-memory, no
  stream effect.
- **LoadCharacters relink:** owner back-link (`+20[+296]=rec`), avatar/mesh/scene build (step
  3), and the next/prev-pointer diagnostic sprintf branches are engine pointer state / dead
  in normal flow (post-conversion values are never `-1`) — correctly dropped; serialized bytes
  unaffected. The `+36/+40` use the documented offset model (`404*idx`, vs binary `404*idx +
  base`).
- **Signedness note:** several loaders use signed `count > 0` guards (person v11, extra
  dword_1234604, object v17); the source uses `u32` loop bounds. Identical for any non-negative
  count (all writers emit live counts); a negative on-disk count would diverge (corruption-only
  path).
- `VIBE_Light_SetGrayColorThunk(0,n,p)` is treated as a zero-`n`-bytes memset (matches the
  observed person-record / slab clear behavior).

## Test status

Built only the touched targets. `GUILD_GAME_DIR` set; all green:
- io_save_tables_test PASS (incl. rewritten `building_record_old_version_gates`)
- io_save_tables_e2e_test PASS
- save_recon3_characters_test PASS
- sim_building{,_lifecycle,_upgrade}{,_itest,_e2e} PASS
- sim_person{,_lifecycle,_relations,nel}{,_itest,_e2e} + person_create/person_reconcile PASS
- Total: 24/24 across two ctest invocations, 0 failures.

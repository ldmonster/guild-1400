# Save / WorldIo / Table serialization cluster

Reconstruction of the save-game serialization, world-object/building write path and a
fixed-stride lookup table. All new files; no existing files modified.

## Done (reconstructed, tested, wired-ready)

| addr | function | new file | notes |
|------|----------|----------|-------|
| 0x4bad28 | VIBE_Table_FindEntrySlotById | src/world/lookup_table_save_recon.{h,cpp} | pure: 64-entry/stride-67-dword id scan; clears matched key; returns dword-index*4. Quirk (first-slot always cleared) preserved. |
| 0x5e5ab4 | VIBE_WorldIo_WriteObject | src/world/world_io_save_recon.{h,cpp} | full object-type switch (cases 0/1/4/2/3/5-8/default), bit-field extractions, name-pointer/switchVar selection recovered from disasm, recursive child/sibling, event-name tail. |
| 0x5e5f74 | VIBE_WorldIo_WriteBuildingData | src/world/world_io_save_recon.{h,cpp} | name gates, two inventory arrays, per-room loop (stride 344), 8 fixed strings, trailing dwords + vec3. |
| 0x5e61ec | VIBE_WorldIo_WriteObjectCallback | src/world/world_io_save_recon.{h,cpp} | leading 1 byte; severs +496 sibling; calls WriteObject; restores. |
| 0x5a57f4 | VIBE_Save_WriteCityAndPersonTables | src/io/save_world_tables_save_recon.{h,cpp} | person table (768), city-tower table (8192, tag 0x1D), character slab (1280, stride 404): pre/post pointer<->index fixups + the 9-field per-character write with copy/zero path. |

Bio_* write primitives (0x5dc8cc WriteByte / 0x5dc918 WriteDword / 0x5dcac0
WriteDwordPair "writes-only-first" quirk / 0x5dc8ec WriteString / 0x5dc9dc WriteVec3 /
0x5dca40 WriteVec4 / 0x5dcbb0 WriteArray) reproduced 1:1 as static leaves; VIBE_Util_StrChr
(0x5d3ef0) and the byte_64A208 ctype table (&0x20 bit) embedded verbatim.

## Tests
- tests/unit/lookup_table_save_recon_test.cpp (6 cases)
- tests/unit/world_io_save_recon_test.cpp (5 cases: case-0 golden, no-body, callback-severs-sibling, building empty/named)
- tests/unit/save_world_tables_save_recon_test.cpp (5 cases: constants, empty, person order, city tag/index, character copy path)
All pass (16/16, 0 failures) under ASan in an isolated harness.

## Boundary handling
- File I/O (rule 4): reconstructed against byte-buffer cursors (WorldIoSink / SaveTableSink).
  Real build routes the same bytes through VIBE_Vfs_WriteStream.
- 32-bit pointer slots: the original is 32-bit. Intra-record pointer slots
  (child/sibling/sub/part/event-list, slab links +0x24/+0x28, handle +0x14) are modeled
  as 4-byte LINK IDs / byte-offsets resolved through installable hooks (WorldIoSetLinkResolver,
  SaveTableEnv.handleTable, CharProbeHook, WriteObjectRecordFn), avoiding 64-bit host-pointer
  overlap with adjacent serialized fields. Stream bytes are byte-exact. (types.h: serialized
  paths store explicit 32-bit ids, not pointers.)

## Deferred / not reconstructed (reported, not faked — rule 8)
- 0x5a986c VIBE_Save_LoadCharacters — the pure trailing action-record READ loop is the
  byte-mirror of the character-write loop reconstructed in save_world_tables; the bulk of
  the function is subsystem glue (CreateMesh, AttachToUniverseNode, avatar creation,
  texture/transport selection) entangled with the character/object/mesh modules. Deferred:
  the serialization core is covered by the write side; the glue is not a serialization slice.
- 0x55a9f8 VIBE_MapTable_RunCityTowerScene — scene/render orchestration (FindByHandle,
  LoadObjectAnimation, AttachToUniverseNode, RunFrameLoop, DetachAndRelease, float math
  over flt_624928/flt_62492C). No serialization/table core; belongs to the play/scene
  module. Deferred (would require those subsystems to be faithful — rule 8).
- 0x604510 VIBE_EventTable_CreateEvent — uses Win32 CreateEventA (a threading/event
  primitive). NOT a pre-approved tech swap (rules 3-5 cover GPU/window/audio only). Per
  rule 6, requires the user's decision before substituting an SDL/std synchronization
  primitive. Also the Hex-Rays return value (v3) is uninitialized in the decompile. ASK FIRST.

## Already present (ODR — not redefined)
- 0x5a55b0 VIBE_Save_WriteObjectRecord — already in src/io/save_serial3.{h,cpp}
  (SaveWriteObjectRecord). The table writer reaches it through the WriteObjectRecordFn hook;
  wire that to SaveWriteObjectRecord in a real build.

## Wiring notes (xrefs)
- VIBE_Save_WriteCityAndPersonTables <- VIBE_Save_WriteGameFile @0x5a348c (not yet reconstructed).
- VIBE_WorldIo_WriteObject <- WriteObjectCallback @0x5e61ec (wired here), SaveSceneObjects
  @0x5e65b8, Scene_SaveObjectGroup @0x5e860c (not yet reconstructed).
- VIBE_WorldIo_WriteBuildingData <- SaveSceneObjects @0x5e65b8 (not yet reconstructed).
- VIBE_WorldIo_WriteObjectCallback <- SaveSceneState @0x5e6250 (data xref; not yet reconstructed).
- VIBE_Table_FindEntrySlotById <- VIBE_DamageLabel_RegisterEntry @0x4bad5c (not yet reconstructed).
Internal wiring (callback->WriteObject, WriteObject recursion) is complete in-file. External
callers are pending their own reconstruction.

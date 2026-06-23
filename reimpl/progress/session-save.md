# Session save/load — real savegame WRITE path + play-session API

The WRITE side of the savegame/city pipeline (the mirror of `io/save_world_load`)
plus the play-session API: load a world with full side-table capture, save it back
in the original on-disk format, enumerate the save directory the way the original
browser does.

## Recovered original flow (IDA, gilde.exe)

| addr | function | role |
|------|----------|------|
| 0x5a348c | `VIBE_Save_WriteGameFile` | top write driver: open "wb", RelinkPersonObjects, header, scalar block, MapTileTable, PersonTable, BuildingTable, GameStateHeader, BuildingSlotTables, Amt_SaveAemter, RelinkPersonExtraData, then `(flags & 2)` partial gate; else the FULL tail (Gesetz/MapTiles/GameGlobals/Avatar/ObjectTable/AmtTable/History/ActionQueues/Hotkey/CityAndPersonTables/Mission/Hotkey) |
| 0x5a3fa4 | `VIBE_Save_WriteMapTileTable` | already reconstructed (`io/save_tables`) — verified against the decompile |
| 0x5a45bc | `VIBE_Save_WriteBuildingTable` | already reconstructed (`io/save_building`) — verified; gates `>=0x10014` group |
| 0x5a4938 | `VIBE_Save_WriteGameStateHeader` | preamble (marker word, re-counted live count, idA/idB-or-(-1), 8 handler ids) + per-live-slot record; verified against `io/save_person`'s pieces |
| 0x483198 | `VIBE_Amt_SaveAemter` | **NEW** — 37 office holders (`byte_B59848` == `world::g_officeHolders`, stride 24): count word 37, then +0(1) +4(4) +8(1) +12(4) +16(1) +20(4) per record (559 stream bytes) |
| 0x4832d0 | `VIBE_Amt_LoadAemter` | **NEW** — read mirror; count word must equal 37 or return 0 |
| 0x5a7604 | `VIBE_Save_LoadGameFile` | load driver; at version >= 0x10045 `Amt_LoadAemter` is read right after `LoadBuildingSlotTables` (inside the partial prefix); below that it sits in the full tail |
| 0x56a804 | `VIBE_Menu_RunSaveGame` | save screen: writes `"Gamedata/saves/GILDE_SAVEGAME_%i.SAV"` (@0x624f6c, slots 2..15) via WriteGameFile flags=1 (FULL); browser dir `"gamedata/saves"` @0x624f30, display fmt `"Gamedata\Saves\%s.SAV"` @0x624f40 |
| 0x56d984 | `VIBE_Save_DoQuickSave` | `"Gamedata\Saves\Quicksave.SAV"` @0x6252b8, `byte_649D50 = 1`; autosave: `"Saves\Autosave.SAV"` @0x623551 |
| 0x528bd0 | `VIBE_Map_LoadCityFile` | writes `"%s/%s.CTY"` under `"gamedata/cities"` @0x63ccb4 via WriteGameFile **flags=2** — the PARTIAL path; the shipped `.cty` seeds ARE partial saves. `VIBE_Net_LoadAndSyncSession` @0x56da74 uses the same partial path for live session snapshots |
| 0x5a3f14 | `VIBE_Save_RelinkPersonExtraData` | appends the WorldIo scene-object dump (`VIBE_WorldIo_SaveSceneObjects` @0x5e65b8) — the ~0.5–0.6 MB trailing block observed in every shipped `.cty` ("..l:MegaCam" + float vectors). The shipping loader's partial path returns without reading it |

Shipped seed versions (leading magic): AUGSBURG / DRESDEN / HANNOVER / KOELN =
**0x1003B**; BERLIN = **0x1003E**. The writer always stamps **0x10045**
(`kSaveVersionWriter`).

## New files

| file | contents |
|------|----------|
| `src/io/save_world_write.{h,cpp}` | `AmtSaveAemter` @0x483198, `AmtLoadAemter` @0x4832d0, `SaveWriteGameStateSection` @0x5a4938 (composed from the reconstructed save_person pieces), `SaveWriteGameFilePartial` @0x5a348c (the partial-path driver composing the existing per-table writers in the recovered order; parameterized over the table bases via `PartialSaveEnv`) |
| `src/play/session_save.{h,cpp}` | `play::LoadLiveWorld(fs, path, seed)` — the @0x5a7604 partial load with FULL side-table capture (header+thumbnail, scalar block, map tiles, extra-96, counters, person preamble ids, building-slot tables, Aemter at >=0x10045; per-record plantmap pool); `play::SaveLiveWorld(fs, path, saveName, slotTag)` — the live world to the original partial format at 0x10045; `play::EnumerateSaves(fs, out, treeRoot, openDirReal)` — the @0x569530 browser scan over `"gamedata/saves"` + per-file header metadata (name/version/slot, QUICKSAVE/AUTOSAVE slot-0 rule); `play::SessionWorldHash(seed)`; `play::CompareSaveSectionsAgainstCity` — the section-by-section source-fidelity verifier; the recovered save-dir layout strings/paths |

Reused (extern, never redefined): `io/save`, `io/save_person`, `io/save_tables`,
`io/save_building`, `io/save_serial3`, `io/save_world_load`, `io/save_browser`,
`io/vfs`, `io/vfs_tree`, `play/save_roundtrip` (`RoundTripZeroWorld`),
`play/world_digest`, `sim/entity`, `sim/command_apply5` (`g_sysGameTime` ==
`qword_13CE852`), `world/office` (`g_officeHolders` == `byte_B59848`), `crt/rand`.

## Validation results (real assets, `GUILD_GAME_DIR`)

ROUND TRIP, all 5 shipped cities — `LoadLiveWorld(.cty)` → `SaveLiveWorld` →
`LoadLiveWorld(save)`:

| city | objects | persons | tiles | h1==h2 | S1==S2 | save bytes |
|------|---------|---------|-------|--------|--------|------------|
| AUGSBURG | 55 | 1 | 551 | yes | yes | 114537 |
| BERLIN | 45 | 1 | 442 | yes | yes | 107578 |
| DRESDEN | 39 | 1 | 416 | yes | yes | 105412 |
| HANNOVER | 43 | 1 | 432 | yes | yes | 106788 |
| KOELN | 43 | 1 | 425 | yes | yes | 106431 |

SOURCE FIDELITY — re-serialization at the SOURCE version byte-compared per
section against the gunzipped `.cty`: **header-tail / scalars / tiles / counters
/ persons / building-slots all byte-identical for all 5 cities.** Named
exceptions (reported, never faked):

* the 4-byte version word — the original writer always stamps 0x10045;
* the object table — its on-disk field set changed at 0x10032 (discard dword
  removed) and 0x10043 (+153 lightmap block added); like the original writer
  @0x5a4134 (ungated), only the current field set is emitted, so the compare is
  skipped below 0x10043 (round-trip covers it at 0x10045);
* one masked dword per person record at source < 0x1003E: `LoadCityRecords`
  @0x5a8d3c stamps `*(rec+400) = 4` AFTER the read, destroying the on-disk
  value — the real engine itself cannot reproduce that dword on a resave
  (verified: AUGSBURG's source byte there is 0x0A, post-load it is 4);
* the trailing scene-object sidecar (~0.5–0.6 MB per seed, "l:MegaCam"… —
  `VIBE_WorldIo_SaveSceneObjects` @0x5e65b8 appended by RelinkPersonExtraData
  @0x5a3f14): not read by the shipping loader's partial path; its read side
  belongs to the scene/universe module — reported via `trailingBytes`.

## Tests

* `tests/unit/session_save_test.cpp` — suite **SessionSave**, 5 cases, 283 checks, 0 failures:
  `AmtAemter_StreamLayoutAndRoundTrip` (golden bytes, 559-byte stream, count!=37 rejected),
  `SaveThenLoad_HashAndFieldsRoundTrip` (MemFileSystem; hash + field restoration + S1==S2),
  `PlantObject_PlantmapRoundTrip` (kind-30 plantmap through the session pool),
  `EnumerateSaves_BrowserScanAndMetadata` (listDir-capable mem fs; .SAV filter; header metadata; QUICKSAVE slot-0 rule),
  `SaveDirLayoutStrings`.
* `tests/e2e/session_save_e2e_test.cpp` — suite **SessionSaveE2E**, 3 cases, 166 checks, 0 failures (guarded on `GUILD_GAME_DIR`):
  `AllShippedCities_SaveLoadRoundTrip`, `AllShippedCities_SourceSectionFidelity`,
  `EnumerateSaves_RealAndSeededDirs` (real install dir + a seeded temp dir in the
  original layout, written through the real session writer and re-loaded).
* Neighbor suites re-run clean: io_save_test 132, io_save_tables_test 1067,
  io_save_world_test 53, save_roundtrip_test 24, save_serial3_test 27,
  io_save_browser_test 57, save_roundtrip_e2e_test 32, io_save_world_real_e2e_test 1,
  io_save_e2e_test 19 — all 0 failures.

## Deferred / named gaps (rule 8 — not faked)

* **FULL `.SAV` tail** (WriteGameFile flags=1: Gesetz / MapTiles / GameGlobals /
  Avatar / ObjectTable / AmtTable / History / ActionQueues / Hotkeys /
  CityAndPersonTables / Mission / Hotkey @0x5a348c, and the matching load tail
  @0x5a7604): some per-table serializers exist (`io/save_tables`,
  `io/save_building`, `io/save_world_tables_save_recon`) but the live state they
  serialize (avatar, history, action queues, hotkeys, mission) is owned by
  not-yet-portable modules. The session uses the original's PARTIAL path
  (flags=2 — the original's own live-session snapshot format, used for
  `.cty`/`.NET`/network sync). `LoadLiveWorld` REFUSES a non-partial file rather
  than half-loading it.
* **Scene-object sidecar** (`VIBE_WorldIo_SaveSceneObjects` @0x5e65b8 via
  RelinkPersonExtraData @0x5a3f14): write/read deferred with the scene/universe
  module (the per-object write core is already in
  `src/world/world_io_save_recon`); our saves omit it (the shipping loader's
  partial path never reads it).
* **Thumbnail regeneration**: the original grabs the live framebuffer
  (`VIBE_Render_CaptureScreenThumbnail` @0x56d48c) before a save; the session
  writer re-emits the load-time captured thumbnail bytes instead.
* **PostLoadInitScene** @0x5a7ef8 (render/scene refresh on load) — deferred with
  the render modules, as in `io::LoadWorld`.
* **Menu hookup** (`RunLoadGame` in `src/play/native_main_menu.cpp`): the API is
  delivered here; the wiring into the native menu happens in the next wave (the
  menu files are owned by another work stream).

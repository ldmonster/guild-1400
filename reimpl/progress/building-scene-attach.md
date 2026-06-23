# building-scene-attach — city objects → real 3D scene presence

Module: `src/play/building_scene_attach.{h,cpp}` (namespace `guild::play`)
Tests: `tests/unit/building_scene_attach_test.cpp` (suite `BSA`, 19 tests;
170 checks / 0 failures with `GUILD_GAME_DIR` set, 99 checks / 0 failures on
the clean skip path; includes a guarded e2e over the real AUGSBURG city).

## Reconstructed functions (gilde.exe, imagebase 0x400000)

| addr | symbol | where |
|---|---|---|
| 0x50d01c | `VIBE_Building_LoadAndAlignGebaeudeModel` ("lc_GebaeudeAusrichten") | `play::BuildingLoadAndAlignGebaeudeModel` — full 1:1 control flow (837 instructions): duplicate-building gate (type kind==2 → Person query 2/4/city/5/2 + msg 5125), "gb_"+typename (StrToUpper from index 2), the LABEL_9/LABEL_15 `.ogr` variant probe, sp_BAU_WIMPEL planting on every free Bauplatz (transparency 65664, tint 192/128/160, `sonstiges\sp_WIMPEL.baf` anim, nearest-to-viewer scan vs +1e9), the interactive frame loop (preview load on first iter, '2'-key/rotate camera focus via PanelDispatcher(8), nearest-plot reseat (SetPosition + ComputePlacementHeight + SetWorldTranslation), blue 64/64/192 / red 192/64/64 tint state, 65757 pulse w/ +530 bit-2 flip until counter 90.0, cursor-drag via Heightmap raycast when Camera_Update idle, drag-anchor `(0,(maxY-minY)*0.5,maxZ+400)` through MatrixFromEuler(yaw+π), price dialog msg 5122 + ComputeSalePrice on left-click), exits: confirm → `paths[firstRandomPick]` to outPath + plot node returned; cancel → "" + null; release of wimpels/preview. Bug-faithful quirks kept: `v118` first aliases the wimpel pointer; outPath uses the FIRST RandomModulo pick while the preview shows the SECOND; `v117` exit flag never set nonzero; `v116` colour state is an uninitialized stack slot in the original (modeled as 0 — observably identical for any value ∉ {1,2}). |
| 0x50cec0 | `VIBE_Building_ComputePlacementHeight` | `play::BuildingComputePlacementHeight` — out euler = `(0, VectorAngleBetween(+Z, plotHierarchyRot·+Z), 0)` (flt_5CA2B0 = (0,0,1)), wrapped in UniverseSwitchActiveSlot(0,1)/restore. Note: the engine's `VectorAngleBetween` +acos branch wraps by −2π (e.g. a +π/2 plot yields −3π/2 — same orientation mod 2π; golden-tested). |
| 0x50cfd0 | `VIBE_Building_AlignMeshToTerrain` | `play::BuildingAlignMeshToTerrain` — SetPosition; terrain resolve; PointThroughBoneChain(node, node+76); if world-y sank below pos.y → SetPosition again. |
| 0x5e67c8 | `VIBE_WorldIo_ReadObject` (FULL body) | `play::ReadCityObjectRecord` — every stream read with its node-offset destination (see "finding" below), incl. the kind ladder + `VIBE_Object_Spawn @0x5b054c` type rule (`play::SpawnKindForVersion`/`SpawnTypeForKind`), the '!'-prefix strip/re-prefix, the 10-pair anchor block (ver ≥ 0x3A6C00AF), both light-branch forms, child/sibling recursion, STRANGEFUCK placeholder + empty-leaf free, event-binding skip (ver ≥ 0x3A6C00A7). |
| 0x5e7e38 | object-list portion | `play::ParseCityWorld` — header via the REUSED `render::ParseSceneHeader`, then objCount × ReadCityObjectRecord. |
| 0x5e84f4 | `VIBE_Scene_LoadObjectGroup` framing | `play::ParseObjectGroupOgr` — REUSES `io::LoadObjectGroup` with a record-reader adapter. |
| 0x4ffe0c | `VIBE_Object_BuildModelName` | `play::ObjectBuildModelName` — adapter that REUSES `sim::ObjectBuildModelName` (object_lifecycle7) for the body and adds the two link writes the twin leaves to the caller: `*(rec+97) = node` (0x4ffe93) and `*(node+512) = rec` (0x4ffe4e); links stored as indices (repo pointer-as-index model). lc7 hooks (ParseNameAndBind/UniverseRestore) trampolined onto `BuildingSceneHooks`. |
| 0x5a8140 | `VIBE_Object_RebuildModelByOwner` | `play::ObjectRebuildModelByOwner` — the 169-stride × 256 row scan with the original's PER-ITERATION re-read of `*(node+512)` (the in-tree lc9 twin caches it before the loop, which diverges after the first bind overwrites +512; this loop keeps the original semantics and routes binds through the lc7-reusing ObjectBuildModelName). |
| 0x50d01c/0x49c19c shared | `.ogr` variant probe | `play::ResolveGebaeudeOgrVariants` — `"%sgebaeude/*%s.ogr"` then `"_%c"` with `c=(u8)(int)(counter+64.0f)`, float counter, stop at first missing variant or 128.0; hits store the PATTERN into 96-byte slots (max 128 = the 12288-byte frame). |

Constants recovered by `get_bytes`: `unk_621458`="gb_", patterns @0x621474/0x62145c/0x6214e0, anim @0x6214f8, `dbl_621558`=0.5, `flt_621560`=64.0, `flt_621564`=400.0, `flt_621568`=π, `asc_621550`=" ", `byte_621554`="", `flt_5CA2B0`=(0,0,1), transparency 65664/65757, pulse 90.0, frame-loop id 415687, 1e9 best-dist seed.

## THE PLACEMENT-FIELDS FINDING (where city objects get their world transform)

1. **The city scene is EMBEDDED IN THE .cty.** `VIBE_Save_LoadGameFile @0x5a7604`
   reads the save tables, then `VIBE_Save_PostLoadInitScene @0x5a7ef8` hands the
   still-open save stream to `VIBE_Scene_LoadFromStream @0x5e7e38` (edx=stream).
   Verified on the real file: gunzipped AUGSBURG.cty offset 0x1b9c2 carries a
   0x3A6C00BB scene. `Resources/scenes.BIN:Staedte/stadt_<CITY>.ed3` is the SAME
   scene as a template — identical names/positions but ALL owner ids 0.
2. **Record fields → node offsets** (0x5e67c8 disasm):
   - name → node+0; **ownerId (ver≥0x3A6C00B2) → node+512**; class dword
     (ver≥0x3A6C00AB) → node+535; kind → spawn type byte +533;
   - mesh body: lod dword → +532; mesh name → `Mesh_LoadOrFindByName @0x5d345c`
     + `Mesh_AttachStockObjectLods @0x5d1824` (drawdata +492 slot 0);
   - **POSITION vec3 (@0x5e6f87) → node+76/+80/+84**;
     **ROTATION euler vec3 (@0x5e6f99) → node+132/136/140**;
     optional aux pair → +92 / +144; 10 anchor pairs → +156+24i / +168+24i.
   - tail (every type, LABEL_31 @0x5e6a78):
     `SetWorldTranslation @0x5af50c (node, node+132)` → euler stored +132 and
     `MatrixFromEuler @0x5cb1bc` builds the 16-float frame at **node+396**;
     `SetPosition @0x5af38c (node, node+76)` → world pos dwords node[19..21].
3. **Sim records bind BY ID**: `PostLoadInitScene` traverses the scene tree with
   `RebuildModelByOwner @0x5a8140`: alive 169-stride record (`sim::g_objects`,
   type byte +0, **id dword +1**) matches `*(node+512)`; `BuildModelName
   @0x4ffe0c` renames the node `gb_<typeTable[type]+1>` (589-stride table
   dword_13CE294), sets class +535=3 (type 10→0), stores node↔record links
   (rec+97 / node+512), and ParseNameAndBind resolves the model — the .ogr
   group `gebaeude/*<NAME>.ogr` (Resources/Groups.BIN) whose records name the
   .bgf meshes (Resources/Objects.BIN). **Proven on AUGSBURG**: the embedded
   scene's 56 owner-tagged nodes carry ids 1,14,27,34,41,53,… exactly matching
   the alive `g_objects` records.
4. New in-play buildings: 0x50d01c picks a Bauplatz; world pos =
   `PointThroughBoneChain(plot, plot+76) @0x5c8b38`, yaw from 0x50cec0;
   `VIBE_Command_ExCreateGebaeude @0x49c19c` repeats SetPosition(cmd+25)/
   SetWorldTranslation(cmd+41)/AlignMeshToTerrain + BuildModelName.

## Wave-2 integration contract (the 3D city view)

```cpp
io::WorldState world; std::vector<u8> sceneBlob;
io::LoadWorldEx("Resources/gamedata/Cities/AUGSBURG.cty", world, &sceneBlob);
io::ArchiveMount groups;  groups.Mount(fs, "Resources/Groups.BIN", true);
play::GroupsArchiveResolver hooks(&groups);           // real VFS wildcard resolve
play::BuildingSceneEnv env;  env.hooks = &hooks;      // pathPrefix "" for Groups.BIN
play::CityAttachResult R =
    play::AttachCityBuildingScene(sceneBlob.data(), sceneBlob.size(), env);
// R.liveNodes  : one sim::SceneNode3 per scene record, REAL +76 pos / +132 euler /
//                +396 matrix written through the real setters (render-walk ready;
//                play::SceneNodeWorldPlacement decodes them).
// R.attached   : per owner-bound building: objSlot, typeId, gb-name, resolved
//                .ogr member + its .bgf mesh names, pos[3]/euler[3].
```
E2E numbers (real AUGSBURG): 1375 scene records parsed, 796 mesh nodes,
55 alive object records, **53 buildings owner-bound, 49 resolved to real
Groups.BIN `.ogr` members (each parsed for real .bgf mesh names), 53 distinct
world positions**, 372 draw-walk-visible nodes.

## Named gaps (rule 8 — hooks with inert defaults, addresses)

- 0x4ffb40 `VIBE_Object_ParseNameAndBind` (gb_/ob_ name → live model re-bind).
- The 65-stride scene-type table `dword_13CE27C` name source (mode-1 "ob_%s").
- Drawdata (+492) block: destructor install (`*v7 = &HandlerEntry_DestroyIconsAndMesh`),
  AllocDrawData @0x5b107c, the old-version multi-LOD/_s shadow attach state.
- Live leaves routed through `BuildingSceneHooks` (hosts wire real ones):
  0x5e84f4 live VFS open (the 1:1 framing is reused; the wildcard resolve has a
  REAL Groups.BIN-backed default in `GroupsArchiveResolver`), 0x429070
  Mesh_ApplyTransformRecursive, 0x5b2710 ChangeTransparency, 0x428928
  SetVertexColors, 0x426488 Character_LoadObjectAnimation, 0x5c886c/0x5c8218
  Light_*, 0x40dca8 Input_ClearMouseButtonsByMask, 0x5b4258 DetachAndRelease,
  0x427b60 Collision_ResolveMeshAgainstTerrain, 0x4bcdcc Hud banner, 0x5441d0
  MapView_PanelDispatcher, 0x4b5974 Camera_ZoomOut, 0x5c67b8/0x5c65d4
  Heightmap raycast/tile→world, 0x5b5c70 ComputeScreenBounds, 0x4284f4
  ComputeWorldAabb, 0x591480 ComputeSalePrice, 0x586c20/0x586a6c Person query,
  0x4ad6f0 Dialog, 0x59f99c Text message, 0x4c09a0 GameLogic_RunFrameLoop,
  0x5b43f0 Universe_RestoreObjectStates, 0x5b4a24 SwitchActiveSlot, 0x438da8
  ErrorLog (default records to `lastError`).
- 0x4b4c68 `Camera_Update`: default hook routes to the REAL
  `render::Camera_Update` over an owned inert env (no-camera early path → 0);
  hosts with a live camera env override.
- Callers 0x50de7c `VIBE_Building_OpenGebaeudeBauenWindow` / 0x50e784
  `VIBE_Building_OpenStadtBauenWindow` are not yet reconstructed (the
  scene_recon2_orchestrator hook table already names 0x50de7c); call-site
  contract recovered: `al` = building type id, `edx` = out path buffer
  (receives the wildcard pattern), `eax` return = chosen plot node — wired the
  day those windows land.
- In-tree divergence flagged: `sim::ObjectRebuildModelByOwner`
  (object_lifecycle9) caches the +512 owner tag before its loop; the original
  re-reads it per row (observable after the first bind). This module's loop
  keeps the original semantics.

## Tests (`BSA`, tests/unit/building_scene_attach_test.cpp)

19 tests / 170 checks (99 on skip), 0 failures:
ReadRecord_MeshBody_PosEulerOwner, ReadRecord_DummyAndChildSiblingFraming,
ReadRecord_BangPrefixStrip, ParseCityWorld_HeaderPlusObjects, SpawnKindLadders,
ComputePlacementHeight_PlotYaw, AlignMeshToTerrain_RestoresWhenBelow,
BuildModelName_Gb, RebuildModelByOwner_BindsMatchingRecord,
VariantProbe_BaseAndSuffixes, VariantProbe_MissingBaseStillTriesA,
VariantProbe_NoneFound, MainFn_NoOgr_ReturnsNullAndLogs,
MainFn_NoFreePlot_ShowsNoPlotDialog, MainFn_ConfirmPlacesOnNearestPlot,
MainFn_CancelReturnsNullAndEmptyPath, MainFn_DuplicateChurchGateDeclined,
Attach_SyntheticCity_RealPositionsAndLink, E2E_Augsburg_RealCityAttach
(guarded on GUILD_GAME_DIR; skips cleanly when absent).

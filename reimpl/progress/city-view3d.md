# CityView3D — the whole loaded city in real 3D (wave 1)

`src/play/city_view3d.{h,cpp}` — `guild::play::CityView3D`. The session-consumable
real-3D replacement for the top-down synthetic-grid city view
(`real_city_render.*`): every city scene node and every live world object
(`sim::g_objects` after `io::LoadWorld` of a `.cty`) renders at its **real world
position** through the **existing reconstructed universe render chain**, driven by
the **real engine camera model** (eye + rotation euler).

## Where the real positions come from (IDA-verified)

The 169-byte world object record (`g_objects` / `dword_13CE298`) carries **no
position**. The chain recovered from the binary:

* The city geometry is a **scene stream in `.ed3` grammar**. Two sources:
  * `scenes/Staedte/stadt_<CITY>.ed3` (scenes.BIN) — `VIBE_Scene_LoadStadtScene
    @0x500218` → `VIBE_Scene_LoadFromStream @0x5e7e38`. Ships the static city
    (1375 nodes for AUGSBURG) but **no owner links**.
  * **The `.cty` itself embeds the persisted city scene**: at step 9 of the save
    load driver (`VIBE_Save_LoadGameFile @0x5a7604`), `VIBE_Save_PostLoadInitScene
    @0x5a7ef8` hands the **still-open save stream** to `Scene_LoadFromStream`
    (disasm @0x5e7e54: `edx != 0` → use the supplied stream, no path). This blob
    carries every node's world position (+76), rotation euler (+132) **and the
    +512 owner-object id**.
* Node placement: `Bio_ReadVec3` → `VIBE_Object_SetPosition @0x5af38c` (+76) and
  `VIBE_Object_SetWorldTranslation @0x5af50c` (+132 euler → the +396 frame matrix).
* Object↔node bind: `PostLoadInitScene` → `TraverseTree(VIBE_Object_RebuildModelByOwner
  @0x5a8140, mask 448)`: for each node, scan `g_objects`; when `node+512 ==
  record id (+1)` → `VIBE_Object_BuildModelName @0x4ffe0c` attaches
  `"gb_<typeName>"` (the 589-stride type table `dword_13CE294` indexed by the
  record's type byte `+0`) and links `record+97 = node`, `node+512 = record`.

New plumbing for this:

* `io::LoadWorldEx(path, world, embeddedSceneOut)` (additive,
  `src/io/save_world_load.{h,cpp}`; `LoadWorld` is now a thin wrapper) — captures
  the raw embedded scene stream at exactly the PostLoadInitScene position
  (gated to version < 0x10045; the shipped cities are 0x1003B).
  AUGSBURG: 600,493 bytes, tag `0x3A6C00BB`.
* `SceneObjectInst::ownerId` (additive field, `src/play/scene_view.{h,cpp}`) —
  the `.ed3` +512 dword the parser previously discarded.

## The frame (all reconstructed leaves, addresses)

Per `RenderFrame(cam, opt)`:

```
render::RenderMainViewFrame @0x5B6074  (gate + clear)
  clearRect hook  -> SurfaceColorFill (sky)
  sceneWalk hook  -> per instance:
    ComposeModelViewMatrix       view = R^T*(l2w*v + t - eye), R = MatrixFromEuler(-rot)
                                 (VIBE_Object_SetWorldTranslation @0x5af50c type-3 negate
                                  + VIBE_Transform_PointToBoneLocalSpace @0x5c8c40)
    TransformMeshVerticesByMatrix  the record+72 static vertex walk @0x5c953c
    FinalizeVertexShadeLuma        ambient light-cache shade (named gap: per-light accum)
    ComputeVertexClipFlags @0x5ad614  over BuildEngineFrustum (BuildViewMatrix @0x5accd0:
                                 ang = atan2(W/2, scale), ang2 = atan2(H/2, scale),
                                 eps d = 0x360637BD/0xB60637BD — from the DISASM;
                                 the Hex-Rays FPU pairing of the 2nd Atan2 is wrong)
    ProjectObjectVertices @0x5ac970   the genuine 1/z perspective + backface cull,
                                 scalars from SetupViewTransform @0x5af5f8:
                                 xScale=scale(=W/2), xOff=W/2, yScale=-scale, yOff=H/2
                                 (flt_13FCD0C/13FCD18/13FCAF8 = scale*-1.0 [flt_6280E4]/13FCD10)
    WalkSceneTree -> ProcessSceneNodeAppend @0x5ADD1C  (real dispatch, Software keys)
  render::RadixSortDrawList
render::RasterizeMeshList @0x5AEC88
    straddling polys clipped against the REAL six-plane view set (the 13DB398
    plane-table rows: sides 0..3 + near {0,0,1,near} + far {0,0,-1,-far}) and
    re-projected with the SetupViewTransform scalars
    textured spans via RasterizeTexturedTriangleRgbz @0x5F6C30 (real Textures.BIN
    BMP per material), flat/shaded fallback otherwise
PresentToDevice  -> backbuffer blit + present() (MemoryGraphicsDevice headless /
                    VulkanGraphicsDevice on-screen; rule 3/4 swap layer)
```

The camera struct is the engine camera-node state — `CityCamera3D { eye[3] (node
+76), rot[3] (node +132 euler) }` — no invented camera math. `AimCamera` is a host
convenience inverting `render::CameraForward` (unit-tested round-trip).

## Real vs hooked (rule 8)

REAL (default behaviour, no hook installed):
* Placement — the owner-id node match (RebuildModelByOwner @0x5a8140 semantics)
  over the embedded `.cty` scene. AUGSBURG: 56 owner-linked nodes, 53/55 live
  objects bind, 53 distinct world positions.
* Model — the owner node's own shipped mesh (the `.bgf` the `.ed3` node carries),
  resolved through Objects.BIN.
* Everything in the frame above.

HOOKED / named gaps (`CityView3DHooks`):
* `resolveObjectModel` — the engine's `gb_<typeName>` model attach
  (`VIBE_Object_BuildModelName @0x4ffe0c` /
  `VIBE_Building_LoadAndAlignGebaeudeModel @0x50d01c`) is a **parallel agent's
  module** — wave 2 binds it here. 3 of 53 bound AUGSBURG objects currently have
  no node mesh (`modelUnresolved()` counts them; they are placed but not drawn).
* `resolveObjectPlacement` — override point (default is the real match above);
  2 AUGSBURG objects have no owner node (`unplacedObjects()`).
* Per-light vertex shade — vertices carry the ambient light-cache seed
  (`FinalizeVertexShadeLuma`); per-light accumulation is the same named boundary
  the universe driver documents.
* `CullNodeAgainstFrustum` per-node byte = 0 (not culled); per-polygon clip/cull
  is fully real (ComputeVertexClipFlags + ProjectObjectVertices).
* Terrain: the city ground is the heightmap mesh (`VIBE_Heightmap_BuildTerrainMesh
  @0x5c5610` over the "boden" node), a separate subsystem — not drawn here.
* Draw order: the Software append path keys by texture (the engine's software
  sort, `NodeAppendMode::Software`); the Hardware depth-key variant of
  `ProcessSceneNodeAppend` is available if wave 2 prefers depth ordering.
* near/far/viewScale: runtime gfx-config values in the original (13ECEBC/13ECEC0
  block); `Options` defaults near=10, far=scene fog far (ConfigureFog @0x5ae384
  behaviour; 6900 for AUGSBURG), viewScale = W/2 (the engine value).

## Wave-2 integration contract

```cpp
// boot (once per city load):
play::CityView3D view;
view.Init(&fs);                          // scenes.BIN + Objects.BIN + Textures.BIN
io::WorldState world{};
std::vector<u8> sceneBlob;
io::LoadWorldEx(ctyPath, world, &sceneBlob);
view.LoadCityFromWorld(sceneBlob);       // REAL placements + owner ids
                                          // (view.LoadCity("AUGSBURG") = scenes.BIN fallback)
view.SetHooks({ /* wave 2: bind the gb_ model loader */ });
view.BindWorldObjects();                 // live g_objects -> nodes

// per frame (the session loop):
play::CityCamera3D cam;
cam.eye = { sessionCam.eyeX(), sessionCam.eyeHeight(), sessionCam.eyeZ() }; // node +76
cam.rot = { sessionCam.pitch(), sessionCam.yaw(), 0 };                      // node +132
play::CityView3D::Options opt;            // fbW/fbH = session framebuffer
view.RenderFrame(cam, opt);
view.PresentToDevice(device);             // Memory/Vulkan IGraphicsDevice
```

`SessionCamera` exposes eyeX/eyeZ/zoom today; the wave-2 rotation/eye-height
accessors plug straight into the struct (it is the camera node's +76/+132 state,
which the reconstructed camera cluster owns). `view.OverviewCamera()` gives a
bounds-framed default seat.

## Files

* `src/play/city_view3d.{h,cpp}` — new module.
* `src/io/save_world_load.{h,cpp}` — additive `LoadWorldEx` (embedded scene capture).
* `src/play/scene_view.{h,cpp}` — additive `SceneObjectInst::ownerId` (+512).
* `tests/unit/city_view3d_test.cpp`, `tests/e2e/city_view3d_e2e_test.cpp`.

## Tests

* Unit `city_view3d_test` — 7 tests, **52 checks, 0 failures** (asset-free):
  frustum constants vs the binary dwords, clip-classify drive through the real
  0x5ad614 leaf, AimCamera↔CameraForward/WorldToView round trip, model→view
  compose vs the 0x5c953c walk arithmetic, camera-rotation matrix sensitivity,
  hook/named-gap bind counts over a synthetic live world, clean no-asset failure.
* E2E `city_view3d_e2e_test` (guarded on `GUILD_GAME_DIR`) — 4 tests, **52
  checks, 0 failures** over real AUGSBURG: embedded-scene capture (600 KB, tag
  0x3A6C00BB, 56 owner nodes), 53 objects bound at 53 distinct real positions,
  whole-city frame (796 instances, 34,829 polys in, 13,852 rasterized, 7,743 via
  the real textured span, 1,026 distinct colours), byte-identical determinism,
  camera yaw/pitch/eye-move/eye-descend each change >5,000 pixels, stadt_*.ed3
  fallback render, headless present blit. BMP artifact:
  `/tmp/guild_cityview3d_augsburg.bmp`.

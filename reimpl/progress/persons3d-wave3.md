# Persons in the 3D city view (wave 3, agent W3-B)

Status: **landed** (2026-06-11). Live `sim::g_persons` rows appear in the REAL
3D city view (`play::CityView3D`) as real posed character meshes, the way the
original does it — a person is a character OBJECT in the same universe/scene
graph the buildings render through, seated at its anchor building's entrance
dummy, created through the REAL factory chain and posed through the REAL pose
driver. Additive: a view that never calls `BindPersons()` renders **byte-
identically** to before (pinned by the new e2e and the untouched
`city_view3d` / `universe_render` suites).

Modules:
* `src/play/city_view3d.{h,cpp}` — person binding/draw pass (additive API).
* `src/play/session_persons3d.{h,cpp}` — **NEW**: the ONE session entry point.
* `tests/unit/session_persons3d_test.cpp`, `tests/e2e/session_persons3d_e2e_test.cpp`.

## Where a person's WORLD POSITION comes from (the evidence)

The original's only live person placement path:

```
VIBE_Character_SpawnAtBuildingEntrance @0x57c8f0
  person = VIBE_Person_FindRecordById(id)            // g_persons, 536-stride
  position = the BUILDING's entrance dummy node:
     "dummy_EINGANG" (queue/entrance tag, 0x57ca38) else "dummy_TUER" (0x57ca54)
     resolved by VIBE_Object_FindByHandle @0x5b7be4 (subtree walk, StrCmpNoCase)
     world point = VIBE_Transform_PointThroughBoneChain @0x5c8b38 over node +76
  no-dummy fallback (evidence): VIBE_Object_IsNearDoorAlt @0x4b0ee8 —
     FindByHandle(buildingNode, 256, "dummy_TUER") else
     PointThroughBoneChain(buildingNode, buildingNode+76)
  default spawn rotation: dword_577A78 (all zero; SpawnOfficeStaffActor @0x57c744
     pushes SetWorldTranslation only when its caller passes a rotation)
```

The person record itself carries **no world position** — but it does carry the
building columns the daily director dispatches against (`sim/npc_daily.h`,
IDA-grounded):

| offset | column | meaning |
|---|---|---|
| +0x16C | dword_12CEA7C | **homeBld** — home/work building (0 = absent) |
| +0x170 | dword_12CEA80 | **workBld** — work/production building (0 = absent) |
| +0x184 | dword_12CEA94 | destBld / live char ptr (the spawn writes the char here) |

Verified on the real data: the embedded AUGSBURG `.cty` city scene carries
**54 `dummy_TUER` nodes** (one per building subtree; the 53 owner-bound
buildings all resolve one in the e2e — 6/6 test persons seat on real door
dummies). `dummy_EINGANG` lives in the `gb_` building MODEL subtree (the
parallel model-attach module), so in the city scene the TUER tag is the live
hit; the resolver still prefers EINGANG when a host attaches those subtrees.

**Default placement** (`CityView3D::DefaultPersonPlacement`): anchor =
`resolvePersonAnchor` hook, else `+0x16C` then `+0x170` (as a building id, the
repo pointer-as-id model) → the building's owner-matched scene node
(`ownerToNode_`, the RebuildModelByOwner @0x5a8140 match BindWorldObjects
already uses) → `play::FindEntranceDummyNode` (pre-order subtree,
case-insensitive, EINGANG > TUER) → the node's COMPOSED world position
(the PointThroughBoneChain parent-chain composition `scenePos_` carries) →
euler zero (the dword_577A78 default). **No anchor ⇒ NOT placed** (counted in
`personsUnplaced()`; never an invented position — rule 8).

## The chain wired (addresses)

1. Gate: `sim::PersonIsValidActiveRecord` — VIBE_Person_IsValidActiveRecord
   @0x4f8e60 (marker != -1, +8 live byte != 0, kind < 10).
2. Model: `play::ResolveStaffModel` — VIBE_Office_ResolveStaffModel @0x57c1e8
   (the 1:1 person_render reconstruction; hookable).
3. Create: `sim::CreateFromModel` — VIBE_Character_CreateFromModel @0x402d10 →
   AllocSlot @0x402254 (the real `g_live` table) → CreateMesh @0x4029c4 (raw
   field writes, DecomposeModelName @0x402a4c → `rec+304` base, node flag
   writes into a real node-sized image, the genuine preload set capture).
   The mesh attach hook resolves Mesh_LoadOrFindByName @0x5d345c semantics:
   `"_DYNAMIC/Character/<model>.bgf"` case-insensitively out of Objects.BIN.
4. Pose: the factory preload set ({"bewegung/gehen","stehen/stehen_newnoise"},
   PreloadAniSet @0x403c34 — captured from the factory, idle played first as
   the post-spawn playback) through `play::PersonCharacterPose`:
   `render::LoadAnimation` @0x5e450c → **`render::UpdateSkeletonPose`
   @0x5cd1d8** → `SamplePosedMeshSeg` (@0x5c9394 + @0x5ca2fc) →
   `CalculateClipNormals` @0x5d0020 + `RelightPosedFrame` per frame.
5. Draw: persons enter the SAME frame as the city — ComposeModelViewMatrix
   (record+72), TransformMeshVerticesByMatrix @0x5c953c, ComputeVertexClipFlags
   @0x5ad614, ProjectObjectVertices @0x5ac970, WalkSceneTree →
   ProcessSceneNodeAppend @0x5ADD1C, RadixSortDrawList, RasterizeMeshList
   @0x5AEC88. Person textures: the shipped character BMPs
   (`_DYNAMIC/Character/*.bmp`) are **24-bit** (no palette), so the person bind
   (`bindFor(member, rgbAffineFallback=true)`) carries the decoded RGB BMP and
   the span samples it through the in-tree affine kernel
   `play::RasterTexturedTriangleAffine` — the SAME path the legacy person pass
   rasterizes these BMPs; city binds are untouched (byte-identical frames).
6. Draw order: persons are APPENDED FIRST (the flush paints the sorted list
   back-to-front by index, so first-appended paints on top). The Software
   append keys by texture-record id — a named gap in this view (no live
   texture records), so ordering among the key-0 entries is append order; this
   is the documented host ordering of that same gap.

## THE SESSION CALL CONTRACT (`src/play/session_persons3d.h`)

```cpp
// boot — AFTER view.Init(&fs) + view.LoadCityFromWorld(sceneBlob)
//        + view.BindWorldObjects():
play::SessionPersons3DOptions po;                 // defaults are fine
po.anchorProvider = ...;                          // OPTIONAL person->building id
play::SessionPersons3DStatus st = play::WireSessionPersons3D(view, &fs, po);

// per frame — BEFORE view.RenderFrame(cam, opt):
play::UpdateSessionPersons3D(view, po.animStepPerFrame);

// after sim person/anchor changes (new-game commit, day tick):
play::RebindSessionPersons3D(view, po);

// teardown (optional):
play::UnwireSessionPersons3D(view);
```

Signatures:
```cpp
SessionPersons3DStatus WireSessionPersons3D(CityView3D& view,
                                            shim::IFileSystem* fs,
                                            const SessionPersons3DOptions& opt = {});
int  UpdateSessionPersons3D(CityView3D& view, float stepTicks);
SessionPersons3DStatus RebindSessionPersons3D(CityView3D& view,
                                              const SessionPersons3DOptions& opt = {});
void UnwireSessionPersons3D(CityView3D& view);
```
`SessionPersons3DOptions`: `maxPersons` (0=all), `mountAnims`/`animsArchive`,
`animStepPerFrame` (default 8.0), and the three optional hooks
(`anchorProvider`, `placementOverride`, `modelOverride`). Wire/Unwire PRESERVE
the building/object hooks the session installed (only the person slots are
set/cleared). `RenderFrame` reports `Result::personInstances / personPosed /
personRestPose`; `view.boundPersons()` is the roster (id, slot, anchor
building, dummy node, world seat, model, member, posed).

## Named gaps (rule 8 — never faked)

* **person → building anchor**: the original's spawn receives the building from
  its CALLERS (VIBE_NpcAction_DailyRoutineStep @0x4e7e88, the command/combat
  clusters); no single reconstructed Person column drives the spawn. Default =
  the +0x16C/+0x170 columns (above); nothing in-tree populates them yet, so
  with no anchorProvider and untouched records, persons stay unplaced
  (counted) rather than invented.
* **live walking positions**: `wire_npc_movement` keeps a moving entity's
  CURRENT TILE at record +0x6C/+0x70 (state +0x74); tile→world needs the
  terrain heightmap (`render::TileToWorld` @0x5c65d4), which CityView3D does
  not own. A session with the heightmap wired supplies `placementOverride`
  from `GetEntityMovePos` + `TileToWorld` (precise handoff; no edit to
  wire_npc_movement/render needed).
* **clip selection** (what a live person plays right now) is the
  NpcAction/charaction runtime; the pose plays the factory preload set.
* **spawn rotation**: zero (dword_577A78); persons render identity-oriented.
* VIBE_Character_ApplyHeadVariant @0x57c548 / ResolveHeadBone @0x57c5d4 and
  VIBE_Object_SelectTextureSet @0x5b3f54 — not reconstructed.
* The factory node image: CreateMesh's raw node writes (+529..+536, +72) land
  in a per-person node buffer; the +492 sub-object stays null (the original's
  own null-sub-object path skips the 1.8f anim-rate write). The live
  scene-node integration (AttachToUniverseNode @0x5b3e30 proper) is the
  universe runtime.
* Unbind teardown = the VIBE_Character_Destroy @0x402120 essentials (clear the
  g_live slot + free, per the factory header); the full Destroy is its own
  reconstruction target.

## Handoff note (pre-existing failures, NOT this module)

The wave-3 W3-A render change (`src/render/raster.cpp` —
`FillTexturedSpansShaded` now refuses `bpp != 8` surfaces, the pink-polygon
fidelity fix) makes the LEGACY flat/shaded path paint nothing on 16bpp
surfaces. The legacy-renderer suites fail on that (verified independent of
this module's diff): `object_mesh_render_{itest,e2e}`, `world_render_{itest,
e2e}`, `real_city_render_{itest,e2e}`, `session_persons_render_e2e`,
`full_session_{itest,e2e}`, `multi_city_{itest,e2e}`, `playable_slice_{itest,
e2e}`, `object_transform_e2e`, `atmos_lighting_itest` — all "non-clear pixels
= 0" in the legacy 16bpp flat raster. W3-A / the orchestrator owns reconciling
those suites with the new guard. CityView3D is unaffected (its city paint is
the real palettized Rgbz textured path; its person paint adds the RGB affine
bind).

## Tests

* `tests/unit/session_persons3d_test.cpp` — suite `SessionPersons3D`,
  **5 tests, 50 checks, 0 failures** (asset-free): entrance-dummy pre-order
  subtree resolution (EINGANG>TUER, case-insensitive, miss/-1), homeBld anchor
  → composed dummy world seat + the REAL resolver row (golden `dieb_MANN2`) +
  named-gap counters, workBld fallback + building-node seat (0x4b0ee8), the
  0x4f8e60 record gate + maxPersons cap, the full Wire/Update/Rebind/Unwire
  contract (hook preservation, anchorProvider, placementOverride, clean
  unwire).
* `tests/e2e/session_persons3d_e2e_test.cpp` — suite `SessionPersons3DE2E`,
  **2 tests, 69 checks, 0 failures** (guarded; honors `GUILD_GAME_DIR`):
  - `PersonsRenderAtBuildingDoorsInAugsburg`: real AUGSBURG LoadWorldEx +
    CityView3D, 6 persons created through the REAL factory
    (Person_CreateAndSpawn @0x58da70) anchored via +0x16C to real buildings —
    6/6 bound, 6/6 posed, **6/6 seated on real `dummy_TUER` nodes**, all
    models shipped (`modelUnresolved 0`); persons frame: 6 instances, raster
    15302 vs 13852 baseline; zoomed persons-on/off pixel delta **8666**;
    determinism (rebind + same step ⇒ byte-identical, delta 0); pose advance
    (the real 0x5cd1d8 ladder) moves **2836** pixels; artifact dumped.
  - `NoPersonsBoundKeepsFrameByteIdentical`: no BindPersons ⇒ frames render
    with `personInstances 0` and re-render byte-identically.
* Untouched suites green: `city_view3d_test` 52, `city_view3d_e2e_test` 52,
  `universe_render_e2e_test` 22, `render_skeleton*` and `person_model_resolve`
  suites all pass. Full battery: **1401 tests, all green except the 15
  pre-existing W3-A legacy-raster failures listed in the handoff note** (none
  reference this module; verified by stashing this module's diff).

Artifact: `/tmp/guild_w3b_persons.ppm` (320x240, the textured `dieb_MANN2` at
a real building door in AUGSBURG).

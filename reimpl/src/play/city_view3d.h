#pragma once
// =============================================================================
// guild::play — CityView3D: the WHOLE loaded city in REAL 3D through the
// reconstructed UNIVERSE render chain, with the REAL engine camera model.
//
// Today the city session renders top-down with a synthetic grid placement
// (real_city_render.*). This module is the real-3D replacement view: every city
// scene node and every live world object (sim::g_objects, populated by
// io::LoadWorld from a .cty) is rendered AT ITS REAL WORLD POSITION through the
// genuine engine frame spine:
//
//   render::RenderMainViewFrame   @0x5B6074   (gate + clear)
//     -> render::RenderUniverseFrame @0x5B3DE8 -> BeginUniverseFrame @0x5B3900
//        clearRect hook -> SurfaceColorFill (sky)
//        sceneWalk hook -> per instance:
//          model->view transform   render::TransformMeshVerticesByMatrix
//                                  (VIBE_Mesh_InterpolateMorphVertices @0x5c953c,
//                                   static walk over the composed record+72 matrix)
//          clip classify           render::ComputeVertexClipFlags @0x5ad614
//                                  (the engine frustum built 1:1 from
//                                   VIBE_Render_BuildViewMatrix @0x5accd0)
//          perspective project     render::ProjectObjectVertices @0x5ac970
//                                  (the genuine 1/z universe projection, with the
//                                   VIBE_Render_SetupViewTransform @0x5af5f8
//                                   scalars: xScale=scale, xOff=W/2, yScale=-scale,
//                                   yOff=H/2; flt_13FCD0C/13FCD18/13FCAF8/13FCD10)
//          draw-list append        play::WalkSceneTree -> the REAL per-node
//                                  dispatch render::ProcessSceneNodeAppend @0x5ADD1C
//        render::RadixSortDrawList
//     -> render::RasterizeMeshList @0x5AEC88, textured spans via
//        render::RasterizeTexturedTriangleRgbz @0x5F6C30 (real Textures.BIN BMPs)
//     -> PresentToDevice (MemoryGraphicsDevice headless / Vulkan on-screen).
//
// WHERE THE REAL POSITIONS COME FROM (verified in IDA)
// ---------------------------------------------------------------------------
// The 169-byte world object record (g_objects / dword_13CE298) carries NO world
// position. The city geometry lives in the city SCENE `scenes/Staedte/
// stadt_<CITY>.ed3` (VIBE_Scene_LoadStadtScene @0x500218 -> Scene_LoadFromStream
// @0x5e7e38): each scene node is spawned with its world position (+76) and euler
// (+132) read straight from the file (Bio_ReadVec3 -> SetPosition @0x5af38c /
// SetWorldTranslation @0x5af50c). After the .cty tables load, the engine REBINDS
// the live object records onto those nodes by id: VIBE_Save_PostLoadInitScene
// @0x5a7ef8 traverses the scene graph with VIBE_Object_RebuildModelByOwner
// @0x5a8140, matching node+512 (the .ed3 ownerId field, ver >= 0x3A6C00B2) ==
// objectRec id (+1), then VIBE_Object_BuildModelName @0x4ffe0c attaches the
// "gb_<typeName>" model and links record+97 = node / node+512 = record. So an
// object's REAL placement IS its owner-matched scene node's placement — exactly
// what BindWorldObjects() reproduces.
//
// WHAT IS HOOKED (rule 8, named gaps with REAL defaults)
// ---------------------------------------------------------------------------
//  * resolveObjectModel — the engine resolves a bound object's model as
//    "gb_<typeName>" through the 589-stride type-name table (dword_13CE294) and
//    the gebaeude .ogr group loader (VIBE_Object_BuildModelName @0x4ffe0c /
//    VIBE_Building_LoadAndAlignGebaeudeModel @0x50d01c — reconstructed by a
//    PARALLEL agent; NOT duplicated here). REAL default: draw the owner-matched
//    scene node's own shipped mesh (the .ed3 node's .bgf), which is the real
//    on-disk model at the real position; wave 2 binds the gb_ loader hook.
//  * resolveObjectPlacement — REAL default: the owner-id scene-node match above.
//  * Per-light vertex shade — like UniverseFrameDriver, vertices carry the
//    ambient light-cache shade (render::FinalizeVertexShadeLuma over the 200
//    seed); the per-light accumulation is the same named boundary.
//  * CullNodeAgainstFrustum per-node byte: 0 (not culled) — per-polygon culling
//    is fully real via ComputeVertexClipFlags + ProjectObjectVertices.
//
// THE CAMERA (the engine way — no invented math)
// ---------------------------------------------------------------------------
// CityCamera3D is the engine camera NODE state: eye = node +76/+80/+84, rot =
// node +132/+136/+140 euler. For a type-3 camera node the engine builds the view
// basis R = MatrixFromEuler(-rot) (VIBE_Object_SetWorldTranslation @0x5af50c
// negates the euler) and views a world point as view = R^T * (world - eye)
// (VIBE_Transform_PointToBoneLocalSpace @0x5c8c40) — the same MegaCam math the
// choosecity scene uses. ComposeModelViewMatrix() composes that with each
// instance's local->world transform into the 16-float record+72 matrix the
// vertex walk applies. Wave 2 feeds eye/rot from the live SessionCamera.
// =============================================================================
//
// PERSONS IN THE 3D VIEW (wave 3 — additive; nothing changes until BindPersons)
// ---------------------------------------------------------------------------
// The original puts a live person into the city as a CHARACTER OBJECT in the
// same universe/scene graph the buildings render through:
//
//   VIBE_Character_SpawnAtBuildingEntrance @0x57c8f0
//     person = VIBE_Person_FindRecordById(id)        (g_persons, 536-stride)
//     position = the BUILDING's entrance dummy node ("dummy_EINGANG", else
//                "dummy_TUER" — VIBE_Object_FindByHandle @0x5b7be4 subtree scan,
//                StrCmpNoCase) transformed through the bone/parent chain
//                (VIBE_Transform_PointThroughBoneChain @0x5c8b38 over node +76);
//                VIBE_Object_IsNearDoorAlt @0x4b0ee8 shows the engine's own
//                fallback when the dummy is absent: the building node itself.
//     VIBE_Character_SpawnOfficeStaffActor @0x57c744:
//        model = VIBE_Office_ResolveStaffModel(person) @0x57c1e8   (1:1 in
//                play/person_render.*)
//        char  = VIBE_Character_CreateFromModel(model->name) @0x402d10 (1:1 in
//                sim/character_factory.* -> AttachToUniverseNode @0x5b3e30 ->
//                Mesh_LoadOrFindByName @0x5d345c "*<name>.bgf")
//        default spawn rotation = dword_577A78 (all zero)
//        PreloadAniSet(char, "bewegung/gehen") @0x403c34 (the factory itself
//        preloads {"bewegung/gehen","stehen/stehen_newnoise"})
//     per-frame pose: VIBE_Anim_UpdateSkeletonPose @0x5cd1d8 (1:1 in
//        render/skeleton_pose_driver.*, driven via play::PersonCharacterPose).
//
// BindPersons() reproduces exactly that: each live person (the
// VIBE_Person_IsValidActiveRecord @0x4f8e60 gate) is anchored to a BUILDING,
// the building's owner-matched scene node subtree is searched for the entrance
// dummy (the embedded .cty city scene ships one "dummy_TUER" per building;
// "dummy_EINGANG" lives in the gb_ model subtree, which is the parallel model-
// attach module), the person's character model is created through the REAL
// factory (sim::CreateFromModel with the mesh attach routed into Objects.BIN),
// and the pose advances through the REAL driver.
//
// PERSON NAMED GAPS (rule 8):
//  * person -> building ANCHOR: the original's spawn receives the building from
//    its CALLERS (VIBE_NpcAction_DailyRoutineStep @0x4e7e88, ExEnterBuilding
//    @0x495300-family, combat slots) — there is no single reconstructed Person
//    column that drives the spawn. The DEFAULT anchor reads the person record's
//    own building columns the daily director reads (sim/npc_daily.h):
//    +0x16C homeBld (dword_12CEA7C), else +0x170 workBld (dword_12CEA80), as a
//    building id (the repo pointer-as-id model). 0 == no anchor == NOT placed
//    (counted in personsUnplaced(); never seated at an invented position).
//    `resolvePersonAnchor` / `resolvePersonPlacement` are the host hooks.
//  * spawn rotation: the default spawn pushes NO rotation (dword_577A78 zeros);
//    persons render with the identity orientation.
//  * which CLIP a live person plays is the NpcAction/charaction runtime; the
//    pose plays the factory-preloaded idle ("stehen/stehen_newnoise", gait
//    "bewegung/gehen" fallback) — the genuine CreateMesh @0x4029c4 preload set.
//  * VIBE_Character_ApplyHeadVariant @0x57c548 / ResolveHeadBone @0x57c5d4 and
//    VIBE_Object_SelectTextureSet @0x5b3f54 (texVariant surface swap) are not
//    reconstructed (the resolved record carries the variant bytes).
// =============================================================================
#include "guild/common/types.h"
#include "io/archive_mount.h"
#include "play/person_render.h"          // ResolveStaffModel (0x57c1e8) + pose chain
#include "play/real_mesh_source.h"
#include "play/real_texture_source.h"
#include "play/scene_view.h"             // SceneObjectInst (.ed3 nodes, ownerId)
#include "play/terrain_render.h"         // FloorGround / GroundFrame (ground pass)
#include "render/geometry_types.h"       // Frustum / MeshGeometry / DrawListEntry
#include "render/heightmap.h"            // Heightmap (the city terrain sampler)
#include "render/scene.h"                // DrawListBuffers
#include "render/fog.h"                  // FogState (per-pixel vertex fog, W7-FOGPIX)
#include "render/scene_lights.h"         // SceneLightSet / CullForObject (W8-SCENELIGHTS)
#include "render/skycolor_recon.h"       // SkySceneTable (per-scene sky band table)
#include "render/floorgfx_recon.h"       // FloorTextureResolver (the slot loader)
#include "render/scene_transform.h"      // Mat3 / MatrixFromEuler / WorldToView
#include "render/texture.h"
#include "render/texture_asset.h"        // TextureAssetCache (ground slot textures)

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace guild::render { struct Surface; }
namespace guild::shim { class IFileSystem; class IGraphicsDevice; }
namespace guild::sim { struct ObjectRec; struct Person; struct LiveActor; }

namespace guild::play {

struct SceneDrawNode;   // wire_scene_bridge.h (the bridge walk node)

// ---------------------------------------------------------------------------
// The engine camera-node state the session feeds per frame (node +76 / +132).
// ---------------------------------------------------------------------------
struct CityCamera3D {
    float eye[3] = {0, 0, 0};   // camera node +76/+80/+84 world position
    float rot[3] = {0, 0, 0};   // camera node +132/+136/+140 euler (radians);
                                // view basis R = MatrixFromEuler(-rot) (type-3 node)
};

// A decoded world placement (position + full euler — city nodes rotate about
// more than yaw, so this carries the whole +132 euler, unlike WorldPlacement).
struct CityPlacement {
    float pos[3]   = {0, 0, 0};   // node +76/+80/+84
    float euler[3] = {0, 0, 0};   // node +132/+136/+140
};

// ---------------------------------------------------------------------------
// Hooks (wave-2 binding points; REAL defaults run when a slot is empty).
// ---------------------------------------------------------------------------
struct CityView3DHooks {
    // Object -> model. `node` is the owner-matched scene node (null if none).
    // Returns an Objects.BIN member path OR a bare .bgf basename (resolved
    // through the archive base-name index). Empty => no model (counted as
    // modelUnresolved; the node's own mesh default already covers the shipped
    // city). Wave 2 binds the gb_<typeName> loader (0x4ffe0c / 0x50d01c) here.
    std::function<std::string(const sim::ObjectRec& rec,
                              const SceneObjectInst* node)> resolveObjectModel;
    // Object -> world placement. Returns false => unplaced (skipped). Default:
    // the RebuildModelByOwner @0x5a8140 owner-id scene-node match.
    std::function<bool(const sim::ObjectRec& rec,
                       CityPlacement& out)> resolveObjectPlacement;

    // ---- PERSON slots (wave 3; all optional, defaults documented above) ----
    // Person -> staff-model record. Default: the REAL resolver chain
    // play::MakePersonModelView + play::ResolveStaffModel (VIBE_Office_
    // ResolveStaffModel @0x57c1e8, reconstructed 1:1). Null result == no model.
    std::function<const StaffModelRecord*(const sim::Person& p)> resolvePersonModel;
    // Person -> anchor BUILDING object id (the building VIBE_Character_
    // SpawnAtBuildingEntrance @0x57c8f0 receives from its callers). Default:
    // the person record's +0x16C homeBld (dword_12CEA7C), else +0x170 workBld
    // (dword_12CEA80) — the columns VIBE_NpcAction_DailyRoutineStep @0x4e7e88
    // dispatches to (sim/npc_daily.h). 0 == no anchor == NOT placed.
    std::function<i32(const sim::Person& p)> resolvePersonAnchor;
    // Person -> full world placement override. Returns false => unplaced.
    // Default: anchor building's owner node subtree -> entrance dummy
    // ("dummy_EINGANG", else "dummy_TUER" — the 0x57c8f0 spawn names; the
    // 0x4b0ee8 fallback is the building node itself), composed world position
    // (PointThroughBoneChain @0x5c8b38 semantics), zero euler (dword_577A78).
    std::function<bool(const sim::Person& p,
                       CityPlacement& out)> resolvePersonPlacement;
};

// ---------------------------------------------------------------------------
// The entrance-dummy resolution of VIBE_Character_SpawnAtBuildingEntrance
// @0x57c8f0 over a parsed scene: the first PRE-ORDER descendant of
// scene[buildingIdx] named "dummy_EINGANG" (the spawn's queue/entrance tag),
// else the first named "dummy_TUER" (the plain-building tag, also the
// VIBE_Object_IsNearDoorAlt @0x4b0ee8 door probe). Name compare is
// case-insensitive (the engine's FindByHandle StrCmpNoCase walk @0x5b7be4).
// Returns the scene index of the dummy, or -1 when the subtree has neither
// (callers then use the building node itself — the 0x4b0ee8 fallback).
// Exposed as a free function for unit tests.
// ---------------------------------------------------------------------------
int FindEntranceDummyNode(const std::vector<SceneObjectInst>& scene, int buildingIdx);

// ---------------------------------------------------------------------------
// Engine math helpers (exposed for tests; all 1:1 with the cited functions).
// ---------------------------------------------------------------------------

// gilde.exe 0x5accd0 (prologue) — build the engine frustum planes exactly as
// VIBE_Render_BuildViewMatrix does (operands from the disassembly): ang =
// atan2(W*0.5, viewScale) for the x planes, ang2 = atan2(H*0.5, viewScale) for
// the y planes; the four side planes carry the binary's exact epsilon d terms
// (dwords 0x360637BD / 0xB60637BD), near/far mirror flt_13FC76C / flt_13FCAFC.
void BuildEngineFrustum(float fbW, float fbH, float viewScale,
                        float nearZ, float farZ, render::Frustum& out);

// Compose the 16-float column-major model->VIEW matrix (the engine's record+72
// that VIBE_Mesh_InterpolateMorphVertices applies): linear part = R_view *
// localToWorld with R_view = Transpose(MatrixFromEuler(-cam.rot)); translation =
// R_view * (worldPos - cam.eye)  (PointToBoneLocalSpace @0x5c8c40).
void ComposeModelViewMatrix(const render::Mat3& localToWorld,
                            const float worldPos[3], const CityCamera3D& cam,
                            float out16[16]);

// Host convenience: solve cam.rot (pitch/yaw, roll=0) so the engine camera at
// `eye` faces `target` — the exact inverse of the engine forward (column 2 of
// R = MatrixFromEuler(-rot)): fwd = (-cx*sy, sx, cx*cy). Unit-tested round-trip
// through render::CameraForward / WorldToView.
void AimCamera(CityCamera3D& cam, const float eye[3], const float target[3]);

// ---------------------------------------------------------------------------
// CityView3D — session-consumable whole-city real-3D view.
// ---------------------------------------------------------------------------
class CityView3D {
public:
    struct Options {
        int   fbW = 160, fbH = 120;
        u8    clearR = 0, clearG = 0, clearB = 64;   // sky clear
        // Near/far clip planes (flt_13FC76C / flt_13FCAFC). The original reads
        // them from the runtime gfx config block (13ECEBC/13ECEC0, Gilde.INI
        // driven); the scene fog far (ConfigureFog @0x5ae384) tightens far.
        float nearZ = 10.0f;
        float farZ  = 0.0f;        // 0 => the loaded scene's fog far, else 20000
        // flt_13FCD0C — the view scale. 0 => fbW*0.5 (the engine's value: the
        // projection/pick consumers all store width*flt_6280DC == W*0.5).
        float viewScale = 0.0f;
        int   maxInstances = 0;    // 0 == draw every instance (the whole city)
        bool  textured = true;     // bind Textures.BIN materials when mounted
        // GROUND PASS (terrain-ground wave 4): draw the REAL parsed city floor
        // through the BeginUniverseFrame @0x5B3900 terrain hook (the 0x5b3a2f
        // VIBE_Floor_RenderTerrain arm — frame.cpp's fs.hasTerrain branch).
        // DEFAULT OFF so every pre-existing pinned frame (city_view3d /
        // playable_flow / full_session hashes + pixel counts) stays byte-
        // identical; the session / e2e opt in. See play/terrain_render.h for
        // the data flow + named gaps.
        bool  terrain = false;
        // WATER (wave-6 W6-WR): build + animate + draw the floor's water regions
        // (VIBE_FloorWater_PrepareRegions @0x5ba95c -> AnimateWaterVertices
        // @0x5be428 -> the @0x5be668 water render arm) in the SAME terrain pass /
        // draw list, AFTER the ground tiles. Follows terrain by default (water can
        // only draw when the ground pass runs); DEFAULT OFF so a scene with no
        // water (or terrain off) is byte-identical to today.
        bool  water = false;
        // ---- WAVE-6 WORLD-ENTITY RENDER FEATURES (W6-INTEGRATE) --------------
        // Each is the additive opt-in for one reconstructed wave-6 module, wired
        // into BeginUniverseFrame @0x5b3900's exact order. ALL DEFAULT FALSE so
        // every pre-existing pinned frame (city_view3d/universe/playable_flow/
        // full_session/terrain/fidelity e2es) stays BYTE-IDENTICAL; the session
        // (sdl_session.cpp) opts in. Each carries its engine gate global.
        bool  sky          = false;  // RenderSky @0x5b3953 clear-to-sky-colour
        bool  shadows      = false;  // RenderObjectShadow (dword_1408A60 enable)
        bool  particles    = false;  // render_system_to_surface (after objects)
        bool  weather      = false;  // snow/rain overlay (winter/rain gates)
        bool  mirror       = false;  // ShouldRenderMirrorPass reflection append
        bool  fog          = false;  // BlendFog565 per-pixel span (byte_649DD8)
        bool  dynamicLight = false;  // LightMeshVertices sun+point per-vertex
        bool  worldSprites = false;  // ProjectBillboardVertices depth-fade
        bool  lodSelect    = false;  // SelectLodFrame per-distance node LOD
        // ---- WAVE-8 WORLD-ENTITY FEATURES (W8-INTEGRATE) --------------------
        // Each additive, DEFAULT FALSE so every pinned frame stays byte-identical;
        // the session opts in. The render/sim module bodies are CALLED, not edited.
        // sceneLights: feed the wave-7 bone-matrix LightMeshVertices (sun NdotL +
        // scene point lights) over REAL object-space normals (mesh_normals
        // GenerateVertexNormals + BindInstanceNormals) so objects are SUN-LIT, not
        // flat-ambient. Implies dynamicLight's day/night ambient as the seed. The
        // genuine fix for the wave-6/7 "no per-vertex normals" named gap.
        bool  sceneLights  = false;  // W8-NORMALS + W8-SCENELIGHTS per-vertex shade
        // ---- WAVE-9 WORLD-ENTITY FRAME ENRICHMENT (W9-FRAME-ENRICH) ----------
        // Each additive, DEFAULT FALSE so every pinned frame stays byte-identical;
        // the session opts in. The render/sim module bodies are CALLED, not edited.
        // The wave-8 reconstructions were inert in the live frame because the
        // simplified instance pipeline did not carry the per-object data they need;
        // wave-9 threads that data through so they FIRE.
        // flagAnim: walk each building/person's universe-node CHILD list (the scene
        // nodes whose parent is that node) and run render::RefreshFlagAnimation (the
        // heraldry-gated flag attach @0x4b5ef8) so the sp_WIMPEL flag object exists
        // and waves via the already-wired pose driver (cloth-anim-wave8.md handoff).
        bool  flagAnim     = false;
        // vegRelight: per frame, run render::BuildVegetationCache @0x5c8560 on the
        // type-4 vg_/pfl_ scenery the pose-driver veg gate (0x5cebed) targets, so
        // moving lights re-shade leaves (vegetation-anim-wave8.md handoff).
        bool  vegRelight   = false;
        // reflective: consult render::reflective_nodes on scene meshes — if a mesh
        // carries a reflective material (high-shift + palette), set the mirror gate +
        // derive the plane so the wave-6/7 mirror pass fires (reflective-nodes-wave8.md
        // handoff). AUGSBURG ships none, so the gate stays idle there (documented).
        bool  reflective   = false;
        // animals: draw the ambient animals the sim spawns (sim::Animal_Update) by
        // routing each through the SAME character/person 3D render path persons use
        // (creature-wave8.md render-arm gap). Requires AddAnimalInstance() to have
        // seated the spawned actors (the session's CityAnimalWorld does this).
        bool  animals      = false;
        // The world time-of-day the sun/sky/light/shadow passes read (the session
        // feeds tick.worldTime(); 0/0/0 == predawn). Drives render::ComputeSunState.
        i32   worldDay = 0;
        int   worldHour = 12;
        int   worldMinute = 0;
    };

    struct Result {
        int  sceneNodes      = 0;  // parsed .ed3 nodes
        int  instancesDrawn  = 0;  // instances fed through the frame this render
        int  objectInstances = 0;  // ... of which are live-object bound nodes
        int  meshPolysIn     = 0;  // source polygons before cull
        int  appendedPolys   = 0;  // draw-list entries the real dispatch appended
        int  nodesDispatched = 0;  // nodes ProcessSceneNodeAppend dispatched
        int  rasterTris      = 0;  // triangles RasterizeMeshList flushed
        int  nonClearPixels  = 0;  // pixels != the sky clear
        int  distinctColors  = 0;  // distinct non-clear colours
        bool textured        = false;
        int  texturedPolys   = 0;  // polys rasterized via the RGBZ textured span
        int  boundMaterials  = 0;  // materials bound to a real Textures.BIN BMP
        // -- person pass (all 0 unless BindPersons() placed persons) ----------
        int  personInstances = 0;  // person character meshes drawn this frame
        int  personPosed     = 0;  // ... of which drawn POSED (the 0x5cd1d8 chain)
        int  personRestPose  = 0;  // ... drawn at the static rest pose (named gap)
        // -- ground pass (all 0/false unless Options::terrain && a parsed floor) -
        bool terrainDrawn      = false;  // the renderTerrain hook ran + rasterized
        int  terrainTiles      = 0;      // tiles with a nonzero LOD this frame
        int  terrainPolys      = 0;      // draw-list entries the 0x5bf22c walk appended
        int  terrainRasterTris = 0;      // polys the 0x5AEC88 flush iterated
        // -- water pass (wave-6 W6-WR; 0 unless Options::water && a water floor) --
        int  waterRegions      = 0;      // BuildWaterRegions region count (0 == none)
        int  waterPolys        = 0;      // water polys the @0x5be668 arm appended
        int  waterVerts        = 0;      // water surface vertices built this frame
        // -- wave-6 world-entity render features (0/false unless the Options flag) -
        bool skyDrawn          = false;  // RenderSky filled the surface this frame
        u32  skyColor          = 0;      // the time-of-day sky colour used (0=off)
        int  sunBand           = -1;     // ComputeSunState band (-1 == sun off)
        int  sunBrightness     = 0;      // ComputeSunState 0..600 brightness
        int  litObjects        = 0;      // objects re-lit via LightMeshVertices
        int  sunLitVerts       = 0;      // (W8) per-vertex sun-NdotL shades applied
        int  sceneLightCount   = 0;      // (W8) scene light nodes collected this frame
        int  lodObjects        = 0;      // objects whose LOD frame was reselected
        int  shadowCasters     = 0;      // objects that emitted a drop shadow
        int  shadowPixels      = 0;      // shadow pixels splatted into the frame
        int  particlePixels    = 0;      // particle pixels blended after objects
        int  particleSystems   = 0;      // (W8) live particle systems walked this frame
        bool mirrorPass        = false;  // ShouldRenderMirrorPass ran this frame
        int  weatherDrops      = 0;      // rain streaks / snow flakes drawn
        bool fogApplied        = false;  // per-pixel span fog was enabled
        // -- wave-9 frame enrichment (0/false unless the Options flag) -----------
        int  flagObjects       = 0;      // (W9) flag objects RefreshFlagAnimation produced
        int  flagRefreshNodes  = 0;      // (W9) flag-bearing nodes RefreshFlagAnimation walked
        int  vegRelitMeshes    = 0;      // (W9) vg_/pfl_ type-4 meshes BuildVegetationCache relit
        int  vegRelitVerts     = 0;      // (W9) vegetation vertices relit this frame
        int  reflectiveMeshes  = 0;      // (W9) scene meshes carrying a reflective material
        int  animalInstances   = 0;      // (W9) ambient animals drawn through the character path
    };

    // One renderable city instance (a scene node or a bound live object).
    struct Instance {
        std::string  name;           // scene node name
        std::string  member;         // Objects.BIN member (.bgf)
        int          sceneIndex = -1;// index into scene() (-1 == hook-placed)
        i32          objectId = 0;   // live object id (0 == static scenery)
        render::Mat3 l2w{};          // composed local->world rotation
        float        pos[3] = {0, 0, 0};  // composed world position
    };

    // The outcome of binding one live world object (BindWorldObjects).
    struct BoundObject {
        i32           id   = 0;
        int           slot = -1;
        int           sceneIndex = -1;   // owner-matched scene node (-1 == hook)
        CityPlacement place;
        std::string   member;            // resolved model member ("" == named gap)
    };

    CityView3D();
    ~CityView3D();

    // Mount Resources/scenes.BIN + Resources/Objects.BIN (and, best-effort,
    // Resources/Textures.BIN for the textured raster). Returns true when the
    // scene + object archives mounted.
    bool Init(shim::IFileSystem* fs);
    bool mounted() const { return mounted_; }
    bool texturesMounted() const { return tex_.mounted(); }

    // Parse scenes/Staedte/stadt_<CITY>.ed3 (the engine's city scene,
    // VIBE_Scene_LoadStadtScene @0x500218 name scheme), compose the node world
    // transforms (PointThroughBoneChain @0x5c8b38 parent chain), and build the
    // static scenery instances + the ownerId -> node index. Returns true when
    // the scene parsed with nodes.
    bool LoadCity(const char* cityName);

    // Load the city scene from the EMBEDDED .cty scene stream — the blob
    // io::LoadWorldEx captures at the exact position VIBE_Save_PostLoadInitScene
    // @0x5a7ef8 feeds to VIBE_Scene_LoadFromStream @0x5e7e38. This is the REAL
    // post-load source of the city node placements AND the +512 owner-object
    // ids (the shipped stadt_<CITY>.ed3 carries no owner links; the persisted
    // save scene does). Preferred over LoadCity() after a world load.
    bool LoadCityFromWorld(const std::vector<u8>& sceneStream);

    // Bind every alive sim::g_objects record to its city placement + model via
    // the hooks (defaults: the REAL owner-id match / the node's shipped mesh).
    // Re-runs cleanly. Returns the number of objects placed.
    int BindWorldObjects();

    void SetHooks(CityView3DHooks h) { hooks_ = std::move(h); }
    const CityView3DHooks& hooks() const { return hooks_; }

    // ------------------------------------------------------------------------
    // PERSONS (wave 3 — additive; with no BindPersons() call every frame is
    // byte-identical to before).
    // ------------------------------------------------------------------------

    // Load the city scene from an ALREADY-PARSED node list (the same composition
    // loadSceneBuffer runs after ParseSceneObjects). Test/host seam for synthetic
    // scenes; the real paths stay LoadCity/LoadCityFromWorld. Returns true when
    // nodes were accepted.
    bool LoadCityFromNodes(std::vector<SceneObjectInst> nodes);

    // Mount Resources/animations.BIN so bound persons pose through the REAL anim
    // chain (the factory-preloaded .baf clips). Optional: without it persons draw
    // at the static rest pose (Result::personRestPose; named gap). True on mount.
    bool InitPersonAnims(shim::IFileSystem* fs,
                         const char* archivePath = "Resources/animations.BIN");
    bool personAnimsMounted() const { return animsMounted_; }

    // The outcome of binding one live person (BindPersons).
    struct BoundPerson {
        i32         id = 0;             // sim::Person::id
        int         slot = -1;          // g_persons slot
        i32         anchorBuildingId = 0; // resolved building anchor (0 == hook-placed)
        int         dummySceneIndex = -1; // entrance-dummy node (-1 == building-node
                                          // fallback @0x4b0ee8 / hook placement)
        CityPlacement place;            // world seat (the dummy's composed +76)
        std::string model;              // resolved staff model ("dieb_MANN2")
        std::string member;             // Objects.BIN member ("" == model named gap)
        std::string base;               // factory name decomposition (rec+304)
        bool        posed = false;      // a clip bound through the pose chain
    };

    // Bind every live person (the VIBE_Person_IsValidActiveRecord @0x4f8e60
    // gate) to a placement + character model through the chain documented above:
    // placement hook / building-anchor entrance dummy; model through the REAL
    // VIBE_Office_ResolveStaffModel @0x57c1e8 + the REAL character factory
    // (sim::CreateFromModel @0x402d10, mesh attach routed into Objects.BIN).
    // `maxPersons` 0 == no cap. Re-runs cleanly (UnbindPersons first).
    // Returns the number of persons placed.
    int BindPersons(int maxPersons = 0);

    // Release the bound persons: frees the factory records (the g_live slots the
    // REAL AllocSlot @0x402254 filled) and drops the pose states.
    void UnbindPersons();

    // Move an already-bound person instance to a new placement (position+euler),
    // keeping its factory record, pose state and material binds intact (the
    // living-city wave-4 movement handoff: the per-commit tile move updates ONLY
    // the draw seat — the original moves the character NODE position (+76), not
    // the model). Updates persons_[i].info.place + the parallel boundPersons_
    // copy; l2w is rebuilt from the new euler (identity for the zero spawn euler
    // dword_577A78). Returns false when `id` is not bound.
    bool MoveBoundPerson(i32 id, const CityPlacement& place);

    // WAVE-8 (W8-NPCCLIP) — flip a bound person's playing clip to the named
    // factory-preloaded clip (the gait "bewegung/gehen" when moving, the idle
    // "stehen/stehen_newnoise" when still — the choice made by
    // sim::SelectPersonClipFromMovement). Reloads the person's PersonCharacterPose
    // over the named .baf from the mounted animations.BIN (the clip is in the
    // factory preload set, so no new stream beyond the archive). No-op (returns
    // false) when `id` is not bound, the clip is already playing, the clip name is
    // empty/None, or the .baf is absent. Idempotent.
    bool SetBoundPersonClip(i32 id, const char* clipName);
    const char* boundPersonClip(i32 id) const;

    // Advance every poseable bound person's clip through the REAL pose driver
    // (UpdateSkeletonPose @0x5cd1d8): folds `stepTicks` into the track phase (the
    // documented host-rate seam). Returns the number of poses advanced. The next
    // RenderFrame samples the advanced cursors.
    int AdvancePersonPoses(float stepTicks);

    const std::vector<BoundPerson>& boundPersons() const { return boundPersons_; }
    int personsUnplaced() const { return personsUnplaced_; }          // named gap
    int personModelUnresolved() const { return personModelUnresolved_; }

    // ------------------------------------------------------------------------
    // WAVE-9 W9-FRAME-ENRICH — FLAGS / ANIMALS / VEG / REFLECTIVE wiring.
    // ------------------------------------------------------------------------

    // FLAGS (cloth-anim-wave8.md handoff). Walk each bound BUILDING node's
    // universe-node CHILDREN (the scene nodes whose parent index is that node) and
    // run render::RefreshFlagAnimation @0x4b5ef8 — the heraldry-gated flag attach
    // that ensures the sp_WIMPEL flag object exists with its .baf playing, so the
    // already-wired pose driver waves it. `heraldryOf` maps a bound object's id to
    // its heraldry index (0xFFFF == none) and the build-type byte the gate reads
    // (byte_12CE912[536*heraldry] in {5,6,7}); when null every bound object is
    // treated as ungated (heraldry 0xFFFF) so nothing fires (faithful: no heraldry
    // table carried in this view -> the gate the engine itself applies stays shut).
    // Records the produced flag objects into the per-node child instances so the
    // frame draws + poses them. Re-runs cleanly. Returns the flag objects produced.
    struct FlagHeraldry {
        u16 heraldry  = 0xFFFF;   // person/building +39 (0xFFFF == no flag)
        u8  buildType = 0;        // byte_12CE912[536*heraldry] (gate: {5,6,7})
        u8  heraldryByte = 0;     // LOBYTE(word_12CE910[268*heraldry+42]) (texture)
    };
    int RefreshObjectFlags(
        const std::function<FlagHeraldry(i32 objectId)>& heraldryOf = {});
    int flagObjectsProduced() const { return flagObjectsProduced_; }

    // ANIMALS (creature-wave8.md render-arm gap). Seat one spawned ambient animal
    // as a renderable character instance at `place`, resolving `model` (a species
    // model name e.g. "hund_HUND") through the SAME mesh source persons use. Returns
    // a nonzero actor TOKEN (the index into the animal instance list, +1) the caller
    // stores in AnimalRec+0 (so DestroyAnimal can remove it), or 0 when the model is
    // not shipped. The session's CityAnimalWorld::SpawnAnimal calls this; the animal
    // then renders through the character path each frame (Options::animals).
    i32 AddAnimalInstance(const char* model, const CityPlacement& place);
    // Remove a previously seated animal by its actor token (AddAnimalInstance ret).
    void RemoveAnimalInstance(i32 actorToken);
    // Drop every seated animal (city unload).
    void ClearAnimalInstances();
    int  animalInstanceCount() const;

    // Drive ONE engine frame over all instances into the owned Surface.
    Result RenderFrame(const CityCamera3D& cam, const Options& opt);

    // Blit + present into `device`'s backbuffer (headless MemoryGraphicsDevice /
    // on-screen VulkanGraphicsDevice). Returns true when present() ran.
    bool PresentToDevice(shim::IGraphicsDevice& device);

    render::Surface* surface() const { return fb_; }

    // Host helper for tests / the initial session seat: an overview camera above
    // the city bounds centre, aimed at it (AimCamera).
    CityCamera3D OverviewCamera(float elevationFactor = 0.9f,
                                float backFactor = 0.7f) const;

    // ---- introspection -----------------------------------------------------
    const std::vector<SceneObjectInst>& scene() const { return scene_; }
    const std::vector<Instance>& instances() const { return instances_; }
    const std::vector<BoundObject>& boundObjects() const { return bound_; }
    int modelUnresolved() const { return modelUnresolved_; }  // named-gap count
    int unplacedObjects() const { return unplaced_; }
    float sceneFogFar() const { return fogFar_; }
    void WorldBounds(float lo[3], float hi[3]) const;

    // ------------------------------------------------------------------------
    // GROUND / CITY TERRAIN introspection (terrain-ground wave 4).
    // ------------------------------------------------------------------------
    // The parsed floor block of the loaded scene (LoadFloorRegions @0x5e78a8;
    // parsed by LoadCity / LoadCityFromWorld over the same stream bytes).
    const render::SceneFloorBlock& floorBlock() const { return floorBlock_; }
    // The walk-ready ground built from it (invalid when the scene has no floor).
    const FloorGround& ground() const { return ground_; }
    bool hasGround() const { return ground_.valid(); }

    // Build (lazily) the city terrain Heightmap the engine samplers walk:
    // scales via BuildCityHeightmapFromFloor (@0x5c5610 DeriveGridScaleXZ +
    // originY/scaleY), then the REAL heights/entries fill through
    // BuildTileIlluminationTable (@0x5c4718) + BuildLitTileGeometry (@0x5c47dc).
    // Returns null when no floor block is loaded. The returned Heightmap feeds
    // TileToWorld / WorldToTileWithHeight / AverageAreaHeight (the camera ground
    // queries) and the ground-meets-building e2e checks.
    const render::Heightmap* cityHeightmap();

    // ---- internal step bodies (public only for the C frame-hook trampolines
    // and the textured SpanFill; not part of the intended call surface) --------
    void doClear();
    int  doSceneWalk();
    void doFlush();
    void doRenderTerrain(char a2);   // the renderTerrain hook body (0x5b3a2f arm)
    void doParticles(char a2);       // renderParticles hook (0x5b3a98 — after objects)
    // WAVE-8 W8-EMITTER + W8-SMOKE: spawn the city's chimney-smoke particle systems
    // by scanning the parsed scene for "dummy_RAUCH_0" smoke-dummy nodes and running
    // render::SpawnEmitterAtPosition at each dummy's composed world position (the
    // effekte\Schornstein_dunkel.esc CreateEmitter the smoke script body invokes —
    // building-fx-wave8.md default runSmokeScript). Links each system into
    // render::LiveSystems() (the list the doParticles walk drives). Re-runnable
    // (clears prior systems first). Returns the number of smoke systems spawned.
    int SpawnCityChimneySmoke();
    int chimneySmokeSystems() const { return chimneySmokeSystems_; }
    void doMirrors(char a2);         // buildMirrors hook (0x5b3af0 — after particles)
    // WAVE-9 W9-FRAME-ENRICH frame steps (each gated on its Options flag):
    void doVegRelight();             // type-4 vg_/pfl_ scenery relight (BuildVegetationCache)
    void doReflectiveScan();         // consult reflective_nodes -> mirror gate + plane
    void doResetLights();            // resetLights hook (ShadowResetLightList 0x5f4428)
    void doShadows();                // per-object drop shadows (after terrain, under objects)
    struct BoundTex {
        const render::Texture* tex = nullptr;
        const u16*             palette = nullptr;
        // RGB (24-bit) BMP fallback bind (person character textures: the shipped
        // _DYNAMIC/Character/*.bmp are 24-bit, no palette/indices). Sampled via
        // the in-tree affine textured kernel (play::RasterTexturedTriangleAffine,
        // the same path the legacy person pass rasterizes these BMPs through).
        // Only set when bindFor ran with rgbAffineFallback (the person pass);
        // city binds are unchanged.
        const render::DecodedBmp* rgb = nullptr;
    };
    // Decode a frame-local poly key ((instanceIdx<<12)|matIndex) to its bind.
    const BoundTex* boundTexForKey(i32 key) const;
    void addTexturedPoly() { ++texturedPolysThisFrame_; }

    // The by-name texture cache the ground FloorTextureResolver loads slot BMPs
    // through (wave-5 W5-TX). Mounted over Textures.BIN by SetupGroundTexCache:
    // the resolver's LoadByName("*"+name+".BMP", name) is served the matching
    // archive member (e.g. "WIESE" -> _DYNAMIC/Boden/Wiese.bmp) through the
    // cache's BmpFetch hook (the engine's VFS-resolve-over-archives behaviour).
    render::TextureAssetCache* textureCache() { return &groundTexCache_; }
    // Factory attach-hook target (sim::CharacterFactoryHooks::attachToUniverseNode
    // trampoline): resolve `model` to its Objects.BIN member + decoded geometry —
    // the Mesh_LoadOrFindByName @0x5d345c "*<name>.bgf" case-insensitive VFS
    // resolve over "_DYNAMIC/Character/<name>.bgf". Null when not shipped.
    const render::MeshGeometry* ResolvePersonMesh(const char* model,
                                                  std::string* memberOut);

private:
    // Per-member material bind (one per distinct .bgf, shared by instances).
    struct MatBind {
        std::vector<render::Texture>  tex;
        std::vector<std::vector<u16>> pal;
        std::vector<BoundTex>         bound;
        int boundCount = 0;
    };
    // `rgbAffineFallback`: also bind 24-bit (non-palettized) BMPs as RGB binds
    // (BoundTex::rgb) — used by the person pass; the city pass passes false so
    // every pre-existing frame stays byte-identical.
    const MatBind* bindFor(const std::string& member, bool rgbAffineFallback = false);

    // Per-instance per-frame transformed geometry (vertex/poly copies).
    struct FrameMesh {
        std::vector<render::Vertex>  verts;
        std::vector<render::Polygon> polys;
        render::MeshGeometry         geom{};
    };

    std::string ResolveMember(const std::string& nameOrMember) const;
    void buildInstances();
    bool loadSceneBuffer(const u8* data, std::size_t size);
    // The per-frame view parameters (engine frustum + SetupViewTransform
    // scalars), shared by doSceneWalk and doRenderTerrain; also fills
    // clipPlanes_/projScalars_ for the flush. (Pure factor of the doSceneWalk
    // prologue; behaviour unchanged.)
    void buildViewParams(render::Frustum& fr, render::ObjectProjectScalars& s);
    // Compose scene_ world transforms + owner index + instances (the shared tail
    // of loadSceneBuffer / LoadCityFromNodes; pure refactor, behaviour unchanged).
    void composeSceneTransforms();
    // The default person placement (anchor columns -> owner node -> entrance
    // dummy; see CityView3DHooks::resolvePersonPlacement).
    bool DefaultPersonPlacement(const sim::Person& p, CityPlacement& out,
                                int* dummyIdxOut, i32* anchorOut) const;

    shim::IFileSystem*  fs_ = nullptr;
    bool                mounted_ = false;
    io::ArchiveMount    scenes_;     // Resources/scenes.BIN
    io::ArchiveMount    names_;      // Objects.BIN member-name index
    RealMeshSource      src_;        // Objects.BIN AGF decode + cache
    RealTextureSource   tex_;        // Resources/Textures.BIN (optional)
    std::map<std::string, std::string> baseToMember_;  // UPPER base -> member
    std::map<std::string, MatBind>     matBinds_;      // member -> material bind

    std::string                  cityName_;
    std::vector<SceneObjectInst> scene_;
    std::vector<render::Mat3>    sceneRot_;   // composed local->world rotations
    std::vector<float>           scenePos_;   // composed world positions (3*i)
    std::map<u32, int>           ownerToNode_;
    float                        fogFar_ = 0.0f;
    bool                         hasFog_ = false;
    // WAVE-7 W7-SKYBANDS: the per-scene sky/fog band table (flt_13FD1B8.. /
    // dword_13FD1D0..) loaded once from the scene header at scene-open (the runtime
    // data VIBE_Scene_LoadFromStream @0x5e7e38 reads — closes the wave-6 fallback
    // gap). ComputeSkyFog(skyBands_, sun.band, sun.blend, ...) drives the real
    // time-of-day clear when bands are present; empty == the wave-6 fallback.
    render::SkySceneTable        skyBands_{};
    bool                         hasSkyBands_ = false;
    // WAVE-7 W7-FOGPIX: the per-frame D3D fixed-function VERTEX fog state the
    // textured spans read to compute each vertex's per-pixel fog factor
    // (ComputeFogFactor over the vertex view-space depth). Configured in RenderFrame
    // when Options::fog; fogState_.enabled mirrors SpanFog().enabled. The span
    // trampolines (CV3D_SpanTextured / GroundSpanTextured) seed RgbzVertex::fogFactor
    // from it so RasterizeTexturedTriangleRgbz interpolates the factor per pixel.
    render::FogState             fogState_{};
public:
    // Accessor for the span trampolines (the genuine per-vertex fog factor source).
    const render::FogState&      fogStateForSpan() const { return fogState_; }
private:

    std::vector<Instance>    instances_;
    std::vector<int>         instanceOfNode_;  // sceneIndex -> instances_ idx (-1)
    std::vector<BoundObject> bound_;
    int                      modelUnresolved_ = 0;
    int                      unplaced_ = 0;

    CityView3DHooks hooks_;

    render::Surface* fb_ = nullptr;
    Options          opt_{};
    CityCamera3D     cam_{};

    std::vector<render::DrawListEntry> pool1_, pool2_;
    render::DrawListBuffers db_{};

    std::vector<FrameMesh> frameMeshes_;       // per drawn instance, per frame
    std::vector<const MatBind*> frameBinds_;   // parallel to frameMeshes_
    std::vector<SceneDrawNode> nodes_;         // per-frame walk nodes (C++17
                                               // incomplete-type vector; complete
                                               // in the .cpp where it is used)
    // The per-frame view clip set (the 13DB398 plane-table rows the engine's
    // flush clips straddling polys against: side planes 0..3 + near + far) and
    // the SetupViewTransform reproject scalars, built by doSceneWalk for doFlush.
    float clipPlanes_[6][4] = {};
    float projScalars_[4] = {1, 0, 1, 0};   // xScale,xOffset,yScale,yOffset

    int lastNodesDispatched_ = 0;
    int lastRasterTris_ = 0;
    int lastPolysIn_ = 0;
    int lastInstances_ = 0;
    int lastObjectInstances_ = 0;
    int texturedPolysThisFrame_ = 0;
    int boundMaterialsThisFrame_ = 0;

    // ---- GROUND pass state (terrain-ground wave 4) --------------------------
    render::SceneFloorBlock floorBlock_;   // the parsed 0x5e78a8 block
    FloorGround             ground_;       // walk-ready floor (invalid == none)
    GroundFrame             groundFrame_;  // walk buffers (bound lazily)
    GroundRenderStats       groundStats_{};// last doRenderTerrain read-back
    bool                    groundDrawn_ = false;
    // ---- WATER pass state (wave-6 W6-WR) ------------------------------------
    bool                    waterBuilt_ = false;  // BuildWater ran for this floor
    i32                     waterTime_  = 0;      // monotonic anim clock (62EB38)
    // ---- WAVE-6 world-entity render features (W6-INTEGRATE) ------------------
    // Per-frame outcome accumulators the FrameHooks trampolines + doFlush write,
    // read back into Result by RenderFrame. The weather systems persist across
    // frames (seeded once, integrated per frame) so they animate deterministically.
    struct Wave6FrameState {
        bool skyDrawn = false; u32 skyColor = 0;
        int  sunBand = -1; int sunBrightness = 0;
        float sunDir[3] = {0, 0, 1};   // ComputeSunState elevation -> direction
        int  litObjects = 0, lodObjects = 0;
        int  shadowCasters = 0, shadowPixels = 0;
        int  particlePixels = 0; bool mirrorPass = false;
        int  weatherDrops = 0; bool fogApplied = false;
    } w6_{};
    i32  w6Time_ = 0;                  // monotonic ms clock for weather animation
    // ---- WAVE-8 OBJECT LIGHTING (W8-NORMALS + W8-SCENELIGHTS) ---------------
    // The 1024-entry angular falloff LUT (render::BuildFalloffLUT @0x5c88f8) the
    // per-vertex light accumulation indexes; built once, lazily.
    float lightFalloffLut_[1024] = {};
    bool  lightFalloffBuilt_ = false;
    // Per-.bgf member cache of the STATIC object-space per-vertex normals
    // (mesh_normals::GenerateVertexNormals @0x5D1A6C over the source mesh), keyed by
    // Objects.BIN member, generated once and reused by every instance of that mesh.
    std::map<std::string, std::vector<float>> meshNormalCache_;  // member -> 3*vc floats
    const std::vector<float>* meshNormalsFor(const std::string& member,
                                             const render::MeshGeometry* geom);
    // The scene's collected light set (SceneLightSet), built once per frame from the
    // parsed light nodes (computeSceneLights). CullForObject runs per drawn object.
    render::SceneLightSet sceneLights_{};
    float sunColor_[3] = {1.0f, 1.0f, 1.0f};   // the directional sun colour this frame
    void computeSceneLights();        // build sceneLights_ + sunColor_ for the frame
    int  w8SunLitVerts_ = 0;          // per-frame sun-NdotL vertex count (Result)
    int  chimneySmokeSystems_ = 0;    // smoke systems spawned by SpawnCityChimneySmoke
    int  liveParticleSystems_ = 0;    // last doParticles WalkAndRender visit count
    u32  particleClock_ = 0;          // (W9) monotonic frame tick fed to WalkAndUpdate
    // ---- WAVE-9 W9-FRAME-ENRICH state --------------------------------------
    // FLAGS: the flag objects RefreshObjectFlags produced (the scene node child
    // index of each flag-bearing node + the produced FlagObject state) so the frame
    // can mark them. Keyed by the flag node's instance index.
    int  flagObjectsProduced_ = 0;    // last RefreshObjectFlags count (cumulative roster)
    int  flagRefreshNodes_ = 0;       // flag-bearing nodes walked last refresh
    // ANIMALS: ambient animals seated through AddAnimalInstance, drawn through the
    // character mesh path (creature render arm). Each is a model + placement; an
    // actor token (index+1) identifies it for RemoveAnimalInstance.
    struct AnimalInst {
        std::string  model;          // species model name ("hund_HUND")
        std::string  member;         // resolved Objects.BIN member ("" == not shipped)
        CityPlacement place;
        render::Mat3 l2w{};
        bool         alive = false;  // false == removed slot (reusable)
    };
    std::vector<AnimalInst> animals_;
    int lastAnimalInstances_ = 0;
    int vegRelitMeshes_ = 0, vegRelitVerts_ = 0;   // last doVegRelight read-back
    int reflectiveMeshes_ = 0;                      // last doReflectiveScan read-back
    void doWave6Overlay();            // snow/rain screen-space overlay (after flush)
    void computeSunForFrame();        // ComputeSunState -> w6_.sunBand/sunDir/sky
    std::unique_ptr<struct CityWeather> weather_;  // persistent rain/snow systems
    std::vector<u8> shadowStencil_;   // 8bpp shadow splat buffer (doShadows)
    // ---- GROUND tile textures (wave-5 W5-TX) --------------------------------
    render::TextureAssetCache groundTexCache_{16};  // 8 floor slots + headroom
    render::FloorTextureResolver groundTexResolver_; // typeByte -> slot record
    char groundSlotNames_[8][64] = {};               // resolver-bound names (must
                                                     // outlive the resolver: it
                                                     // keeps the array by pointer)
    bool                    groundTexReady_ = false; // cache BmpFetch installed
    bool                    groundTexBound_ = false; // resolver bound to ground_
    // 565 palette per resolved slot record (built once; keyed by record ptr).
    std::map<const render::Texture*, std::vector<u16>> groundTexPal_;
    void SetupGroundTexCache();            // install the Textures.BIN BmpFetch
    void BindGroundTextures();             // bind+install the resolver/binder
    // WAVE-7 W7-WATERTEX: the WaterTextureLoadFn BuildWater calls to resolve the
    // EF_WASS_06A_2T_W_AN0 blue water texture through the SAME groundTexCache_ the
    // ground uses (water-texture-wave7.md handoff (a)). Returns the loaded
    // render::Texture* record (stored into WaterMesh+0/+4 + GroundFrame::waterTexture_)
    // or null (the white-default branch — byte-identical to wave-6). `ctx` == this.
    static void* CV3D_LoadWaterTexture(const char* name, int flags, void* ctx);
    const render::Texture* waterTexRec_ = nullptr;  // last EF_WASS record loaded
    // GroundTexBinder trampolines (plain fn ptrs; thread through a file static —
    // the same single-active pattern as the C frame hooks). getTileTextureRec
    // returns the active getTileTexture hook's record; palette565 builds/caches
    // the 565 LUT for a resolved record.
    static const render::Texture* GroundTileTextureRec(u8 typeByte);
    static const u16*             GroundTilePalette565(const render::Texture* rec);
    const u16* groundPalette565For(const render::Texture* rec);
    // the lazily-built city terrain Heightmap (cityHeightmap()):
    render::Heightmap       cityHm_{};
    std::vector<u8>         cityHmHeights_;  // hm +40 storage
    std::vector<u8>         cityHmEntries_;  // hm +36 storage (24-byte stride)
    bool                    cityHmBuilt_ = false;

    // ---- PERSON pass state (wave 3) ----------------------------------------
    struct PersonInst {
        BoundPerson  info;
        render::Mat3 l2w{};   // identity (set at bind) — the zero spawn rotation
        std::unique_ptr<PersonCharacterPose> pose;   // null == rest pose
        sim::LiveActor* actor = nullptr;  // the factory record (a real g_live slot)
        std::vector<u8> nodeBuf;          // the engine-node image (rec+52 target)
        // WAVE-8 W8-NPCCLIP: the currently-attached clip (the factory-preload name,
        // e.g. "bewegung/gehen" / "stehen/stehen_newnoise") + the rest mesh the pose
        // binds, so SetBoundPersonClip can re-LoadClip without re-resolving the mesh.
        std::string                 clipName;
        const render::MeshGeometry* restMesh = nullptr;
    };
    // Load `clipName`'s .baf into `pi`'s pose over its rest mesh. Returns true on a
    // poseable result. Shared by BindPersons (initial idle/gait) + SetBoundPersonClip.
    bool LoadPersonClip(PersonInst& pi, const char* clipName);
    io::ArchiveMount         anims_;          // Resources/animations.BIN
    bool                     animsMounted_ = false;
    std::vector<PersonInst>  persons_;
    std::vector<BoundPerson> boundPersons_;   // parallel introspection copies
    int personsUnplaced_       = 0;
    int personModelUnresolved_ = 0;
    int lastPersonInstances_ = 0;
    int lastPersonPosed_     = 0;
    int lastPersonRest_      = 0;
};

} // namespace guild::play

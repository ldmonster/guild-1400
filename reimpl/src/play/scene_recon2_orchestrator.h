#pragma once
// ===========================================================================
// gilde.exe — Scene / frame ORCHESTRATION cluster, reconstructed 1:1.
//
// This unit reconstructs the *orchestration* of the in-city play loop and the
// scene-(re)build pipeline: the exact gate conditions, loop structure, call
// ordering, season/time thresholds and constant tables that the original
// engine uses to drive a frame and to (re)sync the world when a scene is
// entered or rebuilt.
//
// The COUPLED leaves — the live entity arrays (word_12CE910 / dword_13CE294 /
// dword_13CE298 ...), the scene-graph walker, the command/network packet
// subsystem (VIBE_Command_*), the audio engine, the script VM, camera movers,
// terrain/heightmap meshing and the renderer — are exposed here as
// inert-default hooks (a SceneHooks vtable + a small modeled SceneState). The
// pure orchestration runs in-process against that model so it is faithful and
// testable; the live wiring is supplied by the caller (real game build).
//
// This mirrors the established pattern of picksel_recon / camera_recon2.
//
// Provenance (each reconstructed entry carries its gilde.exe address):
//   0x50f0c0  VIBE_Scene_RunMainFrameLoop            (__usercall)  [orchestrator]
//   0x506df4  VIBE_Scene_ActivateAndRefreshCharacters             [orchestrator]
//   0x506d34  VIBE_GameState_ResetVoicesAndScript    (__usercall) [orchestrator]
//   0x500270  VIBE_Scene_LoadObjektScene             (__usercall)
//   0x5006a8  VIBE_Scene_LoadGebaeudeScene           (__usercall)
//   0x501c34  VIBE_Scene_SyncDecorObjects            (__usercall)
//   0x501f1c  VIBE_Scene_SyncWorkshopProduction      (__usercall)
//   0x50456c  VIBE_Scene_SyncWorldOnEnter            (__usercall)
//   0x504e14  VIBE_Scene_SyncObjectHeights           (__usercall)
//   0x504ef8  VIBE_Scene_SyncMovableObjects          (__thiscall / noreturn)
//   0x5036bc  VIBE_Scene_SpawnBuildingMesh           (__usercall)
//   0x5e860c  VIBE_Scene_SaveObjectGroup             (__usercall)
//   0x5e872c  VIBE_Scene_HandleDebugKeyToggle        (__thiscall)
//
// PARTIAL (rule 8 — control-flow skeleton only; the body is dominated by the
// command/network packet subsystem and by Hex-Rays register-arg artifacts that
// cannot be translated byte-exactly without the live entity layout):
//   0x502568  VIBE_Scene_SyncCityBuildings           (4367 insns)
//     The recoverable orchestration (the candidate-building scan, the season
//     gate table v127 = dword_4FFAFC, the per-bucket Light_SetGrayColorThunk
//     seeding, the SyncRange begin/flush/end loops, and the call sequence into
//     SyncBuildingAndOffices / City_TickStatsAndBroadcast / MeisterAi_Request*)
//     is captured; the inner Command_QueueRequest* packet emission is delegated
//     to hooks. Statements depending on uninitialized decompiler temporaries
//     are OMITTED and noted inline, not faked.
//
// ODR: every target symbol above was grep'd over src/ before writing — none is
// defined elsewhere. (camera_recon2 only *mentions* HandleDebugKeyToggle in a
// comment as one of its inert hooks; it does not define it.) All names here
// live in guild::play::scene_recon2 to avoid any clash with the live wiring
// symbols (VIBE_Camera_* etc.) that exist in other headers with their own
// prototypes.
// ===========================================================================
#include "guild/common/types.h"

namespace guild::play {
namespace scene_recon2 {

// ---------------------------------------------------------------------------
// Recovered rodata constants (decoded with get_bytes).
// ---------------------------------------------------------------------------
namespace scene_const {
    // flt_6476FC[season] — chimney-smoke START hour (RunMainFrameLoop).
    //   bytes 6476FC: 8.0 7.0 8.0 9.0
    constexpr float kSmokeStartHour[4] = {8.0f, 7.0f, 8.0f, 9.0f};
    // flt_64770C[season] — chimney-smoke END hour.
    //   bytes 64770C: 20.0 21.0 20.0 19.0
    constexpr float kSmokeEndHour[4]   = {20.0f, 21.0f, 20.0f, 19.0f};

    // dbl_62109C = -1.0 (ActivateAndRefreshCharacters "no family pos" sentinel).
    constexpr double kNoFamilyPos = -1.0;
    // dbl_6210A4 = 0.01 — per-cloud-cover brightness step.
    constexpr double kBrightnessStep = 0.01;

    // SyncObjectHeights random base offsets:
    //   dbl_620F1C = 0.2  (default branch),  dbl_620F24 = 0.5 (type 23/37 branch)
    constexpr double kHeightRandBaseDefault = 0.2;
    constexpr double kHeightRandBaseTall    = 0.5;

    // dword_4FFAF8 = {0x20,0x1f,0x1e,0x00} — SyncDecorObjects season list,
    // {32,31,30} NUL-terminated. Iterated until a zero byte.
    constexpr u8 kDecorSeasonList[4] = {0x20, 0x1f, 0x1e, 0x00};

    // dword_4FFAFC (29 bytes) — SyncCityBuildings per-type gate table (v127).
    constexpr u8 kCityBuildGate[29] = {
        0,0,3,0,1,1,0,1,1,1,0,2,2,2,1,0,2,1,1,1,1,1,1,0,0,0,0,3,3
    };

    // RunMainFrameLoop magic frame-loop tag (VIBE_GameLogic_RunFrameLoop arg0).
    constexpr i32 kFrameLoopTag = 425983;
}

// ---------------------------------------------------------------------------
// SceneHooks — inert-default vtable for every coupled leaf. The pure
// orchestrators below call ONLY through this. A headless build leaves these at
// their inert defaults; the live game build overrides them to drive the real
// subsystems. Argument shapes mirror the decompile (opaque handles as i32).
// ---------------------------------------------------------------------------
struct SceneHooks {
    // --- time / clock ---
    // 0x58339c VIBE_GameTime_GetSeasonFromDay(&qword_13CE852) -> season (0..3+).
    u8  (*GetSeasonFromDay)()                         = nullptr;
    // qword_13CE852 game time-of-day hour (WORD2 in decompile) for smoke gate.
    float (*GetTimeOfDayHour)()                        = nullptr;
    // 0x527778 VIBE_Clock_ComputeGameTimeOfDay proc id + 0x44e3ac SetProcInterval
    void (*SetClockProcInterval)(i32 active)           = nullptr;
    bool (*IsClockProcActive)()                        = nullptr;

    // --- frame driver ---
    // 0x4c09a0 VIBE_GameLogic_RunFrameLoop(tag,...) -> keep-running bool.
    bool (*RunFrameLoop)(i32 tag)                      = nullptr;

    // --- floor / terrain ---
    void (*FloorComputeSlopeFlags)()                   = nullptr;  // 0x5bbdb0
    void (*HeightmapFreeAndRebuild)()                  = nullptr;  // 0x5c6438/0x5c5610

    // --- camera movers ---
    void (*CameraUpdate)()                             = nullptr;  // 0x4b4c68
    void (*CameraZoomReset)(i32 obj)                   = nullptr;  // 0x4b5250
    void (*CameraZoomIn)(i32 obj)                      = nullptr;  // 0x4b4e24
    void (*CameraAnchorToTerrain)(i32 rec, i32 zbits)  = nullptr;  // 0x4b2900

    // --- audio ---
    void (*AmbientStartMarketLoop)()                   = nullptr;  // 0x582858
    void (*AmbientStopMarketLoop)()                    = nullptr;  // 0x5828bc
    void (*Sound3dUpdateListener)()                    = nullptr;  // 0x50f028
    void (*Sound3dDetachObjectSfx)()                   = nullptr;  // 0x505b74 (walk cb)
    void (*AudioStopVoice)(i32 handle, i32 flag)       = nullptr;  // 0x447508

    // --- script VM ---
    i32  (*ScriptFindByHandle)(i32 handle)             = nullptr;  // 0x442174
    void (*ScriptFinish)(i32 script)                   = nullptr;  // 0x443f38
    void (*ScriptFindActiveByHandle)(i32 handle)       = nullptr;  // 0x4ba284

    // --- objects / characters / scene graph ---
    void (*SceneGraphTraverse)(i32 op, i32 a)          = nullptr;  // 0x5ac86c
    i32  (*SceneGraphWalkAndInvoke)(i32 op)            = nullptr;  // 0x5ac738
    void (*ObjectSpawnChimneySmoke)(i32 person)        = nullptr;  // 0x4b60a0
    void (*WeatherApplySeasonalMeshes)()               = nullptr;  // 0x505df4
    void (*ObjectUpdateBuildingVisualState)(i32 enter) = nullptr;  // 0x506b68
    void (*DayCycleUpdateBrightness)()                 = nullptr;  // 0x4b2504
    bool (*RenderPresentSceneAndClearFlags)(i32 mode)  = nullptr;  // 0x5b499c
    void (*CharActionCancelForObject)()                = nullptr;  // 0x40c3c8

    // --- person/object query iterators (modeled as a flat list) ---
    // 0x586c20 VIBE_Person_QueryBegin / 0x586a6c VIBE_Person_IterNext.
    // Returns an opaque person handle, 0 == end of iteration.
    i32  (*PersonQueryBegin)(i32 cls, i32 sub, i32 key) = nullptr;
    i32  (*PersonIterNext)()                            = nullptr;
    // 0x149 (+0x95) is the script handle field on a person; expose r/w.
    i32  (*PersonGetScriptHandle)(i32 person)           = nullptr;
    void (*PersonSetScriptHandle)(i32 person, i32 h)    = nullptr;

    // --- building interaction (RunMainFrameLoop tail) ---
    bool (*BuildingIsProductionType)(i32 b)             = nullptr;  // 0x587f80
    bool (*BuildingIsStorageType)(i32 b)                = nullptr;  // 0x587f50
    void (*BuildingEnterForeignShop)(i32 b)             = nullptr;  // 0x51e88c
    void (*BuildingEnterAndDispatch)(i32 b)             = nullptr;  // 0x51defc
    void (*BuildingTeleportPlayerToJail)()              = nullptr;  // 0x50f698
    void (*BuildingOpenBauenWindow)()                   = nullptr;  // 0x50de7c
    i32  (*InteractionInvokeHandlerSlot60)(i32 op, i32 b) = nullptr;// 0x595e74
    void (*MarketStallRouteContact)()                   = nullptr;  // 0x519918
    void (*PanelRunChooseWappen)()                      = nullptr;  // 0x551404

    // --- HUD / selection (RunMainFrameLoop teardown) ---
    void (*WidgetSetTooltipText)()                      = nullptr;  // 0x421a24
    void (*HudSetStatusBannerText)()                    = nullptr;  // 0x4bcdcc
    void (*SelectionReset)()                            = nullptr;  // 0x4b9444

    // --- scene load / fade ---
    bool (*SceneLoadFromStream)(const char* path)       = nullptr;  // 0x5e7e38
    void (*FadeRunUntilDone)(bool gebaeude)             = nullptr;  // 0x41f0e8 loop
    void (*UniverseSwitchActiveSlot)(i32 slot)          = nullptr;  // 0x5b4a24
    void (*ObjectInitParticleEmitters)()                = nullptr;  // 0x500174 (walk)
    void (*CharacterResolveMeshSelf)()                  = nullptr;  // 0x4014f8
    i32  (*CharacterFindFreeSlot)()                     = nullptr;  // 0x4266f4
    void (*ErrorLogReportMessage)(const char* msg)      = nullptr;  // 0x438da8

    // --- command / network packet subsystem (Sync* + SyncCityBuildings) ---
    void (*CommandQueueRequest17)(i32 a,i32 b,i32 c,i32 d) = nullptr;// 0x49465c
    void (*CommandBeginDeltaPacket)(i32 obj)              = nullptr; // 0x493a94
    void (*CommandAppendDeltaField)(u32 sz,u32 cnt)       = nullptr; // 0x493aec
    void (*CommandQueueRequestState22)()                  = nullptr; // 0x494750
    i32  (*CommandQueueRequestQuad56)(i32 a,i32 b)        = nullptr; // 0x495098
    void (*CommandQueueRequestPair57)(i32 a,i32 b)        = nullptr; // 0x495100
    i32  (*CommandQueueRequestGuardTarget61)(i32 o,i32 m) = nullptr; // 0x49514c
    void (*CommandQueueRequestCoord27)(i32 a,i32 b,i32 d) = nullptr; // 0x494878
    void (*CommandQueueRequestSlotReset28)()              = nullptr; // 0x4948c8
    void (*CommandEnqueueCmd15)(i32 a,i32 amt)            = nullptr; // 0x494604
    void (*CommandQueueRequest16)(i32 a,i32 amt)          = nullptr; // 0x494630
    bool (*CommandGetPacketStatusById)(i32 id)            = nullptr; // 0x4939d4
    i32  (*CommandGetPacketSeqById)(i32 id)               = nullptr; // 0x4939fc
    void (*AmtRefreshGuildState)()                        = nullptr; // 0x4becdc
    void (*CommandMarkSyncRangeStart)()                   = nullptr; // 0x493a1c
    void (*CommandMarkSyncRangeEnd)()                     = nullptr; // 0x493a28
    bool (*CommandCheckSyncRangeAcked)()                  = nullptr; // 0x493a34
    void (*CommandFlushSendQueue)()                       = nullptr; // 0x4934cc
    void (*CommandReceiveAndQueue)()                      = nullptr; // 0x493ebc
    void (*CommandExecCommands)()                         = nullptr; // 0x494088
    void (*CommandSyncSceneObjectStates)()                = nullptr; // 0x500c38
    void (*LoadingUpdateProgressBar)()                    = nullptr; // 0x52effc

    // --- gameobject helpers (decor / workshop / world sync) ---
    i32  (*GameObjectQueryFind)(i32 handle, i32 want)     = nullptr; // 0x5857fc
    i32  (*GameObjectAddObjekt)(i32 src, i32 kind)        = nullptr; // 0x585af4
    i32  (*MathRandomModulo)(u32 m)                       = nullptr; // 0x58b89c

    // --- SyncWorldOnEnter extras ---
    void (*UtilRandSeed)()                                = nullptr; // 0x5cb8e0
    void (*GameLogicSetupHomeSweetHome)()                 = nullptr; // 0x52aa34
    void (*CommandSyncCharSlotAssignments)(i32 cnt)       = nullptr; // 0x501064
    void (*SceneSyncCityBuildings)()                      = nullptr; // 0x502568 (self)
    i32  (*PersonQueryByGoodType)(i32 t)                  = nullptr; // 0x5929f0

    // --- SyncCityBuildings tail leaves ---
    void (*SceneSyncBuildingAndOffices)()                 = nullptr; // 0x501274
    void (*CityTickStatsAndBroadcast)()                   = nullptr; // 0x57919c
    void (*MeisterAiRequestCmd109)()                      = nullptr; // 0x4c7164
    void (*MeisterAiRequestCmd107)()                      = nullptr; // 0x4c71f0
    void (*MeisterAiRequestCmd122)()                      = nullptr; // 0x4c727c
    void (*MeisterAiRequestBuildingCmd43)()               = nullptr; // 0x4c7308
};

// ---------------------------------------------------------------------------
// SceneState — the modeled subset of the global engine state these
// orchestrators read/write. Field names map to the gilde.exe globals; values
// default to the static-image defaults (mostly 0).
// ---------------------------------------------------------------------------
struct SceneState {
    // RunMainFrameLoop gates
    i32  dword_634498        = 0;   // slope-flags dirty flag
    u16  word_63C740         = 0;   // scene mode bitmask (bits 1,4,8,0x80)
    u16  word_63CC5C         = 0;   // active player index
    i32  dword_63CC2C        = 0;   // wappen/zoom gate
    i32  dword_672224        = 0;   // building-zoom gate
    i32  dword_672234        = 0;
    i32  dword_631730        = 0;   // selected building
    i32  dword_62D4E8        = 0;
    i32  dword_75BF38        = -1;
    i32  dword_62D22C        = 0;
    i32  dword_63178C        = 0;
    i32  dword_11BC278       = 0;   // pending building-enter request
    i32  dword_11BC27C       = 0;   // pending building-enter request (2)
    i32  dword_11BC280       = 0;
    i32  dword_11BC284       = 0;
    i32  dword_6477A4        = 0;   // market stall contact
    i32  dword_67222C        = 0;
    i32  dword_67221C        = 0;
    u16  word_62D310         = 0;
    u8   byte_642008         = 0;   // "in city loop" flag
    u8   byte_63CC40         = 0;   // "in city loop" flag (2)

    // ActivateAndRefreshCharacters / GameState reset
    i32  dword_649D60        = 0;   // active universe slot (0 == default)
    i32  dword_6344A0        = 0;   // active voice count
    i32  dword_634494        = -1;  // active intro script handle
    i32  dword_62D080        = -1;
    i32  dword_64A05C        = 0;
    i32  dword_64A054        = 0;
    u8   byte_634484         = 0;   // committed season
    u8   byte_64A01C         = 0;   // present-mode selector
    float flt_64A018         = 1.0f;// brightness scalar
    u8   byte_123351C        = 0;   // cloud cover units

    // SyncWorldOnEnter
    i32  dword_764CE0        = -1;  // rng-seed sentinel
    i32  dword_63C794        = 0;   // home-sweet-home gate
    u8   byte_63CC1D         = 0;

    // SyncCityBuildings gates
    i32  dword_63C7A8        = 0;   // "place/wage missing buildings" gate
    u8   byte_63C8F4         = 0;   // tutorial-step selector (==5 spawns hint)
};

// ===========================================================================
// Orchestrators (1:1).
// ===========================================================================

// 0x506d34 — VIBE_GameState_ResetVoicesAndScript.
// Detaches per-object 3D sfx, stops all active voices, resets render limits,
// finishes the active intro script, cancels char actions.
void GameState_ResetVoicesAndScript(SceneState& s, const SceneHooks& h);

// 0x506df4 — VIBE_Scene_ActivateAndRefreshCharacters.
// Anchors the camera, refreshes/sets visible every live character mesh, applies
// seasonal foliage/scaffold hides, weather meshes, brightness, then presents.
// Returns the season byte (or the early-out season when slot != 0).
u8 Scene_ActivateAndRefreshCharacters(SceneState& s, const SceneHooks& h);

// 0x50f0c0 — VIBE_Scene_RunMainFrameLoop. The in-city play loop. Returns the
// final RunFrameLoop result (0).
i32 Scene_RunMainFrameLoop(SceneState& s, const SceneHooks& h);

// ===========================================================================
// Scene (re)build pipeline (1:1 orchestration; entity bodies via hooks).
// ===========================================================================

// 0x500270 — VIBE_Scene_LoadObjektScene. Loads "scenes/*ob_<name>.ed3" into a
// universe slot (reusing the cached slot when present). Returns load success.
bool Scene_LoadObjektScene(SceneState& s, const SceneHooks& h,
                           const char* name, i32 cachedSlot /*-1 == none*/);

// 0x5006a8 — VIBE_Scene_LoadGebaeudeScene. Loads "scenes/*gb_<name>.ed3".
bool Scene_LoadGebaeudeScene(SceneState& s, const SceneHooks& h,
                             const char* name, i32 cachedSlot /*-1 == none*/);

// 0x501c34 — VIBE_Scene_SyncDecorObjects. For each decor-season in
// kDecorSeasonList, walks people of that season and emits the per-season decor
// command bundle (ids 439..467). Returns 0 (last iter handle low word).
i32 Scene_SyncDecorObjects(const SceneHooks& h);

// 0x501f1c — VIBE_Scene_SyncWorkshopProduction. For each profession-71 person,
// counts the 16 active production slots in two tables (475 / 476) and emits the
// delta + per-slot request17 pairs.
void Scene_SyncWorkshopProduction(const SceneHooks& h);

// 0x504e14 — VIBE_Scene_SyncObjectHeights. For 62 work-slots of building a1,
// rolls a random height and queues a coord request.  buildingIndex == a1.
void Scene_SyncObjectHeights(const SceneHooks& h, i32 buildingIndex);

// 0x504ef8 — VIBE_Scene_SyncMovableObjects. Walks all 256 building records and
// for movable categories emits the position/guard/request bundle.
// NOTE: original is __noreturn (the for-loop has no exit condition — it walks a
// fixed-stride array off the end and faults / is never entered live). The
// reconstruction takes a record count so it terminates; faithfulness of the
// PER-RECORD body is preserved.
void Scene_SyncMovableObjects(const SceneHooks& h, i32 recordCount);

// 0x50456c — VIBE_Scene_SyncWorldOnEnter. Top-level world (re)sync on entering
// a city: seed rng, sync object states, optional HomeSweetHome, char-slot
// assignment delta, city buildings, and a stock-up request. Returns 1.
i32 Scene_SyncWorldOnEnter(SceneState& s, const SceneHooks& h,
                           i32 cityIndex, i32 charSlotDelta, bool foundType6);

// 0x5036bc — VIBE_Scene_SpawnBuildingMesh. Walks the scene tree for meshes
// matching <name>, accumulates a bbox into the dword_1234600 table, then spawns
// and reparents a stand-in object per match. Returns last op result.
// Pure bbox-merge math is reconstructed; mesh/scene-graph access via hooks.
u8 Scene_SpawnBuildingMesh(const SceneHooks& h, const char* name);

// Pure bbox-merge kernel used by SpawnBuildingMesh: min/max over 8 source
// vertices (stride 20 floats / 80 bytes; merge loop v10+80..v10+640
// @0x503821..0x503977), seeded from vertex 0. Exposed for testing.
void SpawnBuildingMesh_MergeBBox(const float* srcVerts /*[8*20]*/,
                                 float outMin[3], float outMax[3]);

// 0x5e860c — VIBE_Scene_SaveObjectGroup. Writes an object group to <path>:
// validates all roots share a parent, optionally re-parents, then serializes a
// magic + count + each object. Pure validation/branch logic; IO via hooks.
struct SaveObjectGroupIO {
    void (*ReportError)(const char* msg)          = nullptr; // 0x438da8
    i32  (*GetParent)(i32 obj)                     = nullptr; // *(obj+504)
    void (*UnlinkFromList)(i32 obj)                = nullptr; // 0x5b23ec
    void (*SetParent)(i32 obj)                     = nullptr; // 0x5b0b9c
    i32  (*OpenFile)(const char* path, i32 parent) = nullptr; // 0x450bc8
    void (*WriteDwordPair)(i32 f, i32 v)           = nullptr; // 0x5dcac0
    void (*WriteObject)(i32 f, i32 obj)            = nullptr; // 0x5e5ab4
    void (*CloseStream)(i32 f)                     = nullptr; // 0x451354
};
void Scene_SaveObjectGroup(const SaveObjectGroupIO& io, const char* path,
                           const i32* objs, i32 count, i32 sharedParent);

// 0x5e872c — VIBE_Scene_HandleDebugKeyToggle. Translates a debug keycode
// (byte_67225C, edge-detected against byte_64A7A3) into a renderer/scene toggle.
// Returns 1 only for keycode 0x23 (engine-enable toggle), else 0.
struct DebugKeyState {
    u8  byte_64A7A3  = 0;   // last-seen key (edge latch)
    u8  byte_67225C  = 0;   // current key
    u8  byte_64A351  = 0;   // z-enable shadow
    u8  byte_649D70  = 0;   // engine-enable shadow
    u32 dword_649D7D = 0;   // wireframe toggle
    u8  byte_1408A6D = 0;   // shadow-mode sub-toggle
    i32 trackTarget    = 0; // dword_13FD45C[0]
    i32 selectedObject = 0; // dword_13FCD1C (current selection)
    i32 renderState[3] = {0,0,0}; // v10/v11/v12 render-state words (modeled)
};
struct DebugKeyHooks {
    void (*RenderSetZEnable)(bool on)        = nullptr; // 0x5de6e4
    void (*RenderSetEngineEnabled)(bool on)  = nullptr; // 0x5af8e4
    void (*LightRefreshAllObjects)()         = nullptr; // 0x5c886c
    void (*SceneGraphTraverseShadow)()       = nullptr; // 0x5ac86c + 0x5f4880
    void (*ObjectInvalidateCurrent)()        = nullptr; // 0x5af2e4
    void (*RenderApplyRenderStates)()        = nullptr; // 0x5dda1c
    i32  (*TrackTargetWalk)()                = nullptr; // tab-cycle scene walk
};
u8 Scene_HandleDebugKeyToggle(DebugKeyState& s, const DebugKeyHooks& h);

// 0x502568 — VIBE_Scene_SyncCityBuildings. PARTIAL (see header note). Only the
// recoverable orchestration skeleton: the per-bucket Light_SetGrayColorThunk
// zero-seed, the candidate-building scan (type in {5,6,7}), the season-gate
// table (kCityBuildGate), the three SyncRange begin/flush/end barriers, and the
// fixed tail call sequence (SyncBuildingAndOffices / City_TickStatsAndBroadcast
// / MeisterAi_Request*). Inner command emission is delegated to hooks; the body
// statements that depend on uninitialized decompiler temporaries are omitted.
void Scene_SyncCityBuildings(SceneState& s, const SceneHooks& h);

// ---------------------------------------------------------------------------
// Pure helper exposed for testing: chimney-smoke time gate.
// In RunMainFrameLoop:  start <= hour < end  (per-season thresholds).
// ---------------------------------------------------------------------------
bool SmokeTimeGate(u8 season, float hour);

} // namespace scene_recon2
} // namespace guild::play

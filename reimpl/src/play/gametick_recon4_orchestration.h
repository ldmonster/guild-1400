#pragma once
// ===========================================================================
//  gametick_recon4_orchestration.h
//
//  Turn / round / universe-slot / shutdown ORCHESTRATION reconstruction.
//
//  Cluster (gilde.exe, imagebase 0x400000):
//    GameTick:
//      0x4c0750  VIBE_GameTick_RunAdvanceGameDialog
//      0x52f66c  VIBE_GameTick_BeginRound          (a.k.a. main_RundenBeginn)
//      0x579530  VIBE_GameTick_RequestStartTurn
//      0x57957c  VIBE_GameTick_AdvanceTurnTimer
//    Universe (slot suspend/resume + script pass + destroy):
//      0x5b3030  VIBE_Universe_DisplayLogAndCleanup  (slot SUSPEND: free GPU geom)
//      0x5b3410  VIBE_Universe_InitLogAndInflate     (slot RESUME : inflate geom)
//      0x5b3628  VIBE_Universe_RunObjectScriptPass
//      0x5b5050  VIBE_Universe_DestroySlot
//    Game teardown (shutdown ORDER is the orchestration sequence):
//      0x5278cc  VIBE_Game_ShutdownSubsystems        (inner, called first)
//      0x52794c  VIBE_Game_ShutdownAllSubsystems     (outer, app exit)
//      0x52f44c  VIBE_Game_ShutdownWorldAndSubsystems(world/level teardown)
//
//  This unit reconstructs the *control flow / state transitions / teardown
//  ORDER* exactly. The many coupled engine leaves (audio bank unloads, mesh
//  free, scene-graph traversal, geometry inflate/free, net close, ...) are
//  not reachable headless; they are routed through an INERT-DEFAULT hook
//  table so the sequence is observable and testable without faking behavior.
//
//  NOTE on "Inflate" (rule 6): VIBE_Object_InflateGeometry @0x5b30d4 and
//  VIBE_Floor_AllocInflateBuffers @0x5bce10 are NOT a zlib/LZ codec. The
//  string literals are "d3:InflateObject(allpolys/allpoints/mat)" — this is
//  in-engine *geometry expansion* (unpacking a packed mesh record into
//  runtime point/poly/material arrays). No third-party compression library
//  is involved, so no rule-6 substitution is needed; it is a normal engine
//  leaf behind the hook.
// ===========================================================================

#include "guild/common/types.h"

namespace guild {
namespace play {

using guild::u8;
using guild::u16;
using guild::i32;
using guild::u32;

// ---------------------------------------------------------------------------
// Recording hook table. Every coupled engine leaf is a function pointer here.
// Defaults are inert (record-only / no-op) so the orchestration runs headless.
// Tests bind recording stubs to verify SEQUENCE and ORDER.
// ---------------------------------------------------------------------------
struct GameTickRecon4Hooks {
    // --- shutdown leaves (in their call order; see cpp provenance) ---
    void (*configWriteGfxSettings)()        = nullptr; // 0x56af54
    void (*audioUnloadSampleBank)(i32 bank) = nullptr; // 0x446e4c
    void (*scriptShutdownEngine)()          = nullptr; // 0x445288
    void (*cutsceneUnregisterTickProc)()    = nullptr; // 0x4ad4fc
    void (*soundLibShutdown)()              = nullptr; // 0x445ea4
    void (*sound3dFreePool)()               = nullptr; // 0x4245c4
    void (*soundShutdown)()                 = nullptr; // 0x439ccc
    void (*audioShutdown49878)()            = nullptr; // 0x449878
    void (*soundWaveFreeTables)()           = nullptr; // 0x424eb4
    void (*textFreeAllTextFiles)()          = nullptr; // 0x44d8a4
    void (*gameObjectFreeAllTables)()       = nullptr; // 0x583ab0
    void (*charActionQueueShutdown)()       = nullptr; // 0x40c07c
    void (*commandQueueResetAlt)()          = nullptr; // 0x4933c0

    void (*widgetShutdownSystem)()          = nullptr; // 0x4201f4
    void (*gameStateFreeAllResources)()     = nullptr; // 0x40e308
    void (*universeSwitchActiveSlot)(i32 s) = nullptr; // 0x5b4a24
    void (*tableResetLightmaps)()           = nullptr; // 0x42e19c
    void (*renderShutdownEngine)()          = nullptr; // 0x5b0228
    void (*inputDirectInputShutdown)()      = nullptr; // 0x40cd40
    void (*timeBaseStopTimer)()             = nullptr; // 0x44e2c4
    void (*vfsShutdown)()                   = nullptr; // 0x452004
    void (*memPoolShutdownStack)()          = nullptr; // 0x44e544
    void (*memoryShutdownTracker)()         = nullptr; // 0x439640
    void (*errorLogShutdown)()              = nullptr; // 0x438c0c
    void (*pluginShutdownAndFree)()         = nullptr; // dword_63C76C + FreeLibrary
    void (*windowDestroyAndUnregister)()    = nullptr; // 0x527868

    // --- world-teardown leaves (in their call order) ---
    void (*statusTextClearTable)()          = nullptr; // 0x4bcc30
    void (*widgetSetTooltipText)()          = nullptr; // 0x421a24
    void (*hudSetStatusBannerText)()        = nullptr; // 0x4bcdcc
    void (*timeBaseUnregisterProc)()        = nullptr; // 0x44e370
    void (*scriptFreeFinished)()            = nullptr; // 0x445370
    void (*meshReleaseStockObject)()        = nullptr; // 0x5d3668
    void (*voiceQueueFlushAll)()            = nullptr; // 0x57ee40
    void (*sound3dStopAll)()                = nullptr; // 0x424890
    void (*historyResetChronicleState)()    = nullptr; // 0x4fd090
    void (*objectResetStateAlt)()           = nullptr; // 0x53841c
    void (*heTickActiveHandlers)()          = nullptr; // 0x4c52d8
    void (*eventPanelDestroyBar)()          = nullptr; // 0x4c57b0
    void (*heUpdateSubsystems)()            = nullptr; // 0x4c5370
    void (*groundplanDestroyWindow)()       = nullptr; // 0x4ae970
    void (*buildingResetAllBuildings)()     = nullptr; // 0x5896fc
    void (*worldResetPersonTable)()         = nullptr; // 0x58389c
    void (*worldRelinkObjectOwners)()       = nullptr; // 0x5838d4
    void (*characterDestroy)(i32 idx)       = nullptr; // 0x402120
    void (*snowUpdateScene)()               = nullptr; // 0x42a2cc
    void (*rainDestroy)()                   = nullptr; // 0x429290
    void (*skyRemoveLayer)(i32 layer)       = nullptr; // 0x5efb14
    void (*skyDestroy)()                    = nullptr; // 0x5efda0
    void (*lightApplyAmbient)()             = nullptr; // 0x42e0b0
    void (*objectDestroySpawnedEntities)()  = nullptr; // 0x4fff10
    void (*inventoryDestroyGridSurface)()   = nullptr; // 0x5513d8
    void (*animalFreePool)()                = nullptr; // 0x4835ec
    void (*commandQueueReset)()             = nullptr; // 0x493308
    void (*netCloseBroadcastSocket)()       = nullptr; // 0x43ab80
    void (*netDisconnect)()                 = nullptr; // 0x43b868
    void (*dragCursorSetSprite)()           = nullptr; // 0x41fcbc

    // --- universe slot suspend/resume/destroy leaves ---
    void (*floorFreeTileBuffers)()          = nullptr; // 0x5bccf8 (suspend)
    void (*sceneTraverseFreeMesh)()         = nullptr; // 0x5ac86c + 0x5b2ef8
    void (*floorAllocInflateBuffers)()      = nullptr; // 0x5bce10 (resume)
    void (*sceneTraverseInflateGeom)()      = nullptr; // 0x5ac86c + 0x5b30d4
    void (*heightmapBuildTerrainMesh)()     = nullptr; // 0x5c5610
    // script pass:
    void (*sceneWalkRunScript)(u8 first)    = nullptr; // 0x5ac738 + 0x5b34e4
    void (*textureUploadAllRecords)()       = nullptr; // 0x5db4b8
    void (*lightRefreshAllObjects)()        = nullptr; // 0x5c886c
    // destroy slot:
    void (*universeResetCurrentSlot)(i32 s) = nullptr; // 0x5b44c4
    void (*objectDispose)()                 = nullptr; // 0x5b0790
    void (*renderFreeObjectNode)()          = nullptr; // 0x5e0f30
    void (*floorFreeBuffers)()              = nullptr; // 0x5ba508
    void (*memoryFreeDebug)()               = nullptr; // 0x43923c

    // --- AdvanceGameDialog leaves ---
    void (*interactionDispatchPanelEvent)(i32 op, i32 close) = nullptr; // 0x595b98
    i32  (*gameTickFinalize)()              = nullptr; // 0x41beb8 -> form handle
    void (*formCenterChildWindows)(i32 f)   = nullptr; // 0x41d6ac
    void (*textRenderRichString)()          = nullptr; // 0x59d6e8
    void (*formDestroy)(i32 f)              = nullptr; // 0x41da04
    // RunFrameLoop returns nonzero to keep looping (caller drives count).
    u8   (*gameLogicRunFrameLoop)()         = nullptr; // 0x4c09a0
    u8   (*interactionTestHandlerFlag)(i32 mask) = nullptr; // 0x595ed4
};

// ===========================================================================
//  Universe slot state (subset reconstructed for the suspend/resume control).
//    byte_649DD0 — current "suspend bits" written by both ops (low 2 bits)
//    dword_64A028 — floor/tile object (0 => floor work skipped)
//    dword_64A048 — heightmap object
//    dword_64A7C8 — sky object
//    dword_649D60 — active slot index
// ===========================================================================
struct UniverseState {
    i32 floorObj      = 0;  // dword_64A028
    i32 heightmapObj  = 0;  // dword_64A048
    i32 skyObj        = 0;  // dword_64A7C8
    i32 activeSlot    = 0;  // dword_649D60
    u8  suspendByte   = 0;  // byte_649DD0
    // slot table flags (dword_13ECF48[246*slot]); 1 => slot allocated.
    u8  slotAllocated[64] = {};
    i32 slotMemory[64]    = {}; // dword_13ED290[246*slot]
};

// flag bits used by InitLogAndInflate / DisplayLogAndCleanup (a1 high byte)
enum : u8 {
    kSlotFlagFloor = 0x02,   // (flag>>24 & 0x02000000) tested as bit25 of full word
    kSlotFlagGeom  = 0x01,   // bit24
};

// ---------------------------------------------------------------------------
// 0x5b3030 — DisplayLogAndCleanup: SUSPEND a universe slot.
//   flag bit25 set && floorObj -> free tile buffers
//   flag bit24 set            -> traverse scene, free attached mesh buffers
//   byte_649DD0 = flag
// Returns 1 (al). `flag` is the original high byte of __usercall al-arg.
// ---------------------------------------------------------------------------
u8 Universe_DisplayLogAndCleanup(UniverseState& st, u8 flag,
                                 const GameTickRecon4Hooks& hk);

// ---------------------------------------------------------------------------
// 0x5b3410 — InitLogAndInflate: RESUME a universe slot.
//   flag bit25 set && floorObj -> alloc inflate buffers
//   flag bit24 set            -> traverse scene, inflate geometry
//   byte_649DD0 = (~flag & 3)
//   if ((~flag&3)==0 && heightmapObj && !floorObj) -> build terrain mesh
// Returns 1 (al).
// ---------------------------------------------------------------------------
u8 Universe_InitLogAndInflate(UniverseState& st, u8 flag,
                              const GameTickRecon4Hooks& hk);

// ---------------------------------------------------------------------------
// 0x5b3628 — RunObjectScriptPass: two scene-graph walks (first=1 then first=0),
//   then upload all textures, then refresh all lights. Returns light-refresh al.
// ---------------------------------------------------------------------------
u8 Universe_RunObjectScriptPass(const GameTickRecon4Hooks& hk);

// ---------------------------------------------------------------------------
// 0x5b5050 — DestroySlot: free a universe slot.
//   slot >= 0x40                  -> return 0
//   slot == activeSlot            -> ResetCurrentSlot(slot); return 1
//   !slotAllocated[slot]          -> return 1 (nothing to do)
//   otherwise: switch-in slot, dispose objects, free render nodes, free floor,
//              destroy sky, free slot memory, clear suspend byte, switch back.
//   Returns 1 (al).
// ---------------------------------------------------------------------------
u8 Universe_DestroySlot(UniverseState& st, u32 slot,
                        const GameTickRecon4Hooks& hk);

// ===========================================================================
//  GameTick — turn timer state. 36-byte block at dword_1235238.
//   [0] phase flag (1 == "running")     [5] f: accumulator
//   [1] turn counter                    [6] f: threshold
//   [2] timestamp (game-time lo)        [7] pending event type (1..3)
//   [3] f: base increment               [8] last event category (0..4)
//   [4] f: per-turn rate
//  dword_1235254 is a *forced-event* selector (0 => normal random path).
// ===========================================================================
struct TurnTimerState {
    i32   phase     = 1;     // [0]  dword_1235238 (1 == turn active)
    i32   counter   = 0;     // [1]
    i32   timestamp = 0;     // [2]
    float base      = 0.0f;  // [3]
    float rate      = 0.0f;  // [4]
    float accum     = 0.0f;  // [5]
    float threshold = 1.0f;  // [6]
    i32   pending   = 0;     // [7]
    i32   category  = 0;     // [8]
    i32   forced    = 0;     // dword_1235254
};

// Deterministic RNG hooks injected so the timer math is reproducible in tests.
struct TurnTimerEnv {
    // 0x58b910 VIBE_Math_RandomFloatScaled -> [0,1)
    float (*randomFloatScaled)() = nullptr;
    // 0x58b89c VIBE_Math_RandomModulo(m) -> [0,m)
    u16   (*randomModulo)(u32 m) = nullptr;
    // 0x4c63f8 VIBE_He_FindFirstHandlerByFilter(1,0,kind) -> nonzero if a
    // matching handler is already active (suppresses re-fire of forced event).
    i32   (*heFindHandler)(i32 kind) = nullptr;
    // emits the chosen event of type `kind` (89/78/80). Records what fired.
    void  (*emitEvent)(i32 kind) = nullptr;
    // 0x495a90 VIBE_Command_RequestBuildOp86 — flush the timer state command.
    void  (*requestBuildOp86)(i32 tag) = nullptr;
};

// The three 16-entry weight tables (gilde.exe 0x577954/0x577994/0x5779d4).
// Indexed by RandomModulo(16); value+1 becomes the pending event type 1..3.
extern const i32 kEventWeightTableA[16]; // 0x577954
extern const i32 kEventWeightTableB[16]; // 0x577994
extern const i32 kEventWeightTableC[16]; // 0x5779d4

// ---------------------------------------------------------------------------
// 0x57957c — AdvanceTurnTimer. Returns true if a command was flushed (i.e. the
// function ran past the early `phase != 1` guard). `outFired` (if non-null) is
// set to the event kind emitted this call (0 if none).
// ---------------------------------------------------------------------------
bool GameTick_AdvanceTurnTimer(TurnTimerState& st, const TurnTimerEnv& env,
                               i32* outFired = nullptr);

// ---------------------------------------------------------------------------
// 0x579530 — RequestStartTurn: snapshot the 36-byte timer block, zero word[0],
// stamp word[9] with 'turn' tag, flush via RequestBuildOp86.
// Reconstructed as: zero phase, set tag, flush. Returns the tag flushed.
// ---------------------------------------------------------------------------
i32 GameTick_RequestStartTurn(TurnTimerState& st, const TurnTimerEnv& env);

// ---------------------------------------------------------------------------
// 0x4c0750 — RunAdvanceGameDialog. Modal "advance game" dialog: if handler
// flag (mask 8) is set, open panel, run the frame loop until it returns 0,
// then close. Returns the count of frame-loop iterations actually run (the
// original returns the final DispatchPanelEvent al; we expose the loop count
// for testability and also return that al via `outDispatchResult`).
// ---------------------------------------------------------------------------
i32 GameTick_RunAdvanceGameDialog(const GameTickRecon4Hooks& hk,
                                  u8* outDispatchResult = nullptr);

// ===========================================================================
//  Shutdown teardown — the ORCHESTRATION SEQUENCE. Each returns nothing; the
//  ORDER of hook invocations is the reconstructed behavior.
//
//  Flags controlling conditional branches (mirror the engine globals):
// ===========================================================================
struct ShutdownFlags {
    bool soundLibActive   = false; // dword_63C900
    bool soundActive      = false; // dword_63C8F8
    bool pluginLoaded     = false; // dword_63C8F0 && hModule
    bool stockMeshLoaded  = false; // dword_63CD3C
    bool snowActive       = false; // dword_11BC1CC
    bool rainActive       = false; // dword_11BC1C8
    bool skyActive        = false; // dword_64A7C8
    bool ambientLightSet  = false; // dword_62D564
    bool sampleBanksBound = false; // dword_63C74C (world banks)
    i32  audioBankCount   = 0;     // # nonzero entries in dword_122EF18[96]
    i32  characterCount   = 0;     // # nonzero entries in dword_66F0D0[512]
};

// 0x5278cc — VIBE_Game_ShutdownSubsystems (inner).
void Game_ShutdownSubsystems(const ShutdownFlags& f, const GameTickRecon4Hooks& hk);

// 0x52794c — VIBE_Game_ShutdownAllSubsystems (outer / app exit).
void Game_ShutdownAllSubsystems(const ShutdownFlags& f, const GameTickRecon4Hooks& hk);

// 0x52f44c — VIBE_Game_ShutdownWorldAndSubsystems (world/level teardown).
void Game_ShutdownWorldAndSubsystems(const ShutdownFlags& f, const GameTickRecon4Hooks& hk);

} // namespace play
} // namespace guild

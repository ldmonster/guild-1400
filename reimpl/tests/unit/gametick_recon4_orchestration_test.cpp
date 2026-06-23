// ===========================================================================
//  gametick_recon4_orchestration_test.cpp
//  Golden tests for the turn/universe/shutdown orchestration reconstruction.
//  Verifies: turn-timer state transitions, RequestStartTurn, AdvanceGameDialog
//  loop, universe slot suspend/resume/destroy, and the shutdown call ORDER.
// ===========================================================================
#include "play/gametick_recon4_orchestration.h"
#include "test.h"

#include <string>
#include <vector>

using namespace guild;
using namespace guild::play;

// ---- global recorder for hook ordering -----------------------------------
namespace {
std::vector<std::string>* g_rec = nullptr;
void Rec(const char* s) { if (g_rec) g_rec->push_back(s); }

// helper macros to make named recording stubs
#define MK(field, name) hk.field = []{ Rec(name); }
}

// ===========================================================================
//  Universe slot suspend / resume
// ===========================================================================
TEST(GameTickRecon4, DisplayLogAndCleanup_freesBothWhenFlagsSetAndFloor) {
    std::vector<std::string> rec; g_rec = &rec;
    GameTickRecon4Hooks hk;
    MK(floorFreeTileBuffers, "floorFree");
    MK(sceneTraverseFreeMesh, "sceneFree");
    UniverseState st; st.floorObj = 1;
    u8 r = Universe_DisplayLogAndCleanup(st, kSlotFlagFloor | kSlotFlagGeom, hk);
    CHECK_EQ(r, (u8)1);
    CHECK_EQ(st.suspendByte, (u8)(kSlotFlagFloor | kSlotFlagGeom));
    CHECK_EQ(rec.size(), (size_t)2);
    CHECK(rec[0] == "floorFree");
    CHECK(rec[1] == "sceneFree");
}

TEST(GameTickRecon4, DisplayLogAndCleanup_skipsFloorWhenNoFloorObj) {
    std::vector<std::string> rec; g_rec = &rec;
    GameTickRecon4Hooks hk;
    MK(floorFreeTileBuffers, "floorFree");
    MK(sceneTraverseFreeMesh, "sceneFree");
    UniverseState st; st.floorObj = 0;             // no floor -> floor branch skipped
    Universe_DisplayLogAndCleanup(st, kSlotFlagFloor | kSlotFlagGeom, hk);
    CHECK_EQ(rec.size(), (size_t)1);
    CHECK(rec[0] == "sceneFree");
}

TEST(GameTickRecon4, InitLogAndInflate_buildsTerrainOnlyWhenInvZeroAndHeightmapNoFloor) {
    std::vector<std::string> rec; g_rec = &rec;
    GameTickRecon4Hooks hk;
    MK(floorAllocInflateBuffers, "floorAlloc");
    MK(sceneTraverseInflateGeom, "geomInflate");
    MK(heightmapBuildTerrainMesh, "terrain");
    // flag=3 -> ~flag&3 == 0; heightmap set; no floor -> terrain built.
    UniverseState st; st.heightmapObj = 1; st.floorObj = 0;
    u8 r = Universe_InitLogAndInflate(st, 0x03, hk);
    CHECK_EQ(r, (u8)1);
    CHECK_EQ(st.suspendByte, (u8)0);               // ~3 & 3 == 0
    // floorObj==0 -> floor branch skipped; geom branch fires (bit0); then terrain.
    CHECK_EQ(rec.size(), (size_t)2);
    CHECK(rec[0] == "geomInflate");
    CHECK(rec.back() == "terrain");
}

TEST(GameTickRecon4, InitLogAndInflate_noTerrainWhenInvNonZero) {
    std::vector<std::string> rec; g_rec = &rec;
    GameTickRecon4Hooks hk;
    MK(heightmapBuildTerrainMesh, "terrain");
    UniverseState st; st.heightmapObj = 1; st.floorObj = 0;
    // flag=1 -> ~1 & 3 == 2 (nonzero) -> no terrain
    Universe_InitLogAndInflate(st, 0x01, hk);
    CHECK_EQ(st.suspendByte, (u8)2);
    CHECK_EQ(rec.size(), (size_t)0);               // terrain never called
}

TEST(GameTickRecon4, RunObjectScriptPass_orderFirstThenSecondThenUploadThenLight) {
    std::vector<std::string> rec; g_rec = &rec;
    GameTickRecon4Hooks hk;
    hk.sceneWalkRunScript = [](u8 first){ Rec(first ? "walk1" : "walk0"); };
    MK(textureUploadAllRecords, "tex");
    MK(lightRefreshAllObjects, "light");
    u8 r = Universe_RunObjectScriptPass(hk);
    CHECK_EQ(r, (u8)1);
    CHECK_EQ(rec.size(), (size_t)4);
    CHECK(rec[0] == "walk1");
    CHECK(rec[1] == "walk0");
    CHECK(rec[2] == "tex");
    CHECK(rec[3] == "light");
}

// ===========================================================================
//  Universe DestroySlot
// ===========================================================================
TEST(GameTickRecon4, DestroySlot_outOfRangeReturnsZero) {
    GameTickRecon4Hooks hk; UniverseState st;
    CHECK_EQ(Universe_DestroySlot(st, 64, hk), (u8)0);
    CHECK_EQ(Universe_DestroySlot(st, 99, hk), (u8)0);
}

TEST(GameTickRecon4, DestroySlot_activeSlotResetsCurrent) {
    std::vector<std::string> rec; g_rec = &rec;
    GameTickRecon4Hooks hk;
    hk.universeResetCurrentSlot = [](i32 s){ Rec(s == 5 ? "reset5" : "resetX"); };
    UniverseState st; st.activeSlot = 5;
    CHECK_EQ(Universe_DestroySlot(st, 5, hk), (u8)1);
    CHECK_EQ(rec.size(), (size_t)1);
    CHECK(rec[0] == "reset5");
}

TEST(GameTickRecon4, DestroySlot_unallocatedReturnsOneNoWork) {
    std::vector<std::string> rec; g_rec = &rec;
    GameTickRecon4Hooks hk;
    MK(objectDispose, "dispose");
    UniverseState st; st.activeSlot = 0; st.slotAllocated[3] = 0;
    CHECK_EQ(Universe_DestroySlot(st, 3, hk), (u8)1);
    CHECK_EQ(rec.size(), (size_t)0);
}

TEST(GameTickRecon4, DestroySlot_fullTeardownOrderAndSwitchBack) {
    std::vector<std::string> rec; g_rec = &rec;
    GameTickRecon4Hooks hk;
    hk.universeSwitchActiveSlot = [](i32 s){ Rec(("switch" + std::to_string(s)).c_str()); };
    MK(objectDispose, "dispose");
    MK(renderFreeObjectNode, "rendfree");
    MK(floorFreeBuffers, "floorfree");
    MK(skyDestroy, "skydestroy");
    MK(memoryFreeDebug, "memfree");
    UniverseState st;
    st.activeSlot = 0;
    st.slotAllocated[7] = 1;
    st.floorObj = 1; st.skyObj = 1; st.slotMemory[7] = 1;
    u8 r = Universe_DestroySlot(st, 7, hk);
    CHECK_EQ(r, (u8)1);
    // switch-in(7), dispose, rendfree, floorfree, skydestroy, memfree, switch-back(0)
    std::vector<std::string> want = {
        "switch7", "dispose", "rendfree", "floorfree", "skydestroy", "memfree", "switch0"
    };
    CHECK_EQ(rec.size(), want.size());
    for (size_t i = 0; i < want.size() && i < rec.size(); ++i)
        CHECK(rec[i] == want[i]);
    CHECK_EQ(st.floorObj, 0);
    CHECK_EQ(st.skyObj, 0);
    CHECK_EQ(st.slotMemory[7], 0);
    CHECK_EQ(st.suspendByte, (u8)0);
    CHECK_EQ(st.activeSlot, 0);                  // restored
}

// ===========================================================================
//  Turn timer
// ===========================================================================
static float g_rf = 0.0f;
static u16   g_rm = 0;
static std::vector<i32>* g_emit = nullptr;

static TurnTimerEnv MakeEnv() {
    TurnTimerEnv env;
    env.randomFloatScaled = []{ return g_rf; };
    env.randomModulo      = [](u32){ return g_rm; };
    env.heFindHandler     = [](i32){ return 0; };  // never suppress by default
    env.emitEvent         = [](i32 k){ if (g_emit) g_emit->push_back(k); };
    env.requestBuildOp86  = [](i32){};
    return env;
}

TEST(GameTickRecon4, AdvanceTurnTimer_earlyReturnWhenPhaseNotOne) {
    TurnTimerState st; st.phase = 0;
    auto env = MakeEnv();
    CHECK(!GameTick_AdvanceTurnTimer(st, env));
}

TEST(GameTickRecon4, AdvanceTurnTimer_accumulatesBelowThresholdNoEvent) {
    std::vector<i32> emit; g_emit = &emit;
    TurnTimerState st;
    st.phase = 1; st.base = 0.1f; st.rate = 0.0f; st.accum = 0.0f; st.threshold = 1.0f;
    auto env = MakeEnv(); g_rf = 0.0f;
    i32 fired = -1;
    bool ran = GameTick_AdvanceTurnTimer(st, env, &fired);
    CHECK(ran);
    CHECK_EQ(fired, 0);
    CHECK(st.accum > 0.099f && st.accum < 0.101f);
    CHECK_EQ(st.counter, 0);
    CHECK_EQ(emit.size(), (size_t)0);
}

TEST(GameTickRecon4, AdvanceTurnTimer_category0FiresKind80AndIncrementsCounter) {
    std::vector<i32> emit; g_emit = &emit;
    TurnTimerState st;
    st.phase = 1; st.base = 2.0f; st.rate = 0.0f; st.accum = 0.0f; st.threshold = 1.0f;
    st.category = 0;
    auto env = MakeEnv(); g_rf = 0.0f;
    i32 fired = 0;
    GameTick_AdvanceTurnTimer(st, env, &fired);
    CHECK_EQ(fired, 80);
    CHECK_EQ(st.counter, 1);
    CHECK_EQ(st.pending, 3);
    CHECK_EQ(st.category, 3);                     // category <- pending
    CHECK_EQ(emit.size(), (size_t)1);
    CHECK_EQ(emit[0], 80);
}

TEST(GameTickRecon4, AdvanceTurnTimer_category4ResetsNoEvent) {
    std::vector<i32> emit; g_emit = &emit;
    TurnTimerState st;
    st.phase = 1; st.base = 2.0f; st.threshold = 1.0f; st.category = 4;
    auto env = MakeEnv(); g_rf = 0.0f;
    i32 fired = -1;
    GameTick_AdvanceTurnTimer(st, env, &fired);
    CHECK_EQ(fired, 0);
    CHECK_EQ(st.category, 0);                     // reset
    CHECK_EQ(st.counter, 1);
    CHECK_EQ(emit.size(), (size_t)0);
}

TEST(GameTickRecon4, AdvanceTurnTimer_category1TableLookup) {
    std::vector<i32> emit; g_emit = &emit;
    TurnTimerState st;
    st.phase = 1; st.base = 2.0f; st.threshold = 1.0f; st.category = 1;
    auto env = MakeEnv(); g_rf = 0.0f;
    // table A index 0 -> 0; picked = 1 -> kind 89
    g_rm = 0;
    i32 fired = 0;
    GameTick_AdvanceTurnTimer(st, env, &fired);
    CHECK_EQ(kEventWeightTableA[0], 0);
    CHECK_EQ(fired, 89);
    CHECK_EQ(st.pending, 1);

    // table A index 8 -> 2; picked = 3 -> kind 80
    emit.clear();
    TurnTimerState st2;
    st2.phase = 1; st2.base = 2.0f; st2.threshold = 1.0f; st2.category = 1;
    g_rm = 8;
    fired = 0;
    GameTick_AdvanceTurnTimer(st2, env, &fired);
    CHECK_EQ(kEventWeightTableA[8], 2);
    CHECK_EQ(fired, 80);
    CHECK_EQ(st2.pending, 3);
}

TEST(GameTickRecon4, AdvanceTurnTimer_forcedSuppressedWhenHandlerActive) {
    std::vector<i32> emit; g_emit = &emit;
    TurnTimerState st;
    st.phase = 1; st.forced = 1;
    auto env = MakeEnv();
    env.heFindHandler = [](i32 kind){ return kind == 89 ? 1 : 0; };
    bool ran = GameTick_AdvanceTurnTimer(st, env);
    CHECK(!ran);                                  // suppressed -> early return
    CHECK_EQ(emit.size(), (size_t)0);
}

TEST(GameTickRecon4, AdvanceTurnTimer_forcedClearsPendingWhenNotSuppressed) {
    TurnTimerState st;
    st.phase = 1; st.forced = 2; st.pending = 9; st.accum = 5.0f;
    auto env = MakeEnv();
    env.heFindHandler = [](i32){ return 0; };     // not suppressed
    bool ran = GameTick_AdvanceTurnTimer(st, env);
    CHECK(ran);
    CHECK_EQ(st.pending, 0);
    CHECK(st.accum == 0.0f);
}

TEST(GameTickRecon4, RequestStartTurn_zeroesPhaseAndReturnsTag) {
    TurnTimerState st; st.phase = 1;
    auto env = MakeEnv();
    i32 tag = GameTick_RequestStartTurn(st, env);
    CHECK_EQ(st.phase, 0);
    CHECK_EQ(tag, 1685283436);
}

// ===========================================================================
//  AdvanceGameDialog
// ===========================================================================
TEST(GameTickRecon4, RunAdvanceGameDialog_notShownWhenFlagClear) {
    GameTickRecon4Hooks hk;
    hk.interactionTestHandlerFlag = [](i32){ return (u8)0; };
    u8 res = 1;
    i32 iters = GameTick_RunAdvanceGameDialog(hk, &res);
    CHECK_EQ(iters, 0);
    CHECK_EQ(res, (u8)0);
}

static int g_loopRemain = 0;
TEST(GameTickRecon4, RunAdvanceGameDialog_runsLoopAndOrders) {
    std::vector<std::string> rec; g_rec = &rec;
    GameTickRecon4Hooks hk;
    hk.interactionTestHandlerFlag = [](i32 m){ Rec(m == 8 ? "test8" : "testX"); return (u8)1; };
    hk.interactionDispatchPanelEvent = [](i32 op, i32 close){
        Rec(("dispatch" + std::to_string(op) + "_" + std::to_string(close)).c_str());
    };
    hk.gameTickFinalize = []{ Rec("finalize"); return 42; };
    hk.formCenterChildWindows = [](i32 f){ Rec(f == 42 ? "center42" : "centerX"); };
    MK(textRenderRichString, "rich");
    g_loopRemain = 3;
    hk.gameLogicRunFrameLoop = []{ return (u8)(g_loopRemain-- > 0 ? 1 : 0); };
    hk.formDestroy = [](i32 f){ Rec(f == 42 ? "destroy42" : "destroyX"); };

    i32 iters = GameTick_RunAdvanceGameDialog(hk, nullptr);
    CHECK_EQ(iters, 3);
    std::vector<std::string> want = {
        "test8", "dispatch11_0", "finalize", "center42", "rich", "destroy42", "dispatch11_1"
    };
    CHECK_EQ(rec.size(), want.size());
    for (size_t i = 0; i < want.size() && i < rec.size(); ++i)
        CHECK(rec[i] == want[i]);
}

// ===========================================================================
//  Shutdown ORDER
// ===========================================================================
static GameTickRecon4Hooks MakeShutdownRecorder() {
    GameTickRecon4Hooks hk;
    hk.configWriteGfxSettings     = []{ Rec("gfx"); };
    hk.audioUnloadSampleBank      = [](i32 b){ Rec(("bank" + std::to_string(b)).c_str()); };
    hk.scriptShutdownEngine       = []{ Rec("scriptShut"); };
    hk.cutsceneUnregisterTickProc = []{ Rec("cutscene"); };
    hk.soundLibShutdown           = []{ Rec("soundLib"); };
    hk.sound3dFreePool            = []{ Rec("snd3dPool"); };
    hk.soundShutdown              = []{ Rec("soundShut"); };
    hk.audioShutdown49878         = []{ Rec("audioShut"); };
    hk.soundWaveFreeTables        = []{ Rec("waveTables"); };
    hk.textFreeAllTextFiles       = []{ Rec("textFree"); };
    hk.gameObjectFreeAllTables    = []{ Rec("goFree"); };
    hk.charActionQueueShutdown    = []{ Rec("charAct"); };
    hk.commandQueueResetAlt       = []{ Rec("cmdAlt"); };
    hk.widgetShutdownSystem       = []{ Rec("widget"); };
    hk.gameStateFreeAllResources  = []{ Rec("gsFree"); };
    hk.universeSwitchActiveSlot   = [](i32 s){ Rec(("switch" + std::to_string(s)).c_str()); };
    hk.tableResetLightmaps        = []{ Rec("lightmaps"); };
    hk.renderShutdownEngine       = []{ Rec("render"); };
    hk.inputDirectInputShutdown   = []{ Rec("input"); };
    hk.timeBaseStopTimer          = []{ Rec("timer"); };
    hk.vfsShutdown                = []{ Rec("vfs"); };
    hk.memPoolShutdownStack       = []{ Rec("mempool"); };
    hk.memoryShutdownTracker      = []{ Rec("memtrack"); };
    hk.errorLogShutdown           = []{ Rec("errlog"); };
    hk.pluginShutdownAndFree      = []{ Rec("plugin"); };
    hk.windowDestroyAndUnregister = []{ Rec("window"); };
    return hk;
}

TEST(GameTickRecon4, ShutdownSubsystems_innerOrder) {
    std::vector<std::string> rec; g_rec = &rec;
    GameTickRecon4Hooks hk = MakeShutdownRecorder();
    ShutdownFlags f;
    f.soundLibActive = true; f.soundActive = true; f.audioBankCount = 2;
    Game_ShutdownSubsystems(f, hk);
    std::vector<std::string> want = {
        "gfx", "bank0", "bank1", "scriptShut", "cutscene",
        "soundLib", "snd3dPool", "soundShut", "audioShut", "waveTables",
        "textFree", "goFree", "charAct", "cmdAlt"
    };
    CHECK_EQ(rec.size(), want.size());
    for (size_t i = 0; i < want.size() && i < rec.size(); ++i)
        CHECK(rec[i] == want[i]);
}

TEST(GameTickRecon4, ShutdownSubsystems_skipsSoundWhenInactive) {
    std::vector<std::string> rec; g_rec = &rec;
    GameTickRecon4Hooks hk = MakeShutdownRecorder();
    ShutdownFlags f; // all sound off, no banks
    Game_ShutdownSubsystems(f, hk);
    std::vector<std::string> want = {
        "gfx", "scriptShut", "cutscene", "textFree", "goFree", "charAct", "cmdAlt"
    };
    CHECK_EQ(rec.size(), want.size());
    for (size_t i = 0; i < want.size() && i < rec.size(); ++i)
        CHECK(rec[i] == want[i]);
}

TEST(GameTickRecon4, ShutdownAllSubsystems_outerOrderInnerFirst) {
    std::vector<std::string> rec; g_rec = &rec;
    GameTickRecon4Hooks hk = MakeShutdownRecorder();
    ShutdownFlags f; f.pluginLoaded = true; // no sound, no banks
    Game_ShutdownAllSubsystems(f, hk);
    // inner sequence runs first, then outer.
    std::vector<std::string> want = {
        // inner:
        "gfx", "scriptShut", "cutscene", "textFree", "goFree", "charAct", "cmdAlt",
        // outer:
        "widget", "gfx", "gsFree", "switch0", "lightmaps", "render", "input",
        "timer", "vfs", "mempool", "memtrack", "errlog", "plugin", "window"
    };
    CHECK_EQ(rec.size(), want.size());
    for (size_t i = 0; i < want.size() && i < rec.size(); ++i)
        CHECK(rec[i] == want[i]);
}

TEST(GameTickRecon4, ShutdownAllSubsystems_skipsPluginWhenNotLoaded) {
    std::vector<std::string> rec; g_rec = &rec;
    GameTickRecon4Hooks hk = MakeShutdownRecorder();
    ShutdownFlags f; // pluginLoaded false
    Game_ShutdownAllSubsystems(f, hk);
    bool sawPlugin = false;
    for (auto& s : rec) if (s == "plugin") sawPlugin = true;
    CHECK(!sawPlugin);
    CHECK(rec.back() == "window");
}

static GameTickRecon4Hooks MakeWorldRecorder() {
    GameTickRecon4Hooks hk;
    hk.statusTextClearTable        = []{ Rec("statusClear"); };
    hk.widgetSetTooltipText        = []{ Rec("tooltip"); };
    hk.hudSetStatusBannerText      = []{ Rec("banner"); };
    hk.timeBaseUnregisterProc      = []{ Rec("unregProc"); };
    hk.scriptFreeFinished          = []{ Rec("scriptFree"); };
    hk.meshReleaseStockObject      = []{ Rec("meshRel"); };
    hk.voiceQueueFlushAll          = []{ Rec("voiceFlush"); };
    hk.sound3dStopAll              = []{ Rec("snd3dStop"); };
    hk.audioUnloadSampleBank       = [](i32 b){ Rec(("wbank" + std::to_string(b)).c_str()); };
    hk.historyResetChronicleState  = []{ Rec("history"); };
    hk.objectResetStateAlt         = []{ Rec("objReset"); };
    hk.heTickActiveHandlers        = []{ Rec("heTick"); };
    hk.eventPanelDestroyBar        = []{ Rec("panelBar"); };
    hk.heUpdateSubsystems          = []{ Rec("heUpd"); };
    hk.groundplanDestroyWindow     = []{ Rec("groundplan"); };
    hk.buildingResetAllBuildings   = []{ Rec("bldgReset"); };
    hk.worldResetPersonTable       = []{ Rec("personTbl"); };
    hk.worldRelinkObjectOwners     = []{ Rec("relink"); };
    hk.characterDestroy            = [](i32 i){ Rec(("char" + std::to_string(i)).c_str()); };
    hk.snowUpdateScene             = []{ Rec("snow"); };
    hk.rainDestroy                 = []{ Rec("rain"); };
    hk.skyRemoveLayer              = [](i32 j){ Rec(("skyL" + std::to_string(j)).c_str()); };
    hk.skyDestroy                  = []{ Rec("skyDestroy"); };
    hk.lightApplyAmbient           = []{ Rec("ambient"); };
    hk.objectDestroySpawnedEntities= []{ Rec("spawned"); };
    hk.inventoryDestroyGridSurface = []{ Rec("invGrid"); };
    hk.animalFreePool              = []{ Rec("animal"); };
    hk.commandQueueReset           = []{ Rec("cmdReset"); };
    hk.netCloseBroadcastSocket     = []{ Rec("netClose"); };
    hk.netDisconnect               = []{ Rec("netDisc"); };
    hk.dragCursorSetSprite         = []{ Rec("drag"); };
    return hk;
}

TEST(GameTickRecon4, ShutdownWorld_fullOrderAllFlags) {
    std::vector<std::string> rec; g_rec = &rec;
    GameTickRecon4Hooks hk = MakeWorldRecorder();
    ShutdownFlags f;
    f.stockMeshLoaded = true; f.soundLibActive = true; f.sampleBanksBound = true;
    f.characterCount = 2; f.snowActive = true; f.rainActive = true;
    f.skyActive = true; f.ambientLightSet = true;
    Game_ShutdownWorldAndSubsystems(f, hk);
    std::vector<std::string> want = {
        "statusClear", "tooltip", "banner", "unregProc", "scriptFree",
        "meshRel",
        "voiceFlush", "snd3dStop",
        "wbank0", "wbank1", "wbank2", "wbank3", "wbank4", "wbank5",
        "history", "objReset", "heTick", "panelBar", "heUpd", "groundplan",
        "bldgReset", "personTbl", "relink",
        "char0", "char1",
        "snow", "rain",
        "skyL0", "skyL1", "skyL2", "skyL3", "skyL4", "skyL5", "skyDestroy",
        "ambient",
        "spawned", "invGrid", "animal", "cmdReset", "netClose", "netDisc", "drag"
    };
    CHECK_EQ(rec.size(), want.size());
    for (size_t i = 0; i < want.size() && i < rec.size(); ++i)
        CHECK(rec[i] == want[i]);
}

TEST(GameTickRecon4, ShutdownWorld_skipsConditionalBranches) {
    std::vector<std::string> rec; g_rec = &rec;
    GameTickRecon4Hooks hk = MakeWorldRecorder();
    ShutdownFlags f; // everything off, no characters
    Game_ShutdownWorldAndSubsystems(f, hk);
    for (auto& s : rec) {
        CHECK(s != "meshRel");
        CHECK(s != "voiceFlush");
        CHECK(s != "wbank0");
        CHECK(s != "snow");
        CHECK(s != "rain");
        CHECK(s != "skyDestroy");
        CHECK(s != "ambient");
    }
    // the unconditional tail still runs in order
    CHECK(rec.front() == "statusClear");
    CHECK(rec.back() == "drag");
}

#undef MK

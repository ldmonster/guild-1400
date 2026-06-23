// ===========================================================================
// Golden-vector unit tests for the scene/frame ORCHESTRATION reconstruction.
// Self-contained: only depends on the test framework + the recon header.
// Suite prefix: Scene2Recon.
// ===========================================================================
#include "test.h"
#include "play/scene_recon2_orchestrator.h"

#include <vector>
#include <string>
#include <cstring>

using namespace guild;
using namespace guild::play::scene_recon2;

// ---------------------------------------------------------------------------
// Recovered rodata constants (golden bytes from get_bytes).
// ---------------------------------------------------------------------------
TEST(Scene2ReconConst, SmokeHourTablesMatchRodata) {
    // flt_6476FC: 8 7 8 9
    CHECK_EQ(scene_const::kSmokeStartHour[0], 8.0f);
    CHECK_EQ(scene_const::kSmokeStartHour[1], 7.0f);
    CHECK_EQ(scene_const::kSmokeStartHour[2], 8.0f);
    CHECK_EQ(scene_const::kSmokeStartHour[3], 9.0f);
    // flt_64770C: 20 21 20 19
    CHECK_EQ(scene_const::kSmokeEndHour[0], 20.0f);
    CHECK_EQ(scene_const::kSmokeEndHour[1], 21.0f);
    CHECK_EQ(scene_const::kSmokeEndHour[2], 20.0f);
    CHECK_EQ(scene_const::kSmokeEndHour[3], 19.0f);
}

TEST(Scene2ReconConst, DecorSeasonListBytes) {
    // dword_4FFAF8 = {0x20,0x1f,0x1e,0x00} = {32,31,30,0}
    CHECK_EQ((int)scene_const::kDecorSeasonList[0], 0x20);
    CHECK_EQ((int)scene_const::kDecorSeasonList[1], 0x1f);
    CHECK_EQ((int)scene_const::kDecorSeasonList[2], 0x1e);
    CHECK_EQ((int)scene_const::kDecorSeasonList[3], 0x00);
}

TEST(Scene2ReconConst, CityBuildGateTable) {
    // dword_4FFAFC golden 29 bytes.
    static const guild::u8 expect[29] = {
        0,0,3,0,1,1,0,1,1,1,0,2,2,2,1,0,2,1,1,1,1,1,1,0,0,0,0,3,3
    };
    for (int i = 0; i < 29; ++i)
        CHECK_EQ((int)scene_const::kCityBuildGate[i], (int)expect[i]);
}

TEST(Scene2ReconConst, FrameLoopTag) {
    CHECK_EQ(scene_const::kFrameLoopTag, 425983);
}

// ---------------------------------------------------------------------------
// SmokeTimeGate: start <= hour < end, per season.
// ---------------------------------------------------------------------------
TEST(Scene2ReconSmoke, GateWindowsPerSeason) {
    // season 0: [8,20)
    CHECK(!SmokeTimeGate(0, 7.9f));
    CHECK(SmokeTimeGate(0, 8.0f));
    CHECK(SmokeTimeGate(0, 19.9f));
    CHECK(!SmokeTimeGate(0, 20.0f));
    // season 1: [7,21)
    CHECK(SmokeTimeGate(1, 7.0f));
    CHECK(!SmokeTimeGate(1, 6.5f));
    CHECK(SmokeTimeGate(1, 20.9f));
    CHECK(!SmokeTimeGate(1, 21.0f));
    // season 3: [9,19)
    CHECK(!SmokeTimeGate(3, 8.5f));
    CHECK(SmokeTimeGate(3, 9.0f));
    CHECK(!SmokeTimeGate(3, 19.0f));
}

TEST(Scene2ReconSmoke, SeasonMaskedToFour) {
    // season byte is masked & 3 against the 4-entry tables.
    CHECK_EQ(SmokeTimeGate(4, 8.0f), SmokeTimeGate(0, 8.0f));
    CHECK_EQ(SmokeTimeGate(7, 9.0f), SmokeTimeGate(3, 9.0f));
}

// ---------------------------------------------------------------------------
// SpawnBuildingMesh bbox merge (pure kernel).
// ---------------------------------------------------------------------------
TEST(Scene2ReconBBox, MergeMinMaxOver20Verts) {
    float verts[20 * 20];
    std::memset(verts, 0, sizeof(verts));
    // vert0 = (1,1,1) seed
    verts[0] = 1.0f; verts[1] = 1.0f; verts[2] = 1.0f;
    // vert5 = (-3, 2, 0.5) -> pushes min x, max y
    verts[5 * 20 + 0] = -3.0f; verts[5 * 20 + 1] = 2.0f; verts[5 * 20 + 2] = 0.5f;
    // vert9 = (4, -1, 7)    -> pushes max x, min y, max z
    verts[9 * 20 + 0] = 4.0f; verts[9 * 20 + 1] = -1.0f; verts[9 * 20 + 2] = 7.0f;
    float mn[3], mx[3];
    SpawnBuildingMesh_MergeBBox(verts, mn, mx);
    CHECK_EQ(mn[0], -3.0f); CHECK_EQ(mx[0], 4.0f);
    CHECK_EQ(mn[1], -1.0f); CHECK_EQ(mx[1], 2.0f);
    // remaining verts are 0 so min z = 0, max z = 7.
    CHECK_EQ(mn[2], 0.0f);  CHECK_EQ(mx[2], 7.0f);
}

// ---------------------------------------------------------------------------
// SaveObjectGroup: parent-validation + write-path branching.
// ---------------------------------------------------------------------------
namespace {
struct SaveTrace {
    std::vector<std::string> ev;
    int openParent = -999;
    std::vector<int> magics;
    std::vector<int> written;
};
SaveTrace g_save;

void sg_err(const char*)            { g_save.ev.push_back("ERR"); }
guild::i32 sg_parent(guild::i32 o)  { return o >= 100 ? (o - o % 100) : 0; } // 100..199->100, else 0
void sg_unlink(guild::i32)          { g_save.ev.push_back("unlink"); }
void sg_setparent(guild::i32)       { g_save.ev.push_back("setparent"); }
guild::i32 sg_open(const char*, guild::i32 p) { g_save.openParent = p; g_save.ev.push_back("open"); return 7; }
void sg_writepair(guild::i32, guild::i32 v)   { g_save.magics.push_back(v); }
void sg_writeobj(guild::i32, guild::i32 o)    { g_save.written.push_back(o); }
void sg_close(guild::i32)           { g_save.ev.push_back("close"); }

SaveObjectGroupIO makeSaveIO() {
    SaveObjectGroupIO io;
    io.ReportError = sg_err;
    io.GetParent = sg_parent;
    io.UnlinkFromList = sg_unlink;
    io.SetParent = sg_setparent;
    io.OpenFile = sg_open;
    io.WriteDwordPair = sg_writepair;
    io.WriteObject = sg_writeobj;
    io.CloseStream = sg_close;
    return io;
}
} // namespace

TEST(Scene2ReconSave, EmptyCountDoesNothing) {
    g_save = SaveTrace{};
    auto io = makeSaveIO();
    int objs[1] = {100};
    Scene_SaveObjectGroup(io, "g.bin", objs, 0, 0);
    CHECK(g_save.ev.empty());
}

TEST(Scene2ReconSave, MismatchedParentsReportsAndAborts) {
    g_save = SaveTrace{};
    auto io = makeSaveIO();
    int objs[2] = {100, 200}; // parents 100 vs 200
    Scene_SaveObjectGroup(io, "g.bin", objs, 2, 0);
    CHECK_EQ((int)g_save.ev.size(), 1);
    CHECK_EQ(g_save.ev[0], std::string("ERR"));
    CHECK(g_save.written.empty());
}

TEST(Scene2ReconSave, SharedParentWritesSingleObject) {
    g_save = SaveTrace{};
    auto io = makeSaveIO();
    int objs[3] = {100, 150, 199}; // all parent 100
    Scene_SaveObjectGroup(io, "g.bin", objs, 3, 0);
    // shared-parent path: unlink+setparent each (3), open with parent 100,
    // magic + count(1), write objs[0] only.
    CHECK_EQ(g_save.openParent, 100);
    CHECK_EQ((int)g_save.magics.size(), 2);
    CHECK_EQ(g_save.magics[0], 980156603);
    CHECK_EQ(g_save.magics[1], 1);
    CHECK_EQ((int)g_save.written.size(), 1);
    CHECK_EQ(g_save.written[0], 100);
}

TEST(Scene2ReconSave, NoSharedParentWritesAll) {
    g_save = SaveTrace{};
    auto io = makeSaveIO();
    // GetParent returns 0 for objs<100 -> shared parent resolves to 0.
    int objs[2] = {1, 2};
    Scene_SaveObjectGroup(io, "g.bin", objs, 2, 0);
    CHECK_EQ(g_save.openParent, 0);
    CHECK_EQ(g_save.magics[0], 980156603);
    CHECK_EQ(g_save.magics[1], 2);          // count = 2
    CHECK_EQ((int)g_save.written.size(), 2);
    CHECK_EQ(g_save.written[0], 1);
    CHECK_EQ(g_save.written[1], 2);
}

// ---------------------------------------------------------------------------
// HandleDebugKeyToggle: dispatch ladder golden behavior.
// ---------------------------------------------------------------------------
namespace {
struct DbgTrace {
    int setZ = -1, setEngine = -1, lights = 0, shadow = 0, applied = 0, invalidate = 0, walked = 0;
};
DbgTrace g_dbg;
void d_setz(bool on)     { g_dbg.setZ = on ? 1 : 0; }
void d_seteng(bool on)   { g_dbg.setEngine = on ? 1 : 0; }
void d_lights()          { g_dbg.lights++; }
void d_shadow()          { g_dbg.shadow++; }
void d_invalidate()      { g_dbg.invalidate++; }
void d_apply()           { g_dbg.applied++; }
guild::i32 d_walk()      { g_dbg.walked++; return 42; }
DebugKeyHooks makeDbgHooks() {
    DebugKeyHooks h;
    h.RenderSetZEnable = d_setz;
    h.RenderSetEngineEnabled = d_seteng;
    h.LightRefreshAllObjects = d_lights;
    h.SceneGraphTraverseShadow = d_shadow;
    h.ObjectInvalidateCurrent = d_invalidate;
    h.RenderApplyRenderStates = d_apply;
    h.TrackTargetWalk = d_walk;
    return h;
}
} // namespace

TEST(Scene2ReconDebugKey, NoEdgeReturnsZero) {
    DebugKeyState s; auto h = makeDbgHooks();
    s.byte_64A7A3 = 35; s.byte_67225C = 35; // same -> no edge
    g_dbg = DbgTrace{};
    CHECK_EQ((int)Scene_HandleDebugKeyToggle(s, h), 0);
    CHECK_EQ(g_dbg.setEngine, -1); // untouched
}

TEST(Scene2ReconDebugKey, Key35TogglesEngineReturnsOne) {
    DebugKeyState s; auto h = makeDbgHooks();
    s.byte_67225C = 0x23;     // 35
    s.byte_649D70 = 0;        // engine off -> enable
    g_dbg = DbgTrace{};
    CHECK_EQ((int)Scene_HandleDebugKeyToggle(s, h), 1);
    CHECK_EQ(g_dbg.setEngine, 1);
    CHECK_EQ(s.byte_64A7A3, 0x23); // latched
}

TEST(Scene2ReconDebugKey, Key38RefreshesLightsReturnsZero) {
    DebugKeyState s; auto h = makeDbgHooks();
    s.byte_67225C = 0x26;     // 38
    g_dbg = DbgTrace{};
    CHECK_EQ((int)Scene_HandleDebugKeyToggle(s, h), 0);
    CHECK_EQ(g_dbg.lights, 1);
}

TEST(Scene2ReconDebugKey, Key21TogglesZEnable) {
    DebugKeyState s; auto h = makeDbgHooks();
    s.byte_67225C = 21;
    s.byte_64A351 = 0;        // z off -> enable
    g_dbg = DbgTrace{};
    CHECK_EQ((int)Scene_HandleDebugKeyToggle(s, h), 0);
    CHECK_EQ(g_dbg.setZ, 1);
}

TEST(Scene2ReconDebugKey, Key30TogglesRenderStateBit0AndApplies) {
    DebugKeyState s; auto h = makeDbgHooks();
    s.byte_67225C = 0x1E;     // 30
    s.renderState[0] = 0;     // bit0 clear -> set
    g_dbg = DbgTrace{};
    CHECK_EQ((int)Scene_HandleDebugKeyToggle(s, h), 0);
    CHECK_EQ((s.renderState[0] & 1), 1);
    CHECK_EQ(g_dbg.applied, 1);
    CHECK_EQ(g_dbg.invalidate, 1);
}

TEST(Scene2ReconDebugKey, Key32TogglesRenderStateBit2) {
    DebugKeyState s; auto h = makeDbgHooks();
    s.byte_67225C = 0x20;     // 32
    s.renderState[0] = 0;     // bit2 clear -> set to 4
    g_dbg = DbgTrace{};
    Scene_HandleDebugKeyToggle(s, h);
    CHECK_EQ((s.renderState[0] & 4), 4);
}

TEST(Scene2ReconDebugKey, Key48TogglesRenderStateBit1) {
    DebugKeyState s; auto h = makeDbgHooks();
    s.byte_67225C = 0x30;     // 48
    s.renderState[0] = 0;     // bit1 clear -> set to 2
    g_dbg = DbgTrace{};
    Scene_HandleDebugKeyToggle(s, h);
    CHECK_EQ((s.renderState[0] & 2), 2);
}

TEST(Scene2ReconDebugKey, Key50CyclesV12) {
    DebugKeyState s; auto h = makeDbgHooks();
    s.byte_67225C = 50;
    s.renderState[2] = 1;     // 1 -> 2
    g_dbg = DbgTrace{};
    Scene_HandleDebugKeyToggle(s, h);
    CHECK_EQ(s.renderState[2], 2);
    // 2 -> 3 (new edge: bump the new key, reset latch)
    s.byte_64A7A3 = 0; s.byte_67225C = 50;
    Scene_HandleDebugKeyToggle(s, h);
    CHECK_EQ(s.renderState[2], 3);
    s.byte_64A7A3 = 0; s.byte_67225C = 50;
    Scene_HandleDebugKeyToggle(s, h);
    CHECK_EQ(s.renderState[2], 1); // 3 -> 1
}

TEST(Scene2ReconDebugKey, Key31ShadowToggleTraverses) {
    DebugKeyState s; auto h = makeDbgHooks();
    s.byte_67225C = 0x1F;     // 31
    s.renderState[1] = 0;     // (v11&7)==0 -> set bit0, clear byte_1408A6D
    s.byte_1408A6D = 1;
    g_dbg = DbgTrace{};
    Scene_HandleDebugKeyToggle(s, h);
    CHECK_EQ((s.renderState[1] & 7), 1);
    CHECK_EQ((int)s.byte_1408A6D, 0);
    CHECK_EQ(g_dbg.shadow, 1);
    CHECK_EQ(g_dbg.applied, 1); // v13=1 so applies
}

TEST(Scene2ReconDebugKey, Key46WalksTrackTarget) {
    DebugKeyState s; auto h = makeDbgHooks();
    s.byte_67225C = 0x2E;     // 46
    g_dbg = DbgTrace{};
    Scene_HandleDebugKeyToggle(s, h);
    CHECK_EQ(g_dbg.walked, 1);
    CHECK_EQ(s.trackTarget, 42);
}

// ---------------------------------------------------------------------------
// GameState_ResetVoicesAndScript: ordering + state reset golden.
// ---------------------------------------------------------------------------
namespace {
struct ResetTrace {
    int detach = 0, stopVoices = 0, charCancel = 0, scriptFinish = 0;
    std::vector<int> traverseOps;
};
ResetTrace g_reset;
void r_traverse(guild::i32 op, guild::i32) { g_reset.traverseOps.push_back(op); }
void r_detach() { g_reset.detach++; }
void r_stop(guild::i32, guild::i32) { g_reset.stopVoices++; }
guild::i32 r_find(guild::i32 hnd) { return hnd == 555 ? 999 : 0; }
void r_finish(guild::i32) { g_reset.scriptFinish++; }
void r_cancel() { g_reset.charCancel++; }
} // namespace

TEST(Scene2ReconReset, ResetsCountersAndFinishesScript) {
    SceneState s;
    s.dword_6344A0 = 3;
    s.dword_634494 = 555;     // active script handle that resolves
    SceneHooks h;
    h.SceneGraphTraverse = r_traverse;
    h.Sound3dDetachObjectSfx = r_detach;
    h.AudioStopVoice = r_stop;
    h.ScriptFindByHandle = r_find;
    h.ScriptFinish = r_finish;
    h.CharActionCancelForObject = r_cancel;
    g_reset = ResetTrace{};

    GameState_ResetVoicesAndScript(s, h);

    CHECK_EQ(g_reset.detach, 1);
    CHECK_EQ((int)g_reset.traverseOps.size(), 1);
    CHECK_EQ(g_reset.traverseOps[0], 160);    // detach-sfx op
    CHECK_EQ(g_reset.stopVoices, 3);          // dword_6344A0 voices stopped
    CHECK_EQ(s.dword_6344A0, 0);
    CHECK_EQ(s.dword_64A05C, 1024);
    CHECK_EQ(s.dword_64A054, 64);
    CHECK_EQ(g_reset.scriptFinish, 1);
    CHECK_EQ(s.dword_634494, -1);             // cleared after finish
    CHECK_EQ(g_reset.charCancel, 1);
    CHECK_EQ(s.dword_62D080, -1);
}

TEST(Scene2ReconReset, NoScriptWhenHandleMinusOne) {
    SceneState s;
    s.dword_6344A0 = 0;
    s.dword_634494 = -1;      // no active script
    SceneHooks h;
    h.ScriptFindByHandle = r_find;
    h.ScriptFinish = r_finish;
    h.CharActionCancelForObject = r_cancel;
    g_reset = ResetTrace{};
    GameState_ResetVoicesAndScript(s, h);
    CHECK_EQ(g_reset.scriptFinish, 0);
    CHECK_EQ(g_reset.charCancel, 1);          // still cancels char actions
}

// ---------------------------------------------------------------------------
// ActivateAndRefreshCharacters: early-out gate + brightness math + present mode.
// ---------------------------------------------------------------------------
namespace {
struct ActTrace { int anchor=0, weather=0, buildVis=0, bright=0, present=-1; };
ActTrace g_act;
guild::u8 a_season() { return 2; }
void a_anchor(guild::i32, guild::i32) { g_act.anchor++; }
void a_traverse(guild::i32, guild::i32) {}
void a_weather() { g_act.weather++; }
void a_buildvis(guild::i32) { g_act.buildVis++; }
void a_bright() { g_act.bright++; }
bool a_present(guild::i32 m) { g_act.present = m; return true; }
} // namespace

TEST(Scene2ReconActivate, EarlyOutWhenSlotNonzero) {
    SceneState s; s.dword_649D60 = 1; // non-default slot -> early out
    SceneHooks h; h.GetSeasonFromDay = a_season;
    h.CameraAnchorToTerrain = a_anchor;
    g_act = ActTrace{};
    guild::u8 r = Scene_ActivateAndRefreshCharacters(s, h);
    CHECK_EQ((int)r, 2);          // returns season
    CHECK_EQ(g_act.anchor, 0);    // no refresh work
}

TEST(Scene2ReconActivate, FullRefreshComputesBrightnessAndPresents) {
    SceneState s;
    s.dword_649D60 = 0;
    s.byte_123351C = 10;          // cloud units
    s.byte_64A01C = 0;            // -> present mode 1
    SceneHooks h;
    h.GetSeasonFromDay = a_season;
    h.CameraAnchorToTerrain = a_anchor;
    h.SceneGraphTraverse = a_traverse;
    h.WeatherApplySeasonalMeshes = a_weather;
    h.ObjectUpdateBuildingVisualState = a_buildvis;
    h.DayCycleUpdateBrightness = a_bright;
    h.RenderPresentSceneAndClearFlags = a_present;
    g_act = ActTrace{};

    guild::u8 r = Scene_ActivateAndRefreshCharacters(s, h);

    CHECK_EQ((int)r, 2);
    CHECK_EQ((int)s.byte_634484, 2);              // committed season
    CHECK_EQ(g_act.anchor, 1);
    CHECK_EQ(g_act.weather, 1);
    CHECK_EQ(g_act.buildVis, 1);
    CHECK_EQ(g_act.bright, 1);
    CHECK_EQ(g_act.present, 1);                   // byte_64A01C==0 -> mode 1
    // brightness = 1.0 - 10 * 0.01 = 0.9
    CHECK(s.flt_64A018 > 0.8999f && s.flt_64A018 < 0.9001f);
}

TEST(Scene2ReconActivate, PresentModeZeroWhenFlagSet) {
    SceneState s; s.dword_649D60 = 0; s.byte_64A01C = 1; // -> present mode 0
    SceneHooks h;
    h.GetSeasonFromDay = a_season;
    h.CameraAnchorToTerrain = a_anchor;
    h.RenderPresentSceneAndClearFlags = a_present;
    g_act = ActTrace{};
    Scene_ActivateAndRefreshCharacters(s, h);
    CHECK_EQ(g_act.present, 0);
}

// ---------------------------------------------------------------------------
// RunMainFrameLoop: enters/exits the loop and toggles the in-city flags.
// ---------------------------------------------------------------------------
namespace {
int g_frames = 0;
bool fl_run(guild::i32 tag) {
    // tag must be the magic; run exactly 2 iterations then exit.
    if (tag != 425983) return false;
    return g_frames++ < 2;
}
struct FrameTrace { int marketStart=0, marketStop=0, selReset=0, camUpdate=0; };
FrameTrace g_ftr;
void fl_mstart() { g_ftr.marketStart++; }
void fl_mstop()  { g_ftr.marketStop++; }
void fl_selreset() { g_ftr.selReset++; }
void fl_camupd() { g_ftr.camUpdate++; }
guild::u8 fl_season() { return 0; }
} // namespace

TEST(Scene2ReconFrameLoop, SetsAndClearsInCityFlagsAcrossLoop) {
    SceneState s;
    SceneHooks h;
    h.GetSeasonFromDay = fl_season;
    h.RunFrameLoop = fl_run;
    h.AmbientStartMarketLoop = fl_mstart;
    h.AmbientStopMarketLoop = fl_mstop;
    h.SelectionReset = fl_selreset;
    h.CameraUpdate = fl_camupd;
    g_frames = 0; g_ftr = FrameTrace{};

    guild::i32 r = Scene_RunMainFrameLoop(s, h);

    // after teardown the in-city flags are cleared again.
    CHECK_EQ((int)s.byte_642008, 0);
    CHECK_EQ((int)s.byte_63CC40, 0);
    CHECK_EQ(g_ftr.marketStart, 1);
    CHECK_EQ(g_ftr.marketStop, 1);
    CHECK_EQ(g_ftr.selReset, 1);
    CHECK_EQ(g_ftr.camUpdate, 2);     // one per loop iteration
    CHECK_EQ(r, 0);                   // final RunFrameLoop returns 0
}

// ---------------------------------------------------------------------------
// SyncWorldOnEnter: top-level ordering + return value.
// ---------------------------------------------------------------------------
namespace {
struct WorldTrace { int syncStates=0, home=0, charSlots=0, city=0, stock=0; };
WorldTrace g_world;
void w_syncstates() { g_world.syncStates++; }
void w_home() { g_world.home++; }
void w_charslots(guild::i32) { g_world.charSlots++; }
void w_city() { g_world.city++; }
guild::i32 w_querygood(guild::i32) { return 1; }
guild::i32 w_queryfind(guild::i32, guild::i32) { return 88; }  // found stock obj
void w_req16(guild::i32, guild::i32 amt) { if (amt == 480000) g_world.stock++; }
} // namespace

TEST(Scene2ReconWorld, EntersAllStagesAndReturnsOne) {
    SceneState s;
    s.dword_764CE0 = 0;       // not -1 -> skip reseed
    s.dword_63C794 = 1;       // home gate on
    SceneHooks h;
    h.CommandSyncSceneObjectStates = w_syncstates;
    h.GameLogicSetupHomeSweetHome = w_home;
    h.CommandSyncCharSlotAssignments = w_charslots;
    h.SceneSyncCityBuildings = w_city;
    h.PersonQueryByGoodType = w_querygood;
    h.GameObjectQueryFind = w_queryfind;
    h.CommandQueueRequest16 = w_req16;
    g_world = WorldTrace{};

    guild::i32 r = Scene_SyncWorldOnEnter(s, h, /*city*/0, /*delta*/2, /*foundType6*/true);

    CHECK_EQ(r, 1);
    CHECK_EQ(g_world.syncStates, 1);
    CHECK_EQ(g_world.home, 1);        // foundType6 && gate
    CHECK_EQ(g_world.charSlots, 1);   // delta>0
    CHECK_EQ(g_world.city, 1);
    CHECK_EQ(g_world.stock, 1);       // stock-up request emitted
}

TEST(Scene2ReconWorld, SkipsHomeAndCharSlotsWhenGatesOff) {
    SceneState s;
    s.dword_764CE0 = 0;
    s.dword_63C794 = 0;       // home gate off
    SceneHooks h;
    h.CommandSyncSceneObjectStates = w_syncstates;
    h.GameLogicSetupHomeSweetHome = w_home;
    h.CommandSyncCharSlotAssignments = w_charslots;
    h.SceneSyncCityBuildings = w_city;
    g_world = WorldTrace{};
    Scene_SyncWorldOnEnter(s, h, 0, /*delta*/0, /*foundType6*/false);
    CHECK_EQ(g_world.home, 0);
    CHECK_EQ(g_world.charSlots, 0);   // delta==0
    CHECK_EQ(g_world.city, 1);
}

// ---------------------------------------------------------------------------
// SyncDecorObjects: season list iteration + decor id quartets per season.
// ---------------------------------------------------------------------------
namespace {
std::vector<int> g_decorIds;
int g_decorPersons = 0;
guild::i32 dec_begin(guild::i32, guild::i32, guild::i32 key) {
    // yield exactly one person (id=1) for the first season key, none otherwise,
    // so the id quartet for season 30/31/32 is exercised in turn.
    g_decorPersons = (int)key;  // record the season the iteration used
    return 1;
}
guild::i32 dec_next() { return 0; }
guild::i32 dec_qfind(guild::i32, guild::i32) { return 10; } // existing objects
void dec_req17(guild::i32, guild::i32, guild::i32 c, guild::i32 d) {
    // primary emits carry the decor id in arg 'd'; the switch-secondary emits
    // carry the secondary id in arg 'c'. Collect both so any decor id shows up.
    g_decorIds.push_back((int)c);
    g_decorIds.push_back((int)d);
}
guild::i32 dec_rand(guild::u32) { return 0; }
} // namespace

TEST(Scene2ReconDecor, IteratesSeasonListAndEmitsDecor) {
    SceneHooks h;
    h.PersonQueryBegin = dec_begin;
    h.PersonIterNext = dec_next;
    h.GameObjectQueryFind = dec_qfind;
    h.GameObjectAddObjekt = dec_qfind;
    h.CommandQueueRequest17 = dec_req17;
    h.MathRandomModulo = dec_rand;
    g_decorIds.clear();

    guild::i32 last = Scene_SyncDecorObjects(h);
    CHECK_EQ(last, 1);                  // last yielded person low word
    // At least the secondary ids (458..467 range) should appear for season 30.
    bool sawDecor = false;
    for (int v : g_decorIds) if (v >= 458 && v <= 467) sawDecor = true;
    CHECK(sawDecor);
}

// ---------------------------------------------------------------------------
// SyncMovableObjects: bounded walk emits the per-record bundle.
// ---------------------------------------------------------------------------
namespace {
struct MovTrace { int begin=0, state=0, quad=0, guard=0, req17=0; };
MovTrace g_mov;
void m_begin(guild::i32) { g_mov.begin++; }
void m_append(guild::u32, guild::u32) {}
void m_state() { g_mov.state++; }
guild::i32 m_quad(guild::i32, guild::i32) { g_mov.quad++; return 0; }
guild::i32 m_guard(guild::i32, guild::i32) { g_mov.guard++; return 0; }
void m_req17(guild::i32, guild::i32, guild::i32, guild::i32) { g_mov.req17++; }
} // namespace

TEST(Scene2ReconMovable, BoundedWalkEmitsBundlePerRecord) {
    SceneHooks h;
    h.CommandBeginDeltaPacket = m_begin;
    h.CommandAppendDeltaField = m_append;
    h.CommandQueueRequestState22 = m_state;
    h.CommandQueueRequestQuad56 = m_quad;
    h.CommandQueueRequestGuardTarget61 = m_guard;
    h.CommandQueueRequest17 = m_req17;
    g_mov = MovTrace{};
    Scene_SyncMovableObjects(h, 4);
    CHECK_EQ(g_mov.begin, 4);
    CHECK_EQ(g_mov.state, 4);
    CHECK_EQ(g_mov.quad, 4);
    CHECK_EQ(g_mov.guard, 4);
    CHECK_EQ(g_mov.req17, 4);
}

// ---------------------------------------------------------------------------
// SyncCityBuildings (partial skeleton): tail call sequence + SyncRange barriers.
// ---------------------------------------------------------------------------
namespace {
struct CityTrace {
    int rangeStart=0, rangeEnd=0, offices=0, tick=0;
    int ai109=0, ai107=0, ai122=0, ai43=0, progress=0;
};
CityTrace g_city;
void c_rstart() { g_city.rangeStart++; }
void c_rend()   { g_city.rangeEnd++; }
bool c_acked()  { return true; }   // immediately acked -> no drain spin
void c_offices(){ g_city.offices++; }
void c_tick()   { g_city.tick++; }
void c_109()    { g_city.ai109++; }
void c_107()    { g_city.ai107++; }
void c_122()    { g_city.ai122++; }
void c_43()     { g_city.ai43++; }
void c_progress(){ g_city.progress++; }
guild::i32 c_personbegin(guild::i32, guild::i32, guild::i32) { return 0; } // no persons
} // namespace

TEST(Scene2ReconCity, SkeletonRunsBarriersAndTailSequence) {
    SceneState s;
    s.dword_63C7A8 = 0;   // skip the optional placement pass
    s.byte_63C8F4 = 0;
    SceneHooks h;
    h.LoadingUpdateProgressBar = c_progress;
    h.CommandMarkSyncRangeStart = c_rstart;
    h.CommandMarkSyncRangeEnd = c_rend;
    h.CommandCheckSyncRangeAcked = c_acked;
    h.SceneSyncBuildingAndOffices = c_offices;
    h.CityTickStatsAndBroadcast = c_tick;
    h.MeisterAiRequestCmd109 = c_109;
    h.MeisterAiRequestCmd107 = c_107;
    h.MeisterAiRequestCmd122 = c_122;
    h.MeisterAiRequestBuildingCmd43 = c_43;
    h.PersonQueryBegin = c_personbegin;
    g_city = CityTrace{};

    Scene_SyncCityBuildings(s, h);

    // exactly one final SyncRange barrier (placement pass skipped).
    CHECK_EQ(g_city.rangeStart, 1);
    CHECK_EQ(g_city.rangeEnd, 1);
    CHECK_EQ(g_city.offices, 1);
    CHECK_EQ(g_city.tick, 1);
    CHECK_EQ(g_city.ai109, 1);
    CHECK_EQ(g_city.ai107, 1);
    CHECK_EQ(g_city.ai122, 1);
    CHECK_EQ(g_city.ai43, 1);
    CHECK_EQ(g_city.progress, 3); // three progress-bar updates in the skeleton
}

TEST(Scene2ReconCity, PlacementPassAddsSecondBarrier) {
    SceneState s;
    s.dword_63C7A8 = 1;   // run the optional placement pass too
    SceneHooks h;
    h.CommandMarkSyncRangeStart = c_rstart;
    h.CommandMarkSyncRangeEnd = c_rend;
    h.CommandCheckSyncRangeAcked = c_acked;
    h.SceneSyncBuildingAndOffices = c_offices;
    h.CityTickStatsAndBroadcast = c_tick;
    h.MeisterAiRequestCmd109 = c_109;
    h.MeisterAiRequestCmd107 = c_107;
    h.MeisterAiRequestCmd122 = c_122;
    h.MeisterAiRequestBuildingCmd43 = c_43;
    h.PersonQueryBegin = c_personbegin;
    g_city = CityTrace{};
    Scene_SyncCityBuildings(s, h);
    CHECK_EQ(g_city.rangeStart, 2); // placement + final
    CHECK_EQ(g_city.rangeEnd, 2);
}

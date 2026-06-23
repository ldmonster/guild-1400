// ===========================================================================
// Golden-vector tests for gilde.exe 0x50f0c0 — VIBE_Scene_RunMainFrameLoop
// (src/play/scene_main_loop.{h,cpp}). Gates, ordering and state transitions
// are observed through a recording SceneMainLoopHooks.
// ===========================================================================
#include "test.h"

#include <string>
#include <vector>

#include "play/scene_main_loop.h"

using namespace guild;
using namespace guild::play;

namespace {

// ---------------------------------------------------------------------------
// Recording hooks with small fixtures for the entity-coupled accessors.
// ---------------------------------------------------------------------------
struct RecHooks : SceneMainLoopHooks {
    std::vector<std::string> log;

    // frame driver: run `framesToRun` bodies, then stop.
    i32 framesToRun = 0;
    i32 finalRunResult = 0;            // returned by the teardown call
    u32 lastMask = 0;
    std::vector<u32> ownerTokens;      // every ownerProc seen

    // family fixture: parallel arrays indexed by record.
    struct Fam { u16 slot; u8 type; i32 owner; };
    std::vector<Fam> family;           // records beyond size() read as empty
    i32 familyProbes = 0;

    // person iterator fixture (shared by both QueryBegin shapes).
    std::vector<i32> persons;          // handles yielded in order
    size_t iter = 0;
    i32 masterPerson = 0;
    u8 jailFlag = 0;
    // per-person script handles keyed by handle value (small linear map).
    std::vector<std::pair<i32, i32>> scriptHandles;

    // building fixture.
    u8  flag5A = 0;
    i32 dword27 = 0;
    i32 dword61 = 0;
    u16 extWord0 = 0;
    bool isProduction = false;
    bool isStorage = false;
    i16 selFlagsResult = 0;
    i32 slot60Result = 0;

    void note(const char* s) { log.push_back(s); }

    // --- Begin callees ---
    void sceneActivateAndRefreshCharacters() override { note("activate"); }
    void floorComputeSlopeFlags(i32 ctx) override {
        log.push_back("slope(" + std::to_string(ctx) + ")");
    }
    void panelRunChooseWappen(u16 slot) override {
        log.push_back("wappen(" + std::to_string(slot) + ")");
    }
    void ambientStartMarketLoop() override { note("marketStart"); }
    void timeBaseSetClockProc(i32 pause) override {
        log.push_back("clockProc(" + std::to_string(pause) + ")");
    }

    // --- frame driver ---
    i32 runFrameLoop(u32 mask, u32 owner) override {
        lastMask = mask;
        ownerTokens.push_back(owner);
        if (owner == 0) {              // teardown call
            note("runFrameLoop(exit)");
            return finalRunResult;
        }
        note("runFrameLoop");
        if (framesToRun > 0) { --framesToRun; return 1; }
        return 0;
    }

    // --- per-frame callees ---
    void buildingTeleportPlayerToJail() override { note("jail"); }
    void cameraUpdate() override { note("cameraUpdate"); }
    void sound3dUpdateListenerForScene() override { note("listener"); }
    void cameraZoomIn(i32 obj) override {
        log.push_back("zoomIn(" + std::to_string(obj) + ")");
    }
    void cameraZoomReset(i32 obj) override {
        log.push_back("zoomReset(" + std::to_string(obj) + ")");
    }
    void buildingOpenGebaeudeBauenWindow() override { note("bauenWindow"); }
    i16 buildingComputeSelectionFlags(u16 player, i32 b) override {
        log.push_back("selFlags(" + std::to_string(player) + "," +
                      std::to_string(b) + ")");
        return selFlagsResult;
    }
    bool buildingIsProductionType(i32) override {
        note("isProd");
        return isProduction;
    }
    bool buildingIsStorageType(i32) override {
        note("isStore");
        return isStorage;
    }
    i32 interactionInvokeHandlerSlot60(i32 op, i32 b, i32 param) override {
        log.push_back("slot60(" + std::to_string(op) + "," + std::to_string(b) +
                      "," + std::to_string(param) + ")");
        return slot60Result;
    }
    void buildingEnterForeignShop(i32 b) override {
        log.push_back("foreignShop(" + std::to_string(b) + ")");
    }
    void buildingEnterAndDispatch(i32 b, i32 param) override {
        log.push_back("enter(" + std::to_string(b) + "," +
                      std::to_string(param) + ")");
    }
    void marketStallRouteContact(i32 c) override {
        log.push_back("stall(" + std::to_string(c) + ")");
    }
    void objectSpawnChimneySmoke(i32 p) override {
        log.push_back("smoke(" + std::to_string(p) + ")");
    }
    i32 scriptFindByHandle(i32 handle) override {
        // model: script ctx == handle + 1000 when "found" (handle >= 0)
        return handle >= 0 ? handle + 1000 : 0;
    }
    void scriptFinish(i32 script) override {
        log.push_back("finish(" + std::to_string(script) + ")");
    }

    // --- teardown callees ---
    void ambientStopMarketLoop() override { note("marketStop"); }
    void widgetSetTooltipText(const char*) override { note("tooltip"); }
    void hudSetStatusBannerText(const char*) override { note("banner"); }
    void selectionReset() override { note("selReset"); }
    void coordConvertY(i32 a, i32 b) override {
        log.push_back("convertY(" + std::to_string(a) + "," +
                      std::to_string(b) + ")");
    }

    // --- entity accessors ---
    u16 familySlot(i32 rec) override {
        ++familyProbes;
        return rec < (i32)family.size() ? family[rec].slot : 0xFFFF;
    }
    u8 familyType(i32 rec) override {
        return rec < (i32)family.size() ? family[rec].type : 0;
    }
    i32 familyOwner(i32 rec) override {
        return rec < (i32)family.size() ? family[rec].owner : 0;
    }
    i32 playerMasterPerson(u16) override { return masterPerson; }
    u8  playerJailFlag(u16) override { return jailFlag; }
    i32 personQueryBegin_1_4(i32 key) override {
        log.push_back("query14(" + std::to_string(key) + ")");
        iter = 0;
        return iter < persons.size() ? persons[iter++] : 0;
    }
    i32 personQueryBegin_1_6() override {
        note("query16");
        iter = 0;
        return iter < persons.size() ? persons[iter++] : 0;
    }
    i32 personIterNext() override {
        return iter < persons.size() ? persons[iter++] : 0;
    }
    i32 personScriptHandle(i32 person) override {
        for (auto& kv : scriptHandles)
            if (kv.first == person) return kv.second;
        return -1;
    }
    void personSetScriptHandle(i32 person, i32 v) override {
        for (auto& kv : scriptHandles)
            if (kv.first == person) { kv.second = v; return; }
        scriptHandles.push_back({person, v});
    }
    u8  buildingFlagByte5A(i32) override { return flag5A; }
    void buildingSetWord29(i32 b, u16 v) override {
        log.push_back("setW29(" + std::to_string(b) + "," +
                      std::to_string(v) + ")");
    }
    i32 buildingDword27(i32) override { return dword27; }
    i32 buildingDword61(i32) override { return dword61; }
    u16 extObjectWord0(i32) override { return extWord0; }
};

bool Contains(const std::vector<std::string>& log, const std::string& s) {
    for (auto& e : log)
        if (e == s) return true;
    return false;
}

i32 IndexOf(const std::vector<std::string>& log, const std::string& s) {
    for (size_t i = 0; i < log.size(); ++i)
        if (log[i] == s) return (i32)i;
    return -1;
}

} // namespace

// ===========================================================================
// 0x58339c — season leaf.
// ===========================================================================
TEST(SceneMainLoop, SeasonFromDay) {
    CHECK_EQ(GameTime_GetSeasonFromDay(0), 0);
    CHECK_EQ(GameTime_GetSeasonFromDay(1), 1);
    CHECK_EQ(GameTime_GetSeasonFromDay(2), 2);
    CHECK_EQ(GameTime_GetSeasonFromDay(3), 3);
    CHECK_EQ(GameTime_GetSeasonFromDay(4), 0);
    CHECK_EQ(GameTime_GetSeasonFromDay(123), 3);
    // x86 idiv truncates toward zero: -1 % 4 == -1.
    CHECK_EQ(GameTime_GetSeasonFromDay(-1), -1);
    CHECK_EQ(GameTime_GetSeasonFromDay(-3), -3);
}

// ===========================================================================
// Smoke window — golden vectors from flt_6476FC / flt_64770C.
// ===========================================================================
TEST(SceneMainLoop, SmokeWindowGoldenVectors) {
    // season 0 (spring?): [8, 20)
    CHECK(!Scene_SmokeWindow(0, 7));
    CHECK(Scene_SmokeWindow(0, 8));
    CHECK(Scene_SmokeWindow(0, 19));
    CHECK(!Scene_SmokeWindow(0, 20));
    // season 1: [7, 21)
    CHECK(Scene_SmokeWindow(1, 7));
    CHECK(Scene_SmokeWindow(1, 20));
    CHECK(!Scene_SmokeWindow(1, 21));
    // season 2: [8, 20)
    CHECK(Scene_SmokeWindow(2, 8));
    CHECK(!Scene_SmokeWindow(2, 20));
    // season 3 (winter): [9, 19)
    CHECK(!Scene_SmokeWindow(3, 8));
    CHECK(Scene_SmokeWindow(3, 9));
    CHECK(Scene_SmokeWindow(3, 18));
    CHECK(!Scene_SmokeWindow(3, 19));
}

TEST(SceneMainLoop, SmokeWindowNegativeSeasonReadsTrueImage) {
    // A negative day (day%4 == -1) makes the original read .data BELOW the
    // start table: start = *(float*)0x6476F8 == 0.0 and end =
    // *(float*)0x647708 == 9.0 (the last start-table entry). hour 0..8 "in".
    CHECK(Scene_SmokeWindow(-1, 0));
    CHECK(Scene_SmokeWindow(-1, 8));
    CHECK(!Scene_SmokeWindow(-1, 9));
    // day%4 == -3: start = *0x6476F0 == 0.0, end = *0x647700 == 7.0.
    CHECK(Scene_SmokeWindow(-3, 6));
    CHECK(!Scene_SmokeWindow(-3, 7));
}

// ===========================================================================
// Begin — ordering, gates, snapshot.
// ===========================================================================
TEST(SceneMainLoop, BeginOrderAndFlags) {
    SceneMainLoopState s;
    RecHooks h;
    SceneMainLoopRun run;
    s.dword_13CE852 = 7;        // day -> season 3
    s.dword_62D0C4 = 11; s.dword_62D0C8 = 22;
    s.dword_62D0CC = 33; s.dword_62D0D0 = 44;
    SceneMainLoop_Begin(s, h, run);

    CHECK_EQ((int)run.season, 3);
    CHECK_EQ(s.dword_62D4F0, -1);          // ecx == -1 spill @0x50f0de
    CHECK_EQ(run.selFlags, 0);
    CHECK_EQ((int)s.byte_642008, 1);
    CHECK_EQ((int)s.byte_63CC40, 1);
    CHECK_EQ(run.smokePhase, 0);
    // camera snapshot captured
    CHECK_EQ(run.savedC4, 11);
    CHECK_EQ(run.savedC8, 22);
    CHECK_EQ(run.savedCC, 33);
    CHECK_EQ(run.savedD0, 44);
    // call order: activate -> marketStart -> clockProc(0); no slope, no wappen.
    CHECK_EQ(IndexOf(h.log, "activate"), 0);
    CHECK(IndexOf(h.log, "marketStart") >= 0);
    CHECK(IndexOf(h.log, "marketStart") < IndexOf(h.log, "clockProc(0)"));
    CHECK(!Contains(h.log, "slope(0)"));
}

TEST(SceneMainLoop, BeginSlopeRebuildGate) {
    SceneMainLoopState s;
    RecHooks h;
    SceneMainLoopRun run;
    s.dword_634498 = 5;
    s.dword_64A028 = 777;
    SceneMainLoop_Begin(s, h, run);
    CHECK(Contains(h.log, "slope(777)"));
    CHECK_EQ(s.dword_634498, 0);           // cleared with edx == 0
    // slope rebuild happens AFTER the character activation.
    CHECK(IndexOf(h.log, "activate") < IndexOf(h.log, "slope(777)"));
}

TEST(SceneMainLoop, BeginWappenScanMatch) {
    SceneMainLoopState s;
    RecHooks h;
    SceneMainLoopRun run;
    s.word_63C740 = 4;          // bit2 -> scan enabled
    s.word_63CC5C = 0;          // active player = record 0
    h.family = {
        {0, 1, 50},             // rec 0: the active player (owner 50)
        {7, 4, 50},             // rec 1: same owner, wrong type (4)
        {8, 5, 99},             // rec 2: right type, wrong owner
        {0, 6, 50},             // rec 3: slot == active -> skipped
        {9, 7, 50},             // rec 4: MATCH (type 7, owner 50, slot 9)
        {10, 5, 50},            // rec 5: would match but scan stopped
    };
    SceneMainLoop_Begin(s, h, run);
    CHECK(Contains(h.log, "wappen(9)"));
    CHECK(!Contains(h.log, "wappen(10)"));
}

TEST(SceneMainLoop, BeginWappenScanNoMatchProbesAll768) {
    SceneMainLoopState s;
    RecHooks h;
    SceneMainLoopRun run;
    s.word_63C740 = 4;
    SceneMainLoop_Begin(s, h, run);        // empty family table
    CHECK_EQ(h.familyProbes, kFamilyRecordCount);   // 0x64800 / 0x218 == 768
    CHECK(!Contains(h.log, "wappen(65535)"));
}

TEST(SceneMainLoop, BeginIntroZoomScan) {
    SceneMainLoopState s;
    RecHooks h;
    SceneMainLoopRun run;
    s.word_63C740 = 1;          // bit0
    s.dword_63CC2C = 1;         // mode must be exactly 1
    s.word_63CC5C = 3;
    h.masterPerson = 100;
    h.persons = {100, 100, 42, 77};
    SceneMainLoop_Begin(s, h, run);
    CHECK(Contains(h.log, "query14(3)"));
    CHECK(Contains(h.log, "zoomReset(42)"));   // first non-master person
    CHECK(!Contains(h.log, "zoomReset(77)"));  // scan stops at the first
}

TEST(SceneMainLoop, BeginIntroZoomScanGates) {
    // mode != 1 -> no query at all.
    {
        SceneMainLoopState s;
        RecHooks h;
        SceneMainLoopRun run;
        s.word_63C740 = 1;
        s.dword_63CC2C = 2;
        SceneMainLoop_Begin(s, h, run);
        CHECK(!Contains(h.log, "query14(0)"));
    }
    // all yielded persons are the master -> no zoom.
    {
        SceneMainLoopState s;
        RecHooks h;
        SceneMainLoopRun run;
        s.word_63C740 = 1;
        s.dword_63CC2C = 1;
        h.masterPerson = 100;
        h.persons = {100, 100};
        SceneMainLoop_Begin(s, h, run);
        CHECK(Contains(h.log, "query14(0)"));
        CHECK(!Contains(h.log, "zoomReset(100)"));
    }
}

// ===========================================================================
// StepFrame — loop condition, body ordering, gates.
// ===========================================================================
TEST(SceneMainLoop, StepLoopConditionAndOwnerToken) {
    SceneMainLoopState s;
    RecHooks h;
    SceneMainLoopRun run;
    h.framesToRun = 0;                     // condition immediately false
    CHECK(!SceneMainLoop_StepFrame(s, h, run));
    CHECK_EQ(h.lastMask, kSceneFrameLoopMask);          // edx == 0x67FFF
    CHECK_EQ(h.ownerTokens.size(), (size_t)1);
    CHECK_EQ(h.ownerTokens[0], kSceneMainLoopProc);     // eax == &self
    // no body work ran
    CHECK(!Contains(h.log, "cameraUpdate"));
    CHECK(!Contains(h.log, "listener"));
}

TEST(SceneMainLoop, StepBodyOrderAndLatch) {
    SceneMainLoopState s;
    RecHooks h;
    SceneMainLoopRun run;
    run.smokePhase = 3;                    // park the smoke machine
    s.dword_631610 = 9;
    h.framesToRun = 1;
    CHECK(SceneMainLoop_StepFrame(s, h, run));
    CHECK_EQ(s.dword_631618, 9);           // dword_631618 = dword_631610
    CHECK(!Contains(h.log, "jail"));       // jail flag 0
    // ordering: runFrameLoop -> cameraUpdate -> listener
    CHECK(IndexOf(h.log, "runFrameLoop") < IndexOf(h.log, "cameraUpdate"));
    CHECK(IndexOf(h.log, "cameraUpdate") < IndexOf(h.log, "listener"));
}

TEST(SceneMainLoop, StepJailGate) {
    SceneMainLoopState s;
    RecHooks h;
    SceneMainLoopRun run;
    run.smokePhase = 3;
    h.jailFlag = 1;
    h.framesToRun = 1;
    SceneMainLoop_StepFrame(s, h, run);
    CHECK(Contains(h.log, "jail"));
    // jail teleport precedes the camera update (0x50f1e5 < 0x50f1ea).
    CHECK(IndexOf(h.log, "jail") < IndexOf(h.log, "cameraUpdate"));
}

TEST(SceneMainLoop, SmokeMachineFullCycle) {
    SceneMainLoopState s;
    RecHooks h;
    SceneMainLoopRun run;
    run.season = 0;                        // window [8, 20)
    h.persons = {201, 202};
    h.scriptHandles = {{201, 5}, {202, -1}};

    // frame 1: hour 6 -> out of window, phase 0 -> clear pass, phase 1.
    s.word_13CE856 = 6;
    h.framesToRun = 1;
    SceneMainLoop_StepFrame(s, h, run);
    CHECK_EQ(run.smokePhase, 1);
    CHECK(Contains(h.log, "finish(1005)"));        // 201's script finished
    CHECK_EQ(h.personScriptHandle(201), -1);       // handle reset
    h.log.clear();

    // frame 2: hour 10 -> in window, phase 1 -> spawn pass, phase 2.
    s.word_13CE856 = 10;
    h.framesToRun = 1;
    SceneMainLoop_StepFrame(s, h, run);
    CHECK_EQ(run.smokePhase, 2);
    CHECK(Contains(h.log, "smoke(201)"));          // handle == -1 -> spawn
    CHECK(Contains(h.log, "smoke(202)"));
    h.log.clear();

    // frame 3: still in window, phase 2 -> nothing.
    h.framesToRun = 1;
    SceneMainLoop_StepFrame(s, h, run);
    CHECK_EQ(run.smokePhase, 2);
    CHECK(!Contains(h.log, "query16"));
    h.log.clear();

    // frame 4: hour 21 -> out of window, phase 2 -> clear pass, phase 3.
    s.word_13CE856 = 21;
    h.scriptHandles = {{201, 8}, {202, 9}};
    h.framesToRun = 1;
    SceneMainLoop_StepFrame(s, h, run);
    CHECK_EQ(run.smokePhase, 3);
    CHECK(Contains(h.log, "finish(1008)"));
    CHECK(Contains(h.log, "finish(1009)"));
    h.log.clear();

    // frame 5: phase 3 is terminal — nothing more, ever.
    s.word_13CE856 = 10;
    h.framesToRun = 1;
    SceneMainLoop_StepFrame(s, h, run);
    CHECK_EQ(run.smokePhase, 3);
    CHECK(!Contains(h.log, "query16"));
}

TEST(SceneMainLoop, SmokePhase0InWindowWaits) {
    // Entering the scene DURING the window keeps phase 0 (no spawn until the
    // window closes and reopens) — exact original behavior.
    SceneMainLoopState s;
    RecHooks h;
    SceneMainLoopRun run;
    run.season = 0;
    s.word_13CE856 = 12;                   // inside [8, 20)
    h.framesToRun = 1;
    SceneMainLoop_StepFrame(s, h, run);
    CHECK_EQ(run.smokePhase, 0);
    CHECK(!Contains(h.log, "query16"));
}

TEST(SceneMainLoop, AutoZoomInGate) {
    SceneMainLoopState s;
    RecHooks h;
    SceneMainLoopRun run;
    run.smokePhase = 3;
    s.dword_672224 = 1;
    s.dword_672234 = 0;
    s.dword_631730 = 555;
    s.dword_62D4E8 = 0;
    h.framesToRun = 1;
    SceneMainLoop_StepFrame(s, h, run);
    CHECK(Contains(h.log, "zoomIn(555)"));

    // any spoiler kills it
    for (int k = 0; k < 3; ++k) {
        RecHooks h2;
        SceneMainLoopRun r2;
        r2.smokePhase = 3;
        SceneMainLoopState s2 = s;
        if (k == 0) s2.dword_672224 = 0;
        if (k == 1) s2.dword_672234 = 1;
        if (k == 2) s2.dword_62D4E8 = 1;
        h2.framesToRun = 1;
        SceneMainLoop_StepFrame(s2, h2, r2);
        CHECK(!Contains(h2.log, "zoomIn(555)"));
    }
}

TEST(SceneMainLoop, BauenWindowGate) {
    SceneMainLoopState s;
    RecHooks h;
    SceneMainLoopRun run;
    run.smokePhase = 3;
    s.dword_75BF38 = 7;                    // != -1
    s.dword_62D22C = 4;
    s.dword_63178C = 4;                    // equal -> window
    h.framesToRun = 1;
    SceneMainLoop_StepFrame(s, h, run);
    CHECK(Contains(h.log, "bauenWindow"));

    RecHooks h2;
    SceneMainLoopRun r2;
    r2.smokePhase = 3;
    s.dword_63178C = 5;                    // not equal
    h2.framesToRun = 1;
    SceneMainLoop_StepFrame(s, h2, r2);
    CHECK(!Contains(h2.log, "bauenWindow"));
}

TEST(SceneMainLoop, SelectionFlagsLatchSignExtendsAndPersists) {
    SceneMainLoopState s;
    RecHooks h;
    SceneMainLoopRun run;
    run.smokePhase = 3;
    s.dword_11BC278 = 900;
    s.word_63CC5C = 2;
    h.selFlagsResult = (i16)0xFFFF;        // ax = -1 -> cwde -> -1
    h.framesToRun = 1;
    SceneMainLoop_StepFrame(s, h, run);
    CHECK(Contains(h.log, "selFlags(2,900)"));
    CHECK_EQ(run.selFlags, -1);

    // var_28 is NOT reset per frame: with the request gone the latch persists.
    s.dword_11BC278 = 0;
    h.framesToRun = 1;
    SceneMainLoop_StepFrame(s, h, run);
    CHECK_EQ(run.selFlags, -1);
}

TEST(SceneMainLoop, AutoEnterRequest1Production) {
    SceneMainLoopState s;
    RecHooks h;
    SceneMainLoopRun run;
    run.smokePhase = 3;
    s.dword_11BC278 = 900;
    s.dword_631730 = 900;                  // must equal the selection
    s.dword_67222C = 1;                    // first alternative of the OR-gate
    h.selFlagsResult = 1;                  // bit0 set
    h.isProduction = true;
    h.slot60Result = 1;
    h.framesToRun = 1;
    SceneMainLoop_StepFrame(s, h, run);
    CHECK(Contains(h.log, "slot60(27,900,0)"));
    CHECK(Contains(h.log, "foreignShop(900)"));

    // slot60 != 1 -> no enter.
    RecHooks h2 = RecHooks();
    SceneMainLoopRun r2; r2.smokePhase = 3;
    h2.selFlagsResult = 1; h2.isProduction = true; h2.slot60Result = 0;
    h2.framesToRun = 1;
    SceneMainLoop_StepFrame(s, h2, r2);
    CHECK(Contains(h2.log, "slot60(27,900,0)"));
    CHECK(!Contains(h2.log, "foreignShop(900)"));
}

TEST(SceneMainLoop, AutoEnterRequest1NonProduction) {
    SceneMainLoopState s;
    RecHooks h;
    SceneMainLoopRun run;
    run.smokePhase = 3;
    s.dword_11BC278 = 900;
    s.dword_631730 = 900;
    s.dword_67221C = 1;                    // second alternative...
    s.word_62D310 = 11;                    // ...needs panel mode 11
    h.selFlagsResult = 1;
    h.isProduction = false;
    h.isStorage = false;
    h.dword27 = 0x00050000;                // >>16 == 5
    h.slot60Result = 1;
    h.framesToRun = 1;
    SceneMainLoop_StepFrame(s, h, run);
    CHECK(Contains(h.log, "slot60(25,900,5)"));
    CHECK(Contains(h.log, "enter(900,5)"));

    // storage type -> neither slot60 nor enter.
    RecHooks h2;
    SceneMainLoopRun r2; r2.smokePhase = 3;
    h2.selFlagsResult = 1; h2.isStorage = true;
    h2.framesToRun = 1;
    SceneMainLoop_StepFrame(s, h2, r2);
    CHECK(!Contains(h2.log, "slot60(25,900,5)"));
    CHECK(!Contains(h2.log, "enter(900,5)"));

    // busy bit (+0x5A & 1) suppresses everything.
    RecHooks h3;
    SceneMainLoopRun r3; r3.smokePhase = 3;
    h3.selFlagsResult = 1; h3.flag5A = 1;
    h3.framesToRun = 1;
    SceneMainLoop_StepFrame(s, h3, r3);
    CHECK(!Contains(h3.log, "isProd"));
}

TEST(SceneMainLoop, MarketStallGate) {
    SceneMainLoopState s;
    RecHooks h;
    SceneMainLoopRun run;
    run.smokePhase = 3;
    s.dword_6477A4 = 314;
    h.framesToRun = 1;
    SceneMainLoop_StepFrame(s, h, run);
    CHECK(Contains(h.log, "stall(314)"));
}

TEST(SceneMainLoop, AutoEnterRequest2ZoomOnly) {
    // ext == 0: ZoomIn(rec) and the request IS consumed.
    SceneMainLoopState s;
    RecHooks h;
    SceneMainLoopRun run;
    run.smokePhase = 3;
    s.dword_11BC27C = 1;
    s.dword_11BC280 = 700;
    s.dword_11BC284 = 0;
    h.framesToRun = 1;
    SceneMainLoop_StepFrame(s, h, run);
    CHECK(Contains(h.log, "zoomIn(700)"));
    CHECK_EQ(s.dword_11BC27C, 0);
}

TEST(SceneMainLoop, AutoEnterRequest2BusyConsumes) {
    // ext != 0 and (+0x5A & 1): word copy happens, request consumed, no enter.
    SceneMainLoopState s;
    RecHooks h;
    SceneMainLoopRun run;
    run.smokePhase = 3;
    s.dword_11BC27C = 1;
    s.dword_11BC280 = 700;
    s.dword_11BC284 = 12;
    h.extWord0 = 42;
    h.flag5A = 1;
    h.framesToRun = 1;
    SceneMainLoop_StepFrame(s, h, run);
    CHECK(Contains(h.log, "setW29(700,42)"));
    CHECK_EQ(s.dword_11BC27C, 0);
    CHECK(!Contains(h.log, "isProd"));
}

TEST(SceneMainLoop, AutoEnterRequest2DispatchKeepsRequest) {
    // ext != 0, flag clear: LABEL_48 dispatch path; dword_11BC27C is NOT
    // cleared by this function (the Enter* callee owns the request).
    SceneMainLoopState s;
    RecHooks h;
    SceneMainLoopRun run;
    run.smokePhase = 3;
    s.dword_11BC27C = 1;
    s.dword_11BC280 = 700;
    s.dword_11BC284 = 12;
    h.dword61 = 1;                         // zoom-state present -> ZoomReset
    h.isProduction = true;
    h.framesToRun = 1;
    SceneMainLoop_StepFrame(s, h, run);
    CHECK(Contains(h.log, "zoomReset(700)"));
    CHECK(Contains(h.log, "foreignShop(700)"));
    CHECK_EQ(s.dword_11BC27C, 1);          // still pending
}

TEST(SceneMainLoop, AutoEnterRequest2StorageDoubleCheck) {
    // Non-production, storage: the original calls IsStorageType TWICE
    // (0x50f5c8 then 0x50f5e5) before ZoomIn — both calls preserved.
    SceneMainLoopState s;
    RecHooks h;
    SceneMainLoopRun run;
    run.smokePhase = 3;
    s.dword_11BC27C = 1;
    s.dword_11BC280 = 700;
    s.dword_11BC284 = 12;
    h.isStorage = true;
    h.framesToRun = 1;
    SceneMainLoop_StepFrame(s, h, run);
    int stores = 0;
    for (auto& e : h.log)
        if (e == "isStore") ++stores;
    CHECK_EQ(stores, 2);
    CHECK(Contains(h.log, "zoomIn(700)"));
    CHECK(!Contains(h.log, "enter(700,0)"));
    CHECK_EQ(s.dword_11BC27C, 1);

    // Neither production nor storage -> EnterAndDispatch(rec, +0x27 >> 16).
    RecHooks h2;
    SceneMainLoopRun r2; r2.smokePhase = 3;
    h2.dword27 = 0x00090000;
    h2.framesToRun = 1;
    SceneMainLoop_StepFrame(s, h2, r2);
    CHECK(Contains(h2.log, "enter(700,9)"));
    CHECK_EQ(s.dword_11BC27C, 1);
}

// ===========================================================================
// End — teardown ordering, register-artifact stores, snapshot restore.
// ===========================================================================
TEST(SceneMainLoop, EndTeardown) {
    SceneMainLoopState s;
    RecHooks h;
    SceneMainLoopRun run;
    run.savedC4 = 11; run.savedC8 = 22; run.savedCC = 33; run.savedD0 = 44;
    s.byte_642008 = 1;
    s.byte_63CC40 = 1;
    s.word_62D310 = 11;
    s.dword_62D314 = 5;
    s.word_75BF48 = -7;                    // sign-extended into edx
    s.word_75BF4A = 1234;                  // sign-extended into eax
    s.dword_62D0C4 = 999;                  // mutated during the loop
    h.finalRunResult = 0;

    i32 r = SceneMainLoop_End(s, h, run);
    CHECK_EQ(r, 0);
    CHECK_EQ((int)s.byte_642008, 0);
    CHECK_EQ((int)s.byte_63CC40, 0);
    CHECK_EQ((int)s.word_62D310, 0);       // cx == 0
    CHECK_EQ(s.dword_62D314, 0);           // edx == 0
    CHECK_EQ(s.dword_62D0D4, 1);           // ecx == 1
    CHECK(Contains(h.log, "convertY(1234,-7)"));
    // snapshot restored
    CHECK_EQ(s.dword_62D0C4, 11);
    CHECK_EQ(s.dword_62D0C8, 22);
    CHECK_EQ(s.dword_62D0CC, 33);
    CHECK_EQ(s.dword_62D0D0, 44);
    // ordering: marketStop -> tooltip -> banner -> selReset -> convertY ->
    // final runFrameLoop(owner == 0)
    CHECK(IndexOf(h.log, "marketStop") < IndexOf(h.log, "tooltip"));
    CHECK(IndexOf(h.log, "tooltip") < IndexOf(h.log, "banner"));
    CHECK(IndexOf(h.log, "banner") < IndexOf(h.log, "selReset"));
    CHECK(IndexOf(h.log, "selReset") < IndexOf(h.log, "convertY(1234,-7)"));
    CHECK(IndexOf(h.log, "convertY(1234,-7)") <
          IndexOf(h.log, "runFrameLoop(exit)"));
    CHECK_EQ(h.ownerTokens.back(), 0u);    // eax == 0 at the teardown call
}

// ===========================================================================
// Whole-function form (e2e shape): Begin; N bodies; teardown.
// ===========================================================================
TEST(SceneMainLoop, RunWholeFunction) {
    SceneMainLoopState s;
    RecHooks h;
    h.framesToRun = 3;
    s.word_13CE856 = 23;                   // out of every window: phases 0->1
    i32 r = SceneMainLoop_Run(s, h);
    CHECK_EQ(r, 0);
    // 3 in-loop pumps + 1 failing head + 1 teardown call = 5 runFrameLoop.
    CHECK_EQ(h.ownerTokens.size(), (size_t)5);
    for (size_t i = 0; i + 1 < h.ownerTokens.size(); ++i)
        CHECK_EQ(h.ownerTokens[i], kSceneMainLoopProc);
    CHECK_EQ(h.ownerTokens.back(), 0u);
    int cams = 0;
    for (auto& e : h.log)
        if (e == "cameraUpdate") ++cams;
    CHECK_EQ(cams, 3);                     // exactly one body per pump
    // session flags raised in Begin, dropped in End.
    CHECK_EQ((int)s.byte_642008, 0);
    CHECK_EQ((int)s.byte_63CC40, 0);
    CHECK_EQ(s.dword_62D4F0, -1);
}

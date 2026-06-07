// Unit tests for src/world/event4.cpp — the larger VIBE_Event_* "Run" phase
// machines (harvest wage/process, work, night-watchman, conversation/collapse,
// building He-state walkers). The RNG-driven wage paths are checked against the
// real CRT LCG (crt::RandNext / Srand) by recomputing the exact draw the body
// makes; the GameTime arithmetic is checked against VIBE_GameTime_Advance.
#include "test.h"

#include "world/event4.h"
#include "sim/he.h"
#include "sim/npcaction.h"
#include "sim/gametime.h"
#include "util/math_random.h"
#include "crt/rand.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;
using guild::world::Event4Hooks;
using guild::world::SetEvent4Hooks;
using guild::world::GetEvent4Hooks;
using guild::sim::HeRecord;
using guild::sim::HeBytes;
using guild::sim::GameTime;
using guild::sim::SetNpcClock;

namespace {

i32&      RD(HeRecord* h, int off) { return *reinterpret_cast<i32*>(HeBytes(h) + off); }
u16&      RW(HeRecord* h, int off) { return *reinterpret_cast<u16*>(HeBytes(h) + off); }
GameTime& RT(HeRecord* h, int off) { return *reinterpret_cast<GameTime*>(HeBytes(h) + off); }

HeRecord MakeHe() { HeRecord h; std::memset(&h, 0, sizeof(h)); return h; }

// --- recording mock ---------------------------------------------------------
struct Rec {
    int frees = 0;
    int changeActions = 0;
    std::vector<u16> actionCharIds;
    std::vector<i32> queueQty;
    std::vector<int> samples;          // priority of each Voice sample
    std::vector<int> restoreModes;     // mode of each universeRestoreObjectStates
    // a small "person record" for ConversationSink (class byte at +2, flags etc).
    u8 person[600] = {0};
    int audio = 1;
    int announce = 1;
    void* sceneNodes[64] = {0};
};
Rec* g_r = nullptr;

i32   mFree(HeRecord*) { if (g_r) g_r->frees++; return 0; }
void* mActiveHe(int) { return nullptr; }      // no person drives an action by default
u16   mActionCharId(int) { return 0; }
void  mChange(void*, void*, void*, u16 c) { if (g_r) { g_r->changeActions++; g_r->actionCharIds.push_back(c); } }
i32   mQueue17(i32, i32, i32 qty, i32, i32, i32) { if (g_r) g_r->queueQty.push_back(qty); return 1; }
void  mSample(i32, i32, i32, const char*, i32 prio) { if (g_r) g_r->samples.push_back(prio); }
i32   mAudio() { return g_r ? g_r->audio : 0; }
i32   mAnnounce() { return g_r ? g_r->announce : 0; }
void* mFindPerson(i32) { return g_r ? g_r->person : nullptr; }
void  mRestore(void*, i32 mode) { if (g_r) g_r->restoreModes.push_back(mode); }

// Install a table copied from the inert defaults, then override a few entries.
Event4Hooks BaseHooks() {
    SetEvent4Hooks(nullptr);
    Event4Hooks h = GetEvent4Hooks();
    h.freeHandlerEntry = mFree;
    h.activeActionHe = mActiveHe;
    h.personActionCharId = mActionCharId;
    h.changePlayerAction = mChange;
    return h;
}

}  // namespace

// ---------------------------------------------------------------------------
// HarvestWageRun
// ---------------------------------------------------------------------------
TEST(Event4Unit, HarvestWageTeardownCancelsAndFrees) {
    Rec r; g_r = &r;
    Event4Hooks h = BaseHooks();
    SetEvent4Hooks(&h);

    HeRecord he = MakeHe();
    RD(&he, 112) = -1;                 // teardown phase
    i32 rv = world::HarvestWageRun(&he);

    SetEvent4Hooks(nullptr); g_r = nullptr;
    CHECK_EQ(rv, 0);                   // inert free returns 0
    CHECK_EQ(r.frees, 1);
}

TEST(Event4Unit, HarvestWagePhase0Increments) {
    Rec r; g_r = &r;
    Event4Hooks h = BaseHooks();
    SetEvent4Hooks(&h);

    HeRecord he = MakeHe();
    RD(&he, 112) = 0;
    i32 rv = world::HarvestWageRun(&he);

    SetEvent4Hooks(nullptr); g_r = nullptr;
    CHECK_EQ(RD(&he, 112), 1);
    CHECK_EQ(rv, 2);                   // returns counter+2
    CHECK_EQ(r.frees, 0);
}

TEST(Event4Unit, HarvestWageNoWorkFrees) {
    Rec r; g_r = &r;
    Event4Hooks h = BaseHooks();
    SetEvent4Hooks(&h);

    HeRecord he = MakeHe();
    RD(&he, 112) = 1;
    RD(&he, 20) = 0; RD(&he, 24) = 0;  // no work amount -> free
    world::HarvestWageRun(&he);

    SetEvent4Hooks(nullptr); g_r = nullptr;
    CHECK_EQ(r.frees, 1);
}

TEST(Event4Unit, HarvestWageNotDueReturnsCompare) {
    Rec r; g_r = &r;
    Event4Hooks h = BaseHooks();
    SetEvent4Hooks(&h);

    HeRecord he = MakeHe();
    RD(&he, 112) = 1;
    RD(&he, 20) = 5;                   // has work
    // appointment in the FUTURE relative to clock -> Compare >= 0 -> early return.
    GameTime clk{}; clk.day = 2; clk.hour = 8; clk.minute = 0; clk.second = 0;
    SetNpcClock(clk);
    RT(&he, 82).day = 2; RT(&he, 82).hour = 12;  // appt later than clock
    i32 rv = world::HarvestWageRun(&he);

    SetEvent4Hooks(nullptr); g_r = nullptr;
    CHECK(rv >= 0);                    // Compare returned +1 (appt > clock)
    CHECK_EQ(r.frees, 0);              // not freed, just polled
}

// Golden wage path against the REAL CRT LCG.
TEST(Event4Unit, HarvestWageGoldenWageFromRealLcg) {
    Rec r; g_r = &r;
    Event4Hooks h = BaseHooks();
    // resolve writes a 600-byte record whose +54 word is the base.
    static u8 wageRec[600];
    std::memset(wageRec, 0, sizeof(wageRec));
    *reinterpret_cast<u16*>(wageRec + 54) = 3;   // base = 3
    h.resolveEntityById = [](void** a, i32* b, i32, i32) {
        if (a) *a = wageRec;
        if (b) *b = 0;
    };
    h.queueRequest17 = mQueue17;
    h.renderFormattedMessage = [](char*, i32, i32, void*, i32) {};
    h.sendQuickjumpMessage = [](i32, const char*, i32, const char*) {};
    SetEvent4Hooks(&h);

    HeRecord he = MakeHe();
    RD(&he, 112) = 1;
    RD(&he, 20) = 4; RD(&he, 24) = 1;            // amt = 5
    RD(&he, 170) = 0;
    GameTime clk{}; clk.day = 5; clk.hour = 10;
    SetNpcClock(clk);
    RT(&he, 82).day = 5; RT(&he, 82).hour = 9;   // appt in the past -> due

    crt::Srand(777);
    world::HarvestWageRun(&he);

    SetEvent4Hooks(nullptr); g_r = nullptr;

    // Recompute the exact wage: (amt) * (40*base + RandomModulo(40*base)).
    int base = 3;
    crt::Srand(777);
    int draw = util::RandomModulo(40 * base);
    int expectWage = 5 * (40 * base + static_cast<u16>(draw));
    CHECK_EQ(r.queueQty.size() == 1u, true);
    if (!r.queueQty.empty()) CHECK_EQ(r.queueQty[0], expectWage);
    CHECK_EQ(r.frees, 1);
}

// ---------------------------------------------------------------------------
// WorkActionRun
// ---------------------------------------------------------------------------
TEST(Event4Unit, WorkActionPhase0Increments) {
    Rec r; g_r = &r;
    Event4Hooks h = BaseHooks();
    SetEvent4Hooks(&h);

    HeRecord he = MakeHe();
    RD(&he, 112) = 0;
    i32 rv = world::WorkActionRun(&he);

    SetEvent4Hooks(nullptr); g_r = nullptr;
    CHECK_EQ(RD(&he, 112), 1);
    CHECK_EQ(rv, 1);
}

TEST(Event4Unit, WorkActionPhase1NoWorkGoesToTwo) {
    Rec r; g_r = &r;
    Event4Hooks h = BaseHooks();
    SetEvent4Hooks(&h);

    HeRecord he = MakeHe();
    RD(&he, 112) = 1;
    RD(&he, 20) = 0; RD(&he, 24) = 0;
    world::WorkActionRun(&he);

    SetEvent4Hooks(nullptr); g_r = nullptr;
    CHECK_EQ(RD(&he, 112), 2);
}

TEST(Event4Unit, WorkActionPhase2ReworkGoesToOne) {
    Rec r; g_r = &r;
    Event4Hooks h = BaseHooks();
    SetEvent4Hooks(&h);

    HeRecord he = MakeHe();
    RD(&he, 112) = 2;
    RD(&he, 20) = 7;                   // work re-appeared
    world::WorkActionRun(&he);

    SetEvent4Hooks(nullptr); g_r = nullptr;
    CHECK_EQ(RD(&he, 112), 1);
}

TEST(Event4Unit, WorkActionPhase2MissingStationGoesToMinusOne) {
    Rec r; g_r = &r;
    Event4Hooks h = BaseHooks();      // inert resolve -> station null
    SetEvent4Hooks(&h);

    HeRecord he = MakeHe();
    RD(&he, 112) = 2;
    RD(&he, 20) = 0; RD(&he, 24) = 0;
    world::WorkActionRun(&he);

    SetEvent4Hooks(nullptr); g_r = nullptr;
    CHECK_EQ(RD(&he, 112), -1);
    CHECK_EQ(r.frees, 0);
}

// ---------------------------------------------------------------------------
// NightWatchmanAnnounceRun
// ---------------------------------------------------------------------------
TEST(Event4Unit, NightWatchmanWrongDayFrees) {
    Rec r; g_r = &r;
    Event4Hooks h = BaseHooks();
    SetEvent4Hooks(&h);

    HeRecord he = MakeHe();
    RD(&he, 68) = 4;                  // saved day
    GameTime clk{}; clk.day = 9;      // mismatch
    SetNpcClock(clk);
    world::NightWatchmanAnnounceRun(&he);

    SetEvent4Hooks(nullptr); g_r = nullptr;
    CHECK_EQ(r.frees, 1);
}

TEST(Event4Unit, NightWatchmanMorningOutsideWindowNudgesClock) {
    Rec r; g_r = &r;
    Event4Hooks h = BaseHooks();
    h.playQueuedSample = mSample;
    h.audioEnabled = mAudio; h.announceEnabled = mAnnounce;
    SetEvent4Hooks(&h);

    HeRecord he = MakeHe();
    RD(&he, 68) = 8;
    RD(&he, 112) = 0;                 // morning phase
    GameTime clk{}; clk.day = 8; clk.hour = 3;  // season 0 morning is 8; 3 < 8 -> outside
    SetNpcClock(clk);
    world::NightWatchmanAnnounceRun(&he);

    SetEvent4Hooks(nullptr); g_r = nullptr;
    // Outside the window (LABEL_23): no sample; +82 is stamped to the clock (day 8),
    // and the GLOBAL clock is nudged +1 second (the original's edx=0/ecx=1/ebx=0).
    CHECK_EQ(r.samples.size() == 0u, true);
    CHECK_EQ(RT(&he, 82).day, 8);
    CHECK_EQ(guild::sim::NpcClock().second, 1);
}

TEST(Event4Unit, NightWatchmanMorningInWindowAudioOnRingsBell) {
    Rec r; g_r = &r; r.announce = 0;  // announce off -> only the bell, deterministic
    Event4Hooks h = BaseHooks();
    h.playQueuedSample = mSample;
    h.audioEnabled = mAudio; h.announceEnabled = mAnnounce;
    SetEvent4Hooks(&h);

    HeRecord he = MakeHe();
    RD(&he, 68) = 8;
    RD(&he, 112) = 0;
    GameTime clk{}; clk.day = 8; clk.hour = 8;  // season 0 morning hour == 8 -> in window
    SetNpcClock(clk);
    world::NightWatchmanAnnounceRun(&he);

    SetEvent4Hooks(nullptr); g_r = nullptr;
    CHECK_EQ(r.samples.size() == 1u, true);     // just the bell
    CHECK_EQ(RD(&he, 112), 1);                  // advanced to evening phase
    CHECK_EQ((int)RW(&he, 86), 20);             // evening hour for season 0
}

// ---------------------------------------------------------------------------
// ConversationSinkRun
// ---------------------------------------------------------------------------
TEST(Event4Unit, ConversationMissingPersonFrees) {
    Rec r; g_r = &r;
    Event4Hooks h = BaseHooks();
    h.findPersonById = [](i32) -> void* { return nullptr; };
    SetEvent4Hooks(&h);

    HeRecord he = MakeHe();
    world::ConversationSinkRun(&he);

    SetEvent4Hooks(nullptr); g_r = nullptr;
    CHECK_EQ(r.frees, 1);
}

TEST(Event4Unit, ConversationLiveScriptRestamps) {
    Rec r; g_r = &r;
    // person rec[130] (+520) != -1 and counter != 6 -> just re-stamp +5 min.
    *reinterpret_cast<i32*>(r.person + 520) = 99;  // live route
    Event4Hooks h = BaseHooks();
    h.findPersonById = mFindPerson;
    SetEvent4Hooks(&h);

    HeRecord he = MakeHe();
    RD(&he, 112) = 2;
    GameTime clk{}; clk.day = 1; clk.hour = 6; clk.minute = 0;
    SetNpcClock(clk);
    world::ConversationSinkRun(&he);

    SetEvent4Hooks(nullptr); g_r = nullptr;
    CHECK_EQ(r.frees, 0);
    CHECK_EQ(RT(&he, 82).day, 1);
    CHECK_EQ(RT(&he, 82).minute, 5);   // +5 minutes
}

TEST(Event4Unit, ConversationPhase3ClassByteRoutes) {
    Rec r; g_r = &r;
    *reinterpret_cast<i32*>(r.person + 520) = -1;   // no live route -> run machine
    r.person[2] = 6;                                // class 6 -> request-39 route
    Event4Hooks h = BaseHooks();
    h.findPersonById = mFindPerson;
    h.queueRequest39 = [](const void*) -> i32 { return 55; };
    SetEvent4Hooks(&h);

    HeRecord he = MakeHe();
    RD(&he, 112) = 3;
    GameTime clk{}; clk.day = 1; clk.hour = 6;
    SetNpcClock(clk);
    world::ConversationSinkRun(&he);

    SetEvent4Hooks(nullptr); g_r = nullptr;
    CHECK_EQ(RD(&he, 112), 6);          // class 6/7 routes to phase 6
    CHECK_EQ(RD(&he, 180), 55);         // request-39 handle stored
}

TEST(Event4Unit, ConversationPhase4TargetFreesAfterBroadcast) {
    Rec r; g_r = &r;
    *reinterpret_cast<i32*>(r.person + 520) = -1;
    int target = 0;
    Event4Hooks h = BaseHooks();
    h.findPersonById = mFindPerson;
    h.selectConversationTarget = [](void*) -> void* { static int t; return &t; };
    h.broadcastFamilyNews = [](void*, void*, i32) {};
    h.findEmploymentRelation = [](void*) -> i32 { return 0; };
    h.queueRequestPair33 = [](i32, i32) {};
    SetEvent4Hooks(&h);
    (void)target;

    HeRecord he = MakeHe();
    RD(&he, 112) = 4;
    world::ConversationSinkRun(&he);

    SetEvent4Hooks(nullptr); g_r = nullptr;
    CHECK_EQ(r.frees, 1);
}

// ---------------------------------------------------------------------------
// QueryBuildingHeMax
// ---------------------------------------------------------------------------
TEST(Event4Unit, QueryBuildingHeMaxTakesMaxLevel) {
    Rec r; g_r = &r;
    static i32 levels[5] = {0, 3, 7, 2, 5};   // index 0 skipped (scan starts at 1)
    Event4Hooks h = BaseHooks();
    h.universeSwitchSlot = [](i32 s) -> i32 { return s == 0 ? 9 : 0; };
    h.collectHeNodes = [](i32, void** out, int) -> int {
        for (int i = 0; i < 5; ++i) out[i] = &levels[i];
        return 5;
    };
    h.nodeLevelCount = [](void*) -> int { return 1; };
    h.nodeLevel = [](void* n, int) -> i32 { return *reinterpret_cast<i32*>(n); };
    h.nodeSetLevelBase = [](void*, i32) {};
    SetEvent4Hooks(&h);

    HeRecord he = MakeHe();
    RD(&he, 4) = 42;
    i32 maxLvl = world::QueryBuildingHeMax(&he);

    SetEvent4Hooks(nullptr); g_r = nullptr;
    CHECK_EQ(maxLvl, 7);               // max of indices 1..4 = max(3,7,2,5)
}

TEST(Event4Unit, QueryBuildingHeMaxNoNodesReturnsZero) {
    Rec r; g_r = &r;
    Event4Hooks h = BaseHooks();      // inert collect -> 0 nodes
    SetEvent4Hooks(&h);

    HeRecord he = MakeHe();
    i32 maxLvl = world::QueryBuildingHeMax(&he);

    SetEvent4Hooks(nullptr); g_r = nullptr;
    CHECK_EQ(maxLvl, 0);
}

// ---------------------------------------------------------------------------
// UpdateBuildingHeState
// ---------------------------------------------------------------------------
TEST(Event4Unit, UpdateBuildingHeStateTargetMinusOneHidesAll) {
    Rec r; g_r = &r;
    static u8 nodeA[600], nodeB[600];
    std::memset(nodeA, 0, sizeof(nodeA)); std::memset(nodeB, 0, sizeof(nodeB));
    static void* root; int rootObj = 1; root = &rootObj;
    Event4Hooks h = BaseHooks();
    h.universeSwitchSlot = [](i32 s) -> i32 { return s; };
    h.findHeRootObject = [](i32) -> void* { return root; };
    h.collectHeNodes = [](i32, void** out, int) -> int { out[0] = nodeA; out[1] = nodeB; return 2; };
    h.nodeStateByte = [](void* n) -> i32 { return reinterpret_cast<u8*>(n)[535]; };
    h.nodeFlags = [](void* n) -> i32 { return reinterpret_cast<u8*>(n)[531]; };
    h.nodeSetStateByte = [](void* n, i32 v) { reinterpret_cast<u8*>(n)[535] = (u8)v; };
    h.nodeClearDetachedFlag = [](void* n) { reinterpret_cast<u8*>(n)[531] &= ~4u; };
    h.universeRestoreObjectStates = mRestore;
    SetEvent4Hooks(&h);

    HeRecord he = MakeHe();
    RD(&he, 200) = -1;                 // target = -1
    RD(&he, 196) = 3;                  // current
    world::UpdateBuildingHeState(&he);

    SetEvent4Hooks(nullptr); g_r = nullptr;
    CHECK_EQ((int)nodeA[535], 5);      // hidden
    CHECK_EQ((int)nodeB[535], 5);
    CHECK_EQ(r.restoreModes.size() == 1u, true);  // restored the root once
}

TEST(Event4Unit, UpdateBuildingHeStateLowerTargetShowsByLevel) {
    Rec r; g_r = &r;
    static u8 n0[600], n1[600];
    std::memset(n0, 0, sizeof(n0)); std::memset(n1, 0, sizeof(n1));
    static int rootObj = 1; static void* root = &rootObj;
    static i32 lvlOf[2] = {2, 5};
    static void* nodes2[2] = {n0, n1};
    Event4Hooks h = BaseHooks();
    h.universeSwitchSlot = [](i32 s) -> i32 { return s; };
    h.findHeRootObject = [](i32) -> void* { return root; };
    h.collectHeNodes = [](i32, void** out, int) -> int { out[0] = nodes2[0]; out[1] = nodes2[1]; return 2; };
    h.nodeStateByte = [](void*) -> i32 { return 5; };
    h.nodeFlags = [](void*) -> i32 { return 0; };
    h.nodeSetStateByte = [](void*, i32) {};
    h.nodeClearDetachedFlag = [](void*) {};
    h.nodeSetLevelBase = [](void*, i32) {};
    h.nodeLevelCount = [](void*) -> int { return 1; };
    h.nodeLevel = [](void* n, int) -> i32 { return n == nodes2[0] ? lvlOf[0] : lvlOf[1]; };
    h.universeRestoreObjectStates = mRestore;
    SetEvent4Hooks(&h);

    HeRecord he = MakeHe();
    RD(&he, 200) = 3;                  // target
    RD(&he, 196) = 9;                  // current > target -> the "lower" branch
    world::UpdateBuildingHeState(&he);

    SetEvent4Hooks(nullptr); g_r = nullptr;
    // node0 level 2 <= 3 -> shown (mode 1); node1 level 5 > 3 -> hidden (mode 0).
    CHECK_EQ(r.restoreModes.size() == 2u, true);
    if (r.restoreModes.size() == 2u) {
        CHECK_EQ(r.restoreModes[0], 1);
        CHECK_EQ(r.restoreModes[1], 0);
    }
}

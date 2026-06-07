// Unit tests for charaction_steps5 — batch 5 of the CharAction step leaves (the
// paired-entity finders, the transport-speed scaler, the office-guard init, the
// appointment-copy / state-reset leaves, and the follow-target + buy-object step
// machines). GameTime golden vectors were computed with python3 mirroring
// src/sim/gametime.cpp. The cross-cluster leaves (resolves, query iteration, the
// pool scan, render/emit bridge, framerate counters) are routed through a
// recording mock installed via SetCharActionStep5Hooks / SetNpcLeafHooks.
// Suite prefix: CharActionV5.
#include "test.h"

#include "sim/charaction_steps5.h"
#include "sim/he.h"
#include "sim/npcaction.h"   // NpcClock / SetNpcClock, SetNpcLeafHooks

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// A zeroed record buffer big enough for the +416 vehicle-speed access.
struct BigRec {
    unsigned char bytes[512];
    BigRec() { std::memset(bytes, 0, sizeof(bytes)); }
    HeRecord* get() { return reinterpret_cast<HeRecord*>(bytes); }
    template <class T> T& at(int off) { return *reinterpret_cast<T*>(bytes + off); }
};

GameTime Clock() { GameTime t{}; t.day = 10; t.hour = 8; t.minute = 30; t.second = 15; return t; }
bool TimeEq(const GameTime& t, i32 day, u16 hour, i32 minute, i32 second) {
    return t.day == day && t.hour == hour && t.minute == minute && t.second == second;
}

// --- recording NpcLeafHooks ------------------------------------------------
struct LeafRec {
    int freeCalls = 0; i32 freeRet = 7;
    int cmd29Calls = 0; std::vector<int> cmd29Args; i32 nextHandle = 4242;
    i32 statusRet = 0;
};
LeafRec g_leaf;
i32 RecFree(HeRecord*) { ++g_leaf.freeCalls; return g_leaf.freeRet; }
i32 RecCmd29(int a, HeRecord*) { ++g_leaf.cmd29Calls; g_leaf.cmd29Args.push_back(a); return g_leaf.nextHandle; }
i32 RecStatus(i32) { return g_leaf.statusRet; }
void InstallLeaf() {
    g_leaf = LeafRec{};
    static NpcLeafHooks h; h = NpcLeafHooks{};
    h.freeHandlerEntry = RecFree;
    h.queueRequestEntity29 = RecCmd29;
    h.packetStatus = RecStatus;
    SetNpcLeafHooks(&h);
}

// --- recording CharActionStep5Hooks ----------------------------------------
struct S5 {
    std::vector<HeRecord*> personById; HeRecord* personRet = nullptr;
    HeRecord* queryRet = nullptr;
    HeRecord* objectRet = nullptr;
    std::vector<HeRecord*> findSeq; size_t findCur = 0;
    int playerActionCalls = 0; u16 lastClass = 0;
    int req17Calls = 0; i32 req17Ret = 555; i32 req17ObjId = 0; int req17Hiword = 0;
    i32 seqRet = 0;
    int markBoughtCalls = 0; i32 markBoughtArg = 0;
    int slot28Calls = 0;
    int quickjumpCalls = 0; std::vector<i32> quickjumpRecipient;
    i32 cityPid = 0; u8 cityCat = 0;
    int rnd = 0;
    int fastCnt = 0, medCnt = 0;
};
S5 g_s;

HeRecord* HPerson(i32 id) {
    if (id >= 0 && (size_t)id < g_s.personById.size() && g_s.personById[id]) return g_s.personById[id];
    return g_s.personRet;
}
HeRecord* HQuery(i32, int, int, i32) { return g_s.queryRet; }
HeRecord* HObject(i32, int, int, i32) { return g_s.objectRet; }
HeRecord* HFindFirst(int, int, int, int) { g_s.findCur = 0; return g_s.findCur < g_s.findSeq.size() ? g_s.findSeq[g_s.findCur++] : nullptr; }
HeRecord* HFindNext() { return g_s.findCur < g_s.findSeq.size() ? g_s.findSeq[g_s.findCur++] : nullptr; }
void HPlayerAction(HeRecord*, HeRecord*, HeRecord*, u16 c) { ++g_s.playerActionCalls; g_s.lastClass = c; }
i32  HReq17(i32 obj, i32, int, int hi, u8) { ++g_s.req17Calls; g_s.req17ObjId = obj; g_s.req17Hiword = hi; return g_s.req17Ret; }
i32  HSeq(i32) { return g_s.seqRet; }
void HMark(i32 a) { ++g_s.markBoughtCalls; g_s.markBoughtArg = a; }
void HSlot28() { ++g_s.slot28Calls; }
void HQuickjump(i32 r, i32, int) { ++g_s.quickjumpCalls; g_s.quickjumpRecipient.push_back(r); }
i32  HCityPid(u16) { return g_s.cityPid; }
u8   HCityCat(u16) { return g_s.cityCat; }
int  HRnd(int) { return g_s.rnd; }
int  HFast() { return g_s.fastCnt; }
int  HMed() { return g_s.medCnt; }

void InstallS5() {
    g_s = S5{};
    static CharActionStep5Hooks h; h = CharActionStep5Hooks{};
    h.findPersonById = HPerson;
    h.personQueryBegin = HQuery;
    h.objectQueryFind = HObject;
    h.findFirstByFilter = HFindFirst;
    h.findNextMatching = HFindNext;
    h.changePlayerAction = HPlayerAction;
    h.queueRequest17 = HReq17;
    h.packetSeqBase = HSeq;
    h.markObjectBought = HMark;
    h.queueSlotReset28 = HSlot28;
    h.sendQuickjumpMessage = HQuickjump;
    h.cityPersonId = HCityPid;
    h.cityCategory = HCityCat;
    h.randomModulo = HRnd;
    h.fastFrameCounter = HFast;
    h.medFrameCounter = HMed;
    SetCharActionStep5Hooks(&h);
}

void Setup() { SetNpcClock(Clock()); InstallLeaf(); InstallS5(); }

} // namespace

// ===========================================================================
// State-reset / appointment-copy leaves (pure GameTime golden vectors).
// ===========================================================================
TEST(CharActionV5, StateReset24Alt) {
    Setup();
    BigRec r;
    i32 hod = StateReset24Alt(r.get());
    // clock (10,8,30,15) + 24h -> day 11, hour 8; returns hour-of-day 8.
    CHECK_EQ(hod, 8);
    CHECK(TimeEq(He_ApptTime(r.get()), 11, 8, 30, 15));
}

TEST(CharActionV5, StateReset0Alt) {
    Setup();
    BigRec r;
    i32 hod = StateReset0Alt(r.get());
    // clock + 5m -> (10,8,35,15); returns 8.
    CHECK_EQ(hod, 8);
    CHECK(TimeEq(He_ApptTime(r.get()), 10, 8, 35, 15));
}

TEST(CharActionV5, CopyGoalToTargetDup) {
    Setup();
    BigRec r;
    He_SavedTime(r.get()) = GameTime{5, 16, 45, 50};   // +68
    i32 hod = CopyGoalToTargetDup(r.get());
    // saved + 15m -> (5,17,0,50); returns 17.
    CHECK_EQ(hod, 17);
    CHECK(TimeEq(He_ApptTime(r.get()), 5, 17, 0, 50));
}

TEST(CharActionV5, CopyGoalToTargetState2Dup) {
    Setup();
    BigRec r;
    He_SavedTime(r.get()) = GameTime{5, 16, 45, 50};
    i32 hod = CopyGoalToTargetState2Dup(r.get());
    // saved + 2 days -> (7,16,45,50)? day carries via +2 only; returns hour 16+...
    // golden: (day 5+0 ... result hour 18 per python: minute 45 -> 45, +2 days adds
    // to result via addDays). Matches python (5,18,45,50,18) where day stays 5 and
    // the +2 days flow through the result/hour wrap.
    CHECK_EQ(hod, 18);
    CHECK(TimeEq(He_ApptTime(r.get()), 5, 18, 45, 50));
}

// ===========================================================================
// RestorePosFinishAlt — restores pose, 50/50 frees.
// ===========================================================================
TEST(CharActionV5, RestorePosFinishAlt_FreesOnNonzeroRoll) {
    Setup();
    g_s.rnd = 1;   // RandomModulo(2) -> 1 (nonzero) => free
    BigRec r;
    He_SavedTime(r.get()) = GameTime{3, 12, 5, 9};
    i32 res = RestorePosFinishAlt(r.get());
    CHECK_EQ(res, 7);                 // free's return
    CHECK_EQ(g_leaf.freeCalls, 1);
    CHECK(TimeEq(He_ApptTime(r.get()), 3, 12, 5, 9));  // pose restored
}
TEST(CharActionV5, RestorePosFinishAlt_StaysOnZeroRoll) {
    Setup();
    g_s.rnd = 0;   // -> no free
    BigRec r;
    i32 res = RestorePosFinishAlt(r.get());
    CHECK_EQ(res, 0);
    CHECK_EQ(g_leaf.freeCalls, 0);
}

// ===========================================================================
// InitOfficeGuardState.
// ===========================================================================
TEST(CharActionV5, InitOfficeGuard_NoRollWhenEarly) {
    Setup();
    BigRec r;   // clock hour 8 < 17 -> no +24h
    HeRecord* out = InitOfficeGuardState(r.get());
    CHECK(out == r.get());
    CHECK(TimeEq(He_ApptTime(r.get()), 10, 17, 0, 15));  // hour pinned 17, min 0
    CHECK_EQ(r.at<i32>(172), 0);
    CHECK_EQ(r.at<i32>(176), 27);
    CHECK_EQ(r.at<i32>(180), -1);
    CHECK_EQ(r.at<i32>(184), -1);
    CHECK_EQ(g_leaf.cmd29Calls, 1);
    CHECK_EQ(He_ReqHandle(r.get()), 4242);
}
TEST(CharActionV5, InitOfficeGuard_RollsWhenLate) {
    Setup();
    SetNpcClock(GameTime{20, 19, 5, 0});   // hour 19 >= 17 -> +24h then pin 17
    BigRec r;
    InitOfficeGuardState(r.get());
    CHECK(TimeEq(He_ApptTime(r.get()), 21, 17, 0, 0));   // day rolled, hour pinned
}
TEST(CharActionV5, InitOfficeGuard_SkipsWhenSpawned) {
    Setup();
    BigRec r;
    He_Flags(r.get()) = kHeAlreadySpawned;
    HeRecord* out = InitOfficeGuardState(r.get());
    CHECK(out == r.get());
    CHECK_EQ(g_leaf.cmd29Calls, 0);   // no arm
    CHECK_EQ(He_ApptTime(r.get()).day, 0);  // untouched
}

// ===========================================================================
// ApplyTransportSpeed.
// ===========================================================================
TEST(CharActionV5, TransportSpeed_NoScaleVerbatim) {
    Setup();
    BigRec actor, link, veh;
    Cas5_BaseSpeed(actor.get()) = 4.0f;
    *reinterpret_cast<HeRecord**>(reinterpret_cast<unsigned char*>(link.get()) + 59) = veh.get();
    g_s.fastCnt = 0; g_s.medCnt = 0;   // neither tier
    HeRecord* out = ApplyTransportSpeed(actor.get(), link.get());
    CHECK(out == veh.get());
    CHECK(Cas5_VehSpeed(veh.get()) == 4.0f);   // copied verbatim
}
TEST(CharActionV5, TransportSpeed_FastTier) {
    Setup();
    BigRec actor, link, veh;
    Cas5_BaseSpeed(actor.get()) = 3.0f;
    *reinterpret_cast<HeRecord**>(reinterpret_cast<unsigned char*>(link.get()) + 59) = veh.get();
    g_s.fastCnt = 200;   // > 100 -> fast factor 2.0
    ApplyTransportSpeed(actor.get(), link.get());
    CHECK(Cas5_VehSpeed(veh.get()) == 6.0f);
}
TEST(CharActionV5, TransportSpeed_Type2Doubles) {
    Setup();
    BigRec actor, link, veh;
    Cas5_BaseSpeed(actor.get()) = 4.0f;
    Cas5_ActorType(actor.get()) = 2;   // type 2 -> * slow factor 0.5
    *reinterpret_cast<HeRecord**>(reinterpret_cast<unsigned char*>(link.get()) + 59) = veh.get();
    g_s.fastCnt = 0; g_s.medCnt = 0;
    ApplyTransportSpeed(actor.get(), link.get());
    CHECK(Cas5_VehSpeed(veh.get()) == 2.0f);   // 4.0 verbatim then * 0.5
}

// ===========================================================================
// Reverse paired-entity finder.
// ===========================================================================
TEST(CharActionV5, FindPairedReverse_NoCandidates) {
    Setup();
    BigRec h;
    CHECK_EQ(FindPairedEntityReverse(h.get()), 1);
}
TEST(CharActionV5, FindPairedReverse_Match) {
    Setup();
    BigRec h; He_Id(h.get()) = 7;
    BigRec cand; He_Id(cand.get()) = 30; Cas5_IdB176(cand.get()) = 30; Cas5_IdA172(cand.get()) = 7;
    g_s.findSeq = { cand.get() };
    CHECK_EQ(FindPairedEntityReverse(h.get()), 0);   // matched -> 0
}
TEST(CharActionV5, FindPairedReverse_NoMatchExhausts) {
    Setup();
    BigRec h; He_Id(h.get()) = 7;
    BigRec cand; He_Id(cand.get()) = 30; Cas5_IdB176(cand.get()) = 30; Cas5_IdA172(cand.get()) = 99;
    g_s.findSeq = { cand.get() };
    CHECK_EQ(FindPairedEntityReverse(h.get()), 1);
}

// ===========================================================================
// RunFollowTarget.
// ===========================================================================
TEST(CharActionV5, RunFollow_FreesWhenLeaderAbsent) {
    Setup();
    BigRec h; Cas5_IdB176(h.get()) = 5;   // leader id
    // no person installed -> findPersonById returns null
    i32 res = RunFollowTarget(h.get());
    CHECK_EQ(res, 7);
    CHECK_EQ(g_leaf.freeCalls, 1);
}
TEST(CharActionV5, RunFollow_FreesWhenLeaderDead) {
    Setup();
    BigRec h; Cas5_IdB176(h.get()) = 1;
    BigRec leader, leaderAct;
    *reinterpret_cast<u8*>(leaderAct.get()) = 22;   // dead anim
    *reinterpret_cast<HeRecord**>(reinterpret_cast<unsigned char*>(leader.get()) + 380) = leaderAct.get();
    g_s.personById.assign(2, nullptr); g_s.personById[1] = leader.get();
    i32 res = RunFollowTarget(h.get());
    CHECK_EQ(res, 7);
    CHECK_EQ(g_leaf.freeCalls, 1);
}
TEST(CharActionV5, RunFollow_FreesWhenSelfQueryEmpty) {
    Setup();
    BigRec h; Cas5_IdB176(h.get()) = 1;
    BigRec leader;
    g_s.personById.assign(2, nullptr); g_s.personById[1] = leader.get();
    g_s.queryRet = nullptr;   // self query empty
    i32 res = RunFollowTarget(h.get());
    CHECK_EQ(res, 7);
    CHECK_EQ(g_leaf.freeCalls, 1);
}
TEST(CharActionV5, RunFollow_ArmsWalkAndGiveup) {
    Setup();
    BigRec h; Cas5_IdB176(h.get()) = 1; Cas5_IdC180(h.get()) = -1;
    BigRec leader, self;
    g_s.personById.assign(2, nullptr); g_s.personById[1] = leader.get();
    g_s.queryRet = self.get();
    i32 hod = RunFollowTarget(h.get());
    // clock +30m -> appt (10,9,0,15); give-up block (+184) = appt + 6h -> (10,15,..)
    CHECK_EQ(g_leaf.freeCalls, 0);
    CHECK_EQ(g_s.playerActionCalls, 1);
    CHECK(TimeEq(He_ApptTime(h.get()), 10, 9, 0, 15));   // walk schedule
    GameTime& giveup = *reinterpret_cast<GameTime*>(reinterpret_cast<unsigned char*>(h.get()) + 184);
    CHECK(TimeEq(giveup, 10, 15, 0, 15));
    CHECK_EQ(hod, 15);
    // saved-pose snapshot (+68) == the +82 stamp before the +30m advance.
    CHECK(TimeEq(He_SavedTime(h.get()), 10, 8, 30, 15));
}

// ===========================================================================
// RunBuyObject (3-phase machine).
// ===========================================================================
TEST(CharActionV5, RunBuy_State0Arms) {
    Setup();
    BigRec h; He_State(h.get()) = 0;
    u32 res = RunBuyObject(h.get());
    CHECK_EQ(res, 1u);
    CHECK_EQ(He_State(h.get()), 1);
}
TEST(CharActionV5, RunBuy_State1NotDueReturns) {
    Setup();
    BigRec h; He_State(h.get()) = 1;
    // appt in the future relative to the clock -> cmp >= 0 -> return cmp.
    He_ApptTime(h.get()) = GameTime{99, 0, 0, 0};
    u32 res = RunBuyObject(h.get());
    CHECK_EQ(res, 1u);            // GameTimeCompare(future, clock) == +1
    CHECK_EQ(He_State(h.get()), 1);  // unchanged
}
TEST(CharActionV5, RunBuy_State1DueNoSelfFrees) {
    Setup();
    BigRec h; He_State(h.get()) = 1;
    He_ApptTime(h.get()) = GameTime{1, 0, 0, 0};   // past -> due
    g_s.queryRet = nullptr;
    u32 res = RunBuyObject(h.get());
    CHECK_EQ(res, 7u);
    CHECK_EQ(g_leaf.freeCalls, 1);
}
TEST(CharActionV5, RunBuy_State1DueQueuesRequest) {
    Setup();
    BigRec h; He_State(h.get()) = 1;
    He_ApptTime(h.get()) = GameTime{1, 0, 0, 0};
    Cas5_IdB176(h.get()) = 808;   // target object id
    BigRec self;
    g_s.queryRet = self.get();
    g_s.req17Ret = 555;
    u32 res = RunBuyObject(h.get());
    CHECK_EQ(res, 555u);
    CHECK_EQ(g_s.req17Calls, 1);
    CHECK_EQ(g_s.req17ObjId, 808);
    CHECK_EQ(Cas5_IdC180(h.get()), 555);   // handle stored
    CHECK_EQ(He_State(h.get()), 2);        // advanced
}
TEST(CharActionV5, RunBuy_State2PendingReturns0) {
    Setup();
    BigRec h; He_State(h.get()) = 2; Cas5_IdC180(h.get()) = 555;
    g_leaf.statusRet = 0;   // packet not applied
    u32 res = RunBuyObject(h.get());
    CHECK_EQ(res, 0u);
    CHECK_EQ(g_leaf.freeCalls, 0);
}
TEST(CharActionV5, RunBuy_State2AppliedFinalizes) {
    Setup();
    BigRec h; He_State(h.get()) = 2; Cas5_IdC180(h.get()) = 555;
    g_leaf.statusRet = 1;
    BigRec self;
    g_s.queryRet = self.get();
    g_s.seqRet = 9000;
    g_s.cityCat = 6;        // market path -> render + quickjump + mark bought
    g_s.cityPid = 314;
    g_s.objectRet = nullptr;   // missing -> failure template, still quickjumps
    u32 res = RunBuyObject(h.get());
    CHECK_EQ(res, 7u);
    CHECK_EQ(g_leaf.freeCalls, 1);
    CHECK_EQ(g_s.quickjumpCalls, 1);
    CHECK_EQ(g_s.quickjumpRecipient[0], 314);
    CHECK_EQ(g_s.markBoughtCalls, 1);
    CHECK_EQ(g_s.markBoughtArg, 9000);
}
TEST(CharActionV5, RunBuy_State2NonMarketSkipsRender) {
    Setup();
    BigRec h; He_State(h.get()) = 2; Cas5_IdC180(h.get()) = 555;
    g_leaf.statusRet = 1;
    BigRec self;
    g_s.queryRet = self.get();
    g_s.cityCat = 0;   // not 6 -> no render
    u32 res = RunBuyObject(h.get());
    CHECK_EQ(res, 7u);
    CHECK_EQ(g_s.quickjumpCalls, 0);
}

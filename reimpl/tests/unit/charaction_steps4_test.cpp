// Unit tests for charaction_steps4 — batch 4 of the CharAction step leaves (the
// social / examination / duel cluster). GameTimeAdvance / GameTimeCompare golden
// vectors were computed with python3 mirroring src/sim/gametime.cpp. The
// cross-cluster leaves (resolves, render/UI bridge, command emits, dialog-result
// globals) are routed through a recording mock installed via
// SetCharActionStep4Hooks / SetNpcLeafHooks. Suite prefix: CharActionW.
#include "test.h"

#include "sim/charaction_steps4.h"
#include "sim/he.h"
#include "sim/npcaction.h"   // NpcClock / SetNpcClock, SetNpcLeafHooks

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// A zeroed record buffer big enough for the +358/+360 office-byte accesses.
struct BigRec {
    unsigned char bytes[512];
    BigRec() { std::memset(bytes, 0, sizeof(bytes)); }
    HeRecord* get() { return reinterpret_cast<HeRecord*>(bytes); }
    template <class T> T& at(int off) { return *reinterpret_cast<T*>(bytes + off); }
};

GameTime TestClock() { GameTime t{}; t.day = 10; t.hour = 8; t.minute = 30; t.second = 15; return t; }
void SetClock() { SetNpcClock(TestClock()); }

bool TimeEq(const GameTime& t, i32 day, u16 hour, i32 minute, i32 second) {
    return t.day == day && t.hour == hour && t.minute == minute && t.second == second;
}

// --- recording NpcLeafHooks ------------------------------------------------
struct LeafRec {
    int freeCalls = 0;
    int cmd29Calls = 0; std::vector<int> cmd29Args;
    i32 nextHandle = 4242;
    i32 freeRet = 7;
    i32 statusRet = 0;   // packetStatus return
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

// --- recording CharActionStep4Hooks ----------------------------------------
struct S4 {
    HeRecord* personRet = nullptr;
    std::vector<HeRecord*> personById;   // findPersonById by id (sparse)
    HeRecord* resolveRet = nullptr;
    HeRecord* queryRet = nullptr;
    std::vector<HeRecord*> iter;   size_t iterCur = 0;
    HeRecord* findFirstRet = nullptr;
    int buildingCat = 0;
    int wealth = 0;
    int rnd = 0;
    u8 willingness = 0;
    u8 cityCat = 0;
    i32 cityPid = 0;
    int moodCalls = 0; std::vector<int> moodDeltas;
    int args25Calls = 0; std::vector<int> args25Ids;
    int coord27Calls = 0;
    int cmd15Calls = 0; std::vector<int> cmd15Vals;
    int req16Calls = 0; std::vector<int> req16Vals;
    int sendMsgCalls = 0; std::vector<int> sendMsgText;
    int quickjumpCalls = 0; std::vector<int> quickjumpText;
    int buildingAllCalls = 0; std::vector<int> buildingAllOps;
    int highlightCalls = 0;
    int req39Calls = 0;
    int slot28Calls = 0;
    int panelCreate = 0, panelDestroy = 0, dialogLines = 0, voice = 0, card = 0;
    int officeRet = 0; int officeRank = 0;
    int prodRating = 0;
    i32 dlgWindow = 0;
    i32 dlgResult = kDialogNone;
};
S4 g_s;

HeRecord* HPerson(i32 id) {
    if (id >= 0 && (size_t)id < g_s.personById.size() && g_s.personById[id]) return g_s.personById[id];
    return g_s.personRet;
}
HeRecord* HResolve(i32) { return g_s.resolveRet; }
HeRecord* HQuery(int, int, int) { g_s.iterCur = 0; return g_s.queryRet; }
HeRecord* HIter() { return g_s.iterCur < g_s.iter.size() ? g_s.iter[g_s.iterCur++] : nullptr; }
HeRecord* HFindFirst(int, int) { return g_s.findFirstRet; }
HeRecord* HFindNext() { return nullptr; }
int  HBuildCat(u8) { return g_s.buildingCat; }
int  HWealth(u16, HeRecord*) { return g_s.wealth; }
int  HRnd(int) { return g_s.rnd; }
u8   HWilling(u16) { return g_s.willingness; }
u8   HCityCat(u16) { return g_s.cityCat; }
i32  HCityPid(u16) { return g_s.cityPid; }
void HMood(HeRecord*, int d) { ++g_s.moodCalls; g_s.moodDeltas.push_back(d); }
void HArgs25(i32 id, int, int, int, int) { ++g_s.args25Calls; g_s.args25Ids.push_back(id); }
void HCoord27(i32, i32, int) { ++g_s.coord27Calls; }
void HCmd15(i32, i32, int v, u8) { ++g_s.cmd15Calls; g_s.cmd15Vals.push_back(v); }
void HReq16(i32, i32, int v, u8) { ++g_s.req16Calls; g_s.req16Vals.push_back(v); }
void HSendMsg(i32, int t) { ++g_s.sendMsgCalls; g_s.sendMsgText.push_back(t); }
void HQuickjump(i32, int t) { ++g_s.quickjumpCalls; g_s.quickjumpText.push_back(t); }
void HBuildAll(u16, int op) { ++g_s.buildingAllCalls; g_s.buildingAllOps.push_back(op); }
void HHighlight(HeRecord*, int) { ++g_s.highlightCalls; }
void HReq39() { ++g_s.req39Calls; }
void HSlot28() { ++g_s.slot28Calls; }
void HPanelCreate(HeRecord*) { ++g_s.panelCreate; }
void HPanelDestroy(HeRecord*) { ++g_s.panelDestroy; }
void HDialogLine(HeRecord*, int, int) { ++g_s.dialogLines; }
void HVoice() { ++g_s.voice; }
void HCard(HeRecord*) { ++g_s.card; }
int  HOffice(HeRecord*, int* r) { if (r) *r = g_s.officeRank; return g_s.officeRet; }
int  HProd(HeRecord*, int) { return g_s.prodRating; }
i32  HDlgWin() { return g_s.dlgWindow; }
i32  HDlgRes() { return g_s.dlgResult; }

void InstallS4() {
    g_s = S4{};
    static CharActionStep4Hooks h; h = CharActionStep4Hooks{};
    h.findPersonById = HPerson;
    h.resolveEntityById = HResolve;
    h.personQueryBegin = HQuery;
    h.personIterNext = HIter;
    h.findFirstByFilter = HFindFirst;
    h.findNextMatching = HFindNext;
    h.buildingCategory = HBuildCat;
    h.personWealth = HWealth;
    h.randomModulo = HRnd;
    h.cityWillingness = HWilling;
    h.cityCategory = HCityCat;
    h.cityPersonId = HCityPid;
    h.adjustMood = HMood;
    h.queueArgs25 = HArgs25;
    h.queueCoord27 = HCoord27;
    h.enqueueCmd15 = HCmd15;
    h.queueRequest16 = HReq16;
    h.sendEntityMessage = HSendMsg;
    h.sendQuickjumpMessage = HQuickjump;
    h.buildingQueueAll = HBuildAll;
    h.highlightGuildMembers = HHighlight;
    h.queueRequest39 = HReq39;
    h.queueSlotReset28 = HSlot28;
    h.eventPanelCreate = HPanelCreate;
    h.eventPanelDestroy = HPanelDestroy;
    h.renderDialogLine = HDialogLine;
    h.playVoice = HVoice;
    h.buildPersonCard = HCard;
    h.officeHolder = HOffice;
    h.productionRating = HProd;
    h.dialogWindow = HDlgWin;
    h.dialogResult = HDlgRes;
    SetCharActionStep4Hooks(&h);
}
void InstallAll() { InstallLeaf(); InstallS4(); }

} // namespace

// ===========================================================================
// JourneymanRecruitStep.
// ===========================================================================
TEST(CharActionW, RecruitNoEntityFrees) {
    InstallAll(); SetClock();
    BigRec r; He_State(r.get()) = 0; g_s.resolveRet = nullptr;
    JourneymanRecruitStep(r.get());
    CHECK_EQ(g_leaf.freeCalls, 1);                 // +176 entity absent -> free
    CHECK(TimeEq(He_ApptTime(r.get()), 10, 8, 34, 15));   // clock +4 min
}

TEST(CharActionW, RecruitPacketPending) {
    InstallAll(); SetClock();
    BigRec entity;
    BigRec r; He_State(r.get()) = 0; g_s.resolveRet = entity.get();
    g_leaf.statusRet = 0;                          // packet not yet applied
    i32 ret = JourneymanRecruitStep(r.get());
    CHECK_EQ(g_s.quickjumpCalls, 0);
    CHECK_EQ(ret, 0);
    CHECK_EQ(g_leaf.freeCalls, 0);
}

TEST(CharActionW, RecruitPacketApplied) {
    InstallAll(); SetClock();
    BigRec entity;
    BigRec r; He_State(r.get()) = 0; g_s.resolveRet = entity.get();
    g_leaf.statusRet = 1;                          // packet applied -> deliver + free
    JourneymanRecruitStep(r.get());
    CHECK_EQ(g_s.quickjumpCalls, 1);
    CHECK_EQ(g_leaf.freeCalls, 1);
}

// ===========================================================================
// BuyObjectStep.
// ===========================================================================
TEST(CharActionW, BuyObjectTerminal) {
    InstallAll();
    BigRec r; He_State(r.get()) = -2;
    BuyObjectStep(r.get());
    CHECK_EQ(g_leaf.freeCalls, 1);
}

TEST(CharActionW, BuyObjectPositiveState) {
    InstallAll();
    BigRec r; He_State(r.get()) = 5;
    CHECK_EQ(BuyObjectStep(r.get()), 5);
    CHECK_EQ(g_leaf.freeCalls, 0);
}

TEST(CharActionW, BuyObjectPicksSeller) {
    InstallAll();
    BigRec r; He_State(r.get()) = 0;
    BigRec s1, s2;
    g_s.queryRet = s1.get();
    g_s.iter = {s2.get()};      // one more then end
    g_s.buildingCat = 6;        // both qualify as markets
    g_s.rnd = 0;                // pick index 0
    g_s.cityCat = 6;            // city category 6 -> send buyer quickjump
    BuyObjectStep(r.get());
    CHECK_EQ(g_s.moodCalls, 1);
    CHECK_EQ(g_s.moodDeltas[0], -50);
    CHECK_EQ(g_s.quickjumpCalls, 1);
    CHECK_EQ(g_s.quickjumpText[0], 3355);
    CHECK_EQ(g_leaf.freeCalls, 1);
}

TEST(CharActionW, BuyObjectNoMarket) {
    InstallAll();
    BigRec r; He_State(r.get()) = 0;
    BigRec s1; g_s.queryRet = s1.get(); g_s.iter = {};
    g_s.buildingCat = 3;        // not a market
    BuyObjectStep(r.get());
    CHECK_EQ(g_s.moodCalls, 0);
    CHECK_EQ(g_leaf.freeCalls, 1);
}

// ===========================================================================
// WaitThenMoveStep.
// ===========================================================================
TEST(CharActionW, WaitCountsDown) {
    InstallAll();
    BigRec r; He_State(r.get()) = 0; r.at<u8>(172) = 3;
    CHECK_EQ(WaitThenMoveStep(r.get()), 0);
    CHECK_EQ(r.at<u8>(172), 2);     // decremented
    CHECK_EQ(g_leaf.freeCalls, 0);
}

TEST(CharActionW, WaitAccept) {
    InstallAll();
    BigRec r; He_State(r.get()) = 0; r.at<u8>(172) = 0;
    Cas4_Fee176(r.get()) = 50;
    // threshold = 100*0.5 + 64 = 114; roll 200 >= 114 -> accept (3353).
    // accept value = trunc(50 * 0.5) = 25 (flt_61EAF4 + ConvertX truncate).
    g_s.willingness = 100; g_s.rnd = 200;
    WaitThenMoveStep(r.get());
    CHECK_EQ(g_s.sendMsgText[0], 3353);
    CHECK_EQ(g_s.cmd15Vals[0], 25);
    CHECK_EQ(g_leaf.freeCalls, 1);
}

TEST(CharActionW, WaitRefuse) {
    InstallAll();
    BigRec r; He_State(r.get()) = 0; r.at<u8>(172) = 0;
    Cas4_Fee176(r.get()) = 10;      // refuse value = 2*10 = 20
    // threshold = 200*0.5 + 64 = 164; roll 50 < 164 -> refuse (3352).
    g_s.willingness = 200; g_s.rnd = 50;
    WaitThenMoveStep(r.get());
    CHECK_EQ(g_s.sendMsgText[0], 3352);
    CHECK_EQ(g_s.cmd15Vals[0], 20);
}

// ===========================================================================
// GossipBroadcast.
// ===========================================================================
TEST(CharActionW, GossipTerminal) {
    InstallAll();
    BigRec r; He_State(r.get()) = -2;
    GossipBroadcast(r.get());
    CHECK_EQ(g_leaf.freeCalls, 1);
    CHECK_EQ(g_s.sendMsgCalls, 0);
}

TEST(CharActionW, GossipSendsRumor) {
    InstallAll();
    BigRec r; He_State(r.get()) = 0;
    // Make a single eligible person resolve for every table index; with a target.
    BigRec person; person.at<u8>(2) = 6; person.at<i32>(4) = 0x77;
    g_s.personRet = person.get();
    g_s.resolveRet = person.get();        // target resolves -> bribe path
    // gilde.exe 0x4d0ce3: bribe = trunc((double)wealth * flt_61EAD4) * (rnd+2),
    // where flt_61EAD4 = 0x3C23D70A == 0.00999999977f (a *float* 0.01, NOT exact).
    // ConvertX @0x5c6b08 truncates toward zero (frndint, RC=11), so:
    //   trunc(1000.0 * 0.00999999977) = trunc(9.99999977...) = 9; 9*(0+2) = 18.
    g_s.wealth = 1000; g_s.rnd = 0;
    GossipBroadcast(r.get());
    // 768 table entries, each eligible -> rumor (3365) + acceptance (3366) sent.
    CHECK(g_s.sendMsgCalls >= 2);
    CHECK_EQ(g_s.req16Calls, 768);
    CHECK_EQ(g_s.req16Vals[0], 18);
    CHECK_EQ(g_leaf.freeCalls, 1);
}

TEST(CharActionW, GossipSkipsEmpty) {
    InstallAll();
    BigRec r; He_State(r.get()) = 0;
    g_s.personRet = nullptr;              // every slot empty
    GossipBroadcast(r.get());
    CHECK_EQ(g_s.sendMsgCalls, 0);
    CHECK_EQ(g_leaf.freeCalls, 1);
}

// ===========================================================================
// DuelArmCombatant.
// ===========================================================================
TEST(CharActionW, DuelArmDisarmsBoth) {
    InstallAll(); SetClock();
    BigRec h, opp, self;
    opp.at<u8>(2) = 6;                    // opponent class 6 -> message + disarm
    opp.at<i32>(4) = 0x11; self.at<i32>(4) = 0x22;
    i32 ret = DuelArmCombatant(h.get(), opp.get(), self.get());
    CHECK_EQ(g_s.sendMsgText[0], 6529);
    CHECK_EQ(g_s.args25Calls, 2);
    CHECK_EQ(g_s.coord27Calls, 1);
    CHECK_EQ(g_s.highlightCalls, 1);
    CHECK_EQ(g_leaf.cmd29Calls, 1);
    CHECK_EQ(g_leaf.cmd29Args[0], -1);
    CHECK_EQ(ret, g_leaf.nextHandle);
    CHECK(TimeEq(He_ApptTime(h.get()), 10, 8, 30, 15));   // clock, no advance
}

TEST(CharActionW, DuelArmOfficeRankFromHolder) {
    InstallAll(); SetClock();
    BigRec h, opp, self;
    opp.at<u8>(2) = 0;                    // not 6/7 -> no message/disarm
    self.at<u8>(358) = 1;                 // has an office
    g_s.officeRet = 1; g_s.officeRank = 4;
    DuelArmCombatant(h.get(), opp.get(), self.get());
    CHECK_EQ(g_s.sendMsgCalls, 0);
    CHECK_EQ(g_s.args25Calls, 0);
    CHECK_EQ(g_s.highlightCalls, 1);      // highlight still runs (rank 4)
}

// ===========================================================================
// DuelResolveStep.
// ===========================================================================
TEST(CharActionW, DuelResolvePacketPending) {
    InstallAll();
    BigRec r; He_ReqHandle(r.get()) = 99; g_leaf.statusRet = 0;
    CHECK_EQ(DuelResolveStep(r.get()), 0);   // packet not applied -> bail
    CHECK_EQ(g_leaf.freeCalls, 0);
}

TEST(CharActionW, DuelResolveCase0Frees) {
    InstallAll(); SetClock();
    BigRec r; He_ReqHandle(r.get()) = -1;    // no pending packet
    He_State(r.get()) = -2;                  // +2 = 0 -> free
    He_Flags(r.get()) = 2;                   // disarm + rearm runs
    BigRec a, b; g_s.personById.assign(8, nullptr);
    g_s.personRet = a.get();
    DuelResolveStep(r.get());
    CHECK_EQ(g_s.args25Calls, 2);            // both combatants disarmed
    CHECK_EQ(g_leaf.cmd29Calls, 1);          // rearm
    CHECK_EQ(g_leaf.freeCalls, 1);
}

TEST(CharActionW, DuelResolveCase3NoFree) {
    InstallAll(); SetClock();
    BigRec r; He_ReqHandle(r.get()) = -1;
    He_State(r.get()) = 1;                   // +2 = 3 -> rearm, no free
    He_Flags(r.get()) = 2;
    g_s.personRet = nullptr;                 // neither combatant resolves
    DuelResolveStep(r.get());
    CHECK_EQ(g_leaf.freeCalls, 0);
    CHECK_EQ(g_leaf.cmd29Calls, 1);
}

TEST(CharActionW, DuelResolveCase4Move39) {
    InstallAll(); SetClock();
    BigRec r; He_ReqHandle(r.get()) = -1;
    He_State(r.get()) = 2;                   // +2 = 4 -> move39 + rearm
    He_Flags(r.get()) = 2;
    BigRec a, b; g_s.personRet = a.get();    // both resolve (same record)
    DuelResolveStep(r.get());
    CHECK_EQ(g_s.req39Calls, 1);
    CHECK_EQ(g_leaf.cmd29Calls, 1);
}

TEST(CharActionW, DuelResolveFlagClearSkips) {
    InstallAll();
    BigRec r; He_ReqHandle(r.get()) = -1;
    He_State(r.get()) = 1; He_Flags(r.get()) = 0;   // case 3, flag clear
    DuelResolveStep(r.get());
    CHECK_EQ(g_s.args25Calls, 0);
    CHECK_EQ(g_leaf.cmd29Calls, 0);
}

// ===========================================================================
// DuelDispatch.
// ===========================================================================
TEST(CharActionW, DuelDispatchNoPartnerFrees) {
    InstallAll(); SetClock();
    BigRec r; He_State(r.get()) = 0; g_s.findFirstRet = nullptr;
    DuelDispatch(r.get());
    CHECK_EQ(g_leaf.freeCalls, 1);
}

TEST(CharActionW, DuelDispatchOpensDialog) {
    InstallAll(); SetClock();
    BigRec r, partner, combA, combB;
    partner.at<i32>(176) = 1; partner.at<i32>(172) = 2;
    g_s.findFirstRet = partner.get();
    g_s.personById.assign(8, nullptr);
    g_s.personById[1] = combA.get(); g_s.personById[2] = combB.get();
    He_State(r.get()) = 0;
    Cas4_Slot116(r.get()) = 0;       // create will be needed; mark slot after create
    // Make the panel create set the slot so the open path proceeds.
    // Our mock doesn't set it, so simulate by pre-setting the slot.
    Cas4_Slot116(r.get()) = 0x500;
    i32 ret = DuelDispatch(r.get());
    CHECK_EQ(g_s.panelCreate, 1);
    CHECK_EQ(He_State(r.get()), 1);
    // GameTimeAdvance(&appt, +2 days, 0, 0): result = 2 + hour(8) = 10 (< 24, no
    // day carry), so appt becomes (day10, hour10, min30, sec15) and ret == 10.
    CHECK(TimeEq(He_ApptTime(r.get()), 10, 10, 30, 15));
    CHECK_EQ(ret, 10);
    CHECK(TimeEq(He_SavedTime(r.get()), 10, 8, 30, 15));   // clock -> +68
}

TEST(CharActionW, DuelDispatchAcceptIntro) {
    InstallAll(); SetClock();
    BigRec r, partner, combA, combB;
    partner.at<i32>(176) = 1; partner.at<i32>(172) = 2;
    g_s.findFirstRet = partner.get();
    g_s.personById.assign(8, nullptr);
    g_s.personById[1] = combA.get(); g_s.personById[2] = combB.get();
    combA.at<u8>(2) = 6; combB.at<u8>(2) = 6;   // both eligible for intro message
    He_State(r.get()) = 1;
    Cas4_Slot116(r.get()) = 0x700;
    // appt is in the future relative to clock so the deadline branch is skipped.
    He_ApptTime(r.get()) = TestClock(); He_ApptTime(r.get()).day = 99;
    g_s.dlgWindow = 0x700; g_s.dlgResult = kDialogAccept;
    DuelDispatch(r.get());
    CHECK_EQ(g_s.panelDestroy, 1);
    CHECK_EQ(g_leaf.freeCalls, 1);
}

TEST(CharActionW, DuelDispatchDeclineNoArm) {
    InstallAll(); SetClock();
    BigRec r, partner, combA, combB;
    partner.at<i32>(176) = 1; partner.at<i32>(172) = 2;
    g_s.findFirstRet = partner.get();
    g_s.personById.assign(8, nullptr);
    g_s.personById[1] = combA.get(); g_s.personById[2] = combB.get();
    He_State(r.get()) = 1;
    Cas4_Slot116(r.get()) = 0x700;
    He_ApptTime(r.get()) = TestClock(); He_ApptTime(r.get()).day = 99;
    g_s.dlgWindow = 0x700; g_s.dlgResult = kDialogDecline;
    DuelDispatch(r.get());
    CHECK_EQ(g_s.coord27Calls, 0);   // DuelArmCombatant not invoked
    CHECK_EQ(g_leaf.freeCalls, 1);
    CHECK_EQ(g_s.panelDestroy, 1);
}

// ===========================================================================
// MasterExamPromptStep.
// ===========================================================================
TEST(CharActionW, ExamPromptTerminalTeardown) {
    InstallAll();
    BigRec r; He_State(r.get()) = -1; Cas4_Slot116(r.get()) = 0x10;
    MasterExamPromptStep(r.get());
    CHECK_EQ(g_s.panelDestroy, 1);
    CHECK_EQ(g_leaf.freeCalls, 1);
}

TEST(CharActionW, ExamPromptNotDue) {
    InstallAll();
    BigRec r; He_State(r.get()) = 0;
    // saved (+68) in the future -> clock < saved -> compare == -1 -> not due.
    He_SavedTime(r.get()) = TestClock(); He_SavedTime(r.get()).day = 99;
    SetClock();
    CHECK_EQ(MasterExamPromptStep(r.get()), 0);
    CHECK_EQ(g_s.panelCreate, 0);
}

TEST(CharActionW, ExamPromptOpensDialog) {
    InstallAll(); SetClock();
    BigRec r; He_State(r.get()) = 0;
    He_SavedTime(r.get()) = TestClock(); He_SavedTime(r.get()).day = 1;   // past -> due
    MasterExamPromptStep(r.get());
    CHECK_EQ(g_s.panelCreate, 1);
    CHECK_EQ(He_State(r.get()), 1);
}

TEST(CharActionW, ExamPromptAcceptCharges) {
    InstallAll(); SetClock();
    BigRec r; He_State(r.get()) = 1;
    He_SavedTime(r.get()) = TestClock(); He_SavedTime(r.get()).day = 1;
    Cas4_Slot116(r.get()) = 0x800;
    g_s.dlgWindow = 0x800; g_s.dlgResult = kDialogAccept;
    g_s.wealth = 1000;
    MasterExamPromptStep(r.get());
    CHECK_EQ(g_s.cmd15Calls, 1);
    CHECK_EQ(g_s.buildingAllOps[0], 8);
    CHECK_EQ(g_leaf.freeCalls, 1);
}

TEST(CharActionW, ExamPromptDecline) {
    InstallAll(); SetClock();
    BigRec r; He_State(r.get()) = 1;
    He_SavedTime(r.get()) = TestClock(); He_SavedTime(r.get()).day = 1;
    Cas4_Slot116(r.get()) = 0x800;
    g_s.dlgWindow = 0x800; g_s.dlgResult = kDialogDecline;
    MasterExamPromptStep(r.get());
    CHECK_EQ(g_s.buildingAllOps[0], -6);
    CHECK_EQ(g_leaf.freeCalls, 1);
}

// ===========================================================================
// MasterExamDecideStep.
// ===========================================================================
TEST(CharActionW, ExamDecideOpensDialog) {
    InstallAll(); SetClock();
    BigRec r; He_State(r.get()) = 0;
    He_SavedTime(r.get()) = TestClock(); He_SavedTime(r.get()).day = 1;   // due
    g_s.wealth = 777;
    Cas4_Slot116(r.get()) = 0x900;   // slot already exists so render runs
    MasterExamDecideStep(r.get());
    CHECK_EQ(Cas4_Fee176(r.get()), 777);   // fee stored into +176
    CHECK_EQ(He_State(r.get()), 1);
    CHECK_EQ(g_s.dialogLines, 1);
}

TEST(CharActionW, ExamDecideAcceptCharges) {
    InstallAll(); SetClock();
    BigRec r; He_State(r.get()) = 1;
    Cas4_Slot116(r.get()) = 0x900;
    Cas4_Fee176(r.get()) = 500;
    // +82 deadline in the future so the "passed" branch is skipped.
    He_ApptTime(r.get()) = TestClock(); He_ApptTime(r.get()).day = 99;
    g_s.dlgWindow = 0x900; g_s.dlgResult = kDialogAccept;
    MasterExamDecideStep(r.get());
    CHECK_EQ(g_s.req16Calls, 1);
    CHECK_EQ(g_s.req16Vals[0], 500);
    CHECK_EQ(g_s.slot28Calls, 1);
    CHECK_EQ(g_leaf.freeCalls, 1);
}

TEST(CharActionW, ExamDecideDeadlinePassed) {
    InstallAll(); SetClock();
    BigRec r; He_State(r.get()) = 1; Cas4_Slot116(r.get()) = 0x900;
    // +82 deadline in the past -> clock > appt -> teardown + free.
    He_ApptTime(r.get()) = TestClock(); He_ApptTime(r.get()).day = 1;
    MasterExamDecideStep(r.get());
    CHECK_EQ(g_s.panelDestroy, 1);
    CHECK_EQ(g_leaf.freeCalls, 1);
}

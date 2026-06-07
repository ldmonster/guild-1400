// Unit tests for charaction_steps6 — batch 6 of the CharAction coroutines (the
// crime / personnel state machines and the He handler registration table). The
// handler-registration table is asserted byte-for-byte against the recovered
// gilde.exe table. GameTime golden vectors were computed with python3 mirroring
// src/sim/gametime.cpp. The cross-cluster leaves are routed through a recording
// mock installed via SetCharActionStep6Hooks / SetNpcLeafHooks.
// Suite prefix: CharActionV6.
#include "test.h"

#include "sim/charaction_steps6.h"
#include "sim/he.h"
#include "sim/npcaction.h"   // NpcClock / SetNpcClock, SetNpcLeafHooks

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// A zeroed record buffer big enough for the +236 personnel-coroutine accesses.
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
    i32 statusRet = 1;
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

// --- recording CharActionStep6Hooks ----------------------------------------
struct S6 {
    HeRecord* personQueryRet = nullptr;
    HeRecord* personByIdRet = nullptr;
    HeRecord* familyRet = nullptr;
    HeRecord* objectRet = nullptr;
    HeRecord* resolveRet = nullptr;
    HeRecord* findFirstRet = nullptr;
    HeRecord* findGestureRet = nullptr;
    int nearestRet = 0; i32 nearestId = 0;
    int isNearDoorRet = 0;
    int wageRet = 0; int currencyRet = 0;
    u8 cityCatRet = 0; i32 cityPersonRet = 99;
    i32 guard61Ret = 0; i32 op73Ret = 0; i32 named53Ret = 0;
    HeRecord* seqBaseRet = nullptr; i32 violationRet = 0;
    int rollRet = 0; int invActiveRet = 0;
    std::vector<int> randomSeq; size_t randomCur = 0;
    std::vector<int> regResults; size_t regCur = 0;

    // call counters
    int freeNamed53 = 0, single49 = 0, request16 = 0, chrMove = 0, coord27 = 0;
    int pair33 = 0, pair36 = 0, changeAction = 0, sendEntity = 0, sendQuick = 0;
    std::vector<HandlerReg> registered;
};
S6 g6;

HeRecord* RPersonQuery(i32, int, int, i32) { return g6.personQueryRet; }
HeRecord* RPersonById(i32) { return g6.personByIdRet; }
HeRecord* RFamily(u16) { return g6.familyRet; }
HeRecord* RObject(i32, int, int, i32) { return g6.objectRet; }
void RResolve(HeRecord** out, i32) { if (out) *out = g6.resolveRet; }
HeRecord* RFindFirst(int, int, i32) { return g6.findFirstRet; }
HeRecord* RFindNext() { return nullptr; }
int RNearest(u16, int, i32* out) { if (out && g6.nearestRet) *out = g6.nearestId; return g6.nearestRet; }
int RIsNearDoor(HeRecord*, i32) { return g6.isNearDoorRet; }
int RWage(u16, int, int) { return g6.wageRet; }
int RCurrency(u16) { return g6.currencyRet; }
u8 RCityCat(u16) { return g6.cityCatRet; }
i32 RCityPerson(u16) { return g6.cityPersonRet; }
void RChangeAction(HeRecord*, HeRecord*, HeRecord*, u16) { ++g6.changeAction; }
i32 RGuard61(HeRecord*, int, int, int) { return g6.guard61Ret; }
i32 ROp73(int, i32, int, int, int, int) { return g6.op73Ret; }
void RReq16(i32, i32, int, u8) { ++g6.request16; }
void RChrMove(i32, i32, i32) { ++g6.chrMove; }
void RSingle49(i32) { ++g6.single49; }
i32 RNamed53(i32, i32, i32, int) { ++g6.freeNamed53; return g6.named53Ret; }
void RCoord27(i32, i32, int) { ++g6.coord27; }
void RPair33(i32, int) { ++g6.pair33; }
void RPair36(i32, i32) { ++g6.pair36; }
i32 RViolation(int, int, i32, i32, i32) { return g6.violationRet; }
void RSendEntity(i32, int) { ++g6.sendEntity; }
void RSendQuick(i32, i32, int) { ++g6.sendQuick; }
HeRecord* RSeqBase(i32) { return g6.seqBaseRet; }
int RRoll() { return g6.rollRet; }
int RInvActive(i32) { return g6.invActiveRet; }
HeRecord* RGesture() { return g6.findGestureRet; }
int RRandom(int) {
    if (g6.randomCur < g6.randomSeq.size()) return g6.randomSeq[g6.randomCur++];
    return 0;
}
int RRegister(u8 t, u32 i, u32 s) {
    g6.registered.push_back(HandlerReg{t, i, s});
    if (g6.regCur < g6.regResults.size()) return g6.regResults[g6.regCur++];
    return 0;
}

void InstallS6() {
    g6 = S6{};
    static CharActionStep6Hooks h;
    h = CharActionStep6Hooks{};
    h.personQueryBegin = RPersonQuery;
    h.findPersonById = RPersonById;
    h.familyRecord = RFamily;
    h.objectQueryFind = RObject;
    h.resolveEntityById = RResolve;
    h.findFirstByFilter = RFindFirst;
    h.findNextMatching = RFindNext;
    h.findNearestEntity = RNearest;
    h.isNearDoor = RIsNearDoor;
    h.computeWage = RWage;
    h.sumCurrencyHeld = RCurrency;
    h.cityCategory = RCityCat;
    h.cityPersonId = RCityPerson;
    h.changePlayerAction = RChangeAction;
    h.queueGuardTarget61 = RGuard61;
    h.requestBuildOp73 = ROp73;
    h.queueRequest16 = RReq16;
    h.requestChrMove = RChrMove;
    h.queueSingle49 = RSingle49;
    h.queueNamedObject53 = RNamed53;
    h.queueCoord27 = RCoord27;
    h.queuePair33 = RPair33;
    h.queuePair36 = RPair36;
    h.evaluateViolation = RViolation;
    h.sendEntityMessage = RSendEntity;
    h.sendQuickjumpMessage = RSendQuick;
    h.packetSeqBase = RSeqBase;
    h.rollWeatherActivity = RRoll;
    h.inventorySlotActive = RInvActive;
    h.findGestureTarget = RGesture;
    h.randomModulo = RRandom;
    h.registerHandler = RRegister;
    SetCharActionStep6Hooks(&h);
}

void InstallAll() { InstallLeaf(); InstallS6(); SetNpcClock(Clock()); }

} // namespace

// ===========================================================================
// Handler registration table golden vector.
// ===========================================================================
TEST(CharActionV6, HandlerTableShape) {
    CHECK_EQ(kCharActionHandlerCount, 66);
    // Spot-check the recovered (type, init, step) at known indices.
    CHECK_EQ((int)kCharActionHandlerTable[0].type, 0x22);
    CHECK_EQ(kCharActionHandlerTable[0].initAddr, 0x4c9d38u);
    CHECK_EQ(kCharActionHandlerTable[0].stepAddr, 0x4c9d38u);
    // 0x43 patrol pair.
    CHECK_EQ((int)kCharActionHandlerTable[14].type, 0x43);
    CHECK_EQ(kCharActionHandlerTable[14].initAddr, 0x4cdd38u);
    CHECK_EQ(kCharActionHandlerTable[14].stepAddr, 0x4cdf74u);
    // Last entry: tutorial 0x87.
    CHECK_EQ((int)kCharActionHandlerTable[65].type, 0x87);
    CHECK_EQ(kCharActionHandlerTable[65].initAddr, 0x4dab00u);
    // The registration order is NOT sorted by type: index 9 is 0x37, index 10 0x36.
    CHECK_EQ((int)kCharActionHandlerTable[9].type, 0x37);
    CHECK_EQ((int)kCharActionHandlerTable[10].type, 0x36);
}

TEST(CharActionV6, RegisterHandlerAllSucceed) {
    InstallAll();
    i32 r = RegisterHandlerTable();
    CHECK_EQ(r, 0);
    CHECK_EQ((int)g6.registered.size(), 66);
    // The replay must match the table exactly, in order.
    bool ok = true;
    for (int i = 0; i < kCharActionHandlerCount; ++i) {
        if (g6.registered[i].type != kCharActionHandlerTable[i].type ||
            g6.registered[i].initAddr != kCharActionHandlerTable[i].initAddr ||
            g6.registered[i].stepAddr != kCharActionHandlerTable[i].stepAddr)
            ok = false;
    }
    CHECK(ok);
}

TEST(CharActionV6, RegisterHandlerShortCircuit) {
    InstallAll();
    // Make the 4th registration fail -> returns 1, stops after 4 calls.
    g6.regResults = {0, 0, 0, 5};
    i32 r = RegisterHandlerTable();
    CHECK_EQ(r, 1);
    CHECK_EQ((int)g6.registered.size(), 4);
}

// ===========================================================================
// Meister Einstellen (hire) phase machine.
// ===========================================================================
TEST(CharActionV6, EinstellenState0Arms) {
    InstallAll();
    BigRec r; r.at<i32>(112) = 0;
    u32 ret = RunMeisterEinstellen(r.get());
    CHECK_EQ(ret, 1u);
    CHECK_EQ(r.at<i32>(112), 1);
}

TEST(CharActionV6, EinstellenTerminalFrees) {
    InstallAll();
    BigRec r; r.at<i32>(112) = -1;
    u32 ret = RunMeisterEinstellen(r.get());
    CHECK_EQ((int)ret, 7);            // freeRet
    CHECK_EQ(g_leaf.freeCalls, 1);
}

TEST(CharActionV6, EinstellenState1NotDueReturnsCmp) {
    InstallAll();
    BigRec r; r.at<i32>(112) = 1;
    // appointment in the FUTURE relative to clock -> cmp >= 0, return without acting.
    r.at<GameTime>(82) = []{ GameTime t = Clock(); t.minute = 59; return t; }();
    u32 ret = RunMeisterEinstellen(r.get());
    CHECK_EQ((int)ret, 1);            // GameTimeCompare returns +1 (appt later)
    CHECK_EQ(g_leaf.freeCalls, 0);
}

TEST(CharActionV6, EinstellenState1NoPurseFrees) {
    InstallAll();
    BigRec self; self.at<u16>(0) = 3; self.at<u16>(39) = 5;
    g6.personQueryRet = self.get();
    g6.wageRet = 1000; g6.currencyRet = 10;   // cannot afford
    BigRec r; r.at<i32>(112) = 1;
    r.at<GameTime>(82) = []{ GameTime t = Clock(); t.minute = 0; return t; }();  // past -> due
    u32 ret = RunMeisterEinstellen(r.get());
    CHECK_EQ((int)ret, 7);
    CHECK_EQ(g6.sendQuick, 1);
    CHECK_EQ(g_leaf.freeCalls, 1);
}

TEST(CharActionV6, EinstellenState1ArmsGuardTarget) {
    InstallAll();
    BigRec self; self.at<u16>(0) = 3; self.at<u16>(39) = 5;
    g6.personQueryRet = self.get();
    g6.wageRet = 5; g6.currencyRet = 1000;     // affordable
    g6.guard61Ret = 333;
    BigRec r; r.at<i32>(112) = 1;
    r.at<i32>(176) = 1;                         // free recruit slot
    r.at<GameTime>(82) = []{ GameTime t = Clock(); t.minute = 0; return t; }();
    u32 ret = RunMeisterEinstellen(r.get());
    CHECK_EQ(r.at<i32>(228), 333);              // guard target handle stored
    CHECK_EQ(r.at<i32>(112), 2);                // advanced to state 2
    // +1 second from clock.
    CHECK(TimeEq(r.at<GameTime>(82), 10, 8, 30, 16));
    CHECK_EQ((int)ret, 8);                      // returned hour-of-day
}

// ===========================================================================
// Meister Entlassen (fire) phase machine — structural twin.
// ===========================================================================
TEST(CharActionV6, EntlassenState0Arms) {
    InstallAll();
    BigRec r; r.at<i32>(112) = 0;
    u32 ret = RunMeisterEntlassen(r.get());
    CHECK_EQ(ret, 1u);
    CHECK_EQ(r.at<i32>(112), 1);
}

TEST(CharActionV6, EntlassenState2NoSeqMessages) {
    InstallAll();
    g_leaf.statusRet = 1;
    g6.seqBaseRet = 0;          // packet applied but no seq -> failure message + free
    BigRec r; r.at<i32>(112) = 2; r.at<i32>(184) = 77;
    u32 ret = RunMeisterEntlassen(r.get());
    CHECK_EQ(g6.sendEntity, 1);
    CHECK_EQ((int)ret, 7);
}

// ===========================================================================
// Pruegel (assault) coroutine.
// ===========================================================================
TEST(CharActionV6, InitPruegelPicksThug) {
    InstallAll();
    BigRec thug; thug.at<i32>(4) = 555;
    g6.personQueryRet = thug.get();
    g6.op73Ret = 12;
    BigRec r; r.at<i32>(12) = -1;   // +16 (cityId accessor) unset -> pick a thug
    r.at<i32>(180) = 0;
    InitPruegel(r.get());
    CHECK_EQ(r.at<i32>(12), 555);   // thug id stored at +16
    CHECK_EQ(r.at<i32>(200), -1);   // assault packet reset
    CHECK_EQ(r.at<i32>(204), 12);   // op73 handle stored
    // clock stamped into +82.
    CHECK(TimeEq(r.at<GameTime>(82), 10, 8, 30, 15));
}

TEST(CharActionV6, RunPruegelTerminalFreesAndPair) {
    InstallAll();
    BigRec r; r.at<i32>(112) = -2; r.at<i32>(200) = 88;  // packet set -> queuePair33
    u32 ret = RunPruegel(r.get());
    CHECK_EQ(g6.pair33, 1);
    CHECK_EQ((int)ret, 7);
    CHECK_EQ(g_leaf.freeCalls, 1);
}

TEST(CharActionV6, RunPruegelState0Payout) {
    InstallAll();
    BigRec seq; seq.at<u16>(0) = 9; seq.at<i32>(4) = 4321;
    g6.seqBaseRet = seq.get();
    g_leaf.statusRet = 1;
    BigRec fam; fam.at<i32>(40) = 100;
    g6.familyRet = fam.get();
    BigRec r; r.at<i32>(112) = 0; r.at<i32>(204) = 5; r.at<i32>(216) = 50;
    u32 ret = RunPruegel(r.get());
    CHECK_EQ(fam.at<i32>(40), 150);     // payout added (+10 dword)
    CHECK_EQ(r.at<i32>(200), 4321);     // assault target from seq+4
    CHECK_EQ(r.at<i32>(204), -1);       // packet reset
    CHECK_EQ(r.at<i32>(112), 1);        // advanced
    CHECK_EQ(g6.changeAction, 1);
    CHECK_EQ(g6.request16, 1);
    CHECK_EQ((int)ret, 1);
}

// ===========================================================================
// Spionage coroutine.
// ===========================================================================
TEST(CharActionV6, InitSpionageAlreadySpawnedNoOp) {
    InstallAll();
    BigRec r; r.at<u8>(121) = 0x04;   // (flags & 0x400) set
    r.at<i32>(132) = 999;
    i32 ret = InitSpionage(r.get());
    CHECK_EQ(ret, 999);
    CHECK_EQ(g_leaf.cmd29Calls, 0);
}

TEST(CharActionV6, InitSpionageNoSpyArmsMinusOne) {
    InstallAll();
    g6.personByIdRet = nullptr;        // spy not found -> LABEL_19
    BigRec r;
    i32 ret = InitSpionage(r.get());
    CHECK_EQ(g_leaf.cmd29Calls, 1);
    CHECK_EQ(g_leaf.cmd29Args[0], -1);
    CHECK_EQ(ret, g_leaf.nextHandle);
    CHECK_EQ(r.at<i32>(132), g_leaf.nextHandle);
}

TEST(CharActionV6, RunSpionageTerminalStateBails) {
    InstallAll();
    BigRec r; r.at<i32>(112) = -2;     // 0xFFFFFFFE
    u32 ret = RunSpionage(r.get());
    CHECK_EQ((int)ret, -2);
    CHECK_EQ(g_leaf.cmd29Calls, 0);
}

TEST(CharActionV6, RunSpionageState4Finalizes) {
    InstallAll();
    g_leaf.statusRet = 1;
    BigRec r; r.at<i32>(112) = 4; r.at<i32>(132) = -1;
    u32 ret = RunSpionage(r.get());
    CHECK_EQ(g_leaf.cmd29Calls, 1);
    CHECK_EQ(g_leaf.cmd29Args[0], -1);
    CHECK_EQ((int)ret, g_leaf.nextHandle);
    // +1 minute applied to the zeroed +82 appointment slot (the original advances
    // the existing slot, it does not stamp the clock in this phase).
    CHECK(TimeEq(r.at<GameTime>(82), 0, 0, 1, 0));
}

// ===========================================================================
// RunMoveCrowdToObject coroutine.
// ===========================================================================
TEST(CharActionV6, CrowdOutOfWindowAdvances) {
    InstallAll();
    GameTime night = Clock(); night.hour = 22; SetNpcClock(night);
    BigRec r; r.at<i32>(112) = 0;
    u32 ret = RunMoveCrowdToObject(r.get());
    // Out-of-window: +5 minutes applied to the zeroed +82 appointment slot; the
    // returned hour-of-day is that slot's hour (0).
    CHECK(TimeEq(r.at<GameTime>(82), 0, 0, 5, 0));
    CHECK_EQ((int)ret, 0);
    CHECK_EQ(g_leaf.freeCalls, 0);
}

TEST(CharActionV6, CrowdBadCityFrees) {
    InstallAll();   // clock is 8:30 -> in window
    BigRec r; r.at<u16>(8) = 0xFFFF; r.at<i32>(112) = 1;
    u32 ret = RunMoveCrowdToObject(r.get());
    CHECK_EQ((int)ret, 7);
    CHECK_EQ(g_leaf.freeCalls, 1);
}

TEST(CharActionV6, CrowdState1ArmsMembers) {
    InstallAll();
    BigRec leader; leader.at<i32>(4) = 70;
    g6.personQueryRet = leader.get();
    BigRec members; members.at<i32>(4) = 1;
    g6.personByIdRet = members.get();   // every member resolves
    BigRec r; r.at<u16>(8) = 1; r.at<i32>(12) = 2; r.at<i32>(112) = 1;
    r.at<u8>(172) = 2;                  // 2 members
    u32 ret = RunMoveCrowdToObject(r.get());
    CHECK_EQ(r.at<i32>(180), 70);       // leader id stored
    CHECK_EQ(r.at<i32>(112), 2);        // advanced
    CHECK_EQ(g6.single49, 2);           // one per member
    CHECK_EQ(g6.freeNamed53, 2);
    CHECK_EQ((int)ret, 2);              // member count returned
}

TEST(CharActionV6, CrowdState2WaitsWhenNotArrived) {
    InstallAll();
    BigRec target; target.at<i32>(4) = 9;
    g6.personQueryRet = target.get();
    BigRec member; g6.personByIdRet = member.get();
    g6.isNearDoorRet = 0;               // not near -> stay in state 2
    BigRec r; r.at<u16>(8) = 1; r.at<i32>(12) = 2; r.at<i32>(112) = 2;
    r.at<u8>(172) = 1;
    u32 ret = RunMoveCrowdToObject(r.get());
    CHECK_EQ(r.at<i32>(112), 2);        // still state 2
    // +5 minutes applied to the zeroed +82 slot.
    CHECK(TimeEq(r.at<GameTime>(82), 0, 0, 5, 0));
    CHECK_EQ((int)ret, 0);
}

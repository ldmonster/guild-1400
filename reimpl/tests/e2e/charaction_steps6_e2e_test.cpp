// End-to-end tests for charaction_steps6 — drive the crime / personnel coroutines
// across their full phase sequences using a stateful recording host, and replay the
// handler registration table. Verifies the multi-phase coroutines advance their
// state deterministically and the cmd29 entity-request handle is threaded through.
// Suite prefix: CharActionV6E2E.
#include "test.h"

#include "sim/charaction_steps6.h"
#include "sim/he.h"
#include "sim/npcaction.h"

#include <cstring>
#include <map>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

struct Rec {
    unsigned char b[512];
    Rec() { std::memset(b, 0, sizeof(b)); }
    HeRecord* get() { return reinterpret_cast<HeRecord*>(b); }
    template <class T> T& at(int o) { return *reinterpret_cast<T*>(b + o); }
};

GameTime MkClock(i32 d, u16 h, i32 m) { GameTime t{}; t.day = d; t.hour = h; t.minute = m; return t; }

// --- stateful host ---------------------------------------------------------
struct Host {
    int cmd29 = 0; i32 nextHandle = 1000; std::vector<int> args;
    int frees = 0;
    i32 statusRet = 1;
    HeRecord* self = nullptr;
    int registrations = 0;
} H;

i32 HFree(HeRecord*) { ++H.frees; return 9; }
i32 HCmd29(int a, HeRecord*) { ++H.cmd29; H.args.push_back(a); return ++H.nextHandle; }
i32 HStatus(i32) { return H.statusRet; }

HeRecord* HQuery(i32, int, int, i32) { return H.self; }
HeRecord* HById(i32) { return H.self; }
int HRegister(u8, u32, u32) { ++H.registrations; return 0; }

// Inert stubs for the leaves these flows don't drive (every member must be set so
// the coroutines never call through a null pointer).
HeRecord* ZQuery(i32, int, int, i32) { return nullptr; }
HeRecord* ZFamily(u16) { return nullptr; }
HeRecord* ZObject(i32, int, int, i32) { return nullptr; }
void ZResolve(HeRecord** o, i32) { if (o) *o = nullptr; }
HeRecord* ZFirst(int, int, i32) { return nullptr; }
HeRecord* ZNext() { return nullptr; }
int ZNearest(u16, int, i32*) { return 0; }
int ZNearDoor(HeRecord*, i32) { return 0; }
int ZWage(u16, int, int) { return 0; }
int ZCurrency(u16) { return 0; }
u8 ZCat(u16) { return 0; }
i32 ZCityPerson(u16) { return 0; }
void ZChange(HeRecord*, HeRecord*, HeRecord*, u16) {}
i32 ZGuard61(HeRecord*, int, int, int) { return 0; }
i32 ZOp73(int, i32, int, int, int, int) { return 0; }
void ZReq16(i32, i32, int, u8) {}
void ZChrMove(i32, i32, i32) {}
void ZSingle49(i32) {}
i32 ZNamed53(i32, i32, i32, int) { return 0; }
void ZCoord27(i32, i32, int) {}
void ZPair33(i32, int) {}
void ZPair36(i32, i32) {}
i32 ZViolation(int, int, i32, i32, i32) { return 0; }
void ZSendEntity(i32, int) {}
void ZSendQuick(i32, i32, int) {}
HeRecord* ZSeq(i32) { return nullptr; }
int ZRoll() { return 0; }
int ZInv(i32) { return 0; }
HeRecord* ZGesture() { return nullptr; }
int ZRandom(int) { return 0; }

void Install() {
    H = Host{};
    static NpcLeafHooks lh; lh = NpcLeafHooks{};
    lh.freeHandlerEntry = HFree;
    lh.queueRequestEntity29 = HCmd29;
    lh.packetStatus = HStatus;
    SetNpcLeafHooks(&lh);

    static CharActionStep6Hooks h; h = CharActionStep6Hooks{};
    h.personQueryBegin = HQuery;
    h.findPersonById = HById;
    h.familyRecord = ZFamily;
    h.objectQueryFind = ZObject;
    h.resolveEntityById = ZResolve;
    h.findFirstByFilter = ZFirst;
    h.findNextMatching = ZNext;
    h.findNearestEntity = ZNearest;
    h.isNearDoor = ZNearDoor;
    h.computeWage = ZWage;
    h.sumCurrencyHeld = ZCurrency;
    h.cityCategory = ZCat;
    h.cityPersonId = ZCityPerson;
    h.changePlayerAction = ZChange;
    h.queueGuardTarget61 = ZGuard61;
    h.requestBuildOp73 = ZOp73;
    h.queueRequest16 = ZReq16;
    h.requestChrMove = ZChrMove;
    h.queueSingle49 = ZSingle49;
    h.queueNamedObject53 = ZNamed53;
    h.queueCoord27 = ZCoord27;
    h.queuePair33 = ZPair33;
    h.queuePair36 = ZPair36;
    h.evaluateViolation = ZViolation;
    h.sendEntityMessage = ZSendEntity;
    h.sendQuickjumpMessage = ZSendQuick;
    h.packetSeqBase = ZSeq;
    h.rollWeatherActivity = ZRoll;
    h.inventorySlotActive = ZInv;
    h.findGestureTarget = ZGesture;
    h.randomModulo = ZRandom;
    h.registerHandler = HRegister;
    (void)ZQuery;
    SetCharActionStep6Hooks(&h);
    SetNpcClock(MkClock(20, 8, 0));
}

} // namespace

// Full handler table registration end-to-end.
TEST(CharActionV6E2E, RegisterFullTable) {
    Install();
    i32 r = RegisterHandlerTable();
    CHECK_EQ(r, 0);
    CHECK_EQ(H.registrations, 66);
}

// Einstellen: arm (state 0) then immediately reach a terminal frees-on-no-self.
TEST(CharActionV6E2E, EinstellenArmThenDue) {
    Install();
    Rec r;
    // state 0 -> 1
    CHECK_EQ(RunMeisterEinstellen(r.get()), 1u);
    CHECK_EQ(r.at<i32>(112), 1);
    // state 1, appointment already past (zeroed +82 < clock day 20) -> due; no self
    // record installed -> frees.
    H.self = nullptr;
    u32 ret = RunMeisterEinstellen(r.get());
    CHECK_EQ((int)ret, 9);
    CHECK_EQ(H.frees, 1);
}

// Spionage: a full Run pass through the finalize state threads the cmd29 handle.
TEST(CharActionV6E2E, SpionageFinalizeThreadsHandle) {
    Install();
    Rec r; r.at<i32>(112) = 4; r.at<i32>(132) = -1;   // state 4, no armed packet
    u32 first = RunSpionage(r.get());
    CHECK_EQ((int)first, r.at<i32>(132));             // handle stored into +132
    CHECK_EQ(H.cmd29, 1);
    CHECK_EQ(H.args[0], -1);
    // A second pass now sees +132 armed; with statusRet=1 it proceeds and re-arms.
    int prevHandle = r.at<i32>(132);
    u32 second = RunSpionage(r.get());
    CHECK_EQ((int)second, r.at<i32>(132));
    CHECK(r.at<i32>(132) != prevHandle);              // a fresh handle each pass
}

// Pruegel: drive terminal -> free; then a fresh record state 0 with no packet seq
// transitions to the failure (-1) branch.
TEST(CharActionV6E2E, PruegelTerminalAndNoSeq) {
    Install();
    Rec a; a.at<i32>(112) = -1; a.at<i32>(200) = -1;  // no packet -> no pair33
    u32 r1 = RunPruegel(a.get());
    CHECK_EQ((int)r1, 9);
    CHECK_EQ(H.frees, 1);

    Rec b; b.at<i32>(112) = 0; b.at<i32>(204) = -1;   // state 0, packet -1 -> seq path
    H.statusRet = 1;
    // packetSeqBase defaults inert(0) since not installed -> failure -> state -1.
    u32 r2 = RunPruegel(b.get());
    CHECK_EQ(b.at<i32>(112), -1);
    CHECK_EQ((int)r2, -1);
}

// Crowd: out-of-window night clock just advances time; in-window bad-city frees.
TEST(CharActionV6E2E, CrowdWindowGate) {
    Install();
    SetNpcClock(MkClock(20, 23, 0));                  // night -> out of window
    Rec r; r.at<i32>(112) = 0;
    u32 ret = RunMoveCrowdToObject(r.get());
    // +5 min applied to the zeroed +82 slot -> hour-of-day 0; no free.
    CHECK_EQ((int)ret, 0);
    CHECK_EQ(H.frees, 0);

    SetNpcClock(MkClock(20, 9, 0));                   // day -> in window
    Rec r2; r2.at<u16>(8) = 0xFFFF; r2.at<i32>(112) = 1;
    u32 ret2 = RunMoveCrowdToObject(r2.get());
    CHECK_EQ((int)ret2, 9);                           // HFree return
    CHECK_EQ(H.frees, 1);
}

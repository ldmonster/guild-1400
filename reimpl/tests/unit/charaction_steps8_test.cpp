// Unit tests for charaction_steps8 — the final CharAction step-machine slice.
// Each non-trivial function is exercised through the installable hook table with a
// recording Wire, plus the shared NpcLeafHooks (freeHandlerEntry/packetStatus).
// Golden GameTime values are computed with the gilde GameTimeAdvance semantics
// (arg2 "addDays" lands on the HOUR field; see WAVE14_BRIEF). CHECK/CHECK_EQ do not
// abort, so every pointer deref is guarded.
#include "test.h"

#include "sim/charaction_steps8.h"
#include "sim/gametime.h"
#include "sim/npcaction.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

// A record buffer wide enough for the deepest offset this batch touches (+361).
struct Block { u8 b[700]; };

inline HeRecord* H(Block& blk) { return reinterpret_cast<HeRecord*>(blk.b); }
inline void Zero(Block& blk) { std::memset(blk.b, 0, sizeof(blk.b)); }

// Recording Wire: counts leaf invocations and parks return values the tests pin.
struct Wire {
    int freeHandler = 0;
    int packetStatusRet = 1;     // "applied" by default
    int free29 = 0, args25 = 0, single49 = 0, named53 = 0, flag55 = 0;
    int req17 = 0, slotReset28 = 0, entity29 = 0, state22 = 0, beginDelta = 0;
    int rawField = 0, deltaField = 0, enqueue15 = 0, changeZustand = 0;
    int rendered = 0, quickjump = 0, reportErr = 0, changeAction = 0;
    int destroy = 0, freeMesh = 0, releaseMesh = 0, attach = 0;
    int resolveCalls = 0;

    HeRecord* personBegin = nullptr;
    HeRecord* personById  = nullptr;
    HeRecord* active       = nullptr;
    HeRecord* objQuery     = nullptr;
    HeRecord* resolveObj   = nullptr;
    HeRecord* building      = nullptr;
    HeRecord* workProduct   = nullptr;
    HeRecord* storable      = nullptr;
    HeRecord* findFirst     = nullptr;   // single-shot pool walk
    void*     attachRet     = nullptr;
    int roomWorth = 0;
    int prodOutput = 0;
    int childMoney = 0;
    int sumWork = 0;
    int rngValue = 0;
};
Wire g;

// --- NpcLeafHooks (shared) ---
i32 wFree(HeRecord*)        { g.freeHandler++; return 7; }
i32 wPacket(i32)           { return g.packetStatusRet; }
i32 wEnt29Leaf(int, HeRecord*) { return 0; }
i32 wLawText()            { return 0; }
int wFindInv(int, i32*)   { return 0; }
void wShuffle(int, i32*)  {}
void wBuild93(i32,int,i32,u8) {}
NpcLeafHooks gLeaf = { wEnt29Leaf, wFree, wPacket, wLawText, wFindInv, wShuffle, wBuild93 };

// --- CharActionStep8Hooks ---
HeRecord* hPersonBegin(i32, int, int, i32) { return g.personBegin; }
HeRecord* hPersonById(i32)                 { return g.personById; }
HeRecord* hActive(HeRecord*)               { return g.active; }
HeRecord* hObjQuery(i32, int, int, int, i32) { return g.objQuery; }
HeRecord* hResolve(int, HeRecord** out, i32, int) { g.resolveCalls++; if (out) *out = g.resolveObj; return g.resolveObj; }
int  hChildMoney(i32)                      { return g.childMoney; }
HeRecord* hBuilding(i32)                    { return g.building; }
HeRecord* hWorkProduct(HeRecord*)           { return g.workProduct; }
HeRecord* hStorable(HeRecord*)              { return g.storable; }
int  hSumWork(HeRecord*, int, int)          { return g.sumWork; }
int  hRoomWorth(HeRecord*, int, i32)        { return g.roomWorth; }
int  hProdOut(GameTime*, const GameTime*)   { return g.prodOutput; }
HeRecord* hFindFirst(int, int, int)         { HeRecord* r = g.findFirst; g.findFirst = nullptr; return r; }
HeRecord* hFindNext()                       { return nullptr; }
void hArgs25(i32, int, int, int, int)       { g.args25++; }
void hSingle49(i32)                         { g.single49++; }
void hNamed53(i32, i32, int, int, int, const char*) { g.named53++; }
void hFlag55(i32, int)                      { g.flag55++; }
void hReq17(i32, int, int, int, int, int)   { g.req17++; }
void hSlotReset28(void*, int)               { g.slotReset28++; }
i32  hEntity29(i32, void*)                  { g.entity29++; return 321; }
void hState22()                             { g.state22++; }
void hBeginDelta(i32, i32)                  { g.beginDelta++; }
void hRawField(unsigned, unsigned, void*, int) { g.rawField++; }
void hDeltaField(unsigned, unsigned, void*, int) { g.deltaField++; }
void hEnqueue15(i32, i32, int, u8)          { g.enqueue15++; }
void hChangeZ(i32, int, i32)                { g.changeZustand++; }
void hRender(char* dst, int, int, int, int) { g.rendered++; if (dst) dst[0] = 0; }
void hQuickjump(i32, i32, i32, const char*, int) { g.quickjump++; }
void hReportErr(const char*)                { g.reportErr++; }
void hChangeAction(i32, int, int, int)      { g.changeAction++; }
int  hDestroy(i32)                          { g.destroy++; return 99; }
int  hFreeMesh()                            { g.freeMesh++; return 5; }
void hReleaseMesh(int)                      { g.releaseMesh++; }
void* hAttach(i32, bool, int)               { g.attach++; return g.attachRet; }
int  hRng(int)                              { return g.rngValue; }
i32  hCity(u16)                             { return 0; }
u8   hCat(u16)                              { return 0; }

CharActionStep8Hooks MakeHooks() {
    CharActionStep8Hooks h{};
    h.personQueryBegin = hPersonBegin;
    h.personFindRecordById = hPersonById;
    h.personFindActiveByEntity = hActive;
    h.objectQueryFind = hObjQuery;
    h.resolveEntityById = hResolve;
    h.sumChildMoney = hChildMoney;
    h.buildingFindById = hBuilding;
    h.findWorkProduct = hWorkProduct;
    h.findStorable = hStorable;
    h.sumWorkstation = hSumWork;
    h.computeRoomWorth = hRoomWorth;
    h.productionOutput = hProdOut;
    h.heFindFirst = hFindFirst;
    h.heFindNext = hFindNext;
    h.cmdRequestArgs25 = hArgs25;
    h.cmdRequestSingle49 = hSingle49;
    h.cmdRequestNamedObject53 = hNamed53;
    h.cmdRequestFlag55 = hFlag55;
    h.cmdRequest17 = hReq17;
    h.cmdRequestSlotReset28 = hSlotReset28;
    h.cmdRequestEntity29 = hEntity29;
    h.cmdRequestState22 = hState22;
    h.cmdBeginDeltaPacket = hBeginDelta;
    h.cmdAppendRawField = hRawField;
    h.cmdAppendDeltaField = hDeltaField;
    h.cmdEnqueue15 = hEnqueue15;
    h.requestChangeZustand = hChangeZ;
    h.renderFormatted = hRender;
    h.sendQuickjump = hQuickjump;
    h.reportError = hReportErr;
    h.changePlayerAction = hChangeAction;
    h.characterDestroy = hDestroy;
    h.animFindFreeMeshSlot = hFreeMesh;
    h.animReleaseMeshData = hReleaseMesh;
    h.attachAni = hAttach;
    h.randomModulo = hRng;
    h.cityRecipientId = hCity;
    h.cityCategory = hCat;
    h.poolSlot = nullptr;     // pool slots set per-test below
    h.poolClear = nullptr;
    return h;
}

// Pool backing for QueueFreeAll (512 i32 slots).
i32 g_pool[512];
i32 wPoolSlot(int byteIdx) { return g_pool[byteIdx / 4]; }
void wPoolClear(int byteIdx) { g_pool[byteIdx / 4] = 0; }

void Reset() {
    g = Wire{};
    SetNpcLeafHooks(&gLeaf);
}

GameTime MkClock(int day, int hour, int minute, int second) {
    GameTime t{}; t.day = day; t.hour = static_cast<u16>(hour); t.minute = minute; t.second = second;
    return t;
}

} // namespace

// ---------------------------------------------------------------------------
TEST(CharActionSteps8, ResetWalkTargetClearsGlobals) {
    Reset();
    g_walkTargetA = 5; g_walkTargetB = 6; g_walkTargetC = 7; g_walkTargetD = 8;
    ResetWalkTarget();
    CHECK_EQ(g_walkTargetA, -1);
    CHECK_EQ(g_walkTargetB, -1);
    CHECK_EQ(g_walkTargetC, 0);
    CHECK_EQ(g_walkTargetD, 0);
}

TEST(CharActionSteps8, QueueFreeAllDestroysLiveSlots) {
    Reset();
    CharActionStep8Hooks h = MakeHooks();
    h.poolSlot = wPoolSlot; h.poolClear = wPoolClear;
    SetCharActionStep8Hooks(&h);
    std::memset(g_pool, 0, sizeof(g_pool));
    g_pool[3] = 1000;     // byte 12
    g_pool[10] = 2000;    // byte 40
    g_pool[511] = 3000;   // last slot (byte 2044)
    i32 r = QueueFreeAll();
    CHECK_EQ(g.destroy, 3);
    CHECK_EQ(g_pool[3], 0);
    CHECK_EQ(g_pool[10], 0);
    CHECK_EQ(g_pool[511], 0);
    CHECK_EQ(r, 99);      // last Destroy return
    SetCharActionStep8Hooks(nullptr);
}

TEST(CharActionSteps8, QueueFreeAllEmptyPoolNoDestroy) {
    Reset();
    CharActionStep8Hooks h = MakeHooks();
    h.poolSlot = wPoolSlot; h.poolClear = wPoolClear;
    SetCharActionStep8Hooks(&h);
    std::memset(g_pool, 0, sizeof(g_pool));
    QueueFreeAll();
    CHECK_EQ(g.destroy, 0);
    SetCharActionStep8Hooks(nullptr);
}

TEST(CharActionSteps8, InitArrestNullTargetFrees) {
    Reset();
    CharActionStep8Hooks h = MakeHooks();
    g.personBegin = nullptr;          // resolve fails
    SetCharActionStep8Hooks(&h);
    SetNpcClock(MkClock(10, 8, 30, 0));
    Block blk; Zero(blk);
    He_Flags(H(blk)) = 0;
    HeRecord* r = InitArrestPerson(H(blk));
    // clock stamped + advanced (+4h with rng 0) before the resolve bail.
    CHECK_EQ(static_cast<int>(He_ApptTime(H(blk)).hour), 12);
    CHECK_EQ(He_ApptTime(H(blk)).day, 10);
    CHECK_EQ(g.freeHandler, 1);
    CHECK(reinterpret_cast<intptr_t>(r) == 7);
    SetCharActionStep8Hooks(nullptr);
}

TEST(CharActionSteps8, InitArrestImmuneClassFrees) {
    Reset();
    CharActionStep8Hooks h = MakeHooks();
    Block person; Zero(person);
    person.b[0] = 12;                 // class byte 12 -> immune
    g.personBegin = reinterpret_cast<HeRecord*>(person.b);
    SetCharActionStep8Hooks(&h);
    SetNpcClock(MkClock(10, 8, 30, 0));
    Block blk; Zero(blk);
    InitArrestPerson(H(blk));
    CHECK_EQ(g.freeHandler, 1);
    CHECK_EQ(g.args25, 0);            // never armed the arrest cmd
    SetCharActionStep8Hooks(nullptr);
}

TEST(CharActionSteps8, InitArrestNormalArmsCmd) {
    Reset();
    CharActionStep8Hooks h = MakeHooks();
    Block person; Zero(person);
    person.b[0] = 3;                  // ordinary class
    *reinterpret_cast<u16*>(person.b + 37) = 0xFFFF;  // no guild city
    g.personBegin = reinterpret_cast<HeRecord*>(person.b);
    SetCharActionStep8Hooks(&h);
    SetNpcClock(MkClock(10, 8, 30, 0));
    Block blk; Zero(blk);
    He_Flags(H(blk)) = 0;             // no master-notify pass
    HeRecord* r = InitArrestPerson(H(blk));
    CHECK_EQ(g.args25, 1);
    CHECK_EQ(He_ReqHandle(H(blk)), -1);
    CHECK_EQ(Cas8_Iter(H(blk)), 0);
    CHECK(r == H(blk));              // returns the record itself
    CHECK_EQ(g.freeHandler, 0);
    SetCharActionStep8Hooks(nullptr);
}

TEST(CharActionSteps8, RunArrestFinishState) {
    Reset();
    CharActionStep8Hooks h = MakeHooks();
    g.personBegin = nullptr;
    SetCharActionStep8Hooks(&h);
    Block blk; Zero(blk);
    He_Flags(H(blk)) = 0;
    He_State(H(blk)) = -1;            // finish
    i32 r = RunArrestPerson(H(blk), 0, nullptr);
    CHECK_EQ(g.freeHandler, 1);
    CHECK_EQ(r, 7);
    SetCharActionStep8Hooks(nullptr);
}

TEST(CharActionSteps8, RunArrestWorkBumpsIterAndRearms) {
    Reset();
    CharActionStep8Hooks h = MakeHooks();
    SetCharActionStep8Hooks(&h);
    SetNpcClock(MkClock(10, 8, 30, 0));
    Block blk; Zero(blk);
    He_Flags(H(blk)) = 0;             // packet gate off
    He_State(H(blk)) = 0;            // work
    Cas8_Iter(H(blk)) = 0;           // < 2, so skip the bribe check
    g.rngValue = 0;                  // +4h
    i32 r = RunArrestPerson(H(blk), 0, nullptr);
    CHECK_EQ(Cas8_Iter(H(blk)), 1);
    CHECK_EQ(static_cast<int>(He_ApptTime(H(blk)).hour), 12);
    CHECK_EQ(r, 12);                 // GameTimeAdvance return == hour
    SetCharActionStep8Hooks(nullptr);
}

TEST(CharActionSteps8, RunArrestPacketPendingReturnsZero) {
    Reset();
    CharActionStep8Hooks h = MakeHooks();
    SetCharActionStep8Hooks(&h);
    Block blk; Zero(blk);
    He_Flags(H(blk)) = 2;            // gate on
    He_ReqHandle(H(blk)) = 55;
    g.packetStatusRet = 0;           // still pending
    i32 r = RunArrestPerson(H(blk), 0, nullptr);
    CHECK_EQ(r, 0);
    CHECK_EQ(g.freeHandler, 0);
    SetCharActionStep8Hooks(nullptr);
}

TEST(CharActionSteps8, InitEscortNullFrees) {
    Reset();
    CharActionStep8Hooks h = MakeHooks();
    g.personBegin = nullptr;
    SetCharActionStep8Hooks(&h);
    Block blk; Zero(blk);
    i32 r = InitEscortPrisoner(H(blk), 0, nullptr);
    CHECK_EQ(g.freeHandler, 1);
    CHECK_EQ(r, 7);
    SetCharActionStep8Hooks(nullptr);
}

TEST(CharActionSteps8, InitEscortStampsAndAdvances) {
    Reset();
    CharActionStep8Hooks h = MakeHooks();
    Block person; Zero(person);
    *reinterpret_cast<i32*>(person.b + 1) = 4242;  // begin[+1] entity id
    g.personBegin = reinterpret_cast<HeRecord*>(person.b);
    SetCharActionStep8Hooks(&h);
    SetNpcClock(MkClock(10, 8, 30, 0));
    Block blk; Zero(blk);
    Cas8_CountA(H(blk)) = 10;         // speed -> 10*0.5 = 5 minutes on +68
    InitEscortPrisoner(H(blk), 0, reinterpret_cast<HeRecord*>(0x10));
    CHECK_EQ(Cas8_Misc16(H(blk)), 4242);
    CHECK_EQ(g.args25, 1);
    // appointment (+82) advanced +10 min: 8:40
    CHECK_EQ(He_ApptTime(H(blk)).minute, 40);
    // saved pose (+68) advanced +5 min: 8:35
    GameTime* saved = reinterpret_cast<GameTime*>(blk.b + 68);
    CHECK_EQ(saved->minute, 35);
    SetCharActionStep8Hooks(nullptr);
}

TEST(CharActionSteps8, LagerFuellenCancelPhase) {
    Reset();
    CharActionStep8Hooks h = MakeHooks();
    Block obj; Zero(obj);
    *reinterpret_cast<i32*>(obj.b + 1) = 77;
    g.resolveObj = reinterpret_cast<HeRecord*>(obj.b);
    SetCharActionStep8Hooks(&h);
    Block blk; Zero(blk);
    He_State(H(blk)) = -2;            // +2 == 0 -> cancel + free
    i32 r = RunLagerFuellen(H(blk), 0, 0);
    CHECK_EQ(g.args25, 1);            // cancel reservation
    CHECK_EQ(g.freeHandler, 1);
    CHECK_EQ(r, 7);
    SetCharActionStep8Hooks(nullptr);
}

TEST(CharActionSteps8, LagerFuellenFinalizeAnnounces) {
    Reset();
    CharActionStep8Hooks h = MakeHooks();
    Block obj; Zero(obj);
    *reinterpret_cast<i32*>(obj.b + 1) = 77;
    g.resolveObj = reinterpret_cast<HeRecord*>(obj.b);
    g.building = nullptr; g.workProduct = nullptr;
    SetCharActionStep8Hooks(&h);
    Block blk; Zero(blk);
    He_State(H(blk)) = 1;            // +2 == 3 -> finalize
    Cas8_Misc216(H(blk)) = 1;        // announce flag set
    i32 r = RunLagerFuellen(H(blk), 0, 0);
    CHECK_EQ(g.rendered, 1);
    CHECK_EQ(g.quickjump, 1);
    CHECK_EQ(g.beginDelta, 1);
    CHECK_EQ(g.deltaField, 1);
    CHECK_EQ(g.state22, 1);
    CHECK_EQ(g.freeHandler, 1);
    CHECK_EQ(r, 7);
    SetCharActionStep8Hooks(nullptr);
}

TEST(CharActionSteps8, LagerErweiternState0Advances) {
    Reset();
    CharActionStep8Hooks h = MakeHooks();
    SetCharActionStep8Hooks(&h);
    Block blk; Zero(blk);
    He_State(H(blk)) = 0;
    i32 r = RunLagerErweitern(H(blk), 0);
    CHECK_EQ(He_State(H(blk)), 1);   // ++state
    CHECK_EQ(r, 1);
    SetCharActionStep8Hooks(nullptr);
}

TEST(CharActionSteps8, LagerErweiternFinishState) {
    Reset();
    CharActionStep8Hooks h = MakeHooks();
    SetCharActionStep8Hooks(&h);
    Block blk; Zero(blk);
    He_State(H(blk)) = -1;
    i32 r = RunLagerErweitern(H(blk), 0);
    CHECK_EQ(g.freeHandler, 1);
    CHECK_EQ(r, 7);
    SetCharActionStep8Hooks(nullptr);
}

TEST(CharActionSteps8, LagerErweiternWorkEmitsDeltaAndRearms) {
    Reset();
    CharActionStep8Hooks h = MakeHooks();
    Block obj; Zero(obj);
    g.resolveObj = reinterpret_cast<HeRecord*>(obj.b);
    SetCharActionStep8Hooks(&h);
    SetNpcClock(MkClock(10, 8, 30, 0));
    Block blk; Zero(blk);
    He_State(H(blk)) = 1;
    // appt far in the future so GameTimeCompare(appt, clock) < 0.
    He_ApptTime(H(blk)) = MkClock(5, 0, 0, 0);   // earlier than clock day10 -> appt<clock
    Cas8_CountA(H(blk)) = 3;         // count > 1 so it re-arms (not finalize)
    i32 r = RunLagerErweitern(H(blk), 0);
    CHECK_EQ(g.beginDelta, 1);
    CHECK_EQ(g.rawField, 1);
    CHECK_EQ(g.state22, 1);
    CHECK_EQ(Cas8_CountA(H(blk)), 2);
    // re-armed +15 min -> 8:45
    CHECK_EQ(He_ApptTime(H(blk)).minute, 45);
    CHECK_EQ(r, 8);
    SetCharActionStep8Hooks(nullptr);
}

TEST(CharActionSteps8, AdjustObjectFieldState0Advances) {
    Reset();
    CharActionStep8Hooks h = MakeHooks();
    SetCharActionStep8Hooks(&h);
    Block blk; Zero(blk);
    He_State(H(blk)) = 0;
    i32 r = RunAdjustObjectField(H(blk), 0, 0);
    CHECK_EQ(He_State(H(blk)), 1);
    CHECK_EQ(r, 2);                  // v5 + 2
    SetCharActionStep8Hooks(nullptr);
}

TEST(CharActionSteps8, AdjustObjectFieldFinishState) {
    Reset();
    CharActionStep8Hooks h = MakeHooks();
    SetCharActionStep8Hooks(&h);
    Block blk; Zero(blk);
    He_State(H(blk)) = -1;
    i32 r = RunAdjustObjectField(H(blk), 0, 0);
    CHECK_EQ(g.freeHandler, 1);
    CHECK_EQ(r, 7);
    SetCharActionStep8Hooks(nullptr);
}

TEST(CharActionSteps8, HerdResetPhaseFrees) {
    Reset();
    CharActionStep8Hooks h = MakeHooks();
    g.personById = nullptr;
    SetCharActionStep8Hooks(&h);
    Block blk; Zero(blk);
    blk.b[172] = 2;                  // herd count
    He_State(H(blk)) = -2;           // +2 == 0 -> reset + free
    i32 r = RunHerdAnimals(H(blk), 0);
    CHECK_EQ(g.freeHandler, 1);
    CHECK_EQ(r, 7);
    SetCharActionStep8Hooks(nullptr);
}

TEST(CharActionSteps8, HerdMovePhaseIssuesAndArms) {
    Reset();
    CharActionStep8Hooks h = MakeHooks();
    Block animal; Zero(animal);
    *reinterpret_cast<i32*>(animal.b + 91 * 4) = 555;  // rec[91] nonzero
    g.personById = reinterpret_cast<HeRecord*>(animal.b);
    SetCharActionStep8Hooks(&h);
    SetNpcClock(MkClock(10, 8, 30, 0));
    Block blk; Zero(blk);
    blk.b[172] = 2;                  // 2 animals
    Cas8_Member(H(blk), 0) = 100;
    Cas8_Member(H(blk), 1) = 101;
    He_State(H(blk)) = 0;            // +2 == 2 -> move
    g.rngValue = 0;                  // +rnd 0 min, +2h
    i32 r = RunHerdAnimals(H(blk), 0);
    CHECK_EQ(g.single49, 2);
    CHECK_EQ(g.named53, 2);
    CHECK_EQ(He_State(H(blk)), 1);   // ++state
    // +2 days(=hours) -> 10:30
    CHECK_EQ(static_cast<int>(He_ApptTime(H(blk)).hour), 10);
    CHECK_EQ(r, 10);
    SetCharActionStep8Hooks(nullptr);
}

TEST(CharActionSteps8, FindGestureNoHandlersReturnsZero) {
    Reset();
    CharActionStep8Hooks h = MakeHooks();
    g.findFirst = nullptr;           // empty pool
    SetCharActionStep8Hooks(&h);
    Block self; Zero(self);
    *reinterpret_cast<u8**>(self.b + 97) = self.b;  // selfChar (byte 97) nonnull
    Block goal; Zero(goal);
    GestureCtx ctx{}; ctx.self = self.b; ctx.radius = 5.0f; ctx.goal = goal.b; ctx.found = nullptr;
    int r = FindGestureTarget(&ctx);
    CHECK_EQ(r, 0);
    SetCharActionStep8Hooks(nullptr);
}

TEST(CharActionSteps8, FindGestureNullSelfReturnsZero) {
    Reset();
    CharActionStep8Hooks h = MakeHooks();
    SetCharActionStep8Hooks(&h);
    GestureCtx ctx{};  // null self
    int r = FindGestureTarget(&ctx);
    CHECK_EQ(r, 0);
    SetCharActionStep8Hooks(nullptr);
}

TEST(CharActionSteps8, MorphAttachFailUnlinks) {
    Reset();
    CharActionStep8Hooks h = MakeHooks();
    g.attachRet = nullptr;           // attach fails
    SetCharActionStep8Hooks(&h);
    Block blk; Zero(blk);
    Block charRec; Zero(charRec);
    *reinterpret_cast<void**>(blk.b + 5 * 4) = charRec.b;  // a1[5] = char sub-record
    int r = MorphMovementInit(H(blk), nullptr);
    CHECK_EQ(g.attach, 1);
    CHECK_EQ(r, 0);
    SetCharActionStep8Hooks(nullptr);
}

TEST(CharActionSteps8, MorphAttachSuccessSetsFlags) {
    Reset();
    CharActionStep8Hooks h = MakeHooks();
    Block entry; Zero(entry);
    g.attachRet = entry.b;           // attach succeeds
    SetCharActionStep8Hooks(&h);
    Block blk; Zero(blk);
    Block charRec; Zero(charRec);
    *reinterpret_cast<void**>(blk.b + 5 * 4) = charRec.b;
    charRec.b[4] = 0;                // normal speed -> 20.0f base
    int r = MorphMovementInit(H(blk), nullptr);
    CHECK_EQ(r, 1);
    // entry flag byte at +109: low6 set, bit6 on, bit4/bit5 cleared.
    CHECK_EQ(entry.b[109] & 0x40, 0x40);
    CHECK_EQ(entry.b[109] & 0x10, 0);
    CHECK_EQ(entry.b[110] & 0x06, 0x06);   // bits 1 and 2 set
    CHECK_EQ(entry.b[110] & 0x08, 0);      // bit 3 cleared
    // a1[65] (byte +260) set to 1.
    CHECK_EQ(*reinterpret_cast<i32*>(blk.b + 65 * 4), 1);
    SetCharActionStep8Hooks(nullptr);
}

TEST(CharActionSteps8, PickFromGroundFinishState) {
    Reset();
    CharActionStep8Hooks h = MakeHooks();
    SetCharActionStep8Hooks(&h);
    Block blk; Zero(blk);
    He_State(H(blk)) = -1;
    i32 r = RunPickFromGround(H(blk), nullptr, nullptr);
    CHECK_EQ(g.freeHandler, 1);
    CHECK_EQ(r, 7);
    SetCharActionStep8Hooks(nullptr);
}

// Unit tests for the NpcAction10 batch of Guild NPC behaviour state machines.
// Golden vectors are derived from the 1:1 IDA pseudocode (constants via get_bytes,
// RNG sequences computed against the reconstructed CRT LCG, transitions read off the
// +112/+120 switch dispatch). A recording mock hooks struct captures the cross-module
// leaf calls so the state transitions and emission order can be asserted exactly.
#include "test.h"

#include "sim/npcaction10.h"
#include "sim/npcaction.h"      // NpcClock / SetNpcClock
#include "sim/gametime.h"
#include "util/math_random.h"
#include "crt/rand.h"

#include <cstring>
#include <vector>
#include <string>

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// Recording mock. Backed by a small synthetic scene. A "record" handle is just a
// pointer to one of these; the field reads (objId/kind/etc.) come straight off it.
// ---------------------------------------------------------------------------
namespace {

struct FakeRec {
    i32 id = 0;
    u16 marker = 0;
    u8  kind = 0;
    u8  rank = 0;
    bool hasChar = false;
    i32 wealth = 0;
    i32 currency = 0;
};

struct Rec10 {
    std::vector<std::string> calls;
    // configurable backends
    int  packetStatusRet = 1;
    i32  entity29Ret = 4242;
    bool freeCalled = false;
    FakeRec* personById = nullptr;
    FakeRec* queryBeginRet = nullptr;
    FakeRec* resolveRet = nullptr;
    FakeRec* queryFindRet = nullptr;
    int  handlerCount = 0;
    bool handlerMatch = false;
    int  ratingRet = 0;          // *100 form
    double floatRoll = 0.0;
    bool affordable = true;
    int  slotsWorth = 0;
    bool meisterOk = true;
    u8   professionCode = 7;
    bool inventoryActive = false;
    int  relation = -100;
    bool carryTarget = false;
    bool tolerance = false;
    bool formMatch = false;
    int  formCode = -1;
    FakeRec* panelWin = nullptr;
};

Rec10* g_rec = nullptr;

void log(const char* s) { if (g_rec) g_rec->calls.push_back(s); }

i32  m_packetStatus(i32) { return g_rec->packetStatusRet; }
i32  m_entity29(int a, HeRecord*) { g_rec->calls.push_back("e29:" + std::to_string(a)); return g_rec->entity29Ret; }
i32  m_free(HeRecord*) { g_rec->freeCalled = true; log("free"); return 0; }
int  m_count(int) { return g_rec->handlerCount; }
bool m_anyMatch(int, i32) { return g_rec->handlerMatch; }
void* m_findById(i32) { return g_rec->personById; }
void* m_query(i32) { return g_rec->queryBeginRet; }
void* m_resolve(i32) { return g_rec->resolveRet; }
void* m_queryFind(void*, int) { return g_rec->queryFindRet; }
void* m_active(void* r) { return r; }
void* m_family(void* r) { return r; }
i32  m_objId(void* r) { return r ? static_cast<FakeRec*>(r)->id : -1; }
u16  m_marker(void* r) { return r ? static_cast<FakeRec*>(r)->marker : 0; }
u8   m_kind(void* r) { return r ? static_cast<FakeRec*>(r)->kind : 0; }
u8   m_rank(void* r) { return r ? static_cast<FakeRec*>(r)->rank : 0; }
bool m_hasChar(void* r) { return r ? static_cast<FakeRec*>(r)->hasChar : false; }
void* m_cityPerson(u16) { return g_rec->personById; }
i32  m_cityId(u16) { return 555; }
void* m_cityAux(u16) { return g_rec->personById; }
i32  m_sumCur(void* r) { return r ? static_cast<FakeRec*>(r)->currency : 0; }
i32  m_curAmt(void* r) { return r ? static_cast<FakeRec*>(r)->currency : 0; }
i32  m_wealth(void* r) { return r ? static_cast<FakeRec*>(r)->wealth : 0; }
void m_req16(i32 f, i32 t, i32 a) { g_rec->calls.push_back("req16:" + std::to_string(f) + "," + std::to_string(t) + "," + std::to_string(a)); }
i32  m_distribute(i32, void*, i32 amt) { g_rec->calls.push_back("dist:" + std::to_string(amt)); return amt; }
void m_coord27(i32 a, i32 b, int d) { g_rec->calls.push_back("c27:" + std::to_string(a) + "," + std::to_string(b) + "," + std::to_string(d)); }
void m_op91(i32 id, int k) { g_rec->calls.push_back("op91:" + std::to_string(id) + "," + std::to_string(k)); }
void m_op71(i32 id) { g_rec->calls.push_back("op71:" + std::to_string(id)); }
void m_op77(i32 id) { g_rec->calls.push_back("op77:" + std::to_string(id)); }
void m_op90(int, i32) { log("op90"); }
void m_named53(i32, i32, i32, int flag, const char* n) { g_rec->calls.push_back(std::string("n53:") + n + "," + std::to_string(flag)); }
void m_slot28(int k, i32, i32, i32 amt) { g_rec->calls.push_back("slot28:" + std::to_string(k) + "," + std::to_string(amt)); }
void m_setField(void*, int off) { g_rec->calls.push_back("setM1:" + std::to_string(off)); }
void m_op84(const char* n, i32, i32) { g_rec->calls.push_back(std::string("op84:") + n); }
void m_msg(i32 t, int id) { g_rec->calls.push_back("msg:" + std::to_string(t) + "," + std::to_string(id)); }
void m_sample(int, const char* n) { g_rec->calls.push_back(std::string("snd:") + n); }
i32  m_panelCreate(HeRecord*) { log("panelCreate"); return 1; }
i32  m_panelDestroy(HeRecord*) { log("panelDestroy"); return 0; }
void m_rich(int id) { g_rec->calls.push_back("rich:" + std::to_string(id)); }
void* m_panelWindow(HeRecord*) { return g_rec->panelWin; }
bool m_formMatch(void*) { return g_rec->formMatch; }
int  m_formCode(void*) { return g_rec->formCode; }
void m_hwA(void*) { log("hwA"); }
void m_hwB(void*) { log("hwB"); }
void m_hwPair(void*, void*) { log("hwPair"); }
int  m_evalGroup(int n) { g_rec->calls.push_back("evalGrp:" + std::to_string(n)); return 0; }
u8   m_mapCat(int) { return 0; }
int  m_variant(int c) { return c * 10; }
u8   m_profCode(int) { return g_rec->professionCode; }
int  m_slotsWorth(i32) { return g_rec->slotsWorth; }
bool m_meister(int, void*) { return g_rec->meisterOk; }
int  m_rating(void*, int) { return g_rec->ratingRet; }
void m_apEvent(u16) { log("apEvent"); }
bool m_tol(void*, void*, float) { return g_rec->tolerance; }
int  m_rel(void*, void*) { return g_rec->relation; }
bool m_carry(void*, i32* a, i32* b) { *a = 11; *b = -1; return g_rec->carryTarget; }
bool m_invSlot(void*, int) { return g_rec->inventoryActive; }
void m_violation(int, i32, i32, i32) { log("violation"); }
int  m_groupCount(void*) { return 0; }
void* m_groupMember(void*, int) { return nullptr; }

NpcAction10Hooks MakeHooks() {
    NpcAction10Hooks h{};
    h.packetStatus = m_packetStatus; h.queueEntity29 = m_entity29; h.freeHandlerEntry = m_free;
    h.countHandlers = m_count; h.anyHandlerMatchesEntity = m_anyMatch;
    h.findPersonById = m_findById; h.personQueryBegin = m_query; h.resolveEntity = m_resolve;
    h.gameObjectQueryFind = m_queryFind; h.personFindActive = m_active; h.familyRecord = m_family;
    h.objId = m_objId; h.markerWord = m_marker; h.kind = m_kind; h.recRank = m_rank;
    h.hasCharacter = m_hasChar; h.cityPersonRecord = m_cityPerson; h.cityId = m_cityId; h.cityAuxRecord = m_cityAux;
    h.sumCurrencyHeld = m_sumCur; h.currencyAmount = m_curAmt; h.computeTotalWealth = m_wealth;
    h.queueRequest16 = m_req16; h.distributeCredit = m_distribute;
    h.requestCoord27 = m_coord27; h.requestBuildOp91 = m_op91; h.requestBuildOp71 = m_op71;
    h.requestBuildOp77 = m_op77; h.requestBuildOp90 = m_op90; h.requestNamedObject53 = m_named53;
    h.queueSlotReset28 = m_slot28; h.setEntityFieldM1 = m_setField; h.enqueueBuildOp84 = m_op84;
    h.sendMessage = m_msg; h.playSample = m_sample; h.eventPanelCreate = m_panelCreate;
    h.eventPanelDestroy = m_panelDestroy; h.renderRichString = m_rich; h.panelWindow = m_panelWindow;
    h.formEventMatches = m_formMatch; h.formEventCode = m_formCode;
    h.historyWanderA = m_hwA; h.historyWanderB = m_hwB; h.historyWanderPair = m_hwPair;
    h.evalGroupComposition = m_evalGroup; h.mapActionToCategory = m_mapCat;
    h.computeVariantIndex = m_variant; h.mapToProfessionCode = m_profCode;
    h.buildingSlotsWorth = m_slotsWorth; h.evalMeisterTarget = m_meister; h.buildingRating = m_rating;
    h.registerApEvent = m_apEvent; h.withinTolerance = m_tol; h.relationEntry = m_rel;
    h.pickCarryTarget = m_carry; h.inventorySlotActive = m_invSlot; h.evaluateViolation = m_violation;
    h.groupMemberCount = m_groupCount; h.groupMemberRecord = m_groupMember;
    return h;
}

// A zeroed He record (513 bytes) on the heap so the raw-offset accessors are valid.
struct HeBuf {
    alignas(8) unsigned char bytes[600];
    HeBuf() { std::memset(bytes, 0, sizeof(bytes)); }
    HeRecord* rec() { return reinterpret_cast<HeRecord*>(bytes); }
};

void Set(HeRecord* h, int off, i32 v) { *reinterpret_cast<i32*>(reinterpret_cast<u8*>(h) + off) = v; }
i32  Get(HeRecord* h, int off) { return *reinterpret_cast<i32*>(reinterpret_cast<u8*>(h) + off); }

} // namespace

// ===========================================================================
// Constants.
// ===========================================================================
TEST(NpcAction10_Const, RecoveredValues) {
    CHECK_EQ(kCreditChargeMul, 0.01);
    CHECK_EQ(kKidnapWealthCap, 1604000.0f);
    CHECK_EQ(kKidnapRansomR4, 0.1f);
    CHECK_EQ(kWanderSeedTable[0], 1);
    CHECK_EQ(kWanderSeedTable[7], 23);
    CHECK_EQ(kWanderSeedTable[8], 745);
    CHECK_EQ(kWanderSeedTable[15], 767);
}

// ===========================================================================
// RunCreditStep: packet gate + teardown.
// ===========================================================================
TEST(NpcAction10_RunCredit, PendingPacketIsNoOp) {
    Rec10 rec; g_rec = &rec; auto hk = MakeHooks(); SetNpcAction10Hooks(&hk);
    rec.packetStatusRet = 0;  // still pending
    HeBuf b; Set(b.rec(), 132, 99); Set(b.rec(), 112, 0);
    NpcAction10_RunCreditStep(b.rec());
    CHECK(!rec.freeCalled);
    CHECK(rec.calls.empty());
    SetNpcAction10Hooks(nullptr);
}

TEST(NpcAction10_RunCredit, StateMinus2Frees) {
    Rec10 rec; g_rec = &rec; auto hk = MakeHooks(); SetNpcAction10Hooks(&hk);
    HeBuf b; Set(b.rec(), 132, -1); Set(b.rec(), 112, -2);
    NpcAction10_RunCreditStep(b.rec());
    CHECK(rec.freeCalled);
    CHECK_EQ(Get(b.rec(), 132), -1);
    SetNpcAction10Hooks(nullptr);
}

TEST(NpcAction10_RunCredit, ChargeAffordableTransfers) {
    Rec10 rec; g_rec = &rec; auto hk = MakeHooks(); SetNpcAction10Hooks(&hk);
    FakeRec bldg; bldg.id = 7; bldg.marker = 0;
    FakeRec creditor; creditor.id = 9; creditor.currency = 1000000;
    rec.queryBeginRet = &bldg; rec.personById = &creditor;
    HeBuf b; Set(b.rec(), 132, -1); Set(b.rec(), 112, 0);
    *reinterpret_cast<u8*>(reinterpret_cast<u8*>(b.rec()) + 120) = 2;  // flag&2
    Set(b.rec(), 192, 100); Set(b.rec(), 180, 50); Set(b.rec(), 176, 9);
    Set(b.rec(), 188, 5);   // retry counter high so no free
    NpcAction10_RunCreditStep(b.rec());
    // charge = 100 * 0.01 * 50 = 50.
    bool sawReq16 = false;
    for (auto& c : rec.calls) if (c.rfind("req16:", 0) == 0) sawReq16 = true;
    CHECK(sawReq16);
    CHECK(!rec.freeCalled);
    CHECK_EQ(Get(b.rec(), 188), 4);
    SetNpcAction10Hooks(nullptr);
}

// ===========================================================================
// MasterExamState: pass-roll fork.
// ===========================================================================
TEST(NpcAction10_MasterExam, State2PassEmitsApplause) {
    Rec10 rec; g_rec = &rec; auto hk = MakeHooks(); SetNpcAction10Hooks(&hk);
    FakeRec p; p.id = 1; p.kind = 6; rec.personById = &p; rec.ratingRet = 150; // 1.5 -> pass
    HeBuf b; Set(b.rec(), 112, 2);
    i32 r = NpcAction10_MasterExamState(b.rec());
    (void)r;
    bool sawMsg4983 = false;
    for (auto& c : rec.calls) if (c == "msg:1,4983") sawMsg4983 = true;
    CHECK(sawMsg4983);
    CHECK(rec.freeCalled);
    SetNpcAction10Hooks(nullptr);
}

TEST(NpcAction10_MasterExam, MissingExaminerFrees) {
    Rec10 rec; g_rec = &rec; auto hk = MakeHooks(); SetNpcAction10Hooks(&hk);
    rec.personById = nullptr;  // findPersonById -> null
    HeBuf b; Set(b.rec(), 112, 1);
    NpcAction10_MasterExamState(b.rec());
    CHECK(rec.freeCalled);
    SetNpcAction10Hooks(nullptr);
}

// ===========================================================================
// KidnapCarryStep: ransom factor selection.
// ===========================================================================
TEST(NpcAction10_Kidnap, Rank4RansomSlotReset) {
    Rec10 rec; g_rec = &rec; auto hk = MakeHooks(); SetNpcAction10Hooks(&hk);
    FakeRec victim; victim.id = 3; victim.rank = 4; victim.wealth = 1000000;
    FakeRec captor; captor.id = 8;
    rec.personById = &victim; rec.queryBeginRet = &captor;
    HeBuf b; Set(b.rec(), 112, 0);
    *reinterpret_cast<u8*>(reinterpret_cast<u8*>(b.rec()) + 120) = 2;
    NpcAction10_KidnapCarryStep(b.rec());
    // ransom = min(1000000,1604000)*0.1 = 100000.
    bool saw = false;
    for (auto& c : rec.calls) if (c == "slot28:62,100000") saw = true;
    CHECK(saw);
    SetNpcAction10Hooks(nullptr);
}

// ===========================================================================
// FireSpreadStep: ignite SFX then advance state.
// ===========================================================================
TEST(NpcAction10_Fire, State0PlaysBeginAndAdvances) {
    Rec10 rec; g_rec = &rec; auto hk = MakeHooks(); SetNpcAction10Hooks(&hk);
    HeBuf b; Set(b.rec(), 112, 0);
    NpcAction10_FireSpreadStep(b.rec());
    CHECK_EQ(Get(b.rec(), 112), 1);
    bool saw = false; for (auto& c : rec.calls) if (c == "snd:Brand_Beginn") saw = true;
    CHECK(saw);
    SetNpcAction10Hooks(nullptr);
}

TEST(NpcAction10_Fire, State3NoSiblingExtinguishes) {
    Rec10 rec; g_rec = &rec; auto hk = MakeHooks(); SetNpcAction10Hooks(&hk);
    rec.handlerMatch = false;
    HeBuf b; Set(b.rec(), 112, 3);
    NpcAction10_FireSpreadStep(b.rec());
    bool saw = false; for (auto& c : rec.calls) if (c == "snd:Brand_Ende") saw = true;
    CHECK(saw);
    CHECK(rec.freeCalled);
    SetNpcAction10Hooks(nullptr);
}

// ===========================================================================
// EvaluateAssignProfession: gate failures.
// ===========================================================================
TEST(NpcAction10_AssignProf, WrongKindRejected) {
    Rec10 rec; g_rec = &rec; auto hk = MakeHooks(); SetNpcAction10Hooks(&hk);
    FakeRec p; p.kind = 6;  // not a master (kind 5)
    i32 out[8] = {0}; i32 extra[8] = {0};
    u8 r = NpcAction10_EvaluateAssignProfession(0, &p, out, 0, extra);
    CHECK_EQ((int)r, 0);
    SetNpcAction10Hooks(nullptr);
}

TEST(NpcAction10_AssignProf, LowCashRejected) {
    Rec10 rec; g_rec = &rec; auto hk = MakeHooks(); SetNpcAction10Hooks(&hk);
    FakeRec p; p.kind = 5; p.currency = 1000;  // 1000>>5 = 31 < 5000
    i32 out[8] = {0}; i32 extra[8] = {0};
    u8 r = NpcAction10_EvaluateAssignProfession(0, &p, out, 0, extra);
    CHECK_EQ((int)r, 0);
    SetNpcAction10Hooks(nullptr);
}

// ===========================================================================
// StartWanderSearchState: flag gate + >4 handler abort.
// ===========================================================================
TEST(NpcAction10_Wander, Flag4GatesOut) {
    Rec10 rec; g_rec = &rec; auto hk = MakeHooks(); SetNpcAction10Hooks(&hk);
    HeBuf b; *reinterpret_cast<u8*>(reinterpret_cast<u8*>(b.rec()) + 120) = 4;
    i32 r = NpcAction10_StartWanderSearchState(b.rec());
    CHECK_EQ(r, (i32)reinterpret_cast<intptr_t>(b.rec()));
    CHECK(rec.calls.empty());
    SetNpcAction10Hooks(nullptr);
}

TEST(NpcAction10_Wander, TooManyHandlersAborts) {
    Rec10 rec; g_rec = &rec; auto hk = MakeHooks(); SetNpcAction10Hooks(&hk);
    FakeRec w; w.id = 2; rec.personById = &w;
    rec.handlerCount = 5;  // > 4 -> abort
    HeBuf b; Set(b.rec(), 188, 2);
    NpcAction10_StartWanderSearchState(b.rec());
    bool sawHwA = false; for (auto& c : rec.calls) if (c == "hwA") sawHwA = true;
    CHECK(sawHwA);
    CHECK_EQ(Get(b.rec(), 132), rec.entity29Ret);
    SetNpcAction10Hooks(nullptr);
}

// ===========================================================================
// DismissApprenticeStep: relation gate.
// ===========================================================================
TEST(NpcAction10_Dismiss, GoodRelationKeepsApprentice) {
    Rec10 rec; g_rec = &rec; auto hk = MakeHooks(); SetNpcAction10Hooks(&hk);
    FakeRec appr; appr.id = 4; appr.hasChar = true; rec.personById = &appr;
    rec.relation = 0;  // > -26 -> keep (free without ops)
    HeBuf b; Set(b.rec(), 112, 0);
    NpcAction10_DismissApprenticeStep(b.rec());
    CHECK(rec.freeCalled);
    bool sawOp71 = false; for (auto& c : rec.calls) if (c.rfind("op71", 0) == 0) sawOp71 = true;
    CHECK(!sawOp71);
    SetNpcAction10Hooks(nullptr);
}

TEST(NpcAction10_Dismiss, BadRelationDismisses) {
    Rec10 rec; g_rec = &rec; auto hk = MakeHooks(); SetNpcAction10Hooks(&hk);
    FakeRec appr; appr.id = 4; appr.hasChar = true; rec.personById = &appr;
    rec.relation = -30;  // <= -26 -> dismiss
    HeBuf b; Set(b.rec(), 112, 0);
    NpcAction10_DismissApprenticeStep(b.rec());
    CHECK(rec.freeCalled);
    bool sawOp71 = false, sawOp77 = false;
    for (auto& c : rec.calls) { if (c == "op71:4") sawOp71 = true; if (c == "op77:4") sawOp77 = true; }
    CHECK(sawOp71); CHECK(sawOp77);
    SetNpcAction10Hooks(nullptr);
}

// ===========================================================================
// DetachFromGroupState: clears state then needs flag&2.
// ===========================================================================
TEST(NpcAction10_Detach, NoFlagJustClearsState) {
    Rec10 rec; g_rec = &rec; auto hk = MakeHooks(); SetNpcAction10Hooks(&hk);
    HeBuf b; Set(b.rec(), 112, 9);
    i32 r = NpcAction10_DetachFromGroupState(b.rec());
    CHECK_EQ(Get(b.rec(), 112), 0);
    CHECK_EQ(r, (i32)reinterpret_cast<intptr_t>(b.rec()));
    CHECK(rec.calls.empty());
    SetNpcAction10Hooks(nullptr);
}

// ===========================================================================
// Inert defaults: every machine must run with no hooks installed and not crash.
// ===========================================================================
TEST(NpcAction10_Inert, AllMachinesSafeWithoutHooks) {
    SetNpcAction10Hooks(nullptr);
    HeBuf b;
    Set(b.rec(), 112, 0); NpcAction10_RunCreditStep(b.rec());
    Set(b.rec(), 112, 2); (void)NpcAction10_MasterExamState(b.rec());
    Set(b.rec(), 112, 0); (void)NpcAction10_KidnapCarryStep(b.rec());
    Set(b.rec(), 112, 0); NpcAction10_FireSpreadStep(b.rec());
    Set(b.rec(), 112, 0); (void)NpcAction10_EvaluateGroupCompositionState(b.rec());
    Set(b.rec(), 112, 0); (void)NpcAction10_TavernSocializeState(b.rec());
    Set(b.rec(), 112, 0); (void)NpcAction10_MasterExamPayStep(b.rec());
    Set(b.rec(), 112, 0); (void)NpcAction10_StartWanderSearchState(b.rec());
    Set(b.rec(), 112, 0); (void)NpcAction10_GatherFollowersStep(b.rec());
    Set(b.rec(), 112, 0); (void)NpcAction10_DetachFromGroupState(b.rec());
    Set(b.rec(), 112, 0); (void)NpcAction10_DismissApprenticeStep(b.rec());
    i32 o[8]={0}, e[8]={0};
    (void)NpcAction10_EvaluateAssignProfession(0, nullptr, o, 0, e);
    CHECK(true);
}

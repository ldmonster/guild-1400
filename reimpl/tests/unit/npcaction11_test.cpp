// Unit tests for sim/npcaction11 — the eleventh NpcAction batch. Each test
// installs a recording NpcAction11Hooks mock so the deterministic control flow
// (state transitions, RNG/clock-driven appointment math, command emissions) is
// observable, and asserts the 1:1 behaviour against golden values computed by
// hand / python.
#include "test.h"

#include "sim/npcaction11.h"
#include "sim/npcaction.h"   // SetNpcClock / NpcClock
#include "sim/gametime.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// ---------------------------------------------------------------------------
// A generously sized He record (the steps reach +485).
// ---------------------------------------------------------------------------
struct HeBuf {
    alignas(8) std::uint8_t bytes[1024];
    HeBuf() { std::memset(bytes, 0, sizeof bytes); }
    HeRecord* rec() { return reinterpret_cast<HeRecord*>(bytes); }
    void put32(int off, i32 v) { std::memcpy(bytes + off, &v, 4); }
    void put16(int off, u16 v) { std::memcpy(bytes + off, &v, 2); }
    void put8(int off, u8 v)   { bytes[off] = v; }
    i32 get32(int off) { i32 v; std::memcpy(&v, bytes + off, 4); return v; }
};

// ---------------------------------------------------------------------------
// Opaque person/building records the mock returns. Field reads are keyed off the
// record handle (a small struct), never off raw pointer arithmetic.
// ---------------------------------------------------------------------------
struct Rec {
    i32 id = 0;
    u16 marker = 0;
    u8  kind = 0;
    u8  rank = 0;
    u8  equip = 0;
    i32 f101 = -1;
    i32 f364 = 0;
    i32 f92 = 0;
    i32 dur = 0;
    i32 sp0 = 0, sp1 = 0;
    u8  b8 = 0, b356 = 1, b485 = 0, b484 = 0;
};

// ---------------------------------------------------------------------------
// Recording state shared by the mocks.
// ---------------------------------------------------------------------------
struct Recorder {
    int frees = 0;
    int args25 = 0; i32 a25id = -1; int a25a = -1, a25b = -1, a25c = -1, a25d = -1;
    int coord27 = 0; i32 cFrom = -1, cTo = -1; int cDelta = 0;
    int single58 = 0;
    int quickjumps = 0; int lastQjText = -1; i32 lastQjObj = -1;
    int entityMsgs = 0; int lastEntText = -1; i32 lastEntId = -1;
    int buildOp71 = 0, buildOp72 = 0, buildOp77 = 0, named53 = 0;
    int slotReset28 = 0;
    int actionStart = 0, actionEnd = 0;
    int cmd15 = 0; i32 cmd15Amt = -1; i32 cmd15A = 0;
    int request17 = 0;
    int rangedAttacks = 0;
    int state22 = 0, state23 = 0, beginDelta = 0, appendRaw = 0, appendCopied = 0;
} g_rec;

bool g_fast = false;
u16  g_nextRand = 0;
int  g_moneyRate = 1;

// Person store: index by id.
std::vector<Rec> g_recs;
Rec* RecById(i32 id) {
    for (auto& r : g_recs) if (r.id == id) return &r;
    return nullptr;
}

// --- hook implementations ---------------------------------------------------
i32  Free(HeRecord*) { ++g_rec.frees; return 0; }
void* QueryBegin(i32 id) { return RecById(id); }
void* FindById(i32 id)   { return RecById(id); }
void* BuildingFindById(i32 id) { return RecById(id); }

i32  ObjId(void* r)  { return r ? static_cast<Rec*>(r)->id : 0; }
u16  Marker(void* r) { return r ? static_cast<Rec*>(r)->marker : 0; }
u8   Kind(void* r)   { return r ? static_cast<Rec*>(r)->kind : 0; }
u8   Rank(void* r)   { return r ? static_cast<Rec*>(r)->rank : 0; }
u8   Equip(void* r)  { return r ? static_cast<Rec*>(r)->equip : 0; }
void SetEquip(void* r, u8 v) { if (r) static_cast<Rec*>(r)->equip = v; }
i32  F101(void* r)   { return r ? static_cast<Rec*>(r)->f101 : -1; }
i32  F364(void* r)   { return r ? static_cast<Rec*>(r)->f364 : 0; }
i32  F92(void* r)    { return r ? static_cast<Rec*>(r)->f92 : 0; }
i32  FamilyDur(void* r, int) { return r ? static_cast<Rec*>(r)->dur : 0; }
i32  SpouseId(void* r, int w) {
    if (!r) return 0;
    return w == 0 ? static_cast<Rec*>(r)->sp0 : static_cast<Rec*>(r)->sp1;
}
u8 EquipState(void* r, int idx) {
    if (!r) return 0;
    Rec* p = static_cast<Rec*>(r);
    switch (idx) { case 8: return p->b8; case 356: return p->b356;
                   case 484: return p->b484; case 485: return p->b485; default: return 0; }
}

i32  CityId(u16 ci)   { return 1000 + ci; }
u16  CityMarker(u16)  { return 0; }
u8   CityKind(u16 ci) { return ci == 0 ? 6 : (ci == 1 ? 7 : 5); }
void* CityPersonRec(u16) { return nullptr; }

void Args25(i32 id, int a, int b, int c, int d) {
    ++g_rec.args25; g_rec.a25id = id; g_rec.a25a = a; g_rec.a25b = b; g_rec.a25c = c; g_rec.a25d = d;
}
void Request17(i32, i32, int, int, u8, int) { ++g_rec.request17; }
void Single58(i32) { ++g_rec.single58; }
void Coord27(i32 a, i32 b, int d) { ++g_rec.coord27; g_rec.cFrom = a; g_rec.cTo = b; g_rec.cDelta = d; }
void SlotReset28(void*) { ++g_rec.slotReset28; }
void BuildOp71(i32, int, int, int) { ++g_rec.buildOp71; }
void BuildOp72(i32, int) { ++g_rec.buildOp72; }
void BuildOp77(i32) { ++g_rec.buildOp77; }
void Named53(i32, i32, i32, int, const char*) { ++g_rec.named53; }
void State23() { ++g_rec.state23; }
void State22() { ++g_rec.state22; }
void BeginDelta(void*, i32) { ++g_rec.beginDelta; }
void AppendCopied(void*, int) { ++g_rec.appendCopied; }
void AppendRaw(void*, int, i32) { ++g_rec.appendRaw; }
void ActionStart(const char*) { ++g_rec.actionStart; }
void ActionEnd() { ++g_rec.actionEnd; }
void Cmd15(i32 a, i32, i32 amt, u8) { ++g_rec.cmd15; g_rec.cmd15A = a; g_rec.cmd15Amt = amt; }
i32  SumWorth(int) { return 200; }
bool LoadGraphic(void*, void*) { return true; }

void SendEntity(i32 id, int text) { ++g_rec.entityMsgs; g_rec.lastEntId = id; g_rec.lastEntText = text; }
void Quickjump(i32, int text, i32 obj, const char*) {
    ++g_rec.quickjumps; g_rec.lastQjText = text; g_rec.lastQjObj = obj;
}
i32  MoneyRate(i32, u8) { return g_moneyRate; }
void Mood(void*, i32) {}
u16  RandMod(u16) { return g_nextRand; }
bool TryRanged(void*, void*) { ++g_rec.rangedAttacks; return true; }
bool FastMode() { return g_fast; }

void* FindConflict(HeRecord*, int, i32, i32) { return nullptr; }
void* GoQueryFind(i32, int, int, int, int) { return nullptr; }
void  Resolve(i32 id, void** out) { *out = RecById(id); }
void* RunWindow(int) { return nullptr; }
i32   RelLookup(void*, void*) { return 0; }
void  Violation(int, i32, i32, i32, i32) {}
void* OfficeStorage(int, HeRecord*) { return reinterpret_cast<void*>(1); }

NpcAction11Hooks MakeHooks() {
    NpcAction11Hooks h{};
    h.freeHandlerEntry = Free;
    h.findConflictingHandler = FindConflict;
    h.findPersonById = FindById;
    h.personQueryBegin = QueryBegin;
    h.buildingFindById = BuildingFindById;
    h.buildingFindOfficeStorage = OfficeStorage;
    h.resolveEntity = Resolve;
    h.gameObjectQueryFind = GoQueryFind;
    h.objId = ObjId; h.markerWord = Marker; h.kind = Kind; h.rank = Rank;
    h.equipFlags = Equip; h.setEquipFlags = SetEquip;
    h.field101 = F101; h.field364 = F364; h.field92 = F92;
    h.familyDur = FamilyDur; h.spouseId = SpouseId; h.personEquipState = EquipState;
    h.cityId = CityId; h.cityMarker = CityMarker; h.cityKind = CityKind;
    h.cityPersonRecord = CityPersonRec;
    h.requestArgs25 = Args25; h.request17 = Request17; h.requestSingle58 = Single58;
    h.requestCoord27 = Coord27; h.requestSlotReset28 = SlotReset28;
    h.requestBuildOp71 = BuildOp71; h.requestBuildOp72 = BuildOp72;
    h.requestBuildOp77 = BuildOp77; h.requestNamedObject53 = Named53;
    h.requestState23 = State23; h.requestState22 = State22;
    h.beginDelta = BeginDelta; h.appendCopiedField = AppendCopied; h.appendRawField = AppendRaw;
    h.buildingActionStart = ActionStart; h.buildingActionEnd = ActionEnd;
    h.enqueueCmd15 = Cmd15; h.buildingSumFlaggedSlotsWorth = SumWorth;
    h.aiLoadBuildingGraphic = LoadGraphic;
    h.sendEntity = SendEntity; h.sendQuickjump = Quickjump;
    h.runOfficeOverviewWindow = RunWindow; h.relationLookup = RelLookup;
    h.adjustMood = Mood; h.evaluateViolation = Violation;
    h.moneyMultiplyByRate = MoneyRate; h.randomModulo = RandMod;
    h.tryRangedAttack = TryRanged; h.fastMode = FastMode;
    return h;
}

void ResetAll() {
    g_rec = Recorder{};
    g_recs.clear();
    g_fast = false;
    g_nextRand = 0;
    g_moneyRate = 1;
    GameTime t{};
    t.day = 100; t.hour = 8; t.minute = 0; t.second = 0;
    SetNpcClock(t);
}

} // namespace

// ===========================================================================
// BeginStoreObject — the simplest launcher: query + arg25(1024) + clock stamp.
// ===========================================================================
TEST(NpcAction11Unit, BeginStoreObjectEmitsArg25AndStamps) {
    ResetAll();
    g_recs = { Rec{} }; g_recs[0].id = 0xAB;
    NpcAction11Hooks h = MakeHooks(); SetNpcAction11Hooks(&h);

    HeBuf he; he.put32(172, 0xAB);
    void* ret = NpcAction11_BeginStoreObject(he.rec());

    CHECK(ret == &g_recs[0]);
    CHECK_EQ(g_rec.args25, 1);
    CHECK_EQ(g_rec.a25id, 0xAB);
    CHECK_EQ(g_rec.a25b, 1024);
    // +82 stamped with the clock (day 100).
    GameTime st; std::memcpy(&st, he.bytes + 82, sizeof st);
    CHECK_EQ(st.day, 100);
    CHECK_EQ(static_cast<int>(st.hour), 8);
}

TEST(NpcAction11Unit, BeginStoreObjectNoPersonNoCommand) {
    ResetAll();
    NpcAction11Hooks h = MakeHooks(); SetNpcAction11Hooks(&h);
    HeBuf he; he.put32(172, 0x999);  // no such record
    void* ret = NpcAction11_BeginStoreObject(he.rec());
    CHECK(ret == nullptr);
    CHECK_EQ(g_rec.args25, 0);
}

// ===========================================================================
// BeginUnequipObject — RandomModulo(10)+10 minute appointment + arg25(512).
// Golden: clock 08:00 + (rand 3 + 10) = 13 min -> 08:13.
// ===========================================================================
TEST(NpcAction11Unit, BeginUnequipObjectAdvancesByRandPlus10) {
    ResetAll();
    g_recs = { Rec{} }; g_recs[0].id = 0x42;
    g_nextRand = 3;
    NpcAction11Hooks h = MakeHooks(); SetNpcAction11Hooks(&h);

    HeBuf he; he.put32(172, 0x42);
    NpcAction11_BeginUnequipObject(he.rec());

    CHECK_EQ(g_rec.args25, 1);
    CHECK_EQ(g_rec.a25b, 512);
    GameTime st; std::memcpy(&st, he.bytes + 82, sizeof st);
    CHECK_EQ(static_cast<int>(st.hour), 8);
    CHECK_EQ(st.minute, 13);     // 0 + (3 + 10)
}

// ===========================================================================
// BeginEquipObject — fast-mode 0/0 vs apprentice-span hours; +90 equip bit set.
// ===========================================================================
TEST(NpcAction11Unit, BeginEquipObjectFastModeNoAdvanceSetsBit) {
    ResetAll();
    g_recs = { Rec{} }; g_recs[0].id = 7; g_recs[0].equip = 0;
    g_fast = true;
    NpcAction11Hooks h = MakeHooks(); SetNpcAction11Hooks(&h);

    HeBuf he; he.put32(172, 7);
    NpcAction11_BeginEquipObject(he.rec());

    CHECK_EQ(static_cast<int>(g_recs[0].equip), 0x40);  // equip bit set
    CHECK_EQ(g_rec.args25, 1);
    CHECK_EQ(g_rec.a25b, 64);
    CHECK_EQ(he.get32(16), 7);    // entity id recorded at +16
    // fast mode: +82 stays at the stamped clock (no minutes/days added).
    GameTime st; std::memcpy(&st, he.bytes + 82, sizeof st);
    CHECK_EQ(static_cast<int>(st.hour), 8);
    CHECK_EQ(st.minute, 0);
}

TEST(NpcAction11Unit, BeginEquipObjectAlreadyEquippedFrees) {
    ResetAll();
    g_recs = { Rec{} }; g_recs[0].id = 7; g_recs[0].equip = 0x40;
    NpcAction11Hooks h = MakeHooks(); SetNpcAction11Hooks(&h);
    HeBuf he; he.put32(172, 7);
    NpcAction11_BeginEquipObject(he.rec());
    CHECK_EQ(g_rec.frees, 1);
    CHECK_EQ(g_rec.args25, 0);    // no command when already equipped
}

// ===========================================================================
// DecrementCarryStep — counter fork. +184=1 -> first pass decrements to 0 (>=0),
// no completion. Second pass -> -1, host kind -> slot-reset + buildop72 + free.
// ===========================================================================
TEST(NpcAction11Unit, DecrementCarryStepCompletesWhenNegative) {
    ResetAll();
    g_recs = { Rec{} }; g_recs[0].id = 9; g_recs[0].kind = 6; g_recs[0].b356 = 3;
    NpcAction11Hooks h = MakeHooks(); SetNpcAction11Hooks(&h);

    HeBuf he; He_State(he.rec()) = 0; he.put32(172, 9); he.put32(184, 1);
    // pass 1: 1 -> 0, not negative.
    NpcAction11_DecrementCarryStep(he.rec());
    CHECK_EQ(he.get32(184), 0);
    CHECK_EQ(g_rec.frees, 0);
    // pass 2: 0 -> -1, host kind 6 -> slotReset + buildop72(amount 3-1=2) + free.
    NpcAction11_DecrementCarryStep(he.rec());
    CHECK_EQ(he.get32(184), -1);
    CHECK_EQ(g_rec.slotReset28, 1);
    CHECK_EQ(g_rec.buildOp72, 1);
    CHECK_EQ(g_rec.frees, 1);
}

TEST(NpcAction11Unit, DecrementCarryStepStateMinus2Frees) {
    ResetAll();
    NpcAction11Hooks h = MakeHooks(); SetNpcAction11Hooks(&h);
    HeBuf he; He_State(he.rec()) = -2;
    NpcAction11_DecrementCarryStep(he.rec());
    CHECK_EQ(g_rec.frees, 1);
}

// ===========================================================================
// CheckTargetBusyState — busy target rank aborts; clean target advances +1s.
// ===========================================================================
TEST(NpcAction11Unit, CheckTargetBusyAbortsOnRank) {
    ResetAll();
    g_recs = { Rec{} }; g_recs[0].id = 5; g_recs[0].rank = 1; g_recs[0].f101 = -1;
    NpcAction11Hooks h = MakeHooks(); SetNpcAction11Hooks(&h);
    HeBuf he; He_State(he.rec()) = 0; he.put32(172, 5); he.put32(180, 0x999);
    NpcAction11_CheckTargetBusyState(he.rec());
    CHECK_EQ(He_State(he.rec()), -1);   // aborted (rank busy)
    // clock advanced by 1 second on +82.
    GameTime st; std::memcpy(&st, he.bytes + 82, sizeof st);
    CHECK_EQ(st.second, 1);
}

TEST(NpcAction11Unit, CheckTargetBusyCleanKeepsState) {
    ResetAll();
    g_recs = { Rec{} }; g_recs[0].id = 5; g_recs[0].rank = 0;
    NpcAction11Hooks h = MakeHooks(); SetNpcAction11Hooks(&h);
    HeBuf he; He_State(he.rec()) = 0; he.put32(172, 5); he.put32(180, 0x999);
    NpcAction11_CheckTargetBusyState(he.rec());
    CHECK_EQ(He_State(he.rec()), 0);    // not aborted
}

// ===========================================================================
// DismissStaffStep — +8 set & +364 link -> delta-clear + recall + dismiss cmds.
// ===========================================================================
TEST(NpcAction11Unit, DismissStaffStepFullPath) {
    ResetAll();
    g_recs = { Rec{}, Rec{} };
    g_recs[0].id = 11; g_recs[0].b8 = 1; g_recs[0].f364 = 22;
    g_recs[1].id = 22; g_recs[1].marker = 0;
    NpcAction11Hooks h = MakeHooks(); SetNpcAction11Hooks(&h);
    HeBuf he; He_State(he.rec()) = 0; he.put32(172, 11);
    NpcAction11_DismissStaffStep(he.rec());
    CHECK_EQ(g_rec.beginDelta, 1);
    CHECK_EQ(g_rec.appendCopied, 1);
    CHECK_EQ(g_rec.state23, 1);
    CHECK_EQ(g_rec.coord27, 1);
    CHECK_EQ(g_rec.cDelta, -25);
    CHECK_EQ(g_rec.buildOp71, 1);
    CHECK_EQ(g_rec.buildOp77, 1);
    CHECK_EQ(g_rec.named53, 1);
    CHECK_EQ(g_rec.frees, 1);
}

TEST(NpcAction11Unit, DismissStaffStepInactiveAborts) {
    ResetAll();
    g_recs = { Rec{} }; g_recs[0].id = 11; g_recs[0].b8 = 0;
    NpcAction11Hooks h = MakeHooks(); SetNpcAction11Hooks(&h);
    HeBuf he; He_State(he.rec()) = 0; he.put32(172, 11);
    NpcAction11_DismissStaffStep(he.rec());
    CHECK_EQ(He_State(he.rec()), -1);
    CHECK_EQ(g_rec.beginDelta, 0);
}

// ===========================================================================
// DropObjectStep — state 0 host (kind 6) emits the host quickjump + peer sweep.
// ===========================================================================
TEST(NpcAction11Unit, DropObjectStepHostBroadcastsAndFrees) {
    ResetAll();
    g_recs = { Rec{} }; g_recs[0].id = 3;
    NpcAction11Hooks h = MakeHooks(); SetNpcAction11Hooks(&h);
    HeBuf he; He_State(he.rec()) = 0; he.put32(172, 3);
    he.put16(8, 0);   // city index 0 -> kind 6 (host)
    NpcAction11_DropObjectStep(he.rec());
    // host (kind 6) -> 5114 message, then peer sweep over hosts (ci 0 self excluded,
    // ci 1 kind 7 -> 6207). So at least 2 quickjumps.
    CHECK(g_rec.quickjumps >= 2);
    CHECK_EQ(g_rec.single58, 1);
    CHECK_EQ(g_rec.args25, 1);
    CHECK_EQ(g_rec.frees, 1);
}

TEST(NpcAction11Unit, DropObjectStepStateMinus1Frees) {
    ResetAll();
    g_recs = { Rec{} }; g_recs[0].id = 3;
    NpcAction11Hooks h = MakeHooks(); SetNpcAction11Hooks(&h);
    HeBuf he; He_State(he.rec()) = -1; he.put32(172, 3);
    NpcAction11_DropObjectStep(he.rec());
    CHECK_EQ(g_rec.args25, 1);   // arg25(90,0,2,64)
    CHECK_EQ(g_rec.a25d, 64);
    CHECK_EQ(g_rec.frees, 1);
}

// ===========================================================================
// PickupObjectStep — re-arm while +176>0, finish when it drains.
// Golden: clock at day 100 08:00, +96 stamped at 07:00 -> elapsed 60 min,
// amt = 60*0.1 + 0.5 = 6.5 -> 6. +176 = 10 -> 10-6=4 (>0) re-arm (+10m).
// ===========================================================================
TEST(NpcAction11Unit, PickupObjectStepReArmsWhilePositive) {
    ResetAll();
    g_recs = { Rec{} }; g_recs[0].id = 4; g_recs[0].kind = 6;
    NpcAction11Hooks h = MakeHooks(); SetNpcAction11Hooks(&h);
    HeBuf he; He_State(he.rec()) = 0; he.put32(172, 4);
    he.put16(8, 0);
    // stamp +96 at 07:00 (1h before NpcClock 08:00) -> elapsed 60.
    GameTime past{}; past.day = 100; past.hour = 7; past.minute = 0; past.second = 0;
    std::memcpy(he.bytes + 96, &past, sizeof past);
    he.put32(176, 10);
    NpcAction11_PickupObjectStep(he.rec());
    // 10 - (int)(60*0.1+0.5)=10-6=4 (>0) -> re-arm, state still 0.
    CHECK_EQ(he.get32(176), 4);
    CHECK_EQ(He_State(he.rec()), 0);
    // +82 advanced by 10 minutes from clock 08:00.
    GameTime st; std::memcpy(&st, he.bytes + 82, sizeof st);
    CHECK_EQ(st.minute, 10);
}

TEST(NpcAction11Unit, PickupObjectStepFinishesWhenDrained) {
    ResetAll();
    g_recs = { Rec{} }; g_recs[0].id = 4; g_recs[0].kind = 6;
    NpcAction11Hooks h = MakeHooks(); SetNpcAction11Hooks(&h);
    HeBuf he; He_State(he.rec()) = 0; he.put32(172, 4);
    he.put16(8, 0);
    GameTime past{}; past.day = 100; past.hour = 7; past.minute = 0; past.second = 0;
    std::memcpy(he.bytes + 96, &past, sizeof past);
    he.put32(176, 6);     // 6 - 6 = 0 -> finish
    NpcAction11_PickupObjectStep(he.rec());
    CHECK_EQ(He_State(he.rec()), -1);   // finished
    CHECK(g_rec.quickjumps >= 1);       // host kind 6 -> 5086
}

// ===========================================================================
// UseObjectStep — phase switch (state+2). state 2 -> request17 + arg25, advance.
// ===========================================================================
TEST(NpcAction11Unit, UseObjectStepPhase2EmitsAndAdvances) {
    ResetAll();
    g_recs = { Rec{} }; g_recs[0].id = 8;
    NpcAction11Hooks h = MakeHooks(); SetNpcAction11Hooks(&h);
    HeBuf he; He_State(he.rec()) = 0; he.put32(172, 8);   // state 0 -> sw 2
    NpcAction11_UseObjectStep(he.rec());
    CHECK_EQ(g_rec.request17, 1);
    CHECK_EQ(g_rec.args25, 1);
    CHECK_EQ(He_State(he.rec()), 1);   // advanced
}

TEST(NpcAction11Unit, UseObjectStepPhase0Frees) {
    ResetAll();
    g_recs = { Rec{} }; g_recs[0].id = 8;
    NpcAction11Hooks h = MakeHooks(); SetNpcAction11Hooks(&h);
    HeBuf he; He_State(he.rec()) = -2; he.put32(172, 8);  // sw 0
    NpcAction11_UseObjectStep(he.rec());
    CHECK_EQ(g_rec.args25, 1);
    CHECK_EQ(g_rec.a25d, 128);
    CHECK_EQ(g_rec.frees, 1);
}

// ===========================================================================
// EvaluateUseBack — relFlag short-circuit; cooldown gate; AI/ranged paths.
// ===========================================================================
TEST(NpcAction11Unit, EvaluateUseBackRelFlagReturnsZero) {
    ResetAll();
    Rec p; p.id = 1;
    NpcAction11Hooks h = MakeHooks(); SetNpcAction11Hooks(&h);
    u8 outA[24] = {0}, outB[24] = {0};
    CHECK_EQ(static_cast<int>(NpcAction11_EvaluateUseBack(&p, outA, 1, outB)), 0);
}

TEST(NpcAction11Unit, EvaluateUseBackCooldownGateBlocks) {
    ResetAll();
    Rec p; p.id = 1; p.b485 = 2;   // cooldown bit set
    g_nextRand = 5;                // RandomModulo(8) != 0 -> blocked
    NpcAction11Hooks h = MakeHooks(); SetNpcAction11Hooks(&h);
    u8 outA[24] = {0}, outB[24] = {0};
    CHECK_EQ(static_cast<int>(NpcAction11_EvaluateUseBack(&p, outA, 0, outB)), 0);
    CHECK_EQ(g_rec.rangedAttacks, 0);
}

TEST(NpcAction11Unit, EvaluateUseBackVehicleRangedAttack) {
    ResetAll();
    Rec p; p.id = 1; p.kind = 3; p.b484 = 4;   // vehicle with mounted weapon
    NpcAction11Hooks h = MakeHooks(); SetNpcAction11Hooks(&h);
    u8 outA[24] = {0}, outB[24] = {0};
    CHECK_EQ(static_cast<int>(NpcAction11_EvaluateUseBack(&p, outA, 0, outB)), 42);
    CHECK_EQ(g_rec.rangedAttacks, 1);
}

// ===========================================================================
// BeginFriendship / BeginDivorce — relation launchers (non-host resolve path).
// ===========================================================================
TEST(NpcAction11Unit, BeginFriendshipNonHostNotifies) {
    ResetAll();
    g_recs = { Rec{}, Rec{} };
    g_recs[0].id = 100; g_recs[0].kind = 5;        // self non-host
    g_recs[1].id = 200; g_recs[1].kind = 6;        // target host
    Rec ctx{}; ctx.sp0 = 200;
    NpcAction11Hooks h = MakeHooks(); SetNpcAction11Hooks(&h);
    CHECK_EQ(NpcAction11_BeginFriendship(&g_recs[0], &ctx), 1);
    CHECK_EQ(g_rec.coord27, 1);
    CHECK_EQ(g_rec.cDelta, 25);
    CHECK_EQ(g_rec.entityMsgs, 1);
    CHECK_EQ(g_rec.lastEntText, 3246);
}

TEST(NpcAction11Unit, BeginFriendshipNoTargetReturnsZero) {
    ResetAll();
    g_recs = { Rec{} }; g_recs[0].id = 100; g_recs[0].kind = 5;
    Rec ctx{}; ctx.sp0 = 999;   // no such record
    NpcAction11Hooks h = MakeHooks(); SetNpcAction11Hooks(&h);
    CHECK_EQ(NpcAction11_BeginFriendship(&g_recs[0], &ctx), 0);
    CHECK_EQ(g_rec.coord27, 0);
}

TEST(NpcAction11Unit, BeginDivorceNonHostBothNotified) {
    ResetAll();
    g_recs = { Rec{}, Rec{}, Rec{} };
    g_recs[0].id = 100; g_recs[0].kind = 5;
    g_recs[1].id = 201; g_recs[1].kind = 6;
    g_recs[2].id = 202; g_recs[2].kind = 7;
    Rec ctx{}; ctx.sp0 = 201; ctx.sp1 = 202;
    NpcAction11Hooks h = MakeHooks(); SetNpcAction11Hooks(&h);
    CHECK_EQ(NpcAction11_BeginDivorce(&g_recs[0], &ctx), 1);
    CHECK_EQ(g_rec.coord27, 2);          // mutual recall
    CHECK_EQ(g_rec.cDelta, -25);
    CHECK_EQ(g_rec.entityMsgs, 2);       // both host-kind notified
}

// ===========================================================================
// GrantAiCredit — byte gate + transfer bracket.
// ===========================================================================
TEST(NpcAction11Unit, GrantAiCreditFullTransfer) {
    ResetAll();
    g_recs = { Rec{}, Rec{} };
    g_recs[0].id = 50;     // resolved giver entity
    g_recs[1].id = 51;     // resolved secondary entity
    Rec giver{}; giver.kind = 4; giver.sp0 = 50; giver.sp1 = 51;
    Rec obj{};   obj.kind = 20; obj.sp0 = 777;   // credit amount
    NpcAction11Hooks h = MakeHooks(); SetNpcAction11Hooks(&h);
    HeBuf he; He_Id(he.rec()) = 9;
    CHECK_EQ(static_cast<int>(NpcAction11_GrantAiCredit(he.rec(), &giver, &obj)), 0);
    CHECK_EQ(g_rec.actionStart, 1);
    CHECK_EQ(g_rec.actionEnd, 1);
    CHECK_EQ(g_rec.cmd15, 1);
    CHECK_EQ(g_rec.cmd15Amt, 777);
    CHECK_EQ(g_rec.beginDelta, 1);       // secondary entity present
    CHECK_EQ(g_rec.appendRaw, 1);
    CHECK_EQ(g_rec.state22, 1);
}

TEST(NpcAction11Unit, GrantAiCreditWrongGiverByteRejects) {
    ResetAll();
    Rec giver{}; giver.kind = 5; Rec obj{}; obj.kind = 20;
    NpcAction11Hooks h = MakeHooks(); SetNpcAction11Hooks(&h);
    HeBuf he;
    CHECK_EQ(static_cast<int>(NpcAction11_GrantAiCredit(he.rec(), &giver, &obj)), 0);
    CHECK_EQ(g_rec.actionStart, 0);
}

// ===========================================================================
// BuildWell — gate + upgrade vs build. Upgrade cost = 200 * 0.3 = 60.
// ===========================================================================
TEST(NpcAction11Unit, BuildWellUpgradeCostFromSlotWorth) {
    ResetAll();
    Rec bld{}; bld.id = 0;
    NpcAction11Hooks h = MakeHooks(); SetNpcAction11Hooks(&h);
    HeBuf he; he.put8(358, 10);     // office byte gate
    u8 act = 4;                     // upgrade
    CHECK_EQ(static_cast<int>(NpcAction11_BuildWell(he.rec(), &bld, &act)), 47);
    CHECK_EQ(g_rec.actionStart, 1);
    CHECK_EQ(g_rec.cmd15, 1);
    CHECK_EQ(g_rec.cmd15Amt, 60);   // 200 * 0.3
    CHECK_EQ(g_rec.slotReset28, 1);
}

TEST(NpcAction11Unit, BuildWellGateRejectsWrongOfficeByte) {
    ResetAll();
    Rec bld{};
    NpcAction11Hooks h = MakeHooks(); SetNpcAction11Hooks(&h);
    HeBuf he; he.put8(358, 9);      // wrong office byte
    u8 act = 4;
    CHECK_EQ(static_cast<int>(NpcAction11_BuildWell(he.rec(), &bld, &act)), 0);
    CHECK_EQ(g_rec.actionStart, 0);
}

TEST(NpcAction11Unit, BuildWellNewBuildReturns47) {
    ResetAll();
    Rec bld{};
    NpcAction11Hooks h = MakeHooks(); SetNpcAction11Hooks(&h);
    HeBuf he; he.put8(358, 10);
    u8 act = 0;                     // != 4 -> new build
    CHECK_EQ(static_cast<int>(NpcAction11_BuildWell(he.rec(), &bld, &act)), 47);
    CHECK_EQ(g_rec.actionStart, 1);
    CHECK_EQ(g_rec.cmd15, 1);
    CHECK_EQ(g_rec.cmd15Amt, 200);  // full slot-worth for new build
}

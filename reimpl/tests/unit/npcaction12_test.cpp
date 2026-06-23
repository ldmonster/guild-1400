#include "test.h"

// Unit tests for sim/npcaction12 — the final batch of UNTRANSLATED
// VIBE_NpcAction_* behaviours. Golden vectors for the GameTime stamping/advance
// were computed with a python oracle of VIBE_GameTime_Advance/Set; the LCG draws
// for QueueRandomActions with the glibc-LCG oracle. Cross-cluster leaves are
// recorded through installed NpcAction12Hooks.
#include "sim/npcaction12.h"
#include "sim/npcaction.h"   // NpcClock / SetNpcClock
#include "sim/he.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// A record large enough for the deepest offset this module touches (+361).
struct Rec { u8 b[600]; };
HeRecord* AsHe(Rec& r) { return reinterpret_cast<HeRecord*>(r.b); }

void SetClock(int day, int hour, int minute, int second) {
    GameTime t{};
    t.day = day; t.hour = (u16)hour; t.minute = minute; t.second = second;
    SetNpcClock(t);
}

// Recording sink shared across tests.
struct Sink {
    int frees = 0;
    std::vector<int> coord27;       // deltas
    std::vector<int> entityText;    // sendEntity textIds
    std::vector<int> args25;        // 'a' field
    std::vector<int> slotReset;     // n field
    int buildStart = 0, buildEnd = 0;
    long long cmd15Amount = -777;
    int panelAlliance = 0;
} g_sink;

i32 OnFree(HeRecord*) { g_sink.frees++; return 0; }
void OnCoord27(i32, i32, int d) { g_sink.coord27.push_back(d); }
void OnEntity(i32, int t) { g_sink.entityText.push_back(t); }
void OnArgs25(i32, int a, int, int, int) { g_sink.args25.push_back(a); }
void OnSlotReset(void*, int n) { g_sink.slotReset.push_back(n); }
void OnBuildStart(const char*) { g_sink.buildStart++; }
void OnBuildEnd() { g_sink.buildEnd++; }
void OnCmd15(i32, i32, long long amt, u8) { g_sink.cmd15Amount = amt; }
void OnPanelAlliance(int, u16, u16, u16, u16) { g_sink.panelAlliance++; }

void ResetSink() { g_sink = Sink{}; }

const GameTime* ApptOf(Rec& r) { return reinterpret_cast<GameTime*>(r.b + 82); }
const GameTime* At(Rec& r, int off) { return reinterpret_cast<GameTime*>(r.b + off); }

} // namespace

// ---------------------------------------------------------------------------
// Appointment-arming launchers: exact GameTime stamping/advance.
// ---------------------------------------------------------------------------
TEST(NpcAction12, InitWalkStateStampsSixFifteen) {
    SetClock(10, 8, 30, 0);
    Rec r{};
    NpcAction12_InitWalkState(AsHe(r));
    // +176 = 3, +172 = 0
    CHECK_EQ(*reinterpret_cast<i32*>(r.b + 176), 3);
    CHECK_EQ(*reinterpret_cast<i32*>(r.b + 172), 0);
    // appt(+82) = Set(6h, 0s, 15m); day inherited from the clock (10)
    CHECK_EQ(ApptOf(r)->day, 10);
    CHECK_EQ((int)ApptOf(r)->hour, 6);
    CHECK_EQ(ApptOf(r)->minute, 15);
    CHECK_EQ(ApptOf(r)->second, 0);
    // saved(+68) = raw clock
    CHECK_EQ((int)At(r, 68)->hour, 8);
    CHECK_EQ(At(r, 68)->minute, 30);
}

TEST(NpcAction12, BeginGotoHomeStepCopiesAndAdvances) {
    SetClock(10, 8, 30, 0);
    Rec r{};
    NpcAction12_BeginGotoHomeStep(AsHe(r));
    // +96 was copied BEFORE the advance: Set(5h,0,0)
    CHECK_EQ((int)At(r, 96)->hour, 5);
    CHECK_EQ(At(r, 96)->minute, 0);
    // appt(+82) after advance(+1 'day' -> hour): hour 6, day 10
    CHECK_EQ(ApptOf(r)->day, 10);
    CHECK_EQ((int)ApptOf(r)->hour, 6);
    CHECK_EQ(ApptOf(r)->minute, 0);
}

TEST(NpcAction12, InitDualCoordWalkAdvancesBothSlots) {
    SetClock(10, 8, 30, 0);
    Rec r{};
    NpcAction12_InitDualCoordWalk(AsHe(r));
    // +204 advanced by 48 'days' (added to hour) -> +2 days +8h
    CHECK_EQ(At(r, 204)->day, 12);
    CHECK_EQ((int)At(r, 204)->hour, 8);
    CHECK_EQ(At(r, 204)->minute, 30);
    // +82 advanced by +1 second
    CHECK_EQ(ApptOf(r)->day, 10);
    CHECK_EQ(ApptOf(r)->second, 1);
}

TEST(NpcAction12, BeginScanType63StampsAndAdvancesNoConflict) {
    ResetSink();
    SetClock(10, 8, 30, 0);
    NpcAction12Hooks h{};
    h.freeHandlerEntry = &OnFree;
    h.findConflictingHandler = nullptr;     // no conflict resolver -> nullptr
    SetNpcAction12Hooks(&h);

    Rec r{};
    *reinterpret_cast<i32*>(r.b + 112) = 5; // state preserved (no conflict)
    NpcAction12_BeginScanType63(AsHe(r));
    CHECK_EQ(*reinterpret_cast<i32*>(r.b + 112), 5);   // not aborted
    CHECK_EQ(ApptOf(r)->second, 1);
    SetNpcAction12Hooks(nullptr);
}

TEST(NpcAction12, BeginScanType63AbortsOnConflict) {
    ResetSink();
    SetClock(10, 8, 30, 0);
    static int marker = 0;
    NpcAction12Hooks h{};
    h.findConflictingHandler =
        [](HeRecord*, int, i32) -> void* { return &marker; };  // conflict present
    SetNpcAction12Hooks(&h);

    Rec r{};
    *reinterpret_cast<i32*>(r.b + 112) = 5;
    NpcAction12_BeginScanType63(AsHe(r));
    CHECK_EQ(*reinterpret_cast<i32*>(r.b + 112), -1);  // aborted
    CHECK_EQ(ApptOf(r)->second, 1);
    SetNpcAction12Hooks(nullptr);
}

TEST(NpcAction12, BeginScanType50FreesOnForeignMatch) {
    ResetSink();
    SetClock(10, 8, 30, 0);
    NpcAction12Hooks h{};
    h.freeHandlerEntry = &OnFree;
    h.scanFilterHasForeignMatch = [](HeRecord*, int) { return true; };
    SetNpcAction12Hooks(&h);

    Rec r{};
    NpcAction12_BeginScanType50(AsHe(r));
    CHECK_EQ(g_sink.frees, 1);
    CHECK_EQ(ApptOf(r)->second, 0);   // never stamped (freed first)
    SetNpcAction12Hooks(nullptr);
}

TEST(NpcAction12, BeginScanType50StampsWhenAlone) {
    ResetSink();
    SetClock(10, 8, 30, 0);
    NpcAction12Hooks h{};
    h.freeHandlerEntry = &OnFree;
    h.scanFilterHasForeignMatch = [](HeRecord*, int) { return false; };
    SetNpcAction12Hooks(&h);

    Rec r{};
    NpcAction12_BeginScanType50(AsHe(r));
    CHECK_EQ(g_sink.frees, 0);
    CHECK_EQ(ApptOf(r)->second, 1);
    SetNpcAction12Hooks(nullptr);
}

// ---------------------------------------------------------------------------
// QueueRandomActions LCG draws (glibc-LCG oracle).
// ---------------------------------------------------------------------------
TEST(NpcAction12, QueueRandomActionsLcgGolden) {
    ResetSink();
    NpcAction12Hooks h{};
    h.requestSlotReset28 = &OnSlotReset;
    SetNpcAction12Hooks(&h);

    g_lcgState = 1;
    i32 count = NpcAction12_QueueRandomActions(5, 0);
    // oracle: seed1 -> draw 2; emits 2 slot-reset(69).
    CHECK_EQ(count, 2);
    CHECK_EQ((int)g_sink.slotReset.size(), 2);
    if (g_sink.slotReset.size() == 2) {
        CHECK_EQ(g_sink.slotReset[0], 69);
        CHECK_EQ(g_sink.slotReset[1], 69);
    }
    CHECK_EQ(g_lcgState, 662824084u);   // oracle final state

    // maxCount == -1 -> count 0, no emissions.
    ResetSink();
    i32 z = NpcAction12_QueueRandomActions(-1, 0);
    CHECK_EQ(z, 0);
    CHECK_EQ((int)g_sink.slotReset.size(), 0);
    SetNpcAction12Hooks(nullptr);
}

// ---------------------------------------------------------------------------
// TavernJoinLeave — magic-tag switch.
// ---------------------------------------------------------------------------
namespace {
struct TagRec { i32 pad0; i32 id; i32 tag; };  // +0, +4 id, +8 tag
i32 FieldRead(void* rec, int off) { return *reinterpret_cast<i32*>(reinterpret_cast<u8*>(rec) + off); }
}
TEST(NpcAction12, TavernJoinLeaveTags) {
    ResetSink();
    NpcAction12Hooks h{};
    h.field = &FieldRead;
    h.buildingActionStart = &OnBuildStart;
    h.buildingActionEnd = &OnBuildEnd;
    h.requestArgs25 = &OnArgs25;
    h.requestBuildOp84 = [](const void*) {};
    SetNpcAction12Hooks(&h);

    TagRec self{0, 0xA, 0};
    TagRec join{0, 0xB, 1785686382};
    CHECK_EQ((int)NpcAction12_TavernJoinLeave(&self, &join), 53);
    CHECK_EQ(g_sink.buildStart, 1);
    CHECK_EQ(g_sink.buildEnd, 1);
    CHECK_EQ((int)g_sink.args25.size(), 1);

    TagRec leave{0, 0xC, 1818583414};
    CHECK_EQ((int)NpcAction12_TavernJoinLeave(&self, &leave), 53);
    CHECK_EQ(g_sink.buildStart, 2);

    TagRec pra{0, 0xD, 1852796784};
    ResetSink();
    h.field = &FieldRead; h.requestArgs25 = &OnArgs25;
    CHECK_EQ((int)NpcAction12_TavernJoinLeave(&self, &pra), 53);
    CHECK_EQ((int)g_sink.args25.size(), 1);
    if (g_sink.args25.size() == 1) CHECK_EQ(g_sink.args25[0], 456);  // direct args25(456,...)
    CHECK_EQ(g_sink.buildStart, 0);  // '0pra' has no bracket

    TagRec bogus{0, 0xE, 12345};
    CHECK_EQ((int)NpcAction12_TavernJoinLeave(&self, &bogus), 0);
    SetNpcAction12Hooks(nullptr);
}

// ---------------------------------------------------------------------------
// BuildWorkerQuarters — gate + upgrade/build cost.
// ---------------------------------------------------------------------------
namespace {
void* StoreHook(int, HeRecord*) { static int s; return &s; }
i32   ObjIdHook(void* rec) { return rec ? *reinterpret_cast<i32*>(reinterpret_cast<u8*>(rec) + 4) : 0; }
i32   WorthHook(int) { return 1000; }
bool  LoadGfxOk(void*, void*) { return true; }
bool  LoadGfxFail(void*, void*) { return false; }
}
TEST(NpcAction12, BuildWorkerQuartersGate) {
    ResetSink();
    NpcAction12Hooks h{};
    h.buildingFindOfficeStorage = &StoreHook;
    SetNpcAction12Hooks(&h);
    Rec r{};
    r.b[358] = 14;   // != 15 -> gate fails
    u8 act = 4;
    CHECK_EQ((int)NpcAction12_BuildWorkerQuarters(AsHe(r), &act, nullptr), 0);
    SetNpcAction12Hooks(nullptr);
}

TEST(NpcAction12, BuildWorkerQuartersUpgrade) {
    ResetSink();
    NpcAction12Hooks h{};
    h.buildingFindOfficeStorage = &StoreHook;
    h.objId = &ObjIdHook;
    h.sumFlaggedSlotsWorth = &WorthHook;
    h.enqueueCmd15 = &OnCmd15;
    h.requestSlotReset28 = &OnSlotReset;
    h.buildingActionStart = &OnBuildStart;
    h.buildingActionEnd = &OnBuildEnd;
    SetNpcAction12Hooks(&h);
    Rec r{};
    r.b[358] = 15;
    u8 act = 4;     // upgrade
    CHECK_EQ((int)NpcAction12_BuildWorkerQuarters(AsHe(r), &act, nullptr), 46);
    // cost = 1000 * 0.3 = 300
    CHECK_EQ(g_sink.cmd15Amount, 300LL);
    CHECK_EQ(g_sink.buildStart, 1);
    CHECK_EQ(g_sink.buildEnd, 1);
    SetNpcAction12Hooks(nullptr);
}

TEST(NpcAction12, BuildWorkerQuartersBuildSuccessAndFail) {
    NpcAction12Hooks h{};
    h.buildingFindOfficeStorage = &StoreHook;
    h.objId = &ObjIdHook;
    h.sumFlaggedSlotsWorth = &WorthHook;
    h.enqueueCmd15 = &OnCmd15;
    h.buildingActionStart = &OnBuildStart;
    h.buildingActionEnd = &OnBuildEnd;
    h.aiLoadBuildingGraphic = &LoadGfxOk;
    SetNpcAction12Hooks(&h);
    Rec r{};
    r.b[358] = 15;
    u8 act = 0;     // build
    ResetSink();
    h.buildingActionStart = &OnBuildStart; h.buildingActionEnd = &OnBuildEnd;
    h.enqueueCmd15 = &OnCmd15; h.sumFlaggedSlotsWorth = &WorthHook;
    CHECK_EQ((int)NpcAction12_BuildWorkerQuarters(AsHe(r), &act, nullptr), 46);
    CHECK_EQ(g_sink.cmd15Amount, 1000LL);   // raw worth (no factor on build path)

    h.aiLoadBuildingGraphic = &LoadGfxFail;
    ResetSink();
    h.buildingActionStart = &OnBuildStart; h.buildingActionEnd = &OnBuildEnd;
    CHECK_EQ((int)NpcAction12_BuildWorkerQuarters(AsHe(r), &act, nullptr), 0);
    CHECK_EQ(g_sink.buildEnd, 1);           // bracket still closed on failure
    SetNpcAction12Hooks(nullptr);
}

// ---------------------------------------------------------------------------
// FormAllianceGroup — picks distinct eligible peers, coord27 + panel.
// ---------------------------------------------------------------------------
namespace {
// Synthetic 768-slot grid. Slots 100/200/300 are eligible host peers (kind 6);
// everything else is empty (marker -1).
u16 GMarker(u16 i) { return (i==100||i==200||i==300) ? i : (u16)0xFFFF; }
u8  GKind(u16 i)   { return (i==100||i==200||i==300) ? 6 : 0; }
i32 GCityId(u16 i) { return 9000 + i; }
i32 GGroup(u16 i)  { return (i==100||i==200||i==300) ? 7 : 0; }   // peers group 7
u8  GElig(u16 i)   { return (i==100||i==200||i==300) ? 1 : 0; }
// records: a tiny struct keyed by index, marker echoes index, kind 6, id=index.
struct GRec { u16 marker; u8 pad[2]; i32 id; u8 k; };
GRec g_recs[768];
void* GRecord(u16 i) { return &g_recs[i]; }
u8  GRecKind(void* p)   { return reinterpret_cast<GRec*>(p)->k; }
i32 GRecObjId(void* p)  { return reinterpret_cast<GRec*>(p)->id; }
// deterministic RNG that returns the first eligible slot directly.
u16 g_rngSeq[8]; int g_rngIdx = 0;
u16 RngPick(u16) { return g_rngSeq[g_rngIdx++ % 8]; }
}
TEST(NpcAction12, FormAllianceGroupPicksAndBroadcasts) {
    ResetSink();
    for (int i = 0; i < 768; ++i) { g_recs[i].marker = (u16)i; g_recs[i].id = 1000+i; g_recs[i].k = (i==100||i==200||i==300)?6:0; }
    g_rngSeq[0]=100; g_rngSeq[1]=200; g_rngSeq[2]=300; g_rngIdx=0;

    NpcAction12Hooks h{};
    h.markerWord = [](void* p) -> u16 {
        // self uses a distinct sentinel marker 500; grid records echo their index.
        GRec* g = reinterpret_cast<GRec*>(p);
        return g->marker;
    };
    h.cityMarker = &GMarker;
    h.cityKind = &GKind;
    h.cityId = &GCityId;
    h.cityIdShifted = &GGroup;
    h.cityAllianceEligible = &GElig;
    h.cityRecord = &GRecord;
    h.kind = &GRecKind;
    h.objId = &GRecObjId;
    h.randomModulo = &RngPick;
    h.requestCoord27 = &OnCoord27;
    h.sendEntity = &OnEntity;
    h.panelShowAlliance = &OnPanelAlliance;
    // field(+12) for self group word -> 0
    static i32 selfBuf[8] = {0};
    h.field = [](void* p, int off) -> i32 { return reinterpret_cast<i32*>(p)[off/4]; };
    SetNpcAction12Hooks(&h);

    // self: a GRec with marker 500, kind 6, id 555.
    // The source reads the alliance-group word at byte +6 (*(int*)(a1+3), a1 being
    // unsigned __int16*), i.e. field(self,6) -> the GRec id dword (555); 555>>24 == 0,
    // which differs from the peers' group (7), so the picks remain eligible. Back the
    // synthetic self with trailing storage so the read stays in-bounds (ASAN).
    static struct { GRec rec; i32 groupWord; } selfStore{};
    GRec& self = selfStore.rec; self.marker = 500; self.k = 6; self.id = 555;
    selfStore.groupWord = 0;
    i32 rc = NpcAction12_FormAllianceGroup(&self, selfBuf);
    CHECK_EQ(rc, 1);
    CHECK_EQ(selfBuf[4], 1);                 // ctx[4] = 1
    CHECK_EQ((int)g_sink.coord27.size(), 3); // three approaches
    for (int d : g_sink.coord27) CHECK_EQ(d, 20);
    CHECK_EQ((int)g_sink.entityText.size(), 3); // three host peers notified
    // gilde.exe 0x5691e6: He_SendEntityMessage(..., msgId=1418) — the transmitted
    // message-id is 1418 (text 3252 is the pre-rendered body, not the send id).
    for (int t : g_sink.entityText) CHECK_EQ(t, 1418);
    CHECK_EQ(g_sink.panelAlliance, 1);       // self is host kind 6
    SetNpcAction12Hooks(nullptr);
}

// ---------------------------------------------------------------------------
// State-machine "free" semantics for the step coroutines (negative-state paths).
// ---------------------------------------------------------------------------
TEST(NpcAction12, AssignWorkPlaceStepNegativeStatesFree) {
    ResetSink();
    NpcAction12Hooks h{};
    h.freeHandlerEntry = &OnFree;
    SetNpcAction12Hooks(&h);
    Rec r{};
    *reinterpret_cast<i32*>(r.b + 112) = -1;
    NpcAction12_AssignWorkPlaceStep(AsHe(r));
    CHECK_EQ(g_sink.frees, 1);

    *reinterpret_cast<i32*>(r.b + 112) = -2;
    NpcAction12_AssignWorkPlaceStep(AsHe(r));
    CHECK_EQ(g_sink.frees, 2);

    *reinterpret_cast<i32*>(r.b + 112) = -3;  // < -2 with state != -2 -> ignore
    NpcAction12_AssignWorkPlaceStep(AsHe(r));
    CHECK_EQ(g_sink.frees, 2);                 // unchanged
    SetNpcAction12Hooks(nullptr);
}

TEST(NpcAction12, NotifyTrainingStepNegativeStatesFree) {
    ResetSink();
    NpcAction12Hooks h{};
    h.freeHandlerEntry = &OnFree;
    SetNpcAction12Hooks(&h);
    Rec r{};
    *reinterpret_cast<i32*>(r.b + 112) = -1;
    NpcAction12_NotifyTrainingStep(AsHe(r));
    CHECK_EQ(g_sink.frees, 1);
    *reinterpret_cast<i32*>(r.b + 112) = 7;    // state > 0 -> ignore
    NpcAction12_NotifyTrainingStep(AsHe(r));
    CHECK_EQ(g_sink.frees, 1);
    SetNpcAction12Hooks(nullptr);
}

TEST(NpcAction12, DemolishBuildingStepNegativeStatesFree) {
    ResetSink();
    NpcAction12Hooks h{};
    h.freeHandlerEntry = &OnFree;
    SetNpcAction12Hooks(&h);
    Rec r{};
    *reinterpret_cast<i32*>(r.b + 112) = -2;  // state+2 == 0 -> free
    NpcAction12_DemolishBuildingStep(AsHe(r));
    CHECK_EQ(g_sink.frees, 1);
    *reinterpret_cast<i32*>(r.b + 112) = -1;  // state+2 == 1 -> free
    NpcAction12_DemolishBuildingStep(AsHe(r));
    CHECK_EQ(g_sink.frees, 2);
    SetNpcAction12Hooks(nullptr);
}

// EvaluateUseFront: relFlag short-circuit + gun-cooldown gate.
TEST(NpcAction12, EvaluateUseFrontGates) {
    ResetSink();
    NpcAction12Hooks h{};
    SetNpcAction12Hooks(&h);
    u8 outA[24] = {0}, outB[24] = {0};
    // relFlag set -> 0
    CHECK_EQ((int)NpcAction12_EvaluateUseFront(nullptr, outA, 1, outB), 0);

    // gun-cooldown: (+485 & 1) && RandomModulo(4)!=0 -> 0
    static u8 prec = 1;
    h.equipState = [](void*, int off) -> u8 { return off == 485 ? 1 : 0; };
    h.randomModulo = [](u16) -> u16 { return 3; };   // nonzero
    SetNpcAction12Hooks(&h);
    CHECK_EQ((int)NpcAction12_EvaluateUseFront(&prec, outA, 0, outB), 0);
    SetNpcAction12Hooks(nullptr);
}

// MasterExamStep: appointment gate not reached -> no-op return 0.
TEST(NpcAction12, MasterExamStepAppointmentGate) {
    ResetSink();
    SetClock(5, 0, 0, 0);     // clock day 5
    NpcAction12Hooks h{};
    h.freeHandlerEntry = &OnFree;
    h.eventPanelDestroy = [](HeRecord*) {};
    SetNpcAction12Hooks(&h);
    Rec r{};
    // appt(+82) day 10 (in the future) -> Compare(clock, appt) < 0 -> early return.
    reinterpret_cast<GameTime*>(r.b + 82)->day = 10;
    *reinterpret_cast<i32*>(r.b + 112) = -1;
    i32 rc = NpcAction12_MasterExamStep(AsHe(r));
    // gilde.exe 0x4e5c36: result = GameTime_Compare(clock, appt); if (result >= 0) {..};
    // return result. clock day 5 < appt day 10 -> Compare returns -1, which is the
    // function's return value (NOT 0).
    CHECK_EQ(rc, -1);
    CHECK_EQ(g_sink.frees, 0);   // gate not reached -> nothing freed
    SetNpcAction12Hooks(nullptr);
}

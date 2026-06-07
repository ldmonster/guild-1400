// Unit tests for combat batch-3 leaves (gilde.exe VIBE_Combat_*):
//   FindObjectDef (0x485a54), FindActiveTarget (0x485b1c),
//   ResetObjectHighlights (0x485b94), CloseSelectionWindows (0x4870f0),
//   ResetDamageNumberTable (0x48751c), DestroyDamageNumbers (0x487548),
//   DestroyOrderSlotWindows (0x488828), GatherPlayerUnits (0x489578),
//   ResetSelectionState (0x48938c), IssueOrdersForTeam row/slot (0x48c15c),
//   RunBattleFrameLoop (0x48c5e8), RunOrderWaitLoop inner (0x48c648),
//   BeginBattleOrCacheState (0x492e6c).
// The GameObject iterator and the window/widget side effects are driven through a
// recording CombatSlots3Hooks mock so the table-walk logic is exercised in full.
#include "sim/combat_slots3.h"
#include "test.h"

#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// ---- A scripted GameObject iterator mock ----------------------------------
struct FakeObject { i16 id; i32 hp; };

std::vector<FakeObject> g_objs;
size_t g_iter = 0;
int g_queueCount = 0;
int g_windowRemoves = 0;
int g_widgetDestroys = 0;
int g_meshClears = 0;
int g_dragResets = 0;
int g_formDestroys = 0;

const void* FakeQueryFind(u32, int, int) {
    g_iter = 0;
    if (g_objs.empty()) return nullptr;
    return &g_objs[g_iter++];
}
const void* FakeIterNext() {
    if (g_iter >= g_objs.size()) return nullptr;
    return &g_objs[g_iter++];
}
i16 FakeObjId(const void* o) { return static_cast<const FakeObject*>(o)->id; }
i32 FakeObjHp(const void* o) { return static_cast<const FakeObject*>(o)->hp; }
void FakeQueueClear(i32, i32, i16) { ++g_queueCount; }
void FakeWindowRemove(i32) { ++g_windowRemoves; }
void FakeWidgetDestroy(i32) { ++g_widgetDestroys; }
void FakeMeshClear(const void*) { ++g_meshClears; }
void FakeDragReset() { ++g_dragResets; }
void FakeFormDestroy(i32) { ++g_formDestroys; }

CombatSlots3Hooks MakeHooks() {
    CombatSlots3Hooks h;
    h.gameObjectQueryFind = FakeQueryFind;
    h.gameObjectIterNext = FakeIterNext;
    h.gameObjectId = FakeObjId;
    h.gameObjectHp = FakeObjHp;
    h.queueClearRequest = FakeQueueClear;
    h.windowRemoveIfActive = FakeWindowRemove;
    h.widgetDestroyByType = FakeWidgetDestroy;
    h.meshClearHighlight = FakeMeshClear;
    h.dragSlotResetTable = FakeDragReset;
    h.formDestroy = FakeFormDestroy;
    return h;
}

void ResetCounters() {
    g_objs.clear(); g_iter = 0;
    g_queueCount = g_windowRemoves = g_widgetDestroys = 0;
    g_meshClears = g_dragResets = g_formDestroys = 0;
}

// A 10-entry object-def ring.
std::vector<ObjDefEntry> MakeRing() {
    std::vector<ObjDefEntry> ring(kObjDefCount);
    return ring;
}

} // namespace

// --- FindObjectDef -----------------------------------------------------------
TEST(CombatSlots3, FindObjectDefMatchReturnsEntry) {
    CombatSlots3Hooks h = MakeHooks();
    SetCombatSlots3Hooks(&h);
    ResetCounters();

    auto ring = MakeRing();
    ring[3].idHigh = 1234; ring[3].classByte = 0;
    g_objs = { {1234, 50} };
    CHECK_EQ(FindObjectDef(ring, 0u), 3);  // matched, non-conquerable -> return match

    SetCombatSlots3Hooks(nullptr);
}

TEST(CombatSlots3, FindObjectDefDeadConquerableFallsToFree) {
    CombatSlots3Hooks h = MakeHooks();
    SetCombatSlots3Hooks(&h);
    ResetCounters();

    auto ring = MakeRing();
    ring[2].idHigh = 777; ring[2].classByte = 1;  // conquerable
    // first FREE entry is index 0 (idHigh==0).
    g_objs = { {777, 0} };                          // hp <= 0
    CHECK_EQ(FindObjectDef(ring, 0u), 0);

    SetCombatSlots3Hooks(nullptr);
}

TEST(CombatSlots3, FindObjectDefNoMatchReturnsFree) {
    CombatSlots3Hooks h = MakeHooks();
    SetCombatSlots3Hooks(&h);
    ResetCounters();

    auto ring = MakeRing();
    ring[0].idHigh = 1; ring[1].idHigh = 2;        // first free is index 2
    g_objs = { {9999, 100} };                       // not in ring
    CHECK_EQ(FindObjectDef(ring, 0u), 2);

    SetCombatSlots3Hooks(nullptr);
}

TEST(CombatSlots3, FindObjectDefFullRingNoObjectsReturnsMinusOne) {
    CombatSlots3Hooks h = MakeHooks();
    SetCombatSlots3Hooks(&h);
    ResetCounters();

    auto ring = MakeRing();
    for (int i = 0; i < kObjDefCount; ++i) ring[i].idHigh = 100 + i;  // full
    g_objs.clear();                                                   // no objects
    CHECK_EQ(FindObjectDef(ring, 0u), -1);

    SetCombatSlots3Hooks(nullptr);
}

// --- FindActiveTarget --------------------------------------------------------
TEST(CombatSlots3, FindActiveTargetValidNonConquerable) {
    CombatSlots3Hooks h = MakeHooks();
    SetCombatSlots3Hooks(&h);
    ResetCounters();

    auto ring = MakeRing();
    ring[5].idHigh = 42; ring[5].classByte = 7;     // not 1/2 -> always valid
    g_objs = { {42, 0} };                            // hp 0 irrelevant for class 7
    CHECK(FindActiveTarget(ring, 0u) == &g_objs[0]);

    SetCombatSlots3Hooks(nullptr);
}

TEST(CombatSlots3, FindActiveTargetDeadConquerableReturnsNull) {
    CombatSlots3Hooks h = MakeHooks();
    SetCombatSlots3Hooks(&h);
    ResetCounters();

    auto ring = MakeRing();
    ring[5].idHigh = 42; ring[5].classByte = 2;     // conquerable
    g_objs = { {42, 0} };                            // hp <= 0 -> null
    CHECK(FindActiveTarget(ring, 0u) == nullptr);

    SetCombatSlots3Hooks(nullptr);
}

TEST(CombatSlots3, FindActiveTargetLiveConquerableValid) {
    CombatSlots3Hooks h = MakeHooks();
    SetCombatSlots3Hooks(&h);
    ResetCounters();

    auto ring = MakeRing();
    ring[5].idHigh = 42; ring[5].classByte = 1;
    g_objs = { {42, 3} };                            // hp > 0 -> valid
    CHECK(FindActiveTarget(ring, 0u) == &g_objs[0]);

    SetCombatSlots3Hooks(nullptr);
}

TEST(CombatSlots3, FindActiveTargetSkipsNonRingObjects) {
    CombatSlots3Hooks h = MakeHooks();
    SetCombatSlots3Hooks(&h);
    ResetCounters();

    auto ring = MakeRing();
    ring[1].idHigh = 88; ring[1].classByte = 9;
    g_objs = { {7, 1}, {88, 1} };                    // first skipped, second matches
    CHECK(FindActiveTarget(ring, 0u) == &g_objs[1]);

    SetCombatSlots3Hooks(nullptr);
}

// --- ResetObjectHighlights ---------------------------------------------------
TEST(CombatSlots3, ResetObjectHighlightsQueuesPerMatch) {
    CombatSlots3Hooks h = MakeHooks();
    SetCombatSlots3Hooks(&h);
    ResetCounters();

    auto ring = MakeRing();
    ring[0].idHigh = 10; ring[1].idHigh = 20; ring[2].idHigh = 30;
    g_objs = { {10, 1}, {99, 1}, {30, 1} };          // 10 and 30 matched, 99 skipped
    int n = ResetObjectHighlights(ring, 0u, /*team*/2, /*objField7*/5);
    CHECK_EQ(n, 2);
    CHECK_EQ(g_queueCount, 2);

    SetCombatSlots3Hooks(nullptr);
}

// --- CloseSelectionWindows ---------------------------------------------------
TEST(CombatSlots3, CloseSelectionWindowsRemovesAndClears) {
    CombatSlots3Hooks h = MakeHooks();
    SetCombatSlots3Hooks(&h);
    ResetCounters();

    std::vector<i32> wins(kSelWindowCount, -1);
    wins[0] = 100; wins[7] = 200; wins[31] = 300;    // 3 live
    int closed = CloseSelectionWindows(wins);
    CHECK_EQ(closed, 3);
    CHECK_EQ(g_windowRemoves, 3);
    CHECK_EQ(wins[0], -1); CHECK_EQ(wins[7], -1); CHECK_EQ(wins[31], -1);

    SetCombatSlots3Hooks(nullptr);
}

// --- ResetDamageNumberTable (pure) ------------------------------------------
TEST(CombatSlots3, ResetDamageNumberTableClearsFields) {
    std::vector<DamageNumberRecord> tbl(kDmgNumCount);
    for (auto& r : tbl) { r.age = 9; r.handle = 5; r.flag = 1; r.widget = 7; }
    ResetDamageNumberTable(tbl);
    for (const auto& r : tbl) {
        CHECK_EQ(r.age, 0);
        CHECK_EQ(r.handle, -1);
        CHECK_EQ(r.flag, 0);
        // widget is deliberately NOT touched by the reset variant.
        CHECK_EQ(r.widget, 7);
    }
}

// --- DestroyDamageNumbers ----------------------------------------------------
TEST(CombatSlots3, DestroyDamageNumbersFreesLiveWidgets) {
    CombatSlots3Hooks h = MakeHooks();
    SetCombatSlots3Hooks(&h);
    ResetCounters();

    std::vector<DamageNumberRecord> tbl(kDmgNumCount);
    tbl[0].widget = 11; tbl[3].widget = 22; tbl[15].widget = 33;  // 3 live
    tbl[1].widget = -1;                                            // empty
    int destroyed = DestroyDamageNumbers(tbl);
    CHECK_EQ(destroyed, 3);
    CHECK_EQ(g_widgetDestroys, 3);
    for (const auto& r : tbl) { CHECK_EQ(r.handle, -1); CHECK_EQ(r.flag, 0); }

    SetCombatSlots3Hooks(nullptr);
}

// --- DestroyOrderSlotWindows -------------------------------------------------
TEST(CombatSlots3, DestroyOrderSlotWindowsTearsDownLive) {
    CombatSlots3Hooks h = MakeHooks();
    SetCombatSlots3Hooks(&h);
    ResetCounters();

    std::vector<OrderSlotWindow> tbl(kOrderSlotWindowCount);
    tbl[0] = {10, 100}; tbl[2] = {12, 102};   // 2 live; rest window==-1
    int torn = DestroyOrderSlotWindows(tbl);
    CHECK_EQ(torn, 2);
    CHECK_EQ(g_windowRemoves, 2);
    CHECK_EQ(g_widgetDestroys, 2);
    CHECK_EQ(tbl[0].window, -1); CHECK_EQ(tbl[2].window, -1);

    SetCombatSlots3Hooks(nullptr);
}

// --- GatherPlayerUnits -------------------------------------------------------
TEST(CombatSlots3, GatherPlayerUnitsCollectsOwnedOnly) {
    static int a, b, c, d;
    std::vector<CombatUnitSlot> slots = {
        {true,  true,  &a},   // owned
        {true,  false, &b},   // not owned
        {false, true,  &c},   // no unit
        {true,  true,  &d},   // owned
    };
    auto out = GatherPlayerUnits(slots);
    CHECK_EQ(static_cast<int>(out.size()), 2);
    CHECK(out[0] == &a);
    CHECK(out[1] == &d);
}

TEST(CombatSlots3, GatherPlayerUnitsCapsAt31) {
    std::vector<CombatUnitSlot> slots;
    static int handles[40];
    for (int i = 0; i < 40; ++i) slots.push_back({true, true, &handles[i]});
    auto out = GatherPlayerUnits(slots);
    CHECK_EQ(static_cast<int>(out.size()), kPlayerUnitCap);  // 31
}

TEST(CombatSlots3, GatherPlayerUnitsScansAtMost32Slots) {
    // 40 slots, only slot 35 is player-owned -> never reached (scan stops at 32).
    std::vector<CombatUnitSlot> slots(40, CombatUnitSlot{});
    static int late;
    slots[35] = {true, true, &late};
    auto out = GatherPlayerUnits(slots);
    CHECK_EQ(static_cast<int>(out.size()), 0);
}

// --- ResetSelectionState -----------------------------------------------------
TEST(CombatSlots3, ResetSelectionStateFullTeardown) {
    CombatSlots3Hooks h = MakeHooks();
    SetCombatSlots3Hooks(&h);
    ResetCounters();

    static int unitA, unitB;
    std::vector<SelectionRecord> recs(kCombatUnitCount);
    recs[0] = {true,  &unitA, true};     // highlight set + mesh highlighted
    recs[1] = {true,  nullptr, false};   // highlight only
    recs[2] = {false, &unitB, true};     // mesh only
    std::vector<i32> wins(kSelWindowCount, -1);
    wins[4] = 50; wins[9] = 60;          // 2 windows
    i32 form = 12345;

    SelectionResetResult r = ResetSelectionState(recs, wins, form);
    CHECK_EQ(r.highlightsCleared, 2);    // recs[0], recs[1]
    CHECK_EQ(r.meshesCleared, 2);        // recs[0], recs[2]
    CHECK_EQ(r.windowsClosed, 2);
    CHECK(r.formDestroyed);
    CHECK_EQ(form, -1);
    CHECK_EQ(g_dragResets, 1);
    CHECK_EQ(g_formDestroys, 1);
    CHECK(!recs[0].highlightSet);

    SetCombatSlots3Hooks(nullptr);
}

TEST(CombatSlots3, ResetSelectionStateNoFormWhenMinusOne) {
    CombatSlots3Hooks h = MakeHooks();
    SetCombatSlots3Hooks(&h);
    ResetCounters();

    std::vector<SelectionRecord> recs(kCombatUnitCount);
    std::vector<i32> wins(kSelWindowCount, -1);
    i32 form = -1;
    SelectionResetResult r = ResetSelectionState(recs, wins, form);
    CHECK(!r.formDestroyed);
    CHECK_EQ(g_formDestroys, 0);
    CHECK_EQ(g_dragResets, 1);           // drag reset always runs

    SetCombatSlots3Hooks(nullptr);
}

// --- IssueOrdersForTeam: row resolution + slot collection -------------------
TEST(CombatSlots3, FindSquadRowForTeamMatches) {
    std::vector<SquadRow> rows(4);
    rows[0].teamId = 10; rows[1].teamId = 20; rows[2].teamId = 30;
    CHECK_EQ(FindSquadRowForTeam(rows, 3, 20), 1);
    CHECK_EQ(FindSquadRowForTeam(rows, 3, 99), -1);   // no match
    CHECK_EQ(FindSquadRowForTeam(rows, 1, 20), -1);   // rosterCount limits scan
}

TEST(CombatSlots3, CollectOrderSlotIdsPicksSide) {
    SquadRow row;
    row.playerSideIds[0] = 5; row.playerSideIds[1] = -1; row.playerSideIds[2] = 7;
    row.genericSideIds[0] = -1; row.genericSideIds[5] = 9;

    auto player = CollectOrderSlotIds(row, /*isPlayerSide*/true);
    CHECK_EQ(static_cast<int>(player.size()), 2);
    CHECK_EQ(player[0], 5); CHECK_EQ(player[1], 7);

    auto generic = CollectOrderSlotIds(row, /*isPlayerSide*/false);
    CHECK_EQ(static_cast<int>(generic.size()), 1);
    CHECK_EQ(generic[0], 9);
}

// --- RunBattleFrameLoop ------------------------------------------------------
namespace {
int g_frameRemaining = 0;
int FakeRunFrameDecrement(int, int) { return g_frameRemaining > 0 ? g_frameRemaining-- : 0; }
int g_bombA = 0, g_bombB = 0, g_proj = 0, g_pick = 0;
int FakeBombA() { ++g_bombA; return 0; }
int FakeBombB() { ++g_bombB; return 0; }
void FakeProj() { ++g_proj; }
void FakePick() { ++g_pick; }
}

TEST(CombatSlots3, RunBattleFrameLoopRunsEachFrame) {
    CombatSlots3Hooks h;
    h.gameLogicRunFrameLoop = FakeRunFrameDecrement;
    h.combatPickObjectUnderCursor = FakePick;
    h.combatUpdateBombExplosions = FakeBombA;
    h.combatUpdateThrownBombs = FakeBombB;
    h.combatUpdateProjectiles = FakeProj;
    SetCombatSlots3Hooks(&h);

    g_frameRemaining = 3; g_bombA = g_bombB = g_proj = g_pick = 0;
    bool forced = false;
    int result = RunBattleFrameLoop(0, 0, /*slowMoGate*/false,
                                    /*tickCounter*/100, /*tickLimit*/0, forced);
    CHECK_EQ(result, 0);
    CHECK_EQ(g_pick, 3);
    CHECK_EQ(g_bombA, 3);
    CHECK_EQ(g_bombB, 3);
    CHECK_EQ(g_proj, 3);
    CHECK(!forced);                       // 100 > 0 and no slowMoGate

    SetCombatSlots3Hooks(nullptr);
}

TEST(CombatSlots3, RunBattleFrameLoopSlowMoSetsForcedStep) {
    CombatSlots3Hooks h;
    h.gameLogicRunFrameLoop = FakeRunFrameDecrement;
    h.combatUpdateProjectiles = FakeProj;
    SetCombatSlots3Hooks(&h);

    g_frameRemaining = 1; g_proj = 0;
    bool forced = false;
    RunBattleFrameLoop(0, 0, /*slowMoGate*/true, 0, 0, forced);
    CHECK(forced);

    SetCombatSlots3Hooks(nullptr);
}

TEST(CombatSlots3, RunBattleFrameLoopInertReturnsImmediately) {
    SetCombatSlots3Hooks(nullptr);        // inert: RunFrameLoop -> 0
    bool forced = false;
    CHECK_EQ(RunBattleFrameLoop(0, 0, false, 0, 0, forced), 0);
}

// --- RunOrderWaitLoop inner predicate ---------------------------------------
TEST(CombatSlots3, AnyOrderUnitMovingDetectsBusy) {
    std::vector<OrderWaitSlot> slots = {
        {true, true, false},   // active + id but not moving
        {true, true, true},    // busy -> true
        {true, true, true},
    };
    CHECK(AnyOrderUnitMoving(slots));
}

TEST(CombatSlots3, AnyOrderUnitMovingAllIdle) {
    std::vector<OrderWaitSlot> slots = {
        {true,  true,  false},
        {false, true,  true},   // inactive -> ignored even though "moving"
        {true,  false, true},   // no id -> ignored
    };
    CHECK(!AnyOrderUnitMoving(slots));
}

TEST(CombatSlots3, AnyOrderUnitMovingStopsAt16) {
    // 20 slots, only slot 18 is busy -> never reached (scan caps at 16).
    std::vector<OrderWaitSlot> slots(20, OrderWaitSlot{true, true, false});
    slots[18].unitMoving = true;
    CHECK(!AnyOrderUnitMoving(slots));
}

// --- BeginBattleOrCacheState -------------------------------------------------
TEST(CombatSlots3, BeginBattleDuelMode) {
    BattleCacheRecord rec;
    auto a = BeginBattleOrCacheState(rec, /*duelMode*/1, /*pendingFlag*/0, 0);
    CHECK(a == BattleBeginAction::kRunBattleLoop);
}

TEST(CombatSlots3, BeginBattleCachePending) {
    BattleCacheRecord rec;
    auto a = BeginBattleOrCacheState(rec, /*duelMode*/0, /*pendingFlag*/1, /*pendingVal*/77);
    CHECK(a == BattleBeginAction::kCachedPending);
    CHECK_EQ(rec.field852, 0);            // duelMode (==0) written to +852
    CHECK_EQ(rec.field856, 77);
}

TEST(CombatSlots3, BeginBattleCacheDefault) {
    BattleCacheRecord rec;
    auto a = BeginBattleOrCacheState(rec, /*duelMode*/0, /*pendingFlag*/0, 0);
    CHECK(a == BattleBeginAction::kCachedDefault);
    CHECK_EQ(rec.field852, 1);            // default branch writes 1
}

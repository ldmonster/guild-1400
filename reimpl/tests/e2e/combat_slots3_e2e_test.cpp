// E2E flow for combat batch-3 leaves (gilde.exe VIBE_Combat_*): a single battle
// scene lifecycle that threads the slot tables through several leaves.
//
// Scenario: a 3-team battle. We (1) decide whether to begin the battle or cache it,
// (2) gather the local player's combat units, (3) resolve our squad row and collect
// the order-slot ids to issue, (4) walk the order-wait loop predicate until no unit
// is moving, (5) on a kill we re-find the now-dead conquerable object def + verify
// the active-target gate rejects it, (6) tear down the whole UI (selection windows,
// damage numbers, order-slot windows, selection state) and confirm every table is
// drained. All engine side effects route through one recording hooks mock.
#include "sim/combat_slots3.h"
#include "test.h"

#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

struct SceneObject { i16 id; i32 hp; };
std::vector<SceneObject> g_scene;
size_t g_cursor = 0;
int g_windowRemoves = 0, g_widgetDestroys = 0, g_drag = 0, g_form = 0, g_clears = 0;

const void* SceneFind(u32, int, int) { g_cursor = 0; return g_scene.empty() ? nullptr : &g_scene[g_cursor++]; }
const void* SceneNext() { return g_cursor >= g_scene.size() ? nullptr : &g_scene[g_cursor++]; }
i16 SceneId(const void* o) { return static_cast<const SceneObject*>(o)->id; }
i32 SceneHp(const void* o) { return static_cast<const SceneObject*>(o)->hp; }
void OnClear(i32, i32, i16) { ++g_clears; }
void OnWindow(i32) { ++g_windowRemoves; }
void OnWidget(i32) { ++g_widgetDestroys; }
void OnDrag() { ++g_drag; }
void OnForm(i32) { ++g_form; }

CombatSlots3Hooks SceneHooks() {
    CombatSlots3Hooks h;
    h.gameObjectQueryFind = SceneFind;
    h.gameObjectIterNext = SceneNext;
    h.gameObjectId = SceneId;
    h.gameObjectHp = SceneHp;
    h.queueClearRequest = OnClear;
    h.windowRemoveIfActive = OnWindow;
    h.widgetDestroyByType = OnWidget;
    h.dragSlotResetTable = OnDrag;
    h.formDestroy = OnForm;
    return h;
}

} // namespace

TEST(CombatSlots3E2E, FullBattleSceneLifecycle) {
    CombatSlots3Hooks h = SceneHooks();
    SetCombatSlots3Hooks(&h);
    g_scene.clear(); g_cursor = 0;
    g_windowRemoves = g_widgetDestroys = g_drag = g_form = g_clears = 0;

    // (1) Battle begin decision: not a duel, a pending state is queued -> we cache.
    BattleCacheRecord cache;
    auto action = BeginBattleOrCacheState(cache, /*duelMode*/0, /*pendingFlag*/1,
                                          /*pendingValue*/42);
    CHECK(action == BattleBeginAction::kCachedPending);
    CHECK_EQ(cache.field856, 42);

    // (2) Gather the local player's combat units out of a 32-slot field.
    static int u0, u1, u2;
    std::vector<CombatUnitSlot> field(kCombatUnitCount);
    field[0] = {true, true, &u0};
    field[1] = {true, false, &u1};   // enemy
    field[5] = {true, true, &u2};
    auto myUnits = GatherPlayerUnits(field);
    CHECK_EQ(static_cast<int>(myUnits.size()), 2);
    CHECK(myUnits[0] == &u0);
    CHECK(myUnits[1] == &u2);

    // (3) Resolve our squad row (team 7 is the player side) and collect order ids.
    std::vector<SquadRow> rows(3);
    rows[0].teamId = 3;
    rows[1].teamId = 7;             // ours
    rows[1].playerSideIds[0] = 100;
    rows[1].playerSideIds[1] = 101;
    rows[1].playerSideIds[2] = -1;
    rows[1].playerSideIds[3] = 102;
    rows[2].teamId = 9;
    int myRow = FindSquadRowForTeam(rows, /*rosterCount*/3, /*team*/7);
    CHECK_EQ(myRow, 1);
    auto orderIds = CollectOrderSlotIds(rows[myRow], /*isPlayerSide*/true);
    CHECK_EQ(static_cast<int>(orderIds.size()), 3);
    CHECK_EQ(orderIds[0], 100);
    CHECK_EQ(orderIds[2], 102);

    // (4) Order-wait loop: at first one unit is still marching, then all settle.
    std::vector<OrderWaitSlot> waiting = {
        {true, true, false},
        {true, true, true},          // still moving
        {true, true, false},
    };
    CHECK(AnyOrderUnitMoving(waiting));
    waiting[1].unitMoving = false;   // it arrives
    CHECK(!AnyOrderUnitMoving(waiting));

    // (5) A conquerable object (a flag) gets captured (hp -> 0). The object-def ring
    // re-find recycles the slot to the first free entry, and the active-target gate
    // now rejects the dead conquerable.
    std::vector<ObjDefEntry> ring(kObjDefCount);
    ring[4].idHigh = 555; ring[4].classByte = 2;   // a conquerable flag in slot 4
    // While alive it is the active target...
    g_scene = { {555, 10} };
    CHECK(FindActiveTarget(ring, 0u) == &g_scene[0]);
    // We also queue a highlight-clear for it during selection refresh.
    CHECK_EQ(ResetObjectHighlights(ring, 0u, /*team*/7, /*field7*/0), 1);
    CHECK_EQ(g_clears, 1);
    // ...now it dies.
    g_scene = { {555, 0} };
    CHECK(FindActiveTarget(ring, 0u) == nullptr);          // gate rejects dead flag
    CHECK_EQ(FindObjectDef(ring, 0u), 0);                  // recycles to free slot 0

    // (6) Tear down the battle UI. Selection windows, damage numbers, order-slot
    // windows, then the selection state (highlights/mesh/form).
    std::vector<i32> selWindows(kSelWindowCount, -1);
    selWindows[0] = 11; selWindows[20] = 12;
    CHECK_EQ(CloseSelectionWindows(selWindows), 2);

    std::vector<DamageNumberRecord> dmg(kDmgNumCount);
    dmg[0].widget = 1; dmg[1].widget = 2; dmg[2].widget = 3;
    CHECK_EQ(DestroyDamageNumbers(dmg), 3);

    std::vector<OrderSlotWindow> orderWins(kOrderSlotWindowCount);
    orderWins[0] = {7, 70}; orderWins[3] = {8, 80};
    CHECK_EQ(DestroyOrderSlotWindows(orderWins), 2);

    static int sUnit;
    std::vector<SelectionRecord> sel(kCombatUnitCount);
    sel[0] = {true, &sUnit, false};
    std::vector<i32> selWins2(kSelWindowCount, -1);
    selWins2[2] = 99;
    i32 form = 4242;
    SelectionResetResult sr = ResetSelectionState(sel, selWins2, form);
    CHECK_EQ(sr.highlightsCleared, 1);
    CHECK_EQ(sr.windowsClosed, 1);
    CHECK(sr.formDestroyed);
    CHECK_EQ(form, -1);

    // Every UI table is now drained.
    for (i32 w : selWindows) CHECK_EQ(w, -1);
    for (const auto& d : dmg) CHECK_EQ(d.handle, -1);
    for (const auto& o : orderWins) CHECK_EQ(o.window, -1);
    CHECK_EQ(g_drag, 1);

    SetCombatSlots3Hooks(nullptr);
}

TEST(CombatSlots3E2E, InertHooksMakeEveryLeafSafe) {
    // With no hooks installed, the iterator is empty and every side effect is inert:
    // the table walks still run their pure bookkeeping and the loops terminate.
    SetCombatSlots3Hooks(nullptr);

    std::vector<ObjDefEntry> ring(kObjDefCount);
    ring[1].idHigh = 5;                          // first free is slot 0
    CHECK_EQ(FindObjectDef(ring, 0u), 0);        // no objects -> first free
    CHECK(FindActiveTarget(ring, 0u) == nullptr);
    CHECK_EQ(ResetObjectHighlights(ring, 0u, 0, 0), 0);

    std::vector<i32> wins(kSelWindowCount, -1);
    wins[3] = 1;
    CHECK_EQ(CloseSelectionWindows(wins), 1);    // clears even with no remove hook
    CHECK_EQ(wins[3], -1);

    bool forced = false;
    CHECK_EQ(RunBattleFrameLoop(0, 0, false, 0, 0, forced), 0);
}

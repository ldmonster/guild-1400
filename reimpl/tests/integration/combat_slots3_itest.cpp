// Integration test: VIBE_Combat_ResetSelectionState (combat_slots3.cpp) wired
// against the REAL reconstructed drag-slot sibling (guild::sim::DragSlotResetTable
// on the canonical global table, sim/dragslot.cpp — NOT a mock). This is the live
// wiring: the selection-teardown body (0x48938c) calls VIBE_DragSlot_ResetTable()
// as one of its side effects, routed through CombatSlots3Hooks.dragSlotResetTable.
// Production binds that leaf to the global-table reset; we forward it into the
// genuine DragSlotResetTable and assert the cross-module flow — resetting the
// combat selection state actually drains the real carried-item drag table that
// the GUI/inventory side shares.
#include "test.h"

#include "sim/combat_slots3.h"
#include "sim/dragslot.h"      // REAL drag-slot sibling: DragSlotGlobalTable + verbs

using namespace guild;
using namespace guild::sim;

namespace {

int g_realResetReturn = 0;   // observe the REAL DragSlotResetTable return value
int g_resetCallCount  = 0;   // confirm the leaf fired exactly once

// The leaf bound to the REAL sibling. CombatSlots3Hooks.dragSlotResetTable is a
// void(*)() (no captures), so this adapter forwards into the genuine global-table
// reset overload exactly as VIBE_DragSlot_ResetTable() is wired in production.
void RealDragSlotReset() {
    ++g_resetCallCount;
    g_realResetReturn = guild::sim::DragSlotResetTable();  // real global-table reset
}

CombatSlots3Hooks MakeWiredHooks() {
    CombatSlots3Hooks h{};        // all other leaves inert (null) by default
    h.dragSlotResetTable = &RealDragSlotReset;
    return h;
}

} // namespace

// ResetSelectionState drains the REAL global drag table through the wired sibling,
// while also performing its own selection/highlight/window teardown bookkeeping.
TEST(CombatSlots3Itest, ResetDrainsRealDragTable) {
    // Populate the REAL canonical drag table with carried items.
    DragSlotTable& drag = DragSlotGlobalTable();
    DragSlotResetTable(drag);
    DragSlotAddItem(drag, /*key=*/9, /*qty=*/4);
    DragSlotAddItem(drag, /*key=*/12, /*qty=*/2);
    DragSlotAddItem(drag, /*key=*/9, /*qty=*/3);   // accumulates into the key-9 slot
    CHECK_EQ(DragSlotCountUsed(drag), 2);

    CombatSlots3Hooks hooks = MakeWiredHooks();
    SetCombatSlots3Hooks(&hooks);
    g_resetCallCount = 0;
    g_realResetReturn = 0;

    // Two highlighted selection records + a populated selection-window table + a
    // live form: the full teardown the binary performs.
    std::vector<SelectionRecord> records(3);
    records[0].highlightSet = true;
    records[2].highlightSet = true;
    std::vector<i32> selWindows = {-1, 100, -1, 200};
    i32 form = 555;

    SelectionResetResult res = ResetSelectionState(records, selWindows, form);

    // --- the REAL drag-slot sibling was invoked and drained the global table ---
    CHECK_EQ(g_resetCallCount, 1);
    CHECK_EQ(g_realResetReturn, 72);              // VIBE_DragSlot_ResetTable's result
    CHECK_EQ(DragSlotCountUsed(drag), 0);         // real table fully drained
    CHECK_EQ(drag.slots[0].key, -1);
    CHECK_EQ(drag.slots[1].key, -1);

    // --- combat_slots3's own bookkeeping over the rest of the teardown ---------
    CHECK_EQ(res.highlightsCleared, 2);
    CHECK_EQ(res.windowsClosed, 2);               // the two non(-1) windows
    CHECK(res.formDestroyed);
    CHECK_EQ(form, -1);
    CHECK(!records[0].highlightSet);
    CHECK(!records[2].highlightSet);
    CHECK_EQ(selWindows[1], -1);
    CHECK_EQ(selWindows[3], -1);

    SetCombatSlots3Hooks(nullptr);                // restore inert defaults
}

// A second reset over an already-empty real drag table is idempotent through the
// wired sibling: the leaf still fires, the real table stays empty.
TEST(CombatSlots3Itest, ResetIdempotentThroughRealSibling) {
    DragSlotTable& drag = DragSlotGlobalTable();
    DragSlotResetTable(drag);                      // start empty
    CHECK_EQ(DragSlotCountUsed(drag), 0);

    CombatSlots3Hooks hooks = MakeWiredHooks();
    SetCombatSlots3Hooks(&hooks);
    g_resetCallCount = 0;

    std::vector<SelectionRecord> records;          // nothing selected
    std::vector<i32> selWindows;                   // no windows
    i32 form = -1;                                 // no live form

    SelectionResetResult res = ResetSelectionState(records, selWindows, form);

    CHECK_EQ(g_resetCallCount, 1);                 // the real reset still ran
    CHECK_EQ(DragSlotCountUsed(drag), 0);
    CHECK_EQ(res.highlightsCleared, 0);
    CHECK_EQ(res.windowsClosed, 0);
    CHECK(!res.formDestroyed);

    SetCombatSlots3Hooks(nullptr);
}

// Without any installed hooks the leaf is the inert default (null): ResetSelectionState
// must NOT touch the real drag table, proving the wiring above is what drives it.
TEST(CombatSlots3Itest, InertDefaultLeavesRealDragTableAlone) {
    DragSlotTable& drag = DragSlotGlobalTable();
    DragSlotResetTable(drag);
    DragSlotAddItem(drag, /*key=*/7, /*qty=*/5);
    CHECK_EQ(DragSlotCountUsed(drag), 1);

    SetCombatSlots3Hooks(nullptr);                 // inert default: dragSlotResetTable == null
    std::vector<SelectionRecord> records(1);
    records[0].highlightSet = true;
    std::vector<i32> selWindows = {42};
    i32 form = -1;

    SelectionResetResult res = ResetSelectionState(records, selWindows, form);

    // The combat-side bookkeeping still runs...
    CHECK_EQ(res.highlightsCleared, 1);
    CHECK_EQ(res.windowsClosed, 1);
    // ...but the real drag table is untouched (no wired sibling fired).
    CHECK_EQ(DragSlotCountUsed(drag), 1);
    CHECK_EQ(drag.slots[0].key, 7);

    DragSlotResetTable(drag);                      // clean up the shared global
}

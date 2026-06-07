// Cross-module integration: drive the carried-item drag-slot accumulator
// (dragslot.cpp) together with the REAL inventory carried-item model
// (inventory.cpp — InventoryAdd/Remove/Find + the real slot-capacity table).
// The drag table is the transient "in-hand" accumulator; depositing it into the
// person's Inventory must respect the real per-slot capacity arithmetic, and
// the two must stay in lock-step across a full pick-up / deposit / withdraw flow.
#include "test.h"

#include "sim/dragslot.h"
#include "sim/inventory.h"   // real Inventory, InventoryAdd/Remove/Find, capacity table

using namespace guild;
using namespace guild::sim;

// Deposit everything sitting in the drag table into a real Inventory, clearing
// each drag slot as its goods land. Mirrors the binary's "drop the carried
// stack into the container" edge: drag accum -> InventoryAdd (capacity-clamped).
static void DepositDragInto(DragSlotTable& t, Inventory& inv) {
    for (int i = 0; i < kDragSlotCount; ++i) {
        if (t.slots[i].key == -1)
            continue;
        i16 type = static_cast<i16>(t.slots[i].key);
        int added = InventoryAdd(inv, type, t.slots[i].accum);
        // Remove what actually landed from the drag table (real capacity clamp).
        DragSlotRemoveItem(t, t.slots[i].key, added);
    }
}

// ===========================================================================
// Drag accumulate -> deposit into a real Inventory (uncapped goods).
// ===========================================================================
TEST(SimDragSlotItest, AccumulateThenDepositRealInventory) {
    DragSlotTable t;
    DragSlotResetTable(t);

    // Pick up good 9 (currency-class, 20*count capacity) across two drag adds.
    DragSlotAddItem(t, 9, 4);
    DragSlotAddItem(t, 9, 3);
    CHECK_EQ(DragSlotCountUsed(t), 1);
    CHECK_EQ(t.slots[0].accum, 7);

    Inventory inv{};
    DepositDragInto(t, inv);

    // The real inventory model now holds the 7 units...
    CHECK_EQ(InventoryFindStock(inv, 9), 7);
    // ...and the drag table fully drained (slot freed when accum hit 0).
    CHECK_EQ(DragSlotCountUsed(t), 0);
}

// ===========================================================================
// Deposit is clamped by the REAL slot-capacity table; the drag table keeps the
// overflow that the inventory refused.
// ===========================================================================
TEST(SimDragSlotItest, DepositClampedByRealCapacity) {
    // Good 477 (high-cap): an empty stack's fresh capacity is 5*0+10 == 10.
    ItemRec probe{};
    probe.type = kItemHighCap;     // count defaults to 0
    CHECK_EQ(InventoryGetSlotCapacity(&probe), 10);

    DragSlotTable t;
    DragSlotResetTable(t);
    DragSlotAddItem(t, kItemHighCap, 25);  // try to carry 25
    CHECK_EQ(t.slots[0].accum, 25);

    Inventory inv{};
    DepositDragInto(t, inv);

    // Only the capacity (10) lands in the real inventory...
    CHECK_EQ(InventoryFindStock(inv, kItemHighCap), 10);
    // ...and the remaining 15 stays in the drag table.
    CHECK_EQ(DragSlotCountUsed(t), 1);
    CHECK_EQ(t.slots[0].accum, 15);
}

// ===========================================================================
// Withdraw from a real Inventory back into the drag table (store/overwrite).
// ===========================================================================
TEST(SimDragSlotItest, WithdrawIntoDragStoreOverwrites) {
    Inventory inv{};
    InventoryAdd(inv, 9, 12);   // real stock
    CHECK_EQ(InventoryFindStock(inv, 9), 12);

    DragSlotTable t;
    DragSlotResetTable(t);

    // Withdraw 5 into the drag table (store = absolute set), then pull 3 more.
    int got = InventoryRemove(inv, 9, 5);
    CHECK_EQ(got, 5);
    DragSlotStoreItem(t, 9, got);
    CHECK_EQ(t.slots[0].accum, 5);

    got = InventoryRemove(inv, 9, 3);
    CHECK_EQ(got, 3);
    // Drag AddItem accumulates on top of the stored amount: 5 + 3 == 8.
    DragSlotAddItem(t, 9, got);
    CHECK_EQ(t.slots[0].accum, 8);

    // Conservation: inventory now holds 12 - 8 == 4; drag holds 8.
    CHECK_EQ(InventoryFindStock(inv, 9), 4);
    CHECK_EQ(t.slots[0].accum, 8);
}

// ===========================================================================
// Reserve-good (-1 effective) interplay: drag carries the raw count, the real
// inventory's effective-stock rule subtracts the reserved unit.
// ===========================================================================
TEST(SimDragSlotItest, ReserveGoodEffectiveStockVsDragRaw) {
    DragSlotTable t;
    DragSlotResetTable(t);
    DragSlotAddItem(t, kItemReserveA, 6);  // type 42 -> reserve 1

    Inventory inv{};
    InventoryAdd(inv, kItemReserveA, t.slots[0].accum);

    // Real effective stock is count-1 for the reserve good.
    const ItemRec* rec = InventoryFind(inv, kItemReserveA);
    CHECK(rec != nullptr);
    CHECK_EQ(InventoryFindStock(inv, kItemReserveA), 5);  // 6 raw - 1 reserve
    // The drag table tracked the RAW 6 (it has no reserve rule).
    CHECK_EQ(t.slots[0].accum, 6);
}

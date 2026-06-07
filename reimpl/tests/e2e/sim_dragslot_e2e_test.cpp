// End-to-end flow for the carried-item drag-slot module (guild::sim) —
// gilde.exe. Drives a whole "shop a stall, carry goods home, deposit, restock"
// cycle across the real sibling modules: the drag-slot accumulator (dragslot)
// and the real inventory carried-item model + capacity table (inventory).
//
// GUARDED on GUILD_GAME_DIR so the asset-backed leg stays a clean skip without
// game data; the pure table arithmetic (the whole flow here) runs regardless.
#include "test.h"

#include <cstdlib>

#include "sim/dragslot.h"
#include "sim/inventory.h"

using namespace guild;
using namespace guild::sim;

TEST(SimDragSlotE2E, ShopCarryDepositRestockCycle) {
    const bool haveAssets = std::getenv("GUILD_GAME_DIR") != nullptr;

    // ---- 1. Browse a stall: pick three goods into the drag table -----------
    DragSlotResetTable();  // canonical in-hand table
    DragSlotAddItem(11, 8);          // type 11 (uncapped, 20-cap)
    DragSlotAddItem(kItemHighCap, 30);  // type 477 high-cap (5*lvl+10 == 10 fresh)
    DragSlotAddItem(11, 4);          // accumulate onto type 11 -> 12
    CHECK_EQ(DragSlotCountUsed(), 2);
    CHECK_EQ(DragSlotGlobalTable().slots[0].accum, 12);
    CHECK_EQ(DragSlotGlobalTable().slots[1].accum, 30);

    // ---- 2. Carry home: deposit into the person's real Inventory -----------
    // Capacity-clamped through the real InventoryAdd / slot-capacity table.
    Inventory home{};
    auto& t = DragSlotGlobalTable();
    for (int i = 0; i < kDragSlotCount; ++i) {
        if (t.slots[i].key == -1) continue;
        int added = InventoryAdd(home, static_cast<i16>(t.slots[i].key), t.slots[i].accum);
        DragSlotRemoveItem(t.slots[i].key, added);
    }

    // Type 11 (cap 20): all 12 land. Type 477 (fresh cap 10): only 10 land.
    CHECK_EQ(InventoryFindStock(home, 11), 12);
    CHECK_EQ(InventoryFindStock(home, kItemHighCap), 10);
    // Drag table: type 11 drained, type 477 keeps the 20 overflow.
    CHECK_EQ(DragSlotCountUsed(), 1);
    CHECK_EQ(t.slots[1].accum, 20);

    // ---- 3. Restock from home: withdraw 5 of good 11 back into the hand ----
    int got = InventoryRemove(home, 11, 5);
    CHECK_EQ(got, 5);
    DragSlotStoreItem(11, got);  // store == absolute set
    // Now the drag table holds good 11 again (a fresh slot) and good 477.
    CHECK_EQ(DragSlotCountUsed(), 2);
    CHECK_EQ(InventoryFindStock(home, 11), 7);  // 12 - 5

    // ---- 4. Conservation invariant across the whole cycle ------------------
    // Good 11: home(7) + hand(5) == 12 originally carried in.
    int hand11 = 0;
    for (int i = 0; i < kDragSlotCount; ++i)
        if (t.slots[i].key == 11) hand11 = t.slots[i].accum;
    CHECK_EQ(InventoryFindStock(home, 11) + hand11, 12);

    // Reset the shared singleton so we don't leak state to sibling suites.
    DragSlotResetTable();

    if (!haveAssets) {
        // Asset-backed leg (real goods-table prototypes) intentionally skipped.
        return;
    }
    // With assets present the same flow would resolve real prototype ids from
    // the loaded goods table; the arithmetic above is identical either way.
}

// Unit tests for the carried-item drag-slot stacking table (gilde.exe
// dword_75B9F0 family). Golden vectors computed from the decompiled logic with
// python3 (see the agent report). Suites prefixed SimDragSlot* to avoid clashes.
#include "sim/dragslot.h"

#include "tests/framework/test.h"

using namespace guild;
using namespace guild::sim;

// A fresh, locally-owned table so tests don't clobber the global singleton.
static DragSlotTable FreshTable() {
    DragSlotTable t;
    DragSlotResetTable(t);
    return t;
}

// ---------------------------------------------------------------------------
// ResetTable / CountUsed.
// ---------------------------------------------------------------------------
TEST(SimDragSlot, ResetClearsAllSlots) {
    DragSlotTable t;
    // Dirty every slot first.
    for (int i = 0; i < kDragSlotCount; ++i) { t.slots[i].key = i; t.slots[i].accum = 99; }
    CHECK_EQ(DragSlotResetTable(t), 72);  // original returns result*4 with result==18
    for (int i = 0; i < kDragSlotCount; ++i) {
        CHECK_EQ(t.slots[i].key, -1);
        CHECK_EQ(t.slots[i].accum, 0);
    }
    CHECK_EQ(DragSlotCountUsed(t), 0);
}

// ---------------------------------------------------------------------------
// AddItem — accumulate / first-free / no-op / full.
// ---------------------------------------------------------------------------
TEST(SimDragSlot, AddItemGoldenVector) {
    DragSlotTable t = FreshTable();

    // add(100,5) -> slot0, dword index 0
    CHECK_EQ(DragSlotAddItem(t, 100, 5), 0);
    CHECK_EQ(t.slots[0].key, 100);
    CHECK_EQ(t.slots[0].accum, 5);

    // add(200,3) -> first free slot1, dword index 3
    CHECK_EQ(DragSlotAddItem(t, 200, 3), 3);
    CHECK_EQ(t.slots[1].key, 200);
    CHECK_EQ(t.slots[1].accum, 3);

    // add(100,7) -> existing slot0 accumulates to 12, dword index 0
    CHECK_EQ(DragSlotAddItem(t, 100, 7), 0);
    CHECK_EQ(t.slots[0].accum, 12);

    CHECK_EQ(DragSlotCountUsed(t), 2);

    // qty==0 is a no-op and returns the key register unchanged (300).
    CHECK_EQ(DragSlotAddItem(t, 300, 0), 300);
    CHECK_EQ(DragSlotCountUsed(t), 2);
}

TEST(SimDragSlot, AddItemFullTableReturnsSix) {
    DragSlotTable t = FreshTable();
    for (int k = 0; k < kDragSlotCount; ++k)
        CHECK_EQ(DragSlotAddItem(t, 1000 + k, 1), 3 * k);
    CHECK_EQ(DragSlotCountUsed(t), 6);
    // Table full, new key -> no write, returns 6.
    CHECK_EQ(DragSlotAddItem(t, 9999, 1), 6);
    CHECK_EQ(DragSlotCountUsed(t), 6);
    // But an EXISTING key still accumulates even when full.
    CHECK_EQ(DragSlotAddItem(t, 1003, 4), 3 * 3);
    CHECK_EQ(t.slots[3].accum, 5);
}

// ---------------------------------------------------------------------------
// RemoveItem — subtract, free on zero, byte-offset return.
// ---------------------------------------------------------------------------
TEST(SimDragSlot, RemoveItemGoldenVector) {
    DragSlotTable t = FreshTable();
    DragSlotAddItem(t, 100, 10);  // slot0
    DragSlotAddItem(t, 200, 9);   // slot1
    DragSlotAddItem(t, 400, 4);   // slot2

    // remove(100,2) -> slot0 acc 8, byte offset 0
    CHECK_EQ(DragSlotRemoveItem(t, 100, 2), 0);
    CHECK_EQ(t.slots[0].accum, 8);
    CHECK_EQ(t.slots[0].key, 100);

    // remove(200,9) -> slot1 hits 0 and is freed, byte offset 12
    CHECK_EQ(DragSlotRemoveItem(t, 200, 9), 12);
    CHECK_EQ(t.slots[1].key, -1);
    CHECK_EQ(t.slots[1].accum, 0);
    CHECK_EQ(DragSlotCountUsed(t), 2);

    // remove(400, 1) -> slot2 acc 3, byte offset 24
    CHECK_EQ(DragSlotRemoveItem(t, 400, 1), 24);
    CHECK_EQ(t.slots[2].accum, 3);
}

TEST(SimDragSlot, RemoveItemAbsentScansFullTable) {
    DragSlotTable t = FreshTable();
    DragSlotAddItem(t, 1000, 5);  // only slot0 used
    // Absent key (and != slot0 key) -> scan runs the whole table, returns 72.
    CHECK_EQ(DragSlotRemoveItem(t, 7777, 5), 72);
    CHECK_EQ(t.slots[0].accum, 5);  // unchanged
}

// ---------------------------------------------------------------------------
// StoreItem — overwrite (not accumulate), free on qty==0, slot-index return.
// ---------------------------------------------------------------------------
TEST(SimDragSlot, StoreItemGoldenVector) {
    DragSlotTable t = FreshTable();
    DragSlotAddItem(t, 100, 12);  // slot0
    DragSlotAddItem(t, 200, 3);   // slot1

    // store(200,9) OVERWRITES slot1 to 9 (not 3+9), returns slot index 1.
    CHECK_EQ(DragSlotStoreItem(t, 200, 9), 1);
    CHECK_EQ(t.slots[1].accum, 9);

    // store(400,4) -> first free slot2, returns 2.
    CHECK_EQ(DragSlotStoreItem(t, 400, 4), 2);
    CHECK_EQ(t.slots[2].key, 400);
    CHECK_EQ(t.slots[2].accum, 4);

    // store(500,0): finds a free slot, writes key then frees it (qty==0).
    int idx = DragSlotStoreItem(t, 500, 0);
    CHECK_EQ(t.slots[idx].key, -1);
    CHECK_EQ(t.slots[idx].accum, 0);
}

TEST(SimDragSlot, StoreItemFullTableReturnsSix) {
    DragSlotTable t = FreshTable();
    for (int k = 0; k < kDragSlotCount; ++k)
        DragSlotStoreItem(t, 2000 + k, k + 1);
    CHECK_EQ(DragSlotCountUsed(t), 6);
    CHECK_EQ(DragSlotStoreItem(t, 9999, 5), 6);  // full -> 6, no write
    // Existing key overwrites in place.
    CHECK_EQ(DragSlotStoreItem(t, 2002, 50), 2);
    CHECK_EQ(t.slots[2].accum, 50);
}

// ---------------------------------------------------------------------------
// Global singleton overloads route to the canonical table.
// ---------------------------------------------------------------------------
TEST(SimDragSlot, GlobalOverloadsUseSingleton) {
    DragSlotResetTable();
    CHECK_EQ(DragSlotCountUsed(), 0);
    CHECK_EQ(DragSlotAddItem(700, 6), 0);
    CHECK_EQ(DragSlotGlobalTable().slots[0].accum, 6);
    CHECK_EQ(DragSlotCountUsed(), 1);
    DragSlotResetTable();  // leave the singleton clean for siblings.
}

#include "sim/inventory.h"

#include <climits>
#include <cstring>

// Faithful 1:1 port of the inventory item-record rules from gilde.exe, plus a
// small carried-item model that applies the same effective-stock / capacity
// arithmetic. The pure functions (effective stock, slot capacity, UI slot scan)
// are verbatim; the add/remove/find collection helpers reuse those rules so the
// observable stock/capacity behavior matches the binary.

namespace guild::sim {

// Read the count dword at +0x0E (unaligned in the original).
static int ItemCount(const ItemRec* rec) {
    int v;
    std::memcpy(&v, reinterpret_cast<const u8*>(rec) + 0x0E, sizeof(v));
    return v;
}
static void SetItemCount(ItemRec* rec, int v) {
    std::memcpy(reinterpret_cast<u8*>(rec) + 0x0E, &v, sizeof(v));
}

// True for the goods that reserve one unit (effective stock = count - 1).
static bool IsReserveGood(i16 type) {
    return type == kItemReserveA || type == kItemReserveB
        || type == kItemReserveC || type == kItemReserveD;
}

// ===========================================================================
// VIBE_Inventory_GetEffectiveStock  0x5923fc
//   v2 = *a1;  // type word from the type ptr
//   if (v2==42||v2==278||v2==475||v2==476) return *(DWORD*)(a2+14) - 1;
//   else                                    return *(DWORD*)(a2+14);
// ===========================================================================
int InventoryGetEffectiveStock(const ItemRec* typeRec, const ItemRec* rec) {
    int count = ItemCount(rec);
    return IsReserveGood(typeRec->type) ? count - 1 : count;
}

int InventoryGetEffectiveStock(const ItemRec* rec) {
    return InventoryGetEffectiveStock(rec, rec);
}

// ===========================================================================
// VIBE_Inventory_GetSlotCapacity  0x592474
//   if (*(WORD*)a1 == 477) return 5*count + 10;
//   v1 = count;
//   if (v1 == 3) return 80; else return 20*v1;
// ===========================================================================
int InventoryGetSlotCapacity(const ItemRec* rec) {
    int count = ItemCount(rec);
    if (rec->type == kItemHighCap)
        return 5 * count + 10;
    if (count == 3)
        return 80;
    return 20 * count;
}

// ===========================================================================
// VIBE_Inventory_FindSlotByItemId  0x54f04c
//   if (!word_63D1D8[0]) return 0;
//   while (slot[i].type != a1) { if (!nextMarker) return 0; i += 24; }
//   return &slot[i];
// The terminator is the *next* slot's marker word being zero (word_63D1F0[i]).
// We model slots as an InvGridSlot array; the scan stops when it would step past
// a zero-marker successor or the capacity bound.
// ===========================================================================
InvGridSlot* InventoryFindSlotByItemId(const InvGridSlot* slots, int capacity,
                                       i16 itemType) {
    if (capacity <= 0 || slots[0].type == 0)
        return nullptr;
    int i = 0;
    while (slots[i].type != itemType) {
        // word_63D1F0[v2/2] is the successor slot's marker word; 0 terminates.
        bool hasNext = (i + 1 < capacity) && slots[i + 1].type != 0;
        ++i;
        if (!hasNext)
            return nullptr;
    }
    return const_cast<InvGridSlot*>(&slots[i]);
}

// ===========================================================================
// VIBE_Inventory_FindSlotIndexByItemId  0x54f090  (as above + index out).
// ===========================================================================
InvGridSlot* InventoryFindSlotIndexByItemId(const InvGridSlot* slots,
                                            int capacity, i16 itemType,
                                            int* outIndex) {
    if (capacity <= 0 || slots[0].type == 0)
        return nullptr;
    int i = 0;
    while (slots[i].type != itemType) {
        bool hasNext = (i + 1 < capacity) && slots[i + 1].type != 0;
        ++i;
        if (!hasNext)
            return nullptr;
    }
    if (outIndex)
        *outIndex = i;
    return const_cast<InvGridSlot*>(&slots[i]);
}

// ===========================================================================
// Carried-item model.
// ===========================================================================
static InventoryCommandFn g_cmdHook = nullptr;
void InventorySetCommandHook(InventoryCommandFn fn) { g_cmdHook = fn; }

const ItemRec* InventoryFind(const Inventory& inv, i16 itemType) {
    for (int i = 0; i < inv.itemCount; ++i)
        if (inv.items[i].type == itemType)
            return &inv.items[i];
    return nullptr;
}
ItemRec* InventoryFind(Inventory& inv, i16 itemType) {
    return const_cast<ItemRec*>(
        InventoryFind(static_cast<const Inventory&>(inv), itemType));
}

int InventoryFindStock(const Inventory& inv, i16 itemType) {
    const ItemRec* rec = InventoryFind(inv, itemType);
    if (!rec)
        return 0;
    return InventoryGetEffectiveStock(rec);
}

// VIBE_Inventory_ComputeFreeSpaceForItem 0x59266c (rules core):
//   cap = slotCapacity(item);  free = cap - count;  return min(free, ceiling).
int InventoryFreeSpace(const Inventory& inv, i16 itemType, int cap) {
    const ItemRec* rec = InventoryFind(inv, itemType);
    if (!rec) {
        // Empty stack: a fresh stack of this type would have full slot capacity.
        ItemRec probe{};
        probe.type = itemType;
        SetItemCount(&probe, (itemType == kItemHighCap) ? 0 : 1);
        int full = InventoryGetSlotCapacity(&probe);
        return full < cap ? full : cap;
    }
    int free = InventoryGetSlotCapacity(rec) - ItemCount(rec);
    if (free < 0)
        free = 0;
    return free < cap ? free : cap;
}

// Locate-or-create a stack for `itemType`.
static ItemRec* GetOrCreate(Inventory& inv, i16 itemType) {
    ItemRec* rec = InventoryFind(inv, itemType);
    if (rec)
        return rec;
    if (inv.itemCount >= Inventory::kMaxItems)
        return nullptr;
    ItemRec& slot = inv.items[inv.itemCount++];
    slot = ItemRec{};
    slot.type = itemType;
    SetItemCount(&slot, 0);
    return &slot;
}

int InventoryAdd(Inventory& inv, i16 itemType, int qty) {
    if (qty <= 0)
        return 0;
    int room = InventoryFreeSpace(inv, itemType, INT_MAX);
    int add = qty < room ? qty : room;
    if (add <= 0)
        return 0;
    if (g_cmdHook)
        g_cmdHook(inv, itemType, add);
    ItemRec* rec = GetOrCreate(inv, itemType);
    if (!rec)
        return 0;
    SetItemCount(rec, ItemCount(rec) + add);
    return add;
}

int InventoryRemove(Inventory& inv, i16 itemType, int qty) {
    if (qty <= 0)
        return 0;
    ItemRec* rec = InventoryFind(inv, itemType);
    if (!rec)
        return 0;
    int have = ItemCount(rec);
    int rem = qty < have ? qty : have;
    if (rem <= 0)
        return 0;
    if (g_cmdHook)
        g_cmdHook(inv, itemType, -rem);
    SetItemCount(rec, have - rem); // record kept even at count 0 (binary semantics)
    return rem;
}

} // namespace guild::sim

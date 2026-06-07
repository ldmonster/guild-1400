#pragma once
// Inventory / item system for the Guild simulation (gilde.exe).
//
// The carried-item / stock model: each item is a scene-entity record whose only
// inventory-relevant fields are the type word (+0) and the count dword (+0x0E)
// (see ItemRec in types.h). This module ports the pure per-record rules:
//   - effective stock (some goods reserve 1 unit, see kItemReserve*),
//   - per-slot capacity (good 477 scales with its level; level 3 caps at 80),
// and provides a faithful add/remove/find model over an item collection. The
// originals scan/aggregate items through the GameObject scene-tree query
// (VIBE_GameObject_QueryFind) and mutate counts through the command lockstep;
// we model the item set directly and route mutations through a mockable command
// hook (forward-declared) so the rules are testable in isolation.
//
// The inventory GRID UI (VIBE_Inventory_OpenSlotWindow / RefreshSlots /
// RenderItemGrid / *GridSurface, and the runtime UI slot table word_63D1D8) is
// widget-coupled and is LISTED AS DEFERRED. FindSlotByItemId/FindSlotIndexByItemId
// over that UI table are provided here as a faithful scan over an InvGridSlot
// array since they are pure table walks.
//
// Translated functions:
//   VIBE_Inventory_GetEffectiveStock      0x5923fc
//   VIBE_Inventory_GetSlotCapacity        0x592474
//   VIBE_Inventory_FindSlotByItemId       0x54f04c
//   VIBE_Inventory_FindSlotIndexByItemId  0x54f090
#include "guild/common/types.h"
#include "sim/types.h"

namespace guild::sim {

// gilde.exe 0x5923fc — VIBE_Inventory_GetEffectiveStock (__usercall: eax=ret,
// eax=(typePtr@eax), edx=(record@edx)). Returns the usable stock: count (+0x0E)
// of `rec`, minus 1 for the reserve goods (42/278/475/476), where the type id is
// taken from `typeRec` (the original reads the type word from a separate ptr).
int InventoryGetEffectiveStock(const ItemRec* typeRec, const ItemRec* rec);

// Convenience: effective stock of a single item record (type and count from the
// same record — the common case).
int InventoryGetEffectiveStock(const ItemRec* rec);

// gilde.exe 0x592474 — VIBE_Inventory_GetSlotCapacity (__usercall: eax=ret,
// eax=(record@eax)). Per-slot capacity by item type:
//   type 477 -> 5*count + 10;  count==3 -> 80;  else -> 20*count.
// (count is the level/grade dword at +0x0E.)
int InventoryGetSlotCapacity(const ItemRec* rec);

// gilde.exe 0x54f04c — VIBE_Inventory_FindSlotByItemId (__usercall: eax=ret,
// ax=(itemType@ax)). Linear scan of the UI grid slot table for the slot whose
// type word matches `itemType`; the scan terminates at the first slot whose
// successor marker word is 0. Returns the slot or nullptr.
InvGridSlot* InventoryFindSlotByItemId(const InvGridSlot* slots, int capacity,
                                       i16 itemType);

// gilde.exe 0x54f090 — VIBE_Inventory_FindSlotIndexByItemId. As above, but also
// writes the slot index to *outIndex (if non-null).
InvGridSlot* InventoryFindSlotIndexByItemId(const InvGridSlot* slots,
                                            int capacity, i16 itemType,
                                            int* outIndex);

// --- Carried-item model (faithful rules over a small item collection) --------
//
// A lightweight stand-in for a person/container's item set. The originals keep
// these as scene-tree children; we hold them in a flat vector with the same
// type/count semantics so add/remove/find/capacity match the binary's arithmetic.
struct Inventory {
    static constexpr int kMaxItems = 64;
    ItemRec items[kMaxItems] = {};
    int     itemCount = 0;

    // Per-stack capacity context: the inventory's nominal slot capacity used by
    // ComputeFreeCapacity. The originals derive it from the container's own
    // capacity record; we store it explicitly (default: unlimited == INT_MAX).
};

// Mutation command hook (the originals route stock changes through the lockstep
// command system, e.g. ExSetObjectField). Tests install a mock; nullptr applies
// the change directly. Called as cmdHook(inv, itemType, delta) BEFORE the local
// apply so a real backend can veto / serialize.
using InventoryCommandFn = void (*)(Inventory& inv, i16 itemType, int delta);
void InventorySetCommandHook(InventoryCommandFn fn);

// Find the item stack of `itemType` in `inv` (nullptr if absent). Mirrors the
// type-match scan used by FindItemStock / the QueryFind item lookups.
ItemRec* InventoryFind(Inventory& inv, i16 itemType);
const ItemRec* InventoryFind(const Inventory& inv, i16 itemType);

// Effective stock of `itemType` in `inv` (0 if absent), applying the reserve-1
// rule (VIBE_Inventory_GetEffectiveStock semantics).
int InventoryFindStock(const Inventory& inv, i16 itemType);

// Free space remaining for `itemType`: slotCapacity - effectiveStock, clamped to
// the caller's `cap` ceiling (mirrors ComputeFreeSpaceForItem's min/clamp).
int InventoryFreeSpace(const Inventory& inv, i16 itemType, int cap);

// Add `qty` of `itemType` (creating the stack if needed). Returns the amount
// actually added (clamped to free space). Routes through the command hook.
int InventoryAdd(Inventory& inv, i16 itemType, int qty);

// Remove up to `qty` of `itemType`. Returns the amount actually removed. Routes
// through the command hook. Empties (count 0) leave the stack in place (count 0),
// matching the binary which keeps the record and zeroes its count.
int InventoryRemove(Inventory& inv, i16 itemType, int qty);

} // namespace guild::sim

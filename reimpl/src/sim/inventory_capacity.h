#pragma once
// ===========================================================================
// inventory_capacity.{h,cpp} — the inventory/stock CAPACITY engines (gilde.exe)
// ===========================================================================
// MODULE: the Inventory/Item capacity remainder (namespace guild::sim).
//
// This file ports the per-container CAPACITY/FREE-SPACE arithmetic the binary
// runs when goods move between containers (a storage room, a workstation, a
// carried-good slot). Every container in the original is a scene-entity node
// (the 67-byte GameObject record, see object.h) whose stock children are queried
// through VIBE_GameObject_QueryFind. The capacity rules read TWO things off a
// container/stock node:
//   * the item TYPE word at +0  (42/278/475/476 reserve one unit; 477 high-cap),
//   * the LEVEL/grade dword at +0x0E (== object record field +14, "Menge"/level).
//
// The per-slot capacity table (recovered byte-for-byte from GetSlotCapacity and
// inlined into every engine below):
//      type 477  -> 5*level + 10
//      level 3   -> 80
//      else      -> 20*level
// and the effective-stock reserve rule (type in {42,278,475,476} -> count-1).
//
// Because the originals walk the global scene tree, the engines take an explicit
// CONTAINER VIEW (the container's own type/level + its queryable stock children)
// so the arithmetic is byte-faithful AND unit-testable without the whole sim.
// The view reuses the object module's stock semantics; the binary's QueryFind /
// IterNext child scans become walks over the view's child list.
//
// Translated functions:
//   VIBE_Inventory_FindItemStock          0x592428
//   VIBE_Inventory_GetSlotCapacityRaw     0x592474 (reused from inventory.h)
//   VIBE_Inventory_ComputeFreeSpaceForItem 0x59266c
//   VIBE_Inventory_ComputeFreeCapacity    0x5924a8
//   VIBE_Inventory_ComputeCarryCapacity   0x592710
//   VIBE_Inventory_CollectStorageSlots    0x590fc0
//   VIBE_Inventory_CollectWorkstationSlots 0x590df8
#include <vector>

#include "guild/common/types.h"
#include "sim/types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// A stock child of a container: the inventory-relevant header of a scene node.
//   type  : node +0   (item type id)
//   level : node +0x0E (the +14 "Menge"/level dword; capacity & reserve key)
// Mirrors the (*node, *(node+7) as dword == node+14) pair the engines read.
// ---------------------------------------------------------------------------
struct StockChild {
    i16 type = 0;
    i32 level = 0;
};

// ---------------------------------------------------------------------------
// A container view: its own type/level header (the `a1` capacity record), the
// per-category fill counts the originals read at container+28 / +29, and its
// stock children. Engines never mutate the view; they read it exactly as the
// binary reads the scene node.
//   selfType   : container node +0
//   selfLevel  : container node +0x0E (+14)
//   fill28     : container byte +28 (max-slot count A; Collect* / FreeCapacity)
//   fill29     : container byte +29 (max-slot count B)
//   children   : queryable stock children
// ---------------------------------------------------------------------------
struct ContainerView {
    i16 selfType = 0;
    i32 selfLevel = 0;
    u8  fill28 = 0;
    u8  fill29 = 0;
    std::vector<StockChild> children;
};

// gilde.exe 0x592474 — per-slot capacity for a (type, level) pair (the table
// inlined into every engine). Public so tests can reference the golden table.
//   type 477 -> 5*level + 10;  level 3 -> 80;  else -> 20*level.
int InventorySlotCapacity(i16 type, i32 level);

// gilde.exe — effective stock of a (type, count) pair (reserve-1 rule):
//   type in {42,278,475,476} -> count-1; else count. (Shared by the engines.)
int InventoryEffectiveStock(i16 type, i32 count);

// gilde.exe 0x592428 — VIBE_Inventory_FindItemStock (eax=container, dx=type).
// QueryFind the child stack of `type`; returns its effective stock (reserve-1
// applied) or 0 if absent. (Original returns the raw count for a missing stack
// via an uninitialised reg; we return 0, the observable "no stock" value.)
int InventoryFindItemStock(const ContainerView& c, i16 type);

// gilde.exe 0x59266c — VIBE_Inventory_ComputeFreeSpaceForItem
//   (eax=container, dx=type, ebx=ceiling).
// Free room in `c`'s slot for items of `type`, clamped to `ceiling`:
//   cap = SlotCapacity(c.selfType, c.selfLevel);
//   if a stock child of `type` exists: free = cap - child.level;
//       return (free <= ceiling) ? free : ceiling;
//   else: if (childCount >= c.fill28) return 0;
//         return (cap < ceiling) ? cap : ceiling;
int InventoryComputeFreeSpaceForItem(const ContainerView& c, i16 type,
                                     int ceiling);

// gilde.exe 0x5924a8 — VIBE_Inventory_ComputeFreeCapacity
//   (eax=stockRec, dx=proto, ecx=container-id, ebx=ceiling).
// Resolves the root container (modeled as `c`), computes the slot capacity from
// the STOCK RECORD's own type/level (a1+0 / a1+14), then:
//   * if a child stack of `proto` exists: free = cap - effectiveStock(child);
//     return min(free, ceiling).
//   * else for type 42: count children (filter 5); if both fill gates exceeded
//     return 0, else return min(cap, ceiling).
//   * else (non-42): count children; if count < fill28 return min(cap,ceiling)
//     else 0.
// `rootResolved` mirrors the ResolveRootContainer null-guard (false -> 0).
int InventoryComputeFreeCapacity(const StockChild& stockRec,
                                 const ContainerView& container, i16 proto,
                                 int ceiling, bool rootResolved);

// gilde.exe 0x592710 — VIBE_Inventory_ComputeCarryCapacity
//   (eax=person, dx=proto, ebx=ceiling).
// Carry-room for a carried good on a PERSON. `personKind` is person+2 (6/7 ==
// an avatar-bearing actor), `hasAvatar` mirrors VIBE_Avatar_LookupById!=0,
// `typeCategory` is the goods type-def category byte (dword_13CE27C+65*proto+0;
// ==9 -> unlimited carry). `carriable` mirrors the type-class membership test
// (the original's v16). The carried-stack child (if present) supplies its level.
//   * category 9                       -> return ceiling (money: unlimited)
//   * proto 377/378 or !carriable      -> 0
//   * (kind 6/7 && hasAvatar)          -> 0
//   * if a carried child exists: room = 3 - child.level; (ceiling+level>3 path)
//   * else: if childCount >= 6 -> use room; if ceiling<=3 -> ceiling; else 3.
// Returns max(room, 0) per the original's v4>=0 guard.
int InventoryComputeCarryCapacity(const ContainerView& carried, i16 proto,
                                  int ceiling, u8 personKind, bool hasAvatar,
                                  int typeCategory, bool carriable);

// ---------------------------------------------------------------------------
// Collected slot table (the output struct the Collect* engines fill). Mirrors
// the destination record fields the originals write:
//   count   : dword at +4   (number of collected slots)
//   types   : word  at +72  (per-slot item type word)   -> 2*i
//   caps    : dword at +8   (per-slot capacity)          -> +8 + 4*i
//   stocks  : dword at +104 (per-slot effective stock)   -> +104 + 4*i
//   sourceNode is the +180 "source room node" the engine stashes.
// ---------------------------------------------------------------------------
struct CollectedSlots {
    int count = 0;
    std::vector<i16> types;
    std::vector<i32> caps;
    std::vector<i32> stocks;
};

// gilde.exe 0x590fc0 — VIBE_Inventory_CollectStorageSlots (eax=person,
//   edx=out, ebx=mode). Resolves the person's storage building (the 253-typed
//   container holding a 42/278 storage room), picks the cap from the room's
//   fill byte (+28 for 278, else +29; mode picks +28/+29), then iterates the
//   room's stock children, keeping those whose type-def category is 23 when
//   mode!=0 (or != 23 when mode==0), and records (effStock, slotCap, type).
// `room` is the resolved storage room view; `roomTypeIs278` is *room==278;
// `childCategory(i)` supplies the type-def category byte for child i.
// Returns 1 on success (room found), 0 otherwise; fills `out`.
int InventoryCollectStorageSlots(const ContainerView& room, bool roomTypeIs278,
                                 int mode, const std::vector<u8>& childCategory,
                                 CollectedSlots& out);

// gilde.exe 0x590df8 — VIBE_Inventory_CollectWorkstationSlots. As above but the
// category test admits BOTH 23 and 37 (workstation rooms) and the mode==0 path
// rejects neither; otherwise identical capacity/stock recording.
int InventoryCollectWorkstationSlots(const ContainerView& room,
                                     bool roomTypeIs278, int mode,
                                     const std::vector<u8>& childCategory,
                                     CollectedSlots& out);

}  // namespace guild::sim

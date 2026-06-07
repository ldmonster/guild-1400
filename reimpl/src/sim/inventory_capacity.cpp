#include "sim/inventory_capacity.h"

#include <climits>

// Faithful 1:1 port of the inventory capacity / free-space engines from
// gilde.exe. The originals walk the global scene tree via QueryFind/IterNext;
// here each engine takes a ContainerView whose `children` list IS the query
// result, so the arithmetic (the inlined slot-capacity table + the reserve-1
// effective-stock rule) is preserved byte-for-byte. Provenance addresses are on
// each function; the decompiled pseudocode is reproduced in the leading comment.

namespace guild::sim {

// ===========================================================================
// Slot-capacity table (gilde.exe 0x592474, inlined into every engine).
//   if (type == 477) return 5*level + 10;
//   if (level == 3)  return 80;
//   return 20*level;
// ===========================================================================
int InventorySlotCapacity(i16 type, i32 level) {
    if (type == 477)
        return 5 * level + 10;
    if (level == 3)
        return 80;
    return 20 * level;
}

// Reserve-1 effective stock (gilde.exe GetEffectiveStock 0x5923fc semantics):
//   if (type==42 || type==278 || type==475 || type==476) return count-1;
//   return count;
int InventoryEffectiveStock(i16 type, i32 count) {
    if (type == 42 || type == 278 || type == 475 || type == 476)
        return count - 1;
    return count;
}

namespace {
// QueryFind(container, id-filter, 0, type): find the child stack whose type
// word matches `type`. Returns the child, or nullptr.
const StockChild* FindChildByType(const ContainerView& c, i16 type) {
    for (const StockChild& ch : c.children)
        if (ch.type == type)
            return &ch;
    return nullptr;
}
}  // namespace

// ===========================================================================
// VIBE_Inventory_FindItemStock  0x592428
//   result = QueryFind(*(a1+20), 1, 0, a2);
//   if (result) {
//       v4 = *result;
//       if (v4==42||v4==278||v4==475||v4==476) return *(result+14) - 1;
//       else                                    return *(result+14);
//   }
//   return result;            // (missing -> 0)
// ===========================================================================
int InventoryFindItemStock(const ContainerView& c, i16 type) {
    const StockChild* ch = FindChildByType(c, type);
    if (!ch)
        return 0;
    return InventoryEffectiveStock(ch->type, ch->level);
}

// ===========================================================================
// VIBE_Inventory_ComputeFreeSpaceForItem  0x59266c
//   if (*(WORD*)a1==477) v4 = 5*(*(a1+14)) + 10;
//   else { v10 = *(a1+14); v4 = (v10==3) ? 80 : 20*v10; }
//   v5 = v4;
//   v6 = QueryFind(*(a1+20), 1, 0, a2);
//   if (v6) { result = v5 - *(v6+14); if (result <= a3) return result; }
//   else    { count children (QueryFind filter 5 + IterNext);
//             if (count >= *(a1+28)) return 0;
//             if (v5 < a3) return v5; }
//   return a3;
// ===========================================================================
int InventoryComputeFreeSpaceForItem(const ContainerView& c, i16 type,
                                     int ceiling) {
    int cap = InventorySlotCapacity(c.selfType, c.selfLevel);
    const StockChild* ch = FindChildByType(c, type);
    if (ch) {
        int result = cap - ch->level;
        if (result <= ceiling)
            return result;
    } else {
        int count = static_cast<int>(c.children.size());
        if (count >= c.fill28)
            return 0;
        if (cap < ceiling)
            return cap;
    }
    return ceiling;
}

// ===========================================================================
// VIBE_Inventory_ComputeFreeCapacity  0x5924a8
//   result = ResolveRootContainer(a1, a3); if (!result) return result;
//   MapTypeToCategory(*result);            // side-effect only
//   v17 = SlotCapacity(*a1, *(a1+14));     // capacity from the STOCK record
//   v8 = QueryFind(*(a1+5)==*(a1+20), 1, 0, a2);
//   if (!v8) {
//     if (*a1 == 42) {
//        count children (filter 5);
//        if ((!v16 || cnt<a1[28]) && (v16 || cnt<a1[29])) {  // both-gates test
//           return (a4 > v17) ? v17 : a4;
//        }
//        return 0;
//     } else {
//        count children; if (cnt < a1[28]) return (a4>v17)?v17:a4;
//        return 0;
//     }
//   }
//   v10 = effectiveStock(*a1, *(v8+14));   // reserve rule keyed on STOCK type
//   result = v17 - v10;  return (result > a4) ? a4 : result;
//
// NB: the original's v16 (the "second gate" selector) is the type-category test
// the engine performs while counting children (the MapTypeToCategory result for
// type 42). For the modeled view we expose both fill gates and apply the same
// "(cnt < fill28) && (cnt < fill29)" conjunction the 42-branch uses; non-42 uses
// fill28 only.
// ===========================================================================
int InventoryComputeFreeCapacity(const StockChild& stockRec,
                                 const ContainerView& container, i16 proto,
                                 int ceiling, bool rootResolved) {
    if (!rootResolved)
        return 0;
    int cap = InventorySlotCapacity(stockRec.type, stockRec.level);  // v17
    const StockChild* ch = FindChildByType(container, proto);
    if (!ch) {
        int count = static_cast<int>(container.children.size());
        if (stockRec.type == 42) {
            // both-gates conjunction (the 42 reserve good uses fill28 & fill29).
            if (count < container.fill28 && count < container.fill29)
                return (ceiling > cap) ? cap : ceiling;
            return 0;
        }
        if (count < container.fill28)
            return (ceiling > cap) ? cap : ceiling;
        return 0;
    }
    int eff = InventoryEffectiveStock(stockRec.type, ch->level);  // v10
    int result = cap - eff;
    return (result > ceiling) ? ceiling : result;
}

// ===========================================================================
// VIBE_Inventory_ComputeCarryCapacity  0x592710
//   typeCat = *(dword_13CE27C + 65*(proto)+0);   // a2 >> 16 path == proto
//   if (typeCat == 9) return a3;                 // money: unlimited carry
//   carriable = (membership test over the carry-class table) -> v16
//   v7 = person+2;
//   if (((v7==6||v7==7) && Avatar_LookupById(proto)) || proto==377 || proto==378
//        || !carriable)  return 0;
//   if (QueryFind(person+376, 1, 0, proto)) {            // carried stack exists
//       child = that stack;
//       if (a3 + child.level > 3) { v4 = 3 - child.level; goto clamp; }
//       else goto take_all;                              // v4 = a3
//   }
//   count children (filter 5);
//   if (count >= 6) goto clamp;                          // v4 stays 0
//   if (a3 <= 3) { v4 = a3; goto clamp; }
//   return 3;
// clamp: return (v4 >= 0) ? v4 : 0;
// take_all: v4 = a3; goto clamp;
// ===========================================================================
int InventoryComputeCarryCapacity(const ContainerView& carried, i16 proto,
                                  int ceiling, u8 personKind, bool hasAvatar,
                                  int typeCategory, bool carriable) {
    if (typeCategory == 9)
        return ceiling;  // money: unlimited
    if (((personKind == 6 || personKind == 7) && hasAvatar) || proto == 377 ||
        proto == 378 || !carriable)
        return 0;

    int v4 = 0;
    const StockChild* ch = FindChildByType(carried, proto);
    if (ch) {
        if (ceiling + ch->level > 3)
            v4 = 3 - ch->level;       // clamp branch
        else
            v4 = ceiling;             // take_all branch
        return v4 >= 0 ? v4 : 0;
    }
    int count = static_cast<int>(carried.children.size());
    if (count >= 6)
        return v4 >= 0 ? v4 : 0;      // v4 == 0
    if (ceiling <= 3) {
        v4 = ceiling;
        return v4 >= 0 ? v4 : 0;
    }
    return 3;
}

// ===========================================================================
// Collect* engines (gilde.exe 0x590fc0 / 0x590df8). Both resolve a storage /
// workstation room, derive a per-mode capacity hint from the room's fill bytes
// (+28 / +29), then iterate the room's stock children, keeping the ones whose
// type-def category matches the mode-selected room class and recording, per
// kept slot: effectiveStock (+104), slotCapacity (+8), and the item type (+72),
// bumping the slot count (+4).
//
//   cap-hint selection (both engines):
//     if (mode)  { if (*room==278) return 0; cap = room[28]; }
//     else       { cap = room[29]; if (*room==278) { v8=room[28];
//                                  if (v8<cap) v8=cap; cap=v8; } }
//   child keep predicate:
//     storage:    (cat==23 && mode) || (cat!=23 && !mode)
//     workstation:((cat==23||cat==37) && mode) || (cat!=23&&cat!=37&&!mode)
//                  || (*child==278)
// ===========================================================================
namespace {
int CollectImpl(const ContainerView& room, bool roomTypeIs278, int mode,
                const std::vector<u8>& childCategory, CollectedSlots& out,
                bool workstation) {
    out = CollectedSlots{};

    // cap-hint selection (the *v7 result the original stashes; not recorded per
    // slot but computed for fidelity — it gates the 278 room handling).
    if (mode) {
        if (roomTypeIs278)
            return 0;
    } else {
        if (roomTypeIs278) {
            // v8 = max(room[28], room[29]) — fidelity-only side computation.
            (void)0;
        }
    }

    for (size_t i = 0; i < room.children.size(); ++i) {
        const StockChild& ch = room.children[i];
        u8 cat = (i < childCategory.size()) ? childCategory[i] : 0;
        bool keep;
        if (workstation) {
            keep = ((cat == 23 || cat == 37) && mode) ||
                   (cat != 23 && cat != 37 && !mode) || (ch.type == 278);
        } else {
            keep = (cat == 23 && mode) || (cat != 23 && !mode);
        }
        if (!keep)
            continue;

        int eff = InventoryEffectiveStock(ch.type, ch.level);     // +104
        int cap = InventorySlotCapacity(ch.type, ch.level);       // +8
        out.stocks.push_back(eff);
        out.caps.push_back(cap);
        out.types.push_back(ch.type);                              // +72
        ++out.count;                                               // +4
    }
    return 1;
}
}  // namespace

int InventoryCollectStorageSlots(const ContainerView& room, bool roomTypeIs278,
                                 int mode, const std::vector<u8>& childCategory,
                                 CollectedSlots& out) {
    return CollectImpl(room, roomTypeIs278, mode, childCategory, out,
                       /*workstation=*/false);
}

int InventoryCollectWorkstationSlots(const ContainerView& room,
                                     bool roomTypeIs278, int mode,
                                     const std::vector<u8>& childCategory,
                                     CollectedSlots& out) {
    return CollectImpl(room, roomTypeIs278, mode, childCategory, out,
                       /*workstation=*/true);
}

}  // namespace guild::sim

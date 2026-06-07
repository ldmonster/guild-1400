#include "sim/production_slots.h"

#include "sim/building.h"               // g_buildingTypes, BuildingTypeDef
#include "sim/building_production.h"    // Building_ComputeMarketPrice (price model)

// Faithful 1:1 port of the production-slot inventory rules from gilde.exe. The
// pure arithmetic (weapon-category type test, per-slot capacity table, worth
// accumulation, timer decrement/fire decision) is verbatim; the scene-tree
// walks / clock reads / command tails are routed through IProductionSlotHooks.

namespace guild::sim {

// ===========================================================================
// hooks
// ===========================================================================
double IProductionSlotHooks::MarketPrice(i16 type) {
    // Default: the real type-table price model at the canonical qty 100.
    return Building_ComputeMarketPrice(type, 100);
}

static IProductionSlotHooks g_defaultHooks;
static IProductionSlotHooks* g_hooks = &g_defaultHooks;
void SetProductionSlotHooks(IProductionSlotHooks* hooks) {
    g_hooks = hooks ? hooks : &g_defaultHooks;
}
IProductionSlotHooks* ProductionSlotHooks() { return g_hooks; }

// ===========================================================================
// per-slot capacity table (same table as VIBE_Inventory_GetSlotCapacity 0x592474:
//   type 477 -> 5*level + 10;  level 3 -> 80;  else 20*level).
// CollectProductionSlots inlines exactly this in its v9 computation.
// ===========================================================================
static int SlotCapacity(i16 type, i32 level) {
    if (type == kItemHighCap)         // 477
        return 5 * level + 10;
    if (level == 3)
        return 80;
    return 20 * level;
}

// ===========================================================================
// VIBE_Inventory_IsWeaponSlotCompatible  0x550298  (PURE)
// ===========================================================================
bool InventoryWeaponSlotKindOk(i16 slotProt, u8 typeKind) {
    // ((a2!=370 && a2!=372) || kind==4)
    //   && ((a2!=344 && a2!=352) || kind==19)
    //   && ((a2!=366 && a2!=374) || kind==16)
    bool rangedOk = (slotProt != kWeaponSlotRangedA && slotProt != kWeaponSlotRangedB)
                  || typeKind == 4;
    bool shieldOk = (slotProt != kWeaponSlotShieldA && slotProt != kWeaponSlotShieldB)
                  || typeKind == 19;
    bool armourOk = (slotProt != kWeaponSlotArmourA && slotProt != kWeaponSlotArmourB)
                  || typeKind == 16;
    return rangedOk && shieldOk && armourOk;
}

bool InventoryIsWeaponSlotCompatible(bool hasHeld, u8 heldTypeByte, i16 slotProt,
                                     bool typeTableLoaded) {
    // if (!a1 || !v2) return 0;  (a1 is the person; v2 == person+364 held node)
    if (!hasHeld)
        return false;
    // typeKind = *(BYTE*)(dword_13CE294 + 589 * *v2)  (typeDef.kind for the node)
    u8 typeKind = 0;
    if (typeTableLoaded)
        typeKind = g_buildingTypes[heldTypeByte].kind;
    return InventoryWeaponSlotKindOk(slotProt, typeKind);
}

// ===========================================================================
// VIBE_Inventory_IsObjectSlotActive  0x54f0ec
//   v2 = QueryFind(*(DWORD*)(a1+376), 1,0, a2);
//   return v2 && FindSlotByItemId(*v2) && *(DWORD*)(v2 + 33);
// (v2/edx is the found node; +33 == its active dword; FindSlotByItemId(*v2) gates
// on the node's grid slot existing.)
// ===========================================================================
bool InventoryIsObjectSlotActive(int personContainerId, i16 prot) {
    i32 owner = 0, active = 0;
    i16 type = 0;
    if (!g_hooks->QueryObjectNode(personContainerId, prot, &owner, &active, &type))
        return false;                        // !v2
    i32 slotId = 0;
    if (!g_hooks->FindGridSlot(type, &slotId))
        return false;                        // !FindSlotByItemId(*v2)
    return active != 0;                       // *(DWORD*)(v2+33)
}

// ===========================================================================
// VIBE_Inventory_IsProductionSlotMatch  0x54f124
//   v2 = QueryFind(0, 2,7,0, a2);
//   return v2 && FindSlotByItemId(*v2) && *(DWORD*)(v2+33)
//       && *(DWORD*)(v2+37) == *(DWORD*)(slot+1);
// (slot is the FindSlotByItemId result; its +1 dword is the slot id; the node's
// +37 owner must equal it.)
// ===========================================================================
bool InventoryIsProductionSlotMatch(i16 prot) {
    i32 owner = 0, active = 0;
    i16 type = 0;
    if (!g_hooks->QueryObjectNode(/*global*/ 0, prot, &owner, &active, &type))
        return false;
    i32 slotId = 0;
    if (!g_hooks->FindGridSlot(type, &slotId))
        return false;
    if (active == 0)                          // *(DWORD*)(v2+33)
        return false;
    return owner == slotId;                   // node+37 == slot+1
}

// ===========================================================================
// VIBE_Inventory_CollectProductionSlots  0x5922d4
//   v4 = QueryFind(building+20, 1,0, 477);  if (!v4) return 0;
//   for (node = QueryFind(v4[5], 1,5); node; node = IterNext()) {
//       cap = (v4.type==477) ? 5*node.level+10
//                            : (node.level==3 ? 80 : 20*node.level);
//       dst[count].cap = cap;  dst.capTotal += cap;  dst.levTotal += node.level;
//       dst[count].type = node.type;
//       dst.worth += MarketPrice(node.type) * node.level;
//       ++count;
//   }
//   return 1;
// NB: the capacity branch keys on the ROOT work-product node's type (v4), not the
// per-slot node's — faithfully reproduced below via `rootIsHighCap`.
// ===========================================================================
int InventoryCollectProductionSlots(const std::vector<ProdSlotNode>& nodes,
                                    ProdSlotCollect& out) {
    out = ProdSlotCollect{};
    if (nodes.empty())
        return 0;                             // !v4 (no root work-product node)

    // v4 == root work-product node; its type drives the 477 capacity branch.
    const bool rootIsHighCap = (nodes.front().type == kItemHighCap);

    // The work slots are the remaining children IterNext yields. The original's
    // first QueryFind(v4[5], 1, 5) seeds the loop with the first slot; we treat
    // the whole list as the slot set (the root included, as the binary's loop
    // re-reads v4's columns and the first slot is v4's first child).
    for (const ProdSlotNode& node : nodes) {
        int cap;
        if (rootIsHighCap)
            cap = 5 * node.level + 10;
        else if (node.level == 3)
            cap = 80;
        else
            cap = 20 * node.level;

        out.caps.push_back(cap);
        out.types.push_back(node.type);
        out.capTotal += cap;                  // dst[42] += cap
        out.levTotal += node.level;           // dst[43] += node.level
        // dst[176] += MarketPrice(node.type) * node.level  (worth column)
        out.worth += g_hooks->MarketPrice(node.type) * static_cast<double>(node.level);
        ++out.count;                          // dst[1]++
    }
    (void)SlotCapacity;  // table kept available for callers / cross-checks
    return 1;
}

// ===========================================================================
// VIBE_Inventory_TickProductionTimers  0x54f168  (one-order step)
//   *(DWORD*)(node+7) -= DiffMinutes(node+45, now);   // debit elapsed minutes
//   *(QWORD*)(node+45) = now;                          // restamp
//   if (*(int*)(node+7) >= 0) -> still running;
//   else { node.active = 0;                            // clear active flag
//          if (personKind==6) NotifyProductReady(...); // sale building message
//          if (IsObjectForTurn && personKind!=7)       // only owner emits cmd
//              EmitProductionFinished(...); }
// The live game wraps this in a 32-NPC window scan; we expose the per-order step.
// ===========================================================================
ProductionTickResult InventoryTickProductionOrder(ProductionOrder& order,
                                                   const GameTime& now,
                                                   bool personIsOwnerTurn) {
    ProductionTickResult result{};
    if (!order.active)
        return result;                        // idle slot: skipped by the scan

    // *(DWORD*)(node+7) -= DiffMinutes(node+45, now)
    order.timerMinutes -= g_hooks->DiffMinutes(order.lastStamp, now);
    // *(QWORD*)(node+45) = now  (restamp the slot's clock)
    order.lastStamp = now;

    if (order.timerMinutes >= 0)
        return result;                        // still producing

    // --- work order complete ---
    result.finished = true;
    order.active = false;                     // *(DWORD*)(node+33) = 0

    if (order.notifyReady)                    // person kind == 6 (sale building)
        g_hooks->NotifyProductReady(order.personId, order.productType);

    if (personIsOwnerTurn) {                  // VIBE_Character_IsObjectForTurn gate
        g_hooks->EmitProductionFinished(order.personId, order.productType);
        result.emitted = true;
    }
    return result;
}

}  // namespace guild::sim

#pragma once
// ===========================================================================
// production_slots.{h,cpp} — production-SLOT inventory rules (gilde.exe)
// ===========================================================================
// MODULE: the Inventory/production-slot remainder (namespace guild::sim).
//
// These five functions sit between the building-production tick (building_*.cpp)
// and the carried-item/grid inventory (inventory*.cpp): they answer "is this
// production work-slot active / matched / weapon-compatible", aggregate the
// production work slots of a workshop into the UI render list (capacities +
// running worth), and run the per-NPC production-timer countdown that fires a
// "production finished" command when a work order completes.
//
// All scene-tree walks (VIBE_GameObject_QueryFind / _IterNext), the live
// game-clock reads (qword_13CE852), the command/message lockstep tails and the
// Person-array column reads are routed through IProductionSlotHooks (a context
// struct, mocked in tests). The pure arithmetic — the per-slot capacity table,
// the weapon-category type-table test, the running worth accumulation, and the
// timer decrement / fire decision — is translated 1:1 and golden-vector tested.
//
// Translated functions:
//   VIBE_Inventory_IsObjectSlotActive      0x54f0ec
//   VIBE_Inventory_IsProductionSlotMatch   0x54f124
//   VIBE_Inventory_IsWeaponSlotCompatible  0x550298
//   VIBE_Inventory_CollectProductionSlots  0x5922d4
//   VIBE_Inventory_TickProductionTimers    0x54f168
#include <vector>

#include "guild/common/types.h"
#include "sim/types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// (A) VIBE_Inventory_IsWeaponSlotCompatible  0x550298  (PURE — golden vector)
// ---------------------------------------------------------------------------
// The weapon-equip rule. The original:
//   v2 = *(char**)(a1 + 364);              // person's held-weapon node ptr
//   if (!a1 || !v2) return 0;
//   typeKind = *(BYTE*)(dword_13CE294 + 589 * *v2);   // typeDef.kind for the node
//   result = ((a2!=370 && a2!=372) || typeKind==4)    // ranged slot -> kind 4
//         && ((a2!=344 && a2!=352) || typeKind==19)   // shield slot -> kind 19
//         && ((a2!=366 && a2!=374) || typeKind==16);  // armour slot -> kind 16
// i.e. for each equip-slot family, the held node's type-kind must match. Slots
// outside the three families are always compatible.
//
// We expose the pure decision over (slotProt, heldNodeType): `held` is the type
// byte of the node at person+364 (the node's +0 type, used to index the 589
// type table); `typeKind` is g_buildingTypes[held].kind. `hasHeld` mirrors the
// (v2 != 0) guard (no held node -> 0). The weapon-slot prot ids are named below.
enum WeaponSlotProt : i16 {
    kWeaponSlotRangedA = 370,  // requires typeKind == 4
    kWeaponSlotRangedB = 372,  // requires typeKind == 4
    kWeaponSlotShieldA = 344,  // requires typeKind == 19
    kWeaponSlotShieldB = 352,  // requires typeKind == 19
    kWeaponSlotArmourA = 366,  // requires typeKind == 16
    kWeaponSlotArmourB = 374,  // requires typeKind == 16
};

// Pure compatibility test given the held node's type-kind byte.
bool InventoryWeaponSlotKindOk(i16 slotProt, u8 typeKind);

// Full faithful form: `hasHeld` == (person+364 != 0); `heldTypeByte` is the
// node's +0 type used to index g_buildingTypes; `typeTableLoaded` mirrors the
// dword_13CE294 != 0 guard (false -> kind reads 0). Returns the original BOOL.
bool InventoryIsWeaponSlotCompatible(bool hasHeld, u8 heldTypeByte, i16 slotProt,
                                     bool typeTableLoaded);

// ---------------------------------------------------------------------------
// (B) production work-slot record + the per-slot capacity table
// ---------------------------------------------------------------------------
// CollectProductionSlots reads the work-product object node's fields:
//   type   : node +0     (item type word; 477 == high-cap good)
//   level  : node +0x0E  (dword == *(node+7); the level/grade used for capacity)
// and a per-slot capacity exactly mirroring VIBE_Inventory_GetSlotCapacity:
//   type 477 -> 5*level + 10;  level 3 -> 80;  else 20*level.
// (This is the same table as inventory.h::InventoryGetSlotCapacity / the engines
// in inventory_capacity.h; named here for the worth accumulation it feeds.)
struct ProdSlotNode {
    i16 type  = 0;   // node +0
    i32 level = 0;   // node +0x0E (== *(node+7))
};

// The aggregate the UI render list accumulates (the destination record the
// original fills): per-slot (type, capacity), the slot count, the running
// capacity total (dest[42] += slotCap) and the running level total
// (dest[43] += node.level). `worth` accumulates dest[176] += price*level (the
// market-price-weighted worth column the gauge draws).
struct ProdSlotCollect {
    int              count    = 0;   // dest[1] slot count
    int              capTotal = 0;   // dest[42] sum of per-slot capacities
    int              levTotal = 0;   // dest[43] sum of per-slot levels
    double           worth    = 0.0; // dest[176] sum of price*level
    std::vector<i16> types;          // dest[+72 + 2*i] per-slot type word
    std::vector<int> caps;           // dest[+2 + i] per-slot capacity
};

// ---------------------------------------------------------------------------
// (C) Hooks — the scene/clock/command leaves (mocked in tests; default inert).
// ---------------------------------------------------------------------------
struct IProductionSlotHooks {
    virtual ~IProductionSlotHooks() = default;

    // VIBE_GameObject_QueryFind(containerId, 1,0, prot) for IsObjectSlotActive /
    // QueryFind(0, 2,7,0, prot) for IsProductionSlotMatch: locate a scene node
    // and return a small view of it. Returns false if no node was found.
    //   outOwner   : node +37 (owner/player dword)
    //   outActive  : node +0x21 (+33) active dword (non-zero == active)
    //   outType    : node +0   (type word, used to find the grid slot)
    virtual bool QueryObjectNode(int containerId, i16 prot, i32* outOwner,
                                 i32* outActive, i16* outType) {
        (void)containerId; (void)prot; (void)outOwner; (void)outActive;
        (void)outType;
        return false;
    }
    // VIBE_Inventory_FindSlotByItemId(type): returns true if a UI grid slot for
    // `type` exists; `outSlotId` is the matched slot's +1 dword (the id the
    // production-slot-match compares against the object's owner).
    virtual bool FindGridSlot(i16 type, i32* outSlotId) {
        (void)type; (void)outSlotId; return false;
    }

    // VIBE_Building_ComputeMarketPrice(type, 100): goods price at qty 100. The
    // default reuses the real price model (declared in building_production.h);
    // overridable in tests for determinism.
    virtual double MarketPrice(i16 type);

    // ----- TickProductionTimers leaves -----
    // VIBE_GameTime_DiffMinutes(a, now): minutes between a stored clock and now.
    // The tick uses it to debit the slot timer by elapsed minutes.
    virtual int DiffMinutes(const GameTime& stored, const GameTime& now) {
        (void)stored; (void)now; return 0;
    }
    // VIBE_Command_QueueRequest17(...): emit the "production finished" command for
    // the completed work slot of person `personId`, product `productType`.
    virtual void EmitProductionFinished(i32 personId, i16 productType) {
        (void)personId; (void)productType;
    }
    // VIBE_He_SendEntityMessage(...): notify the (kind==6 sale) building that the
    // good is ready. Fire-and-forget.
    virtual void NotifyProductReady(i32 personId, i16 productType) {
        (void)personId; (void)productType;
    }
};
void SetProductionSlotHooks(IProductionSlotHooks* hooks);
IProductionSlotHooks* ProductionSlotHooks();

// ---------------------------------------------------------------------------
// (D) translated functions over the hooks
// ---------------------------------------------------------------------------

// gilde.exe 0x54f0ec — VIBE_Inventory_IsObjectSlotActive (eax=person, dx=prot).
// QueryFind the prot-typed child off the person's container (+376), require its
// grid slot to exist, and return whether the node's +33 active dword is set.
bool InventoryIsObjectSlotActive(int personContainerId, i16 prot);

// gilde.exe 0x54f124 — VIBE_Inventory_IsProductionSlotMatch (ecx=person, dx=prot).
// As above (global QueryFind), but additionally requires the node's owner (+37)
// to equal the matched grid slot's id (+1).
bool InventoryIsProductionSlotMatch(i16 prot);

// gilde.exe 0x5922d4 — VIBE_Inventory_CollectProductionSlots (eax=building view).
// Walk the building's work-product children (QueryFind 477 then IterNext over
// the work slots), and for each: record the slot type, compute its capacity from
// the slot-capacity table, accumulate the capacity/level totals and the
// market-price-weighted worth. Fills `out`. The `nodes` list is the resolved
// child node list (what QueryFind/_IterNext would yield); empty -> returns 0.
// Returns 1 when at least the root work-product node existed, 0 otherwise.
int InventoryCollectProductionSlots(const std::vector<ProdSlotNode>& nodes,
                                    ProdSlotCollect& out);

// ---------------------------------------------------------------------------
// (E) VIBE_Inventory_TickProductionTimers 0x54f168 — the per-NPC timer pass
// ---------------------------------------------------------------------------
// One production work order on one NPC. The tick decrements `timerMinutes` by the
// minutes elapsed since `lastStamp`, restamps to `now`, and when the timer drops
// below zero, fires the completion (command + optional message) and clears the
// active flag. Modeled as a single-order step so the decrement/fire decision is
// golden-vector testable; the live game runs it across a 32-NPC window per call.
struct ProductionOrder {
    bool     active     = false;  // node +33 active dword (0 == idle)
    i32      timerMinutes = 0;    // node +28 (*(node+7)) remaining minutes
    GameTime lastStamp{};         // node +45 last-update clock stamp
    i16      productType  = 0;    // node +0   product item type
    i32      personId     = 0;    // owning NPC id (for the fired command)
    bool     notifyReady  = false;// person kind==6 -> send the ready message
};

// Result of stepping one order through the tick.
struct ProductionTickResult {
    bool finished = false;  // timer dropped below 0 this step (work complete)
    bool emitted  = false;  // a completion command was emitted
};

// Step one production order to `now`. Returns whether it finished. On finish the
// active flag is cleared and (subject to `personIsOwnerTurn`) the completion
// command is emitted via the hooks. `personIsOwnerTurn` mirrors
// VIBE_Character_IsObjectForTurn (only the simulating owner emits the command).
ProductionTickResult InventoryTickProductionOrder(ProductionOrder& order,
                                                   const GameTime& now,
                                                   bool personIsOwnerTurn);

}  // namespace guild::sim

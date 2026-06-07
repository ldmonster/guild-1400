#pragma once
// Building UPGRADE application + demolish-request dispatch for the Guild
// simulation (gilde.exe). MODULE: buildings (namespace guild::sim).
//
// This module is the "apply an upgrade / tear down a building" layer that sits
// ABOVE the value model (building_stock: SumFlaggedSlotsWorth / ComputeSalePrice)
// and BELOW the GUI. It is distinct from building_create*/building_stock/
// production_slots (creation, inventory, work slots): here we translate the
// functions that mutate a building's *upgrade level* and the interaction/AI
// entry points that price an upgrade and enqueue the lockstep command.
//
// THE UPGRADE COST MODEL (recovered, identical across all four entry points):
//   worth = VIBE_Building_SumFlaggedSlotsWorth(typeIndex)   (building_stock)
//   cost  = trunc( worth * 0.3 )                            (flt 0x3E99999A)
//   -> EnqueueCmd15(payerId, targetId, cost, priceMode)     (money deduction)
// flt_61A474 / flt_61A48C / flt_61A5B4 / flt_61A5D0 are ALL 0x3E99999A == 0.3.
//
// THE LEVEL-UP MATH (from VIBE_Command_ExGebUpgrade @0x49aff0, top block):
//   guard : typeDef.security(+583) >= typeDef.maxUpgradeLevel(+584) -> "already
//           highest level", abort (no mutation).
//   apply : ++building.typeByte(+0)        (advance to the next type record)
//           building.condition(+92) =
//               HIBYTE(packed@+89) + (100 - (packed@+89 >> 24)) / 2
//           (recompute the building condition after the level bump).
//
// Translated functions:
//   VIBE_Object_HideUpgradeScaffold                 0x5063f0
//   VIBE_Interaction_PerformBuildingUpgrade         0x46cfc4
//   VIBE_Interaction_PerformBuildingUpgradeOnObject 0x46d978
//   VIBE_NpcAction_UpgradeTownHall                  0x472b90
//   VIBE_NpcAction_UpgradeDungeon                   0x472fd0
//   VIBE_Command_ExGebUpgrade  (level-up math core) 0x49aff0
#include <cstddef>

#include "guild/common/types.h"
#include "sim/types.h"
#include "sim/building_types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// The shared upgrade-cost factor.  flt_61A474, flt_61A48C, flt_61A5B4 and
// flt_61A5D0 are byte-identical (0x3E99999A) — the single-precision constant the
// originals multiply the building worth by to price an upgrade.
// ---------------------------------------------------------------------------
constexpr float kUpgradeCostFactor = 0.30000001192092896f;   // 0x3E99999A

// Building-as-Person record fields the upgrade level-up touches (byte offsets
// into the 536-byte Person/building record, word_12CE910).  Recovered from
// VIBE_Command_ExGebUpgrade @0x49aff0:
//   +0   (byte)  building TYPE index (incremented to advance the upgrade level)
//   +89  (dword) packed quality/upgrade word; high byte == current level
//   +92  (byte)  building condition/quality scalar (recomputed on level-up)
namespace BuildingUpgradeField {
constexpr int kTypeByte   = 0;    // *(building+0): ++ to next type record
constexpr int kPackedQual = 89;   // *(int*)(building+89): HIBYTE == level
constexpr int kCondition  = 92;   // *(building+92): recomputed condition byte
}  // namespace BuildingUpgradeField

// ---------------------------------------------------------------------------
// gilde.exe 0x5063f0 — VIBE_Object_HideUpgradeScaffold  (al ret, eax=object)
//   Reads the object's +535 state byte: if it is 2 or 4 the scaffold is already
//   in a final state and the call is a no-op (returns true).  Otherwise it
//   re-selects the object's texture set (the leaf VIBE_Object_SelectTextureSet
//   at +492 -> +244) to drop the construction scaffold.  We model the predicate
//   1:1 and report whether a texture re-selection is REQUIRED, so the scene leaf
//   can be driven by the caller/test.  `stateByte` is the +535 byte.
// ---------------------------------------------------------------------------
bool Object_HideUpgradeScaffoldNeedsRetexture(u8 stateByte);

// ---------------------------------------------------------------------------
// Upgrade-cost pricing (the shared `worth * 0.3` block all four entry points
// run before they enqueue the money-deduction command).  `typeIndex` is the
// building's +0 type byte (HIBYTE of *(building+1) in the originals).  Returns
// the truncated cost the originals pass to VIBE_Command_EnqueueCmd15.
// ---------------------------------------------------------------------------
int Building_ComputeUpgradeCost(u8 typeIndex);

// ---------------------------------------------------------------------------
// Upgrade level-up core (VIBE_Command_ExGebUpgrade @0x49aff0, top block).
// Result of attempting to advance a building one upgrade level.
// ---------------------------------------------------------------------------
struct UpgradeApplyResult {
    bool atMaxLevel;     // guard hit: typeDef.security >= typeDef.maxUpgradeLevel
    u8   newTypeByte;    // building +0 after ++ (unchanged when atMaxLevel)
    u8   newCondition;   // building +92 after recompute (unchanged when atMaxLevel)
};

// gilde.exe 0x49aff0 — VIBE_Command_ExGebUpgrade (deterministic core).
//   typeIndex    : the building's CURRENT +0 type byte (selects the type def).
//   packedQual   : *(int*)(building+89) — the packed quality/upgrade dword.
// Applies the level-up: when the type is below its max level, increments the
// type byte and recomputes the condition byte; otherwise reports atMaxLevel and
// leaves the record untouched.  The 3D/scene reload + ApplyUpgradeStaff that
// follow in the original are side-effects driven separately by the command path.
UpgradeApplyResult Building_ApplyUpgradeLevel(u8 typeIndex, i32 packedQual);

// ---------------------------------------------------------------------------
// Interaction/AI upgrade entry points.  Each gates on the handler/object kind,
// prices the upgrade (Building_ComputeUpgradeCost), and — when it fires — routes
// the money deduction + slot reset through IUpgradeSink.  They return the
// original's interaction RESULT CODE (17 / 19 / 49 / 60) or 0 when not handled.
// ---------------------------------------------------------------------------
struct IUpgradeSink {
    virtual ~IUpgradeSink() = default;
    // gilde.exe 0x494604 — VIBE_Command_EnqueueCmd15: deduct `cost` money for the
    // upgrade of building `targetId`, paid by `payerId` (-1 == the AI/none payer),
    // tagged with `priceMode` (byte_6477A1).
    virtual void EnqueueUpgradeCharge(i32 payerId, i32 targetId, int cost,
                                      u8 priceMode) {
        (void)payerId; (void)targetId; (void)cost; (void)priceMode;
    }
    // gilde.exe VIBE_Building_EnqueueBuyBuilding — the "buy this building" branch
    // of PerformBuildingUpgradeOnObject (handler kind 8: purchase, not upgrade).
    virtual void EnqueueBuyBuilding(i32 buildingId, i32 sellerId, i32 buyerId) {
        (void)buildingId; (void)sellerId; (void)buyerId;
    }
};
void SetUpgradeSink(IUpgradeSink* sink);
IUpgradeSink* UpgradeSink();

// The price-mode byte (gilde.exe byte_6477A1) the cost commands are tagged with.
void SetUpgradePriceMode(u8 mode);
u8   UpgradePriceMode();

// Minimal handler/object view the interaction entry points read.  The originals
// reach these through raw `_BYTE*` pointers; we surface just the fields the
// upgrade dispatch consumes (see each function's provenance comment).
struct UpgradeInteraction {
    u8  handlerKind;     // *a1 / *a2 / *(a3) — the menu-handler kind
    u8  targetObjKind;   // *(byte*)a3 — the targeted object's kind
    i32 buildingTypeId;  // HIBYTE(*(int*)(rec+1)) source typeIndex for the cost
    i32 payerId;         // *(int*)(rec+4) — the paying person/object id
    i32 targetId;        // the targeted building id for the charge
    i32 secondaryId;     // *(int*)(a3+4) — the object the charge targets
};

// gilde.exe 0x46cfc4 — VIBE_Interaction_PerformBuildingUpgrade (al ret).
//   handlerKind 4 -> price the upgrade (factor 0.3), charge, reset; return 17.
//   any other kind -> not handled here (returns 0; the original tries the
//   graphic-load fallback which is a pure side-effect path).
int Interaction_PerformBuildingUpgrade(const UpgradeInteraction& in);

// gilde.exe 0x46d978 — VIBE_Interaction_PerformBuildingUpgradeOnObject (al ret).
//   handlerKind 4 && targetObjKind 8 -> BUY branch (EnqueueBuyBuilding); return 19.
//   targetObjKind 4               -> upgrade branch (charge); return 19.
//   else                          -> not handled here (returns 0).
// `handlerFound` mirrors the original's He_FindFirstHandlerByFilter gate on the
// buy branch (nullptr -> return 0).
int Interaction_PerformBuildingUpgradeOnObject(const UpgradeInteraction& in,
                                               bool handlerFound);

// gilde.exe 0x472b90 / 0x472fd0 — VIBE_NpcAction_UpgradeTownHall / _UpgradeDungeon.
//   gate: an office storage exists, the actor's +358 byte == 15, and either the
//   handler kind is 4 or the secondary slot (a2+16) is empty.  When the gate
//   passes, prices + charges the upgrade and returns the action code (49 / 60).
//   We pass the recovered gate inputs explicitly so the rules stay leaf-pure.
//     officeStoragePresent : VIBE_Building_FindOfficeStorage != null
//     stateByte358         : the actor's +358 byte (must be 15)
//     secondaryEmpty       : *(int*)(a2+16) == 0
int NpcAction_UpgradeTownHall(const UpgradeInteraction& in,
                              bool officeStoragePresent, u8 stateByte358,
                              bool secondaryEmpty);
int NpcAction_UpgradeDungeon(const UpgradeInteraction& in,
                             bool officeStoragePresent, u8 stateByte358,
                             bool secondaryEmpty);

void ResetUpgradeModule();   // test helper: restore default sink + price mode

}  // namespace guild::sim

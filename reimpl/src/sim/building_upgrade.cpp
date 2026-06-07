// Building UPGRADE application + demolish-request dispatch (gilde.exe).
// See building_upgrade.h for the recovered cost / level-up model.
#include "sim/building_upgrade.h"

#include "sim/building.h"         // BuildingTypeDefAt (type-table accessor)
#include "sim/building_stock.h"   // Building_SumFlaggedSlotsWorth (worth model)

namespace guild::sim {

// trunc-toward-zero, the rounding every "v=x; VIBE_Coord_ConvertX(); (int)v"
// site uses (cvttsd2si).  Matches building_stock.cpp's truncToZero exactly.
static inline int truncToZero(double x) {
    return static_cast<int>(static_cast<long long>(x));
}

// ---------------------------------------------------------------------------
// Cross-module sink + price-mode global (mocked in tests; default inert).
// ---------------------------------------------------------------------------
static IUpgradeSink  g_defaultUpgradeSink;
static IUpgradeSink* g_upgradeSink = &g_defaultUpgradeSink;
static u8            g_upgradePriceMode = 0;          // byte_6477A1

void SetUpgradeSink(IUpgradeSink* sink) {
    g_upgradeSink = sink ? sink : &g_defaultUpgradeSink;
}
IUpgradeSink* UpgradeSink() { return g_upgradeSink; }

void SetUpgradePriceMode(u8 mode) { g_upgradePriceMode = mode; }
u8   UpgradePriceMode()           { return g_upgradePriceMode; }

void ResetUpgradeModule() {
    g_upgradeSink = &g_defaultUpgradeSink;
    g_upgradePriceMode = 0;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5063f0 — VIBE_Object_HideUpgradeScaffold
//   char v2 = *(building+535); if (v2 == 2 || v2 == 4) return 1; else
//   VIBE_Object_SelectTextureSet(...); return 1;
//   -> returns "true (need texture re-selection)" iff the state byte is NOT a
//   final (2/4) scaffold state.
// ---------------------------------------------------------------------------
bool Object_HideUpgradeScaffoldNeedsRetexture(u8 stateByte) {
    if (stateByte == 2 || stateByte == 4)
        return false;            // already final: the leaf is skipped
    return true;                 // would call VIBE_Object_SelectTextureSet
}

// ---------------------------------------------------------------------------
// The shared `worth * 0.3` upgrade-cost block.
//   v21 = SumFlaggedSlotsWorth(typeIndex);
//   v6  = (double)v21 * flt_61A474;   // 0.3
//   VIBE_Coord_ConvertX();            // trunc-to-zero
//   v20 = (__int64)v6;                // cost
// ---------------------------------------------------------------------------
int Building_ComputeUpgradeCost(u8 typeIndex) {
    int worth = Building_SumFlaggedSlotsWorth(typeIndex);     // v21
    double cost = static_cast<double>(worth)
                  * static_cast<double>(kUpgradeCostFactor);  // * 0.3
    return truncToZero(cost);                                 // VIBE_Coord_ConvertX
}

// ---------------------------------------------------------------------------
// gilde.exe 0x49aff0 — VIBE_Command_ExGebUpgrade (deterministic level-up core).
//   guard:
//     if ( typeDef[+583] >= typeDef[+584] ) { report "highest level"; abort; }
//   apply:
//     ++*Begin;                                   // building type byte +0
//     Begin[92] = HIBYTE(*(int*)(Begin+89))
//               + (100 - (*(int*)(Begin+89) >> 24)) / 2;
//   HIBYTE(x) == (x >> 24) & 0xFF; (x >> 24) is the ARITHMETIC (signed) shift in
//   the original (`*(int*)... >> 24`).  We reproduce both faithfully.
// ---------------------------------------------------------------------------
UpgradeApplyResult Building_ApplyUpgradeLevel(u8 typeIndex, i32 packedQual) {
    UpgradeApplyResult r{};
    r.newTypeByte = typeIndex;

    const BuildingTypeDef* td = BuildingTypeDefAt(typeIndex);
    // typeDef[+583] >= typeDef[+584]: the type is at (or past) its top level.
    // When the table is unloaded both bytes read 0, so 0 >= 0 -> at max
    // (matches the original's null-base read returning the same byte twice).
    u8 security = td ? td->security : static_cast<u8>(0);
    u8 maxLevel = td ? td->maxUpgradeLevel : static_cast<u8>(0);
    if (security >= maxLevel) {
        r.atMaxLevel = true;
        r.newCondition = static_cast<u8>(packedQual >> 24);  // unchanged record
        return r;
    }

    // ++*Begin — advance the building one type record (== one upgrade level).
    r.newTypeByte = static_cast<u8>(typeIndex + 1);

    // Begin[92] = HIBYTE(packed) + (100 - (packed >> 24)) / 2.
    // HIBYTE -> unsigned top byte; the (packed >> 24) operand is an arithmetic
    // shift of the signed dword (sign-extends).
    int hibyte = (packedQual >> 24) & 0xFF;                   // HIBYTE(*(int*)..)
    int level  = packedQual >> 24;                            // signed >> 24
    int cond   = hibyte + (100 - level) / 2;
    r.newCondition = static_cast<u8>(cond);
    return r;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x46cfc4 — VIBE_Interaction_PerformBuildingUpgrade
//   if (*a1 == 4) { price upgrade (0.3), charge, reset; return 17; }
//   else if (LoadBuildingGraphic(...)) return 17; else return 0;
//   The graphic-load fallback is a pure scene side-effect; we surface 0 for the
//   non-handled case so the caller/command path drives it.
// ---------------------------------------------------------------------------
int Interaction_PerformBuildingUpgrade(const UpgradeInteraction& in) {
    if (in.handlerKind != 4)
        return 0;                                    // graphic-load fallback path
    int cost = Building_ComputeUpgradeCost(static_cast<u8>(in.buildingTypeId));
    // v19 = *(int*)(rec+4) target; EnqueueCmd15(payer, target, cost, mode).
    g_upgradeSink->EnqueueUpgradeCharge(in.payerId, in.targetId, cost,
                                        g_upgradePriceMode);
    return 17;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x46d978 — VIBE_Interaction_PerformBuildingUpgradeOnObject
//   if (*a2 == 4 && *(byte*)a3 == 8) {            // BUY branch (handler kind 8)
//       h = He_FindFirstHandlerByFilter(...); if (!h) return 0;
//       EnqueueBuyBuilding(...); return 19;
//   } else if (*(byte*)a3 == 4) {                 // UPGRADE branch
//       price (0.3), EnqueueCmd15(-1, *(a3+4), cost, mode), reset; return 19;
//   } else { if (!LoadBuildingGraphic(...)) return 0; ...; return 19; }
// Note the upgrade branch always pays with payer -1 (the original passes -1).
// ---------------------------------------------------------------------------
int Interaction_PerformBuildingUpgradeOnObject(const UpgradeInteraction& in,
                                               bool handlerFound) {
    if (in.handlerKind == 4 && in.targetObjKind == 8) {
        if (!handlerFound)
            return 0;                                // He_FindFirstHandlerByFilter
        g_upgradeSink->EnqueueBuyBuilding(in.buildingTypeId, in.payerId,
                                          in.targetId);
        return 19;
    }
    if (in.targetObjKind == 4) {
        int cost = Building_ComputeUpgradeCost(static_cast<u8>(in.buildingTypeId));
        // EnqueueCmd15(-1, *(int*)(a3+4), cost, byte_6477A1).
        g_upgradeSink->EnqueueUpgradeCharge(-1, in.secondaryId, cost,
                                            g_upgradePriceMode);
        return 19;
    }
    return 0;                                        // graphic-load fallback path
}

// ---------------------------------------------------------------------------
// Shared NpcAction upgrade gate + charge (UpgradeTownHall / UpgradeDungeon).
//   if (!OfficeStorage || stateByte358 != 15 || (handlerKind != 4 && !secEmpty))
//       return 0;
//   <price upgrade (0.3); EnqueueCmd15(...); reset>; return code;
// (0x472b90 returns 49, 0x472fd0 returns 60; only the cost-factor float and the
// label string differ, and both factors are 0x3E99999A == 0.3.)
// ---------------------------------------------------------------------------
static int NpcAction_UpgradeCommon(const UpgradeInteraction& in,
                                   bool officeStoragePresent, u8 stateByte358,
                                   bool secondaryEmpty, int resultCode) {
    if (!officeStoragePresent || stateByte358 != 15
        || (in.handlerKind != 4 && !secondaryEmpty)) {
        return 0;
    }
    int cost = Building_ComputeUpgradeCost(static_cast<u8>(in.buildingTypeId));
    g_upgradeSink->EnqueueUpgradeCharge(in.payerId, in.targetId, cost,
                                        g_upgradePriceMode);
    return resultCode;
}

// gilde.exe 0x472b90 — VIBE_NpcAction_UpgradeTownHall (returns 49).
int NpcAction_UpgradeTownHall(const UpgradeInteraction& in,
                              bool officeStoragePresent, u8 stateByte358,
                              bool secondaryEmpty) {
    return NpcAction_UpgradeCommon(in, officeStoragePresent, stateByte358,
                                   secondaryEmpty, 49);
}

// gilde.exe 0x472fd0 — VIBE_NpcAction_UpgradeDungeon (returns 60).
int NpcAction_UpgradeDungeon(const UpgradeInteraction& in,
                             bool officeStoragePresent, u8 stateByte358,
                             bool secondaryEmpty) {
    return NpcAction_UpgradeCommon(in, officeStoragePresent, stateByte358,
                                   secondaryEmpty, 60);
}

}  // namespace guild::sim

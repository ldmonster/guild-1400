#pragma once
// Building value / production-rating math for the Guild simulation (gilde.exe).
// MODULE: buildings, prefix VIBE_BuildingValue_* and the production-rating /
// output helpers from VIBE_Building_* (namespace guild::sim).
//
// SCOPE — the self-contained RULES/MATH core:
//   * the production-RATING evaluator and its smoothing curves,
//   * the per-item base-value formula,
//   * the production rate formula (type-table driven),
//   * the live-building current/max output interpolation and its ratio.
//
// The HEAVY value aggregators VIBE_BuildingValue_ComputeProductionWorth
// (0x58fe68), _ComputeStockValue (0x590360), _ComputeRoomWorth (0x59116c) and
// _SumStorageItemWorth (0x591658) iterate the Person/scene arrays and call into
// the He_* (handler), Inventory_* and GameObject_QueryFind subsystems — they are
// LISTED AS DEFERRED in building.cpp (cross-module coupling), but the leaf math
// they rely on (ComputeItemBaseValue / ComputeProductionRate) is translated here.
//
// Translated functions:
//   VIBE_Building_EvalProductionRating     0x58a794
//   VIBE_Building_ComputeRatingCurveA      0x58a6e8
//   VIBE_Building_ComputeRatingCurveB      0x58a73c
//   VIBE_Building_ComputeProductionPixels  0x58a6bc
//   VIBE_Building_ComputeItemBaseValue     0x58f328
//   VIBE_Building_ComputeProductionRate    0x58f268
//   VIBE_Building_ComputeCurrentOutput     0x57d26c
//   VIBE_Building_ComputeMaxOutput         0x57d310
//   VIBE_Building_ComputeOutputRatio       0x57d384
#include "guild/common/types.h"
#include "sim/building_types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Cross-module hooks the rating evaluator consults. The originals call into the
// He_* handler-list and the Inventory subsystem; we forward-declare a tiny sink
// so the rating math is testable in isolation (mock it in the test).
// ---------------------------------------------------------------------------
struct IBuildingRatingHooks {
    virtual ~IBuildingRatingHooks() = default;

    // gilde.exe VIBE_He_FindFirstHandlerByFilter / _FindNextMatchingHandler:
    // counts how many active "handler" records for this building match a given
    // stat slot (a2) on either of two packed fields (>>24). The original walks
    // the handler list; we expose the resulting weighted count directly:
    //   returns SUM over matching handlers of 0.16666667^k (k = match index),
    //   exactly reproducing the v16 accumulation loop in EvalProductionRating.
    //   `typeIndex` is the building's +0 type word, `stat` is a2 (0..4).
    virtual float HandlerStatWeight(u16 typeIndex, int stat) { (void)typeIndex; (void)stat; return 0.0f; }

    // gilde.exe VIBE_Inventory_IsObjectSlotActive(building, 346): 1 if the
    // building has the (object 346) equipment slot active, else 0. Only consulted
    // for stat 3. Default: inactive.
    virtual int InventorySlotActive(const BuildingRec* b, int objectId) { (void)b; (void)objectId; return 0; }
};

// Install the rating hooks (nullptr => all hooks return their inert default).
void SetBuildingRatingHooks(IBuildingRatingHooks* hooks);
IBuildingRatingHooks* BuildingRatingHooks();

// ---------------------------------------------------------------------------
// Recovered float constants (byte-for-byte; see building_value.cpp).
//   flt_62670C = 1/252  = 0.003968254   (stat-level scale)
//   flt_626710 = 0.5                     (handler-weight decay base)
//   flt_626714 = 0.16666667             (slot-active bonus / handler step)
//   flt_626734 = 0.333
//   dbl_62671C = 0.2,  dbl_626724 = 1/6, dbl_62672C = 0.333
//   ComputeRatingCurveA: flt_6266F4=0.6, flt_6266F8=0.4, flt_6266FC=0.5
//   ComputeRatingCurveB: flt_626700=0.6, flt_626704=0.4, flt_626708=0.5
//   ComputeProductionPixels: flt_6266F0 = 252.0
//   ComputeProductionRate: flt_6268E4=28, flt_6268E8=32, dbl_6268EC=1/12,
//                          dbl_6268F4=1/60, dbl_6268FC=0.25
//   ComputeItemBaseValue:  factor 896, dbl_6268FC=0.25
// ---------------------------------------------------------------------------

// gilde.exe 0x58a794 — VIBE_Building_EvalProductionRating
//   (__usercall: st0 ret, eax=building@a1, dl=stat@a2)
// Computes the production-rating for stat `stat` (0..4) of `b`:
//   base   = b.statLevel[stat] * (1/252)
//   minus  a handler-weighted penalty (for stat != 1),
//   plus   an inventory-slot bonus (stat == 3 only),
//   minus  a staff/equipment term decoded from b.staffBits (per-stat bitfields).
// Result is clamped to [0, 1]. Returns -1 for a null building or stat >= 5.
float Building_EvalProductionRating(const BuildingRec* b, int stat);

// gilde.exe 0x58a6e8 — VIBE_Building_ComputeRatingCurveA
//   blends the ratings of two buildings (a/b) for stat `stat` through a quadratic
//   smoothing curve (constants flt_6266F4/F8/FC).
double Building_ComputeRatingCurveA(int stat, const BuildingRec* a, const BuildingRec* b);

// gilde.exe 0x58a73c — VIBE_Building_ComputeRatingCurveB
//   the same quadratic blend taking two precomputed ratings directly.
double Building_ComputeRatingCurveB(float a1, float a2);

// gilde.exe 0x58a6bc — VIBE_Building_ComputeProductionPixels
//   trunc(rating * 252.0) — a gauge length in pixels.
int Building_ComputeProductionPixels(int stat, const BuildingRec* b);

// gilde.exe 0x58f328 — VIBE_Building_ComputeItemBaseValue
//   (__usercall: st0 ret, eax=building@a1, dl=typeIndex@a2, ecx=outIdx@a3, ebx=inIdx@a4)
// Base value of an input (a4 != -1) or output (a3 != -1) good for a building of
// type `typeIndex`. Reads the type table factor (896 * factorByte) and, when the
// building's objectKind is 6 or 7 (sale/auction), applies a price discount
// (1 - (2 - g_priceMode)*0.25). `b` supplies objectKind.
float Building_ComputeItemBaseValue(const BuildingRec* b, u8 typeIndex,
                                    int outIdx, int inIdx);

// gilde.exe 0x58f268 — VIBE_Building_ComputeProductionRate
//   (__usercall: st0 ret, ax=typeRemapIndex@a1)
// Rate = (sum over i in 0..1 of typeDef.outputFactor[i] * 28*32*(1/12)*(1/60))
//        * sceneDef.priceField / sceneDef.divisor.
// `remappedType` is the building's type after the byte_13CE862 remap; the caller
// passes the resolved BuildingTypeDef and the scene-object def fields directly.
double Building_ComputeProductionRate(const BuildingTypeDef& typeDef,
                                      u32 scenePriceField, u16 sceneDivisor);

// ---------------------------------------------------------------------------
// Live-building output interpolation (operates on the runtime BuildingRec).
// Empty (marker 0xFFFF) or inactive (activeFlag 0) buildings return -1 / 0.
// ---------------------------------------------------------------------------

// gilde.exe 0x57d26c — VIBE_Building_ComputeCurrentOutput
//   Lerps outZeroFill..outFullFill by (fillLevel / fillCap), clamps fill to cap,
//   adds outBonus, floors the sum at 0.
float Building_ComputeCurrentOutput(const BuildingRec* b);

// gilde.exe 0x57d310 — VIBE_Building_ComputeMaxOutput
//   The same interpolation WITHOUT the +outBonus / floor-at-0.
float Building_ComputeMaxOutput(const BuildingRec* b);

// gilde.exe 0x57d384 — VIBE_Building_ComputeOutputRatio
//   current / max, or 0 for an empty/inactive building.
float Building_ComputeOutputRatio(const BuildingRec* b);

}  // namespace guild::sim

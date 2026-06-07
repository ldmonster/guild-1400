#pragma once
// Supply / demand / price-equilibrium model for the Guild economy (gilde.exe).
//
// Translated functions:
//   VIBE_Economy_ComputeGoodsDemand  0x578438
//   VIBE_Economy_ComputeGoodsSupply  0x578634
//   VIBE_Economy_ComputePriceDeltas  0x5787d4
//   VIBE_Economy_LookupRateScalar    0x579a24
//
// RECORD GEOMETRY (see src/sim/person_record.h for the full decompiled proof):
// the originals iterate the OBJECT/BUILDING array (gilde.exe dword_13CE298,
// stride 169) via VIBE_Person_QueryBegin/IterNext — NOT the 536-byte Person
// array. For each iterated object record `j` they read:
//   * the employment word at OBJECT+39 (0xFFFF == unemployed), and
//   * the need byte at TYPE_DESCRIPTOR+583, where the type descriptor is
//     dword_13CE294 + 589 * (*j) — a SEPARATE 589-stride table indexed by the
//     object's +0 type byte (the recon/05 "person stride 589" was a category
//     error: 589 is a type-descriptor stride, never a person-id stride).
// To keep the arithmetic byte-faithful AND unit-testable without the whole sim,
// the per-person inputs are surfaced as a small POD view; the sim adapter
// guild::sim::PersonReadEcoInputs joins the (object, type-table) pair and feeds
// exactly these numbers (need, unemployed).
#include <vector>

#include "guild/common/types.h"
#include "world/types.h"

namespace guild::world {

// One iterated record's contribution to a single good/profession category.
//   need       : TYPE_DESCRIPTOR+583 (dword_13CE294 + 589*objectType, +583)
//   unemployed : OBJECT+39 == 0xFFFF in the original
// Built from the canonical arrays by guild::sim::PersonReadEcoInputs; the field
// names match that adapter's PersonEcoInputs result one-to-one.
struct PersonEcoView {
    u8   need;
    bool unemployed;
};

// Tuning constants (gilde.exe dbl_62563C..dbl_62567C / dbl_625684). Recovered
// byte-for-byte from the binary; exposed so tests can reference them.
namespace eco {
constexpr double kEmployedScale  = 0.5;  // dbl_62563C  (unemployed extra factor)
constexpr double kDemandSlopeA   = 2.0;  // dbl_625634  (default-class slope)
constexpr double kDemandSlopeB   = 3.0;  // dbl_625644  (service-class slope)
constexpr double kBias           = -1.0; // dbl_625654  (additive bias, demand)
constexpr double kDemandClass3   = 1.5;  // dbl_62564C  (profession-class 3 weight)
constexpr double kSupplySlopeA   = 2.0;  // dbl_62565C  (default-class slope)
constexpr double kSupplyEmpScale = 0.5;  // dbl_625664  (unemployed extra factor)
constexpr double kSupplySlopeB   = 3.0;  // dbl_62566C  (service-class slope)
constexpr double kSupplyBias     = -1.0; // dbl_62567C  (additive bias, supply)
constexpr double kSupplyClass3   = 1.5;  // dbl_625674  (profession-class 3 weight)
constexpr double kDeltaTail      = -1.0; // dbl_625684  (price-delta tail offset)
}

// Bridge to the canonical sim arrays: build a PersonEcoView for one iterated
// object record using the proven (object+39, typeDef+583) join. `objectRecord`
// is a guild::sim::ObjectRec* (169-byte array), `typeTable` a
// guild::sim::BuildingTypeDef* (589-byte table base), `typeTableLoaded` mirrors
// the dword_13CE294-null guard. This is the ONLY place world/economy touches the
// sim record layout; it delegates the offset arithmetic to the sim adapter so
// the two modules cannot drift. (Templated on the sim types to avoid a hard
// header dependency at this layer; the .cpp instantiates it against sim.)
PersonEcoView EcoViewFromObject(const void* objectRecord, const void* typeTable,
                                bool typeTableLoaded);

// Profession-class predicate used by both demand and supply:
//   class 3                              -> "flat" contribution
//   class 7,15,19 or >22                 -> "service" slope (B)
//   otherwise                            -> "default" slope (A)
enum class ProfClass { Flat, Service, Default };
ProfClass ClassifyProfession(int profIndex);

// gilde.exe 0x578438 — VIBE_Economy_ComputeGoodsDemand.
// Accumulates demand into g_goods[g].accum for every good/profession category
// g in [1,27], then computes the two city totals:
//   g_cityTotalMoney = goods[1].contribWeight*goods[1].accum
//                    + goods[2].contribWeight*goods[2].accum
//   g_cityTotalGoods = sum_{g=3..27} goods[g].contribWeight*goods[g].accum
// `persons[g]` supplies the person views for profession/category g.
void EconomyComputeGoodsDemand(const std::vector<PersonEcoView>* persons /*[28]*/);

// gilde.exe 0x578634 — VIBE_Economy_ComputeGoodsSupply.
// Computes supply for ONE good index `g` (the original advances an external
// index word dword_641FCE 1..28). Resets g_goods[g].accum then accumulates.
void EconomyComputeGoodsSupply(int g, const std::vector<PersonEcoView>& persons);

// gilde.exe 0x5787d4 — VIBE_Economy_ComputePriceDeltas.
// For g in [3,28): if cap >= g_capDivisor, delta = 0; else
//   raw = driftWeight * accum / g_capDivisor;
//   normal good : delta = (raw>=1) ? raw + kDeltaTail : -(1 - raw)
//   goods 4 & 16: delta = (raw>=1) ? -(raw + kDeltaTail) : (1 - raw)  [inverted]
void EconomyComputePriceDeltas();

// gilde.exe 0x579a24 — VIBE_Economy_LookupRateScalar (__usercall, al=(id@al)).
// Returns byte_123525C[id] for id<5, else 126. The 5-entry table is runtime
// data; tests inject it via the setter below (the static image has it zeroed).
i8 EconomyLookupRateScalar(i8 id);
void EconomySetRateScalarTable(const i8 table[5]);

} // namespace guild::world

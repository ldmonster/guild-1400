#include "world/economy.h"

#include "sim/person_record.h"
#include "world/city.h"

namespace guild::world {

// Bridge into the canonical sim arrays (see economy.h). Delegates the
// (object+39, typeDef+583) join to the sim adapter so the offset arithmetic
// lives in exactly one place.
PersonEcoView EcoViewFromObject(const void* objectRecord, const void* typeTable,
                                bool typeTableLoaded) {
    guild::sim::PersonEcoInputs in = guild::sim::PersonReadEcoInputs(
        static_cast<const guild::sim::ObjectRec*>(objectRecord),
        static_cast<const guild::sim::BuildingTypeDef*>(typeTable),
        typeTableLoaded);
    return PersonEcoView{in.need, in.unemployed};
}

// Runtime rate-scalar table (gilde.exe byte_123525C, 5 bytes; zeroed in the
// static image, written at runtime). Tests inject values.
namespace {
i8 g_rateScalar[5] = {0, 0, 0, 0, 0};
}

// gilde.exe 0x579a24 — VIBE_Economy_LookupRateScalar.
// The original is __usercall al=(id@al): it tests `id < 5` and indexes
// byte_123525C[id]. A negative `id` would index before the 5-entry table; the
// engine only ever passes ids 0..4 (the bound is its own envelope), so guarding
// the low end is a faithful memory-safety fix that leaves every valid id (0..4)
// and the >=5 path (126) byte-identical.
i8 EconomyLookupRateScalar(i8 id) {
    if (id >= 0 && id < 5)
        return g_rateScalar[id];
    return 126;
}

void EconomySetRateScalarTable(const i8 table[5]) {
    for (int i = 0; i < 5; ++i)
        g_rateScalar[i] = table[i];
}

// Profession-class predicate. Mirrors the branch chain in demand/supply:
//   v5 == 3                               -> Flat
//   v5 == 7 || 19 || 15 || v5 > 22        -> Service
//   else                                  -> Default
ProfClass ClassifyProfession(int profIndex) {
    if (profIndex == 3)
        return ProfClass::Flat;
    if (profIndex == 7 || profIndex == 19 || profIndex == 15 || profIndex > 22)
        return ProfClass::Service;
    return ProfClass::Default;
}

// gilde.exe 0x578438 — VIBE_Economy_ComputeGoodsDemand.
void EconomyComputeGoodsDemand(const std::vector<PersonEcoView>* persons) {
    using namespace eco;

    // Zero every good's accumulator (the original zeroes the dword span
    // dword_1234748[0..111] which covers all 28 entries' +8/+12 floats).
    for (int g = 0; g < kGoodCategoryCount; ++g)
        g_goods[g].accum = 0.0f;

    // Outer loop over profession/good index g in [1,27].
    for (int g = 1; g < 28; ++g) {
        ProfClass cls = ClassifyProfession(g);
        for (const PersonEcoView& pv : persons[g]) {
            double need = static_cast<double>(pv.need);
            double term;
            if (cls == ProfClass::Flat) {
                term = need * kDemandClass3;
            } else if (cls == ProfClass::Service) {
                if (pv.unemployed)
                    term = (need * kDemandSlopeB + kBias) * kEmployedScale;
                else
                    term = need * kDemandSlopeB + kBias;
            } else {  // Default
                if (pv.unemployed)
                    term = (need * kDemandSlopeA + kBias) * kEmployedScale;
                else
                    term = need * kDemandSlopeA + kBias;
            }
            g_goods[g].accum = static_cast<float>(term + g_goods[g].accum);
        }
    }

    // City totals. Money total = goods 1 & 2; goods total = goods 3..27, each
    // weighted by the contribution weight (word_1234752).
    g_cityTotalMoney =
        static_cast<float>(static_cast<double>(g_goods[1].contribWeight) * g_goods[1].accum +
                           static_cast<double>(g_goods[2].contribWeight) * g_goods[2].accum);
    double total = 0.0;
    for (int g = 3; g < 28; ++g)
        total += static_cast<double>(g_goods[g].contribWeight) * g_goods[g].accum;
    g_cityTotalGoods = static_cast<float>(total);
}

// gilde.exe 0x578634 — VIBE_Economy_ComputeGoodsSupply.
// Computes supply for a single good/profession index g.
void EconomyComputeGoodsSupply(int g, const std::vector<PersonEcoView>& persons) {
    using namespace eco;

    g_goods[g].accum = 0.0f;
    ProfClass cls = ClassifyProfession(g);
    for (const PersonEcoView& pv : persons) {
        double need = static_cast<double>(pv.need);
        double term;
        if (cls == ProfClass::Flat) {
            term = need * kSupplyClass3;
        } else if (cls == ProfClass::Service) {
            if (pv.unemployed)
                term = (need * kSupplySlopeB + kSupplyBias) * kSupplyEmpScale;
            else
                term = need * kSupplySlopeB + kSupplyBias;
        } else {  // Default
            if (pv.unemployed)
                term = (need * kSupplySlopeA + kSupplyBias) * kSupplyEmpScale;
            else
                term = need * kSupplySlopeA + kSupplyBias;
        }
        g_goods[g].accum = static_cast<float>(term + g_goods[g].accum);
    }
}

// gilde.exe 0x5787d4 — VIBE_Economy_ComputePriceDeltas.
// Walks good index v0 from 3 to 28. Caps with cap >= g_capDivisor get a zero
// delta; otherwise raw = driftWeight*accum/capDivisor, with goods 4 & 16
// inverted.
void EconomyComputePriceDeltas() {
    using namespace eco;

    // The original walks the good index v0 from 3 while v0<=28, reading the
    // 16-byte-stride slots. v0==28 would touch one slot past the 28-entry table
    // (the binary's adjacent globals); we bound the walk to the valid range
    // [3,27] — behaviour is identical for every in-bounds slot.
    for (int g = 3; g < kGoodCategoryCount; ++g) {
        // Skip goods whose cap meets/exceeds the equilibrium divisor.
        if (static_cast<double>(g_goods[g].cap) >= g_capDivisor) {
            g_goods[g].priceDelta = 0.0f;
            continue;
        }

        float raw = static_cast<float>(static_cast<double>(g_goods[g].driftWeight) *
                                       g_goods[g].accum / g_capDivisor);
        double delta;
        if (g == 4 || g == 16) {  // inverted goods
            double v = raw;
            if (raw >= 1.0f)
                delta = -(v + kDeltaTail);
            else
                delta = 1.0 - v;
        } else {                   // normal goods
            double v = raw;
            if (raw >= 1.0f)
                delta = v + kDeltaTail;
            else
                delta = -(1.0 - v);
        }
        g_goods[g].priceDelta = static_cast<float>(delta);
    }
}

} // namespace guild::world

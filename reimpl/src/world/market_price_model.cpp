#include "world/market_price_model.h"

// Full market price model (gilde.exe 0x58f3d0). The original works directly on
// raw byte offsets of the 65-byte good record and the 589-byte type record; here
// those reads are expressed over the GoodRecord POD, preserving the exact float
// arithmetic, constant order and cache write-back.

namespace guild::world {

// gilde.exe 0x58f3d0 — VIBE_Building_ComputeMarketPrice.
float BuildingComputeMarketPrice(GoodTable& table, i16 goodId, u8 currencyId) {
    if (!table.records || goodId < 0 || goodId >= table.count)
        return 0.0f;
    GoodRecord& rec = table.records[goodId];

    // ---- Cached fast path: record+56 != 0 -----------------------------------
    //   v26 = (double)(32 * cachedBase) * (double)currency * 0.01;
    //   if (record+33 == 3) v26 *= 1.5;   return v26;
    if (rec.cachedBase != 0) {
        double v26 = static_cast<double>(32 * rec.cachedBase)
                   * static_cast<double>(currencyId)
                   * static_cast<double>(mp::kCurrencyScale);
        if (rec.flag == 3)
            v26 *= static_cast<double>(mp::kFlag3Mult);
        return static_cast<float>(v26);
    }

    // ---- Compute path -------------------------------------------------------
    // v29 = divisor + (divisor == 0);     // min 1
    const int v29div = rec.divisor + (rec.divisor == 0 ? 1 : 0);
    // v23 = (double)value / (double)v29;
    const double v23 = static_cast<double>(rec.value) / static_cast<double>(v29div);

    // Type-need accumulation: sum (i16)typeRec[+563..+564] with 28*32/12/60 weight.
    double v27;
    if (rec.typeIndex != 0) {
        double acc = 0.0;
        const int needs[2] = { rec.typeNeed0, rec.typeNeed1 };
        for (int k = 0; k < 2; ++k) {
            // (i16)need * 28.0 * 32.0 * (1/12) * (1/60)
            acc += static_cast<double>(static_cast<i16>(needs[k]))
                 * static_cast<double>(mp::kTypeWeightA)
                 * static_cast<double>(mp::kTypeWeightB)
                 * mp::kTwelfth * mp::kSixtieth;
        }
        v27 = acc;
    } else {
        v27 = static_cast<double>(mp::kNoTypeNeed);   // 4.0
    }

    // v28 = v27 * 0.5 * v23;
    double v28 = v27 * mp::kHalf * v23;

    // Category 23: raw good, no recipe -> return v28 * 2.2 (as float).
    if (rec.category == 23)
        return static_cast<float>(static_cast<float>(v28 * static_cast<double>(mp::kPriceGain)));

    // ---- Component recipe (recursive), up to 4 slots ------------------------
    for (int k = 0; k < mp::kMaxComponents; ++k) {
        u16 compGood = rec.components[k].good;
        if (compGood == 0)
            break;                              // 0 ends the list (while record+46)
        if (compGood == 0xFFFF) {
            // Named ingredient with no price contribution (the original only
            // formats its name string here); skip.
            continue;
        }
        // qty = compSlot+38 (word); price = ComputeMarketPrice(comp+44>>16) * qty
        const u16 qty = rec.components[k].qty;
        const float compPrice =
            BuildingComputeMarketPrice(table, static_cast<i16>(compGood), currencyId)
            * static_cast<float>(qty);
        // v28 = compPrice / (double)divisor + v28;   (record+54 reread)
        v28 = static_cast<double>(compPrice) / static_cast<double>(rec.divisor) + v28;
    }

    // ---- Cache write-back + final return ------------------------------------
    // record+56 = (int)trunc(v28 * 2.2 * (1/32));   (VIBE_Coord_ConvertX == trunc)
    const double cacheVal = static_cast<double>(v28)
                          * static_cast<double>(mp::kPriceGain)
                          * static_cast<double>(mp::kCacheScale);
    rec.cachedBase = static_cast<i32>(cacheVal);   // trunc toward zero

    // return (float)(2.2 * (v28 * (double)(i16)currency * 0.01));
    return static_cast<float>(
        static_cast<double>(mp::kPriceGain)
        * (v28 * static_cast<double>(static_cast<i16>(currencyId))
               * static_cast<double>(mp::kCurrencyScale)));
}

} // namespace guild::world

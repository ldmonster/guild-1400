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
    //   v26 = (double)(32 * cachedBase) * (double)currency * 0.01;   // fstp -> float
    //   if (record+33 == 3) v26 = (float)v26 * 1.5;  // reload float, fstp -> float
    //   return v26;
    // v26 (var_2C) is a 32-bit float on the stack: the product is rounded to
    // float BEFORE the flag-3 test, and the flag-3 result is rounded again.
    if (rec.cachedBase != 0) {
        float v26 = static_cast<float>(static_cast<double>(32 * rec.cachedBase)
                   * static_cast<double>(currencyId)
                   * static_cast<double>(mp::kCurrencyScale));
        if (rec.flag == 3)
            v26 = static_cast<float>(static_cast<double>(v26)
                   * static_cast<double>(mp::kFlag3Mult));
        return v26;
    }

    // ---- Compute path -------------------------------------------------------
    // v29 = divisor + (divisor == 0);     // min 1
    const int v29div = rec.divisor + (rec.divisor == 0 ? 1 : 0);
    // v23 (var_38) is a float: fild value, fidiv -> fstp float.
    const float v23 = static_cast<float>(static_cast<double>(rec.value)
                                       / static_cast<double>(v29div));

    // Type-need accumulation: sum (i16)typeRec[+563..+564] with 28*32/12/60 weight.
    // v27 (var_28) is a float stored after the loop (fstp var_28).
    float v27;
    if (rec.typeIndex != 0) {
        double acc = 0.0;   // kept in st (80-bit) across the 2-iter accumulation
        const int needs[2] = { rec.typeNeed0, rec.typeNeed1 };
        for (int k = 0; k < 2; ++k) {
            // (i16)need * 28.0 * 32.0 * (1/12) * (1/60)
            acc += static_cast<double>(static_cast<i16>(needs[k]))
                 * static_cast<double>(mp::kTypeWeightA)
                 * static_cast<double>(mp::kTypeWeightB)
                 * mp::kTwelfth * mp::kSixtieth;
        }
        v27 = static_cast<float>(acc);
    } else {
        v27 = mp::kNoTypeNeed;   // 4.0
    }

    // v28 (var_24) is a float: fld v27 * 0.5 * v23 -> fstp var_24.
    float v28 = static_cast<float>(static_cast<double>(v27) * mp::kHalf
                                 * static_cast<double>(v23));

    // Category 23: raw good, no recipe -> return (float)(v28 * 2.2).
    if (rec.category == 23)
        return static_cast<float>(static_cast<double>(v28)
                                * static_cast<double>(mp::kPriceGain));

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
        // qty = compSlot+38 (word); fild qty -> fstp float (var_20).
        const float qtyf = static_cast<float>(rec.components[k].qty);
        // v17 = ComputeMarketPrice(comp+44>>16) * qtyf  — stays 80-bit (st7).
        // v28 = v17 / (double)divisor + v28;  result fstp -> float (record+54 reread).
        const double v17 =
            static_cast<double>(
                BuildingComputeMarketPrice(table, static_cast<i16>(compGood), currencyId))
            * static_cast<double>(qtyf);
        v28 = static_cast<float>(v17 / static_cast<double>(rec.divisor)
                               + static_cast<double>(v28));
    }

    // ---- Cache write-back + final return ------------------------------------
    // record+56 = (int)trunc(v28 * 2.2 * (1/32));   (VIBE_Coord_ConvertX == trunc)
    // v28 reloaded from float (var_24); 2.2 from var_34 (float v24).
    const double cacheVal = static_cast<double>(v28)
                          * static_cast<double>(mp::kPriceGain)
                          * static_cast<double>(mp::kCacheScale);
    rec.cachedBase = static_cast<i32>(cacheVal);   // trunc toward zero

    // return (float)(2.2 * (v28 * (double)(i16)currency * 0.01));
    return static_cast<float>(
        static_cast<double>(mp::kPriceGain)
        * (static_cast<double>(v28) * static_cast<double>(static_cast<i16>(currencyId))
               * static_cast<double>(mp::kCurrencyScale)));
}

} // namespace guild::world

#include "world/caravan_cargo.h"

namespace guild::world {

namespace {
CaravanPriceHook g_priceHook = nullptr;
} // namespace

void CaravanSetPriceHook(CaravanPriceHook hook) { g_priceHook = hook; }

double CaravanLookupPrice(i32 goodId, u8 context) {
    if (g_priceHook)
        return g_priceHook(goodId, context);
    return 0.0;
}

// gilde.exe 0x50854c — VIBE_TradePanel_InitSlotTables (cargo-grid reset portion).
//   for ( v12 = 0; v12 < 16; ++v12 ) {           // buy grid (16 slots)
//     word_122E090[k] = 0; dword_122E094[k] = -1; ...
//     dword_122E08C[k] = -1;                       // good-id packed dword
//   }
//   for ( v15 = 0; v15 < 8; ++v15 ) {            // sell grid (8 slots)
//     word_122E790[k] = 0; dword_122E794[k] = -1; ...
//     dword_122E78C[k] = -1;
//   }
// The screen-coordinate writes (dword_122E098/064/798/764) and the auxiliary
// column tables (dword_122EDxx, dword_122E3xx) are owned by gui/trade_panel.cpp.
void CaravanInitSlotTables(CaravanCargoTables& t) {
    t.sell.assign(kCaravanSellSlots, CaravanSlot{});
    t.buy.assign(kCaravanBuySlots, CaravanSlot{});
    // The original seeds the good-id packed dword to -1 (an empty marker); our
    // CaravanSlot default already gives objectId == -1, which is the field the
    // valuation/load loops test. Mirror the original's -1 packed-dword seed too.
    for (CaravanSlot& s : t.sell) {
        s.goodIdPacked = -1;
        s.objectId = -1;
        s.storageSlot = -1;
        s.dataPtr = 0;
    }
    for (CaravanSlot& s : t.buy) {
        s.goodIdPacked = -1;
        s.objectId = -1;
        s.storageSlot = -1;
        s.dataPtr = 0;
    }
}

// gilde.exe 0x53ff3c — VIBE_TradeTransport_ComputeCargoValue (faithful two-array).
//
// PRECISION (asm @53ffd0..540003): the per-slot unit price (var_24) AND the running
// accumulator (var_2C) are both 4-byte SINGLE-precision floats. Each iteration the
// original does: fild DataPtr; fmul [float unit]; fadd [float accum]; fstp [float
// accum]. So both the unit and the accumulation are rounded to single precision —
// NOT double. The accumulator must be `float` to be byte-exact.
double CaravanComputeCargoValue(const CaravanCargoTables& t, bool ownerIsMarket,
                                float priceMul, bool sourceKind71, u8 sourceCtx101,
                                int mode) {
    float total = 0.0f; // v13 == [esp+var_2C] (stored back as a 4-byte float each pass)

    // ctx = (*a1 == 71) ? *(_DWORD*)(a1+101) : byte_6477A1.
    u8 ctx = sourceKind71 ? sourceCtx101 : kCaravanDefaultPriceContext; // v16

    // First loop: sell grid (i = 0; i != 112; i += 14).
    for (const CaravanSlot& s : t.sell) {
        if (s.objectId == -1)
            continue;
        if (s.dataPtr == 0) // VIBE_Object_GetDataPtr(...) == 0
            continue;
        float unit; // v15 == [esp+var_24] (4-byte float)
        double price = CaravanLookupPrice(s.goodId(), ctx);
        if (ownerIsMarket) // *v4 == 10
            unit = static_cast<float>(price * kCaravanMarketPriceFactor); // * dbl_623F30 (1.1)
        else
            unit = static_cast<float>(price * static_cast<double>(priceMul)); // * *(float*)(a1+73)
        if (mode == 2 || mode == 4)
            unit = static_cast<float>(CaravanLookupPrice(s.goodId(), sourceCtx101)); // price(good, a1[101])
        // fild DataPtr; fmul unit; fadd total; fstp total  (single-precision accum)
        total = static_cast<float>(static_cast<double>(s.dataPtr) * static_cast<double>(unit) +
                                   static_cast<double>(total));
    }

    // Second loop: buy grid (j = 0; j != 224; j += 14).
    for (const CaravanSlot& s : t.buy) {
        if (s.objectId == -1)
            continue;
        if (s.dataPtr == 0)
            continue;
        float unit; // v14 == [esp+var_24] (4-byte float)
        double price = CaravanLookupPrice(s.goodId(), ctx);
        if (ownerIsMarket)
            unit = static_cast<float>(price * kCaravanMarketPriceFactor);
        else
            unit = static_cast<float>(price * static_cast<double>(priceMul));
        if (mode == 2 || mode == 4)
            unit = static_cast<float>(CaravanLookupPrice(s.goodId(), sourceCtx101));
        total = static_cast<float>(static_cast<double>(s.dataPtr) * static_cast<double>(unit) +
                                   static_cast<double>(total));
    }

    // The original stores dword_63170C = (int)-v13 (a credit/cost). We return the
    // positive total (as a double-widened float) and let the caller negate /
    // truncate as needed.
    return static_cast<double>(total);
}

// gilde.exe 0x53f6bc — VIBE_TradeTransport_LoadFromStorage (deterministic core).
// One pass over the sell grid then the buy grid; per slot, decide whether to move
// cargo and at what unit price. The capacity (free space / carry capacity) comes
// from the already-translated inventory leaves (sim/inventory_capacity.cpp).
static void LoadGrid(const std::vector<CaravanSlot>& slots,
                     const std::vector<i32>& free, bool applyPricing,
                     bool ownerIsMarket, float priceMul, u8 priceCtx, u8 sellCtx,
                     int mode, float carrierCost, std::vector<CaravanLoadLine>& out) {
    for (std::size_t k = 0; k < slots.size(); ++k) {
        const CaravanSlot& s = slots[k];
        // if ( dword_122ExA0[i] == -1 || !VIBE_Object_GetDataPtr(...) ) continue;
        if (s.objectId == -1)
            continue;
        if (s.dataPtr == 0)
            continue;
        i32 amount = (k < free.size()) ? free[k] : 0; // v56 / v55
        // if ( v56 > 0 ) ... (else slot skipped)
        if (amount <= 0)
            continue;
        // Resource-destination guard: dword_122ExC0[i] >= 0 (storageSlot).
        if (s.storageSlot < 0)
            continue;

        // v57 / v54: the unit price is computed and stored as a 32-bit FLOAT
        // (fstp [esp+var_10] @0x53f7b2 / 0x53fa07), so the factor/override is
        // applied in single precision.
        float unit; // v57 / v54
        if (applyPricing) { // if ( v49 )
            double price = CaravanLookupPrice(s.goodId(), priceCtx);
            if (ownerIsMarket) // *v45 == 10
                unit = static_cast<float>(price * kCaravanMarketPriceFactor); // * dbl_623F28 (1.1)
            else
                unit = static_cast<float>(price * static_cast<double>(priceMul)); // *(float*)(a4+73)
            // a8 == 2 || a8 == 4: sell at remote contor (re-lookup at a4[101], no factor).
            if (mode == 2 || mode == 4)
                unit = static_cast<float>(CaravanLookupPrice(s.goodId(), sellCtx));
        } else {
            unit = 0.0f; // v57 = 0.0
        }

        CaravanLoadLine line;
        line.goodId = s.goodId();
        line.quantity = amount;
        line.toSlot = s.storageSlot;
        // v26 = v57 / a6;  ConvertX (truncate toward zero); v38 = (__int64)v26.
        double scaled = static_cast<double>(unit) / static_cast<double>(carrierCost);
        line.unitPrice = static_cast<i32>(scaled); // C cast truncates == ConvertX
        out.push_back(line);
    }
}

std::vector<CaravanLoadLine> CaravanLoadFromStorage(
    const CaravanCargoTables& t, const std::vector<i32>& sellFree,
    const std::vector<i32>& buyFree, bool applyPricing, bool ownerIsMarket,
    float priceMul, u8 priceCtx, u8 sellCtx, int mode, float carrierCost) {
    std::vector<CaravanLoadLine> lines;
    LoadGrid(t.sell, sellFree, applyPricing, ownerIsMarket, priceMul, priceCtx,
             sellCtx, mode, carrierCost, lines);
    LoadGrid(t.buy, buyFree, applyPricing, ownerIsMarket, priceMul, priceCtx,
             sellCtx, mode, carrierCost, lines);
    return lines;
}

} // namespace guild::world

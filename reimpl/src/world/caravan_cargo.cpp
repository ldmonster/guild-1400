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
double CaravanComputeCargoValue(const CaravanCargoTables& t, bool ownerIsMarket,
                                float priceMul, bool sourceKind71, u8 sourceCtx101,
                                int mode) {
    double total = 0.0; // v13

    // ctx = (*a1 == 71) ? *(_DWORD*)(a1+101) : byte_6477A1.
    u8 ctx = sourceKind71 ? sourceCtx101 : kCaravanDefaultPriceContext; // v16

    // First loop: sell grid (i = 0; i != 112; i += 14).
    for (const CaravanSlot& s : t.sell) {
        if (s.objectId == -1)
            continue;
        if (s.dataPtr == 0) // VIBE_Object_GetDataPtr(...) == 0
            continue;
        double unit; // v15
        double price = CaravanLookupPrice(s.goodId(), ctx);
        if (ownerIsMarket) // *v4 == 10
            unit = price * kCaravanMarketPriceFactor; // * dbl_623F30 (1.1)
        else
            unit = price * static_cast<double>(priceMul); // * *(float*)(a1+73)
        if (mode == 2 || mode == 4)
            unit = CaravanLookupPrice(s.goodId(), sourceCtx101); // price(good, a1[101])
        total += static_cast<double>(s.dataPtr) * unit; // (double)DataPtr * v15 + v13
    }

    // Second loop: buy grid (j = 0; j != 224; j += 14).
    for (const CaravanSlot& s : t.buy) {
        if (s.objectId == -1)
            continue;
        if (s.dataPtr == 0)
            continue;
        double unit; // v14
        double price = CaravanLookupPrice(s.goodId(), ctx);
        if (ownerIsMarket)
            unit = price * kCaravanMarketPriceFactor;
        else
            unit = price * static_cast<double>(priceMul);
        if (mode == 2 || mode == 4)
            unit = CaravanLookupPrice(s.goodId(), sourceCtx101);
        total += static_cast<double>(s.dataPtr) * unit;
    }

    // The original stores dword_63170C = (int)-v13 (a credit/cost). We return the
    // positive total and let the caller negate / truncate as needed.
    return total;
}

// gilde.exe 0x53f6bc — VIBE_TradeTransport_LoadFromStorage (deterministic core).
// One pass over the sell grid then the buy grid; per slot, decide whether to move
// cargo and at what unit price. The capacity (free space / carry capacity) comes
// from the already-translated inventory leaves (sim/inventory_capacity.cpp).
static void LoadGrid(const std::vector<CaravanSlot>& slots,
                     const std::vector<i32>& free, bool applyPricing,
                     bool ownerIsMarket, float priceMul, u8 priceCtx, u8 sellCtx,
                     int mode, std::vector<CaravanLoadLine>& out) {
    for (std::size_t k = 0; k < slots.size(); ++k) {
        const CaravanSlot& s = slots[k];
        // if ( dword_122ExA0[i] == -1 || !VIBE_Object_GetDataPtr(...) ) continue;
        if (s.objectId == -1)
            continue;
        if (s.dataPtr == 0)
            continue;
        i32 amount = (k < free.size()) ? free[k] : 0; // v58 / v57
        // if ( v58 > 0 ) ... (else slot skipped)
        if (amount <= 0)
            continue;
        // Resource-destination guard: dword_122ExC0[i] >= 0 (storageSlot).
        if (s.storageSlot < 0)
            continue;

        double unit; // v59 / v56
        if (applyPricing) { // if ( v51 )
            double price = CaravanLookupPrice(s.goodId(), priceCtx);
            if (ownerIsMarket) // *v47 == 10
                unit = price * kCaravanMarketPriceFactor;
            else
                unit = price * static_cast<double>(priceMul); // *(float*)(a4+73)
            // a8 == 2 || a8 == 4: sell at remote contor (re-lookup, no factor).
            if (mode == 2 || mode == 4)
                unit = CaravanLookupPrice(s.goodId(), sellCtx);
        } else {
            unit = 0.0; // v59 = 0.0
        }

        CaravanLoadLine line;
        line.goodId = s.goodId();
        line.quantity = amount;
        line.toSlot = s.storageSlot;
        line.unitPrice = unit;
        out.push_back(line);
    }
}

std::vector<CaravanLoadLine> CaravanLoadFromStorage(
    const CaravanCargoTables& t, const std::vector<i32>& sellFree,
    const std::vector<i32>& buyFree, bool applyPricing, bool ownerIsMarket,
    float priceMul, u8 priceCtx, u8 sellCtx, int mode) {
    std::vector<CaravanLoadLine> lines;
    LoadGrid(t.sell, sellFree, applyPricing, ownerIsMarket, priceMul, priceCtx,
             sellCtx, mode, lines);
    LoadGrid(t.buy, buyFree, applyPricing, ownerIsMarket, priceMul, priceCtx,
             sellCtx, mode, lines);
    return lines;
}

} // namespace guild::world

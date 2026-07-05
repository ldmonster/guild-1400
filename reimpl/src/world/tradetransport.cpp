#include "world/tradetransport.h"

#include "world/amt.h"  // AmtTrunc, AmtCommitTransfer

namespace guild::world {

namespace {
MarketPriceHook g_marketPriceHook = nullptr;
} // namespace

void TradeTransportSetMarketPriceHook(MarketPriceHook hook) {
    g_marketPriceHook = hook;
}

double TradeTransportLookupMarketPrice(i32 goodId, u8 context) {
    if (g_marketPriceHook)
        return g_marketPriceHook(goodId, context);
    return 0.0;
}

// gilde.exe 0x592220 — VIBE_TradeTransport_ComputeCartCost.
//   v2 = a1;
//   if ((double)a1 > 0.0) { if (a1 < 32000) v2 = 32000; if (v2 > 256000) v2 = 256000; }
//   switch (mode) { 1: v8 = trunc(v2*0.05); 2: trunc(v2*0.1); 3: trunc(v2*0.15); }
//   return trunc((double)v8 + 0.5);
i32 TradeTransportComputeCartCost(i32 goodValue, TransportMode mode) {
    i32 v2 = goodValue;
    if (static_cast<double>(goodValue) > 0.0) {
        if (goodValue < kCartValueFloor)
            v2 = kCartValueFloor;
        if (v2 > kCartValueCeil)
            v2 = kCartValueCeil;
    }

    i32 fee = 0;
    switch (mode) {
        case TransportMode::Slow:
            fee = AmtTrunc(static_cast<double>(v2) * static_cast<double>(kCartRateMode1));
            break;
        case TransportMode::Medium:
            fee = AmtTrunc(static_cast<double>(v2) * static_cast<double>(kCartRateMode2));
            break;
        case TransportMode::Fast:
            fee = AmtTrunc(static_cast<double>(v2) * static_cast<double>(kCartRateMode3));
            break;
        case TransportMode::None:
        default:
            fee = 0; // no case matched: v8 stays 0
            break;
    }
    return AmtTrunc(static_cast<double>(fee) + kCartCostBase);
}

// gilde.exe 0x53ff3c — VIBE_TradeTransport_ComputeCargoValue.
//   for each slot: if id != -1 and qty != 0:
//     price = LookupCachedMarketPrice(goodId, marketCtx);
//     unit  = (float)((ownerKind==10) ? price * 1.1 : price * priceMul);  // fstp v15
//     if (panelMode==2 || panelMode==4)
//         unit = (float)LookupCachedMarketPrice(goodId, sellContext);
//     value = (float)((double)qty * unit + value);                        // fstp v13
//   result = -value;  (we return +value)
// PRECISION (verified against the binary): the unit price (v15 @esp+8) and the
// running total (v13 @esp+0) are 32-bit float stack slots — every product is
// computed on the x87 in extended precision but stored back through `fstp
// dword` each iteration. `panelMode` is the dispatcher's dl mode byte
// (1 = market, 2 = import/contor 475, 4 = export/contor 476) — NOT the cart's
// TransportMode; the original tests `a3 == 2 || a3 == 4`.
// The original walks two physical slot arrays (8 + 16 entries); the caller
// flattens both into `slots`. Behaviour is identical per slot.
double TradeTransportComputeCargoValue(const std::vector<CargoSlot>& slots,
                                       bool ownerIsMarket, float priceMul,
                                       u8 marketCtx, u8 sellContext,
                                       int panelMode) {
    float value = 0.0f;                                       // v13 (float slot)
    bool sellAtContor = (panelMode == 2 || panelMode == 4);   // a3 == 2 || a3 == 4
    for (const CargoSlot& s : slots) {
        if (s.goodId == -1)
            continue;
        if (s.quantity == 0)
            continue;
        double price = TradeTransportLookupMarketPrice(s.goodId, marketCtx);
        float unit = static_cast<float>(ownerIsMarket
                          ? price * kMarketPriceFactor
                          : price * static_cast<double>(priceMul));  // v15 (float slot)
        if (sellAtContor)
            unit = static_cast<float>(
                TradeTransportLookupMarketPrice(s.goodId, sellContext));
        // fild qty; fmul v15; fadd v13; fstp v13 — 80-bit intermediate, float store.
        value = static_cast<float>(static_cast<double>(s.quantity) * unit + value);
    }
    return value;
}

// gilde.exe 0x53f404 — VIBE_TradeTransport_AssignRoute (cost + 1-day time).
//   ... GameTime_Advance(&t, 0, 1, 0);   // +1 day
//   if (cart has cargo) { cost = ComputeCartCost(cargoValue, mode);
//                         QueueRequest16(-1, ownerAccount, cost, currency); }
RouteResult TradeTransportAssignRoute(i32 cargoValue, TransportMode mode,
                                      i32 ownerAccount, bool commit) {
    RouteResult r;
    r.travelDays = 1;
    if (mode == TransportMode::None) {
        // No transport selected: the original only charges when the cart's +40
        // mode byte is non-zero.
        r.cost = 0;
        return r;
    }
    r.cost = TradeTransportComputeCartCost(cargoValue, mode);
    if (commit)
        r.charged = AmtCommitTransfer(-1, ownerAccount, r.cost, 0);
    return r;
}

} // namespace guild::world

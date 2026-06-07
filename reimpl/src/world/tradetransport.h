#pragma once
// Trade transport — caravan/cart cost & cargo-value rules core (gilde.exe).
//
// Translated functions (rules core):
//   VIBE_TradeTransport_ComputeCartCost   0x592220   (cost by mode + clamp)
//   VIBE_TradeTransport_ComputeCargoValue 0x53ff3c   (cargo valuation)
//   VIBE_TradeTransport_AssignRoute       0x53f404   (route cost + +1 day time)
//
// The cart caravans move goods between cities; each route costs a per-good fee
// (scaled by transport mode) plus a fixed base, and takes a fixed time of one
// game day (VIBE_GameTime_Advance(.., 0, 1, 0) in AssignRoute). The valuation
// sums quantity*market-price across the cargo slots. Money mutations and the
// live object/market tables are routed through hooks for isolated testing.
#include <vector>

#include "guild/common/types.h"

namespace guild::world {

// Transport modes (the `a2` switch in ComputeCartCost; AssignRoute passes the
// cart's +40 byte). Mode 0 / out-of-range yields no per-good fee (base only).
enum class TransportMode { None = 0, Slow = 1, Medium = 2, Fast = 3 };

// gilde.exe 0x592220 — VIBE_TradeTransport_ComputeCartCost(goodValue, mode).
//   if goodValue > 0: clamp to [32000, 256000]
//   fee = trunc(goodValue * rate(mode))   (mode 1->0.05, 2->0.1, 3->0.15)
//   return trunc(fee + 0.5)
// The clamp only applies when the input is strictly positive (the original
// guards the clamp with `(double)a1 > 0.0`). For mode None the per-good fee is
// 0 and the result is trunc(0 + 0.5) == 0.
i32 TradeTransportComputeCartCost(i32 goodValue, TransportMode mode);

// One cargo slot for valuation: a good id + its quantity (the original reads the
// object data-ptr count and the cached market price by good id).
struct CargoSlot {
    i32 goodId = -1; // -1 == empty slot (skipped)
    i32 quantity = 0;
};

// Market-price lookup hook (gilde.exe VIBE_Building_LookupCachedMarketPrice
// 0x58f6b8). Takes the good id and a currency/context byte; returns the per-unit
// price. Tests inject a deterministic table; default returns 0.
using MarketPriceHook = double (*)(i32 goodId, u8 context);
void TradeTransportSetMarketPriceHook(MarketPriceHook hook);
double TradeTransportLookupMarketPrice(i32 goodId, u8 context);

// gilde.exe 0x53ff3c — VIBE_TradeTransport_ComputeCargoValue.
// Sums over the cargo slots:  value += quantity * unitPrice * priceFactor,
// where priceFactor is 1.1 (dbl_623F30) when the owner building is a market
// (ownerKind==10) and `priceMul` otherwise. For modes 2/4 ("sell at the
// remote contor") the price is re-looked-up with `sellContext` and NO factor.
// The original stores the negated total (it is a cost/credit); we return the
// positive total and let the caller negate. `priceMul` is the per-cart price
// scalar at object+73; `marketCtx`/`sellContext` are the two price-lookup
// context bytes.
double TradeTransportComputeCargoValue(const std::vector<CargoSlot>& slots,
                                       bool ownerIsMarket, float priceMul,
                                       u8 marketCtx, u8 sellContext,
                                       TransportMode mode);

// Constant exposed for tests / callers.
constexpr double kMarketPriceFactor = 1.1; // dbl_623F30 (0x3FF199999999999A)

// gilde.exe 0x53f404 — VIBE_TradeTransport_AssignRoute (cost + time portion).
// A route assignment charges the cart cost (cost of the loaded cargo by mode)
// and advances the cart's arrival time by exactly one game day. This recovers
// the cost+time rule; the handler-chain/network commit is routed through the
// Amt transfer hook (payer = -1 city sink, recipient = cart owner account).
struct RouteResult {
    i32 cost = 0;       // TradeTransportComputeCartCost(cargoValue, mode)
    int travelDays = 1; // VIBE_GameTime_Advance(.., 0, 1, 0)
    bool charged = false;
};
RouteResult TradeTransportAssignRoute(i32 cargoValue, TransportMode mode,
                                      i32 ownerAccount, bool commit);

} // namespace guild::world

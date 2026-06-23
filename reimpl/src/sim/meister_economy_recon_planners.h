#pragma once
// ===========================================================================
// MeisterAi economy TRADE planners — pure decision-logic reconstruction.
//
// gilde.exe 0x4614d0  VIBE_MeisterAi_TradeGeneral          (__usercall eax=AiPlayer, edx=cart)
// gilde.exe 0x46329c  VIBE_MeisterAi_TradeRemotePurchaseA  (__usercall eax=AiPlayer, edx=cart)
// gilde.exe 0x463c4c  VIBE_MeisterAi_TradeRemotePurchaseB  (__usercall eax=AiPlayer, edx=cart)
//
// These three are the large (1000+-instruction) Guild-Master daily trade planners.
// Each runs over the per-turn city-goods scratch tables that the workstation /
// storage sweep (0x4599f0 / 0x45a62c) already populated:
//   * the item table     dword_B5444E[] (stride 64, count dword_B56FE0)
//   * the workstation tbl dword_B56464[] (stride 88, count dword_B56FDC)
//   * the "sell list"     dword_B564AC[]/dword_B564B4[] (planned-qty columns)
//   * the storage list    dword_B54468[]/dword_B54484[] (planned-qty columns)
// plus a cart-routing tail that walks the scene tree, unloads the cart, reconciles
// what is already aboard, and emits cm_RequestSellObjekt / EnqueueCmd commands.
//
// What is reconstructed here 1:1 (the deterministic, golden-testable PLAN math):
//   - TradeGeneral:
//       * the item-table demand pre-pass (slotCap/3 < stock → plan = stock)
//       * the profit-margin sell decision (ratio<1 && roll*1.25 >= ratio → dump)
//       * the cash-strapped emergency sell loop (qty = max(1,32000/price), proceeds)
//       * the overstock sell decision (3*slotCap/4 < stock → max(5,stock/4))
//   - TradeRemotePurchaseA / B:
//       * the cart-trip count clamp ((rand)*0.006, clamped to [5,16])
//       * the storage-restock build-up / build-down decision against a target
//         (need = (target*9/8)*1.03 when low; remove (count-target*9/8)*1.03 when
//         > 2*target), reproducing the binary's signed /8 idiom EXACTLY.
//       * the per-source quantity clamp (min(slotCapacity, plannedQty)).
//
// What is DEFERRED (engine-coupled, not pure plan math — reported, not faked):
//   - the scene-tree cart walk (GameObject_QueryFind/IterNext), He-handler
//     iteration, Command_Queue*/EnqueueCmd emission, GameTime advance, and the
//     dozens of German cm_RequestSellObjekt log lines. Those are pure plumbing
//     over live engine objects; the COUPLED leaf reads are surfaced as inert hooks
//     (MarketSnapshot inputs + explicit RNG draws) so the decision math is
//     deterministic and verifiable.
//
// Recovered constants (gilde.exe, byte-exact — see get_bytes):
//   flt_619E9C = 1.25   (profit-sell roll slope; TradeGeneral, NO +0.25 bias)
//   dbl_619EA0 = 0.006  (TradeGeneral cart-trip count scale)
//   dbl_619F58 = 0.006  (TradeRemotePurchaseA cart-trip count scale)
//   dbl_619F60 = 1.03   (TradeRemotePurchaseA restock-qty price scale)
//   dbl_619F68 = 0.006  (TradeRemotePurchaseB cart-trip count scale)
//   dbl_619F70 = 1.03   (TradeRemotePurchaseB restock-qty price scale)
// ===========================================================================
#include <functional>
#include <vector>

#include "guild/common/types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Recovered scalar constants.
// ---------------------------------------------------------------------------
constexpr float  kProfitSellSlope     = 1.25f;  // flt_619E9C (TradeGeneral)
constexpr int    kEmergencyFundsFloor = 3200;   // *(AiPlayer+440) < 3200
constexpr int    kEmergencyHeldFloor  = 8000;   // Person_SumCurrencyHeld < 8000
constexpr int    kEmergencyTarget     = 32000;  // running-proceeds target
constexpr int    kOverstockMin        = 5;      // overstock sell floor
constexpr double kCartTripScale       = 0.006;  // dbl_619EA0 / F58 / F68
constexpr int    kCartTripMin         = 5;      // clamp lower bound
constexpr int    kCartTripMax         = 16;     // clamp upper bound
constexpr double kRestockPriceScale   = 1.03;   // dbl_619F60 / F70

// ===========================================================================
// gilde.exe 0x5c6b08 VIBE_Coord_ConvertX — the float->int truncation the planners
// route every floating result through. The original loads the FP value, stores via
// fistp with the FPU in truncate (chop) mode, i.e. truncation toward zero. We
// reproduce that exactly with a (int) cast on a double.
// ===========================================================================
inline int CoordTrunc(double v) { return static_cast<int>(v); }

// ===========================================================================
// gilde.exe 0x4614d0 — TradeGeneral DECISION KERNELS.
// ===========================================================================

// Top-of-function item pre-pass (loop over dword_B56FE0 workstations reading the
// word_B5448C bits + dword_B54478 stock + dword_B54468 plan column):
//   if ((bits & 6) == 0 && (bits & 8) != 0 && slotCap/3 < stock) plan = stock.
// Returns the new plan quantity (0 = no change). `slotCap` = Inventory_GetSlotCapacity.
int GeneralStoragePrePass(int bits, int stock, int slotCap);

// Profit-margin sell over the workstation table (the odd-hour block):
//   gate: (bits & 6) == 0, and ((bits & 8) == 0 || slotCap/3 < stock).
//   sell whole stock iff (sellPrice/buyPrice) < 1.0 AND roll*1.25 >= ratio.
// `sellPrice` = Building_ComputeMarketPrice(typeId,100); `buyPrice` =
// Building_LookupCachedMarketPrice(typeId). `roll` is one RandomFloatScaled() draw.
// Returns the qty to sell (whole stock) or 0. NOTE: unlike TradeManageStorage there
// is NO +0.25 bias on the roll threshold (flt_619E9C path only).
int GeneralProfitSell(int bits, int stock, float sellPrice, float buyPrice,
                      double roll, int slotCap);

// Emergency sell (the funds<3200 || held<8000 block). Per unmatched workstation,
// while running *proceeds < 32000: qty = max(1, 32000/price), clamp to stock; the
// running proceeds accumulate the UN-clamped qty*price (the original adds v23*price
// where v23 is the pre-clamp qty). Returns the clamped sell qty (0 = skip).
int GeneralEmergencySell(int bits, int stock, int price, int* proceeds);

// Overstock sell: gate (bits & 6) == 0; if 3*slotCap/4 < stock, sell
// max(5, stock/4) clamped to stock. Returns the qty (0 = below the line).
int GeneralOverstockSell(int bits, int stock, int slotCap);

// ===========================================================================
// gilde.exe 0x46329c / 0x463c4c — Remote-purchase DECISION KERNELS.
// ===========================================================================

// The cart-trip-count clamp shared by both remote planners:
//   raw = CoordTrunc(activeByTurn * 0.006); n = clamp(raw, 5, 16).
// `activeByTurn` is flt_641DAC (Character_CountActiveByTurn result). Returns the
// clamped trip count; the original sets a "too many trips" flag when the live
// queued count exceeds this — surfaced via the return for the caller to compare.
int RemoteCartTripCount(double activeByTurn);

// The storage restock build-up / build-down decision (the `for k in 1..3` loop in
// both remote planners). Given the current count at the destination market location
// (GameObject_CountAtLocation) and the target stock `target` (the accumulated
// v77[...+184] weighted demand), decide the restock command magnitude:
//   * if count <  target          → build UP by  CoordTrunc((target*9/8) * 1.03)
//   * if count > 2*target         → build DOWN by CoordTrunc((count - target*9/8) * 1.03)
//   * otherwise                   → 0 (no command)
// The `target*9/8` term reproduces the binary's signed division idiom EXACTLY:
//   (9*t - ( (((9*t)>>31)<<3) + 8*((9*t)>>31) )) >> 3   ==  trunc-toward-zero(9*t/8)
// for negative 9*t (it is the standard compiler expansion of `(9*t)/8`). Returns a
// signed magnitude: >0 = build up, <0 = build down (abs = units), 0 = none.
int RemoteRestockDelta(int count, int target);

// The exact signed `(9*t)/8` idiom the binary emits (exposed for golden testing).
int NineEighthsTrunc(int t);

// Per-source planned-purchase quantity clamp (the inner sell-loop in both
// planners): plannedQty clamped to the cart/storage free capacity.
//   qty = min(slotCapacity, plannedQty).
// `slotCapacity` = Inventory_GetSlotCapacity(targetStorage); `plannedQty` =
// dword_B564B4[]/dword_B54484[] column.
int RemotePlanQty(int slotCapacity, int plannedQty);

// ===========================================================================
// Snapshot-driven ORCHESTRATION (deterministic, golden-testable).
//
// Each input row is one entry of the relevant scratch table; the engine-coupled
// leaf reads (market prices, slot capacity, current stock, cart count) are passed
// in as plain numbers so the PLAN is a pure function of the snapshot + RNG draws.
// ===========================================================================

// A workstation row from dword_B56464[] for the TradeGeneral sell decisions.
struct GeneralWorkstation {
    u16   typeId   = 0;     // HIWORD(dword_B56464[..]) item/scene id
    int   bits     = 0;     // word_B564BC flags (0x02 reserve, 0x04 matched, 0x08 flagged)
    int   stock    = 0;     // dword_B564A4 current stock
    float sellPrice = 0.0f; // Building_ComputeMarketPrice(typeId,100)
    float buyPrice  = 0.0f; // Building_LookupCachedMarketPrice(typeId)
    int   plannedQty = 0;   // dword_B564AC sell-plan column (output)
};

// The faction snapshot the TradeGeneral planner reads.
struct GeneralPlayer {
    bool oddHour      = false; // WORD2(qword_13CE852) % 2 — odd-hour profit/emergency block
    bool sellDoneFlag = false; // (*(AiPlayer+437) & 2) — block runs once per odd hour
    int  funds        = 0;     // *(AiPlayer+440)
    int  heldCurrency = 0;     // Person_SumCurrencyHeld
    int  slotCap      = 0;     // Inventory_GetSlotCapacity(cart)
};

// A planned-sell decision the planner produces (collapses cm_RequestSellObjekt).
enum class TradeDecisionKind {
    StoragePrePass,  // GeneralStoragePrePass plan stamp
    ProfitSell,      // whole-stock dump on good margin
    EmergencySell,   // cash-raising forced sell
    OverstockSell,   // trim excess
    RemoteRestockUp, // remote-purchase build-up command
    RemoteRestockDown, // remote-purchase build-down command
    RemotePlanSell,  // remote-purchase per-source sell qty
};
struct TradeDecision {
    TradeDecisionKind kind;
    u16 typeId = 0;
    int qty    = 0;   // signed; for RemoteRestockDown this is the (negative) delta
};

// gilde.exe 0x4614d0 — run the TradeGeneral sell decisions over the workstation
// snapshot. Mirrors the original ordering: the odd-hour profit / emergency block
// (only when oddHour && !sellDoneFlag), then the always-run overstock pass.
//   roll() returns one RandomFloatScaled() draw per profit-sell candidate.
// Mutates each row's plannedQty; appends decisions to `out`; returns count emitted.
int RunTradeGeneral(GeneralPlayer& player,
                    std::vector<GeneralWorkstation>& stations,
                    const std::function<double()>& roll,
                    std::vector<TradeDecision>& out);

// A remote-purchase source row (dword_B56464[]/dword_B5647C[] sell-list entry).
struct RemoteSource {
    u16 typeId      = 0;   // HIWORD(dword_B56464[..]+2) item id
    int plannedQty  = 0;   // dword_B564B4[]/dword_B54484[] planned purchase column
};

// gilde.exe 0x46329c / 0x463c4c — run the remote-purchase plan over a list of
// candidate source rows for a single target market. For each source: the per-source
// purchase quantity is clamped to `slotCapacity` (cart capacity). The market
// restock-balance command (build up / down) is computed once per call from
// `currentCount` vs `target`. `cartActiveByTurn` drives the trip-count clamp.
// Appends decisions to `out`; returns count emitted. (Variant A and B share this
// math; the only binary difference is the table layout, identical decisions.)
int RunRemotePurchase(int slotCapacity, int currentCount, int target,
                      double cartActiveByTurn,
                      const std::vector<RemoteSource>& sources,
                      std::vector<TradeDecision>& out);

} // namespace guild::sim

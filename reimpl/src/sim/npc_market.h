#pragma once
// NpcMarket — the MARKET-SUPERVISOR director for the Guild simulation
// (gilde.exe). This translates the last "big" NPC director alongside the daily
// routine:
//
//   VIBE_NpcAction_RunMarktSupervisorStep (0x4e8bdc, 0x152d bytes) — the
//     market-stall pricing / restock director. Per round (gated on the market
//     enable flag dword_6477A4 and the clock hour band 0..0x14), it: (1) tops up
//     the market person's treasury (EnqueueCmd15 if currency < 5,120,000);
//     (2) sweeps the 62 stall slots of the central market storage (byte_6477A1 =
//     market index) recomputing each item's PRICE and deciding BUY/SELL restock;
//     (3) sweeps up to 3 satellite markets × 62 slots × {sell, buy} object
//     categories. It re-arms the He appointment (+96 / +82) and advances state.
//
// The per-slot PRICE RECOMPUTE + RESTOCK DECISION is the core RULE and is
// translated 1:1 here (MarketRecomputeSlot): it is a deterministic float
// pipeline keyed on the elapsed minutes, the target stock, the effective stock
// on hand, and a sequence of crt RNG draws (RandomModulo / RandomFloatScaled),
// using the recovered constants below and the x87 round-toward-zero truncation
// VIBE_Coord_ConvertX (== (int) cast). It emits VIBE_Command_QueueRequest17
// (the buy/sell stock-transfer command) and VIBE_Command_QueueRequestTransform64
// (the price commit). All cross-cluster leaves (Inventory_GetEffectiveStock,
// Building_FindActiveWorkSlot, Building_ComputeSlotYield, the command emits) go
// through NpcMarketHooks so the pricing math is exercisable in isolation.
//
// Recovered constants (get_bytes @0x61F9A4..0x61FA18) are listed below.
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::sim {

// ===========================================================================
// Recovered IEEE-754 constants (get_bytes; byte-faithful).
//   flt_61F9A4 = 0.0714285746  (1/14, "per-slot demand rate")
//   dbl_61F9A8 = 0.0166666667  (1/60, minutes->fraction-of-hour)
//   flt_61FA18 = 80.0          (RNG bias for the buy/price scatter)
//   flt_61F9B0 = 0.01          (price-scatter scale)
//   flt_61F9C8 = 0.6           (oversupply price decay factor)
//   dbl_61F9B8 = 0.5           (half-target threshold)
//   dbl_61F9C0 = 0.75          (3/4-target threshold)
//   dbl_61F9D0 = 0.25          (price floor fraction of base)
//   flt_61F9D8 = 0.8           (understock price-rise factor)
//   flt_61F9DC = 1.5           (price ceiling fraction of base)
//   flt_61F9E0 = 0.4           (price reset-low fraction of base)
//   dbl_61F9E8 = 3.0           (sell-branch ceiling fraction)
//   dbl_61F9F0 = 0.33          (sell quantity -> price contribution)
//   flt_61F9F8 = 1.2           (sell reset-high fraction)
//   dbl_61FA00 = 0.4           (satellite buy price step)
//   flt_61FA08 = 2.5           (satellite price ceiling fraction)
//   dbl_61FA10 = 0.1           (satellite oversupply decay)
// ===========================================================================
extern const float  kMktDemandRate;     // flt_61F9A4
extern const double kMktMinFrac;         // dbl_61F9A8
extern const float  kMktScatterBias;     // flt_61FA18
extern const float  kMktScatterScale;    // flt_61F9B0
extern const float  kMktOverDecay;       // flt_61F9C8
extern const double kMktHalf;            // dbl_61F9B8
extern const double kMktThreeQuarter;    // dbl_61F9C0
extern const double kMktPriceFloorFrac;  // dbl_61F9D0
extern const float  kMktRiseFactor;      // flt_61F9D8
extern const float  kMktCeilFrac;        // flt_61F9DC
extern const float  kMktResetLowFrac;    // flt_61F9E0
extern const double kMktSellCeilFrac;    // dbl_61F9E8
extern const double kMktSellPriceContrib;// dbl_61F9F0
extern const float  kMktSellResetHi;     // flt_61F9F8

// Market person treasury threshold (the EnqueueCmd15 top-up).
constexpr i32 kMktTreasuryTopUp = 5120000;

// ===========================================================================
// A single stall slot's pricing state (the 128-byte stall record the original
// qmemcpy's out of word_13C3B60). We model exactly the fields the pricing math
// touches (byte offsets into the 128-byte record):
//   itemType : +0  (word)  — the stall item type id (0 == empty slot)
//   buyQty   : +16 (int)   — accumulated buy quantity (offset +274-258)
//   sellQty  : +20 (int)   — accumulated sell quantity
//   refValue : +32 (float) — reference/base value (offset +290-258)
//   price    : +36 (float) — current unit price (mutated)
//   targetStk: +52 (int)   — target stock level (offset +310-258)
//   yield    : +56 (float) — current yield (sometimes recomputed)
// The slot is identified by its index within the market (0..61).
// ===========================================================================
struct MarketSlot {
    i16   itemType;   // +0
    i32   buyQty;     // +16
    i32   sellQty;    // +20
    float refValue;   // +32
    float price;      // +36
    i32   targetStk;  // +52
    float yield;      // +56
    bool  active;     // false => empty slot (skip)
};

// The restock decision a slot recompute can emit (the RULE outcome), captured
// for golden testing. `kind` distinguishes buy vs sell vs none.
enum class MarketRestock { kNone, kBuy, kSell };

struct MarketSlotResult {
    MarketRestock restock;
    i32           quantity;   // QueueRequest17 transfer amount
    float         newPrice;   // committed price after recompute
    bool          yieldRecomputed;
};

// ===========================================================================
// Leaf hooks. Tests install a synthetic provider; nullptr installs an inert
// default. These mirror the originals' cross-cluster leaf calls.
// ===========================================================================
struct NpcMarketHooks {
    // marketEnabled(): dword_6477A4 nonzero (the supervisor is active).
    bool (*marketEnabled)();
    // marketIndex(): byte_6477A1 (the central market index).
    u8   (*marketIndex)();
    // treasury(): VIBE_Person_GetCurrencyAmount(market, idx). < 5,120,000 -> top up.
    i32  (*treasury)();
    // enqueueTopUp(amount): VIBE_Command_EnqueueCmd15 treasury top-up.
    void (*enqueueTopUp)(i32 amount);

    // --- stall provider ---
    // slotCount(): number of stall slots to sweep (the original is fixed 62).
    int  (*slotCount)();
    // slot(i): the stall record for index i.
    MarketSlot (*slot)(int i);
    // commitSlot(i, &s): write the mutated stall record (price/qty/yield) back.
    void (*commitSlot)(int i, const MarketSlot* s);
    // slotIsActiveWorkable(i): the Building_FindActiveWorkSlot + type-def gate
    //   (*v11 == 37 || *v11 == 23) for the central-market sweep. Returns false to
    //   skip the slot (the original logs "Could not find markt-lager" when the
    //   work slot is null but the type matched).
    bool (*slotIsActiveWorkable)(int i);
    // effectiveStock(i): VIBE_Inventory_GetEffectiveStock for the slot's item
    //   (the QueryFind(...) + GetEffectiveStock chain). 0 == none.
    i32  (*effectiveStock)(int i);
    // computeYield(i): VIBE_Building_ComputeSlotYield(marketIdx, i).
    float (*computeYield)(int i);

    // --- command emissions ---
    // queueRequest17(from, to, qty, itemType, market): the stock-transfer cmd.
    void (*queueRequest17)(i32 from, i32 to, i32 qty, i16 itemType, u8 market);
    // queueTransform64(slotRecord, market): commit the slot's recomputed price.
    void (*queueTransform64)(const MarketSlot* s, u8 market);
    // worldSyncSuppressed(): (word_63C740 & 0x80) != 0 — when set, the sell-side
    //   QueueRequest17 in the understock branch is suppressed (host-only path).
    bool (*worldSyncSuppressed)();

    // --- completion ---
    i32  (*freeHandlerEntry)(HeRecord* h);
};

void SetNpcMarketHooks(const NpcMarketHooks* hooks);
const NpcMarketHooks& GetNpcMarketHooks();

// ===========================================================================
// The core per-slot price recompute + restock decision (the RULE).
//   Inputs: the slot state, the elapsed minutes since the last supervisor run
//   (GameTime_DiffMinutes), the slot index (for yield), the market index. The
//   RNG (crt::RandNext via util::RandomModulo / util::RandomFloatScaled) is
//   consumed in the exact count and order of the original:
//     1) RandomModulo(0xA)   — discarded (a demand jitter advance)
//     2) RandomModulo(0x64)  — the price scatter (0..99) + 80 bias
//     3..4) RandomFloatScaled — the price clamp probability rolls
//     5) RandomModulo(0x64)  — the yield-recompute gate (>10 or hour==12)
//   Mutates `slot` (price/buyQty/sellQty/yield) in place and returns the
//   restock decision. `clockHour` is WORD2(qword_13CE852).
// This is the 0x4e8ca3..0x4e91d3 central-market slot body, translated 1:1.
// ===========================================================================
MarketSlotResult MarketRecomputeSlot(MarketSlot* slot, int elapsedMinutes,
                                     i32 effectiveStock, int slotIndex,
                                     int clockHour, u8 marketIndex,
                                     float (*computeYield)(int),
                                     void (*queueRequest17)(i32, i32, i32, i16, u8));

// gilde.exe 0x4e8bdc — VIBE_NpcAction_RunMarktSupervisorStep(h@eax).
//   Returns the He record (or the free result). State machine on +112:
//   <-1 -> (==-2 -> SetTargetCityRef + state 0; else return state); 0 -> the
//   treasury top-up + central-market sweep + satellite sweeps, then re-arm
//   (+1 day) state 0 (or free if past the daily window).
HeRecord* NpcMarket_RunMarktSupervisorStep(HeRecord* h);

} // namespace guild::sim

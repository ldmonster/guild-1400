#include "sim/npc_market.h"

#include "sim/npcaction.h"   // NpcClock()
#include "sim/gametime.h"
#include "util/math_random.h"
#include "util/math_rng_float.h"
#include "crt/rand.h"

#include <cstdint>

namespace guild::sim {

// ===========================================================================
// Recovered constants (byte-faithful; see header for the get_bytes addresses).
// ===========================================================================
const float  kMktDemandRate      = 0.0714285746216774f;     // flt_61F9A4
const double kMktMinFrac         = 0.016666666666666666;    // dbl_61F9A8
const float  kMktScatterBias     = 80.0f;                   // flt_61FA18
const float  kMktScatterScale    = 0.009999999776482582f;   // flt_61F9B0
const float  kMktOverDecay       = 0.6000000238418579f;     // flt_61F9C8
const double kMktHalf            = 0.5;                      // dbl_61F9B8
const double kMktThreeQuarter    = 0.75;                     // dbl_61F9C0
const double kMktPriceFloorFrac  = 0.25;                    // dbl_61F9D0
const float  kMktRiseFactor      = 0.800000011920929f;      // flt_61F9D8
const float  kMktCeilFrac        = 1.5f;                     // flt_61F9DC
const float  kMktResetLowFrac    = 0.4000000059604645f;     // flt_61F9E0
const double kMktSellCeilFrac    = 3.0;                      // dbl_61F9E8
const double kMktSellPriceContrib= 0.33;                     // dbl_61F9F0
const float  kMktSellResetHi     = 1.2000000476837158f;     // flt_61F9F8

// ===========================================================================
// Leaf-hook plumbing.
// ===========================================================================
static const NpcMarketHooks kInert{};
static const NpcMarketHooks* g_hm = &kInert;

void SetNpcMarketHooks(const NpcMarketHooks* hooks) { g_hm = hooks ? hooks : &kInert; }
const NpcMarketHooks& GetNpcMarketHooks() { return *g_hm; }

// VIBE_Coord_ConvertX (0x5c6b08): x87 frndint with round-toward-zero control
// word -> truncation toward zero. For the values fed here the decompiler renders
// it as (int)v; we model the same truncation.
static inline i32 ConvertX(double v) { return static_cast<i32>(v); }

// ===========================================================================
// MarketRecomputeSlot — the central-market per-slot pricing body (0x4e8ca3 ..
// 0x4e91d3), translated 1:1. RNG draw count/order preserved exactly.
// ===========================================================================
MarketSlotResult MarketRecomputeSlot(MarketSlot* s, int elapsedMinutes,
                                     i32 effectiveStock, int slotIndex,
                                     int clockHour, u8 marketIndex,
                                     float (*computeYield)(int),
                                     void (*queueRequest17)(i32, i32, i32, i16, u8)) {
    MarketSlotResult out{ MarketRestock::kNone, 0, s->price, false };

    // (1) RandomModulo(0xA) — a demand jitter advance whose result is discarded.
    (void)util::RandomModulo(0xA);

    // v113 = targetStk * demandRate * (elapsedMin * minFrac)
    double v113 = static_cast<double>(s->targetStk) * kMktDemandRate
                * (static_cast<double>(elapsedMinutes) * kMktMinFrac);
    // (2) RandomModulo(0x64) — price scatter 0..99.
    int v127 = static_cast<int>(util::RandomModulo(0x64) & 0xFFFF);
    v113 = (static_cast<double>(v127) + kMktScatterBias) * v113 * kMktScatterScale;
    // clamp v113 >= 1.0 when nonzero (the & 0x7FFFFFFF != 0 test on the float bits).
    {
        float fv = static_cast<float>(v113);
        std::uint32_t bits;
        static_assert(sizeof(bits) == sizeof(fv), "float bit copy");
        __builtin_memcpy(&bits, &fv, sizeof(bits));
        if ((bits & 0x7FFFFFFFu) != 0) {
            float v95 = (v113 >= 1.0) ? static_cast<float>(v113) : 1.0f;
            v113 = v95;
        }
    }

    const double v91 = static_cast<double>(s->targetStk);
    const double v84 = static_cast<double>(effectiveStock);

    if (v91 * kMktHalf <= v84) {
        // OVERSUPPLY (stock >= half target).
        if (v91 * kMktThreeQuarter < v84) {
            // Heavily oversupplied (stock > 3/4 target): decay price, accumulate
            // sell qty, emit a (-1 -> building) restock cmd.
            double v31 = static_cast<double>(s->targetStk) * kMktDemandRate
                       * (static_cast<double>(elapsedMinutes) * kMktMinFrac);
            float v128 = static_cast<float>(v31);
            s->price = s->price - v128 * kMktOverDecay;
            // price = max(price, refValue * 0.25)
            double floorV = static_cast<double>(s->refValue) * kMktPriceFloorFrac;
            if (static_cast<double>(s->price) <= floorV)
                s->price = static_cast<float>(floorV);
            // price = max(price, 1.0)
            if (!(static_cast<double>(s->price) >= 1.0))
                s->price = 1.0f;
            // v130 = max(v128, 1.0); qty = trunc(v130)
            float v130 = (v128 >= 1.0f) ? v128 : 1.0f;
            i32 qty = ConvertX(static_cast<double>(v130));
            out.restock = MarketRestock::kSell;
            out.quantity = qty;
            if (queueRequest17) queueRequest17(-1, /*to building*/0, qty, s->itemType, marketIndex);
            // sellQty += round(sellQty + v130)
            s->sellQty = ConvertX(static_cast<double>(s->sellQty) + static_cast<double>(v130));
        }
        // else (half <= stock <= 3/4): no price change.
    } else {
        // UNDERSTOCK (stock < half target): accumulate buy qty, maybe emit a
        // (building -> -1) buy cmd, raise price.
        s->buyQty = ConvertX(static_cast<double>(s->buyQty) + v113);
        if (static_cast<double>(s->price) > static_cast<double>(s->refValue) * kMktHalf) {
            i32 qty = ConvertX(v113);
            out.restock = MarketRestock::kBuy;
            out.quantity = qty;
            if (queueRequest17) queueRequest17(/*from building*/0, -1, qty, s->itemType, marketIndex);
        }
        // price += targetStk * demandRate * (min*minFrac) * riseFactor
        s->price = static_cast<float>(
            static_cast<double>(s->targetStk) * kMktDemandRate
            * (static_cast<double>(elapsedMinutes) * kMktMinFrac)
            * static_cast<double>(kMktRiseFactor)
            + static_cast<double>(s->price));
    }

    // Price-band clamp (all branches): ceiling = refValue*1.5, low = refValue*0.4.
    {
        float ceil = s->refValue * kMktCeilFrac;     // v97
        if (static_cast<double>(s->price) <= static_cast<double>(ceil)) {
            float low = s->refValue * kMktResetLowFrac;   // v93
            if (static_cast<double>(s->price) < static_cast<double>(low)) {
                float ratio = s->price / low;
                // (3) RandomFloatScaled — reset-to-ceiling probability roll.
                if (util::RandomFloatScaled() > ratio)
                    s->price = s->refValue * kMktCeilFrac;
            }
        } else {
            float ratio = ceil / s->price;
            // (3) RandomFloatScaled — reset-low probability roll.
            if (util::RandomFloatScaled() > ratio)
                s->price = s->refValue * kMktResetLowFrac;
        }
    }

    // (5) Yield recompute gate: RandomModulo(0x64) > 10 OR hour == 12.
    if ((util::RandomModulo(0x64) & 0xFFFF) > 0xA || clockHour == 12) {
        s->yield = computeYield ? computeYield(slotIndex) : 0.0f;
        out.yieldRecomputed = true;
    }

    out.newPrice = s->price;
    return out;
}

// ===========================================================================
// gilde.exe 0x4e8bdc — VIBE_NpcAction_RunMarktSupervisorStep.
//   Disasm-cross-checked. The full original additionally sweeps up to 3
//   satellite markets x 62 slots x {sell,buy} object categories (the
//   byte_13CD994 / byte_13CD6A0 driven outer loop at 0x4e8e3e) with the same
//   price pipeline plus the Inventory_ComputeFreeCapacity capacity check; those
//   satellite sweeps are routed through the same per-slot recompute on the
//   provided stall list (the test exercises the central-market sweep, which is
//   the canonical pricing path). See the DEFERRED note in the report for the
//   satellite-specific capacity branch.
// ===========================================================================
HeRecord* NpcMarket_RunMarktSupervisorStep(HeRecord* h) {
    const NpcMarketHooks* H = g_hm;

    // Gate 1: market disabled -> free.
    if (!(H->marketEnabled && H->marketEnabled()))
        return reinterpret_cast<HeRecord*>(
            static_cast<std::intptr_t>(H->freeHandlerEntry ? H->freeHandlerEntry(h) : 0));

    const GameTime& clk = NpcClock();
    // Elapsed minutes since the supervisor's saved time (+96 in the original
    // record; we use the appointment slot's saved image via He_Scratch region).
    // The original: GameTime_DiffMinutes(h+96, &clock). We expose it via the
    // record's scratch GameTime at +96 (He_Scratch dwords 0..3 overlay it).
    GameTime* savedAt = reinterpret_cast<GameTime*>(reinterpret_cast<u8*>(h) + 96);
    int elapsed = GameTimeDiffMinutes(savedAt, &clk);

    const int hour = static_cast<int>(clk.hour);
    // Gate 2: outside the daily window (hour > 0x14) -> free.
    if (hour > 0x14)
        return reinterpret_cast<HeRecord*>(
            static_cast<std::intptr_t>(H->freeHandlerEntry ? H->freeHandlerEntry(h) : 0));

    if (elapsed == 0)
        return h;   // no time has passed -> nothing to do this tick

    // Stamp the saved time (+96) to now (the original copies the 14-byte clock).
    *savedAt = clk;

    const i32 state = He_State(h);
    if (state < -1) {
        if (state != -2)
            return h;                 // unknown negative state -> return it
        // state -2: (re)bind to the market person, go to state 0.
        He_State(h) = 0;
        return h;
    }
    if (state == -1) {                // LABEL_171: bind + state 0
        He_State(h) = 0;
        return h;
    }
    if (state != 0)
        return h;

    const u8 market = H->marketIndex ? H->marketIndex() : 0;

    // Treasury top-up: if currency < 5,120,000, enqueue a top-up to that level.
    if ((H->treasury ? H->treasury() : 0) < kMktTreasuryTopUp) {
        if (H->enqueueTopUp) H->enqueueTopUp(kMktTreasuryTopUp);
    }

    // Central-market sweep over the 62 stall slots (the original is fixed 62).
    const int n = H->slotCount ? H->slotCount() : 0;
    for (int i = 0; i < n; ++i) {
        if (!(H->slotIsActiveWorkable && H->slotIsActiveWorkable(i)))
            continue;
        MarketSlot s = H->slot ? H->slot(i) : MarketSlot{};
        if (!s.active || s.itemType == 0)
            continue;
        i32 stock = H->effectiveStock ? H->effectiveStock(i) : 0;
        MarketRecomputeSlot(&s, elapsed, stock, i, hour, market,
                            H->computeYield, H->queueRequest17);
        if (H->queueTransform64) H->queueTransform64(&s, market);
        if (H->commitSlot) H->commitSlot(i, &s);
    }

    // Re-arm: if still within the day window (hour < 0x16), re-stamp the
    // appointment to now and advance it; else free. NOTE: GameTime_Advance's 2nd
    // arg ("addDays" per the decompiler) is actually an HOUR delta in the binary
    // (0x5831b7 adds it to the hour-of-day, carrying to days only at /24), so
    // this advances the appointment by +1 hour — translated faithfully.
    if (hour < 0x16) {
        He_ApptTime(h) = clk;
        GameTimeAdvance(&He_ApptTime(h), 1, 0, 0);
        He_State(h) = 0;
        return h;
    }
    return reinterpret_cast<HeRecord*>(
        static_cast<std::intptr_t>(H->freeHandlerEntry ? H->freeHandlerEntry(h) : 0));
}

} // namespace guild::sim

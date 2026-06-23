// Golden-vector unit tests for the reconstructed combat raid-loot ("strength")
// leaves: gilde.exe 0x48d160 VIBE_Combat_ComputeAttackerStrength and
// 0x48d318 VIBE_Combat_ComputeDefenderStrength.
//
// These two functions were named *Strength by the IDA auto-namer but are the
// economy raid-loot transfer (cash/market-sell accumulation), per the verified
// note in combat_strength.h. The recovered .rdata doubles they read are:
//   dbl_61B9F4 = 0.001  (attacker base-cash roll scale)
//   dbl_61B9FC = 0.07   (attacker base-cash roll bias)
//   dbl_61BA04 = 0.01   (attacker per-ware quantity scale)
//   dbl_61BA0C = 0.5    (defender bulk-cash fraction when no commander present)
// This file PINS those constants and the documented integer-truncation math, the
// max(1, ...) quantity clamp, the per-ware market accumulation, the command-sink
// emission order (OnCashCredit / OnSellWare), and the family-credit side effect.
// Before this file the strength cluster had NO golden coverage.
#include "tests/framework/test.h"

#include "sim/combat_strength.h"

#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// A recording command sink that captures the lockstep emissions in order.
struct RecordingSink : ILootCommandSink {
    struct Sell { i32 seller, buyer; int qty; i32 ware; };
    std::vector<Sell> sells;
    std::vector<int>  cashCredits;
    void OnSellWare(i32 sellerId, i32 buyerId, int qty, i32 wareType) override {
        sells.push_back({sellerId, buyerId, qty, wareType});
    }
    void OnCashCredit(int value) override { cashCredits.push_back(value); }
};

} // namespace

// --- The recovered .rdata constants ----------------------------------------
TEST(CombatStrength, RecoveredConstants) {
    CHECK_EQ(kLootBaseCashScale, 0.001);  // dbl_61B9F4
    CHECK_EQ(kLootBaseCashBias,  0.07);   // dbl_61B9FC
    CHECK_EQ(kLootWareQtyScale,  0.01);   // dbl_61BA04
    CHECK_EQ(kLootDefenderFrac,  0.5);    // dbl_61BA0C
}

// --- Attacker: base-cash roll, no wares ------------------------------------
// cashTotal = (int)((RandInt(50)*0.001 + 0.07) * commanderCash).
// With state seeded so RandInt(50) is deterministic, pin the truncation.
TEST(CombatStrength, AttackerBaseCashOnly) {
    CutsceneRng rng;
    rng.state = 0;  // RandInt advances first: state=12345, (12345>>16)=0, 0%0x7FFF=0, %50=0
    RecordingSink sink;
    // baseRoll == 0 -> baseCash = (0*0.001 + 0.07) * 10000 = 700.0 -> 700.
    LootResult r = ComputeAttackerStrength(/*commanderCash*/ 10000, /*wares*/ {},
                                           /*targetSellerId*/ 5, /*attackerBuyerId*/ 9,
                                           /*hasFamilyRecord*/ false, /*familyMoneyBefore*/ 0,
                                           /*marketPrice*/ nullptr, rng, &sink);
    CHECK_EQ(r.cashTotal, 700);
    CHECK_EQ(r.familyCredited, -1);          // no family record
    // EnqueueCmd15(cashTotal) emitted exactly once with the base cash.
    CHECK_EQ((int)sink.cashCredits.size(), 1);
    CHECK_EQ(sink.cashCredits[0], 700);
    CHECK_EQ((int)sink.sells.size(), 0);     // no wares -> no sells
}

// --- Attacker: per-ware quantity clamp + market accumulation ---------------
// qty = max(1, (int)((RandInt(20)+80) * 0.01 * ware.quantity)).
TEST(CombatStrength, AttackerWareQuantityAndMarketAccum) {
    CutsceneRng rng;
    rng.state = 0;
    RecordingSink sink;
    // Two wares; market price is a fixed 3.0 per unit.
    std::vector<LootWare> wares = {
        {/*ware*/ 11, /*qty*/ 100, /*owner*/ 0},
        {/*ware*/ 22, /*qty*/ 0,   /*owner*/ 0},  // qty 0 -> clamped to 1
    };
    auto price = [](i32) -> double { return 3.0; };
    LootResult r = ComputeAttackerStrength(/*commanderCash*/ 10000, wares,
                                           /*targetSellerId*/ 5, /*attackerBuyerId*/ 9,
                                           /*hasFamilyRecord*/ true, /*familyMoneyBefore*/ 1000,
                                           price, rng, &sink);
    // base: state=0 -> RandInt(50)=0 -> 700.  Then per-ware:
    // ware0: RandInt(20) advances LCG; the value v depends on state, but qty>=1
    //   and the second ware's qty roll yields qty>=1 (0 quantity clamps to 1).
    // We pin the OBSERVABLE invariants the rule guarantees:
    CHECK_EQ((int)sink.sells.size(), 2);
    // sells carry (targetSellerId, attackerBuyerId, qty, wareTypeId) in order.
    CHECK_EQ(sink.sells[0].seller, 5);
    CHECK_EQ(sink.sells[0].buyer, 9);
    CHECK_EQ(sink.sells[0].ware, 11);
    CHECK(sink.sells[0].qty >= 1);
    CHECK_EQ(sink.sells[1].ware, 22);
    CHECK_EQ(sink.sells[1].qty, 1);          // qty 0 -> clamped to 1
    // cashTotal = 700 + 3*qty0 + 3*qty1 (each truncation folded into the int accum).
    int expected = 700 + 3 * sink.sells[0].qty + 3 * sink.sells[1].qty;
    CHECK_EQ(r.cashTotal, expected);
    // family credited = familyMoneyBefore + cashTotal.
    CHECK_EQ(r.familyCredited, 1000 + expected);
}

// --- Defender: rate-scaled quantity, commander present (no halving) --------
// qty = (ware.quantity*rate >= 1.0) ? (int)(ware.quantity*rate) : 1.
TEST(CombatStrength, DefenderWithCommanderNoHalving) {
    RecordingSink sink;
    std::vector<std::vector<LootWare>> stock = {
        { {/*ware*/ 7, /*qty*/ 10, /*owner*/ 42} },
    };
    auto price = [](i32) -> double { return 2.0; };
    // rate 0.5: 10*0.5 = 5.0 -> qty 5. cashTotal = 2*5 = 10. Commander present -> no 0.5.
    LootResult r = ComputeDefenderStrength(stock, /*rate*/ 0.5f,
                                           /*commanderSellerId*/ 3, /*hasCommander*/ true,
                                           /*hasFamilyRecord*/ true, /*familyMoneyBefore*/ 500,
                                           price, &sink);
    CHECK_EQ((int)sink.sells.size(), 1);
    CHECK_EQ(sink.sells[0].seller, 3);       // commander present -> seller = commanderSellerId
    CHECK_EQ(sink.sells[0].buyer, 42);       // ware.ownerId
    CHECK_EQ(sink.sells[0].qty, 5);
    CHECK_EQ(sink.sells[0].ware, 7);
    CHECK_EQ(r.cashTotal, 10);
    CHECK_EQ((int)sink.cashCredits.size(), 0);  // commander present -> no bulk cash cmd
    CHECK_EQ(r.familyCredited, 510);
}

// --- Defender: no commander -> halve haul + emit bulk cash, seller = -1 ----
TEST(CombatStrength, DefenderNoCommanderHalvesAndCredits) {
    RecordingSink sink;
    std::vector<std::vector<LootWare>> stock = {
        { {/*ware*/ 7, /*qty*/ 10, /*owner*/ 42} },
    };
    auto price = [](i32) -> double { return 2.0; };
    // rate 1.0: qty 10. cashTotal = 2*10 = 20 -> *0.5 = 10.
    LootResult r = ComputeDefenderStrength(stock, /*rate*/ 1.0f,
                                           /*commanderSellerId*/ 3, /*hasCommander*/ false,
                                           /*hasFamilyRecord*/ true, /*familyMoneyBefore*/ 100,
                                           price, &sink);
    CHECK_EQ((int)sink.sells.size(), 1);
    CHECK_EQ(sink.sells[0].seller, -1);      // no commander -> seller = -1
    CHECK_EQ(sink.sells[0].qty, 10);
    CHECK_EQ(r.cashTotal, 10);               // 20 * 0.5
    CHECK_EQ((int)sink.cashCredits.size(), 1);
    CHECK_EQ(sink.cashCredits[0], 10);
    CHECK_EQ(r.familyCredited, 110);
}

// --- Defender: quantity below 1.0 clamps to 1 ------------------------------
TEST(CombatStrength, DefenderSubUnitQuantityClampsToOne) {
    RecordingSink sink;
    std::vector<std::vector<LootWare>> stock = {
        { {/*ware*/ 7, /*qty*/ 1, /*owner*/ 42} },
    };
    auto price = [](i32) -> double { return 4.0; };
    // rate 0.1: 1*0.1 = 0.1 < 1.0 -> qty 1. cashTotal = 4*1 = 4. Commander present.
    LootResult r = ComputeDefenderStrength(stock, /*rate*/ 0.1f,
                                           /*commanderSellerId*/ 3, /*hasCommander*/ true,
                                           /*hasFamilyRecord*/ false, /*familyMoneyBefore*/ 0,
                                           price, &sink);
    CHECK_EQ(sink.sells[0].qty, 1);
    CHECK_EQ(r.cashTotal, 4);
    CHECK_EQ(r.familyCredited, -1);
}

// --- Defender: MULTI-slot -> per-slot bulk-cash cmd + per-slot family credit -
// Binary 0x48d318: the no-commander bulk-cash command (0x48d44e) and the family
// credit (0x48d496) are INSIDE the per-production-slot loop body, so with two
// non-empty stockpile slots they fire TWICE against the running cumulative haul.
// (Verified against live decompile: v18 is never reset between slots.)
TEST(CombatStrength, DefenderMultiSlotPerSlotEmission) {
    RecordingSink sink;
    std::vector<std::vector<LootWare>> stock = {
        { {/*ware*/ 7, /*qty*/ 10, /*owner*/ 42} },   // slot 0
        { {/*ware*/ 8, /*qty*/ 10, /*owner*/ 43} },   // slot 1
    };
    auto price = [](i32) -> double { return 2.0; };
    // rate 1.0 -> qty 10 each. No commander -> 0.5 halving per slot.
    //  slot0: cash 0 + 2*10 = 20 -> *0.5 = 10; emit cash 10; family 100+10=110.
    //  slot1: cash 10 + 2*10 = 30 -> *0.5 = 15; emit cash 15; family 110+15=125.
    LootResult r = ComputeDefenderStrength(stock, /*rate*/ 1.0f,
                                           /*commanderSellerId*/ 3, /*hasCommander*/ false,
                                           /*hasFamilyRecord*/ true, /*familyMoneyBefore*/ 100,
                                           price, &sink);
    CHECK_EQ((int)sink.sells.size(), 2);
    CHECK_EQ((int)sink.cashCredits.size(), 2);   // one bulk-cash cmd PER slot
    CHECK_EQ(sink.cashCredits[0], 10);
    CHECK_EQ(sink.cashCredits[1], 15);
    CHECK_EQ(r.cashTotal, 15);                    // final cumulative haul
    CHECK_EQ(r.familyCredited, 125);             // 100 + 10 + 15 (per-slot credits)
}

// --- Defender: empty stockpiles -> no halving, no family credit ------------
// The `!stockpiles.empty()` guards on the 0.5 fraction and the family credit.
TEST(CombatStrength, DefenderEmptyStockpilesNoSideEffects) {
    RecordingSink sink;
    std::vector<std::vector<LootWare>> empty;
    LootResult r = ComputeDefenderStrength(empty, /*rate*/ 1.0f,
                                           /*commanderSellerId*/ 3, /*hasCommander*/ false,
                                           /*hasFamilyRecord*/ true, /*familyMoneyBefore*/ 999,
                                           nullptr, &sink);
    CHECK_EQ(r.cashTotal, 0);
    CHECK_EQ((int)sink.cashCredits.size(), 0);   // empty -> no bulk cash cmd
    CHECK_EQ(r.familyCredited, -1);              // empty -> no family credit
}

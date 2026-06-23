// Golden-vector tests for the MeisterAi economy trade planners
// (gilde.exe 0x4614d0 / 0x46329c / 0x463c4c). Each case feeds a market snapshot +
// explicit RNG draws and checks the chosen good / quantity against the math
// translated 1:1 from the Hex-Rays decompile.
#include "tests/framework/test.h"

#include <vector>

#include "sim/meister_economy_recon_planners.h"

using namespace guild::sim;

// ---------------------------------------------------------------------------
// TradeGeneral kernels
// ---------------------------------------------------------------------------

TEST(MeisterEconReconGeneral, StoragePrePass_FlaggedOverThird) {
    // (bits&6)==0, (bits&8)!=0, slotCap/3 < stock -> plan = stock.
    CHECK_EQ(GeneralStoragePrePass(/*bits*/ 8, /*stock*/ 40, /*slotCap*/ 90), 40);
    // slotCap/3 == 30 >= stock 30 -> no plan.
    CHECK_EQ(GeneralStoragePrePass(8, 30, 90), 0);
    // not flagged (bit8 clear) -> no plan.
    CHECK_EQ(GeneralStoragePrePass(0, 40, 90), 0);
    // matched/reserved (bit 2) -> no plan.
    CHECK_EQ(GeneralStoragePrePass(8 | 2, 40, 90), 0);
}

TEST(MeisterEconReconGeneral, ProfitSell_RatioAndRoll) {
    // sell/buy = 80/100 = 0.8 < 1.0; roll*1.25 must be >= 0.8 -> roll >= 0.64.
    CHECK_EQ(GeneralProfitSell(0, /*stock*/ 25, 80.0f, 100.0f, /*roll*/ 0.7, 30), 25);
    // roll 0.5 -> 0.625 < 0.8 -> no sell.
    CHECK_EQ(GeneralProfitSell(0, 25, 80.0f, 100.0f, 0.5, 30), 0);
    // ratio == 1.0 (not < 1.0) -> no sell regardless of roll.
    CHECK_EQ(GeneralProfitSell(0, 25, 100.0f, 100.0f, 1.0, 30), 0);
    // bit8 set + slotCap/3 (10) >= stock(9) -> gated out.
    CHECK_EQ(GeneralProfitSell(8, 9, 50.0f, 100.0f, 1.0, 30), 0);
    // bit8 set + slotCap/3 (10) < stock(20) -> proceeds; 0.5<1 && 1.0*1.25>=0.5.
    CHECK_EQ(GeneralProfitSell(8, 20, 50.0f, 100.0f, 1.0, 30), 20);
    // matched -> never.
    CHECK_EQ(GeneralProfitSell(4, 20, 50.0f, 100.0f, 1.0, 30), 0);
    // zero buy price -> guarded.
    CHECK_EQ(GeneralProfitSell(0, 20, 50.0f, 0.0f, 1.0, 30), 0);
}

TEST(MeisterEconReconGeneral, EmergencySell_QtyAndProceeds) {
    int proceeds = 0;
    // price 200 -> 32000/200 = 160; stock 50 -> clamp to 50; proceeds += 160*200.
    CHECK_EQ(GeneralEmergencySell(0, /*stock*/ 50, /*price*/ 200, &proceeds), 50);
    CHECK_EQ(proceeds, 160 * 200);
    // already at target -> skip.
    int p2 = kEmergencyTarget;
    CHECK_EQ(GeneralEmergencySell(0, 50, 200, &p2), 0);
    // tiny price floors qty at 1 (32000/40000 == 0 -> 1).
    int p3 = 0;
    CHECK_EQ(GeneralEmergencySell(0, 50, 40000, &p3), 1);
    CHECK_EQ(p3, 40000);
    // matched -> never.
    int p4 = 0;
    CHECK_EQ(GeneralEmergencySell(2, 50, 200, &p4), 0);
}

TEST(MeisterEconReconGeneral, OverstockSell_Trim) {
    // 3*slotCap/4 = 3*40/4 = 30; stock 80 > 30 -> max(5, 80/4=20)=20.
    CHECK_EQ(GeneralOverstockSell(0, /*stock*/ 80, /*slotCap*/ 40), 20);
    // small stock over the line but stock/4 < 5 -> floor 5.
    CHECK_EQ(GeneralOverstockSell(0, /*stock*/ 7, /*slotCap*/ 4), 5); // 3*4/4=3<7
    // below the overstock line.
    CHECK_EQ(GeneralOverstockSell(0, /*stock*/ 30, /*slotCap*/ 40), 0);
    // qty floor clamps to stock when stock < 5.
    CHECK_EQ(GeneralOverstockSell(0, /*stock*/ 4, /*slotCap*/ 1), 4); // 3*1/4=0<4, qty=5->clamp 4
}

TEST(MeisterEconReconGeneral, RunTradeGeneral_OrderAndMutation) {
    GeneralPlayer player;
    player.oddHour = true;
    player.sellDoneFlag = false;
    player.funds = 100000;        // not cash-strapped
    player.heldCurrency = 100000;
    player.slotCap = 40;

    std::vector<GeneralWorkstation> st = {
        // good margin, will profit-sell whole stock 25.
        GeneralWorkstation{/*type*/ 7, /*bits*/ 0, /*stock*/ 25, 80.0f, 100.0f, 0},
        // overstock only (ratio == 1 so no profit-sell), stock 80 slotCap 40 -> 20.
        GeneralWorkstation{/*type*/ 9, /*bits*/ 0, /*stock*/ 80, 100.0f, 100.0f, 0},
    };
    std::vector<TradeDecision> out;
    int n = RunTradeGeneral(player, st, []() { return 0.9; }, out);

    CHECK_EQ(n, 2);
    CHECK_EQ(out[0].kind == TradeDecisionKind::ProfitSell, true);
    CHECK_EQ(out[0].typeId, (guild::u16)7);
    CHECK_EQ(out[0].qty, 25);
    CHECK_EQ(st[0].plannedQty, 25);
    CHECK_EQ(out[1].kind == TradeDecisionKind::OverstockSell, true);
    CHECK_EQ(out[1].typeId, (guild::u16)9);
    CHECK_EQ(out[1].qty, 20);
}

TEST(MeisterEconReconGeneral, RunTradeGeneral_EmergencyWhenStrapped) {
    GeneralPlayer player;
    player.oddHour = true;
    player.sellDoneFlag = false;
    player.funds = 1000;          // below 3200 -> strapped
    player.heldCurrency = 500;    // proceeds base = min(500,1000)=500
    player.slotCap = 1000;        // huge so overstock never triggers

    std::vector<GeneralWorkstation> st = {
        // ratio 1.0 -> no profit sell; price 200, stock 50 -> emergency sells 50.
        GeneralWorkstation{/*type*/ 3, /*bits*/ 0, /*stock*/ 50, 100.0f, 200.0f, 0},
    };
    std::vector<TradeDecision> out;
    int n = RunTradeGeneral(player, st, []() { return 0.0; }, out);
    CHECK_EQ(n, 1);
    CHECK_EQ(out[0].kind == TradeDecisionKind::EmergencySell, true);
    CHECK_EQ(out[0].qty, 50);
}

TEST(MeisterEconReconGeneral, RunTradeGeneral_OddHourLatch) {
    // sellDoneFlag set -> profit/emergency skipped; only overstock can run.
    GeneralPlayer player;
    player.oddHour = true;
    player.sellDoneFlag = true;
    player.slotCap = 40;
    std::vector<GeneralWorkstation> st = {
        GeneralWorkstation{7, 0, 25, 80.0f, 100.0f, 0}, // would profit-sell but latched
    };
    std::vector<TradeDecision> out;
    int n = RunTradeGeneral(player, st, []() { return 0.9; }, out);
    CHECK_EQ(n, 0); // stock 25, 3*40/4=30 >= 25 -> no overstock either
}

// ---------------------------------------------------------------------------
// Remote-purchase kernels
// ---------------------------------------------------------------------------

TEST(MeisterEconReconRemote, NineEighthsTrunc_SignedDiv8) {
    CHECK_EQ(NineEighthsTrunc(0), 0);
    CHECK_EQ(NineEighthsTrunc(8), 9);    // 72/8
    CHECK_EQ(NineEighthsTrunc(9), 10);   // 81/8 = 10.125 -> 10
    CHECK_EQ(NineEighthsTrunc(100), 112);// 900/8 = 112.5 -> 112
    CHECK_EQ(NineEighthsTrunc(-1), -1);  // -9/8 -> -1 (toward zero)
    CHECK_EQ(NineEighthsTrunc(-8), -9);  // -72/8
    CHECK_EQ(NineEighthsTrunc(-9), -10); // -81/8 = -10.125 -> -10
}

TEST(MeisterEconReconRemote, CartTripCount_Clamp) {
    // 0.006 * activeByTurn, clamped [5,16].
    CHECK_EQ(RemoteCartTripCount(100.0), 5);    // 0.6 -> trunc 0 -> floor 5
    CHECK_EQ(RemoteCartTripCount(1000.0), 6);   // 6.0 -> 6
    CHECK_EQ(RemoteCartTripCount(900.0), 5);    // 5.4 -> 5
    CHECK_EQ(RemoteCartTripCount(2000.0), 12);  // 12.0
    CHECK_EQ(RemoteCartTripCount(5000.0), 16);  // 30 -> clamp 16
}

TEST(MeisterEconReconRemote, RestockDelta_UpDownNone) {
    // target 100 -> 9/8 = 112; *1.03 = 115.36 -> trunc 115. count 50 < 100 -> +115.
    CHECK_EQ(RemoteRestockDelta(/*count*/ 50, /*target*/ 100), 115);
    // count between target and 2*target -> 0.
    CHECK_EQ(RemoteRestockDelta(150, 100), 0);
    CHECK_EQ(RemoteRestockDelta(100, 100), 0); // count==target not < target
    // count 300 > 200; delta = 300 - 112 = 188; *1.03 = 193.64 -> 193; negative.
    CHECK_EQ(RemoteRestockDelta(300, 100), -193);
}

TEST(MeisterEconReconRemote, PlanQty_ClampToCapacity) {
    CHECK_EQ(RemotePlanQty(/*cap*/ 30, /*planned*/ 50), 30);
    CHECK_EQ(RemotePlanQty(/*cap*/ 100, /*planned*/ 40), 40);
    CHECK_EQ(RemotePlanQty(/*cap*/ 40, /*planned*/ 40), 40);
}

TEST(MeisterEconReconRemote, RunRemotePurchase_SourcesAndBalance) {
    std::vector<RemoteSource> srcs = {
        RemoteSource{/*type*/ 11, /*planned*/ 50},  // clamp to cap 30
        RemoteSource{/*type*/ 12, /*planned*/ 10},  // stays 10
        RemoteSource{/*type*/ 13, /*planned*/ 0},   // dropped
    };
    std::vector<TradeDecision> out;
    // cap 30, count 50 < target 100 -> build up +115.
    int n = RunRemotePurchase(/*cap*/ 30, /*count*/ 50, /*target*/ 100,
                              /*active*/ 2000.0, srcs, out);
    CHECK_EQ(n, 3); // two sources + one restock-up
    CHECK_EQ(out[0].kind == TradeDecisionKind::RemotePlanSell, true);
    CHECK_EQ(out[0].typeId, (guild::u16)11);
    CHECK_EQ(out[0].qty, 30);
    CHECK_EQ(out[1].typeId, (guild::u16)12);
    CHECK_EQ(out[1].qty, 10);
    CHECK_EQ(out[2].kind == TradeDecisionKind::RemoteRestockUp, true);
    CHECK_EQ(out[2].qty, 115);
}

TEST(MeisterEconReconRemote, RunRemotePurchase_BuildDown) {
    std::vector<RemoteSource> srcs;
    std::vector<TradeDecision> out;
    int n = RunRemotePurchase(/*cap*/ 50, /*count*/ 300, /*target*/ 100,
                              /*active*/ 1000.0, srcs, out);
    CHECK_EQ(n, 1);
    CHECK_EQ(out[0].kind == TradeDecisionKind::RemoteRestockDown, true);
    CHECK_EQ(out[0].qty, -193);
}

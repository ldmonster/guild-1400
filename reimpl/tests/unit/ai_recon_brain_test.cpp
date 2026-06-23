// Golden-vector unit tests for the pure AI decision/scoring kernels reconstructed
// in src/sim/ai_recon_brain.h (gilde.exe Meister-AI / AI-method family).
//
// Vectors are derived directly from the Hex-Rays decompile arithmetic; each test
// references the originating gilde.exe address.

#include "tests/framework/test.h"
#include "sim/ai_recon_brain.h"

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// AiNeedTier — gilde.exe 0x469e1c / 0x46a4d8 cascade
//   <42 ->1  [42,84)->2  [84,126)->3  [126,168)->5  [168,210)->7  >=210 ->10
// ---------------------------------------------------------------------------
TEST(AiReconBrainNeedTier, ThresholdBoundaries) {
    CHECK_EQ(AiNeedTier(0),    1);
    CHECK_EQ(AiNeedTier(41),   1);   // 0x29
    CHECK_EQ(AiNeedTier(42),   2);   // 0x2A boundary
    CHECK_EQ(AiNeedTier(83),   2);
    CHECK_EQ(AiNeedTier(84),   3);   // 0x54
    CHECK_EQ(AiNeedTier(125),  3);
    CHECK_EQ(AiNeedTier(126),  5);   // 0x7E  (note: jumps 3 -> 5)
    CHECK_EQ(AiNeedTier(167),  5);
    CHECK_EQ(AiNeedTier(168),  7);   // 0xA8
    CHECK_EQ(AiNeedTier(209),  7);
    CHECK_EQ(AiNeedTier(210),  10);  // 0xD2
    CHECK_EQ(AiNeedTier(255),  10);
}

TEST(AiReconBrainNeedTier, SlotCostAndValidity) {
    // cost = (int)(tier + debugBonus). debugBonus is -1.0 when DebugCmd bit2 set.
    CHECK_EQ(AiNeedSlotCost(200, 0.0f),  7);
    CHECK_EQ(AiNeedSlotCost(200, -1.0f), 6);   // 7 + (-1) truncated
    CHECK_EQ(AiNeedSlotCost(10,  -1.0f), 0);   // tier 1 - 1 = 0
    // valid iff cost <= statBudget
    CHECK(AiNeedSlotValid(200, 0.0f, 7));
    CHECK(!AiNeedSlotValid(200, 0.0f, 6));
    CHECK(AiNeedSlotValid(200, -1.0f, 6));
}

TEST(AiReconBrainNeedTier, SlotRatioRateScalarSubstitution) {
    // ratio = need / rateScalar, with rateScalar 0 -> 252 (@0x469f86).
    CHECK(AiNeedSlotRatio(126, 0) == 126.0 / 252.0);
    CHECK(AiNeedSlotRatio(126, 63) == 126.0 / 63.0);
    CHECK(AiNeedSlotRatio(0, 100) == 0.0);
}

// ---------------------------------------------------------------------------
// BankmeisterNewRate — gilde.exe 0x459264 interest-rate state machine
// ---------------------------------------------------------------------------
TEST(AiReconBrainBankRate, FarFromTargetSingleStep) {
    // |cur-law|>3 : step one toward law.
    CHECK_EQ(BankmeisterNewRate(/*cur*/10, /*law*/4, /*loans*/0, 2, 2), 9);  // cur>law -> cur-1
    CHECK_EQ(BankmeisterNewRate(/*cur*/2,  /*law*/9, /*loans*/5, 1, 1), 3);  // cur<law -> cur+1
}

TEST(AiReconBrainBankRate, CloseFewLoansNudgeDown) {
    // loans==0 && (law-2)<cur : v = cur-1-randDown, clamp >=law-2, >=4.
    // cur=8 law=7 randDown=1 -> 8-1-1=6 ; law-2=5 ; clamp4 -> 6
    CHECK_EQ(BankmeisterNewRate(8, 7, 0, /*up*/0, /*down*/1), 6);
    // clamp to law-2: cur=8 law=9 randDown=2 -> 8-1-2=5 ; law-2=7 -> 7
    CHECK_EQ(BankmeisterNewRate(8, 9, 0, 0, 2), 7);
    // clamp to floor 4: cur=5 law=6 randDown=2 -> 5-1-2=2 ; law-2=4 -> 4 ; >=4 ->4
    CHECK_EQ(BankmeisterNewRate(5, 6, 0, 0, 2), 4);
}

TEST(AiReconBrainBankRate, CloseManyLoansRaise) {
    // not(loans==0&&law-2<cur); loans>3; cur<law : v = cur+1+randUp clamp <=law+3.
    // NOTE: |cur-law|<=3 is required to reach the close branch (else the far
    // single-step branch fires, per decompile 0x4592db). cur=7 law=9 loans=5
    // randUp=1 -> 7+1+1=9 ; law+3=12 -> 9
    CHECK_EQ(BankmeisterNewRate(7, 9, 5, /*up*/1, /*down*/0), 9);
    // clamp to law+3: cur=8 law=9 loans=5 randUp=2 -> 8+1+2=11 ; law+3=12 -> 11
    CHECK_EQ(BankmeisterNewRate(8, 9, 5, 2, 0), 11);
}

TEST(AiReconBrainBankRate, CloseStableHolds) {
    // loans<=3 and not the nudge-down branch: hold cur.
    // cur=8 law=8 loans=2 : nudge-down guard (law-2=6 < cur=8) true AND loans==0? no.
    //   -> loans==0 false, so fall through; loans<=3 -> hold.
    CHECK_EQ(BankmeisterNewRate(8, 8, 2, 1, 1), 8);
    // many loans but cur>=law: hold.
    CHECK_EQ(BankmeisterNewRate(10, 9, 5, 1, 1), 10);
}

// ---------------------------------------------------------------------------
// Bankmeister reserve rebalance — gilde.exe 0x459264 (3/20,5/20,6/20,4/20 bands)
// ---------------------------------------------------------------------------
TEST(AiReconBrainBankReserve, BuyBandLowStock) {
    // capital=200: 3*200/20=30, 5*200/20=50. count=10 < 30 -> BUY raw=50-10=40
    // amount=(int)(40*1.03)=41
    BankReserveOp op = BankmeisterReserveTier(/*count*/10, /*capital*/200);
    CHECK_EQ(static_cast<int>(op.kind), static_cast<int>(BankReserveOp::Buy));
    CHECK_EQ(op.rawDelta, 40);
    CHECK_EQ(op.amount, 41);
}

TEST(AiReconBrainBankReserve, SellBandHighStock) {
    // capital=200: 6*200/20=60, 4*200/20=40. count=100 > 60 -> SELL raw=100-40=60
    // amount=(int)(60*1.03)=61
    BankReserveOp op = BankmeisterReserveTier(/*count*/100, /*capital*/200);
    CHECK_EQ(static_cast<int>(op.kind), static_cast<int>(BankReserveOp::Sell));
    CHECK_EQ(op.rawDelta, 60);
    CHECK_EQ(op.amount, 61);
}

TEST(AiReconBrainBankReserve, MidBandNoOp) {
    // count=45 in [30,60] -> no op
    BankReserveOp op = BankmeisterReserveTier(45, 200);
    CHECK_EQ(static_cast<int>(op.kind), static_cast<int>(BankReserveOp::None));
}

TEST(AiReconBrainBankReserve, IntegerDivisionTruncation) {
    // capital=10: 3*10/20=1, 5*10/20=2, 6*10/20=3, 4*10/20=2 (all truncated)
    // count=0 < 1 -> BUY raw=2-0=2 amount=(int)(2*1.03)=2
    BankReserveOp op = BankmeisterReserveTier(0, 10);
    CHECK_EQ(static_cast<int>(op.kind), static_cast<int>(BankReserveOp::Buy));
    CHECK_EQ(op.rawDelta, 2);
    CHECK_EQ(op.amount, 2);
}

TEST(AiReconBrainBankReserve, FinalBalanceDeficitBuy) {
    // count < capital*1.5 and capital-netMoved>0 -> BUY (int)((cap-net)*0.8)
    // capital=100 count=100 (<150) netMoved=20 -> v19=80 amount=(int)(80*0.8)=64
    BankReserveOp op = BankmeisterFinalBalance(100, 100, 20, false);
    CHECK_EQ(static_cast<int>(op.kind), static_cast<int>(BankReserveOp::Buy));
    CHECK_EQ(op.rawDelta, 80);
    CHECK_EQ(op.amount, 64);
}

TEST(AiReconBrainBankReserve, FinalBalanceOversupplySell) {
    // count >= capital*1.5 path; type allows sell; 2*cap < count.
    // capital=100 count=300 net=0 -> not <150; !block; 200<300
    // amount=(int)(300 - 100*2.25)=(int)(300-225)=75
    BankReserveOp op = BankmeisterFinalBalance(300, 100, 0, false);
    CHECK_EQ(static_cast<int>(op.kind), static_cast<int>(BankReserveOp::Sell));
    CHECK_EQ(op.amount, 75);
}

TEST(AiReconBrainBankReserve, FinalBalanceBlockedSell) {
    // building type 6/7 blocks the sell branch -> None
    BankReserveOp op = BankmeisterFinalBalance(300, 100, 0, /*block*/true);
    CHECK_EQ(static_cast<int>(op.kind), static_cast<int>(BankReserveOp::None));
}

// ---------------------------------------------------------------------------
// GroupComposition — gilde.exe 0x4686a4
// ---------------------------------------------------------------------------
TEST(AiReconBrainGroup, ValidationCascade) {
    CHECK_EQ(static_cast<int>(GroupCompositionValidate(false, true, 0, 0, 1)),
             static_cast<int>(GroupValidate::NullStruct));
    CHECK_EQ(static_cast<int>(GroupCompositionValidate(true, false, 0, 0, 1)),
             static_cast<int>(GroupValidate::NullLeader));
    CHECK_EQ(static_cast<int>(GroupCompositionValidate(true, true, 6, 0, 1)),
             static_cast<int>(GroupValidate::WantMaleBad));
    CHECK_EQ(static_cast<int>(GroupCompositionValidate(true, true, 0, 6, 1)),
             static_cast<int>(GroupValidate::WantFemBad));
    CHECK_EQ(static_cast<int>(GroupCompositionValidate(true, true, 0, 0, 0)),
             static_cast<int>(GroupValidate::SizeBad));
    CHECK_EQ(static_cast<int>(GroupCompositionValidate(true, true, 0, 0, 5)),
             static_cast<int>(GroupValidate::SizeBad));   // >4
    CHECK_EQ(static_cast<int>(GroupCompositionValidate(true, true, 5, 5, 4)),
             static_cast<int>(GroupValidate::Ok));
}

TEST(AiReconBrainGroup, TargetArithmetic) {
    // v10 = (5-wantB) - floor(rank/4) + rank + (5-wantA) - rank/3
    // wantA=2 wantB=3 rank=12: (5-3)=2 ; floor(12/4)=3 ; +12 ; (5-2)=3 ; -(12/3=4)
    //   = 2 - 3 + 12 + 3 - 4 = 10
    CHECK_EQ(GroupCompositionTarget(2, 3, 12), 10);
    // rank=0: 2 - 0 + 0 + 3 - 0 = 5
    CHECK_EQ(GroupCompositionTarget(2, 3, 0), 5);
    // negative rank floor-divide check: rank=-5 -> floor(-5/4)=-2 (arith shift),
    //   -5/3 = -1 (C truncation). wantA=0 wantB=0:
    //   (5) - (-2) + (-5) + (5) - (-1) = 5+2-5+5+1 = 8
    CHECK_EQ(GroupCompositionTarget(0, 0, -5), 8);
}

TEST(AiReconBrainGroup, MemberAdjustClamp) {
    // diff<-1 -> -2 ; diff>=2 -> 2 ; else diff.
    CHECK_EQ(static_cast<int>(GroupMemberAdjust(/*target*/0, /*rank*/5)), -2); // diff=-5
    CHECK_EQ(static_cast<int>(GroupMemberAdjust(0, 1)), -1);  // diff=-1 (not < -1)
    CHECK_EQ(static_cast<int>(GroupMemberAdjust(5, 5)), 0);   // diff=0
    CHECK_EQ(static_cast<int>(GroupMemberAdjust(5, 4)), 1);   // diff=1
    CHECK_EQ(static_cast<int>(GroupMemberAdjust(10, 5)), 2);  // diff=5 -> clamp 2
}

TEST(AiReconBrainGroup, ScoreBuckets) {
    // base/span/negate per adjustment code; randDraw substitutes RandomModulo(span).
    CHECK_EQ(GroupMemberScore(static_cast<i8>(-2) /*0xFE*/, 0),  -16); // -(0+16)
    CHECK_EQ(GroupMemberScore(static_cast<i8>(-2),          15), -31); // -(15+16)
    CHECK_EQ(GroupMemberScore(static_cast<i8>(-1) /*0xFF*/, 7),  -15); // -(7+8)
    CHECK_EQ(GroupMemberScore(0, 0),  8);   //  0+8
    CHECK_EQ(GroupMemberScore(0, 7),  15);  //  7+8
    CHECK_EQ(GroupMemberScore(1, 0),  16);  //  0+16
    CHECK_EQ(GroupMemberScore(2, 0),  24);  //  0+24
    CHECK_EQ(GroupMemberScore(2, 15), 39);  // 15+24
    // unknown code -> 0
    CHECK_EQ(GroupMemberScore(static_cast<i8>(3), 5), 0);
}

TEST(AiReconBrainGroup, CompositionValue) {
    // v19 = (unsigned)(extra - wantA + 5 - wantB) * 0.001 * wealth * size, trunc.
    // extra=10 wantA=2 wantB=3 wealth=100000 size=4:
    //   n = 10-2+5-3 = 10 ; 10*0.001*100000*4 = 4000
    CHECK_EQ(GroupCompositionValue(10, 2, 3, 100000, 4), 4000);
    // small case rounding-toward-zero: n=1 wealth=1500 size=1 -> 1*0.001*1500 = 1.5 -> 1
    CHECK_EQ(GroupCompositionValue(1, 0, 5, 1500, 1), 1);
}

// Golden-vector unit tests for the AI action/needs/target numeric cores
// reconstructed in src/sim/aiaction_recon.{h,cpp} from gilde.exe.
//
// Every expected value here is derived directly from the Hex-Rays decompile of
// the corresponding function (addresses in the provenance comments) and the
// .rdata float constants recovered via get_bytes:
//   flt_61A690=3.0 flt_61A694=0.4 flt_61A698=0.5 flt_61A69C=0.25
//   flt_61A6B8=0.015 flt_61AC28=33.0 flt_61AC2C=40.0
//   flt_61AB88=0.5 flt_61AB8C=0.75 dbl_61AE48=33.0
#include "sim/aiaction_recon.h"
#include "tests/framework/test.h"

#include <cmath>

using namespace guild;
using namespace guild::sim;

namespace {
bool feq(double a, double b) { return std::fabs(a - b) < 1e-5; }
}  // namespace

// ---- EvalGuildhallTarget @0x475700 -----------------------------------------
TEST(AiActionReconGuildhall, GateThresholdAndScale) {
    // threshold = rand16 + 50. rand16=10 -> 60.
    GuildhallGate g = EvalGuildhallGate(/*relTop=*/60, /*rand16=*/10);
    CHECK(g.loyalBranch);                 // 60 >= 60
    CHECK(feq(g.ownScale, 1.0));

    g = EvalGuildhallGate(/*relTop=*/59, /*rand16=*/10);
    CHECK(!g.loyalBranch);                // 59 < 60 -> own path
    CHECK(feq(g.ownScale, 3.0));          // flt_61A690

    // rand16=0 -> threshold 50; relTop=50 loyal, 49 not.
    CHECK(EvalGuildhallGate(50, 0).loyalBranch);
    CHECK(!EvalGuildhallGate(49, 0).loyalBranch);
    // rand16=15 -> threshold 65 (max).
    CHECK(!EvalGuildhallGate(64, 15).loyalBranch);
    CHECK(EvalGuildhallGate(65, 15).loyalBranch);
}

// ---- PlanGuildhallUpgrade @0x4757e8 ----------------------------------------
TEST(AiActionReconUpgrade, TierWeights) {
    CHECK_EQ(GuildhallUpgradeTierWeight(0), 50);
    CHECK_EQ(GuildhallUpgradeTierWeight(32), 50);
    CHECK_EQ(GuildhallUpgradeTierWeight(33), 30);
    CHECK_EQ(GuildhallUpgradeTierWeight(65), 30);
    CHECK_EQ(GuildhallUpgradeTierWeight(66), 10);
    // 0x4758ce mov ah,[esi+5Ch]; 0x4758d1 cmp ah,21h jge (SIGNED 8-bit).
    // 255 as i8 == -1 < 33 -> takes the "< 33" path -> 50 (NOT 10).
    CHECK_EQ(GuildhallUpgradeTierWeight(static_cast<i8>(255)), 50);
}

TEST(AiActionReconUpgrade, EnabledGate) {
    // enabled iff relTop < rand16+50 AND (flag90 & 4)==0
    CHECK(GuildhallUpgradeEnabled(/*rel=*/49, /*r16=*/0, /*flag=*/0));   // 49<50
    CHECK(!GuildhallUpgradeEnabled(50, 0, 0));                           // 50!<50
    CHECK(!GuildhallUpgradeEnabled(49, 0, 0x04));                        // flag set
    CHECK(GuildhallUpgradeEnabled(49, 0, 0x03));                         // other bits ok
}

TEST(AiActionReconUpgrade, CostMath) {
    // worth*0.4: 1000 -> 400
    CHECK_EQ(UpgradeCostFromBuildWorth(1000), 400);
    CHECK_EQ(UpgradeCostFromBuildWorth(2503), 1001);   // 2503*0.4 = 1001.2 -> 1001
    // (cash-80000)*0.5
    CHECK_EQ(UpgradeCostCashScaleA(100000), 10000);    // 20000*0.5
    CHECK_EQ(UpgradeCostCashScaleA(80000), 0);
    // (cash-80000)*0.25
    CHECK_EQ(UpgradeCostCashScaleB(100000), 5000);     // 20000*0.25
    CHECK_EQ(UpgradeCostCashScaleB(80004), 1);         // 4*0.25 = 1
}

// ---- EvalNeedFulfillTarget @0x4761a0 ---------------------------------------
TEST(AiActionReconNeed, RatioFloorsCapAtOne) {
    // cap > 1 -> direct divide
    CHECK(feq(NeedRatio(/*stat=*/50, /*cap=*/100), 0.5));
    CHECK(feq(NeedRatio(30, 4), 7.5));
    // cap <= 1 -> floored to 1.0
    CHECK(feq(NeedRatio(7, 1), 7.0));
    CHECK(feq(NeedRatio(7, 0), 7.0));   // cap 0 floored to 1
}

TEST(AiActionReconNeed, PickSlotMinMaxModes) {
    // LITERAL semantics: i = (s0 OP s1) as 0/1, then fold to 2 if s[i] OP s2.
    // max-mode (rand2==0), OP is >=:
    //   (1,5,3): i=(1>=5)=0; s[0]=1; 1>=3? no -> 0.
    CHECK_EQ(NeedPickSlot(1.0f, 5.0f, 3.0f, 0), 0);
    //   (1,2,9): i=(1>=2)=0; s[0]=1; 1>=9? no -> 0.
    CHECK_EQ(NeedPickSlot(1.0f, 2.0f, 9.0f, 0), 0);
    //   (5,1,3): i=(5>=1)=1; s[1]=1; 1>=3? no -> 1.
    CHECK_EQ(NeedPickSlot(5.0f, 1.0f, 3.0f, 0), 1);
    // min-mode (rand2!=0), OP is <=:
    //   (5,1,3): i=(5<=1)=0; s[0]=5; 5<=3? no -> 0.
    CHECK_EQ(NeedPickSlot(5.0f, 1.0f, 3.0f, 1), 0);
    //   (5,2,0): i=(5<=2)=0; s[0]=5; 5<=0? no -> 0.
    CHECK_EQ(NeedPickSlot(5.0f, 2.0f, 0.0f, 1), 0);
    //   (5,1,9): i=(5<=1)=0; s[0]=5; 5<=9? yes -> 2.
    CHECK_EQ(NeedPickSlot(5.0f, 1.0f, 9.0f, 1), 2);
    //   (1,5,9): i=(1<=5)=1; s[1]=5; 5<=9? yes -> 2.
    CHECK_EQ(NeedPickSlot(1.0f, 5.0f, 9.0f, 1), 2);
}

TEST(AiActionReconNeed, MaxModeS2TieGoesToTwo) {
    // 4,4,1: i=(4>=4)->1; s[1]=4 >= s2=1 -> i=2.
    CHECK_EQ(NeedPickSlot(4.0f, 4.0f, 1.0f, 0), 2);
}

TEST(AiActionReconNeed, FulfillCostTimesTen) {
    CHECK_EQ(NeedFulfillCost(0), 0);
    CHECK_EQ(NeedFulfillCost(37), 370);
}

// ---- PlanPersonInteraction @0x475f48 ---------------------------------------
TEST(AiActionReconInteraction, CostMath) {
    // wealth*0.015f (the 32-bit float 0.015 ~= 0.0149999996) truncated.
    // 10000*0.0149999996 = 149.99999 -> 149 (faithful float truncation).
    CHECK_EQ(PersonInteractionUnitCost(10000), 149);
    CHECK_EQ(PersonInteractionUnitCost(199), 2);       // 199*0.015 = 2.985 -> 2
    CHECK_EQ(PersonInteractionTotalCost(149), 894);    // 6 * 149
}

TEST(AiActionReconInteraction, FamilyNeedScan) {
    u8 occupied[13] = {0};
    // start at 3, slot 3 free -> 3.
    CHECK_EQ(PersonInteractionFamilyNeed(3, occupied), 3);
    // start at 0 -> skip to 1.
    CHECK_EQ(PersonInteractionFamilyNeed(0, occupied), 1);
    // occupy 5,6,7; start at 5 -> 8.
    occupied[5] = occupied[6] = occupied[7] = 1;
    CHECK_EQ(PersonInteractionFamilyNeed(5, occupied), 8);
    // all occupied except 0 (which is always skipped) -> -1.
    u8 full[13];
    for (int i = 0; i < 13; ++i) full[i] = 1;
    full[0] = 0;
    CHECK_EQ(PersonInteractionFamilyNeed(1, full), -1);
}

// ---- EvalConversationTarget @0x47b468 --------------------------------------
TEST(AiActionReconConversation, PrimaryGate) {
    CHECK(ConversationPrimaryAccept(32.9f, 1));    // <33 and state 1
    CHECK(!ConversationPrimaryAccept(33.0f, 1));   // not <33
    CHECK(!ConversationPrimaryAccept(10.0f, 2));   // state != 1
}

TEST(AiActionReconConversation, SecondaryGate) {
    // (rand32 + 40) > favor && state==1
    CHECK(ConversationSecondaryAccept(/*favor=*/39.0f, /*rand32=*/0, 1)); // 40>39
    CHECK(!ConversationSecondaryAccept(40.0f, 0, 1));                     // 40>40 false
    CHECK(ConversationSecondaryAccept(70.0f, 31, 1));                     // 71>70
    CHECK(!ConversationSecondaryAccept(50.0f, 31, 2));                    // state !=1
}

// ---- EvalSleepSpot @0x47b7d4 -----------------------------------------------
TEST(AiActionReconSleep, WindowCheck) {
    // valid: pred && value!=current && low<=value<=high
    CHECK(SleepSpotValid(true, /*val=*/5, /*cur=*/9, /*low=*/0, /*high=*/10));
    CHECK(!SleepSpotValid(false, 5, 9, 0, 10));   // predicate false
    CHECK(!SleepSpotValid(true, 9, 9, 0, 10));    // value == current
    CHECK(!SleepSpotValid(true, -1, 9, 0, 10));   // below low
    CHECK(!SleepSpotValid(true, 11, 9, 0, 10));   // above high
    CHECK(SleepSpotValid(true, 0, 9, 0, 10));     // boundary low ok
    CHECK(SleepSpotValid(true, 10, 9, 0, 10));    // boundary high ok
}

// ---- DispatchTargetSearch @0x47c430 ----------------------------------------
TEST(AiActionReconDispatch, PickSet) {
    CHECK_EQ(DispatchPickSet(0, 0, 0), -1);   // neither
    CHECK_EQ(DispatchPickSet(3, 0, 0), 0);    // A only
    CHECK_EQ(DispatchPickSet(0, 4, 1), 1);    // B only
    CHECK_EQ(DispatchPickSet(2, 2, 0), 0);    // both, rand2==0 -> A
    CHECK_EQ(DispatchPickSet(2, 2, 1), 1);    // both, rand2!=0 -> B
}

TEST(AiActionReconDispatch, PickIndex) {
    CHECK_EQ(DispatchPickIndex(0, 0), -1);
    CHECK_EQ(DispatchPickIndex(5, 3), 3);
    CHECK_EQ(DispatchPickIndex(5, 0), 0);
}

// ---- AiNeeds_EvaluateActions @0x47852c (tail) ------------------------------
TEST(AiActionReconSplit, SurvivorsThresholdWalk) {
    // table thresholds {-1,1,3,5}; survivors = entries strictly below n.
    // n=2: walk from v45=3: 2>5? no -> dec; 2>3? no -> dec; 2>1? yes break.
    //   survivors decremented twice from 4 -> 2.
    CHECK_EQ(ActionSplitSurvivors(2, 4), 2);
    // n=10: 10>5 yes immediately -> survivors 4.
    CHECK_EQ(ActionSplitSurvivors(10, 4), 4);
    // n=4: 4>5 no; 4>3 yes -> survivors 3.
    CHECK_EQ(ActionSplitSurvivors(4, 4), 3);
    // n=0: never > any positive; clamps to 1.
    CHECK_EQ(ActionSplitSurvivors(0, 4), 1);
}

TEST(AiActionReconSplit, SingleSplitWhenSmall) {
    ActionSplit s = SelectActionSplit(/*n=*/3, 0, 0, 0);
    CHECK_EQ(s.divisor, 1);
    CHECK_EQ(s.groupIndex, 0);
    CHECK_EQ(s.jitter, 0);
}

TEST(AiActionReconSplit, DivisorAndJitter) {
    // n=10, survivors=4, randDiv=2 -> divisors[2]=5.
    // groupIndex = 3 * randGroup. n % 5 == 0 -> jitter applies.
    ActionSplit s = SelectActionSplit(/*n=*/10, /*randGroup=*/2,
                                      /*randDiv=*/2, /*randJitter=*/1);
    CHECK_EQ(s.divisor, 5);
    CHECK_EQ(s.groupIndex, 6);       // 3*2
    CHECK_EQ(s.jitter, 1);           // rand2==1 -> +1
    s = SelectActionSplit(10, 2, 2, 0);
    CHECK_EQ(s.jitter, -1);          // rand2==0 -> -1
    // n=10, divisors[1]=3 -> 10 % 3 != 0 -> no jitter.
    s = SelectActionSplit(10, 1, 1, 1);
    CHECK_EQ(s.divisor, 3);
    CHECK_EQ(s.jitter, 0);
    // divisor 1 never jitters (divisors[0]=1).
    s = SelectActionSplit(8, 0, 0, 1);
    CHECK_EQ(s.divisor, 1);
    CHECK_EQ(s.jitter, 0);
}

// ---- FindNearestPerson @0x479dd8 -------------------------------------------
TEST(AiActionReconNearest, NoisyDistanceMetric) {
    // noise = rfs*0.5 + 0.75. rfs=0 -> 0.75; rfs=1 -> 1.25.
    // dx=3,dy=4,dz=0 -> mag 5. dist = 5 * noise.
    CHECK(feq(NoisyDistance(3, 4, 0, 0.0f), 5.0 * 0.75));
    CHECK(feq(NoisyDistance(3, 4, 0, 1.0f), 5.0 * 1.25));
    CHECK(feq(NoisyDistance(3, 4, 0, 0.5f), 5.0 * 1.0));  // noise=1.0
    // 3D: dx=2,dy=3,dz=6 -> mag 7.
    CHECK(feq(NoisyDistance(2, 3, 6, 0.0f), 7.0 * 0.75));
}

// ---- TryRangedAttack @0x47d364 ---------------------------------------------
TEST(AiActionReconRanged, DepthGate) {
    CHECK(!RangedAttackDepthOk(2));
    CHECK(RangedAttackDepthOk(3));
    CHECK(RangedAttackDepthOk(100));
}

TEST(AiActionReconRanged, AimRadius) {
    // rand16 + 33.0
    CHECK(feq(RangedAttackAimRadius(0), 33.0));
    CHECK(feq(RangedAttackAimRadius(15), 48.0));
}

TEST(AiActionReconRanged, HitAccept) {
    // curve + rand >= 1.0
    CHECK(RangedAttackHitAccept(0.6, 0.4));    // 1.0
    CHECK(RangedAttackHitAccept(0.9, 0.2));    // 1.1
    CHECK(!RangedAttackHitAccept(0.3, 0.3));   // 0.6
    CHECK(!RangedAttackHitAccept(0.0, 0.99));  // 0.99
}

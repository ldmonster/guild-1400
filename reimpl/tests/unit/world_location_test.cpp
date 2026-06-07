// Unit tests for the world/location rule-cores (church / thief / tavern /
// residence). Golden vectors computed with python3 against the exact recovered
// FP constants; eligibility gates and relation/reputation deltas exercised.
#include "world/location_church.h"
#include "world/location_thief.h"
#include "world/location_tavern.h"
#include "world/location_residence.h"

#include "tests/framework/test.h"

using namespace guild;
using namespace guild::world;

// ---------------------------------------------------------------------------
// Church donation (gilde.exe 0x521674).
// ---------------------------------------------------------------------------
TEST(WorldLocChurch, DonationCostAndReputation) {
    // player=500000, employer=300000, paid=4000  (golden: sug=7999 rep=1 spread=0.2)
    auto r = ChurchComputeDonation(500000, 300000, 4000);
    CHECK_EQ(r.wealthPlayer, 500000);
    CHECK_EQ(r.wealthEmployer, 300000);
    CHECK_EQ(r.wealthCombined, 800000);
    CHECK_EQ(r.suggestedCost, 7999);
    CHECK_EQ(r.reputDelta, 1);
    CHECK(r.reputSpread > 0.199f && r.reputSpread < 0.201f);
}

TEST(WorldLocChurch, DonationWealthGuardClampsToOne) {
    // both wealth <= 1 -> clamp to 1 each, combined 2, sug 0 (golden).
    auto r = ChurchComputeDonation(0, 1, 100);
    CHECK_EQ(r.wealthPlayer, 1);
    CHECK_EQ(r.wealthEmployer, 1);
    CHECK_EQ(r.wealthCombined, 2);
    CHECK_EQ(r.suggestedCost, 0);
    // 255*100/2 = 12750
    CHECK_EQ(r.reputDelta, 12750);
}

// ---------------------------------------------------------------------------
// Church indulgence (gilde.exe 0x521bac).
// ---------------------------------------------------------------------------
TEST(WorldLocChurch, IndulgenceNotOfferedWithoutCrime) {
    auto r = ChurchComputeIndulgence(/*hasCrime*/false, 1000000, false, 50);
    CHECK(!r.offered);
    CHECK_EQ(r.cost, 0);
}

TEST(WorldLocChurch, IndulgenceFavorabilityFactorAndFloor) {
    // wealth=1e6, npc!=player, fav=0 -> factor=(200-0)*0.01=2.0, raw=9999? no:
    // base=1e6*0.005=4999.99..; raw=int(4999.99*2.0)=9999; t=999.9 -> floor 3200.
    auto r = ChurchComputeIndulgence(true, 1000000, /*npcIsPlayer*/false, 0);
    CHECK(r.offered);
    CHECK(r.factor > 1.99f && r.factor < 2.01f);
    CHECK_EQ(r.rawCost, 9999);
    CHECK_EQ(r.cost, 3200); // floored at kIndulgenceMinClamp
}

TEST(WorldLocChurch, IndulgenceSelfFixedFactor) {
    // npc==player -> factor fixed 1.5; wealth=1e6 -> raw=7499 -> floor 3200.
    auto r = ChurchComputeIndulgence(true, 1000000, /*npcIsPlayer*/true, 999);
    CHECK(r.factor > 1.49f && r.factor < 1.51f);
    CHECK_EQ(r.rawCost, 7499);
    CHECK_EQ(r.cost, 3200);
}

TEST(WorldLocChurch, IndulgenceCeilingClamp) {
    // wealth=1e9, fav=0 -> factor=2.0, raw=9999999, t=999999.9 >= 320000 -> max.
    auto r = ChurchComputeIndulgence(true, 1000000000, false, 0);
    CHECK_EQ(r.rawCost, 9999999);
    CHECK_EQ(r.cost, 320000);
}

// ---------------------------------------------------------------------------
// Church confession (gilde.exe 0x522ae8).
// ---------------------------------------------------------------------------
TEST(WorldLocChurch, ConfessionEligibilityAndReput) {
    auto unemployed = ChurchComputeConfession(/*emp*/0xFFFF, 3, 100);
    CHECK(!unemployed.eligible);
    CHECK(!unemployed.canConfess);

    auto noCrime = ChurchComputeConfession(/*emp*/5, 0, 100);
    CHECK(noCrime.eligible);
    CHECK(!noCrime.canConfess);
    CHECK_EQ(noCrime.reputGain, 0);

    auto confess = ChurchComputeConfession(/*emp*/5, 2, 100);
    CHECK(confess.eligible);
    CHECK(confess.canConfess);
    CHECK_EQ(confess.activeCrimes, 2);
    CHECK_EQ(confess.reputGain, 50); // paid/2
}

// ---------------------------------------------------------------------------
// Thief burglary (gilde.exe 0x524074 / 0x5242d4).
// ---------------------------------------------------------------------------
TEST(WorldLocThief, BurglaryGatesAndEmit) {
    std::vector<u8> markers = {1, 0, 1, 1};
    auto ok = ThiefComputeBurglary(true, true, markers);
    CHECK(ok.offered);
    CHECK_EQ(ok.eligibleThieves, 3);
    CHECK(ok.emit.cmd == ThiefCommand::QueueBurglary);
    CHECK_EQ(ok.emit.arg, 3);

    auto unreachable = ThiefComputeBurglary(false, true, markers);
    CHECK(!unreachable.offered);
    CHECK(unreachable.emit.cmd == ThiefCommand::None);

    auto secure = ThiefComputeBurglary(true, false, markers);
    CHECK(!secure.offered);

    auto noThieves = ThiefComputeBurglary(true, true, {0, 0});
    CHECK(noThieves.offered);
    CHECK_EQ(noThieves.eligibleThieves, 0);
    CHECK(noThieves.emit.cmd == ThiefCommand::None);
}

TEST(WorldLocThief, BurglaryStartGuard) {
    CHECK(ThiefBurglaryStartAllowed(false));
    CHECK(!ThiefBurglaryStartAllowed(true));
}

// ---------------------------------------------------------------------------
// Thief ransom (gilde.exe 0x5259f8).
// ---------------------------------------------------------------------------
TEST(WorldLocThief, RansomCutByState) {
    CHECK(ThiefRansomCut(1) > 0.019f && ThiefRansomCut(1) < 0.021f);
    CHECK(ThiefRansomCut(2) > 0.039f && ThiefRansomCut(2) < 0.041f);
    CHECK(ThiefRansomCut(3) > 0.059f && ThiefRansomCut(3) < 0.061f);
    CHECK(ThiefRansomCut(5) > 0.099f && ThiefRansomCut(5) < 0.101f);
}

TEST(WorldLocThief, RansomPriceWealthCapped) {
    // state2, wealth=500000, rngRoll=1.6e6: wealth<=roll -> base=500000, cut .04
    auto r = ThiefComputeRansom(true, true, false, 2, 500000, 1600000.0, true);
    CHECK(r.offered);
    CHECK_EQ(r.baseValue, 500000);
    CHECK_EQ(r.ransom, 19999); // golden
    CHECK(r.emit.cmd == ThiefCommand::PayRansom);
    CHECK_EQ(r.emit.arg, 19999);
}

TEST(WorldLocThief, RansomPriceRollCapped) {
    // state3, wealth=5e6, rngRoll=1.4e6: wealth>roll -> base=1.4e6, cut .06
    auto r = ThiefComputeRansom(true, true, false, 3, 5000000, 1400000.0, false);
    CHECK_EQ(r.baseValue, 1400000);
    CHECK_EQ(r.ransom, 83999); // golden
    CHECK(r.emit.cmd == ThiefCommand::None); // declined -> no emit
}

TEST(WorldLocThief, RansomNotOfferedGates) {
    CHECK(!ThiefComputeRansom(false, true, false, 2, 1, 1.0, true).offered);  // no hostage
    CHECK(!ThiefComputeRansom(true, false, false, 2, 1, 1.0, true).offered);  // no kidnap cmd
    CHECK(!ThiefComputeRansom(true, true, true, 2, 1, 1.0, true).offered);    // pending
}

// ---------------------------------------------------------------------------
// Thief kidnap + spy (gilde.exe 0x5258bc / 0x524740).
// ---------------------------------------------------------------------------
TEST(WorldLocThief, KidnapEligibilityChain) {
    auto ok = ThiefComputeKidnap(false, false, false);
    CHECK(ok.offered);
    CHECK(ok.emit.cmd == ThiefCommand::StartKidnap);

    CHECK(!ThiefComputeKidnap(true, false, false).offered);  // active char busy
    CHECK(!ThiefComputeKidnap(false, true, false).offered);  // already captured
    CHECK(!ThiefComputeKidnap(false, false, true).offered);  // kidnap in progress
}

TEST(WorldLocThief, SpyBuildingTypeSelector) {
    auto prod = ThiefComputeSpyBuilding(false, 19);
    CHECK(prod.offered);
    CHECK(prod.isProduction);
    CHECK_EQ(prod.promptValue, 589840);

    auto other = ThiefComputeSpyBuilding(false, 8);
    CHECK(!other.isProduction);
    CHECK_EQ(other.promptValue, 134038452);

    CHECK(!ThiefComputeSpyBuilding(true, 19).offered);
}

// ---------------------------------------------------------------------------
// Tavern card game (gilde.exe 0x516b78).
// ---------------------------------------------------------------------------
TEST(WorldLocTavern, CardGameOfferedAndDecay) {
    // rand=0.3 < willingness=0.8, hour=18 > 11, wealth=20000 >= 10000 -> offered.
    auto r = TavernComputeCardGame(0.3f, 0.8f, 18, 20000);
    CHECK(r.offered);
    CHECK(r.npcInclined);
    CHECK(r.hourOk);
    CHECK(r.wealthOk);
    CHECK(r.willingnessUpdated);
    // accept decay: 0.8*0.66 - 0.1 = 0.428 (golden)
    CHECK(r.newWillingness > 0.427f && r.newWillingness < 0.429f);
}

TEST(WorldLocTavern, CardGameDeclineByWealthStillDecays) {
    // wealth too low, but hour ok -> declined, willingness decays with -0.02 bias.
    auto r = TavernComputeCardGame(0.3f, 0.8f, 18, 5000);
    CHECK(!r.offered);
    CHECK(r.willingnessUpdated);
    // 0.8*0.66 - 0.02 = 0.508 (golden)
    CHECK(r.newWillingness > 0.507f && r.newWillingness < 0.509f);
}

TEST(WorldLocTavern, CardGameEarlyHourSkipsDecay) {
    // hour <= 11 -> LABEL_6: willingness unchanged.
    auto r = TavernComputeCardGame(0.3f, 0.8f, 9, 20000);
    CHECK(!r.offered);
    CHECK(!r.willingnessUpdated);
    CHECK(r.newWillingness > 0.799f && r.newWillingness < 0.801f);
}

TEST(WorldLocTavern, CardGameNpcDisinclined) {
    // rand >= willingness -> NPC won't play; hour ok so still decays (-0.02).
    auto r = TavernComputeCardGame(0.9f, 0.8f, 18, 20000);
    CHECK(!r.offered);
    CHECK(!r.npcInclined);
    CHECK(r.willingnessUpdated);
}

// ---------------------------------------------------------------------------
// Residence mistress (gilde.exe 0x5150f4).
// ---------------------------------------------------------------------------
TEST(WorldLocResidence, MistressEligibilityAndCooldown) {
    // owner exists, handler present, cooldown elapsed (last=100 < now=500) -> enabled.
    auto en = ResidenceComputeMistress(true, true, 100, 500, true, true);
    CHECK(en.affairEnabled);
    CHECK(en.cooldownElapsed);
    CHECK(en.emit.cmd == ResidenceCommand::StartAffair);

    // cooldown NOT elapsed (last >= now).
    auto cd = ResidenceComputeMistress(true, true, 600, 500, true, true);
    CHECK(!cd.cooldownElapsed);
    CHECK(!cd.affairEnabled);
    CHECK(cd.emit.cmd == ResidenceCommand::None);

    // missing owner / handler.
    CHECK(!ResidenceComputeMistress(false, true, 100, 500, true, true).affairEnabled);
    CHECK(!ResidenceComputeMistress(true, false, 100, 500, true, true).affairEnabled);

    // enabled but not confirmed -> no emit.
    auto noConfirm = ResidenceComputeMistress(true, true, 100, 500, true, false);
    CHECK(noConfirm.affairEnabled);
    CHECK(noConfirm.emit.cmd == ResidenceCommand::None);
}

// ---------------------------------------------------------------------------
// Residence master exam (gilde.exe 0x5159fc).
// ---------------------------------------------------------------------------
TEST(WorldLocResidence, MasterExamEligibilityAndPass) {
    // gauge=1.5, rank=4, no pending, journeymen=5, takeExam -> eligible & promote.
    auto pass = ResidenceComputeMasterExam(false, 1.5f, 4, 5, true);
    CHECK(pass.eligible);
    CHECK(pass.gaugeOk);
    CHECK(pass.rankOk);
    CHECK_EQ(pass.gaugeMax, 150); // 1.5 * 100
    CHECK(pass.staffOk);
    CHECK(pass.emit.cmd == ResidenceCommand::PromoteMaster);
    CHECK_EQ(pass.emit.arg, 2); // +2 days

    // pending letter -> ineligible.
    CHECK(!ResidenceComputeMasterExam(true, 1.5f, 4, 5, true).eligible);
    // gauge < 1 -> ineligible.
    CHECK(!ResidenceComputeMasterExam(false, 0.5f, 4, 5, true).eligible);
    // rank > 6 -> ineligible.
    CHECK(!ResidenceComputeMasterExam(false, 1.5f, 7, 5, true).eligible);
    // not enough staff -> no promote even when eligible.
    auto fewStaff = ResidenceComputeMasterExam(false, 1.5f, 4, 3, true);
    CHECK(fewStaff.eligible);
    CHECK(!fewStaff.staffOk);
    CHECK(fewStaff.emit.cmd == ResidenceCommand::None);
}

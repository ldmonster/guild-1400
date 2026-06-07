// End-to-end flow across the world/location rule-cores: one player visits the
// church (donation -> reputation), the thieves' guild (commission a burglary,
// settle a ransom), the tavern (card stake offer), and a residence (mistress +
// master exam) — verifying cost/outcome/relation state and the emitted commands
// against a hand-computed reference. RNG is the real crt LCG, seeded for
// determinism; relation deltas use the recovered world/relation matrix.
#include "world/location_church.h"
#include "world/location_thief.h"
#include "world/location_tavern.h"
#include "world/location_residence.h"
#include "world/relation.h"

#include "crt/rand.h"
#include "tests/framework/test.h"

#include <vector>

using namespace guild;
using namespace guild::world;

namespace {

// Mock command sink: records what each rule-core would have queued so the e2e
// flow can assert on the emitted commands without the network VM.
struct CommandLog {
    std::vector<ThiefEmit>     thief;
    std::vector<ResidenceEmit> residence;
};

// A synthetic player + visited NPC + city state.
struct Scenario {
    int playerId = 3;
    int npcId = 7;
    int playerWealth = 500000;
    int employerWealth = 300000;
};

} // namespace

// A church visit: the player donates 4000, raising the parish's standing. The
// donation's reputation delta is fed into the relation matrix (player -> npc).
TEST(WorldLocE2E, ChurchDonationRaisesReputation) {
    RelationReset();
    Scenario sc;

    auto don = ChurchComputeDonation(sc.playerWealth, sc.employerWealth, 4000);
    // Reference: combined=800000, sug=7999, rep=255*4000/800000 = 1.
    CHECK_EQ(don.wealthCombined, 800000);
    CHECK_EQ(don.suggestedCost, 7999);
    CHECK_EQ(don.reputDelta, 1);

    // Apply the reputation bump to the relation matrix and read it back.
    int before = RelationGet(sc.playerId, sc.npcId);
    RelationSet(sc.playerId, sc.npcId, before + don.reputDelta);
    CHECK_EQ(RelationGet(sc.playerId, sc.npcId), before + 1);

    // Confession of the player's two active crimes converts to a priest-standing
    // gain of paid/2.
    auto conf = ChurchComputeConfession(/*emp*/5, /*crimes*/2, /*paid*/200);
    CHECK(conf.canConfess);
    CHECK_EQ(conf.reputGain, 100);
}

// A thieves'-guild visit: commission a burglary on a reachable, low-security
// target with three available thieves, then settle a ransom on a held hostage.
TEST(WorldLocE2E, ThiefGuildBurglaryAndRansom) {
    CommandLog log;

    // Burglary: target reachable, security passed, 3/4 thieves free.
    std::vector<u8> thieves = {1, 0, 1, 1};
    auto burg = ThiefComputeBurglary(/*reachable*/true, /*security*/true, thieves);
    CHECK(burg.offered);
    CHECK_EQ(burg.eligibleThieves, 3);
    if (burg.emit.cmd != ThiefCommand::None) log.thief.push_back(burg.emit);

    // Ransom: hostage in captivity-state 2, wealth 500000, roll caps at wealth.
    // Reference: cut .04, base=500000, ransom = int(500000*0.04)=19999.
    double roll = 1600000.0; // (rand*0.5+0.75)*1.6e6 with rand=0.5
    auto rans = ThiefComputeRansom(/*hostage*/true, /*kidnapCmd*/true,
                                   /*pending*/false, /*state*/2,
                                   /*wealth*/500000, roll, /*accept*/true);
    CHECK_EQ(rans.ransom, 19999);
    if (rans.emit.cmd != ThiefCommand::None) log.thief.push_back(rans.emit);

    // The flow emitted exactly: QueueBurglary(3) then PayRansom(19999).
    CHECK_EQ((int)log.thief.size(), 2);
    CHECK(log.thief[0].cmd == ThiefCommand::QueueBurglary);
    CHECK_EQ(log.thief[0].arg, 3);
    CHECK(log.thief[1].cmd == ThiefCommand::PayRansom);
    CHECK_EQ(log.thief[1].arg, 19999);
}

// A tavern visit driven by the real seeded LCG: the card-game offer gate uses a
// random draw vs the NPC's willingness. We seed crt::Srand and derive a [0,1)
// draw the same way the engine's Math_RandomFloatScaled would feed the compare.
TEST(WorldLocE2E, TavernCardStakeWithSeededRng) {
    crt::Srand(12345);
    // RandNext() is in [0, 0x7FFF]; map to [0,1) for the willingness compare.
    int raw = crt::RandNext();
    float rand01 = (float)raw / 32768.0f;
    CHECK(rand01 >= 0.0f && rand01 < 1.0f);

    // High willingness, late hour, wealthy player -> whether offered depends on
    // the seeded draw; either way the willingness decays and the result is
    // internally consistent with the offer gate.
    float willingness = 0.9f;
    auto game = TavernComputeCardGame(rand01, willingness, /*hour*/20, /*wealth*/50000);
    CHECK_EQ(game.offered, (rand01 < willingness) && true && true);
    CHECK(game.hourOk);
    CHECK(game.wealthOk);
    // Hour is late (>11) so willingness is always updated here.
    CHECK(game.willingnessUpdated);
    if (game.offered) {
        // accept decay: w*0.66 - 0.10
        float expect = willingness * tavern::kWillingnessDecay + tavern::kWillingnessBiasYes;
        CHECK(game.newWillingness > expect - 0.001f && game.newWillingness < expect + 0.001f);
    } else {
        float expect = willingness * tavern::kWillingnessDecay + tavern::kWillingnessBiasNo;
        CHECK(game.newWillingness > expect - 0.001f && game.newWillingness < expect + 0.001f);
    }

    // Determinism: re-seeding reproduces the same draw and the same decision.
    crt::Srand(12345);
    int raw2 = crt::RandNext();
    CHECK_EQ(raw, raw2);
}

// A residence visit: court the mistress (cooldown elapsed) then sit the master's
// exam with a passing gauge and enough journeymen.
TEST(WorldLocE2E, ResidenceMistressThenMasterExam) {
    CommandLog log;

    auto aff = ResidenceComputeMistress(/*owner*/true, /*handler*/true,
                                        /*last*/100, /*now*/500,
                                        /*skillOk*/true, /*confirm*/true);
    CHECK(aff.affairEnabled);
    if (aff.emit.cmd != ResidenceCommand::None) log.residence.push_back(aff.emit);

    auto exam = ResidenceComputeMasterExam(/*pending*/false, /*gauge*/2.0f,
                                           /*rank*/3, /*journeymen*/6,
                                           /*takeExam*/true);
    CHECK(exam.eligible);
    CHECK_EQ(exam.gaugeMax, 200); // 2.0 * 100
    CHECK(exam.emit.cmd == ResidenceCommand::PromoteMaster);
    CHECK_EQ(exam.emit.arg, residence::kExamDaysAdvance);
    if (exam.emit.cmd != ResidenceCommand::None) log.residence.push_back(exam.emit);

    CHECK_EQ((int)log.residence.size(), 2);
    CHECK(log.residence[0].cmd == ResidenceCommand::StartAffair);
    CHECK(log.residence[1].cmd == ResidenceCommand::PromoteMaster);
}

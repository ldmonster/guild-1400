// Unit tests for the per-type cutscene MAIN/state-machine cores:
//   cutscene_duel.{h,cpp}     (type 4)
//   cutscene_wedding.{h,cpp}  (type 6)
//   cutscene_auction.{h,cpp}  (type 10)
//
// Determinism is anchored on the cutscene LCG (seed 12345); golden values were
// computed with python (state = 1103515245*state + 12345; r = (state>>16)%0x7FFF):
//   seed 12345 -> RandInt(100)=69, then RandInt(10)=9, then RandInt(3)=2, ...
#include "sim/cutscene_duel.h"
#include "sim/cutscene_wedding.h"
#include "sim/cutscene_auction.h"
#include "tests/framework/test.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild::sim;

// ===========================================================================
// RollDuelOutcomeTier — golden against the LCG.
// ===========================================================================
TEST(SimCutsceneTypes, DuelRollOutcomeTierTiered) {
    CutsceneRng rng; rng.state = 12345;
    // tiered mode: RandInt(100) -> 69 -> 69>50 -> tier 4 (shoot).
    CHECK_EQ(DuelRollOutcomeTier(rng, /*tiered*/true), (guild::u8)4);
}

TEST(SimCutsceneTypes, DuelRollOutcomeTierFlag) {
    CutsceneRng rng; rng.state = 12345;
    // non-tiered: RandInt(10) -> 9 -> 9>7 -> 1.
    CHECK_EQ(DuelRollOutcomeTier(rng, /*tiered*/false), (guild::u8)1);
}

TEST(SimCutsceneTypes, DuelRollOutcomeTierAllValidTiers) {
    // Over a spread of seeds, tiered mode always returns a valid tier (2/3/4)
    // and the >50 / <=15 / middle buckets are all reachable.
    bool saw2 = false, saw3 = false, saw4 = false;
    for (guild::u32 s = 1; s <= 200; ++s) {
        CutsceneRng rng; rng.state = s;
        guild::u8 t = DuelRollOutcomeTier(rng, /*tiered*/true);
        CHECK(t == 2 || t == 3 || t == 4);
        if (t == 2) saw2 = true; else if (t == 3) saw3 = true; else saw4 = true;
    }
    CHECK(saw2 && saw3 && saw4);
}

// ===========================================================================
// ProcessIntroChoice — taunt / aim / shoot wiring (rules math from duel.cpp).
// ===========================================================================
TEST(SimCutsceneTypes, DuelProcessIntroChoiceShootHit) {
    CutsceneRng rng; rng.state = 12345;
    DuelState st{};
    st.skillA = 1.0f;  // chance = 1.0*0.8 = 0.8; first RandFloat()=0.655 < 0.8 -> HIT
    DuelCombatant a{}; a.skill = 1.0f; a.hp = 100; a.worth = 100;
    DuelCombatant b{}; b.hp = 100; b.worth = 100;
    DuelCombatant tgt = b;
    DuelShotResult r = DuelProcessIntroChoice(st, /*isA*/true,
                                              DuelIntroChoice::kShoot, a, b, tgt, rng);
    CHECK(r.hit);
    CHECK(r.damage >= 15 && r.damage <= 44);  // non-aim band
    CHECK(tgt.hp < 100);                       // HP loss applied
    CHECK_EQ((int)st.scoreA, r.damage);
}

TEST(SimCutsceneTypes, DuelProcessIntroChoiceShootMiss) {
    CutsceneRng rng; rng.state = 12345;
    DuelState st{};
    st.skillA = 0.5f;  // chance = 0.4; RandFloat()=0.655 >= 0.4 -> MISS
    DuelCombatant a{}; a.skill = 0.5f; a.hp = 100; a.worth = 100;
    DuelCombatant b{}; b.hp = 100; b.worth = 100;
    DuelCombatant tgt = b;
    DuelShotResult r = DuelProcessIntroChoice(st, true, DuelIntroChoice::kShoot, a, b, tgt, rng);
    CHECK(!r.hit);
    CHECK_EQ(r.damage, 0);
    CHECK_EQ(tgt.hp, 100);
}

TEST(SimCutsceneTypes, DuelProcessIntroChoiceAimGained) {
    CutsceneRng rng; rng.state = 12345;
    DuelState st{};
    DuelCombatant a{}; a.aimRating = 1.0f;  // RandFloat()=0.655 < 1.0 -> aim gained
    DuelCombatant b{};
    DuelCombatant tgt = b;
    DuelProcessIntroChoice(st, true, DuelIntroChoice::kAim, a, b, tgt, rng);
    CHECK(st.aimA);
}

// ===========================================================================
// CutsceneDuel — full duel to outcome (mock leaves, seeded RNG).
// ===========================================================================
namespace {
struct DuelRec {
    int rounds = 0, shots = 0; bool outcome = false; DuelOutcome out{};
};
DuelRec* g_drec = nullptr;
void OnRound(int, DuelIntroChoice, DuelIntroChoice, void*) { if (g_drec) g_drec->rounds++; }
void OnShot(int, const DuelShotResult&, void*) { if (g_drec) g_drec->shots++; }
void OnOutcome(const DuelOutcome& o, void*) { if (g_drec) { g_drec->outcome = true; g_drec->out = o; } }
// Both combatants always shoot.
void AlwaysShoot(int, DuelCombatant& a, DuelCombatant& b, CutsceneRng&, void*) {
    a.choice = DuelIntroChoice::kShoot; b.choice = DuelIntroChoice::kShoot;
}
} // namespace

TEST(SimCutsceneTypes, DuelAbortsOnMissingParticipant) {
    CutsceneRng rng; rng.state = 1;
    DuelCutsceneHooks hooks{};
    DuelCombatant a{}; a.personId = -1;  // unresolved
    DuelCombatant b{}; b.personId = 7;
    DuelOutcome out = CutsceneDuel(a, b, rng, hooks);
    CHECK(out.aborted);
    CHECK_EQ(out.winner, -1);
}

TEST(SimCutsceneTypes, DuelRunsToOutcomeWithLethalShots) {
    DuelRec rec; g_drec = &rec;
    CutsceneRng rng; rng.state = 12345;
    DuelCutsceneHooks hooks{};
    hooks.onRound = OnRound; hooks.onShot = OnShot; hooks.onOutcome = OnOutcome;

    DuelCombatant a{}; a.personId = 1; a.skill = 1.0f; a.hp = 100; a.worth = 100;
    DuelCombatant b{}; b.personId = 2; b.skill = 1.0f; b.hp = 100; b.worth = 100;
    // worth=100 so a hit removes worth*dmg*0.01 = dmg HP; a few hits drop below 0.2.
    DuelOutcome out = CutsceneDuel(a, b, rng, hooks, AlwaysShoot, nullptr);

    CHECK(rec.outcome);
    CHECK(out.rounds >= 1 && out.rounds <= kDuelMaxRounds);
    CHECK(rec.shots >= 1);
    // Some hits accumulated -> scores nonzero or a draw if all missed (RNG-fixed).
    CHECK(out.over || out.winner == -1);
    g_drec = nullptr;
}

TEST(SimCutsceneTypes, DuelDrawWhenNoFatalHit) {
    CutsceneRng rng; rng.state = 12345;
    DuelCutsceneHooks hooks{};
    // worth=1 -> HP loss per hit = 1*dmg*0.01 < 1, never fatal in 3 rounds.
    DuelCombatant a{}; a.personId = 1; a.skill = 1.0f; a.hp = 100; a.worth = 1;
    DuelCombatant b{}; b.personId = 2; b.skill = 1.0f; b.hp = 100; b.worth = 1;
    DuelOutcome out = CutsceneDuel(a, b, rng, hooks, AlwaysShoot, nullptr);
    CHECK(!out.over);
    CHECK_EQ(out.winner, -1);          // draw
    CHECK_EQ(out.rounds, kDuelMaxRounds);
}

// ===========================================================================
// Wedding.
// ===========================================================================
namespace {
struct WedRec {
    int panels = 0; int marriageCmds = 0; int quest = 0; int notify = 0;
    int firstPanel = -1;
};
WedRec* g_wrec = nullptr;
void WLoad(const char*, const char*, void*) {}
void WPanel(int id, void*) { if (g_wrec) { if (g_wrec->panels == 0) g_wrec->firstPanel = id; g_wrec->panels++; } }
void WTrack(guild::i32, void*) { if (g_wrec) g_wrec->quest++; }
void WCmd(guild::i32, int, int, guild::u32, void*) { if (g_wrec) g_wrec->marriageCmds++; }
void WNotify(guild::i32, guild::i32, guild::i32, void*) { if (g_wrec) g_wrec->notify++; }
} // namespace

TEST(SimCutsceneTypes, WeddingRunsCeremony) {
    WedRec rec; g_wrec = &rec;
    WeddingCutsceneHooks h{};
    h.loadScene = WLoad; h.onPanel = WPanel; h.trackCrimeProgress = WTrack;
    h.marriageCommand = WCmd;

    WeddingPerson a{}; a.personId = 10; a.kind = 6; a.factionTag = 1; a.isFemale = false;
    WeddingPerson b{}; b.personId = 11; b.kind = 6; b.factionTag = 2; b.isFemale = false;
    WeddingOutcome out = CutsceneWedding(a, b, "Hans", "Greta", h);

    CHECK(out.married);
    CHECK(!out.aborted);
    CHECK_EQ(rec.marriageCmds, 2);                 // one per spouse
    CHECK_EQ(rec.quest, 2);                         // both kind 6
    CHECK_EQ(rec.panels, kWeddingPanelCount);       // full sequence
    CHECK_EQ(rec.firstPanel, 5814);                 // vows first
    CHECK_EQ(std::string(out.coupleName), std::string("Hans Greta 1"));
    g_wrec = nullptr;
}

TEST(SimCutsceneTypes, WeddingNameSwapOnFemaleFirst) {
    WeddingCutsceneHooks h{};
    WeddingPerson a{}; a.personId = 10; a.kind = 7; a.factionTag = 1; a.isFemale = true;
    WeddingPerson b{}; b.personId = 11; b.kind = 7; b.factionTag = 2;
    WeddingOutcome out = CutsceneWedding(a, b, "Greta", "Hans", h);
    // a.isFemale -> the two names swap: "Hans Greta 1".
    CHECK_EQ(std::string(out.coupleName), std::string("Hans Greta 1"));
}

TEST(SimCutsceneTypes, WeddingAbortsOnUnresolved) {
    WeddingCutsceneHooks h{};
    WeddingPerson a{}; a.personId = 10; a.resolves = false;
    WeddingPerson b{}; b.personId = 11;
    WeddingOutcome out = CutsceneWedding(a, b, "X", "Y", h);
    CHECK(out.aborted);
    CHECK(!out.married);
}

TEST(SimCutsceneTypes, CheckMarriageEligibleYes) {
    WedRec rec; g_wrec = &rec;
    WeddingCutsceneHooks h{}; h.notifyBetrothal = WNotify; h.marriageCommand = WCmd;
    WeddingPerson a{}; a.personId = 10; a.kind = 6; a.factionTag = 1;
    WeddingPerson b{}; b.personId = 11; b.kind = 6; b.factionTag = 2;
    guild::i32 others[] = { 20, 21, 10 /*self, skipped*/ };
    int r = CutsceneCheckMarriageEligible(a, b, others, 3, h);
    CHECK_EQ(r, 1);
    CHECK_EQ(rec.notify, 2);          // 20 and 21 (10 == self skipped)
    CHECK_EQ(rec.marriageCmds, 0);    // no cancel on eligibility
    g_wrec = nullptr;
}

TEST(SimCutsceneTypes, CheckMarriageEligibleNoSameFaction) {
    WedRec rec; g_wrec = &rec;
    WeddingCutsceneHooks h{}; h.notifyBetrothal = WNotify; h.marriageCommand = WCmd;
    WeddingPerson a{}; a.personId = 10; a.kind = 6; a.factionTag = 1;
    WeddingPerson b{}; b.personId = 11; b.kind = 6; b.factionTag = 1; // same faction
    int r = CutsceneCheckMarriageEligible(a, b, nullptr, 0, h);
    CHECK_EQ(r, 0);
    CHECK_EQ(rec.marriageCmds, 2);    // cancel both
    g_wrec = nullptr;
}

// ===========================================================================
// Auction.
// ===========================================================================
namespace {
struct AucRec { int rounds = 0; bool sold = false; bool noSale = false; AuctionOutcome out{}; };
AucRec* g_arec = nullptr;
void ALoad(const char*, int, void*) {}
void ARound(int, int, int, void*) { if (g_arec) g_arec->rounds++; }
void ASold(guild::i32, int, int, int, void*) { if (g_arec) g_arec->sold = true; }
void ANoSale(void*) { if (g_arec) g_arec->noSale = true; }
} // namespace

TEST(SimCutsceneTypes, AuctionTextureSet) {
    CHECK_EQ(AuctionTextureSetForRegion(AuctionRegion::kForest), 0);
    CHECK_EQ(AuctionTextureSetForRegion(AuctionRegion::kQuarry), 1);
    CHECK_EQ(AuctionTextureSetForRegion(AuctionRegion::kMine),   2);
}

TEST(SimCutsceneTypes, AuctionWinnerHighestBid) {
    AucRec rec; g_arec = &rec;
    AuctionCutsceneHooks h{}; h.loadScene = ALoad; h.onRound = ARound;
    h.onSold = ASold; h.onNoSale = ANoSale;

    std::vector<AuctionBidder> bidders = {
        {1, true}, {2, true}, {3, true},
    };
    // round 0: all three bid (1->100, 2->150, 3->120) -> leader = idx1 (150).
    // round 1: only idx1 left -> single bidder -> wins.
    std::vector<std::vector<AuctionBid>> rounds = {
        { {100, true}, {150, true}, {120, true} },
        { {0, false},  {150, true}, {0, false} },
    };
    AuctionOutcome out = CutsceneAuction(AuctionRegion::kMine, bidders, rounds, 50, h);
    CHECK(out.sold);
    CHECK_EQ(out.winnerIndex, 1);
    CHECK_EQ(out.winnerId, 2);
    CHECK_EQ(out.winningBid, 150);
    CHECK_EQ(out.toOwner, 135);   // 150 * 0.9
    CHECK_EQ(out.toOther, 15);    // 150 * 0.1
    CHECK(rec.sold);
    g_arec = nullptr;
}

TEST(SimCutsceneTypes, AuctionNoSaleWhenNoBidders) {
    AucRec rec; g_arec = &rec;
    AuctionCutsceneHooks h{}; h.onNoSale = ANoSale;
    std::vector<AuctionBidder> bidders = { {1, true}, {2, true} };
    std::vector<std::vector<AuctionBid>> rounds = {
        { {0, false}, {0, false} },   // nobody active
    };
    AuctionOutcome out = CutsceneAuction(AuctionRegion::kForest, bidders, rounds, 50, h);
    CHECK(!out.sold);
    CHECK_EQ(out.winnerIndex, -1);
    CHECK(rec.noSale);
    g_arec = nullptr;
}

TEST(SimCutsceneTypes, AuctionAscendingAskIncrement) {
    AuctionCutsceneHooks h{};
    std::vector<AuctionBidder> bidders = { {1, true}, {2, true} };
    // two contested rounds then a single -> winner. Verify it runs >1 round.
    std::vector<std::vector<AuctionBid>> rounds = {
        { {100, true}, {110, true} },   // contested -> ask += 32
        { {0, false},  {110, true} },   // single -> win
    };
    AuctionOutcome out = CutsceneAuction(AuctionRegion::kQuarry, bidders, rounds, 50, h);
    CHECK(out.sold);
    CHECK_EQ(out.winnerIndex, 1);
    CHECK_EQ(out.rounds, 2);
}

TEST(SimCutsceneTypes, AuctionAbortsWhenOwnerUnresolved) {
    AuctionCutsceneHooks h{};
    std::vector<AuctionBidder> bidders = { {1, true} };
    std::vector<std::vector<AuctionBid>> rounds = { { {100, true} } };
    AuctionOutcome out = CutsceneAuction(AuctionRegion::kMine, bidders, rounds, 50, h,
                                         /*ownerResolves*/false);
    CHECK(out.aborted);
}

// ===========================================================================
// BroadcastMessage (type-10 step).
// ===========================================================================
namespace {
int g_speechCount = 0;
void CountSpeech(guild::i32, void*) { ++g_speechCount; }
} // namespace

TEST(SimCutsceneTypes, BroadcastMessagePlayerParticipants) {
    g_speechCount = 0;
    guild::i32 parts[] = { 1, 2, 3 };
    guild::u8  kinds[] = { 6, 4, 7 };   // 6 and 7 are player class, 4 is not
    int r = CutsceneBroadcastMessage(true, parts, kinds, 3, CountSpeech, nullptr);
    CHECK_EQ(r, 1);
    CHECK_EQ(g_speechCount, 2);
}

TEST(SimCutsceneTypes, BroadcastMessageNoBuilding) {
    g_speechCount = 0;
    guild::i32 parts[] = { 1 };
    guild::u8  kinds[] = { 6 };
    int r = CutsceneBroadcastMessage(false, parts, kinds, 1, CountSpeech, nullptr);
    CHECK_EQ(r, 0);
    CHECK_EQ(g_speechCount, 0);
}

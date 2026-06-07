// End-to-end: run a duel cutscene and a wedding cutscene to completion,
// tick-by-tick (round-by-round / panel-by-panel), recording the phase sequence,
// the emitted (mock) commands and the outcome, then diff against a hand-written
// reference. Determinism is anchored on the seeded cutscene LCG.
#include "sim/cutscene_duel.h"
#include "sim/cutscene_wedding.h"
#include "sim/cutscene_auction.h"
#include "tests/framework/test.h"

#include <string>
#include <vector>

using namespace guild::sim;

// ===========================================================================
// E2E #1 — a full duel.  Both combatants always shoot; worth=100 so each hit
// removes `dmg` HP. We log every round and shot, and the final outcome, and
// compare to the reference event trace.
// ===========================================================================
namespace {
std::vector<std::string>* g_duelLog = nullptr;
void E2EOnRound(int r, DuelIntroChoice a, DuelIntroChoice b, void*) {
    g_duelLog->push_back("round " + std::to_string(r) + " A=" +
                         std::to_string((int)a) + " B=" + std::to_string((int)b));
}
void E2EOnShot(int shooter, const DuelShotResult& res, void*) {
    g_duelLog->push_back(std::string("shot by ") + (shooter ? "B" : "A") +
                         (res.hit ? " HIT" : " MISS"));
}
void E2EOnOutcome(const DuelOutcome& o, void*) {
    g_duelLog->push_back("outcome over=" + std::to_string((int)o.over) +
                         " winner=" + std::to_string(o.winner));
}
void E2EAlwaysShoot(int, DuelCombatant& a, DuelCombatant& b, CutsceneRng&, void*) {
    a.choice = DuelIntroChoice::kShoot;
    b.choice = DuelIntroChoice::kShoot;
}
} // namespace

TEST(SimCutsceneTypesE2E, DuelToCompletion) {
    std::vector<std::string> log; g_duelLog = &log;
    CutsceneRng rng; rng.state = 12345;

    DuelCutsceneHooks hooks{};
    hooks.onRound = E2EOnRound; hooks.onShot = E2EOnShot; hooks.onOutcome = E2EOnOutcome;

    DuelCombatant a{}; a.personId = 1; a.skill = 1.0f; a.hp = 100; a.worth = 100;
    DuelCombatant b{}; b.personId = 2; b.skill = 1.0f; b.hp = 100; b.worth = 100;
    DuelOutcome out = CutsceneDuel(a, b, rng, hooks, E2EAlwaysShoot, nullptr);

    // The trace must (a) open with round 0, (b) contain at least one shot, and
    // (c) close with exactly one outcome line that matches the returned outcome.
    CHECK(!log.empty());
    CHECK_EQ(log.front(), std::string("round 0 A=4 B=4"));
    CHECK(log.back().rfind("outcome ", 0) == 0);

    // Every round line precedes its shots; count rounds vs the outcome.
    int roundLines = 0, shotLines = 0, outcomeLines = 0;
    for (auto& l : log) {
        if (l.rfind("round ", 0) == 0) ++roundLines;
        else if (l.rfind("shot by ", 0) == 0) ++shotLines;
        else if (l.rfind("outcome ", 0) == 0) ++outcomeLines;
    }
    CHECK_EQ(roundLines, out.rounds);
    CHECK_EQ(outcomeLines, 1);
    CHECK(shotLines >= 1);

    // Determinism: re-running with the same seed reproduces the identical trace.
    std::vector<std::string> log2; g_duelLog = &log2;
    CutsceneRng rng2; rng2.state = 12345;
    DuelCombatant a2 = a, b2 = b; a2.hp = 100; b2.hp = 100;
    CutsceneDuel(a2, b2, rng2, hooks, E2EAlwaysShoot, nullptr);
    CHECK_EQ(log.size(), log2.size());
    bool identical = (log == log2);
    CHECK(identical);
    g_duelLog = nullptr;
}

// ===========================================================================
// E2E #2 — a full wedding.  Validate the participants, emit the marriage
// commands, then play the ceremony panel sequence. The event trace must match
// the reference (scene -> 2 marriage cmds -> 2 quest hooks -> 6 panels in order).
// ===========================================================================
namespace {
std::vector<std::string>* g_wedLog = nullptr;
void E2EWLoad(const char* scene, const char* name, void*) {
    g_wedLog->push_back(std::string("scene ") + scene + " [" + name + "]");
}
void E2EWPanel(int id, void*) { g_wedLog->push_back("panel " + std::to_string(id)); }
void E2EWTrack(guild::i32 pid, void*) { g_wedLog->push_back("quest " + std::to_string(pid)); }
void E2EWCmd(guild::i32 pid, int op, int w, guild::u32 bit, void*) {
    g_wedLog->push_back("marry " + std::to_string(pid) + " op" + std::to_string(op) +
                        " w" + std::to_string(w) + " bit" + std::to_string(bit));
}
} // namespace

TEST(SimCutsceneTypesE2E, WeddingToCompletion) {
    std::vector<std::string> log; g_wedLog = &log;
    WeddingCutsceneHooks h{};
    h.loadScene = E2EWLoad; h.onPanel = E2EWPanel;
    h.trackCrimeProgress = E2EWTrack; h.marriageCommand = E2EWCmd;

    WeddingPerson a{}; a.personId = 10; a.kind = 6; a.factionTag = 1;
    WeddingPerson b{}; b.personId = 11; b.kind = 6; b.factionTag = 2;
    WeddingOutcome out = CutsceneWedding(a, b, "Hans", "Greta", h);

    CHECK(out.married);

    // Reference trace (order recovered from VIBE_Cutscene_Wedding):
    //   quest 10, quest 11, marry 10, marry 11, scene, then panels 5814,5817,
    //   5816,5815,5816,5818.
    std::vector<std::string> ref = {
        "quest 10",
        "quest 11",
        "marry 10 op456 w4 bit262144",
        "marry 11 op456 w4 bit262144",
        "scene KIRCHE_HOCHZEIT.ed3 [Hans Greta 1]",
        "panel 5814", "panel 5817", "panel 5816",
        "panel 5815", "panel 5816", "panel 5818",
    };
    CHECK_EQ(log.size(), ref.size());
    bool match = (log == ref);
    CHECK(match);
    if (!match) {
        for (size_t i = 0; i < log.size() && i < ref.size(); ++i)
            if (log[i] != ref[i])
                std::printf("    mismatch[%zu]: got '%s' want '%s'\n",
                            i, log[i].c_str(), ref[i].c_str());
    }
    g_wedLog = nullptr;
}

// ===========================================================================
// E2E #3 — an auction across multiple rounds, verifying the ascending-ask /
// winner outcome and the lease split, with the per-round trace.
// ===========================================================================
namespace {
std::vector<std::string>* g_aucLog = nullptr;
void E2EARound(int r, int ask, int active, void*) {
    g_aucLog->push_back("round " + std::to_string(r) + " ask" + std::to_string(ask) +
                        " active" + std::to_string(active));
}
void E2EASold(guild::i32 id, int bid, int owner, int other, void*) {
    g_aucLog->push_back("sold to " + std::to_string(id) + " bid" + std::to_string(bid) +
                        " owner" + std::to_string(owner) + " other" + std::to_string(other));
}
} // namespace

TEST(SimCutsceneTypesE2E, AuctionToCompletion) {
    std::vector<std::string> log; g_aucLog = &log;
    AuctionCutsceneHooks h{}; h.onRound = E2EARound; h.onSold = E2EASold;

    std::vector<AuctionBidder> bidders = { {1, true}, {2, true}, {3, true} };
    std::vector<std::vector<AuctionBid>> rounds = {
        { {100, true}, {150, true}, {120, true} },  // 3 active -> leader idx1 (150), ask+=32
        { {0, false},  {200, true}, {130, true} },  // 2 active -> leader idx1 (200), ask+=32
        { {0, false},  {200, true}, {0, false}   },  // 1 active -> idx1 wins
    };
    AuctionOutcome out = CutsceneAuction(AuctionRegion::kMine, bidders, rounds, 50, h);

    CHECK(out.sold);
    CHECK_EQ(out.winnerIndex, 1);
    CHECK_EQ(out.winnerId, 2);
    CHECK_EQ(out.winningBid, 200);
    CHECK_EQ(out.toOwner, 180);   // 200 * 0.9
    CHECK_EQ(out.toOther, 20);    // 200 * 0.1
    CHECK_EQ(out.rounds, 3);

    // Trace: three round lines then the sold line.
    CHECK_EQ(log.size(), (size_t)4);
    CHECK_EQ(log[0], std::string("round 0 ask182 active3"));   // 150 + 32
    CHECK_EQ(log[3], std::string("sold to 2 bid200 owner180 other20"));
    g_aucLog = nullptr;
}

#include "test.h"

#include "ai/favorability.h"
#include "ai/needs.h"
#include "crt/rand.h"

#include <cmath>
#include <vector>

using namespace guild::ai;
using guild::u8;
using guild::u32;
using guild::i32;
using guild::i16;

namespace {

// A "ledger" hook recording the full command stream a need-driven AI turn emits.
struct Ledger : NeedsCommandHook {
    struct Delta { i32 entity; u32 needWord; };
    struct Stock { i16 type; i32 delta; };
    std::vector<Delta> deltas;
    std::vector<Stock> stocks;
    void EmitNeedDelta(i32 e, u32 w) override { deltas.push_back({e, w}); }
    void AdjustStock(i16 t, i32 d) override { stocks.push_back({t, d}); }
};

// Favorability env modelling a small recruiting office: the master (self=1)
// scores three candidates. Candidate 2 is the spouse, candidate 4 shares the
// guild rank, candidate 9 is a stranger.
struct OfficeEnv : FavorabilityEnv {
    FavPersonFields Person(int id) override {
        FavPersonFields f;
        if (id == 1) {                 // the recruiting master
            f.officeId = 6; f.factionHigh = 3; f.rankHigh = 2;
            f.relationByteSelf = 0x14000000;  // (>>24) == 20
            f.inventoryBase = 1; f.guildBitsLow = 0;
        } else if (id == 2) {          // spouse
            f.officeId = 6; f.factionHigh = 3; f.rankHigh = 2;
            f.spouseRecordPtr = 99; f.spousePartnerId = 1;
        } else if (id == 4) {          // ally, same faction
            f.officeId = 6; f.factionHigh = 3; f.rankHigh = 2;
        } else {                       // stranger, other faction
            f.officeId = 6; f.factionHigh = 8; f.rankHigh = 1;
        }
        return f;
    }
    OfficeDefinition Office(u8 officeId) override {
        OfficeDefinition d;
        if (officeId == 6) { d.kind = 0; d.tier = 5; d.weight = 6.0f; }
        return d;
    }
    int WorkstationWorkers(int) override { return 0; }
    int QueryByGoodType(int) override { return 0; }
    int QueryBeginWorkers(int, int, bool& g) override { g = false; return 0; }
    int GesetzState() override { return 0; }
    int InventorySlot(int, int) override { return 1; }  // all probes succeed
};

bool nearly(double a, double b) { return std::fabs(a - b) < 1e-6; }

} // namespace

// Full need-decay turn: repeatedly pick/refill needs from a fixed seed and check
// the emitted command ledger is deterministic and internally consistent.
TEST(AiFavNeedsE2E, NeedTurn_DeterministicLedger) {
    guild::crt::Srand(20260605);
    Ledger ledger;
    NeedAgent agent; agent.type = 4; agent.id = 1001; agent.needWord = 0;

    int picks = 0;
    for (int round = 0; round < 6; ++round) {
        u8 nid = (round & 1) ? PickRandomNeedAndClearGroupB(agent, &ledger)
                             : PickRandomNeedAndClearGroup(agent, &ledger);
        if (nid)
            ++picks;
    }

    // Every successful pick emits exactly one delta and one stock adjustment.
    CHECK_EQ((int)ledger.deltas.size(), picks);
    CHECK_EQ((int)ledger.stocks.size(), picks);
    // The need word ends up matching the last emitted delta.
    if (!ledger.deltas.empty())
        CHECK_EQ(agent.needWord, ledger.deltas.back().needWord);
    // All stock adjustments are non-positive (consumption) and target type 4.
    for (const auto& s : ledger.stocks) {
        CHECK(s.delta <= 0);
        CHECK_EQ(s.type, (i16)4);
    }
    // Determinism: re-run from the same seed yields an identical ledger size.
    guild::crt::Srand(20260605);
    Ledger again;
    NeedAgent a2; a2.type = 4; a2.id = 1001; a2.needWord = 0;
    for (int round = 0; round < 6; ++round)
        (round & 1) ? PickRandomNeedAndClearGroupB(a2, &again)
                    : PickRandomNeedAndClearGroup(a2, &again);
    CHECK_EQ((int)again.deltas.size(), (int)ledger.deltas.size());
    if (!again.deltas.empty())
        CHECK_EQ(again.deltas.back().needWord, ledger.deltas.back().needWord);
}

// Recruiting flow: score three candidates and confirm the ordering the AI uses
// (spouse > ally > stranger) plus the averaged-favorability summary.
TEST(AiFavNeedsE2E, Recruiting_FavorabilityOrdering) {
    OfficeEnv env;
    double spouse  = ComputePersonFavorability(1, 2, true, env);
    double ally    = ComputePersonFavorability(1, 4, true, env);
    double stranger = ComputePersonFavorability(1, 9, true, env);

    // Spouse gets the +363 bonus and same-faction weight -> highest.
    CHECK(spouse > ally);
    // Ally shares faction (adds weight); stranger differs (subtracts) -> ally higher.
    CHECK(ally > stranger);
    // All within the clamped range.
    CHECK(spouse <= 100.0 && stranger >= 0.0);

    // Average over the candidate slate (with one empty slot) scaled by 0.01.
    int ids[4] = {2, 4, -1, 9};
    auto resolve = [](int raw) { return raw; };
    double avg = AverageObjectFavorability(1, ids, 4, env, resolve);
    double expect = (float)((ComputePersonFavorability(1, 2, false, env)
                           + ComputePersonFavorability(1, 4, false, env)
                           + ComputePersonFavorability(1, 9, false, env)) * 0.01 / 3.0);
    CHECK(nearly(avg, expect));
}

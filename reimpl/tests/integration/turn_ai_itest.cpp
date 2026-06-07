// Integration: RunAiTurn drives the REAL reconstructed AI/NPC passes over the
// LIVE shared entity arrays for one turn, mutating real Person records AND
// advancing play::HashWorldState(). The same seed over the same starting world is
// byte-identical across reruns (determinism), and a multi-turn run genuinely
// evolves the world state turn over turn.
#include "test.h"

#include "play/turn_ai.h"
#include "play/determinism.h"
#include "sim/entity.h"
#include "sim/person.h"
#include "sim/types.h"
#include "sim/building_lifecycle.h"
#include "crt/rand.h"

#include <cstdint>
#include <vector>

using namespace guild;

namespace {

// Seed a small deterministic synthetic world into the LIVE g_persons array: a few
// live "worker" persons owned by faction 0, with attitudes/relations the MeisterAi
// mood rule will move, plus a couple of group-leader (kind 3) and meister (kind 6)
// persons so the broadcast + event passes have work to do. RNG rooted at Srand so
// the seeding itself is reproducible.
void SeedSyntheticWorld(std::uint32_t seed) {
    sim::ResetEntityArrays();
    sim::ResetBuildingPersons();
    crt::Srand(seed);

    const int kWorkers = 12;
    for (int i = 0; i < kWorkers; ++i) {
        sim::Person& p = sim::g_persons[i];
        p.marker      = 4;                 // alive (!= -1)
        p.isPlayer    = 1;                 // a live actor / worker
        p.id          = 1000 + i;
        p.ownerPlayer = 0;                 // faction 0
        // rotate kinds so some are workers(5), some group leaders(3), some
        // meisters(6) -> exercises mood + broadcast + event passes.
        u8 kinds[] = {5, 5, 3, 6, 5, 3, 5, 6, 5, 5, 3, 6};
        p.kind = kinds[i % (int)(sizeof(kinds))];

        // attitude bytes (+61/+65) + relation/heat (+0x80) the mood core reads,
        // spread around 100 so MoodRelationDelta produces both signs.
        sim::PersonSetByte(&p, 61, (u8)(70 + (crt::RandNext() % 60)));   // 70..129
        sim::PersonSetByte(&p, 65, (u8)(70 + (crt::RandNext() % 60)));
        sim::PersonSetByte(&p, sim::kPfReputation, (u8)(crt::RandNext() % 40)); // 0..39
        sim::PersonSetDword(&p, sim::kPfTurnBits,
                            (i32)(0xFFFFFFFFu));   // all transient bits set -> sweep clears
        sim::PersonSetDword(&p, sim::kPfStatusFlag, 0);
    }
}

} // namespace

TEST(TurnAiItest, OneTurnMutatesLiveWorldAndAdvancesHash) {
    SeedSyntheticWorld(4242);

    std::uint64_t before = play::HashWorldState();
    play::AiTurnEffects fx = play::RunAiTurn(/*seed=*/4242);
    std::uint64_t after = play::HashWorldState();

    // The AI cascade ran the full recovered ordered pass list.
    CHECK(fx.passesRun > 0);
    CHECK(fx.aiPassesRun > 0);

    // The per-faction MeisterAi turn evaluated the live workers.
    CHECK(fx.factionsProcessed >= 1);
    CHECK(fx.workersEvaluated >= 1);

    // It actually mutated the world: the NPC turn-flag sweep cleared transient
    // bits on live records, and the mood pass moved some relation values.
    CHECK(fx.npcFlagsCleared >= 1);
    CHECK(fx.moodDeltasApplied >= 1);

    // The group-state broadcast ran over the kind-3 leaders.
    CHECK(fx.groupBroadcasts >= 1);

    // ==> the whole-world digest advanced (the world genuinely changed).
    CHECK(before != after);
}

TEST(TurnAiItest, SameSeedSameWorldIsByteIdenticalAcrossReruns) {
    // Run A.
    SeedSyntheticWorld(4242);
    play::AiTurnEffects fxA = play::RunAiTurn(4242);
    std::uint64_t hashA = play::HashWorldState();
    play::WorldSnapshot snapA = play::SnapshotWorld();

    // Run B — identical seed + identical starting world.
    SeedSyntheticWorld(4242);
    play::AiTurnEffects fxB = play::RunAiTurn(4242);
    std::uint64_t hashB = play::HashWorldState();
    play::WorldSnapshot snapB = play::SnapshotWorld();

    // Identical effect tally and identical end-of-turn world digest.
    CHECK(fxA == fxB);
    CHECK_EQ(hashA, hashB);

    std::string diff;
    CHECK(play::CompareSnapshots(snapA, snapB, &diff));
    CHECK(diff.empty());
}

TEST(TurnAiItest, MultiTurnRunEvolvesAndStaysDeterministic) {
    // Run K turns from one root seed; capture the per-turn world digest.
    const int K = 4;
    auto runK = [&](std::vector<std::uint64_t>& hashes,
                    std::vector<play::AiTurnEffects>& fx) {
        SeedSyntheticWorld(7);
        hashes.clear(); fx.clear();
        for (int t = 0; t < K; ++t) {
            // re-seed per turn (seed + t) so each turn's RNG stream is distinct
            // -> the world keeps evolving, not converging to a fixed point.
            fx.push_back(play::RunAiTurn((std::uint32_t)(100 + t)));
            hashes.push_back(play::HashWorldState());
        }
    };

    std::vector<std::uint64_t> h1, h2;
    std::vector<play::AiTurnEffects> f1, f2;
    runK(h1, f1);
    runK(h2, f2);

    // Determinism: the two K-turn runs produced identical per-turn digests.
    CHECK_EQ(h1.size(), (std::size_t)K);
    CHECK_EQ(h1.size(), h2.size());
    bool sameHashes = (h1.size() == h2.size());
    for (std::size_t i = 0; i < h1.size() && sameHashes; ++i)
        sameHashes = sameHashes && (h1[i] == h2[i]);
    CHECK(sameHashes);

    bool sameFx = (f1.size() == f2.size());
    for (std::size_t i = 0; i < f1.size() && sameFx; ++i)
        sameFx = sameFx && (f1[i] == f2[i]);
    CHECK(sameFx);

    // Evolution: at least one turn changed the digest relative to the previous
    // (the world is not static across the K turns).
    bool evolved = false;
    for (std::size_t i = 1; i < h1.size(); ++i)
        if (h1[i] != h1[i - 1]) evolved = true;
    CHECK(evolved);
}

// Unit: the determinism harness's hash is order-stable, sensitive to a single
// field change, and reproducible. Operates on the REAL entity arrays via extern.
#include "test.h"

#include "play/determinism.h"
#include "sim/entity.h"
#include "sim/building_lifecycle.h"
#include "crt/rand.h"

using namespace guild;

namespace {
// Bring the world to a known baseline before each assertion block.
void ResetWorld() {
    sim::ResetEntityArrays();
    sim::ResetBuildingPersons();
    crt::Srand(1);
}
} // namespace

TEST(Determinism, EmptyWorldIsStableAndReproducible) {
    ResetWorld();
    std::uint64_t h1 = play::HashWorldState();
    std::uint64_t h2 = play::HashWorldState();
    // Hashing twice without mutation must be identical (order-stable, pure).
    CHECK_EQ(h1, h2);

    // Reset to the same empty state again -> same digest (reproducible zero).
    ResetWorld();
    std::uint64_t h3 = play::HashWorldState();
    CHECK_EQ(h1, h3);

    // The FNV-1a basis is non-zero, so a non-empty fold never collapses to 0.
    CHECK(h1 != 0u);
}

TEST(Determinism, SensitiveToSinglePersonFieldChange) {
    ResetWorld();
    std::uint64_t before = play::HashWorldState();

    // Flip exactly one byte/field of one person slot.
    sim::g_persons[7].marker = 4;   // was -1 (free)
    std::uint64_t after = play::HashWorldState();
    CHECK(before != after);

    // Restore that one field -> digest returns to the original.
    sim::g_persons[7].marker = -1;
    std::uint64_t restored = play::HashWorldState();
    CHECK_EQ(before, restored);
}

TEST(Determinism, SensitiveToRngStateChange) {
    ResetWorld();
    std::uint64_t a = play::HashWorldState();

    // Advancing/changing the RNG state alone must move the digest.
    crt::Srand(12345);
    std::uint64_t b = play::HashWorldState();
    CHECK(a != b);

    // Re-seeding back recovers the digest (state is the only difference).
    crt::Srand(1);
    std::uint64_t c = play::HashWorldState();
    CHECK_EQ(a, c);
}

TEST(Determinism, SensitiveToObjectAndSceneChanges) {
    ResetWorld();
    std::uint64_t base = play::HashWorldState();

    sim::g_objects[3].alive = 1;
    CHECK(play::HashWorldState() != base);
    sim::g_objects[3].alive = 0;
    CHECK_EQ(play::HashWorldState(), base);

    sim::g_sceneNodeCount = 5;
    CHECK(play::HashWorldState() != base);
    sim::g_sceneNodeCount = 0;
    CHECK_EQ(play::HashWorldState(), base);

    sim::g_buildingPersons[2].kind = 15;
    CHECK(play::HashWorldState() != base);
    sim::g_buildingPersons[2].kind = 0;
    CHECK_EQ(play::HashWorldState(), base);
}

TEST(Determinism, SnapshotCompareLocalizesDivergence) {
    ResetWorld();
    play::WorldSnapshot a = play::SnapshotWorld();

    // Identical state -> compare succeeds, no diff text.
    play::WorldSnapshot a2 = play::SnapshotWorld();
    std::string diff;
    CHECK(play::CompareSnapshots(a, a2, &diff));
    CHECK(diff.empty());

    // Mutate one region; compare must fail and name that region.
    sim::g_objects[10].id = 0x1234;
    play::WorldSnapshot b = play::SnapshotWorld();
    diff.clear();
    CHECK(!play::CompareSnapshots(a, b, &diff));
    CHECK(diff.find("g_objects") != std::string::npos);
    // Untouched regions must NOT appear in the diff.
    CHECK(diff.find("g_persons'") == std::string::npos);

    // Both snapshots carry the full fixed region list.
    CHECK(a.regions.size() == b.regions.size());
    CHECK(a.regions.size() >= 9u);

    sim::g_objects[10].id = 0;  // restore for hygiene
}

TEST(Determinism, RegionSubHashesSumToSameOrder) {
    ResetWorld();
    play::WorldSnapshot s = play::SnapshotWorld();
    // Region fold order is fixed; first region is always g_persons, RNG last.
    CHECK(s.regions.front().label == std::string("g_persons"));
    CHECK(s.regions.back().label == std::string("crt_rng_state"));
}

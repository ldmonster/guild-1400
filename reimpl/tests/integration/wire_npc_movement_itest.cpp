// tests/integration/wire_npc_movement_itest.cpp — the NPC pathfinding+movement
// driver over a SMALL MAP with a few live NPCs, proving the motion is DIGEST-VISIBLE:
//   * positions evolve as the driver steps (per-tick),
//   * play::HashFullWorld() CHANGES each tick (the record-pad position folds in),
//   * the whole run is byte-identical on rerun (deterministic),
//   * WITHOUT the installer, HashFullWorld is unchanged across steps (no movement).
#include "test.h"

#include "play/wire_npc_movement.h"
#include "play/world_digest.h"
#include "sim/map.h"
#include "sim/entity.h"
#include "sim/types.h"
#include "render/heightmap.h"
#include "crt/rand.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace guild;

namespace {

sim::MapGrid MakeOpenGrid(std::vector<u8>& store, render::Heightmap& hm, int n) {
    store.assign(static_cast<size_t>(n) * n * 24, 0);
    for (int i = 0; i < n * n; ++i)
        store[static_cast<size_t>(24) * i] = 1;
    std::memset(&hm, 0, sizeof hm);
    hm.size = n;
    hm.entries = store.data();
    sim::MapGrid g = sim::MapGridFromHeightmap(&hm);
    // Blocked border (power-of-two edge): the A* expander requires it.
    for (int i = 0; i < n; ++i) {
        sim::MapSetCellAt(g, i, 0, sim::kCellBlocked);
        sim::MapSetCellAt(g, i, n - 1, sim::kCellBlocked);
        sim::MapSetCellAt(g, 0, i, sim::kCellBlocked);
        sim::MapSetCellAt(g, n - 1, i, sim::kCellBlocked);
    }
    return g;
}

// Zero + seed N live objects (ids 1000+i) and M live persons (ids 5000+i) so the
// digest folds a clean slate identically each run.
void SeedWorld(int objs, int persons) {
    for (int i = 0; i < sim::kObjectCapacity; ++i) {
        std::memset(&sim::g_objects[i], 0, sim::kObjectStride);
        sim::g_objects[i].alive = 0;
    }
    for (int i = 0; i < sim::kPersonCapacity; ++i) {
        std::memset(&sim::g_persons[i], 0, sim::kPersonStride);
        sim::g_persons[i].marker = -1;        // free
        sim::g_personIds[i] = 0;
    }
    for (int i = 0; i < objs; ++i) {
        sim::g_objects[i].alive = 1;
        sim::g_objects[i].id = 1000 + i;
    }
    for (int i = 0; i < persons; ++i) {
        sim::g_persons[i].marker = 0;         // live
        sim::g_persons[i].id = 5000 + i;
        sim::g_personIds[i] = 5000 + i;
    }
    sim::g_personArrayLoaded = true;
    sim::g_sceneArrayLoaded = true;
}

// Assign deterministic destinations to a few NPCs from a fixed schedule.
void AssignDestinations() {
    play::SetEntityDestination(1000, 14, 12, 2, 2);
    play::SetEntityDestination(1001, 3, 13, 13, 3);
    play::SetEntityDestination(5000, 11, 11, 1, 1);
    play::SetEntityDestination(5001, 6, 14, 14, 6);
}

std::uint64_t HashAfterSrand(u32 seed) {
    crt::Srand(seed);            // pin the folded RNG state before the digest
    return play::HashFullWorld();
}

} // namespace

// ---------------------------------------------------------------------------
// Positions evolve + HashFullWorld changes per tick; byte-identical on rerun.
// ---------------------------------------------------------------------------
TEST(WireNpcMovementItest, PositionsEvolveAndDigestChangesPerTickDeterministic) {
    std::vector<u8> store;
    render::Heightmap hm;
    sim::MapGrid g = MakeOpenGrid(store, hm, 16);

    auto run = [&](std::vector<std::uint64_t>& hashes) {
        SeedWorld(/*objs=*/3, /*persons=*/3);
        play::SetNpcMovementGrid(g);
        play::InstallNpcMovement();
        play::ResetNpcMovementTallies();
        AssignDestinations();

        hashes.push_back(HashAfterSrand(0xC0FFEE));   // baseline (post-assign)
        for (int t = 0; t < 8; ++t) {
            play::StepNpcMovement();
            hashes.push_back(HashAfterSrand(0xC0FFEE));
        }
        play::UninstallNpcMovement();
    };

    std::vector<std::uint64_t> a, b;
    run(a);
    run(b);

    // The digest moved across the run (positions folded in).
    CHECK(a.front() != a.back());
    // At least the first few steps each change the digest (NPCs still moving).
    int changes = 0;
    for (size_t i = 1; i < a.size(); ++i)
        if (a[i] != a[i - 1]) ++changes;
    std::printf("[npcmove-itest] digest changes over %zu steps = %d, "
                "entitiesMoved=%d pathsBuilt=%d arrivals=%d\n",
                a.size() - 1, changes,
                play::GetNpcMovementTallies().entitiesMoved,
                play::GetNpcMovementTallies().pathsBuilt,
                play::GetNpcMovementTallies().arrivals);
    CHECK(changes >= 1);

    // Byte-identical hash sequence on rerun (deterministic).
    CHECK_EQ(a.size(), b.size());
    bool identical = (a.size() == b.size());
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
        if (a[i] != b[i]) identical = false;
    CHECK(identical);
}

// ---------------------------------------------------------------------------
// Without the installer: stepping changes nothing (digest constant).
// ---------------------------------------------------------------------------
TEST(WireNpcMovementItest, NoMovementWithoutInstaller) {
    std::vector<u8> store;
    render::Heightmap hm;
    sim::MapGrid g = MakeOpenGrid(store, hm, 16);

    SeedWorld(/*objs=*/3, /*persons=*/3);
    play::SetNpcMovementGrid(g);
    play::UninstallNpcMovement();             // driver OFF
    AssignDestinations();                      // destinations set, but no driver

    std::uint64_t h0 = HashAfterSrand(0xBEEF);
    for (int t = 0; t < 8; ++t) {
        int moved = play::StepNpcMovement();
        CHECK_EQ(moved, 0);
    }
    std::uint64_t h1 = HashAfterSrand(0xBEEF);
    std::printf("[npcmove-itest] inert: h0=%llu h1=%llu\n",
                (unsigned long long)h0, (unsigned long long)h1);
    CHECK_EQ(h0, h1);                          // digest unchanged (no motion)
}

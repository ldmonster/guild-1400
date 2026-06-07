// tests/unit/wire_npc_movement_test.cpp — the NPC pathfinding+movement driver on a
// SYNTHETIC world (no assets). Proves the REAL per-tick path-follow:
//   * a synthetic entity with a destination advances toward it along the REAL A*
//     path one waypoint per tick (monotonic Manhattan-distance decrease),
//   * the motion is deterministic (same setup -> identical tile sequence on rerun),
//   * INERT DEFAULT (not installed) => no movement (current behavior).
#include "test.h"

#include "play/wire_npc_movement.h"
#include "sim/map.h"
#include "sim/entity.h"
#include "sim/types.h"
#include "render/heightmap.h"

#include <cstdio>
#include <cstring>
#include <vector>
#include <cstdlib>

using namespace guild;

namespace {

// Build an N x N walkable grid (type byte 1) view over a heightmap, with a BLOCKED
// border (type 13). The A* node index packs as (row<<log2(size))+col, so the grid
// edge must be a power of two AND the border must be impassable (the expander
// relies on it to avoid walking off the buffer) — exactly as sim_path_test sets up.
sim::MapGrid MakeOpenGrid(std::vector<u8>& store, render::Heightmap& hm, int n) {
    store.assign(static_cast<size_t>(n) * n * 24, 0);
    for (int i = 0; i < n * n; ++i)
        store[static_cast<size_t>(24) * i] = 1;   // walkable terrain type
    std::memset(&hm, 0, sizeof hm);
    hm.size = n;
    hm.entries = store.data();
    sim::MapGrid g = sim::MapGridFromHeightmap(&hm);
    for (int i = 0; i < n; ++i) {
        sim::MapSetCellAt(g, i, 0, sim::kCellBlocked);
        sim::MapSetCellAt(g, i, n - 1, sim::kCellBlocked);
        sim::MapSetCellAt(g, 0, i, sim::kCellBlocked);
        sim::MapSetCellAt(g, n - 1, i, sim::kCellBlocked);
    }
    return g;
}

// Seed a single live object id at index 0 in the global object array.
void SeedOneObject(i32 id) {
    for (int i = 0; i < sim::kObjectCapacity; ++i) {
        std::memset(&sim::g_objects[i], 0, sim::kObjectStride);
        sim::g_objects[i].alive = 0;
    }
    sim::g_objects[0].alive = 1;
    sim::g_objects[0].id = id;
    sim::g_personArrayLoaded = true;
}

int Manhattan(int ax, int az, int bx, int bz) {
    return std::abs(ax - bx) + std::abs(az - bz);
}

} // namespace

// ---------------------------------------------------------------------------
// A destination-bound entity advances one waypoint/tick toward its goal.
// ---------------------------------------------------------------------------
TEST(WireNpcMovementUnit, EntityAdvancesAlongRealPathMonotonically) {
    std::vector<u8> store;
    render::Heightmap hm;
    sim::MapGrid g = MakeOpenGrid(store, hm, 16);

    SeedOneObject(/*id=*/4242);

    play::SetNpcMovementGrid(g);
    play::InstallNpcMovement();
    play::ResetNpcMovementTallies();

    // Start at (2,2), destination (12,9): a clear diagonal/orthogonal route.
    const int sx = 2, sz = 2, dx = 12, dz = 9;
    bool bound = play::SetEntityDestination(4242, dx, dz, sx, sz);
    CHECK(bound);

    play::NpcMovePos p0 = play::GetEntityMovePos(4242);
    CHECK_EQ(p0.curX, sx);
    CHECK_EQ(p0.curZ, sz);
    CHECK(p0.active);

    int prevDist = Manhattan(sx, sz, dx, dz);
    int ticks = 0;
    int lastX = sx, lastZ = sz;
    // Step until arrival (cap to avoid runaway).
    for (int t = 0; t < 64; ++t) {
        int moved = play::StepNpcMovement();
        play::NpcMovePos p = play::GetEntityMovePos(4242);
        if (!p.active && p.curX == dx && p.curZ == dz) { ++ticks; break; }
        if (moved == 0) break;
        int d = Manhattan(p.curX, p.curZ, dx, dz);
        // Each tick: distance strictly decreases (advancing toward goal) and the
        // step is 8-connected (adjacent tile) — the WalkStep one-waypoint advance.
        CHECK(d < prevDist);
        int sdx = std::abs(p.curX - lastX), sdz = std::abs(p.curZ - lastZ);
        CHECK(sdx <= 1 && sdz <= 1 && (sdx || sdz));
        prevDist = d;
        lastX = p.curX; lastZ = p.curZ;
        ++ticks;
    }
    play::NpcMovePos pf = play::GetEntityMovePos(4242);
    std::printf("[npcmove-unit] arrived at (%d,%d) dest (%d,%d) in %d ticks, "
                "lastPathLen=%d active=%d\n",
                pf.curX, pf.curZ, dx, dz, ticks,
                play::GetNpcMovementTallies().lastPathLen, (int)pf.active);
    CHECK_EQ(pf.curX, dx);
    CHECK_EQ(pf.curZ, dz);
    CHECK(!pf.active);                 // reached -> active flag cleared (arrival)
    CHECK(play::GetNpcMovementTallies().arrivals >= 1);

    play::UninstallNpcMovement();
}

// ---------------------------------------------------------------------------
// Determinism: identical setup -> identical tile sequence on rerun.
// ---------------------------------------------------------------------------
TEST(WireNpcMovementUnit, MotionIsDeterministicAcrossRuns) {
    std::vector<u8> store;
    render::Heightmap hm;
    sim::MapGrid g = MakeOpenGrid(store, hm, 16);

    auto run = [&](std::vector<std::pair<int,int>>& trace) {
        SeedOneObject(/*id=*/77);
        play::SetNpcMovementGrid(g);
        play::InstallNpcMovement();
        play::SetEntityDestination(77, 13, 11, 1, 1);
        for (int t = 0; t < 64; ++t) {
            play::StepNpcMovement();
            play::NpcMovePos p = play::GetEntityMovePos(77);
            trace.emplace_back(p.curX, p.curZ);
            if (!p.active) break;
        }
        play::UninstallNpcMovement();
    };

    std::vector<std::pair<int,int>> a, b;
    run(a);
    run(b);
    CHECK_EQ(a.size(), b.size());
    bool identical = (a.size() == b.size());
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
        if (a[i] != b[i]) identical = false;
    CHECK(identical);
    std::printf("[npcmove-unit] deterministic trace len=%zu identical=%d\n",
                a.size(), (int)identical);
}

// ---------------------------------------------------------------------------
// Inert default: with the driver NOT installed, nothing moves.
// ---------------------------------------------------------------------------
TEST(WireNpcMovementUnit, InertDefaultDoesNotMove) {
    std::vector<u8> store;
    render::Heightmap hm;
    sim::MapGrid g = MakeOpenGrid(store, hm, 16);

    SeedOneObject(/*id=*/9);
    play::UninstallNpcMovement();            // explicitly NOT installed
    CHECK(!play::NpcMovementInstalled());
    play::SetNpcMovementGrid(g);
    play::SetEntityDestination(9, 10, 10, 2, 2);

    int moved = play::StepNpcMovement();     // driver off -> no-op
    CHECK_EQ(moved, 0);
    play::NpcMovePos p = play::GetEntityMovePos(9);
    CHECK_EQ(p.curX, 2);                      // unchanged
    CHECK_EQ(p.curZ, 2);
    std::printf("[npcmove-unit] inert: moved=%d pos=(%d,%d)\n", moved, p.curX, p.curZ);
}

// ---------------------------------------------------------------------------
// No grid bound: installed but no grid -> safe no-op.
// ---------------------------------------------------------------------------
TEST(WireNpcMovementUnit, NoGridIsSafeNoOp) {
    SeedOneObject(/*id=*/3);
    play::InstallNpcMovement();
    play::SetNpcMovementGrid(sim::MapGrid{0, nullptr});
    play::SetEntityDestination(3, 5, 5, 1, 1);
    int moved = play::StepNpcMovement();
    CHECK_EQ(moved, 0);
    play::UninstallNpcMovement();
}

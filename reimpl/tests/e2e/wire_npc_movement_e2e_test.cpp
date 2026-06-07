// tests/e2e/wire_npc_movement_e2e_test.cpp — GUARDED real-asset NPC MOVEMENT on
// AUGSBURG. Loads the real city into the live arrays, picks a REAL person, assigns
// a destination, and drives the REAL per-tick path-follow (sim::PathBuildWaypointList
// = VIBE_Path_BuildWaypointList over the bidirectional A* VIBE_Path_FindRoute; the
// per-tick advance = VIBE_CharAction_WalkStep). Proves the real person's recorded
// position EVOLVES over a few "days" of steps and is byte-identical on rerun.
//
// NOTE (documented stand-in): the engine's walkable grid is the loaded terrain
// heightmap; the destination<->person binding flows through the NPC daily director's
// scene-coupled building-tile resolution (no standalone reconstructed leaf — see
// wire_npc_actions.h / wire_npc_movement.h). This e2e drives the REAL path-follow
// over an open walkable grid sized to the city and an explicit SetEntityDestination
// on a real person; the A* path-follow and the real person record are the genuine
// reconstructed pieces under test.
//
// Skips cleanly when AUGSBURG.cty is absent (honors GUILD_GAME_DIR).
#include "test.h"

#include "play/wire_npc_movement.h"
#include "play/world_digest.h"
#include "app/real_boot.h"
#include "io/save_world_load.h"
#include "io/vfs.h"
#include "shim_impl/disk_filesystem.h"
#include "sim/map.h"
#include "sim/entity.h"
#include "sim/types.h"
#include "render/heightmap.h"
#include "crt/rand.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Gilde.INI") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty");
}

void ZeroLiveArrays() {
    for (int i = 0; i < sim::kObjectCapacity; ++i) {
        std::memset(&sim::g_objects[i], 0, sim::kObjectStride);
        sim::g_objects[i].alive = 0;
    }
    for (int i = 0; i < sim::kPersonCapacity; ++i) {
        std::memset(&sim::g_persons[i], 0, sim::kPersonStride);
        sim::g_persons[i].marker = -1;
        sim::g_personIds[i] = 0;
    }
}

// Find the first live real person id in g_persons (marker != -1, id != 0).
i32 FirstLivePersonId() {
    for (int i = 0; i < sim::kPersonCapacity; ++i)
        if (sim::g_persons[i].marker != -1 && sim::g_personIds[i] != 0)
            return sim::g_personIds[i];
    return 0;
}

} // namespace

TEST(WireNpcMovementE2E, RealPersonPositionEvolvesDeterministically) {
    if (!RealAssetsPresent()) {
        std::printf("[npcmove-e2e] AUGSBURG assets absent — skipping cleanly\n");
        CHECK(true);
        return;
    }

    const int kGridN = 64;             // walkable grid edge (open; see file header)

    // Build the open walkable grid once (backing buffer must outlive the steps).
    std::vector<u8> store(static_cast<size_t>(kGridN) * kGridN * 24, 0);
    for (int i = 0; i < kGridN * kGridN; ++i)
        store[static_cast<size_t>(24) * i] = 1;
    render::Heightmap hm;
    std::memset(&hm, 0, sizeof hm);
    hm.size = kGridN;
    hm.entries = store.data();
    sim::MapGrid grid = sim::MapGridFromHeightmap(&hm);
    for (int i = 0; i < kGridN; ++i) {  // blocked border (A* requirement)
        sim::MapSetCellAt(grid, i, 0, sim::kCellBlocked);
        sim::MapSetCellAt(grid, i, kGridN - 1, sim::kCellBlocked);
        sim::MapSetCellAt(grid, 0, i, sim::kCellBlocked);
        sim::MapSetCellAt(grid, kGridN - 1, i, sim::kCellBlocked);
    }

    // One full run: load AUGSBURG, pick a real person, step a few "days".
    auto run = [&](std::vector<std::pair<int,int>>& trace,
                   std::vector<std::uint64_t>& hashes, i32* outPid,
                   int* outPathLen) {
        shim::DiskFileSystem fs(GameDir());
        app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI", {},
                                 /*caseInsensitive=*/false);
        ZeroLiveArrays();
        io::WorldState world;
        std::string cityPath = app::RealCityPath("Augsburg");
        bool loaded = io::LoadWorld(cityPath.c_str(), world);
        CHECK(loaded);

        i32 pid = FirstLivePersonId();
        *outPid = pid;
        CHECK(pid != 0);

        play::SetNpcMovementGrid(grid);
        play::InstallNpcMovement();
        play::ResetNpcMovementTallies();

        // Assign a destination on the REAL person: start near one corner, head to
        // the opposite. Deterministic (pure function of the grid size).
        const int sx = 4, sz = 4, dx = kGridN - 6, dz = kGridN - 8;
        bool bound = play::SetEntityDestination(pid, dx, dz, sx, sz);
        CHECK(bound);

        crt::Srand(0xA065B);
        hashes.push_back(play::HashFullWorld());
        play::NpcMovePos p0 = play::GetEntityMovePos(pid);
        trace.emplace_back(p0.curX, p0.curZ);

        // Step a few "days" (each day advances one path waypoint).
        for (int d = 0; d < 6; ++d) {
            play::StepNpcMovement();
            play::NpcMovePos p = play::GetEntityMovePos(pid);
            trace.emplace_back(p.curX, p.curZ);
            crt::Srand(0xA065B);
            hashes.push_back(play::HashFullWorld());
        }
        *outPathLen = play::GetNpcMovementTallies().lastPathLen;

        play::UninstallNpcMovement();
        io::VfsShutdown();
    };

    std::vector<std::pair<int,int>> ta, tb;
    std::vector<std::uint64_t> ha, hb;
    i32 pidA = 0, pidB = 0;
    int plA = 0, plB = 0;
    run(ta, ha, &pidA, &plA);
    run(tb, hb, &pidB, &plB);

    std::printf("[npcmove-e2e] real person id=%d, path len=%d, positions:",
                pidA, plA);
    for (auto& p : ta) std::printf(" (%d,%d)", p.first, p.second);
    std::printf("\n");

    // The real person's recorded position EVOLVED across the days.
    CHECK(ta.size() >= 2);
    bool evolved = false;
    for (size_t i = 1; i < ta.size(); ++i)
        if (ta[i] != ta[0]) { evolved = true; break; }
    CHECK(evolved);

    // The digest reflected the motion (changed across the run).
    CHECK(ha.front() != ha.back());

    // Deterministic on rerun (same person, same tile trace, same hash sequence).
    CHECK_EQ(pidA, pidB);
    CHECK_EQ(ta.size(), tb.size());
    CHECK_EQ(ha.size(), hb.size());
    bool sameTrace = (ta.size() == tb.size());
    for (size_t i = 0; i < ta.size() && i < tb.size(); ++i)
        if (ta[i] != tb[i]) sameTrace = false;
    bool sameHash = (ha.size() == hb.size());
    for (size_t i = 0; i < ha.size() && i < hb.size(); ++i)
        if (ha[i] != hb[i]) sameHash = false;
    CHECK(sameTrace);
    CHECK(sameHash);
    std::printf("[npcmove-e2e] evolved=%d sameTrace=%d sameHash=%d\n",
                (int)evolved, (int)sameTrace, (int)sameHash);
}

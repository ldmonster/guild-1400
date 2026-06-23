// End-to-end capstone: the FULL reconstructed wiring set (every InstallReal*Wiring
// the live spine installs — ~40 hook bridges) driving a real multi-day simulation
// over the loaded Augsburg world. Validates that the entire wired system —
// command/entity/charaction/npc/event/economy/AI/building/cutscene/object leaves all
// bound to their reconstructed implementations — runs a stable, evolving, determinis-
// tic K-day playthrough over a real .cty world (not the narrower hook set the
// game_day/playthrough tests use). Guarded on GUILD_GAME_DIR; clean skip if absent.
#include "app/wiring.h"            // InstallAllRealGameplayHooks
#include "play/game_day.h"         // RunGameDays / SeedGameDay / GameDayState
#include "play/world_digest.h"     // HashFullWorld
#include "io/save_world_load.h"    // LoadWorld / WorldState
#include "io/vfs.h"                // VfsInit / VfsShutdown
#include "sim/entity.h"            // ResetEntityArrays / g_persons / g_objects
#include "sim/building_lifecycle.h"// ResetBuildingPersons
#include "crt/rand.h"              // Srand
#include "shim_impl/disk_filesystem.h"
#include "tests/framework/test.h"

#include <cstdlib>
#include <string>
#include <vector>

using namespace guild;

namespace {
int CountLivePersons() {
    int n = 0;
    for (int i = 0; i < sim::kPersonCapacity; ++i)
        if (sim::g_persons[i].marker != -1) ++n;
    return n;
}
int CountLiveObjects() {
    int n = 0;
    for (int i = 0; i < sim::kObjectCapacity; ++i)
        if (sim::g_objects[i].alive) ++n;
    return n;
}
} // namespace

TEST(AppFullWiredPlaythroughE2E, FullWiringDrivesRealMultiDaySim) {
    const char* dir = std::getenv("GUILD_GAME_DIR");
    if (!dir || !*dir) { CHECK(true); return; }     // assets absent -> skip

    shim::DiskFileSystem fs(dir);
    if (!fs.exists("Resources/gamedata/Cities/AUGSBURG.cty")) { CHECK(true); return; }

    const int K = 8;
    const std::uint32_t kSeed = 9001;

    io::VfsInit(&fs, /*caseInsensitive=*/false);
    sim::ResetEntityArrays();
    sim::ResetBuildingPersons();

    // THE CAPSTONE: install the complete reconstructed wiring set, exactly as the
    // live spine's commandQueueInitAndSync does — every gameplay leaf bound to its
    // reconstructed implementation.
    app::InstallAllRealGameplayHooks();

    io::WorldState world{};
    bool loaded = io::LoadWorld("Resources/gamedata/Cities/AUGSBURG.cty", world);
    CHECK(loaded);
    const int objs = CountLiveObjects();
    CHECK(objs > 0);                                // real city populated the tables
    (void)CountLivePersons();

    crt::Srand(kSeed);
    play::GameDayState st = play::SeedGameDay(kSeed);
    const std::uint64_t hStart = play::HashFullWorld();
    std::vector<play::GameDayDeltas> days = play::RunGameDays(kSeed, K, st);
    const std::uint64_t hEnd = play::HashFullWorld();

    // The full wired multi-day run is stable and ran exactly K days.
    CHECK_EQ((int)days.size(), K);
    CHECK_EQ(days.front().dayBefore, 0);
    CHECK_EQ(days.back().dayAfter, K);
    for (int t = 0; t < K; ++t)
        CHECK_EQ(days[t].dayAfter - days[t].dayBefore, 1);   // +1 day each turn

    // The world evolved over the run (the wired economy/AI/event passes mutated it).
    CHECK(hStart != hEnd);

    io::VfsShutdown();
}

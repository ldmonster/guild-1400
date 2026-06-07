// tests/e2e/wire_npc_actions_e2e_test.cpp — GUARDED real-asset e2e for the REAL
// NPC daily-schedule / action-dispatch bridge (play::wire_npc_actions) on AUGSBURG.
//
//   mount real assets -> io::LoadWorld AUGSBURG into the live g_persons/g_objects
//   -> InstallRealNpcActions() -> seed the loaded person record into a live
//   production worker (home/dest/activeB) -> run a few daily-director steps ->
//   assert the REAL NPC state EVOLVED (HashFullWorld changed from the post-load
//   hash; the +456 turn-bits column gained the dispatch bit) AND is byte-identical
//   across a full rerun (deterministic).
//
// Skips cleanly when the shipped AUGSBURG.cty asset is absent (honors GUILD_GAME_DIR).
#include "test.h"

#include "play/wire_npc_actions.h"
#include "play/world_digest.h"
#include "sim/npc_daily.h"
#include "sim/npcaction.h"
#include "sim/entity.h"
#include "sim/he.h"
#include "sim/types.h"
#include "app/real_boot.h"
#include "io/save_world_load.h"
#include "io/vfs.h"
#include "shim_impl/disk_filesystem.h"
#include "crt/rand.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace guild;
using namespace guild::play;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR")) return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}
bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Gilde.INI") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty");
}

template <typename T>
void PutCol(int i, int off, T v) {
    std::memcpy(reinterpret_cast<u8*>(&sim::g_persons[i]) + off, &v, sizeof(T));
}
template <typename T>
T GetCol(int i, int off) {
    T v; std::memcpy(&v, reinterpret_cast<u8*>(&sim::g_persons[i]) + off, sizeof(T));
    return v;
}

int PvFind(int i, i32* u, i32* o) { *u = 100 + i; *o = 200 + i; return 1; }
bool PvDoor(int, i32* a, i32* b) { *a = 1; *b = 1; return true; }

// One full pass: load AUGSBURG, wire the bridge, seed the loaded person 0 into a
// live production worker, run 3 director steps. Returns the post-load and post-run
// world hashes + the worker's final turn-bits.
struct Pass {
    bool loaded = false;
    u32  cityRecCount = 0;
    std::uint64_t hashAfterLoad = 0;
    std::uint64_t hashAfterRun  = 0;
    u32  finalTurnBits = 0;
    int  commandsBuilt = 0;
};

Pass RunPass(std::uint32_t seed) {
    Pass r;
    shim::DiskFileSystem fs(GameDir());

    app::RealGameAssets assets =
        app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI", {}, false);
    if (!assets.vfsBound) { io::VfsShutdown(); return r; }

    sim::ResetEntityArrays();
    io::WorldState world{};
    r.loaded = io::LoadWorld(app::RealCityPath("Augsburg").c_str(), world);
    if (!r.loaded) { io::VfsShutdown(); return r; }
    r.cityRecCount = world.cityRecCount;

    // Post-load baseline hash (pin RNG: HashFullWorld folds the live CRT state).
    crt::Srand(seed);
    r.hashAfterLoad = HashFullWorld();

    // Wire the real NPC daily-schedule dispatch.
    NpcActionsTargetProvider prov{};
    prov.findTarget = PvFind; prov.destDoorIds = PvDoor;
    SetNpcActionsTargetProvider(&prov);
    ResetNpcActionsQueue();
    ResetNpcActionsTallies();
    InstallRealNpcActions();

    // Seed the loaded person slot 0 into a live morning production worker so the
    // director WILL dispatch over the real array (AUGSBURG ships a single seed
    // person; we promote it to a worker to exercise the real schedule path).
    PutCol<i16>(0, 0x00, 0);             // live marker
    PutCol<u8>(0, 357, 1);              // activeB
    PutCol<i32>(0, 364, 1000);         // homeBld id
    PutCol<i32>(0, 388, 55);           // destBld id
    PutCol<u32>(0, 456, 0u);           // turnBits cleared
    if (!GetCol<i32>(0, 4)) PutCol<i32>(0, 4, 9001);  // ensure an id
    // A production home building in object slot 0.
    sim::g_objects[0].alive = 11;
    sim::g_objects[0].id    = 1000;

    crt::Srand(seed);
    sim::SetNpcClock(sim::GameTime{ 0, 6, 0, 0 });   // morning, hour 6 < workStart
    for (int t = 0; t < 3; ++t) {
        sim::HeRecord rec{};
        sim::He_State(&rec) = 0;
        sim::NpcDaily_DailyRoutineStep(&rec);
    }
    r.finalTurnBits = GetCol<u32>(0, 456);
    r.commandsBuilt = (int)NpcActionsQueue().send_count();

    // Post-run hash (pin RNG again before the compare).
    crt::Srand(seed);
    r.hashAfterRun = HashFullWorld();

    UninstallRealNpcActions();
    SetNpcActionsTargetProvider(nullptr);
    io::VfsShutdown();
    return r;
}

} // namespace

TEST(WireNpcActionsE2E, AugsburgNpcScheduleEvolvesDeterministic) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] WireNpcActionsE2E: real game dir absent (%s)\n",
                    GameDir().c_str());
        CHECK(true);
        return;
    }
    std::printf("[npc-e2e] asset dir: %s\n", GameDir().c_str());

    Pass a = RunPass(0xA065B);
    CHECK(a.loaded);
    if (!a.loaded) { io::VfsShutdown(); return; }

    std::printf("[npc-e2e] cityRecCount=%u | hashAfterLoad=%llu hashAfterRun=%llu "
                "| finalTurnBits=0x%X commandsBuilt=%d\n",
                a.cityRecCount,
                (unsigned long long)a.hashAfterLoad,
                (unsigned long long)a.hashAfterRun,
                a.finalTurnBits, a.commandsBuilt);

    // The real schedule dispatched the worker: turn-bits gained the dispatch bit,
    // real command packets were built, and the world hash MOVED off the post-load
    // baseline (the NPC state evolved).
    CHECK((a.finalTurnBits & sim::kDailyDispWork) != 0);
    CHECK(a.commandsBuilt > 0);
    CHECK(a.hashAfterLoad != 0u);
    CHECK(a.hashAfterRun != a.hashAfterLoad);

    // Determinism: a full rerun reproduces identical hashes + identical NPC state.
    Pass b = RunPass(0xA065B);
    CHECK(b.loaded);
    std::printf("[npc-e2e] rerun hashAfterLoad=%llu hashAfterRun=%llu finalTurnBits=0x%X\n",
                (unsigned long long)b.hashAfterLoad,
                (unsigned long long)b.hashAfterRun, b.finalTurnBits);
    CHECK_EQ((long long)a.hashAfterLoad, (long long)b.hashAfterLoad);
    CHECK_EQ((long long)a.hashAfterRun,  (long long)b.hashAfterRun);
    CHECK_EQ((int)a.finalTurnBits, (int)b.finalTurnBits);
    CHECK_EQ(a.commandsBuilt, b.commandsBuilt);
}

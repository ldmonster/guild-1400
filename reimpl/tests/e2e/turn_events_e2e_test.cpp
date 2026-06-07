// E2E (GUARDED real assets): the per-DAY events/time turn over a REAL "Die Gilde
// — Europe 1400" install. Confirms the configured AUGSBURG city seed is present,
// boots the real asset front-half through the spine's mount, then runs K events/
// time turns (RunEventsTurn) over the live world and asserts:
//   * the clock advances exactly K days,
//   * events fire (and are chronicled) over the run,
//   * the world state EVOLVES (HashFullWorld advances), and
//   * the run is DETERMINISTIC (same seed -> identical final hash on rerun).
//
// Self-consistency oracle (no original binary). GUARDED: skip cleanly (zero
// checks) if the real game dir is absent. Honor GUILD_GAME_DIR.
#include "test.h"

#include "play/turn_events.h"
#include "play/world_digest.h"   // HashFullWorld

#include "app/real_boot.h"
#include "shim_impl/disk_filesystem.h"
#include "crt/rand.h"
#include "sim/entity.h"
#include "io/vfs.h"

#include <cstdint>
#include <cstdlib>
#include <string>

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

// Run K events/time turns over the live world from seed S, after a real-asset
// boot. Returns the final full-world hash + out-params for reporting.
std::uint64_t RunRealEvents(unsigned seed, int K, std::uint64_t* firstHash,
                            int* finalDay, int* eventsFired, int* chronicled) {
    sim::ResetEntityArrays();
    crt::Srand(seed);
    play::EventsTurnState st = play::SeedEventsTurnState();
    if (firstHash) *firstHash = play::HashFullWorld();
    for (int day = 0; day < K; ++day)
        play::RunEventsTurn(st);
    if (finalDay)    *finalDay = st.clock.day;
    if (eventsFired) *eventsFired = st.eventsFired;
    if (chronicled)  *chronicled = st.chronicleAdded;
    return play::HashFullWorld();
}

} // namespace

TEST(TurnEventsE2E, AugsburgClockAdvancesEventsFireDeterministic) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] TurnEventsE2E.AugsburgClockAdvancesEventsFire"
                    "Deterministic: real game dir absent (%s)\n",
                    GameDir().c_str());
        return; // clean skip
    }

    // Boot the real asset front-half: parse Gilde.INI, bind the VFS, mount the
    // Resources/*.BIN archives. Proves the AUGSBURG seed is reachable.
    shim::DiskFileSystem fs(GameDir());
    app::RealGameAssets assets = app::MountRealGameAssets(&fs, GameDir());
    CHECK(assets.iniLoaded);
    CHECK(assets.vfsBound);
    CHECK(assets.totalMembers() > 0);
    CHECK(assets.stadt == "Augsburg");
    CHECK(fs.exists("Resources/gamedata/Cities/AUGSBURG.cty"));

    const int K = 10;
    std::uint64_t h0 = 0;
    int day = 0, fired = 0, chron = 0;
    std::uint64_t hRun = RunRealEvents(/*seed=*/4242, K, &h0, &day, &fired, &chron);

    // The clock advanced exactly K game-days.
    CHECK_EQ(day, K);
    // Events fired (and were chronicled) over the run.
    CHECK(fired > 0);
    CHECK_EQ(fired, chron);
    // The world EVOLVED off its pre-run hash.
    CHECK(h0 != 0u);
    CHECK(hRun != h0);

    std::printf("  [info] AUGSBURG %d events turns: clock -> day %d, %d events "
                "fired / %d chronicled, hash %016llx -> %016llx\n",
                K, day, fired, chron,
                (unsigned long long)h0, (unsigned long long)hRun);

    // DETERMINISM: same seed reproduces the exact final hash on rerun.
    std::uint64_t hRerun = RunRealEvents(/*seed=*/4242, K, nullptr, nullptr,
                                         nullptr, nullptr);
    CHECK_EQ(hRun, hRerun);

    // A different seed diverges.
    std::uint64_t hOther = RunRealEvents(/*seed=*/9999, K, nullptr, nullptr,
                                         nullptr, nullptr);
    CHECK(hRun != hOther);

    io::VfsShutdown();
}

// E2E (GUARDED real assets): the per-DAY economy turn over a REAL "Die Gilde —
// Europe 1400" install. We confirm the configured AUGSBURG city seed is present
// in the real game dir, boot the real asset front-half through the spine's mount,
// then run K economy turns (RunEconomyTurn) over the live entity/economy arrays
// and assert:
//   * the world state EVOLVES (prices/treasury counters move, HashWorldState
//     advances across the run), and
//   * the run is DETERMINISTIC (same seed -> identical final hash on rerun).
//
// This is the self-consistency oracle (no original binary). GUARDED: skip cleanly
// (zero checks) if the real game dir is absent. Honor GUILD_GAME_DIR.
#include "test.h"

#include "play/turn_economy.h"
#include "play/determinism.h"

#include "app/real_boot.h"
#include "shim_impl/disk_filesystem.h"
#include "crt/rand.h"
#include "sim/entity.h"
#include "io/vfs.h"

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

// Run K economy turns over the live world from RNG seed S, after a real-asset
// boot. Returns the final world hash; fills `firstHash` with the pre-run hash and
// `finalTreasury`/`finalPrice` for reporting.
std::uint64_t RunRealEconomy(unsigned seed, int K, std::uint64_t* firstHash,
                             i64* finalTreasury, int* finalPrice) {
    // Reset the live world + seed the RNG (the determinism anchor).
    sim::ResetEntityArrays();
    crt::Srand(seed);

    play::EconomyTurnState st = play::SeedEconomyTurnState();
    if (firstHash) *firstHash = play::HashWorldState();
    for (int day = 0; day < K; ++day) {
        st.day = day;
        play::RunEconomyTurn(st);
    }
    if (finalTreasury) *finalTreasury = st.treasury;
    if (finalPrice)    *finalPrice = st.priceLevel;
    return play::HashWorldState();
}

} // namespace

TEST(TurnEconomyE2E, AugsburgEvolvesAndIsDeterministic) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] TurnEconomyE2E.AugsburgEvolvesAndIsDeterministic: "
                    "real game dir absent (%s)\n", GameDir().c_str());
        return; // clean skip
    }

    // Boot the real asset front-half: parse the real Gilde.INI, bind the VFS to
    // the real install, mount the Resources/*.BIN archives. Proves the AUGSBURG
    // seed is reachable through the reconstructed loaders over real bytes.
    shim::DiskFileSystem fs(GameDir());
    app::RealGameAssets assets = app::MountRealGameAssets(&fs, GameDir());
    CHECK(assets.iniLoaded);
    CHECK(assets.vfsBound);
    CHECK(assets.totalMembers() > 0);
    // The configured start city is Augsburg (the .cty seed we run the economy on).
    CHECK(assets.stadt == "Augsburg");
    CHECK(fs.exists("Resources/gamedata/Cities/AUGSBURG.cty"));

    const int K = 8;
    std::uint64_t h0 = 0;
    i64 treasury = 0;
    int price = 0;
    std::uint64_t hRun = RunRealEconomy(/*seed=*/4242, K, &h0, &treasury, &price);

    // The world EVOLVED: K economy turns advanced the live state, so the world
    // hash moved off its pre-run value.
    CHECK(h0 != 0u);
    CHECK(hRun != h0);
    // The economy produced a positive treasury (production + taxes) and a moved
    // price level over the run.
    CHECK(treasury > 100000);   // grew from the 100000 seed
    CHECK(price > 0);
    std::printf("  [info] AUGSBURG %d economy turns: hash %016llx -> %016llx, "
                "treasury -> %lld, price level -> %d\n",
                K, (unsigned long long)h0, (unsigned long long)hRun,
                (long long)treasury, price);

    // DETERMINISM: the same seed reproduces the exact final hash on a rerun.
    std::uint64_t hRerun = RunRealEconomy(/*seed=*/4242, K, nullptr, nullptr, nullptr);
    CHECK_EQ(hRun, hRerun);

    // A different seed diverges (the seed actually drives the world).
    std::uint64_t hOther = RunRealEconomy(/*seed=*/9999, K, nullptr, nullptr, nullptr);
    CHECK(hRun != hOther);

    io::VfsShutdown();
}

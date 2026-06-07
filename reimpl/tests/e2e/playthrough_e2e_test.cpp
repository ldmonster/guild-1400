// tests/e2e/playthrough_e2e_test.cpp — Wave 28 P7 seed: GUARDED real-asset multi-day
// SCRIPTED PLAYTHROUGH on AUGSBURG.
//
//   load AUGSBURG -> seat the records the script needs -> a ~10-day multi-slice
//   playthrough chaining market / council / church / crime actions, each fired on a
//   scheduled day, interleaved with the REAL per-day game cascade (RunGameDay) ->
//   assert it runs, the per-day witnesses EVOLVE, and the whole run is BYTE-IDENTICAL
//   on a rerun (the closest system-level self-consistency check without the Wine
//   oracle). Reports the real per-day witness table.
//
// Skips cleanly when the shipped AUGSBURG.cty asset is absent (honors GUILD_GAME_DIR).
#include "test.h"

#include "play/playthrough.h"
#include "io/vfs.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace guild;
using namespace guild::play;

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

// A ~10-day multi-slice script: a market trade, a council candidacy, a church
// donation, and two crimes, spread across 10 game-days.
PlaythroughScript MakeScript() {
    PlaythroughScript s;
    s.days = 10;

    PlaythroughAction market;
    market.onDay = 1; market.kind = PlaythroughActionKind::kMarket;
    market.actorId = 0; market.targetId = 90001; market.ware = 5; market.qty = 30;
    market.buy = false;
    s.actions.push_back(market);

    PlaythroughAction council;
    council.onDay = 2; council.kind = PlaythroughActionKind::kCouncil;
    council.actorId = 31337; council.officeType = 5; council.holderKey = 3;
    s.actions.push_back(council);

    PlaythroughAction church;
    church.onDay = 4; church.kind = PlaythroughActionKind::kChurch;
    church.actorId = 90010; church.targetId = 90011; church.amount = 750;
    s.actions.push_back(church);

    PlaythroughAction crime1;
    crime1.onDay = 6; crime1.kind = PlaythroughActionKind::kCrime;
    crime1.actorId = 5; crime1.targetId = 6; crime1.crimeType = 1;
    s.actions.push_back(crime1);

    PlaythroughAction crime2;
    crime2.onDay = 8; crime2.kind = PlaythroughActionKind::kCrime;
    crime2.actorId = 7; crime2.targetId = 8; crime2.crimeType = 4;
    s.actions.push_back(crime2);

    return s;
}

} // namespace

// ---------------------------------------------------------------------------
TEST(PlaythroughE2E, AugsburgTenDayPlaythroughEvolvesAndReproduces) {
    if (!RealAssetsPresent()) {
        std::printf("[playthrough-e2e] AUGSBURG assets absent — skipping (set GUILD_GAME_DIR)\n");
        CHECK(true);
        return;
    }

    const std::uint32_t seed = 0xA06B;
    PlaythroughScript s = MakeScript();

    shim::DiskFileSystem fs(GameDir());
    PlaythroughResult a = RunScriptedPlaythrough(&fs, GameDir(), "Augsburg", s, seed);

    std::printf("[playthrough-e2e] loaded=%d persons=%u objects=%u daysRun=%d actions=%d\n",
                (int)a.loaded, a.personCount, a.objectCount, a.daysRun, a.actionsApplied);

    CHECK(a.loaded);
    CHECK_EQ(a.daysRun, 10);
    CHECK_EQ((int)a.witnesses.size(), 10);
    CHECK(a.actionsApplied >= 4);   // most/all scripted actions fired
    CHECK(a.worldEvolved());

    // Report the real per-day witness table.
    std::printf("[playthrough-e2e] day | hash             | treasury | price | rank | crime | cityMoney\n");
    for (const auto& w : a.witnesses)
        std::printf("[playthrough-e2e]  %2d | %016llx | %8lld | %5d | %4d | %5d | %.2f\n",
                    w.day, (unsigned long long)w.hash, (long long)w.treasury,
                    w.price, w.officeRank, w.crimeCount, w.cityMoney);

    // Day-to-day evolution: several distinct day hashes (not a fixed point).
    int distinct = 0;
    std::uint64_t prev = a.hashAfterLoad;
    for (const auto& w : a.witnesses) {
        if (w.hash != prev) ++distinct;
        prev = w.hash;
    }
    std::printf("[playthrough-e2e] distinct day-to-day hash changes: %d / %d days\n",
                distinct, (int)a.witnesses.size());
    CHECK(distinct >= 5);

    // The chained slices left their marks: council seated -> rank reached 1;
    // crimes committed -> crime count rose by the end.
    CHECK_EQ(a.witnesses.back().officeRank, 1);
    CHECK(a.witnesses.back().crimeCount >= 1);

    // --- byte-identical re-run (fresh world, same script + seed) ---
    PlaythroughResult b = RunScriptedPlaythrough(&fs, GameDir(), "Augsburg", s, seed);
    CHECK_EQ(a.runSignature(), b.runSignature());
    CHECK_EQ(a.hashAfterLoad, b.hashAfterLoad);
    bool allEqual = (a.witnesses.size() == b.witnesses.size());
    for (size_t i = 0; allEqual && i < a.witnesses.size(); ++i)
        if (a.witnesses[i].hash != b.witnesses[i].hash) allEqual = false;
    std::printf("[playthrough-e2e] rerun byte-identical: %d (sig %016llx == %016llx)\n",
                (int)(a.runSignature() == b.runSignature()),
                (unsigned long long)a.runSignature(),
                (unsigned long long)b.runSignature());
    CHECK(allEqual);

    // --- seed divergence ---
    PlaythroughResult c = RunScriptedPlaythrough(&fs, GameDir(), "Augsburg", s, seed ^ 0x1234);
    CHECK(c.runSignature() != a.runSignature());

    io::VfsShutdown();
}

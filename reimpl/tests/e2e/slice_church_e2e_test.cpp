// tests/e2e/slice_church_e2e_test.cpp — GUARDED real-asset CHURCH/RELIGION
// (donation) slice on AUGSBURG. The full end-to-end proof:
//
//   load AUGSBURG -> seat a donor + a church object in g_objects -> click
//   "Spenden" (donate) -> REAL opcode-15 command (EnqueueCmd15 0x494604) through
//   the REAL sim::CommandQueue codec -> REAL apply (ExRemapObjectPair 0x496978)
//   moving currency donor->church in the folded object money fields -> advance one
//   real game-day -> assert the donation moved (real before->after) AND the whole
//   run is byte-identical on rerun.
//
// Skips cleanly when the shipped AUGSBURG.cty asset is absent (honors GUILD_GAME_DIR).
#include "test.h"

#include "play/slice_church.h"
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

ChurchInteraction AugsburgDonation() {
    ChurchInteraction it;
    it.action       = ChurchAction::kDonate;
    it.churchId     = 424242;   // the recipient church object id
    it.donorAccount = 313373;   // the donor (clicking character's account)
    it.amount       = 1500;     // the wealth-scaled donation
    it.currencyType = 0;
    return it;
}

} // namespace

// ---------------------------------------------------------------------------
// The full donation slice on AUGSBURG: real before->after + deterministic rerun.
// ---------------------------------------------------------------------------
TEST(SliceChurchE2E, AugsburgDonationEvolvesWorldDeterministically) {
    if (!RealAssetsPresent()) {
        std::printf("[church-e2e] AUGSBURG assets absent — skipping (set GUILD_GAME_DIR)\n");
        CHECK(true);
        return;
    }

    const std::uint32_t seed = 0x5EA7;
    ChurchInteraction it = AugsburgDonation();

    shim::DiskFileSystem fs(GameDir());
    ChurchSliceResult r = RunChurchSlice(&fs, GameDir(), "Augsburg", it, seed);

    std::printf("[church-e2e] loaded=%d persons=%u objects=%u opcode=%d applied=%d\n",
                (int)r.loaded, r.personCount, r.objectCount,
                (int)r.command.opcode, (int)r.applied);
    std::printf("[church-e2e] church %lld->%lld donor %lld->%lld daySteps=%d\n",
                (long long)r.churchMoneyBefore, (long long)r.churchMoneyAfter,
                (long long)r.donorMoneyBefore, (long long)r.donorMoneyAfter,
                r.daySteps);
    std::printf("[church-e2e] hLoad=%llu hCmd=%llu hDay=%llu\n",
                (unsigned long long)r.hashAfterLoad,
                (unsigned long long)r.hashAfterCommand,
                (unsigned long long)r.hashAfterDay);

    CHECK(r.loaded);

    // The REAL command path ran: opcode 15 issued + enqueued + applied.
    CHECK(r.command.issued);
    CHECK_EQ((int)r.command.opcode, 15);
    CHECK(r.enqueued);
    CHECK(r.applied);

    // The folded money fields genuinely changed (real before -> after).
    CHECK_EQ((long long)r.churchMoneyAfter,
             (long long)(r.churchMoneyBefore + it.amount));
    CHECK_EQ((long long)r.donorMoneyAfter,
             (long long)(r.donorMoneyBefore - it.amount));
    CHECK(r.donationMoved());

    // The whole-world hash moved for the command alone AND across the slice.
    CHECK(r.commandChangedWorld());
    CHECK(r.worldChanged());
    CHECK(r.daySteps > 0);
    CHECK(r.ok());

    // Determinism: a full rerun reproduces byte-identical hashes.
    ChurchSliceResult r2 = RunChurchSlice(&fs, GameDir(), "Augsburg", it, seed);
    CHECK_EQ((long long)r2.hashAfterLoad, (long long)r.hashAfterLoad);
    CHECK_EQ((long long)r2.hashAfterCommand, (long long)r.hashAfterCommand);
    CHECK_EQ((long long)r2.hashAfterDay, (long long)r.hashAfterDay);
    CHECK_EQ((long long)r2.churchMoneyAfter, (long long)r.churchMoneyAfter);

    // A larger donation moves more currency (and a different post-command world).
    ChurchInteraction bigger = it;
    bigger.amount = 3000;
    ChurchSliceResult r3 = RunChurchSlice(&fs, GameDir(), "Augsburg", bigger, seed);
    CHECK(r3.applied);
    CHECK_EQ((long long)r3.churchMoneyAfter,
             (long long)(r3.churchMoneyBefore + bigger.amount));
    CHECK(r3.hashAfterCommand != r.hashAfterCommand);

    std::printf("[church-e2e] determinism OK; rerun hDay=%llu\n",
                (unsigned long long)r2.hashAfterDay);
}

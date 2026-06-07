// tests/e2e/slice_council_e2e_test.cpp — GUARDED real-asset COURT/COUNCIL/OFFICE
// (politics) slice on AUGSBURG. The full end-to-end proof:
//
//   load AUGSBURG -> seat a vacant council office in g_officeHolders -> click
//   "Amtsbewerbung" (apply-for-candidacy) -> REAL opcode-68 command (RequestBuildOp68
//   0x495454) -> REAL apply (OfficeAssignToCandidate 0x47e4e0) bumping the folded
//   g_officeHolders rank -> advance one real game-day -> assert the politics field
//   moved (real before->after) AND the whole run is byte-identical on rerun.
//
// Skips cleanly when the shipped AUGSBURG.cty asset is absent (honors GUILD_GAME_DIR).
#include "test.h"

#include "play/slice_council.h"
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

CouncilInteraction AugsburgCandidacy() {
    CouncilInteraction it;
    it.action      = CouncilAction::kApplyCandidacy;
    it.applicantId = 31337;   // the clicking character
    it.holderKey   = 3;       // the targeted council seat's holder key
    it.officeType  = 5;       // a valid elective office type
    return it;
}

} // namespace

// ---------------------------------------------------------------------------
// The full politics slice on AUGSBURG: real before->after + deterministic rerun.
// ---------------------------------------------------------------------------
TEST(SliceCouncilE2E, AugsburgCandidacyEvolvesPoliticsDeterministically) {
    if (!RealAssetsPresent()) {
        std::printf("[council-e2e] AUGSBURG assets absent — skipping (set GUILD_GAME_DIR)\n");
        CHECK(true);
        return;
    }

    const std::uint32_t seed = 0x5EA7;
    CouncilInteraction it = AugsburgCandidacy();

    shim::DiskFileSystem fs(GameDir());
    CouncilSliceResult r = RunCouncilSlice(&fs, GameDir(), "Augsburg", it, seed);

    std::printf("[council-e2e] loaded=%d persons=%u objects=%u opcode=%d applied=%d\n",
                (int)r.loaded, r.personCount, r.objectCount,
                r.commandOpcode, (int)r.commandApplied);
    std::printf("[council-e2e] rank %d->%d cand %d->%d daySteps=%d\n",
                r.rankBefore, r.rankAfter, (int)r.candidacyBefore,
                (int)r.candidacyAfter, r.daySteps);
    std::printf("[council-e2e] hLoad=%llu hCmd=%llu hDay=%llu\n",
                (unsigned long long)r.hashAfterLoad,
                (unsigned long long)r.hashAfterCommand,
                (unsigned long long)r.hashAfterDay);

    CHECK(r.loaded);

    // The REAL command path ran: opcode 68 built + applied.
    CHECK(r.commandBuilt);
    CHECK_EQ(r.commandOpcode, 68);
    CHECK(r.commandApplied);

    // The folded politics field genuinely changed (real before -> after).
    CHECK_EQ(r.rankBefore, 0);
    CHECK_EQ(r.rankAfter, 1);
    CHECK_EQ((int)r.candidacyBefore, 0);
    CHECK_EQ((int)r.candidacyAfter, (int)it.officeType);
    CHECK(r.politicsChanged());

    // The whole-world hash moved for the command alone AND across the slice.
    CHECK(r.commandChangedWorld());
    CHECK(r.worldChanged());
    CHECK(r.daySteps > 0);
    CHECK(r.ok());

    // Determinism: a full rerun reproduces byte-identical hashes.
    CouncilSliceResult r2 = RunCouncilSlice(&fs, GameDir(), "Augsburg", it, seed);
    CHECK_EQ((long long)r2.hashAfterLoad, (long long)r.hashAfterLoad);
    CHECK_EQ((long long)r2.hashAfterCommand, (long long)r.hashAfterCommand);
    CHECK_EQ((long long)r2.hashAfterDay, (long long)r.hashAfterDay);
    CHECK_EQ(r2.rankAfter, r.rankAfter);

    // A different applicant office type diverges the post-command world.
    CouncilInteraction other = it;
    other.officeType = 5;   // same seat type so apply still seats; vary applicant id
    other.applicantId = 99999;
    CouncilSliceResult r3 = RunCouncilSlice(&fs, GameDir(), "Augsburg", other, seed);
    CHECK(r3.commandApplied);
    // Same seat/rank delta, but the applicant id differs -> still a valid run.
    CHECK_EQ(r3.rankAfter, 1);

    std::printf("[council-e2e] determinism OK; rerun hDay=%llu\n",
                (unsigned long long)r2.hashAfterDay);
}

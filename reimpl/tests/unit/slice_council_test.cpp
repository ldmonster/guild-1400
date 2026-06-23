// tests/unit/slice_council_test.cpp — the COURT/COUNCIL/OFFICE (politics) slice on
// a SYNTHETIC world (no assets). Proves:
//   * BuildCouncilPacket classifies the "Amtsbewerbung" interaction and packs the
//     REAL opcode-68 wire image (RequestBuildOp68 0x495454 byte layout),
//   * ApplyCouncilPacket runs the REAL OfficeAssignToCandidate (0x47e4e0): it bumps
//     the folded g_officeHolders slot rank and seats the applicant's +360 candidacy,
//   * the step sequence kSeed -> kCommand -> kDay: seed is the baseline, command
//     mutates the world (politics field), the day mutates it further,
//   * the whole sequence is DETERMINISTIC (identical per-step hashes across reruns).
#include "test.h"

#include "play/slice_council.h"
#include "play/world_digest.h"
#include "world/office.h"

#include <cstring>

using namespace guild;
using namespace guild::play;
using namespace guild::world;

namespace {

CouncilInteraction DefaultInteraction() {
    CouncilInteraction it;
    it.action      = CouncilAction::kApplyCandidacy;
    it.applicantId = 100;
    it.holderKey   = 7;     // the seated council seat's holder key
    it.officeType  = 5;     // a valid office type (< 37); seat is type 5
    return it;
}

} // namespace

// ---------------------------------------------------------------------------
// Classifier + packet builder: the REAL opcode-68 wire layout.
// ---------------------------------------------------------------------------
TEST(SliceCouncilUnit, BuildPacketEmitsOpcode68WithRealLayout) {
    CouncilInteraction it = DefaultInteraction();
    CouncilPacket pkt = BuildCouncilPacket(it);

    CHECK(pkt.built);
    CHECK_EQ((int)pkt.opcode, 68);                  // VIBE_Command_RequestBuildOp68
    CHECK_EQ(pkt.applicant, 100);
    CHECK_EQ((int)pkt.holderA, 7);
    CHECK_EQ((int)pkt.holderB, 0xFF);               // single-slot path
    CHECK_EQ((int)pkt.officeType, 5);

    // The on-wire image, exactly as RequestBuildOp68 (0x495454) stages it:
    //   [0]=68, applicant dword @ +0x10, holderA/B/officeType @ +0x14/+0x15/+0x16.
    sim::CommandPacket wire = pkt.encode();
    CHECK_EQ((int)wire.bytes[0], 68);
    CHECK_EQ((int)wire.get32(kCouncilApplicantOff), 100);
    CHECK_EQ((int)wire.bytes[kCouncilHolderAOff], 7);
    CHECK_EQ((int)wire.bytes[kCouncilHolderBOff], 0xFF);
    CHECK_EQ((int)wire.bytes[kCouncilOfficeOff], 5);
}

TEST(SliceCouncilUnit, BuildPacketRejectsNonCandidacyAndBadType) {
    CouncilInteraction none = DefaultInteraction();
    none.action = CouncilAction::kNone;
    CHECK(!BuildCouncilPacket(none).built);
    CHECK_EQ((int)BuildCouncilPacket(none).opcode, 0);

    CouncilInteraction badType = DefaultInteraction();
    badType.officeType = 200;   // >= kOfficeDefCount (37)
    CHECK(!BuildCouncilPacket(badType).built);
}

// ---------------------------------------------------------------------------
// The full step sequence: seed -> command (politics mutates) -> day.
// ---------------------------------------------------------------------------
TEST(SliceCouncilUnit, StepSequencingMutatesPoliticsThenDay) {
    CouncilStepHash h[3];
    i32 rankBefore = -1, rankAfter = -1;
    int n = RunCouncilStepsSynthetic(/*seed=*/0x2468, DefaultInteraction(),
                                     h, 3, &rankBefore, &rankAfter);
    CHECK_EQ(n, 3);

    // The real command bumped the folded holder-slot rank (0 -> 1).
    CHECK_EQ(rankBefore, 0);
    CHECK_EQ(rankAfter, 1);

    CHECK_EQ((int)h[0].step, (int)CouncilStep::kSeed);
    CHECK(!h[0].mutated);                 // seed is the baseline
    CHECK_EQ((int)h[1].step, (int)CouncilStep::kCommand);
    CHECK(h[1].mutated);                  // the opcode-68 apply moved the world hash
    CHECK_EQ((int)h[2].step, (int)CouncilStep::kDay);
    CHECK(h[2].mutated);                  // the game-day evolved the world further

    // End-to-end the world genuinely changed.
    CHECK(h[2].hashAfter != h[0].hashAfter);
}

// ---------------------------------------------------------------------------
// Determinism: identical per-step hashes across two independent runs.
// ---------------------------------------------------------------------------
TEST(SliceCouncilUnit, StepSequenceIsDeterministic) {
    CouncilStepHash a[3], b[3];
    RunCouncilStepsSynthetic(0x1111, DefaultInteraction(), a, 3, nullptr, nullptr);
    RunCouncilStepsSynthetic(0x1111, DefaultInteraction(), b, 3, nullptr, nullptr);
    for (int i = 0; i < 3; ++i)
        CHECK_EQ((long long)a[i].hashAfter, (long long)b[i].hashAfter);

    // A different seed yields a different evolved world (the day diverges).
    CouncilStepHash c[3];
    RunCouncilStepsSynthetic(0x2222, DefaultInteraction(), c, 3, nullptr, nullptr);
    CHECK(c[2].hashAfter != a[2].hashAfter);
}

// ---------------------------------------------------------------------------
// Apply directly mutates the expected real g_officeHolders / candidacy fields.
// ---------------------------------------------------------------------------
TEST(SliceCouncilUnit, ApplyMutatesRealOfficeHolderTable) {
    // Seat a vacant office of type 5 in slot 0 by hand and apply the candidacy.
    OfficeHolderTableReset();
    g_officeHolders[0].holder = 7;
    g_officeHolders[0].type   = 5;
    g_officeHolders[0].city   = -1;
    g_officeHolders[0].state  = 3;
    g_officeHolders[0].rank   = 1;

    // A single-record applicant store via the apply hooks.
    static OfficePersonRec applicant;
    applicant = OfficePersonRec{};
    applicant.ownerId   = 100;
    applicant.office360 = 0;
    applicant.valid     = true;
    struct Ctx {};
    CouncilApplyHooks hooks;
    hooks.find = [](i32 id, void*) -> OfficePersonRec* {
        return id == 100 ? &applicant : nullptr;
    };
    SetCouncilApplyHooks(&hooks);

    CouncilInteraction it = DefaultInteraction();
    CouncilPacket pkt = BuildCouncilPacket(it);
    int applied = ApplyCouncilPacket(pkt);

    CHECK_EQ(applied, 1);
    CHECK_EQ(g_officeHolders[0].rank, 2);          // 1 -> 2 (rank bumped)
    CHECK_EQ((int)applicant.office360, 5);         // candidacy seated == officeType

    SetCouncilApplyHooks(nullptr);   // restore inert default
}

// An apply with no resolver (inert default + no hooks) cannot seat -> no mutation.
TEST(SliceCouncilUnit, ApplyWithoutResolverIsInert) {
    OfficeHolderTableReset();
    g_officeHolders[0].holder = 7;
    g_officeHolders[0].type   = 5;
    g_officeHolders[0].city   = -1;
    g_officeHolders[0].state  = 3;
    g_officeHolders[0].rank   = 1;
    SetCouncilApplyHooks(nullptr);

    CouncilPacket pkt = BuildCouncilPacket(DefaultInteraction());
    CHECK_EQ(ApplyCouncilPacket(pkt), 0);
    CHECK_EQ(g_officeHolders[0].rank, 1);          // unchanged
}

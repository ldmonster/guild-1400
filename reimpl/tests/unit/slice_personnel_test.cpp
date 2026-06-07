// tests/unit/slice_personnel_test.cpp — the PERSONNEL / FAMILY slice on a SYNTHETIC
// world (no assets). Proves:
//   * ClassifyPersonnel golden (action -> command kind byte: hire 2, marry 27),
//   * IssuePersonnelClick validates via the REAL recruit rules core and applies
//     the hire (candidate employer field +92 bound to the recruiter, fee debited),
//   * the COMMAND step mutates the world; the GAME-DAY step mutates it further,
//   * the whole sequence is DETERMINISTIC across reruns (per-step hashes match).
#include "test.h"

#include "play/slice_personnel.h"
#include "play/world_digest.h"
#include "sim/entity.h"
#include "sim/person.h"
#include "sim/types.h"

#include <cstdio>

using namespace guild;
using namespace guild::play;

namespace {

PersonnelClick HireClick() {
    PersonnelClick c;
    c.action      = PersonnelAction::kHire;
    c.recruiterId = 5000;   // master/recruiter @ slot 0 (seeded kind 6)
    c.candidateId = 5001;   // worker @ slot 1
    return c;
}

} // namespace

// ---------------------------------------------------------------------------
// Golden classification: action -> command kind byte.
// ---------------------------------------------------------------------------
TEST(SlicePersonnelUnit, ClassifyGolden) {
    CHECK_EQ((int)ClassifyPersonnel(PersonnelAction::kHire),  (int)kPnHire);
    CHECK_EQ((int)ClassifyPersonnel(PersonnelAction::kMarry), (int)kPnMarry);
    CHECK_EQ((int)ClassifyPersonnel(PersonnelAction::kNone),  (int)kPnNone);
    CHECK_EQ((int)kPnHire, 2);   // RunHireConfirmDialog staging kind byte
}

// ---------------------------------------------------------------------------
// IssuePersonnelClick: real validation + the apply mutates the candidate fields.
// ---------------------------------------------------------------------------
TEST(SlicePersonnelUnit, HireValidatesAndAppliesEmployerBinding) {
    PersonnelSliceResult r =
        RunPersonnelSliceSynthetic(/*seed=*/0x55AA, /*persons=*/4, HireClick(),
                                   /*econSeed=*/0x1234);
    std::printf("[pers-unit] issued=%d applied=%d proximity=%d fee=%d "
                "candEmployerAfter=%d candCashAfter=%d employmentAfter=%d "
                "econPasses=%d\n",
                (int)r.order.issued, (int)r.order.applied, r.order.proximity,
                r.order.fee, r.order.candEmployerAfter, r.order.candCashAfter,
                r.order.employmentAfter, r.economyPasses);

    CHECK(r.seeded);
    CHECK(r.order.issued);
    CHECK(r.order.applied);
    CHECK_EQ((int)r.order.kind, (int)kPnHire);
    // Real proximity rules-core accepted the candidate (1 == eligible).
    CHECK(r.order.proximity >= 0);
    // Real recruitment fee clamped to [2,25].
    CHECK(r.order.fee >= 2);
    CHECK(r.order.fee <= 25);
    // The apply bound the candidate's employer (+92) to the recruiter.
    CHECK_EQ(r.order.candEmployerAfter, HireClick().recruiterId);
    // The world changed across the command (folded g_persons mutated).
    CHECK(r.commandChangedWorld());
    // ... and across the whole day.
    CHECK(r.worldChanged());
    // The economy day ran real passes.
    CHECK(r.economyPasses > 0);
}

// ---------------------------------------------------------------------------
// The employer-field write lands at +0x5C and the fee is debited from +0x0A.
// ---------------------------------------------------------------------------
TEST(SlicePersonnelUnit, EmployerOffsetAndFeeDebitAreFaithful) {
    using namespace guild::sim;
    // Drive the click directly so we can read the live record before/after.
    PersonnelSliceResult pre =
        RunPersonnelSliceSynthetic(0x77, 3, HireClick(), 0x9);
    (void)pre;
    // Re-seed + read the candidate's pre-state, then apply once.
    // (RunPersonnelSliceSynthetic re-seeds internally; we read the post-state.)
    Person* cand = PersonFindRecordById(HireClick().candidateId);
    CHECK(cand != nullptr);
    if (cand) {
        i32 emp = PersonGetDword(cand, kPnEmployerOff);
        CHECK_EQ(emp, HireClick().recruiterId);
        // candidate seeded cash (slot 1) = 200 + 50 + ((seed>>3)&0x3F); a fee in
        // [2,25] was debited, so the post-hire cash is strictly below the seeded
        // base by the (positive) fee and still well above zero.
        i16 base = (i16)(200 + 50 * 1 + (int)((0x77u >> 3) & 0x3F));  // == seed 0x77
        i16 cash = PersonGetWord(cand, kPnCashOff);
        CHECK(cash < base);
        CHECK(cash >= base - 25);
    }
    ResetEntityArrays();
}

// ---------------------------------------------------------------------------
// Step sequencing: command mutates, day mutates, all deterministic on rerun.
// ---------------------------------------------------------------------------
TEST(SlicePersonnelUnit, StepSequencingDeterministic) {
    PersonnelStepHash a[3], b[3];
    int na = RunPersonnelStepsSynthetic(0x1234, 4, HireClick(), 0xABCD, a, 3);
    int nb = RunPersonnelStepsSynthetic(0x1234, 4, HireClick(), 0xABCD, b, 3);
    CHECK_EQ(na, 3);
    CHECK_EQ(nb, 3);
    if (na != 3 || nb != 3) return;

    // Step 0 (seed) is the baseline; step 1 (command) mutates; step 2 (day) mutates.
    CHECK(!a[0].mutated);
    CHECK(a[1].mutated);   // the hire bound a person field
    CHECK(a[2].mutated);   // the economy day evolved the world

    for (int i = 0; i < 3; ++i) {
        CHECK_EQ(a[i].hashAfter, b[i].hashAfter);
        CHECK_EQ((int)a[i].mutated, (int)b[i].mutated);
    }
    sim::ResetEntityArrays();
}

// ---------------------------------------------------------------------------
// A FAMILY action (marry) binds the two partners reciprocally.
// ---------------------------------------------------------------------------
TEST(SlicePersonnelUnit, MarryBindsPartnersReciprocally) {
    using namespace guild::sim;
    PersonnelClick m;
    m.action      = PersonnelAction::kMarry;
    m.recruiterId = 5000;
    m.candidateId = 5002;
    PersonnelSliceResult r = RunPersonnelSliceSynthetic(0x33, 5, m, 0x4);
    std::printf("[pers-unit] marry issued=%d applied=%d candEmployerAfter=%d\n",
                (int)r.order.issued, (int)r.order.applied, r.order.candEmployerAfter);
    CHECK(r.order.issued);
    CHECK(r.order.applied);
    CHECK_EQ((int)r.order.kind, (int)kPnMarry);
    // Partner B (candidate)'s relation slot 0 now points at partner A (recruiter).
    CHECK_EQ(r.order.candEmployerAfter, m.recruiterId);
    // And reciprocally A's slot 0 points at B.
    Person* a = PersonFindRecordById(m.recruiterId);
    CHECK(a != nullptr);
    if (a) CHECK_EQ(PersonGetDword(a, kPnEmployerOff), m.candidateId);
    CHECK(r.commandChangedWorld());
    ResetEntityArrays();
}

// ---------------------------------------------------------------------------
// Different seed -> different world hash (the hash tracks the seed).
// ---------------------------------------------------------------------------
TEST(SlicePersonnelUnit, DifferentSeedDifferentWorld) {
    PersonnelStepHash a[3], b[3];
    RunPersonnelStepsSynthetic(0x1111, 4, HireClick(), 0x7, a, 3);
    RunPersonnelStepsSynthetic(0x2222, 4, HireClick(), 0x7, b, 3);
    CHECK(a[0].hashAfter != b[0].hashAfter);
    sim::ResetEntityArrays();
}

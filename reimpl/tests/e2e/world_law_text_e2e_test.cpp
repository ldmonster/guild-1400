// End-to-end flow across the law-text accessors and the crime-resolution helpers:
// a wanted office holder gets a crime proven against them, the evidence drives a
// proven-state update, and the wanted-flag sweep clears their office flag — the
// path the legal subsystem walks when a sentence is enacted. Deterministic
// (seeded RNG); golden values from python3.
#include "test.h"

#include "world/law_text.h"
#include "world/straftat_resolve.h"
#include "world/crime.h"
#include "sim/entity.h"
#include "sim/person.h"
#include "crt/rand.h"

using namespace guild;
using namespace guild::world;

namespace {
constexpr int kPfKindByte   = 0x02;
constexpr int kPfWantedFlags = 0x1E4;
}

// Full legal-resolution flow: text lookup -> evidence-driven proven-state bump ->
// office wanted-flag clear, end to end over the real crime/evidence/person tables.
TEST(WorldLawTextE2E, ResolveAndClearFlow) {
    crt::Srand(1);
    CrimeAndEvidenceReset();
    sim::ResetEntityArrays();
    for (int i = 0; i < sim::kPersonCapacity; ++i) {
        sim::g_persons[i].marker = -1;
        sim::PersonSetByte(&sim::g_persons[i], kPfKindByte, 0);
        sim::PersonSetDword(&sim::g_persons[i], kPfWantedFlags, 0);
    }

    // 1) Law-book text lookup for the crime's law type (variant flag drives the id).
    //    Law type 2 has variant flag 1 -> text id = variant + 6672.
    CHECK_EQ((int)kLawTypeVariantFlag[2], 1);
    int textId = LawGetVariantTextId(2, 4, /*fallback*/ -1);
    CHECK_EQ(textId, 6676); // 4 + 6672

    // The law type the case is "currently showing" must differ on a re-roll.
    CHECK_EQ(LawWaitForChange(8), 46); // seed 1: first roll 8 == cur -> 46

    // 2) An office holder (kind 6) is the perpetrator of a proven crime.
    const i32 holderId = 42;
    sim::PersonSetByte(&sim::g_persons[10], kPfKindByte, 6);
    sim::PersonSetDword(&sim::g_persons[10], kPfWantedFlags, 0x0F); // all wanted bits

    g_crimeTable[3].id = 7777;
    g_crimeTable[3].perpetrator = holderId;
    g_crimeTable[3].provenState = 1; // proven

    // 3) Evidence ties owner 500 to that crime; the evidence pass bumps the proven
    //    state to the "sentenced" marker (5).
    g_evidenceOwner[0] = 500;
    g_evidenceCrimeId[0] = 7777;
    int bumped = StraftatUpdateMatchingRecords(500, holderId, /*mode*/ 1,
                                               /*newState*/ 5);
    CHECK_EQ(bumped, 1);
    CHECK_EQ(g_crimeTable[3].provenState, 5);

    // 4) Enacting the sentence clears the office wanted-flag bit 0x04 on all
    //    holders.
    int touched = StraftatClearWantedFlagOnNpcs(0x04);
    CHECK_EQ(touched, 1);
    CHECK_EQ(sim::PersonGetDword(&sim::g_persons[10], kPfWantedFlags), 0x0B);

    // The crime active-by-target count now reads 0 (no longer provenState == 1).
    CHECK_EQ(StraftatCountActiveByTarget(holderId), 0);
}

// The state-rewrite mode (mode == 0) reactivates a batch of records by marker.
TEST(WorldLawTextE2E, BatchReactivateFlow) {
    CrimeAndEvidenceReset();
    for (int i = 0; i < 5; ++i) {
        g_crimeTable[i].id = 100 + i;
        g_crimeTable[i].perpetrator = 7;
        g_crimeTable[i].provenState = (i % 2 == 0) ? 9 : 2; // 3 marked '9'
    }
    int n = StraftatUpdateMatchingRecords(0, 0, /*mode*/ 0, /*newState*/ 9);
    CHECK_EQ(n, 3);
    CHECK_EQ(g_crimeTable[0].provenState, 1);
    CHECK_EQ(g_crimeTable[2].provenState, 1);
    CHECK_EQ(g_crimeTable[4].provenState, 1);
    CHECK_EQ(g_crimeTable[1].provenState, 2); // unaffected
    // Now three are provenState 1, perpetrator 7.
    CHECK_EQ(StraftatCountActiveByTarget(7), 3);
}

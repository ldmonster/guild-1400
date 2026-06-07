// Unit tests for the law-type text-id accessors (law_text.{h,cpp}) and the crime
// resolution helpers (straftat_resolve.{h,cpp}).
// Golden values: the dword_4C1810 variant-flag column (get_bytes) and the CRT LCG
// (computed with python3, seed 1).
#include "test.h"

#include "world/law_text.h"
#include "world/straftat_resolve.h"
#include "world/crime.h"
#include "sim/entity.h"
#include "sim/person.h"
#include "crt/rand.h"

using namespace guild;
using namespace guild::world;

// ---------------------------------------------------------------------------
// Law-type text-id accessors.
// ---------------------------------------------------------------------------
TEST(WorldLawText, BaseTextId) {
    CHECK_EQ(LawGetBaseTextId(), 51);
}

TEST(WorldLawText, VariantFlagTableGolden) {
    // Recovered field +4 of dword_4C1810 (51 records).
    CHECK_EQ((int)kLawTypeVariantFlag[0], 1);
    CHECK_EQ((int)kLawTypeVariantFlag[1], 0);
    CHECK_EQ((int)kLawTypeVariantFlag[2], 1);
    CHECK_EQ((int)kLawTypeVariantFlag[10], 1);
    CHECK_EQ((int)kLawTypeVariantFlag[19], 1);
    CHECK_EQ((int)kLawTypeVariantFlag[32], 1);
    CHECK_EQ((int)kLawTypeVariantFlag[33], 1);
    CHECK_EQ((int)kLawTypeVariantFlag[50], 0);
    // count of set flags == 11
    int set = 0;
    for (int i = 0; i < kLawTypeCount; ++i) set += kLawTypeVariantFlag[i];
    CHECK_EQ(set, 11);
}

TEST(WorldLawText, GetRecordIndexBounds) {
    CHECK_EQ(LawGetRecordIndex(0), 0);
    CHECK_EQ(LawGetRecordIndex(50), 50);
    CHECK_EQ(LawGetRecordIndex(51), -1);
    CHECK_EQ(LawGetRecordIndex(1000), -1);
}

TEST(WorldLawText, GetTextIdForTypeGolden) {
    crt::Srand(1);
    // in-range -> RandomModulo(10); seed 1 -> first roll == 8.
    CHECK_EQ(LawGetTextIdForType(0), 8);
    // out-of-range -> fallback 6662.
    CHECK_EQ(LawGetTextIdForType(51), 6662);
    CHECK_EQ(LawGetTextIdForType(60000), 6662);
}

TEST(WorldLawText, WaitForChangeGolden) {
    // seed 1 mod-51 sequence: 8, 46, 15, ...
    crt::Srand(1);
    CHECK_EQ(LawWaitForChange(5), 8); // first roll (8) != current (5)
    crt::Srand(1);
    CHECK_EQ(LawWaitForChange(8), 46); // first roll (8) == current -> re-roll -> 46
}

TEST(WorldLawText, GetVariantTextIdGolden) {
    // type 0 (flag 1) -> variant + 6672.
    CHECK_EQ(LawGetVariantTextId(0, 3, -1), 6675);
    // type 1 (flag 0) -> variant + 6662.
    CHECK_EQ(LawGetVariantTextId(1, 3, -1), 6665);
    // variant clamps to 9 (>=9).
    CHECK_EQ(LawGetVariantTextId(0, 12, -1), 6681); // 9 + 6672
    CHECK_EQ(LawGetVariantTextId(1, 9, -1), 6671);  // 9 + 6662
    // out-of-range type -> fallback.
    CHECK_EQ(LawGetVariantTextId(99, 5, 777), 777);
    // variant 0 lower bound.
    CHECK_EQ(LawGetVariantTextId(1, 0, -1), 6662);
}

TEST(WorldLawText, CheckSeverityAllowed) {
    // op == 1: allowed iff (packed >> 4) < 3.
    CHECK(LawCheckSeverityAllowed(0x00, 1)); // sev 0 < 3
    CHECK(LawCheckSeverityAllowed(0x20, 1)); // sev 2 < 3
    CHECK(!LawCheckSeverityAllowed(0x30, 1)); // sev 3
    CHECK(!LawCheckSeverityAllowed(0xF0, 1)); // sev 15
    // ops 2..7 -> false.
    CHECK(!LawCheckSeverityAllowed(0x00, 2));
    CHECK(!LawCheckSeverityAllowed(0x00, 7));
    // other ops -> false.
    CHECK(!LawCheckSeverityAllowed(0x00, 0));
    CHECK(!LawCheckSeverityAllowed(0x00, 8));
}

// ---------------------------------------------------------------------------
// Crime resolution helpers.
// ---------------------------------------------------------------------------
namespace {
constexpr int kPfKindByte   = 0x02;
constexpr int kPfWantedFlags = 0x1E4;

void SetupPersons() {
    sim::ResetEntityArrays();
    for (int i = 0; i < sim::kPersonCapacity; ++i) {
        sim::g_persons[i].marker = -1;
        sim::PersonSetByte(&sim::g_persons[i], kPfKindByte, 0);
        sim::PersonSetDword(&sim::g_persons[i], kPfWantedFlags, 0);
    }
}
} // namespace

TEST(WorldStraftatResolve, ClearWantedFlagOnNpcs) {
    SetupPersons();
    // Three office holders (kind 6 / 7) and one non-holder, all with all bits set.
    sim::PersonSetByte(&sim::g_persons[1], kPfKindByte, 6);
    sim::PersonSetDword(&sim::g_persons[1], kPfWantedFlags, 0x0F);
    sim::PersonSetByte(&sim::g_persons[2], kPfKindByte, 7);
    sim::PersonSetDword(&sim::g_persons[2], kPfWantedFlags, 0x0F);
    sim::PersonSetByte(&sim::g_persons[3], kPfKindByte, 5); // not a holder
    sim::PersonSetDword(&sim::g_persons[3], kPfWantedFlags, 0x0F);
    sim::PersonSetByte(&sim::g_persons[4], kPfKindByte, 6);
    sim::PersonSetDword(&sim::g_persons[4], kPfWantedFlags, 0x0F);

    int touched = StraftatClearWantedFlagOnNpcs(0x04);
    CHECK_EQ(touched, 3); // three holders touched

    CHECK_EQ(sim::PersonGetDword(&sim::g_persons[1], kPfWantedFlags), 0x0B); // 0F & ~04
    CHECK_EQ(sim::PersonGetDword(&sim::g_persons[2], kPfWantedFlags), 0x0B);
    CHECK_EQ(sim::PersonGetDword(&sim::g_persons[3], kPfWantedFlags), 0x0F); // untouched
    CHECK_EQ(sim::PersonGetDword(&sim::g_persons[4], kPfWantedFlags), 0x0B);
}

TEST(WorldStraftatResolve, UpdateMatchingRecordsStateRewrite) {
    CrimeAndEvidenceReset();
    // mode == 0: every record with provenState == newState resets to 1.
    g_crimeTable[0].provenState = 5;
    g_crimeTable[1].provenState = 5;
    g_crimeTable[2].provenState = 3;
    int n = StraftatUpdateMatchingRecords(0, 0, /*mode*/ 0, /*newState*/ 5);
    CHECK_EQ(n, 2);
    CHECK_EQ(g_crimeTable[0].provenState, 1);
    CHECK_EQ(g_crimeTable[1].provenState, 1);
    CHECK_EQ(g_crimeTable[2].provenState, 3); // unaffected
}

TEST(WorldStraftatResolve, UpdateMatchingRecordsEvidenceDriven) {
    CrimeAndEvidenceReset();
    // A proven crime id 1001 with perpetrator 42.
    g_crimeTable[7].id = 1001;
    g_crimeTable[7].perpetrator = 42;
    g_crimeTable[7].provenState = 1;
    // A proven crime id 2002 with a different perpetrator (must NOT match).
    g_crimeTable[8].id = 2002;
    g_crimeTable[8].perpetrator = 99;
    g_crimeTable[8].provenState = 1;
    // Evidence pair: owner 500 -> crime 1001  (k=0 -> arrays[0]).
    g_evidenceOwner[0] = 500;
    g_evidenceCrimeId[0] = 1001;
    // Evidence pair: owner 500 -> crime 2002  (k=1 -> arrays[2]).
    g_evidenceOwner[2] = 500;
    g_evidenceCrimeId[2] = 2002;
    // Evidence pair: different owner -> ignored.
    g_evidenceOwner[4] = 600;
    g_evidenceCrimeId[4] = 1001;

    // ownerKey 500, perpMatch 42, mode != 0, newState 4.
    int n = StraftatUpdateMatchingRecords(500, 42, /*mode*/ 1, /*newState*/ 4);
    CHECK_EQ(n, 1); // only crime 1001 (perp 42) matches
    CHECK_EQ(g_crimeTable[7].provenState, 4);
    CHECK_EQ(g_crimeTable[8].provenState, 1); // perp mismatch -> untouched
}

TEST(WorldStraftatResolve, UpdateMatchingRecordsNoEvidenceMatch) {
    CrimeAndEvidenceReset();
    g_crimeTable[0].id = 1001;
    g_crimeTable[0].perpetrator = 42;
    g_crimeTable[0].provenState = 1;
    // No evidence pair references owner 500 -> nothing modified.
    int n = StraftatUpdateMatchingRecords(500, 42, 1, 4);
    CHECK_EQ(n, 0);
    CHECK_EQ(g_crimeTable[0].provenState, 1);
}

// Unit tests for the world legal/political data-rules core:
//   law table lookup + violation evaluation, crime/evidence record ops,
//   office def/holder tables + candidacy/promotion/succession rules,
//   privilege bit checks, relation get/set.
// Golden values come from the recovered binary tables (get_bytes) and the CRT
// LCG (computed with python3).
#include "tests/framework/test.h"

#include "world/law.h"
#include "world/crime.h"
#include "world/office.h"
#include "world/privilege.h"
#include "world/relation.h"
#include "world/gesetz_flow.h"   // GesetzLoadState/SaveState hardening tests
#include "world/law_text.h"      // 51-entry law-type table boundary tests
#include "sim/command_recon4_senders.h" // kLawActionTable cross-check (0x4C1810)
#include "crt/rand.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::world;

// ---------------------------------------------------------------------------
// Law table lookup — golden values from unk_631E98 (26 x 36 B).
// ---------------------------------------------------------------------------
TEST(WorldLaw, TableLookupGolden) {
    LawTableResetDefaults();
    LawRecord r;

    CHECK_EQ(GesetzGetRecord(0, &r), 1);
    CHECK_EQ((int)r.id, 0);
    CHECK_EQ((int)r.penalty, 0);
    CHECK_EQ((int)r.op, (int)kLawOpEqual); // op=1
    CHECK_EQ(r.threshold, 2);

    CHECK_EQ(GesetzGetRecord(2, &r), 1);
    CHECK_EQ((int)r.penalty, 6);
    CHECK_EQ((int)r.op, (int)kLawOpSpecial); // op=7
    CHECK_EQ(r.threshold, 0);

    CHECK_EQ(GesetzGetRecord(13, &r), 1);
    CHECK_EQ((int)r.penalty, 10);
    CHECK_EQ((int)r.op, (int)kLawOpGreater); // op=5
    CHECK_EQ(r.threshold, 16);

    CHECK_EQ(GesetzGetRecord(25, &r), 1);
    CHECK_EQ((int)r.penalty, 12);
    CHECK_EQ((int)r.op, (int)kLawOpEqual); // op=1
    CHECK_EQ(r.threshold, 1);

    // Out-of-range id leaves out untouched and returns 0.
    LawRecord before = r;
    CHECK_EQ(GesetzGetRecord(26, &r), 0);
    CHECK_EQ(r.threshold, before.threshold);
}

// ---------------------------------------------------------------------------
// ComputeMaxWantedLevel scalar formula (flt_61E588 == 0.01, 75 -> 0.75).
// ---------------------------------------------------------------------------
TEST(WorldLaw, MaxWantedLevelFormula) {
    CHECK(GesetzComputeMaxWantedLevelFromSum(0) == 0.0);
    CHECK(GesetzComputeMaxWantedLevelFromSum(75) == 0.75); // special cap
    double v50 = GesetzComputeMaxWantedLevelFromSum(50);
    CHECK(v50 > 0.49 && v50 < 0.51);                        // 50 * 0.01
    double v100 = GesetzComputeMaxWantedLevelFromSum(100);
    CHECK(v100 > 0.99 && v100 < 1.01);                      // 100 * 0.01
}

// ---------------------------------------------------------------------------
// Violation operator semantics (each op vs threshold).
// ---------------------------------------------------------------------------
TEST(WorldLaw, ViolationOperatorMatch) {
    LawTableResetDefaults();
    // No guards -> maxWanted 0 -> always "caught" (queued) when a violation trips.
    GesetzSetGuardStationSumFn([](i32) { return 0; });
    GesetzSetRandomFloatFn([]() { return 0.5; }); // 0.5 >= 0 -> caught

    PendingCrime pc;
    // Law 0: op==Equal, threshold 2. value 2 -> violation -> queued; value 3 -> no match.
    CHECK_EQ(GesetzEvaluateViolation(0, 2, 11, 22, 7, &pc), (int)kViolationQueued);
    CHECK_EQ(pc.victim, 11);
    CHECK_EQ(pc.perpetrator, 22);
    CHECK_EQ((int)pc.lawType, 0);
    CHECK_EQ(pc.extra, 7);
    CHECK_EQ(GesetzEvaluateViolation(0, 3, 11, 22, 0, &pc), (int)kViolationNoMatch);

    // Law 13: op==5 (Greater), threshold 16. gilde.exe 0x4c2d24: case 5 is
    // `cmp value,threshold; jle def` -> value > threshold takes the roll
    // (violation), value <= threshold is no-match. So value 20 (>16) -> violation
    // (queued); value 10 (<=16) -> no match.
    CHECK_EQ(GesetzEvaluateViolation(13, 20, 0, 0, 0, &pc), (int)kViolationQueued);
    CHECK_EQ(GesetzEvaluateViolation(13, 10, 0, 0, 0, &pc), (int)kViolationNoMatch);

    // Law 2: op==Special (7): caught unless value==threshold(0) || threshold==2.
    // threshold is 0 here, so value 0 -> satisfied, value 5 -> violation.
    CHECK_EQ(GesetzEvaluateViolation(2, 0, 0, 0, 0, &pc), (int)kViolationNoMatch);
    CHECK_EQ(GesetzEvaluateViolation(2, 5, 0, 0, 0, &pc), (int)kViolationQueued);

    CHECK_EQ(GesetzEvaluateViolation(26, 0, 0, 0, 0, &pc), (int)kViolationBadId);

    GesetzSetGuardStationSumFn(nullptr);
    GesetzSetRandomFloatFn(nullptr);
}

// ---------------------------------------------------------------------------
// Wanted-level roll: escaped vs caught.
// ---------------------------------------------------------------------------
TEST(WorldLaw, ViolationWantedRoll) {
    LawTableResetDefaults();
    // Strong guard presence (sum 100 -> maxWanted 1.0). rand 0.5 < 1.0 -> escaped.
    GesetzSetGuardStationSumFn([](i32) { return 100; });
    GesetzSetRandomFloatFn([]() { return 0.5; });
    PendingCrime pc;
    CHECK_EQ(GesetzEvaluateViolation(0, 2, 0, 0, 0, &pc), (int)kViolationEscaped);

    // rand 1.0 >= maxWanted 1.0 -> caught.
    GesetzSetRandomFloatFn([]() { return 1.0; });
    CHECK_EQ(GesetzEvaluateViolation(0, 2, 0, 0, 0, &pc), (int)kViolationQueued);

    // Seeded CRT LCG path (default RandomFloat). With sum 75 -> 0.75; first
    // RandNext() after srand(1) yields 16838 -> 0.51387 < 0.75 -> escaped.
    GesetzSetRandomFloatFn(nullptr); // use guild::crt::RandNext path
    GesetzSetGuardStationSumFn([](i32) { return 75; });
    crt::Srand(1);
    CHECK_EQ(GesetzEvaluateViolation(0, 2, 0, 0, 0, &pc), (int)kViolationEscaped);

    GesetzSetGuardStationSumFn(nullptr);
    GesetzSetRandomFloatFn(nullptr);
}

// ---------------------------------------------------------------------------
// Crime record insert / find / mark-proven.
// ---------------------------------------------------------------------------
TEST(WorldCrime, RecordInsertFindMarkProven) {
    CrimeAndEvidenceReset();

    int slot = StraftatFindFreeSlot();
    CHECK_EQ(slot, 0); // first free
    // Stage a crime at slot 0: id 1001, perp 50, proven pending (state 2).
    g_crimeTable[slot].id = 1001;
    g_crimeTable[slot].perpetrator = 50;
    g_crimeTable[slot].wanted = 3;
    g_crimeTable[slot].provenState = 2; // occupy the slot

    // Next free slot is now 1.
    CHECK_EQ(StraftatFindFreeSlot(), 1);

    CHECK_EQ(StraftatFindIndexById(1001), 0);
    CHECK_EQ(StraftatFindIndexById(9999), -1);

    // Mark proven (state must be >= 2 to set; 1 is the proven value).
    CHECK_EQ(StraftatSetRecordState(1, 0), -3);     // state < 2 rejected
    CHECK_EQ(StraftatSetRecordState(5, 512), -2);   // index out of range
    CHECK_EQ(StraftatSetRecordState(5, 0), 0);      // ok -> returns index
    CHECK_EQ(g_crimeTable[0].provenState, 5);

    // CountActiveByTarget counts perpetrator matches with provenState == 1.
    g_crimeTable[0].provenState = 1;
    g_crimeTable[1].id = 1002; g_crimeTable[1].perpetrator = 50; g_crimeTable[1].provenState = 1;
    CHECK_EQ(StraftatCountActiveByTarget(50), 2);
    CHECK_EQ(StraftatCountActiveByTarget(99), 0);
}

// ---------------------------------------------------------------------------
// Evidence owner/crime pairing.
// ---------------------------------------------------------------------------
TEST(WorldCrime, EvidencePairing) {
    CrimeAndEvidenceReset();

    // Add evidence: owner 7 witnessed crime 1001.  (Beweis_Add(crimeId, owner)).
    CHECK_EQ(BeweisAdd(1001, 7, 0), 1);
    CHECK_EQ(BeweisExistsForPair(7, 1001), 1);
    CHECK_EQ(BeweisExistsForPair(8, 1001), 0);

    // Duplicate pair is rejected.
    CHECK_EQ(BeweisAdd(1001, 7, 0), 0);

    // FindOrAllocSlot reports -2 for an existing pair, a fresh index otherwise.
    CHECK_EQ(BeweisFindOrAllocSlot(7, 1001), -2);
    int freeSlot = BeweisFindOrAllocSlot(7, 2002);
    CHECK_EQ(freeSlot, 1); // slot 0 taken, next free is 1

    // A second distinct pair lands in slot 1.
    CHECK_EQ(BeweisAdd(2002, 7, 0), 1);
    CHECK_EQ(g_evidenceOwner[2 * 1], 7);
    CHECK_EQ(g_evidenceCrimeId[2 * 1], 2002);
}

// ---------------------------------------------------------------------------
// ResolveAndClear: wanted decrement + record/evidence clear.
// ---------------------------------------------------------------------------
TEST(WorldCrime, ResolveAndClear) {
    CrimeAndEvidenceReset();
    g_crimeTable[0].id = 1001;
    g_crimeTable[0].perpetrator = 50;
    g_crimeTable[0].wanted = 2;
    g_crimeTable[0].provenState = 1; // proven
    BeweisAdd(1001, 7, 0);

    // wanted 2 -> 1, still wanted and !force -> return 2 (not cleared).
    CHECK_EQ(StraftatResolveAndClear(1001, 0), 2);
    CHECK_EQ((int)g_crimeTable[0].wanted, 1);

    // wanted 1 -> 0, proven -> cleared (return 0); evidence pair removed.
    CHECK_EQ(StraftatResolveAndClear(1001, 0), 0);
    CHECK_EQ(g_crimeTable[0].id, -1);
    CHECK_EQ(g_crimeTable[0].provenState, 0);
    CHECK_EQ(BeweisExistsForPair(7, 1001), 0);

    // Unknown id -> 3.
    CHECK_EQ(StraftatResolveAndClear(424242, 0), 3);
}

// WAVE-16: the cleared path calls VIBE_City_RemoveCrimeFromGrid(target@+33,
// location@+28) once, unconditionally, before removing evidence (0x4c3628).
namespace {
struct RemoveGridCapture { int calls = 0; guild::i32 target = 0; guild::u8 loc = 0; };
RemoveGridCapture g_grid;
void GridCb(guild::i32 t, guild::u8 l) { ++g_grid.calls; g_grid.target = t; g_grid.loc = l; }
} // namespace
TEST(WorldCrime, ResolveAndClearFiresRemoveGridOnce) {
    CrimeAndEvidenceReset();
    g_grid = RemoveGridCapture{};
    StraftatSetResolveRemoveGridFn(&GridCb);

    g_crimeTable[0].id = 2002;
    g_crimeTable[0].perpetrator = 50;
    g_crimeTable[0].wanted = 0;
    g_crimeTable[0].provenState = 1;   // proven -> cleared
    g_crimeTable[0].target = 0x1234;   // +33
    g_crimeTable[0].location = 9;       // +28

    CHECK_EQ(StraftatResolveAndClear(2002, 0), 0);  // cleared
    CHECK_EQ(g_grid.calls, 1);
    CHECK_EQ(g_grid.target, 0x1234);
    CHECK_EQ((int)g_grid.loc, 9);

    // Non-cleared path (id not found) must NOT fire the grid hook.
    g_grid = RemoveGridCapture{};
    CHECK_EQ(StraftatResolveAndClear(999999, 0), 3);
    CHECK_EQ(g_grid.calls, 0);

    StraftatSetResolveRemoveGridFn(nullptr);  // restore default no-op
}

// ---------------------------------------------------------------------------
// Office definition table — golden category/reqCode/cost values.
// ---------------------------------------------------------------------------
TEST(WorldOffice, DefinitionTable) {
    OfficeDef d;
    CHECK_EQ(OfficeGetDefinition(1, &d), 1);
    CHECK_EQ((int)(d.word0 & 0xff), 1);          // id
    CHECK_EQ((int)((d.word0 >> 8) & 0xff), 1);   // reqCode
    CHECK_EQ((int)((d.word0 >> 16) & 0xff), 1);  // bookCat
    CHECK_EQ(d.flag, 1);                          // promotable-pair head

    // GetCategoryByRank == reqCode byte.
    CHECK_EQ((int)OfficeGetCategoryByRank(1), 1);
    CHECK_EQ((int)OfficeGetCategoryByRank(10), 4);
    CHECK_EQ((int)OfficeGetCategoryByRank(22), 6);
    CHECK_EQ((int)OfficeGetCategoryByRank(28), 7);

    CHECK_EQ(OfficeGetDefinition(37, &d), 0); // out of range
}

// ---------------------------------------------------------------------------
// Candidacy + promotion + succession rules.
// ---------------------------------------------------------------------------
TEST(WorldOffice, CandidacyAndPromotion) {
    OfficeHolderTableReset();
    // Make slot 0 an open seat of office type 1 (city -1, state 3, rank 0).
    g_officeHolders[0].holder = 0;
    g_officeHolders[0].type   = 1;
    g_officeHolders[0].city   = -1;
    g_officeHolders[0].state  = 3;
    g_officeHolders[0].rank   = 0;

    OfficePerson p{};
    p.valid = true; p.ownerId = 500; p.officeType = 1; p.rank = 1; p.candidacy = 0;
    CHECK(OfficeCanRunForOffice(p));

    // Already a candidate -> cannot run.
    OfficePerson pc2 = p; pc2.candidacy = 1;
    CHECK(!OfficeCanRunForOffice(pc2));

    // Slot occupied (city != -1) -> cannot run.
    g_officeHolders[0].city = 5;
    CHECK(!OfficeCanRunForOffice(p));
    g_officeHolders[0].city = -1;

    // Promotion rule: requires bookCat(rank)+1 >= bookCat(target) AND
    // bookCat(officeType) < bookCat(target).  officeType 1 -> bookCat 1, rank 1
    // -> bookCat 1.  target rank 2 -> bookCat 2: 1+1>=2 ok, 1<2 ok -> promotable.
    // cost row=reqCode(officeType 1)=1, col=reqCode(target 2)=1 -> matrix[1][1]=0.
    float cost = 99.0f;
    OfficePerson promo{};
    promo.valid = true; promo.officeType = 1; promo.rank = 1;
    CHECK_EQ(OfficeCanPromoteRank(promo, 2, &cost), 1);
    CHECK(cost == 0.0f);

    // target rank 4 -> bookCat 4: bookCat(office 1)=1 < 4 ok, but
    // bookCat(rank 1)=1, 1+1 < 4 -> fails the adjacency gate -> rejected.
    CHECK_EQ(OfficeCanPromoteRank(promo, 4, &cost), 0);

    // target rank 0 -> rejected (a2 == 0).
    CHECK_EQ(OfficeCanPromoteRank(promo, 0, &cost), 0);
}

TEST(WorldOffice, SuccessorCandidates) {
    // IsNextRankInCategory: reqCode(rank)==reqCode(officeType) AND the bookCat%3
    // progression. Build a pool and verify only progression-valid people pass.
    // Target rank 4: reqCode 4, bookCat 4 (-> %3 == 1).
    // A candidate with officeType reqCode 4 and bookCat 6 (-> %3 == 0): t=1,o=0
    //   -> none of the OR branches -> NOT next.
    // bookCat 5 (-> %3 == 2): t=1,o=2 -> third branch true -> IS next.
    OfficePerson pool[3]{};
    pool[0].valid = true; pool[0].ownerId = 1; pool[0].officeType = 10; // reqCode 4, bookCat 4
    pool[1].valid = true; pool[1].ownerId = 2; pool[1].officeType = 13; // reqCode 4, bookCat 5
    pool[2].valid = true; pool[2].ownerId = 3; pool[2].officeType = 15; // reqCode 4, bookCat 6

    // Sanity: confirm each candidate's reqCode/bookCat assumptions via accessors.
    CHECK_EQ((int)OfficeGetCategoryByRank(10), 4);
    CHECK_EQ((int)OfficeGetCategoryByRank(13), 4);
    CHECK_EQ((int)OfficeGetCategoryByRank(15), 4);

    CHECK(OfficeIsNextRankInCategory(pool[1], 10)); // t=bookCat(10)%3=1, o=bookCat(13)%3=2
    CHECK(!OfficeIsNextRankInCategory(pool[2], 10)); // o=bookCat(15)%3=0 -> false for t=1

    i32 out[8];
    int n = OfficeCollectSuccessorCandidates(10, pool, 3, 6, out, 8);
    CHECK_EQ(n, 1);
    CHECK_EQ(out[0], 2); // only pool[1] progresses
}

TEST(WorldOffice, RankRequirements) {
    RankRequirements rq;
    // rank 1 -> reqCode 1 -> first tier.
    CHECK_EQ(OfficeGetRankRequirements(1, &rq), 1);
    CHECK_EQ((int)rq.ageMin, 16);
    CHECK_EQ((int)rq.ageMax, 28);
    CHECK_EQ(rq.moneyMin, 32000);
    CHECK_EQ(rq.moneyMax, 192000);
    // rank 22 -> reqCode 6 -> third tier.
    CHECK_EQ(OfficeGetRankRequirements(22, &rq), 1);
    CHECK_EQ(rq.moneyMin, 640000);
    CHECK_EQ(rq.moneyMax, 1600000);
    // rank 28 -> reqCode 7 -> default (no requirement block).
    CHECK_EQ(OfficeGetRankRequirements(28, &rq), 0);
}

// ---------------------------------------------------------------------------
// Privilege bit checks.
// ---------------------------------------------------------------------------
TEST(WorldPrivilege, BitChecks) {
    CHECK(PrivilegeHasFlag(0x104, 0x100));
    CHECK(!PrivilegeHasFlag(0x004, 0x100));
    CHECK(PrivilegeHasFlag(kPrivBitBlackmailImmune, kPrivBitBlackmailImmune));

    // Simple-cmd gate: immune bit set -> blocked.
    CHECK(PrivilegeSimpleCmdAllowed(0x0));
    CHECK(!PrivilegeSimpleCmdAllowed(0x4));
    CHECK(PrivilegeSimpleCmdAllowed(0x100)); // unrelated bit -> still allowed

    u8 cityPriv[11] = {0,1,0,0,5,0,0,0,0,0,1};
    CHECK(!CityPrivilegeEnabled(cityPriv, 0));
    CHECK(CityPrivilegeEnabled(cityPriv, 1));
    CHECK(CityPrivilegeEnabled(cityPriv, 4));
    CHECK(CityPrivilegeEnabled(cityPriv, 10));
    CHECK(!CityPrivilegeEnabled(cityPriv, 11)); // out of range
}

// ---------------------------------------------------------------------------
// Relation get/set (self == 127, asymmetric per the byte-addressed matrix).
// ---------------------------------------------------------------------------
TEST(WorldRelation, GetSetSelfAsymmetric) {
    RelationReset();

    // Self always 127, regardless of storage.
    CHECK_EQ(RelationGet(3, 3), kRelationSelf);
    CHECK_EQ(RelationGet(0, 0), kRelationSelf);

    // Default (unset) reads 0.
    CHECK_EQ(RelationGet(1, 2), 0);

    // Set a positive and a negative (signed high byte).
    RelationSet(1, 2, 40);
    CHECK_EQ(RelationGet(1, 2), 40);
    RelationSet(2, 1, -30);
    CHECK_EQ(RelationGet(2, 1), -30);

    // Asymmetric: (1,2) and (2,1) are distinct cells.
    CHECK_EQ(RelationGet(1, 2), 40);
    CHECK_EQ(RelationGet(2, 1), -30);

    // Setting self is a no-op (still reads 127).
    RelationSet(4, 4, 10);
    CHECK_EQ(RelationGet(4, 4), kRelationSelf);

    // Full signed range round-trips through the high byte.
    RelationSet(5, 6, 127);
    CHECK_EQ(RelationGet(5, 6), 127);
    RelationSet(5, 7, -128);
    CHECK_EQ(RelationGet(5, 7), -128);
}

// ===========================================================================
// HARDENING (wave-12): boundary + malformed-input tests for the law/crime
// cluster. These pin the OOB/UB fixes and the existing in-range guards. They
// run clean under -fsanitize=address,undefined.
// ===========================================================================

// --- law table (26-entry) index boundary -----------------------------------
TEST(LawHarden, GesetzGetRecordOutOfRange) {
    LawTableResetDefaults();
    LawRecord r;
    CHECK_EQ(GesetzGetRecord(25, &r), 1);   // last valid id
    CHECK_EQ(GesetzGetRecord(26, &r), 0);   // first past kLawCount
    CHECK_EQ(GesetzGetRecord(255, &r), 0);  // u8 max
}

TEST(LawHarden, EvaluateViolationBadId) {
    LawTableResetDefaults();
    PendingCrime out{};
    CHECK_EQ(GesetzEvaluateViolation(26, 0, 0, 0, 0, &out), kViolationBadId);
    CHECK_EQ(GesetzEvaluateViolation(255, 0, 0, 0, 0, nullptr), kViolationBadId);
}

// --- 51-entry law-TYPE descriptor table (dword_4C1810) boundary ------------
TEST(LawHarden, LawTypeTableIndexBoundary) {
    // kLawTypeCount == 51 (0x33). Every accessor gates on `< 0x33u`.
    CHECK_EQ(LawGetRecordIndex(50), 50);     // last valid law-type
    CHECK_EQ(LawGetRecordIndex(51), -1);     // first out of range
    CHECK_EQ(LawGetRecordIndex(0xFFFF), -1);

    CHECK_EQ(LawGetTextIdForType(51), 6662);     // fallback id
    CHECK_EQ(LawGetTextIdForType(0xFFFF), 6662);

    // Variant text id: out-of-range type returns the fallback unchanged.
    CHECK_EQ(LawGetVariantTextId(51, 3, -7), -7);
    CHECK_EQ(LawGetVariantTextId(0xFFFF, 0, 123), 123);
    // In-range, variant clamps to [0,9]; flag drives +6662 / +6672 base. type 0
    // has variant flag 1 (kLawTypeVariantFlag[0]==1) -> +6672 base.
    CHECK_EQ(LawGetVariantTextId(0, 100, 0), 9 + 6672); // variant clamped to 9
    CHECK_EQ(LawGetVariantTextId(0, 0, 0), 0 + 6672);
    // type 1 has flag 0 -> +6662 base.
    CHECK_EQ(LawGetVariantTextId(1, 2, 0), 2 + 6662);
}

TEST(LawHarden, PenaltyTextBadId) {
    LawTableResetDefaults();
    char buf[64] = {0};
    PenaltyText p = GesetzBuildPenaltyText(buf, 26, 0);  // lawId >= 26
    CHECK_EQ(p.valid, false);
    p = GesetzBuildPenaltyText(buf, 255, 0);
    CHECK_EQ(p.valid, false);
}

// --- crime / straftat record boundaries ------------------------------------
TEST(LawHarden, StraftatSetRecordStateIndexBound) {
    CrimeAndEvidenceReset();
    CHECK_EQ(StraftatSetRecordState(2, kCrimeCount), -2);       // index == 512
    CHECK_EQ(StraftatSetRecordState(2, 0xFFFFFFFFu), -2);       // huge index
    CHECK_EQ(StraftatSetRecordState(1, 0), -3);                 // state < 2
    CHECK_EQ(StraftatSetRecordState(2, kCrimeCount - 1), kCrimeCount - 1); // last
}

// BeweisCollectByOwner with a degenerate (zero / too-small) output buffer must
// not overrun — the do/while previously wrote out[0] before the bound check.
TEST(LawHarden, BeweisCollectZeroCapNoOverflow) {
    CrimeAndEvidenceReset();
    g_crimeTable[0].provenState = 7;
    g_crimeTable[1].provenState = 7;
    i32 dummy = -1;
    // outCapacity 0: nothing may be written, count stays 0.
    CHECK_EQ(BeweisCollectByOwner(7, &dummy, 0), 0);
    CHECK_EQ(dummy, -1);                         // untouched
    // outCapacity 1: at most one entry written.
    i32 one[1] = {-1};
    CHECK_EQ(BeweisCollectByOwner(7, one, 1), 1);
    CHECK_EQ(one[0], 0);
    // Valid (large) capacity collects both matches as before.
    i32 big[32];
    CHECK_EQ(BeweisCollectByOwner(7, big, 32), 2);
    CHECK_EQ(big[0], 0);
    CHECK_EQ(big[1], 1);
}

// --- Gesetz save/load malformed-blob hardening -----------------------------
namespace {
// Build a load header: lawCount, 26 thresholds, marker, crimeCount, evidenceCount.
std::vector<guild::u8> GesetzLoadHeader(i32 lawCount, i32 crimeCount,
                                        i32 evidenceCount) {
    std::vector<guild::u8> b;
    auto put = [&](const void* s, std::size_t n) {
        const guild::u8* p = static_cast<const guild::u8*>(s);
        for (std::size_t i = 0; i < n; ++i) b.push_back(p[i]);
    };
    put(&lawCount, 4);
    for (int i = 0; i < 26; ++i) { i32 t = 0; put(&t, 4); }
    i32 marker = 0; put(&marker, 4);
    put(&crimeCount, 4);
    put(&evidenceCount, 4);
    return b;
}
} // namespace

TEST(LawHarden, GesetzLoadRejectsOversizeCrimeCount) {
    LawTableResetDefaults();
    CrimeAndEvidenceReset();
    auto b = GesetzLoadHeader(26, /*crimeCount*/ kCrimeCount + 88, /*ev*/ 0);
    for (int i = 0; i < kCrimeCount + 88; ++i) { guild::u8 r[45] = {0}; b.insert(b.end(), r, r + 45); }
    GesetzStream s{b.data(), b.size(), 0};
    // Previously a global-buffer-overflow into g_crimeTable[512]; now rejected.
    CHECK_EQ(GesetzLoadState(s, 0x10041u), 0);
}

TEST(LawHarden, GesetzLoadRejectsNegativeCounts) {
    LawTableResetDefaults();
    CrimeAndEvidenceReset();
    {
        auto b = GesetzLoadHeader(26, -1, 0);
        GesetzStream s{b.data(), b.size(), 0};
        CHECK_EQ(GesetzLoadState(s, 0x10041u), 0);
    }
    {
        auto b = GesetzLoadHeader(26, 0, -7);
        GesetzStream s{b.data(), b.size(), 0};
        CHECK_EQ(GesetzLoadState(s, 0x10041u), 0);
    }
}

TEST(LawHarden, GesetzLoadRejectsOversizeEvidenceCount) {
    LawTableResetDefaults();
    CrimeAndEvidenceReset();
    auto b = GesetzLoadHeader(26, 0, /*evidence*/ kEvidenceCapacity + 100);
    for (int i = 0; i < kEvidenceCapacity + 100; ++i) {
        i32 pair[2] = {0, 0};
        b.insert(b.end(), reinterpret_cast<guild::u8*>(pair),
                 reinterpret_cast<guild::u8*>(pair) + 8);
    }
    GesetzStream s{b.data(), b.size(), 0};
    // Previously a global-buffer-overflow into g_evidence*[4096]; now rejected.
    CHECK_EQ(GesetzLoadState(s, 0x10041u), 0);
}

TEST(LawHarden, GesetzLoadRejectsTruncatedAndBadHeader) {
    LawTableResetDefaults();
    CrimeAndEvidenceReset();
    // Truncated header (only 3 bytes).
    {
        guild::u8 b[3] = {26, 0, 0};
        GesetzStream s{b, sizeof(b), 0};
        CHECK_EQ(GesetzLoadState(s, 0x10041u), 0);
    }
    // Wrong lawCount.
    {
        auto b = GesetzLoadHeader(99, 0, 0);
        GesetzStream s{b.data(), b.size(), 0};
        CHECK_EQ(GesetzLoadState(s, 0x10041u), 0);
    }
    // Null stream.
    {
        GesetzStream s{nullptr, 0, 0};
        CHECK_EQ(GesetzLoadState(s, 0x10041u), 0);
    }
}

// At-capacity counts (the boundary) still load successfully — the fix only
// rejects counts strictly PAST capacity, preserving the valid envelope. We build
// the at-capacity blob with SaveState (so the on-disk record stride matches the
// reader exactly) by marking every crime / evidence slot active, then load it
// back into a scrubbed table.
TEST(LawHarden, GesetzLoadAcceptsExactCapacityCounts) {
    LawTableResetDefaults();
    CrimeAndEvidenceReset();
    // Fill every crime slot (active == id != -1) and every evidence pair so the
    // save emits exactly kCrimeCount crimes + kEvidenceCapacity pairs.
    for (int i = 0; i < kCrimeCount; ++i) {
        g_crimeTable[i].id          = i + 1;
        g_crimeTable[i].perpetrator = i;
        g_crimeTable[i].provenState = 1;
    }
    for (int i = 0; i < kEvidenceCapacity; ++i) {
        g_evidenceOwner[2 * i]   = i + 1;
        g_evidenceCrimeId[2 * i] = i + 1;
    }

    std::vector<guild::u8> buf(1u << 20, 0);  // 1 MiB scratch
    GesetzStream ws{buf.data(), buf.size(), 0};
    CHECK_EQ(GesetzSaveState(ws), 1);
    const std::size_t written = ws.pos;

    // Scrub the tables, then load the at-capacity blob back.
    CrimeAndEvidenceReset();
    GesetzStream rs{buf.data(), written, 0};
    CHECK_EQ(GesetzLoadState(rs, 0x10041u), 1);   // exact capacity accepted
    CHECK_EQ(g_crimeTable[kCrimeCount - 1].id, kCrimeCount);
    CHECK_EQ(g_evidenceOwner[2 * (kEvidenceCapacity - 1)], kEvidenceCapacity);
}

// WAVE-16: the LoadState clear loop (0x4c2af4) sets, per uncleared record:
//   id(+0)=-1, +18 dword=-1, perpetrator(+22)=-1, provenState(+37)=0,
//   wanted(+26 u16)=0  — NOTE it clears +18, NOT target(+33). This pins the
//   corrected field set against the binary.
TEST(LawHarden, GesetzLoadClearLoopMatchesBinaryFields) {
    LawTableResetDefaults();
    CrimeAndEvidenceReset();
    // Poison record 5 so we can see exactly which fields the clear loop touches.
    {
        guild::u8* p = reinterpret_cast<guild::u8*>(&g_crimeTable[5]);
        for (int i = 0; i < kCrimeStride; ++i) p[i] = 0x7E;
    }
    // Load a header declaring just 1 crime + 0 evidence; records 1..511 are cleared.
    auto b = GesetzLoadHeader(26, /*crimeCount*/ 1, /*evidence*/ 0);
    // Append exactly one crime record (all zero) for the single declared crime.
    for (int i = 0; i < kCrimeStride; ++i) b.push_back(0);
    GesetzStream s{b.data(), b.size(), 0};
    CHECK_EQ(GesetzLoadState(s, 0x10041u), 1);

    const guild::u8* p = reinterpret_cast<const guild::u8*>(&g_crimeTable[5]);
    i32 id, f18, perp, st; guild::u16 wanted, f33lo;
    std::memcpy(&id, p + 0, 4);
    std::memcpy(&f18, p + 18, 4);
    std::memcpy(&perp, p + 22, 4);
    std::memcpy(&wanted, p + 26, 2);
    std::memcpy(&st, p + 37, 4);
    std::memcpy(&f33lo, p + 33, 2);
    CHECK_EQ(id, -1);          // +0
    CHECK_EQ(f18, -1);         // +18 (binary clears this, not +33)
    CHECK_EQ(perp, -1);        // +22
    CHECK_EQ((int)wanted, 0);  // +26
    CHECK_EQ(st, 0);           // +37
    // +33 (target) is NOT cleared by the loop: the poison 0x7E bytes remain.
    CHECK_EQ((int)f33lo, 0x7E7E);
}

// ===========================================================================
// Wave-14 1:1 value pins (W14-LAW). These lock the recovered table BYTES and
// the field decode for the WHOLE 26-entry law table, not just the spot-checks
// above. Every byte traces to kLawTableDefault in src/world/law.cpp (recovered
// via get_bytes from unk_631E98 @0x631E98); no value is invented.
// ===========================================================================

// The exact in-memory contents of g_lawTable after LawTableResetDefaults().
// This is the 26 x 36-byte image as the engine sees it: the C aggregate
// initializer in law.cpp zero-fills any short record to the 36-byte stride
// (see the record-1 drift pin below). Generated from law.cpp, byte-for-byte.
static const guild::u8 kLawTableGolden[kLawCount][kLawStride] = {
    {0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x18,0x01,0x00,0x00,0x60,0x46,0x46,0x00},
    {0x01,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x02,0x00,0x00,0xf8,0x47,0x46,0x00},
    {0x02,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x06,0x00,0x00,0x00,0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1a,0x01,0x00,0x00,0x30,0x4a,0x46,0x00},
    {0x03,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x06,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x12,0x01,0x00,0x00,0xd4,0x4a,0x46,0x00},
    {0x04,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x0a,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x16,0x01,0x00,0x00,0x00,0x4c,0x46,0x00},
    {0x05,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x0e,0x01,0x00,0x00,0x0c,0x4d,0x46,0x00},
    {0x06,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x0a,0x01,0x00,0x00,0x84,0x4d,0x46,0x00},
    {0x07,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x0b,0x01,0x00,0x00,0xdc,0x4d,0x46,0x00},
    {0x08,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0a,0x00,0x00,0x00,0x0f,0x01,0x00,0x00,0x4c,0x4e,0x46,0x00},
    {0x09,0x00,0x00,0x00,0x06,0x00,0x00,0x00,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0a,0x00,0x00,0x00,0x0f,0x02,0x00,0x00,0x5c,0x50,0x46,0x00},
    {0x0a,0x00,0x00,0x00,0x06,0x00,0x00,0x00,0x0a,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x15,0x01,0x00,0x00,0xa8,0x52,0x46,0x00},
    {0x0b,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x0b,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x0c,0x01,0x00,0x00,0xc0,0x53,0x46,0x00},
    {0x0c,0x00,0x00,0x00,0x0a,0x00,0x00,0x00,0x19,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x0e,0x02,0x00,0x00,0xa4,0x54,0x46,0x00},
    {0x0d,0x00,0x00,0x00,0x0a,0x00,0x00,0x00,0x19,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x0a,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x10,0x01,0x00,0x00,0x88,0x55,0x46,0x00},
    {0x0e,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x19,0x01,0x00,0x00,0x6c,0x56,0x46,0x00},
    {0x0f,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0d,0x01,0x00,0x00,0xb0,0x57,0x46,0x00},
    {0x10,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x19,0x02,0x00,0x00,0xf8,0x58,0x46,0x00},
    {0x11,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x12,0x01,0x00,0x00,0xe0,0x59,0x46,0x00},
    {0x12,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x13,0x01,0x00,0x00,0xc8,0x5a,0x46,0x00},
    {0x13,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x13,0x02,0x00,0x00,0xc0,0x5b,0x46,0x00},
    {0x14,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x11,0x01,0x00,0x00,0xcc,0x5c,0x46,0x00},
    {0x15,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x14,0x01,0x00,0x00,0xd8,0x5d,0x46,0x00},
    {0x16,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x17,0x01,0x00,0x00,0xfc,0x5e,0x46,0x00},
    {0x17,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x16,0x02,0x00,0x00,0x20,0x60,0x46,0x00},
    {0x18,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x17,0x02,0x00,0x00,0x38,0x61,0x46,0x00},
    {0x19,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x14,0x02,0x00,0x00,0x5c,0x62,0x46,0x00},
};

// Pin every byte of every record via the public accessor GesetzGetRecord.
TEST(WorldLawW14, FullLawTableByteExact) {
    LawTableResetDefaults();
    for (int id = 0; id < kLawCount; ++id) {
        LawRecord r;
        std::memset(&r, 0xAB, sizeof(r));
        CHECK_EQ(GesetzGetRecord(static_cast<guild::u8>(id), &r), 1);
        const guild::u8* got = reinterpret_cast<const guild::u8*>(&r);
        for (int b = 0; b < kLawStride; ++b)
            CHECK_EQ((int)got[b], (int)kLawTableGolden[id][b]);
    }
}

// Pin the decoded (id, penalty, op, threshold) for ALL 26 records — these are
// the only fields the evaluator/penalty maths touch. Values decoded from
// kLawTableGolden (penalty=i16@+16, op=u8@+20, threshold=i32@+24).
TEST(WorldLawW14, AllRecordFieldDecode) {
    LawTableResetDefaults();
    // {id, penalty, op, threshold} from the recovered bytes.
    struct Exp { int penalty; int op; int threshold; };
    static const Exp exp[kLawCount] = {
        {  0, 1,    2}, // id 0
        {  0, 0,    0}, // id 1  (WAVE-16: corrected to the binary's 36-byte record)
        {  6, 7,    0}, // id 2
        {  6, 1,    0}, // id 3
        { 10, 1,    0}, // id 4
        {  0, 0,    2}, // id 5
        {  0, 0,    2}, // id 6
        {  0, 0,    2}, // id 7
        {  0, 0,   10}, // id 8
        {  0, 0,   10}, // id 9
        {  0, 0,    8}, // id 10
        {  0, 0,    8}, // id 11
        {  0, 0,   16}, // id 12
        { 10, 5,   16}, // id 13
        {  0, 0,    2}, // id 14
        { 12, 1,    0}, // id 15
        { 12, 1,    0}, // id 16
        { 12, 1,    0}, // id 17
        { 12, 1,    0}, // id 18
        { 12, 1,    0}, // id 19
        { 12, 1,    1}, // id 20
        { 12, 1,    1}, // id 21
        { 12, 1,    1}, // id 22
        { 12, 1,    1}, // id 23
        { 12, 1,    1}, // id 24
        { 12, 1,    1}, // id 25
    };
    for (int id = 0; id < kLawCount; ++id) {
        LawRecord r;
        CHECK_EQ(GesetzGetRecord(static_cast<guild::u8>(id), &r), 1);
        CHECK_EQ((int)r.id, id);
        CHECK_EQ((int)r.penalty, exp[id].penalty);
        CHECK_EQ((int)r.op, exp[id].op);
        CHECK_EQ((int)r.threshold, exp[id].threshold);
    }
}

// WAVE-16 FIX (was Record1IsShortAndZeroPaddedDrift): the wave-14 pass flagged
// law record id=1 as initialized with only 32 bytes in src/world/law.cpp, so the
// aggregate zero-fill shifted its trailing string pointer to +28 and corrupted
// the op/threshold decode. With live MCP we read get_bytes(0x631E98, 936) and
// confirmed record 1 IS a full 36-byte record:
//   01 02 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
//   00 00 00 00 18 02 00 00 f8 47 46 00
// The 4 missing zero bytes (at +24..27) were restored in law.cpp. This test now
// pins that record 1 carries its 0x004647f8 string pointer at +32..35 like every
// other record (no drift), and that the load-bearing fields decode to 0.
TEST(WorldLawW14, Record1Is36ByteCorrect) {
    LawTableResetDefaults();
    LawRecord r0, r1;
    CHECK_EQ(GesetzGetRecord(0, &r0), 1);
    CHECK_EQ(GesetzGetRecord(1, &r1), 1);
    const guild::u8* p0 = reinterpret_cast<const guild::u8*>(&r0);
    CHECK_EQ((int)p0[34], 0x46);   // record 0 ptr high bytes intact at +32..35
    CHECK_EQ((int)p0[35], 0x00);
    // Record 1 now has its string pointer 0x004647f8 at +32..35 (NO drift).
    const guild::u8* p1 = reinterpret_cast<const guild::u8*>(&r1);
    CHECK_EQ((int)p1[32], 0xf8);
    CHECK_EQ((int)p1[33], 0x47);
    CHECK_EQ((int)p1[34], 0x46);
    CHECK_EQ((int)p1[35], 0x00);
    // The +24..27 dword (the 4 restored zero bytes) is 0; op/threshold decode 0.
    CHECK_EQ((int)p1[24], 0x00);
    CHECK_EQ((int)r1.op, 0);
    CHECK_EQ((int)r1.threshold, 0);
}

// The 51-entry law-TYPE +4 variant-flag column is recovered independently in two
// places: world/law_text.cpp (kLawTypeVariantFlag) and sim/command_recon4_senders
// .cpp (kLawActionTable[].flag, the full 0x4C1810 dump). Pin that they agree
// byte-for-byte (the brief's explicit cross-check), so a future edit to either
// recovery cannot drift silently.
TEST(WorldLawW14, VariantFlagMatchesSimLawActionTable) {
    CHECK_EQ((int)guild::sim::kLawActionCount, kLawTypeCount);
    for (int i = 0; i < kLawTypeCount; ++i)
        CHECK_EQ((int)kLawTypeVariantFlag[i],
                 (int)guild::sim::kLawActionTable[i].flag);
}

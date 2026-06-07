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
#include "crt/rand.h"

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

    // Law 13: op==Greater, threshold 16. value 20 (>16) -> satisfied (no match);
    // value 10 (<=16) -> violation.
    CHECK_EQ(GesetzEvaluateViolation(13, 20, 0, 0, 0, &pc), (int)kViolationNoMatch);
    CHECK_EQ(GesetzEvaluateViolation(13, 10, 0, 0, 0, &pc), (int)kViolationQueued);

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

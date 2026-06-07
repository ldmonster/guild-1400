// End-to-end flow for the world legal/political system:
//   stage a crime -> attach evidence -> evaluate the law (seeded RNG) ->
//   record/prove the crime -> run an office succession over a small population.
// Outcomes are checked against a hand-computed reference.
#include "tests/framework/test.h"

#include "world/law.h"
#include "world/crime.h"
#include "world/office.h"
#include "world/relation.h"
#include "crt/rand.h"

using namespace guild;
using namespace guild::world;

// Hand-computed reference for the law-evaluation step:
//   Law 4: op==Equal (1), threshold 0, penalty 10.  We feed value 0 -> violation.
//   Guard sum 0 -> maxWanted 0.  Seeded srand(1): RandNext()=16838 -> float
//   0.5138... >= 0 -> CAUGHT -> queued.  (See python golden in the unit test.)
TEST(WorldLawE2E, CrimeEvidenceLawSuccession) {
    LawTableResetDefaults();
    CrimeAndEvidenceReset();
    OfficeHolderTableReset();
    RelationReset();

    // --- 1. Evaluate a law violation with the seeded CRT RNG. ----------------
    GesetzSetGuardStationSumFn([](i32) { return 0; }); // no guards -> maxWanted 0
    GesetzSetRandomFloatFn(nullptr);                   // use guild::crt::RandNext
    crt::Srand(1);

    PendingCrime pc;
    const i32 perp = 50, victim = 11;
    int outcome = GesetzEvaluateViolation(/*lawId*/4, /*value*/0, victim, perp,
                                          /*extra*/3, &pc);
    CHECK_EQ(outcome, (int)kViolationQueued);
    CHECK_EQ(pc.perpetrator, perp);
    CHECK_EQ(pc.victim, victim);
    CHECK_EQ((int)pc.penalty, 10); // law 4 penalty
    CHECK_EQ(pc.extra, 3);

    // --- 2. Commit the pending crime into the table. -------------------------
    int slot = StraftatFindFreeSlot();
    CHECK_EQ(slot, 0);
    const i32 crimeId = 7001;
    g_crimeTable[slot].id          = crimeId;
    g_crimeTable[slot].perpetrator = pc.perpetrator;
    g_crimeTable[slot].wanted      = 1;
    g_crimeTable[slot].provenState = 2; // pending (occupies the slot)

    CHECK_EQ(StraftatFindIndexById(crimeId), 0);

    // --- 3. Attach two pieces of evidence (two witnesses). -------------------
    CHECK_EQ(BeweisAdd(crimeId, /*owner witness A*/7, 0), 1);
    CHECK_EQ(BeweisAdd(crimeId, /*owner witness B*/8, 0), 1);
    CHECK_EQ(BeweisExistsForPair(7, crimeId), 1);
    CHECK_EQ(BeweisExistsForPair(8, crimeId), 1);
    // A duplicate witness is ignored.
    CHECK_EQ(BeweisAdd(crimeId, 7, 0), 0);

    // --- 4. Prove the crime, then resolve it (clears record + evidence). -----
    // SetRecordState requires state >= 2; use it to move the record to a pending
    // judged state (3), then mark the proven value (1) the resolve path checks.
    CHECK_EQ(StraftatSetRecordState(/*state*/3, /*index*/0), 0); // returns index
    CHECK_EQ(g_crimeTable[0].provenState, 3);
    g_crimeTable[0].provenState = 1; // proven (the cleared-path requires == 1)
    // wanted 1 -> 0 and proven -> cleared (return 0).
    CHECK_EQ(StraftatResolveAndClear(crimeId, 0), 0);
    CHECK_EQ(g_crimeTable[0].id, -1);
    CHECK_EQ(BeweisExistsForPair(7, crimeId), 0);
    CHECK_EQ(BeweisExistsForPair(8, crimeId), 0);

    // --- 5. Office succession over a small population. -----------------------
    // The vacated office is rank 10 (reqCode 4, bookCat 4 -> %3 == 1).
    // Candidates' eligibility (IsNextRankInCategory) requires reqCode 4 AND a
    // bookCat whose %3 pairs with the target's per the recovered rule:
    //   t = bookCat(target=10)%3 = 1; candidate qualifies iff o = bookCat%3 == 2.
    // Hand-picked pool:
    //   A: officeType 13 (reqCode 4, bookCat 5 -> %3 2)  -> ELIGIBLE
    //   B: officeType 15 (reqCode 4, bookCat 6 -> %3 0)  -> not eligible
    //   C: officeType 10 (reqCode 4, bookCat 4 -> %3 1)  -> not eligible
    //   D: officeType  1 (reqCode 1) wrong book           -> not eligible
    OfficePerson pool[4]{};
    pool[0].valid = true; pool[0].ownerId = 201; pool[0].officeType = 13;
    pool[1].valid = true; pool[1].ownerId = 202; pool[1].officeType = 15;
    pool[2].valid = true; pool[2].ownerId = 203; pool[2].officeType = 10;
    pool[3].valid = true; pool[3].ownerId = 204; pool[3].officeType = 1;

    i32 successors[8];
    int n = OfficeCollectSuccessorCandidates(/*targetRank*/10, pool, 4, 6,
                                             successors, 8);
    CHECK_EQ(n, 1);
    CHECK_EQ(successors[0], 201); // candidate A is the sole eligible successor

    // --- 6. Record the succession outcome in the relation matrix. ------------
    // The new office holder (201) gains standing with the victim (11); store and
    // read back, verifying the asymmetric byte-addressed cell + self sentinel.
    RelationSet(201 % kRelationDim, 11, 25);
    CHECK_EQ(RelationGet(201 % kRelationDim, 11), 25);
    CHECK_EQ(RelationGet(11, 11), kRelationSelf);

    GesetzSetGuardStationSumFn(nullptr);
    GesetzSetRandomFloatFn(nullptr);
}

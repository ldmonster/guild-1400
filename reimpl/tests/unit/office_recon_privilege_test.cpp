// Golden-vector unit tests for office_recon_privilege.{h,cpp} — the pure
// privilege/office decision predicates and session controls reconstructed 1:1
// from gilde.exe. Self-contained; no game assets required.
#include "tests/framework/test.h"
#include "world/office_recon_privilege.h"

using namespace guild;
using namespace guild::world;

// --- Privilege flag / category gates --------------------------------------

TEST(OfficeReconPriv, LawScrollGate) {
    // +457 bit2 set -> blocked; clear -> allowed.
    CHECK(PrivLawScrollAllowed(0x00));
    CHECK(PrivLawScrollAllowed(0x02));   // other bits don't matter
    CHECK(!PrivLawScrollAllowed(0x04));
    CHECK(!PrivLawScrollAllowed(0xFF));
    // text variant: (+9 != 0) + 6504
    CHECK_EQ(PrivLawScrollTextId(0), 6504);
    CHECK_EQ(PrivLawScrollTextId(1), 6505);
    CHECK_EQ(PrivLawScrollTextId(200), 6505);
}

TEST(OfficeReconPriv, CharmGate) {
    // Same office category -> refused; different -> allowed.
    CHECK(!PrivCharmAllowed(3, 3));
    CHECK(PrivCharmAllowed(3, 4));
    CHECK(PrivCharmAllowed(0, 7));
    // refusal text: status-low non-zero -> 6579, else 6578
    CHECK_EQ(PrivCharmRefusalTextId(0), 6578);
    CHECK_EQ(PrivCharmRefusalTextId(1), 6579);
}

// --- Blackmail -------------------------------------------------------------

TEST(OfficeReconPriv, BlackmailSubjectGate) {
    CHECK(PrivBlackmailSubjectOk(true, true, 6));
    CHECK(PrivBlackmailSubjectOk(true, true, 7));
    CHECK(!PrivBlackmailSubjectOk(true, true, 5));   // wrong kind
    CHECK(!PrivBlackmailSubjectOk(false, true, 6));  // no subject
    CHECK(!PrivBlackmailSubjectOk(true, false, 6));  // no target
}

TEST(OfficeReconPriv, BlackmailRoll) {
    // success iff roll <= matchCount  (roll == RandomModulo(8) in 0..7)
    CHECK(PrivBlackmailRollSucceeds(0, 0));   // equal -> success
    CHECK(PrivBlackmailRollSucceeds(3, 5));
    CHECK(PrivBlackmailRollSucceeds(5, 5));   // equal boundary
    CHECK(!PrivBlackmailRollSucceeds(6, 5));  // roll > matches -> fail
    CHECK(!PrivBlackmailRollSucceeds(7, 0));
}

TEST(OfficeReconPriv, BlackmailFollowupAndResult) {
    CHECK(PrivBlackmailHoldsOffice(0x100));
    CHECK(PrivBlackmailHoldsOffice(0x1FF));
    CHECK(!PrivBlackmailHoldsOffice(0x0FF));
    CHECK(!PrivBlackmailHoldsOffice(0));
    CHECK_EQ(PrivBlackmailResult(true), 1);
    CHECK_EQ(PrivBlackmailResult(false), 0);
}

// --- Shared confirm/cancel dispatch ----------------------------------------

TEST(OfficeReconPriv, PanelDispatchCodes) {
    CHECK_EQ(kPrivBtnConfirm, 1210);
    CHECK_EQ(kPrivBtnCancel, 1155);
    CHECK_EQ(PrivPanelConfirmState(PrivPanel::BlackmailConfirm), 1);
    CHECK_EQ(PrivPanelConfirmState(PrivPanel::LawScroll), 3);
    CHECK_EQ(PrivPanelConfirmState(PrivPanel::InstillFear), 4);
    CHECK_EQ(PrivPanelConfirmState(PrivPanel::Charm), 4);
    CHECK_EQ(kPrivPanelCancelState, 2);
}

// --- Evidence detail panel + BuildEvidenceEntry ----------------------------

TEST(OfficeReconPriv, EvidencePrechecks) {
    CHECK(PrivEvidenceHasTarget(0));
    CHECK(PrivEvidenceHasTarget(1234));
    CHECK(!PrivEvidenceHasTarget(0xFFFF));
    CHECK_EQ(PrivEvidencePrecheck(0), kPrivEvidenceNoTarget); // 96
    CHECK_EQ(PrivEvidencePrecheck(-3), 96);
    CHECK_EQ(PrivEvidencePrecheck(1), 0);
    CHECK_EQ(PrivEvidencePrecheck(7), 0);
}

TEST(OfficeReconPriv, EvidenceAggregation) {
    i32 none[3] = {0, 2, 3};
    i32 some[4] = {0, 1, 2, 5};
    CHECK(!PrivEvidenceAnyActionable(none, 3));
    CHECK(PrivEvidenceAnyActionable(some, 4));
    CHECK(!PrivEvidenceAnyActionable(some, 0)); // empty count
    // self-target suppresses the confirm regardless of evidence
    CHECK(PrivEvidenceConfirmEnabled(true, false));
    CHECK(!PrivEvidenceConfirmEnabled(true, true));
    CHECK(!PrivEvidenceConfirmEnabled(false, false));
}

TEST(OfficeReconPriv, BuildEvidenceReturnCodes) {
    // No judge -> always 16
    CHECK_EQ(PrivBuildEvidenceResult(false, false), kPrivEvidenceBuilt); // 16
    CHECK_EQ(PrivBuildEvidenceResult(false, true), 16);
    // Judge present: witnesses found -> 16, else 64
    CHECK_EQ(PrivBuildEvidenceResult(true, true), 16);
    CHECK_EQ(PrivBuildEvidenceResult(true, false), kPrivEvidenceNoWitness); // 64
}

// --- Candidate list paging -------------------------------------------------

TEST(OfficeReconPriv, CandidatePaging) {
    CHECK(!PrivCandidateListHasRows(0));
    CHECK(PrivCandidateListHasRows(1));
    CHECK_EQ(kCandidateRowStride, 112);
    CHECK_EQ(kCandidateRowX, 75);
    CHECK_EQ(PrivCandidateRowY(0), 40);
    CHECK_EQ(PrivCandidateRowY(1), 152);
    CHECK_EQ(PrivCandidateRowY(3), 3 * 112 + 40);
}

// --- Session timer split ---------------------------------------------------

TEST(OfficeReconPriv, SessionTimeSplit) {
    CHECK_EQ(kOfficeSessionActorSlots, 16);
    // elapsed = 14 * (now - start).  Pick now-start so 14*delta = 125_000 ms
    // = 2 min : 5 sec : 0 ms.   delta such that 14*delta == 125000 is not int;
    // use raw ms domain: choose now-start = 0 -> all zero.
    SessionTimeSplit z = OfficeSessionTimeSplit(100, 100);
    CHECK_EQ(z.minutes, 0); CHECK_EQ(z.seconds, 0); CHECK_EQ(z.millis, 0);
    // delta = 5000 -> elapsed = 70000 ms = 1:10:0
    SessionTimeSplit a = OfficeSessionTimeSplit(5000, 0);
    CHECK_EQ(a.minutes, 1);
    CHECK_EQ(a.seconds, 10);
    CHECK_EQ(a.millis, 0);
    // delta = 1 -> elapsed = 14 ms = 0:0:14
    SessionTimeSplit b = OfficeSessionTimeSplit(1, 0);
    CHECK_EQ(b.minutes, 0);
    CHECK_EQ(b.seconds, 0);
    CHECK_EQ(b.millis, 14);
    // delta = 100000 -> elapsed = 1_400_000 ms = 23:20:0
    SessionTimeSplit c = OfficeSessionTimeSplit(100000, 0);
    CHECK_EQ(c.minutes, 23);
    CHECK_EQ(c.seconds, 20);
    CHECK_EQ(c.millis, 0);
}

// --- Session actor teardown ------------------------------------------------

static int g_destroyCount = 0;
static i32 g_lastHandle = -1;
static void CountDestroy(i32 h, void*) { ++g_destroyCount; g_lastHandle = h; }

TEST(OfficeReconPriv, DestroySessionActors_All) {
    i32 actors[kOfficeSessionActorSlots];
    for (int i = 0; i < kOfficeSessionActorSlots; ++i) actors[i] = 1000 + i;
    g_destroyCount = 0; g_lastHandle = -1;
    int n = OfficeDestroySessionActors(actors, /*abort*/false, CountDestroy, nullptr);
    CHECK_EQ(n, 16);
    CHECK_EQ(g_destroyCount, 16);
    CHECK_EQ(g_lastHandle, 1015);
}

TEST(OfficeReconPriv, DestroySessionActors_NullSlotsSkipped) {
    i32 actors[kOfficeSessionActorSlots] = {0};
    actors[2] = 77;
    actors[9] = 88;
    g_destroyCount = 0;
    int n = OfficeDestroySessionActors(actors, false, CountDestroy, nullptr);
    CHECK_EQ(n, 2);            // only the two non-null slots destroyed
    CHECK_EQ(g_destroyCount, 2);
}

TEST(OfficeReconPriv, DestroySessionActors_AbortDestroysNothing) {
    i32 actors[kOfficeSessionActorSlots];
    for (int i = 0; i < kOfficeSessionActorSlots; ++i) actors[i] = 1 + i;
    g_destroyCount = 0;
    // abort flag set at entry -> the `|| dword_6315BC` term skips every slot.
    int n = OfficeDestroySessionActors(actors, /*abort*/true, CountDestroy, nullptr);
    CHECK_EQ(n, 0);
    CHECK_EQ(g_destroyCount, 0);
}

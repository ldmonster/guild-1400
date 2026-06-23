// Unit tests for the world law/office FLOW core:
//   office assign/transfer/swap holder-table mutation, apply-for-candidacy +
//   add-table-entry command emit, law apply/notify + save/load, privilege
//   command dispatch, crime sync + accusation broadcast.
// References are the recovered tables and hand-computed reference states.
#include "tests/framework/test.h"

#include <map>

#include "world/office.h"
#include "world/office_assign.h"
#include "world/gesetz_flow.h"
#include "world/privilege_cmd.h"
#include "world/straftat_sync.h"
#include "world/law.h"
#include "world/crime.h"
#include "crt/rand.h"

using namespace guild;
using namespace guild::world;

// ---------------------------------------------------------------------------
// A simple person store for the office-flow tests.
// ---------------------------------------------------------------------------
namespace {
struct MapStore : OfficePersonStore {
    std::map<i32, OfficePersonRec> recs;
    OfficePersonRec* Find(i32 id) override {
        if (id == -1) return nullptr;
        auto it = recs.find(id);
        return it == recs.end() ? nullptr : &it->second;
    }
    OfficePersonRec& add(i32 id) {
        OfficePersonRec r;
        r.ownerId = id;
        r.valid = true;
        recs[id] = r;
        return recs[id];
    }
};
} // namespace

// ===========================================================================
// AssignToCandidate.
// ===========================================================================
TEST(WorldOfficeFlow, AssignToCandidateSingleSlot) {
    OfficeHolderTableReset();
    OfficeCommandLogReset();
    MapStore st;
    OfficePersonRec& cand = st.add(100);
    cand.office360 = 0;

    // Slot 0: holder key 7, type 5, open (city=-1, state=3, rank<4).
    g_officeHolders[0].holder = 7;
    g_officeHolders[0].type   = 5;
    g_officeHolders[0].city   = -1;
    g_officeHolders[0].state  = 3;
    g_officeHolders[0].rank   = 1;

    AssignRequest req{100, 7, 0xFF, 5};
    CHECK_EQ(OfficeAssignToCandidate(req, st), 1);
    // Candidacy flag set on the person, slot rank bumped.
    CHECK_EQ((int)st.recs[100].office360, 5);
    CHECK_EQ(g_officeHolders[0].rank, 2);
}

TEST(WorldOfficeFlow, AssignToCandidateRejectsAlreadyCandidate) {
    OfficeHolderTableReset();
    MapStore st;
    OfficePersonRec& cand = st.add(100);
    cand.office360 = 3; // already standing
    g_officeHolders[0].holder = 7;
    g_officeHolders[0].type   = 5;
    g_officeHolders[0].city   = -1;
    g_officeHolders[0].state  = 3;
    AssignRequest req{100, 7, 0xFF, 5};
    CHECK_EQ(OfficeAssignToCandidate(req, st), 0);
}

TEST(WorldOfficeFlow, AssignToCandidateRejectsTypeMismatch) {
    OfficeHolderTableReset();
    MapStore st;
    st.add(100);
    g_officeHolders[0].holder = 7;
    g_officeHolders[0].type   = 5;
    g_officeHolders[0].city   = -1;
    g_officeHolders[0].state  = 3;
    AssignRequest req{100, 7, 0xFF, 9}; // officeType 9 != slot type 5
    CHECK_EQ(OfficeAssignToCandidate(req, st), 0);
}

TEST(WorldOfficeFlow, AssignToCandidateRejectsOccupiedSlot) {
    OfficeHolderTableReset();
    MapStore st;
    st.add(100);
    g_officeHolders[0].holder = 7;
    g_officeHolders[0].type   = 5;
    g_officeHolders[0].city   = 42; // occupied
    g_officeHolders[0].state  = 3;
    AssignRequest req{100, 7, 0xFF, 5};
    CHECK_EQ(OfficeAssignToCandidate(req, st), 0);
}

// ===========================================================================
// ApplyForCandidacy — emits a candidacy command (build-op 68).
// ===========================================================================
TEST(WorldOfficeFlow, ApplyForCandidacyEmitsCommand) {
    OfficeHolderTableReset();
    OfficeCommandLogReset();

    // Office type 1 (def flag for type 1 is non-zero per kOfficeDefTable).
    // Slot 0 holds type 1, open. (office.cpp default table: record 1 flag == 1.)
    g_officeHolders[0].holder = 11;
    g_officeHolders[0].type   = 1;
    g_officeHolders[0].city   = -1;
    g_officeHolders[0].state  = 3;
    g_officeHolders[0].rank   = 0;

    OfficePersonRec applicant;
    applicant.ownerId = 200;
    applicant.valid = true;
    applicant.office360 = 0;

    int r = OfficeApplyForCandidacy(applicant, 1);
    CHECK(r >= 1);
    int n = 0;
    const OfficeCommand* log = OfficeCommandLog(&n);
    CHECK(n >= 1);
    if (n >= 1) {
        CHECK_EQ(log[0].opcode, 68);
        CHECK_EQ(log[0].personId, 200);
        CHECK_EQ((int)log[0].holderA, 11);
        CHECK_EQ((int)log[0].officeType, 1);
    }
}

TEST(WorldOfficeFlow, ApplyForCandidacyRejectsAlreadyStanding) {
    OfficeHolderTableReset();
    OfficeCommandLogReset();
    OfficePersonRec applicant;
    applicant.ownerId = 200;
    applicant.valid = true;
    applicant.office360 = 1; // already a candidate
    CHECK_EQ(OfficeApplyForCandidacy(applicant, 1), -1);
}

// ===========================================================================
// AddTableEntry — finds slot, emits build-op 69.
// ===========================================================================
TEST(WorldOfficeFlow, AddTableEntryEmits) {
    OfficeHolderTableReset();
    OfficeCommandLogReset();
    g_officeHolders[3].holder = 55;

    OfficePersonRec primary;
    primary.ownerId = 300; primary.valid = true;

    int r = OfficeAddTableEntry(55, &primary, 7, nullptr, 1);
    CHECK_EQ(r, 0);
    int n = 0;
    const OfficeCommand* log = OfficeCommandLog(&n);
    CHECK_EQ(n, 1);
    if (n == 1) {
        CHECK_EQ(log[0].opcode, 69);
        CHECK_EQ(log[0].personId, 300);
        CHECK_EQ(log[0].secondaryId, -1);
        CHECK_EQ((int)log[0].holderA, 55);
        CHECK_EQ((int)log[0].officeType, 7);
        CHECK_EQ((int)log[0].state, 1);
    }
}

TEST(WorldOfficeFlow, AddTableEntryMissingSlot) {
    OfficeHolderTableReset();
    OfficeCommandLogReset();
    OfficePersonRec primary; primary.ownerId = 300; primary.valid = true;
    // holder key 200 not present in any slot.
    CHECK_EQ(OfficeAddTableEntry(200, &primary, 7, nullptr, 1), -1);
}

// ===========================================================================
// TransferHoldership — installs incoming holder into the slot.
// ===========================================================================
TEST(WorldOfficeFlow, TransferInstallsHolder) {
    OfficeHolderTableReset();
    OfficeNotifyLogReset();
    MapStore st;
    OfficePersonRec& incoming = st.add(500);
    incoming.office358 = 0;

    // Slot 2: holder key 20, currently vacant.
    g_officeHolders[2].holder    = 20;
    g_officeHolders[2].type      = 5;   // normal office (<= 0x1B)
    g_officeHolders[2].city      = -1;
    g_officeHolders[2].secondary = -1;
    g_officeHolders[2].state     = 3;
    g_officeHolders[2].rank      = 0;

    // Transfer: holderKey 20, new primary owner = person 500, seated = 500.
    TransferRequest req{20, /*primaryId*/500, /*state*/2, /*seatedId*/500};
    CHECK_EQ(OfficeTransferHoldership(req, st), 1);

    // The slot now owned by 500, state 2.
    CHECK_EQ(g_officeHolders[2].city, 500);
    CHECK_EQ(g_officeHolders[2].secondary, 500);
    CHECK_EQ((int)g_officeHolders[2].state, 2);
    // Incoming person's held office-type updated to the slot type.
    CHECK_EQ((int)st.recs[500].office358, 5);
}

// ===========================================================================
// SwapHolders — swaps the two slots' owners + the seated persons' office types.
// ===========================================================================
TEST(WorldOfficeFlow, SwapHolders) {
    OfficeHolderTableReset();
    OfficeNotifyLogReset();
    MapStore st;
    OfficePersonRec& initiator = st.add(1);
    initiator.money = 100000;
    OfficePersonRec& pA = st.add(10);
    OfficePersonRec& pB = st.add(20);
    pA.office358 = 1;
    pB.office358 = 2;

    // Slot 0: key 30, owner = person 10, type 1.
    g_officeHolders[0].holder = 30;
    g_officeHolders[0].city   = 10;
    g_officeHolders[0].type   = 1;
    // Slot 1: key 31, owner = person 20, type 2.
    g_officeHolders[1].holder = 31;
    g_officeHolders[1].city   = 20;
    g_officeHolders[1].type   = 2;

    SwapRequest req{1, 30, 31, 5000};
    CHECK_EQ(OfficeSwapHolders(req, st), 1);

    // Owners swapped.
    CHECK_EQ(g_officeHolders[0].city, 20);
    CHECK_EQ(g_officeHolders[1].city, 10);
    // Seated persons' office types took the OTHER slot's type.
    CHECK_EQ((int)st.recs[10].office358, 2); // pA <- slot B type
    CHECK_EQ((int)st.recs[20].office358, 1); // pB <- slot A type
    // Initiator charged the cost.
    CHECK_EQ(st.recs[1].money, 95000);

    // Notification emitted.
    int n = 0;
    const OfficeNotify* log = OfficeNotifyLog(&n);
    CHECK_EQ(n, 1);
    if (n == 1) CHECK_EQ((int)log[0].kind, (int)OfficeNotify::Swap);
}

TEST(WorldOfficeFlow, SwapHoldersRejectsInsufficientMoney) {
    OfficeHolderTableReset();
    MapStore st;
    OfficePersonRec& initiator = st.add(1);
    initiator.money = 100; // < cost
    g_officeHolders[0].holder = 30; g_officeHolders[0].city = 10;
    SwapRequest req{1, 30, 31, 5000};
    CHECK_EQ(OfficeSwapHolders(req, st), 0);
}

// ===========================================================================
// ReleaseCharacterHoldings — vacates owned slots, clears office fields.
// ===========================================================================
TEST(WorldOfficeFlow, ReleaseHoldings) {
    OfficeHolderTableReset();
    OfficePersonRec person;
    person.ownerId = 77;
    person.office358 = 5;
    person.office360 = 0;
    person.office361 = 6;

    g_officeHolders[4].city = 77; // owned slot
    g_officeHolders[4].state = 2;
    g_officeHolders[4].rank = 3;
    g_officeHolders[6].secondary = 77; // secondary holding

    int changed = OfficeReleaseCharacterHoldings(person, /*vacantStateBig*/false);
    CHECK(changed != 0);
    CHECK_EQ(g_officeHolders[4].city, -1);
    CHECK_EQ((int)g_officeHolders[4].state, 3); // vacantState (not big)
    CHECK_EQ(g_officeHolders[4].rank, 0);
    CHECK_EQ(g_officeHolders[6].secondary, -1);
    CHECK_EQ((int)g_officeHolders[6].state, 1);
    CHECK_EQ((int)person.office358, 0);
    CHECK_EQ((int)person.office361, 0);
}

// ===========================================================================
// Gesetz RequestApply — clamp + emit.
// ===========================================================================
TEST(WorldGesetzFlow, RequestApplyClampsAndEmits) {
    LawTableResetDefaults();
    GesetzCommandLogReset();

    // Law 0: +4 (lo) == 0, +8 (hi) == 4 (from kLawTableDefault row 0).
    GesetzPerson p;
    p.ownerId = 9; p.valid = true; p.present = true;

    // value 100 clamps to hi (4).
    CHECK_EQ(GesetzRequestApply(0, 100, p), 0);
    int n = 0;
    const GesetzCommand* log = GesetzCommandLog(&n);
    CHECK_EQ(n, 1);
    if (n == 1) {
        CHECK_EQ(log[0].opcode, 70);
        CHECK_EQ(log[0].masterId, 9);
        CHECK_EQ((int)log[0].lawId, 0);
        CHECK_EQ(log[0].value, 4); // clamped to hi
    }
}

TEST(WorldGesetzFlow, RequestApplyNullPersonUsesMinusOne) {
    LawTableResetDefaults();
    GesetzCommandLogReset();
    GesetzPerson p; // present == false -> id -1
    CHECK_EQ(GesetzRequestApply(0, -5, p), 0); // value -5 clamps to lo (0)
    int n = 0;
    const GesetzCommand* log = GesetzCommandLog(&n);
    CHECK_EQ(n, 1);
    if (n == 1) {
        CHECK_EQ(log[0].masterId, -1);
        CHECK_EQ(log[0].value, 0);
    }
}

TEST(WorldGesetzFlow, RequestApplyBadLawId) {
    LawTableResetDefaults();
    GesetzPerson p; p.present = true; p.valid = true; p.ownerId = 1;
    CHECK_EQ(GesetzRequestApply(26, 0, p), -1);
}

// ===========================================================================
// Gesetz ApplyAndNotify — mutates the law table threshold + notifies.
// ===========================================================================
TEST(WorldGesetzFlow, ApplyAndNotifyMutatesAndNotifies) {
    LawTableResetDefaults();
    GesetzNotifyLogReset();

    GesetzApplyCmd cmd{777, 5, 42};
    // initiator 777 != local master 1, and it resolves -> notify.
    CHECK_EQ(GesetzApplyAndNotify(cmd, /*localMaster*/1, /*resolves*/true), 1);
    CHECK_EQ(g_lawTable[5].threshold, 42);

    int n = 0;
    const GesetzNotifyEvent* log = GesetzNotifyLog(&n);
    CHECK_EQ(n, 1);
    if (n == 1) {
        CHECK_EQ(log[0].masterId, 777);
        CHECK_EQ((int)log[0].lawId, 5);
        CHECK_EQ(log[0].newThreshold, 42);
    }
}

TEST(WorldGesetzFlow, ApplyAndNotifyLocalMasterNoNotify) {
    LawTableResetDefaults();
    GesetzNotifyLogReset();
    GesetzApplyCmd cmd{1, 5, 99};
    CHECK_EQ(GesetzApplyAndNotify(cmd, /*localMaster*/1, true), 1); // initiator==master
    CHECK_EQ(g_lawTable[5].threshold, 99); // table still mutated
    int n = 0;
    GesetzNotifyLog(&n);
    CHECK_EQ(n, 0); // no notify
}

// ===========================================================================
// Gesetz save/load roundtrip.
// ===========================================================================
TEST(WorldGesetzFlow, SaveLoadRoundtrip) {
    LawTableResetDefaults();
    CrimeAndEvidenceReset();
    // The binary's free-slot id is -1 (the save path counts active by id != -1).
    for (int i = 0; i < kCrimeCount; ++i) g_crimeTable[i].id = -1;

    // Set distinctive law thresholds.
    for (int i = 0; i < kLawCount; ++i)
        g_lawTable[i].threshold = i * 7 + 1;
    // One active crime + one evidence pair.
    g_crimeTable[0].id = 1234;
    g_crimeTable[0].perpetrator = 88;
    g_crimeTable[0].wanted = 3;
    g_crimeTable[0].target = 9;
    g_crimeTable[0].provenState = 1;
    g_evidenceOwner[0] = 5;
    g_evidenceCrimeId[0] = 1234;

    static u8 buf[8192];
    GesetzStream ws{buf, sizeof(buf), 0};
    CHECK_EQ(GesetzSaveState(ws), 1);
    std::size_t written = ws.pos;

    // Scramble, then load back.
    for (int i = 0; i < kLawCount; ++i) g_lawTable[i].threshold = -999;
    CrimeAndEvidenceReset();

    GesetzStream rs{buf, written, 0};
    CHECK_EQ(GesetzLoadState(rs, /*formatVersion*/0x10041u), 1);

    for (int i = 0; i < kLawCount; ++i)
        CHECK_EQ(g_lawTable[i].threshold, i * 7 + 1);
    CHECK_EQ(g_crimeTable[0].id, 1234);
    CHECK_EQ(g_crimeTable[0].perpetrator, 88);
    CHECK_EQ((int)g_crimeTable[0].wanted, 3);
    CHECK_EQ(g_crimeTable[0].provenState, 1);
    CHECK_EQ(g_evidenceOwner[0], 5);
    CHECK_EQ(g_evidenceCrimeId[0], 1234);
}

// ===========================================================================
// Privilege command dispatch.
// ===========================================================================
TEST(WorldPrivilegeCmd, SimpleCmdConcreteTargetEmits) {
    PrivilegeCommandLogReset();
    guild::crt::Srand(1);

    PrivilegeDispatch d{};
    d.subjectKind = 0; // concrete-target path
    d.actorId = 11;
    d.targetResolves = true;
    d.targetId = 22;

    int low = 0, high = 0;
    CHECK_EQ(PrivilegeSendSimpleCmd(d, &low, &high), kPrivRetCommitted);
    CHECK(high >= 6518 && high <= 6526);
    CHECK(low >= 6509 && low <= 6517);
    int n = 0;
    const PrivilegeCommand* log = PrivilegeCommandLog(&n);
    CHECK_EQ(n, 1);
    if (n == 1) {
        CHECK_EQ(log[0].opcode, 90);
        CHECK_EQ(log[0].actorId, 11);
        CHECK_EQ(log[0].targetId, 22);
    }
}

TEST(WorldPrivilegeCmd, SimpleCmdNoTarget) {
    PrivilegeCommandLogReset();
    PrivilegeDispatch d{};
    d.subjectKind = 0;
    d.targetResolves = false;
    CHECK_EQ(PrivilegeSendSimpleCmd(d, nullptr, nullptr), kPrivRetNoTarget);
}

TEST(WorldPrivilegeCmd, SimpleCmdOfficeImmune) {
    PrivilegeDispatch d{};
    d.subjectKind = 6;
    d.officeHolderPicked = true;
    d.officeHolderFlags457 = 0x4; // immune
    CHECK_EQ(PrivilegeSendSimpleCmd(d, nullptr, nullptr), kPrivRetImmune);
    d.officeHolderFlags457 = 0x0;
    CHECK_EQ(PrivilegeSendSimpleCmd(d, nullptr, nullptr), kPrivRetSimpleOffice);
}

TEST(WorldPrivilegeCmd, BuildCmdConcreteTarget) {
    PrivilegeCommandLogReset();
    PrivilegeDispatch d{};
    d.subjectKind = 0;
    d.actorId = 3;
    d.targetResolves = true;
    d.targetId = 4;
    CHECK_EQ(PrivilegeSendBuildCmd(d), kPrivRetCommitted);
    int n = 0;
    const PrivilegeCommand* log = PrivilegeCommandLog(&n);
    CHECK_EQ(n, 1);
    if (n == 1) {
        CHECK_EQ(log[0].op, -2);
        CHECK_EQ(log[0].targetId, 4);
    }
}

TEST(WorldPrivilegeCmd, BuildCmdOfficeOverview) {
    PrivilegeDispatch d{};
    d.subjectKind = 7;
    d.overviewShown = true;
    CHECK_EQ(PrivilegeSendBuildCmd(d), kPrivRetBuildOffice);
    d.overviewShown = false;
    CHECK_EQ(PrivilegeSendBuildCmd(d), kPrivRetBuildCancel);
}

// ===========================================================================
// Crime sync + accusation broadcast.
// ===========================================================================
TEST(WorldStraftatSync, SyncAllByProvenState) {
    CrimeAndEvidenceReset();
    StraftatSyncLogReset();
    StraftatSetHandlerExistsFn(nullptr); // default: no handler

    g_crimeTable[0].id = 10; g_crimeTable[0].provenState = 1; // proven -> flag 0
    g_crimeTable[1].id = 11; g_crimeTable[1].provenState = 5; // pending -> flag 1
    g_crimeTable[2].id = 12; g_crimeTable[2].provenState = 0; // free -> skip

    int emitted = StraftatSyncAllToNetwork();
    CHECK_EQ(emitted, 2);
    int n = 0;
    const StraftatSyncCommand* log = StraftatSyncLog(&n);
    CHECK_EQ(n, 2);
    if (n == 2) {
        CHECK_EQ(log[0].crimeId, 10); CHECK_EQ(log[0].flag, 0);
        CHECK_EQ(log[1].crimeId, 11); CHECK_EQ(log[1].flag, 1);
    }
}

TEST(WorldStraftatSync, BroadcastAccusationDeliversWithEvidence) {
    CrimeAndEvidenceReset();
    StraftatAccusationLogReset();

    g_crimeTable[0].id = 4321; g_crimeTable[0].perpetrator = 99;
    g_crimeTable[0].provenState = 1;
    // Evidence: owner 50 holds evidence for crime 4321.
    g_evidenceOwner[0] = 50; g_evidenceCrimeId[0] = 4321;

    AccusationRecipient recips[2];
    recips[0] = AccusationRecipient{6, 50, 0};  // office holder, has evidence
    recips[1] = AccusationRecipient{6, 60, 0};  // office holder, no evidence

    int delivered = StraftatBroadcastAccusation(
        /*crimeId*/4321, /*ownerId*/7, /*mask*/0, recips, 2,
        /*perpetratorId*/99, /*perpetratorResolves*/true);
    CHECK_EQ(delivered, 1);
    int n = 0;
    const AccusationMessage* log = StraftatAccusationLog(&n);
    CHECK_EQ(n, 1);
    if (n == 1) {
        CHECK_EQ(log[0].recipientOwnerId, 50);
        CHECK_EQ(log[0].crimeId, 4321);
        CHECK_EQ(log[0].perpetratorId, 99);
    }
}

// WAVE-16: pin the binary's flagField mask-out side effect (0x4c382b):
// dword_12CEAF4[134 * v8] &= v12, where v8 == the matching evidence-pair index.
TEST(WorldStraftatSync, BroadcastAccusationMasksRecipientByPairIndex) {
    CrimeAndEvidenceReset();
    StraftatAccusationLogReset();
    g_crimeTable[0].id = 4321; g_crimeTable[0].perpetrator = 99;
    g_crimeTable[0].provenState = 1;
    // The matching evidence pair sits at slot index 2 (e==4 -> v8 == 2), so the
    // recipient masked is recipients[2], NOT the delivering recipient.
    g_evidenceOwner[4] = 50; g_evidenceCrimeId[4] = 4321;

    AccusationRecipient recips[3];
    recips[0] = AccusationRecipient{6, 50, 0};      // delivers (has evidence)
    recips[1] = AccusationRecipient{6, 60, 0};      // no evidence
    recips[2] = AccusationRecipient{6, 70, 0x0F};   // gets its flagField &= mask

    int delivered = StraftatBroadcastAccusation(
        4321, /*ownerId*/7, /*mask*/0x06, recips, 3, 99, true);
    CHECK_EQ(delivered, 1);
    // recipients[v8=2].flagField &= 0x06  -> 0x0F & 0x06 == 0x06.
    CHECK_EQ(recips[2].flagField, 0x06);
    // The delivering recipient (index 0) is NOT the one masked.
    CHECK_EQ(recips[0].flagField, 0);
}

TEST(WorldStraftatSync, BroadcastAccusationSkipsOwnerAndMasked) {
    CrimeAndEvidenceReset();
    StraftatAccusationLogReset();
    g_crimeTable[0].id = 4321; g_crimeTable[0].perpetrator = 99;
    g_evidenceOwner[0] = 50; g_evidenceCrimeId[0] = 4321;

    AccusationRecipient recips[2];
    recips[0] = AccusationRecipient{6, 50, 0xFF}; // mask&flag != 0 -> skip
    recips[1] = AccusationRecipient{6, 7, 0};      // ownerId == crime owner -> skip

    int delivered = StraftatBroadcastAccusation(
        4321, /*ownerId*/7, /*mask*/0xFF, recips, 2, 99, true);
    CHECK_EQ(delivered, 0);
}

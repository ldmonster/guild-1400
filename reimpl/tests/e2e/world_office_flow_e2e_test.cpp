// End-to-end flow across the law/office/privilege/crime modules:
//   1. assign an office to a candidate,
//   2. transfer the office to a new holder,
//   3. apply a law change (mutate the law table + notify),
//   4. grant a privilege (issue a privilege command),
//   5. sync the crime table to the network,
// then verify the holder / law / privilege / sync state against a hand-computed
// reference.
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

namespace {
struct E2EStore : OfficePersonStore {
    std::map<i32, OfficePersonRec> recs;
    OfficePersonRec* Find(i32 id) override {
        if (id == -1) return nullptr;
        auto it = recs.find(id);
        return it == recs.end() ? nullptr : &it->second;
    }
    OfficePersonRec& add(i32 id) {
        OfficePersonRec r; r.ownerId = id; r.valid = true; recs[id] = r;
        return recs[id];
    }
};
} // namespace

TEST(WorldOfficeFlowE2E, FullLawOfficeFlow) {
    // --- fresh world ---
    OfficeHolderTableReset();
    OfficeCommandLogReset();
    OfficeNotifyLogReset();
    LawTableResetDefaults();
    CrimeAndEvidenceReset();
    GesetzNotifyLogReset();
    PrivilegeCommandLogReset();
    StraftatSyncLogReset();
    StraftatSetHandlerExistsFn(nullptr);
    guild::crt::Srand(42);

    E2EStore st;

    // Office slot 1: holder key 21, office type 5, open & electable.
    g_officeHolders[1].holder    = 21;
    g_officeHolders[1].type      = 5;
    g_officeHolders[1].city      = -1;
    g_officeHolders[1].secondary = -1;
    g_officeHolders[1].state     = 3;
    g_officeHolders[1].rank      = 0;

    // --- step 1: assign the office to candidate person 1000 ---
    OfficePersonRec& cand = st.add(1000);
    cand.office360 = 0;
    AssignRequest assign{1000, 21, 0xFF, 5};
    CHECK_EQ(OfficeAssignToCandidate(assign, st), 1);
    // Reference: candidacy flag = office type 5, slot rank bumped 0 -> 1.
    CHECK_EQ((int)st.recs[1000].office360, 5);
    CHECK_EQ(g_officeHolders[1].rank, 1);

    // --- step 2: transfer the office to a new holder person 2000 ---
    OfficePersonRec& newHolder = st.add(2000);
    newHolder.office358 = 0;
    TransferRequest xfer{/*holderKey*/21, /*primaryId*/2000, /*state*/2,
                         /*seatedId*/2000};
    CHECK_EQ(OfficeTransferHoldership(xfer, st), 1);
    // Reference: slot 1 now owned by 2000, state 2, type-5 office on the person.
    CHECK_EQ(g_officeHolders[1].city, 2000);
    CHECK_EQ((int)g_officeHolders[1].state, 2);
    CHECK_EQ((int)st.recs[2000].office358, 5);

    // --- step 3: apply a law change (law 7 threshold -> 13) ---
    // First the request (clamp + emit), then the apply that mutates the table.
    GesetzCommandLogReset();
    GesetzPerson initiator; initiator.ownerId = 2000; initiator.valid = true;
    initiator.present = true;
    // Law 7: +4 (lo) and +8 (hi); kLawTableDefault row 7 has lo=0, hi=4. Request a
    // big value -> clamps to hi (4). Verify the emitted enact value.
    CHECK_EQ(GesetzRequestApply(7, 1000, initiator), 0);
    int gn = 0;
    const GesetzCommand* glog = GesetzCommandLog(&gn);
    CHECK_EQ(gn, 1);
    i32 enactValue = (gn == 1) ? glog[0].value : -1;
    CHECK_EQ(enactValue, 4); // hand-computed clamp

    // The command is acked -> ApplyAndNotify writes the threshold.
    GesetzApplyCmd applyCmd{2000, 7, enactValue};
    CHECK_EQ(GesetzApplyAndNotify(applyCmd, /*localMaster*/9999, /*resolves*/true), 1);
    CHECK_EQ(g_lawTable[7].threshold, 4);
    int nn = 0;
    GesetzNotifyLog(&nn);
    CHECK_EQ(nn, 1); // initiator != local master -> notified

    // --- step 4: grant a privilege (issue a privilege command on a concrete target) ---
    PrivilegeDispatch d{};
    d.subjectKind = 0;       // concrete-target path
    d.actorId = 2000;        // the new office holder acting
    d.targetResolves = true;
    d.targetId = 1000;       // act on the former candidate
    int low = 0, high = 0;
    CHECK_EQ(PrivilegeSendSimpleCmd(d, &low, &high), kPrivRetCommitted);
    int pn = 0;
    const PrivilegeCommand* plog = PrivilegeCommandLog(&pn);
    CHECK_EQ(pn, 1);
    if (pn == 1) {
        CHECK_EQ(plog[0].opcode, 90);
        CHECK_EQ(plog[0].actorId, 2000);
        CHECK_EQ(plog[0].targetId, 1000);
        CHECK_EQ(plog[0].textIdLow, low);
        CHECK_EQ(plog[0].textIdHigh, high);
    }

    // --- step 5: register a crime and sync the table ---
    g_crimeTable[0].id = 5000; g_crimeTable[0].provenState = 1; // proven
    g_crimeTable[1].id = 5001; g_crimeTable[1].provenState = 4; // pending
    int emitted = StraftatSyncAllToNetwork();
    CHECK_EQ(emitted, 2);
    int sn = 0;
    const StraftatSyncCommand* slog = StraftatSyncLog(&sn);
    CHECK_EQ(sn, 2);
    if (sn == 2) {
        CHECK_EQ(slog[0].crimeId, 5000); CHECK_EQ(slog[0].flag, 0);
        CHECK_EQ(slog[1].crimeId, 5001); CHECK_EQ(slog[1].flag, 1);
    }

    // --- final reference state ---
    CHECK_EQ(g_officeHolders[1].city, 2000);   // office transferred
    CHECK_EQ(g_lawTable[7].threshold, 4);       // law applied
    CHECK_EQ(pn, 1);                            // one privilege command
    CHECK_EQ(emitted, 2);                       // two crimes synced
}

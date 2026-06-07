// End-to-end flow across the office / privilege / law cores:
//   1. eligibility: collect an elective office slot whose holder resolves
//   2. grant a privilege action against the resolved target (build-op command)
//   3. request a law apply (clamp + enact command emit)
//   4. apply-and-notify (law-table threshold mutation + master notify)
#include "test.h"

#include "world/law_apply.h"
#include "world/office.h"
#include "world/gesetz_flow.h"
#include "world/privilege_cmd.h"
#include "world/law.h"

#include <cstring>

using namespace guild;
using namespace guild::world;

namespace {

struct E2ePerson { i32 id; u8 office358; u8 busy433; };
E2ePerson g_e2ePeople[8];
int       g_e2eCount = 0;

OfficePersonRecord E2eResolve(i32 id, void*) {
    OfficePersonRecord r{};
    if (id == -1) return r;
    for (int i = 0; i < g_e2eCount; ++i)
        if (g_e2ePeople[i].id == id) {
            r.present = true;
            r.office358 = g_e2ePeople[i].office358;
            r.busy433 = g_e2ePeople[i].busy433;
            return r;
        }
    return r;
}

void SetHolder(int idx, u8 holderId, i32 city, u8 type, i32 rank, u8 state, i32 secondary) {
    OfficeHolder& h = g_officeHolders[idx];
    std::memset(&h, 0, sizeof(h));
    h.holder = holderId; h.city = city; h.type = type;
    h.rank = rank; h.state = state; h.secondary = secondary;
}

} // namespace

TEST(LawApplyE2E, AssignGrantApplyNotifyFlow) {
    // ---- setup --------------------------------------------------------------
    OfficeHolderTableReset();
    OfficeSetPersonResolver(&E2eResolve, nullptr);
    g_e2eCount = 0;
    g_e2ePeople[g_e2eCount++] = {7000, /*office358*/ 28, 0}; // the office holder
    g_e2ePeople[g_e2eCount++] = {7001, 0, 0};                // a target person

    // Seed the law table: RequestApply clamps the enact value into the record's
    // +4 (lo) / +8 (hi) dwords (v5[1]/v5[2]). ApplyAndNotify writes the new
    // threshold into +0x18.
    std::memset(&g_lawTable[3], 0, sizeof(LawRecord));
    {
        i32 lo = 2, hi = 40;
        std::memcpy(reinterpret_cast<u8*>(&g_lawTable[3]) + 4, &lo, 4);  // clamp lo
        std::memcpy(reinterpret_cast<u8*>(&g_lawTable[3]) + 8, &hi, 4);  // clamp hi
    }

    GesetzCommandLogReset();
    GesetzNotifyLogReset();
    PrivilegeCommandLogReset();

    // ---- 1) eligibility: an elective office slot with a live holder ---------
    // Elective types are 28..34 (reqCode 7). Slot 31 holds an elective office
    // whose +4 id is the live holder 7000.
    SetHolder(31, 9, /*id*/ 7000, /*type*/ 28, 0, 3, -1);
    OfficeHolder elective[8];
    int ne = OfficeCollectElectiveOffices(/*allowVacant*/ 0, 8, elective);
    CHECK_EQ(ne, 1);
    CHECK_EQ((int)elective[0].type, 28);
    CHECK_EQ(elective[0].city, 7000); // the +4 holder id we resolve against

    // ---- 2) grant privilege: build-op command against the target -----------
    PrivilegeDispatch d{};
    d.subjectKind   = 0;            // concrete-target path (not 6/7 overview)
    d.actorId       = 7000;
    d.targetResolves = true;
    d.targetId      = 7001;
    int rc = PrivilegeSendBuildCmd(d);
    CHECK_EQ(rc, 16);              // emitted
    int pcmdN = 0;
    const PrivilegeCommand* pcmds = PrivilegeCommandLog(&pcmdN);
    CHECK_EQ(pcmdN, 1);
    CHECK_EQ(pcmds[0].opcode, 90);
    CHECK_EQ(pcmds[0].actorId, 7000);
    CHECK_EQ(pcmds[0].targetId, 7001);

    // ---- 3) request law apply: clamp + enact emit --------------------------
    GesetzPerson initiator{};
    initiator.ownerId = 7000;
    initiator.valid   = true;
    initiator.present = true;
    int ra = GesetzRequestApply(/*lawId*/ 3, /*value*/ 100, initiator);
    CHECK_EQ(ra, 0);              // emit path
    int gcmdN = 0;
    const GesetzCommand* gcmds = GesetzCommandLog(&gcmdN);
    CHECK_EQ(gcmdN, 1);
    CHECK_EQ(gcmds[0].opcode, 70);
    CHECK_EQ((int)gcmds[0].lawId, 3);
    // value 100 clamped to [2, 40] -> 40.
    CHECK_EQ(gcmds[0].value, 40);

    // ---- 4) apply-and-notify: mutate threshold + notify the master ---------
    GesetzApplyCmd apply{};
    apply.initiatorId  = 7000;       // initiator != localMaster -> notify fires
    apply.lawId        = 3;
    apply.newThreshold = gcmds[0].value;
    int an = GesetzApplyAndNotify(apply, /*localMasterId*/ 9999,
                                  /*initiatorResolves*/ true);
    CHECK_EQ(an, 1);
    CHECK_EQ(g_lawTable[3].threshold, 40);   // table mutated
    int nN = 0;
    const GesetzNotifyEvent* notes = GesetzNotifyLog(&nN);
    CHECK_EQ(nN, 1);
    CHECK_EQ(notes[0].masterId, 7000);
    CHECK_EQ((int)notes[0].lawId, 3);
    CHECK_EQ(notes[0].newThreshold, 40);
}

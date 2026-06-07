#include "test.h"

#include <cstring>
#include <vector>

#include "crt/rand.h"
#include "world/guild_assignment.h"
#include "world/guild_election.h"   // real sibling: GuildSuccessorPick / delta tables
#include "world/amt_slot_table.h"   // real sibling: AmtSlot search primitives

using namespace guild;
using namespace guild::world;

// Cross-module: drive AssignGuildMembers (orchestrator) over real
// ComputeGuildAssignment, and cross-check the successor tally against the real
// guild_election.cpp GuildSuccessorPick tie-break; also place slots through
// AssignSlotData and read them back with the real amt_slot_table search leaves.

namespace {

struct IState {
    std::vector<std::pair<i32, GuildPersonRec>> people;
    std::vector<GuildOfficeObject> objects;
    int approachReturn = 1;
    i32 pickReturn = -1;
    std::vector<std::tuple<u8, i32, u8>> installs; // key, person, state

    // category -> holders (CollectByCategory mock)
    std::vector<std::pair<u8, std::vector<OfficeHolder>>> cats;

    GuildPersonRec* find(i32 id) {
        for (auto& p : people) if (p.first == id) return &p.second;
        return nullptr;
    }
};

IState* g_st = nullptr;

GuildPersonRec IFind(i32 id, void*) {
    auto* p = g_st->find(id); return p ? *p : GuildPersonRec{};
}
int IApproach(i32, i32, i32, void*) { return g_st->approachReturn; }
i32 IPick(i32, u8, void*) { return g_st->pickReturn; }
bool IIsNextRank(i32, u8, void*) { return true; }
void IRelation(i32, i32, i32, void*) {}
void IInstall(u8 k, i32 p, u8 st, void*) { g_st->installs.push_back({k, p, st}); }

int ICollect(u8 catKey, OfficeHolder* out, int maxOut, void*) {
    for (auto& c : g_st->cats) {
        if (c.first == catKey) {
            int n = static_cast<int>(c.second.size());
            if (n > maxOut) n = maxOut;
            for (int i = 0; i < n; ++i) out[i] = c.second[i];
            return n;
        }
    }
    return 0;
}

GuildAssignContext IMakeCtx(IState& s) {
    GuildAssignContext gx;
    gx.findPerson = &IFind; gx.approachDir = &IApproach; gx.pickTarget = &IPick;
    gx.isNextRank = &IIsNextRank; gx.relation = &IRelation; gx.install = &IInstall;
    gx.objects = s.objects.empty() ? nullptr : s.objects.data();
    gx.objectCount = static_cast<int>(s.objects.size());
    gx.ctx = &s;
    return gx;
}

OfficeHolder Mk(u8 key, i32 primary, u8 type, i32 succ, u8 state, i32 sec) {
    OfficeHolder h{};
    h.holder = key; h.city = primary; h.type = type;
    h.rank = succ; h.state = state; h.secondary = sec;
    return h;
}

} // namespace

// ===========================================================================
// AssignGuildMembers latches a master when one is present in a category.
// ===========================================================================
TEST(GuildAssignIT, OrchestratorLatchesMaster) {
    IState s; g_st = &s;
    // category key 34 (the 6th in the recovered table) gets a state-3 entry whose
    // object is a non-busy master (role 6) -> bit1 set -> latch.
    s.objects.push_back({777, 50, /*role*/6, /*busy*/0});
    std::vector<OfficeHolder> cat34;
    cat34.push_back(Mk(1, 0, 50, /*succ*/1, /*state*/3, 0));
    const GuildCategoryRule* tbl = GuildCategoryTable();
    s.cats.push_back({tbl[5].categoryKey, cat34}); // last category key

    GuildAssignContext gx = IMakeCtx(s);
    int latch = AssignGuildMembers(&ICollect, gx);
    CHECK_EQ(latch, 1);
}

TEST(GuildAssignIT, OrchestratorRunsAssignmentWhenNoMaster) {
    IState s; g_st = &s;
    // a member-present category with NO master -> ComputeGuildAssignment runs and
    // installs the primary (WIN branch, approach 1).
    GuildPersonRec eligible{}; eligible.valid = true; eligible.eligible = true;
    eligible.hasBuilding = true;
    s.people.push_back({100, eligible});
    GuildPersonRec sec{}; sec.valid = true; sec.hasBuilding = true;
    s.people.push_back({200, sec});
    s.approachReturn = 1;

    std::vector<OfficeHolder> cat;
    cat.push_back(Mk(7, 100, 34, 0, /*state*/2, 200));
    const GuildCategoryRule* tbl = GuildCategoryTable();
    s.cats.push_back({tbl[0].categoryKey, cat});

    GuildAssignContext gx = IMakeCtx(s);
    int latch = AssignGuildMembers(&ICollect, gx);
    CHECK_EQ(latch, 0);
    // ComputeGuildAssignment fired an install for the primary (state 1) — among the
    // AssignGuildMembers member-classification installs OR the assignment install.
    bool sawPrimaryInstall = false;
    for (auto& t : s.installs)
        if (std::get<1>(t) == 100 && std::get<2>(t) == 1) sawPrimaryInstall = true;
    CHECK(sawPrimaryInstall);
}

// ===========================================================================
// Cross-check: ComputeGuildAssignment's successor tally agrees with the real
// guild_election.cpp GuildSuccessorPick tie-break on the same tallies.
// ===========================================================================
TEST(GuildAssignIT, SuccessorTallyAgreesWithElectionPick) {
    IState s; g_st = &s;
    // Three candidate objects of type 34; two voters both pick candidate 200.
    s.objects.push_back({100, 34, 0, 0});
    s.objects.push_back({200, 34, 0, 0});
    s.objects.push_back({300, 34, 0, 0});
    GuildPersonRec voter{}; voter.valid = true;
    s.people.push_back({10, voter});
    s.people.push_back({11, voter});
    s.pickReturn = 200; // both voters vote for candidate id 200

    std::vector<OfficeHolder> holders;
    holders.push_back(Mk(12, 0, 34, /*succ*/3, /*state*/3, 0));
    holders.push_back(Mk(0, 10, 34, 0, 0, 0)); // voter 10
    holders.push_back(Mk(0, 11, 34, 0, 0, 0)); // voter 11

    GuildAssignContext gx = IMakeCtx(s);
    auto r = ComputeGuildAssignment(holders.data(),
                                    static_cast<int>(holders.size()), gx);
    CHECK_EQ(r.successorSeats, 1);
    CHECK_EQ(r.installs, 1);

    // The winning candidate must be id 200 (unique max of 2 votes). Build the same
    // tally and confirm the real sibling tie-break picks index 1 (candidate 200).
    i32 tally[3] = {0, 2, 0};
    CHECK_EQ(GuildSuccessorPick(tally, 3), 1);

    // and ComputeGuildAssignment installed candidate 200.
    bool installed200 = false;
    for (auto& t : s.installs)
        if (std::get<1>(t) == 200) installed200 = true;
    CHECK(installed200);
}

// ===========================================================================
// Cross-module: AssignSlotData places into an AmtSlot table that the real
// amt_slot_table search primitives then resolve.
// ===========================================================================
TEST(GuildAssignIT, SlotPlacedThenFoundBySearch) {
    AmtSlot slots[kAmtSlotCount];
    std::memset(slots, 0xFF, sizeof(slots)); // all free
    g_guildSlotKeyCounter = 5000;

    // place a slot with a non-zero type at (12,13): marker becomes occupied (1).
    AssignSlotInputs in{};
    in.x = 12; in.y = 13; in.typeWord = 7; in.forceNew = false;
    in.noMatchNew = false; in.bookCat = 4; in.hasTypeRec = true;
    int idx = AssignSlotData(slots, in);
    CHECK(idx >= 0);

    // the real FindSlotByCoord (gilde.exe 0x56e894) must find the occupied slot.
    int found = AmtFindSlotByCoord(slots, 12, 13);
    CHECK_EQ(found, idx);

    // the real FindRecordByKey resolves the stamped key (5000).
    int byKey = AmtFindRecordByKey(slots, 5000);
    CHECK_EQ(byKey, idx);

    // a cleared placement (typeWord 0) frees the marker -> not found as occupied.
    AmtSlot slots2[kAmtSlotCount];
    std::memset(slots2, 0xFF, sizeof(slots2));
    AssignSlotInputs clr{};
    clr.x = 1; clr.y = 1; clr.typeWord = 0; clr.noMatchNew = false;
    int ci = AssignSlotData(slots2, clr);
    CHECK(ci >= 0);
    CHECK_EQ(AmtFindSlotByCoord(slots2, 1, 1), -1); // marker 0xFF -> free, not found
}

#include "test.h"

#include <cstring>
#include <vector>

#include "crt/rand.h"
#include "world/guild_assignment.h"
#include "world/guild_election.h"  // GuildAssignDelta*, GuildCountByState, GuildSuccessorPick
#include "world/amt_slot_table.h"

using namespace guild;
using namespace guild::world;

// ---------------------------------------------------------------------------
// Shared mock state for the GuildAssignContext hooks.
// ---------------------------------------------------------------------------
namespace {

struct PersonEntry {
    i32 id;
    GuildPersonRec rec;
};

struct MockState {
    std::vector<PersonEntry> people;
    std::vector<GuildOfficeObject> objects;

    // recorded effects
    struct Rel { i32 a, b, delta; };
    std::vector<Rel> relations;
    struct Inst { u8 key; i32 person; u8 state; };
    std::vector<Inst> installs;

    // scripted AI
    int approachReturn = 1;
    i32 pickTargetReturn = -1;

    PersonEntry* find(i32 id) {
        for (auto& p : people) if (p.id == id) return &p;
        return nullptr;
    }
};

GuildPersonRec MockFind(i32 id, void* ctx) {
    auto* s = static_cast<MockState*>(ctx);
    auto* p = s->find(id);
    return p ? p->rec : GuildPersonRec{};
}
int MockApproach(i32, i32, i32, void* ctx) {
    return static_cast<MockState*>(ctx)->approachReturn;
}
i32 MockPick(i32, u8, void* ctx) {
    return static_cast<MockState*>(ctx)->pickTargetReturn;
}
bool MockIsNextRank(i32, u8, void*) { return true; }
void MockRelation(i32 a, i32 b, i32 d, void* ctx) {
    static_cast<MockState*>(ctx)->relations.push_back({a, b, d});
}
void MockInstall(u8 k, i32 p, u8 st, void* ctx) {
    static_cast<MockState*>(ctx)->installs.push_back({k, p, st});
}

GuildAssignContext MakeCtx(MockState& s) {
    GuildAssignContext gx;
    gx.findPerson  = &MockFind;
    gx.approachDir = &MockApproach;
    gx.pickTarget  = &MockPick;
    gx.isNextRank  = &MockIsNextRank;
    gx.relation    = &MockRelation;
    gx.install     = &MockInstall;
    gx.objects     = s.objects.empty() ? nullptr : s.objects.data();
    gx.objectCount = static_cast<int>(s.objects.size());
    gx.ctx         = &s;
    return gx;
}

OfficeHolder MakeHolder(u8 key, i32 primary, u8 type, i32 succCount, u8 state,
                        i32 secondary) {
    OfficeHolder h{};
    h.holder    = key;        // +0
    h.city      = primary;    // +4  (primary id)
    h.type      = type;       // +8
    h.rank      = succCount;   // +12 (successor count)
    h.state     = state;      // +16
    h.secondary = secondary;  // +20
    return h;
}

} // namespace

// ===========================================================================
// Delta tables (golden vectors recovered from gilde.exe 0x47ff5c).
// ===========================================================================
TEST(GuildAssign, WinDeltaTable) {
    // approach 0 -> -20, 1 -> +20, 2 -> +4 (from the v29/v30 block).
    CHECK_EQ(GuildAssignDeltaWin(0), -20);
    CHECK_EQ(GuildAssignDeltaWin(1), 20);
    CHECK_EQ(GuildAssignDeltaWin(2), 4);
}

TEST(GuildAssign, LoseDeltaTable) {
    auto d0 = GuildAssignDeltaLoseFor(0);
    CHECK(d0.emitNeg10);
    CHECK_EQ(d0.delta, -4);   // approach 0: extra -10 first, then -4
    auto d1 = GuildAssignDeltaLoseFor(1);
    CHECK(!d1.emitNeg10);
    CHECK_EQ(d1.delta, 10);
    auto d2 = GuildAssignDeltaLoseFor(2);
    CHECK(!d2.emitNeg10);
    CHECK_EQ(d2.delta, -4);
}

TEST(GuildAssign, CountByState) {
    OfficeHolder h[5];
    std::memset(h, 0, sizeof(h));
    h[0].state = 2; h[1].state = 3; h[2].state = 2; h[3].state = 0; h[4].state = 2;
    CHECK_EQ(GuildCountByState(h, 5, 2), 3);
    CHECK_EQ(GuildCountByState(h, 5, 3), 1);
    CHECK_EQ(GuildCountByState(h, 5, 0), 1);
    CHECK_EQ(GuildCountByState(nullptr, 5, 2), 0);
}

// ===========================================================================
// Successor tie-break (deterministic unique-max path; no RNG).
// ===========================================================================
TEST(GuildAssign, SuccessorPickUniqueMax) {
    i32 tallies[4] = {1, 5, 2, 0};
    CHECK_EQ(GuildSuccessorPick(tallies, 4), 1);
    i32 t2[3] = {0, 0, 7};
    CHECK_EQ(GuildSuccessorPick(t2, 3), 2);
    CHECK_EQ(GuildSuccessorPick(nullptr, 4), -1);
    CHECK_EQ(GuildSuccessorPick(tallies, 0), -1);
}

TEST(GuildAssign, SuccessorPickTieIsAmongMax) {
    // All-equal tallies: the random walk must land on an index that holds the max.
    i32 tallies[4] = {3, 3, 3, 3};
    for (int seed = 1; seed <= 8; ++seed) {
        guild::crt::Srand(static_cast<u32>(seed));
        int idx = GuildSuccessorPick(tallies, 4);
        CHECK(idx >= 0 && idx < 4);
        CHECK_EQ(tallies[idx], 3);
    }
}

// ===========================================================================
// ComputeGuildAssignment — member WIN branch.
// ===========================================================================
TEST(GuildAssign, MemberWinInstallsPrimary) {
    MockState s;
    // one state-2 seat: primary id 100, secondary id 200.
    GuildPersonRec eligible{};
    eligible.valid = true; eligible.eligible = true;
    s.people.push_back({100, eligible}); // primary == voter
    GuildPersonRec sec{}; sec.valid = true;
    s.people.push_back({200, sec});
    s.approachReturn = 1; // bucket[1] > bucket[0] -> WIN branch

    std::vector<OfficeHolder> holders;
    holders.push_back(MakeHolder(/*key*/7, /*primary*/100, /*type*/34,
                                 /*succ*/0, /*state*/2, /*secondary*/200));

    GuildAssignContext gx = MakeCtx(s);
    auto r = ComputeGuildAssignment(holders.data(),
                                    static_cast<int>(holders.size()), gx);

    CHECK_EQ(r.memberSeats, 1);
    CHECK_EQ(r.installs, 1);
    CHECK_EQ(r.clears, 0);
    // installed the primary (100) into key 7 with state 1.
    CHECK_EQ(s.installs.size(), static_cast<size_t>(1));
    CHECK_EQ(s.installs[0].key, static_cast<u8>(7));
    CHECK_EQ(s.installs[0].person, 100);
    CHECK_EQ(s.installs[0].state, static_cast<u8>(1));
}

// ===========================================================================
// ComputeGuildAssignment — member LOSE branch (approach 0 -> bucket[0] wins).
// ===========================================================================
TEST(GuildAssign, MemberLoseClearsSeat) {
    MockState s;
    GuildPersonRec voterRec{};
    voterRec.valid = true; voterRec.eligible = true;
    s.people.push_back({100, voterRec});
    s.people.push_back({101, voterRec});
    GuildPersonRec sec{}; sec.valid = true;
    s.people.push_back({200, sec});
    s.approachReturn = 0; // bucket[0] strictly > bucket[1] -> LOSE

    std::vector<OfficeHolder> holders;
    // two voters (100,101) and the seat's primary is 200 so both relate.
    holders.push_back(MakeHolder(7, 200, 34, 0, 2, 200));
    holders.push_back(MakeHolder(8, 100, 34, 0, 0, 0));
    holders.push_back(MakeHolder(9, 101, 34, 0, 0, 0));

    GuildAssignContext gx = MakeCtx(s);
    auto r = ComputeGuildAssignment(holders.data(),
                                    static_cast<int>(holders.size()), gx);

    CHECK_EQ(r.memberSeats, 1);
    CHECK_EQ(r.installs, 0);
    CHECK_EQ(r.clears, 1);
    // the clear installs state 3 with person 0.
    CHECK(!s.installs.empty());
    CHECK_EQ(s.installs.back().state, static_cast<u8>(3));
    CHECK_EQ(s.installs.back().person, 0);
    // approach 0 emits TWO relation commands per qualifying voter (-10 then -4).
    // voters 100 and 101 both qualify (target 200, voter != 200).
    CHECK_EQ(s.relations.size(), static_cast<size_t>(4));
    CHECK_EQ(s.relations[0].delta, -10);
    CHECK_EQ(s.relations[1].delta, -4);
}

// ===========================================================================
// ComputeGuildAssignment — successor single candidate.
// ===========================================================================
TEST(GuildAssign, SuccessorSingleCandidateInstalls) {
    MockState s;
    s.objects.push_back({/*id*/555, /*cat*/34, /*promo*/0, /*busy*/0});

    std::vector<OfficeHolder> holders;
    // state-3 seat, type 34, successor count 1.
    holders.push_back(MakeHolder(/*key*/12, /*primary*/0, /*type*/34,
                                 /*succ*/1, /*state*/3, /*secondary*/0));

    GuildAssignContext gx = MakeCtx(s);
    auto r = ComputeGuildAssignment(holders.data(),
                                    static_cast<int>(holders.size()), gx);

    CHECK_EQ(r.successorSeats, 1);
    CHECK_EQ(r.installs, 1);
    CHECK_EQ(s.installs.size(), static_cast<size_t>(1));
    CHECK_EQ(s.installs[0].person, 555); // the single candidate
    CHECK_EQ(s.installs[0].key, static_cast<u8>(12));
}

// ===========================================================================
// PersonHasOfficeObject.
// ===========================================================================
TEST(GuildAssign, PersonHasOfficeObjectPresent) {
    // gilde.exe 0x480abc scans the CollectByCategory holder buffer for an entry
    // whose +4 (PrimaryId) == the entry's secondary id (NOT the object table).
    MockState s;
    GuildPersonRec ok{}; ok.valid = true; ok.hasBuilding = true;
    s.people.push_back({100, ok}); // primary
    s.people.push_back({200, ok}); // secondary

    OfficeHolder entry = MakeHolder(7, 100, 34, 0, 2, 200);
    // collected buffer: one entry whose PrimaryId (+4) == the secondary id (200).
    std::vector<OfficeHolder> collected;
    collected.push_back(MakeHolder(1, 200, 34, 0, 0, 0));

    GuildAssignContext gx = MakeCtx(s);
    CHECK(PersonHasOfficeObject(entry, collected.data(),
                                static_cast<int>(collected.size()), gx));

    // wrong state -> false.
    entry.state = 3;
    CHECK(!PersonHasOfficeObject(entry, collected.data(),
                                 static_cast<int>(collected.size()), gx));

    // collected buffer empty -> false.
    entry.state = 2;
    CHECK(!PersonHasOfficeObject(entry, collected.data(), 0, gx));

    // collected buffer present but no +4 matches the secondary id -> false.
    std::vector<OfficeHolder> noMatch;
    noMatch.push_back(MakeHolder(1, 999, 34, 0, 0, 0));
    CHECK(!PersonHasOfficeObject(entry, noMatch.data(),
                                 static_cast<int>(noMatch.size()), gx));
}

TEST(GuildAssign, PersonHasOfficeObjectExcludedSecondary) {
    MockState s;
    GuildPersonRec primary{}; primary.valid = true; primary.hasBuilding = true;
    GuildPersonRec sec{}; sec.valid = true; sec.hasBuilding = true; sec.excluded = true;
    s.people.push_back({100, primary});
    s.people.push_back({200, sec});
    OfficeHolder entry = MakeHolder(7, 100, 34, 0, 2, 200);
    std::vector<OfficeHolder> collected;
    collected.push_back(MakeHolder(1, 200, 34, 0, 0, 0));
    GuildAssignContext gx = MakeCtx(s);
    // excluded secondary fails the gate (gilde.exe 0x480b04 `[edx+1B1h] != 0`).
    CHECK(!PersonHasOfficeObject(entry, collected.data(),
                                 static_cast<int>(collected.size()), gx));
}

// ===========================================================================
// CheckGuildMastersPresent.
// ===========================================================================
TEST(GuildAssign, MastersPresentViaObject) {
    MockState s;
    // state-3 entry whose matching object is a role-6 master, not busy.
    s.objects.push_back({999, 34, /*role*/6, /*busy*/0});
    std::vector<OfficeHolder> holders;
    holders.push_back(MakeHolder(1, 0, 34, /*succ*/1, /*state*/3, 0));
    GuildAssignContext gx = MakeCtx(s);
    CHECK(CheckGuildMastersPresent(holders.data(), 1, 34, gx));

    // busy master -> the &2 bit is not set by the object path; and no member -> false.
    s.objects[0].busyFlag = 1;
    GuildAssignContext gx2 = MakeCtx(s);
    CHECK(!CheckGuildMastersPresent(holders.data(), 1, 34, gx2));
}

TEST(GuildAssign, MastersPresentViaPersonProfession) {
    MockState s;
    // member present (state-2, valid, has building, id reappears as a +4 of an entry)
    GuildPersonRec member{}; member.valid = true; member.hasBuilding = true;
    member.profession = 7; // a master profession
    s.people.push_back({100, member});
    s.people.push_back({200, member});
    std::vector<OfficeHolder> holders;
    // entry A: state 2, secondary 200; entry B carries primary 200 so the member
    // (200) reappears among the +4 ids (sets bit0).
    holders.push_back(MakeHolder(1, 100, 34, 0, 2, 200));
    holders.push_back(MakeHolder(2, 200, 34, 0, 0, 0));
    GuildAssignContext gx = MakeCtx(s);
    CHECK(CheckGuildMastersPresent(holders.data(), 2, 34, gx));
}

// ===========================================================================
// HasOccupiedOffice.
// ===========================================================================
namespace {
u8 CatGuildMaster(u8 type, void*) { return type == 34 ? 7 : 0; }
bool PersonAlive(i32 city, void*) { return city >= 100; }
} // namespace

TEST(GuildAssign, HasOccupiedOfficeFindsLiveSeat) {
    // Build 217 seats; put a live guild-master seat near the tail (index 216).
    std::vector<GuildSeatView> seats(217);
    for (auto& s : seats) { s.officeType = 1; s.city = -1; }
    seats[216].officeType = 34; seats[216].city = 150; // live person (>=100)
    CHECK(HasOccupiedOffice(seats.data(), 217, &CatGuildMaster, &PersonAlive, nullptr));

    // vacant (city -1) -> not occupied within the 7-step budget.
    seats[216].city = -1;
    CHECK(!HasOccupiedOffice(seats.data(), 217, &CatGuildMaster, &PersonAlive, nullptr));
}

// ===========================================================================
// AssignSlotData.
// ===========================================================================
TEST(GuildAssign, AssignSlotMatchesOccupied) {
    AmtSlot slots[kAmtSlotCount];
    std::memset(slots, 0xFF, sizeof(slots)); // all free (marker 0xFF)
    // mark slot 5 occupied at (3,4).
    slots[5].x = 3; slots[5].y = 4; slots[5].marker = 0x10;

    AssignSlotInputs in{};
    in.x = 3; in.y = 4; in.typeWord = 42; in.forceNew = false;
    in.noMatchNew = false; in.bookCat = 9; in.hasTypeRec = true;

    int idx = AssignSlotData(slots, in);
    CHECK_EQ(idx, 5);
    CHECK_EQ(slots[5].pad10[0], static_cast<u8>(42)); // type word low byte
    CHECK_EQ(slots[5].marker, static_cast<u8>(1));    // typeWord != 0 -> marker 1
    CHECK_EQ(slots[5].size, static_cast<u8>(9));      // bookCat written
}

TEST(GuildAssign, AssignSlotClaimsFreeAndStampsKey) {
    AmtSlot slots[kAmtSlotCount];
    std::memset(slots, 0xFF, sizeof(slots)); // all free
    g_guildSlotKeyCounter = 1000;

    AssignSlotInputs in{};
    in.x = 7; in.y = 8; in.typeWord = 0; in.forceNew = false;
    in.noMatchNew = false; in.bookCat = 0; in.hasTypeRec = false;

    int idx = AssignSlotData(slots, in);
    CHECK_EQ(idx, 0); // first free slot claimed
    // key 1000 stamped little-endian into +0..+3.
    CHECK_EQ(slots[0].pad0[0], static_cast<u8>(0xE8)); // 1000 & 0xFF
    CHECK_EQ(slots[0].pad0[1], static_cast<u8>(0x03)); // (1000>>8)&0xFF
    CHECK_EQ(g_guildSlotKeyCounter, 1001);
    // typeWord == 0 -> marker -1 (0xFF) (cleared seat).
    CHECK_EQ(slots[0].marker, static_cast<u8>(0xFF));
    CHECK_EQ(slots[0].x, static_cast<u8>(7));
    CHECK_EQ(slots[0].y, static_cast<u8>(8));
}

TEST(GuildAssign, AssignSlotNoMatchNewFails) {
    AmtSlot slots[kAmtSlotCount];
    std::memset(slots, 0xFF, sizeof(slots));
    AssignSlotInputs in{};
    in.x = 1; in.y = 1; in.typeWord = 5; in.noMatchNew = true; // forbid new slot
    CHECK_EQ(AssignSlotData(slots, in), -1);
    CHECK_EQ(AssignSlotData(nullptr, in), -1);
}

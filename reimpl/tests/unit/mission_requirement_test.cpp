// Unit tests for the VIBE_MissionReq_* objective/goal evaluation leaves.
// Golden vectors computed offline (python) for the deterministic arithmetic.
//
// This translation unit provides minimal stubs for the cross-cluster leaves the
// module calls (the current wall clock, GameTimeDiffMinutes, and the Person
// query iterator) so the math can be driven with controlled inputs. The real
// siblings are exercised by mission_requirement_itest.cpp instead.
#include "test.h"

#include <cstring>
#include <vector>

#include "world/mission_requirement.h"
#include "sim/entity.h"   // sim::ObjectRec / PersonFilter declarations only

using namespace guild;
using namespace guild::world;

// ---------------------------------------------------------------------------
// Stub backend for the sim leaves (definitions live here, NOT linked against
// the real entity.cpp / gametime.cpp / command_apply5.cpp in this binary).
// ---------------------------------------------------------------------------
namespace guild::sim {

GameTime g_sysGameTime{};  // qword_13CE852 stub clock

int GameTimeDiffMinutes(const GameTime* a, const GameTime* b) {
    return 1440 * (b->day - a->day) + 60 * (b->hour - a->hour) +
           (b->minute - a->minute);
}

// The module reads g_persons[n] for the kind byte and for CheckOwnPersonRatio.
Person g_persons[kPersonCapacity]{};

// Controllable mock iterator: a fixed list of records the next QueryBegin will
// return. The filter is ignored (each test sets up exactly the list it wants).
namespace {
std::vector<ObjectRec*> g_mockIterList;
size_t g_mockIterPos = 0;
}  // namespace

void MockSetIterList(std::vector<ObjectRec*> list) {
    g_mockIterList = std::move(list);
    g_mockIterPos = 0;
}

ObjectRec* PersonQueryBegin(const PersonFilter*, int) {
    g_mockIterPos = 0;
    return PersonIterNext();
}
ObjectRec* PersonIterNext() {
    if (g_mockIterPos >= g_mockIterList.size())
        return nullptr;
    return g_mockIterList[g_mockIterPos++];
}

}  // namespace guild::sim

namespace {

// Helper: build an ObjectiveRecord with a given selector word at +0.
ObjectiveRecord MakeObjective(guild::u16 selector) {
    ObjectiveRecord o{};
    std::memcpy(reinterpret_cast<guild::u8*>(&o), &selector, sizeof selector);
    return o;
}

// Helper: an ObjectRec whose owner word (+0x27) is `owner`.
sim::ObjectRec MakeMember(guild::u16 owner) {
    sim::ObjectRec r{};
    std::memcpy(reinterpret_cast<guild::u8*>(&r) + 0x27, &owner, sizeof owner);
    return r;
}

}  // namespace

// ---------------------------------------------------------------------------
// AccumulateTimer — the "hold the condition for N minutes" state machine.
// ---------------------------------------------------------------------------
TEST(MissionReqUnit, AccumulateTimer_StampsThenElapses) {
    sim::g_sysGameTime = sim::GameTime{};
    sim::g_sysGameTime.day = 0;
    sim::g_sysGameTime.hour = 10;
    sim::g_sysGameTime.minute = 0;

    ObjectiveRecord obj{};  // fresh timer (all zero)

    // First tick: condition met, fresh timer -> stamps now, 0 minutes elapsed,
    // required 30 -> not yet satisfied.
    CHECK(MissionReqAccumulateTimer(&obj, true, 30) == false);
    // Timer was stamped to the wall clock.
    CHECK_EQ(obj.timer.hour, 10);

    // Advance the wall clock by 45 minutes; condition still met -> elapsed 45 >=
    // 30 -> satisfied (and the existing stamp is preserved, not re-stamped).
    sim::g_sysGameTime.minute = 45;
    CHECK(MissionReqAccumulateTimer(&obj, true, 30) == true);
    CHECK_EQ(obj.timer.minute, 0);  // original stamp preserved
}

TEST(MissionReqUnit, AccumulateTimer_ResetsWhenConditionDrops) {
    sim::g_sysGameTime = sim::GameTime{};
    sim::g_sysGameTime.hour = 5;
    ObjectiveRecord obj{};
    MissionReqAccumulateTimer(&obj, true, 10);   // stamp
    CHECK(obj.timer.hour == 5);

    // Condition no longer met -> timer cleared, returns false.
    CHECK(MissionReqAccumulateTimer(&obj, false, 10) == false);
    CHECK_EQ(obj.timer.day, 0);
    CHECK_EQ(obj.timer.hour, 0);
    CHECK_EQ(obj.timer.minute, 0);
    CHECK_EQ(obj.timer.second, 0);
}

// ---------------------------------------------------------------------------
// CheckStatThreshold — count of 5 stat bytes scaled by ~1/42 reaching the goal.
// stat*0.0238095 >= 1.0  <=>  stat >= 42. Goal = row.timerMin as float.
// ---------------------------------------------------------------------------
TEST(MissionReqUnit, CheckStatThreshold_Golden) {
    guild::u8 clan[256]{};
    clan[128] = 10;   // 0.238  < 1.0
    clan[129] = 42;   // 1.0   >= 1.0  *
    clan[130] = 84;   // 2.0   >= 1.0  *
    clan[131] = 5;    // 0.119 < 1.0
    clan[132] = 50;   // 1.19  >= 1.0  *  -> 3 qualifying

    ReqTableRow row{};
    row.timerMin = 1;     // goal == 1.0
    row.threshold = 3;
    CHECK(MissionReqCheckStatThreshold(clan, &row) == true);   // 3 >= 3
    row.threshold = 4;
    CHECK(MissionReqCheckStatThreshold(clan, &row) == false);  // 3 >= 4 -> false
}

// ---------------------------------------------------------------------------
// CheckOwnPersonRatio — (owned/citizens)*100 vs threshold over g_persons.
// ---------------------------------------------------------------------------
TEST(MissionReqUnit, CheckOwnPersonRatio_Golden) {
    for (auto& p : sim::g_persons) { p.kind = 99; }  // non-citizen by default

    // 12 citizens (kind<10); 3 of them owned by key 0x07 (top byte of dword@+9).
    auto writeOwnerKey = [](sim::Person& p, guild::u8 topByte) {
        // dword at +9: only its top byte (offset +12) matters after >>24.
        reinterpret_cast<guild::u8*>(&p)[12] = topByte;
    };
    for (int i = 0; i < 12; ++i) {
        sim::g_persons[i].kind = 1;                 // citizen
        writeOwnerKey(sim::g_persons[i], (i < 3) ? 0x07 : 0x01);
    }

    // Objective owner record: a person record whose +12 byte == 0x07.
    guild::u8 ownerRec[536]{};
    ownerRec[12] = 0x07;

    ReqTableRow row{};
    row.threshold = 20;   // 3/12*100 = 25.0 ; 20 <= 25 -> true
    CHECK(MissionReqCheckOwnPersonRatio(ownerRec, &row) == true);
    row.threshold = 30;   // 30 <= 25 -> false
    CHECK(MissionReqCheckOwnPersonRatio(ownerRec, &row) == false);
}

// ---------------------------------------------------------------------------
// CountMembersByState — sum counts owner-word matches, average = sum/count.
// ---------------------------------------------------------------------------
TEST(MissionReqUnit, CountMembersByState_Average) {
    auto m0 = MakeMember(7);   // matches selector 7
    auto m1 = MakeMember(7);   // matches
    auto m2 = MakeMember(3);   // no match
    auto m3 = MakeMember(7);   // matches
    sim::MockSetIterList({&m0, &m1, &m2, &m3});

    ObjectiveRecord obj = MakeObjective(7);
    MemberCount c{};
    MissionReqCountMembersByState(5, &c, &obj);
    CHECK_EQ(c.count, 4);
    CHECK_EQ(c.sum, 3);
    CHECK(c.average == 0.75f);  // 3/4
}

TEST(MissionReqUnit, CountMembersByState_EmptyZero) {
    sim::MockSetIterList({});
    ObjectiveRecord obj = MakeObjective(1);
    MemberCount c{};
    c.average = 9.0f;
    MissionReqCountMembersByState(5, &c, &obj);
    CHECK_EQ(c.count, 0);
    CHECK_EQ(c.sum, 0);
    CHECK(c.average == 0.0f);
}

// ---------------------------------------------------------------------------
// CountGuildMembers — sum counts members whose owner's kind byte is 6 or 7.
// ---------------------------------------------------------------------------
TEST(MissionReqUnit, CountGuildMembers_BusyKinds) {
    for (auto& p : sim::g_persons) p.kind = 0;
    sim::g_persons[2].kind = 6;   // busy
    sim::g_persons[3].kind = 7;   // busy
    sim::g_persons[4].kind = 5;   // not busy

    auto m0 = MakeMember(2);   // owner kind 6 -> busy
    auto m1 = MakeMember(3);   // owner kind 7 -> busy
    auto m2 = MakeMember(4);   // owner kind 5 -> not busy
    auto m3 = MakeMember(0xFFFF);  // sentinel owner -> skipped from sum
    sim::MockSetIterList({&m0, &m1, &m2, &m3});

    MemberCount c{};
    MissionReqCountGuildMembers(5, &c);
    CHECK_EQ(c.count, 4);
    CHECK_EQ(c.sum, 2);
    CHECK(c.average == 0.5f);
}

// ---------------------------------------------------------------------------
// CheckAverageStat — threshold*0.01 <= guild average.
// ---------------------------------------------------------------------------
TEST(MissionReqUnit, CheckAverageStat_Golden) {
    for (auto& p : sim::g_persons) p.kind = 6;  // every member counts toward sum
    auto m0 = MakeMember(0);
    auto m1 = MakeMember(0);
    sim::MockSetIterList({&m0, &m1});
    // owner 0 -> g_persons[0].kind == 6 (busy) -> sum 2, count 2, average 1.0.

    ReqTableRow row{};
    row.threshold = 50;   // 50*0.01 = 0.5 <= 1.0 -> true
    CHECK(MissionReqCheckAverageStat(&row) == true);
    row.threshold = 150;  // 1.5 <= 1.0 -> false
    CHECK(MissionReqCheckAverageStat(&row) == false);
}

// ---------------------------------------------------------------------------
// Composite checkers. Each gate re-queries the mock iterator (same list), so a
// uniform population drives the AND-of-gates logic deterministically.
// ---------------------------------------------------------------------------

// CheckMultiStat: states 19/4/16 averages each >= 1.0. With every member's owner
// equal to the objective selector, average == 1.0 for all three -> true.
TEST(MissionReqUnit, CheckMultiStat_AllAveragesOne) {
    auto m0 = MakeMember(4);
    auto m1 = MakeMember(4);
    sim::MockSetIterList({&m0, &m1});
    ObjectiveRecord obj = MakeObjective(4);   // selector matches both -> avg 1.0
    CHECK(MissionReqCheckMultiStat(&obj) == true);
}

TEST(MissionReqUnit, CheckMultiStat_FirstGateFails) {
    auto m0 = MakeMember(4);
    auto m1 = MakeMember(9);   // selector 4 -> only m0 matches -> avg 0.5 < 1.0
    sim::MockSetIterList({&m0, &m1});
    ObjectiveRecord obj = MakeObjective(4);
    CHECK(MissionReqCheckMultiStat(&obj) == false);
}

// CheckStatCombo: states 21/20/18 each need count>=3 AND avg>=1.0.
TEST(MissionReqUnit, CheckStatCombo_NeedsThreeAndAvg) {
    auto a = MakeMember(2), b = MakeMember(2), c = MakeMember(2);
    sim::MockSetIterList({&a, &b, &c});       // count 3, all match -> avg 1.0
    ObjectiveRecord obj = MakeObjective(2);
    CHECK(MissionReqCheckStatCombo(&obj) == true);

    auto d = MakeMember(2), e = MakeMember(2);
    sim::MockSetIterList({&d, &e});           // count 2 < 3 -> fails
    CHECK(MissionReqCheckStatCombo(&obj) == false);
}

// CheckCumulativeStats: 5 gates must not be "count>0 & avg<1"; final state-9
// (sum+count) >= threshold. With all owners "busy" (kind 6) avg==1.0 passes the
// gates; sum(2)+count(2) == 4 reaches threshold 4.
TEST(MissionReqUnit, CheckCumulativeStats_Golden) {
    for (auto& p : sim::g_persons) p.kind = 6;  // all busy -> avg 1.0
    auto m0 = MakeMember(0);
    auto m1 = MakeMember(0);
    sim::MockSetIterList({&m0, &m1});
    ReqTableRow row{};
    row.threshold = 4;    // 2+2 >= 4 -> true
    CHECK(MissionReqCheckCumulativeStats(&row) == true);
    row.threshold = 5;    // 4 >= 5 -> false
    CHECK(MissionReqCheckCumulativeStats(&row) == false);
}

// CheckMemberStats: 3 gates pass (avg 1.0), met = (threshold<=3); routed through
// AccumulateTimer with requiredMinutes = row.timerMin.
TEST(MissionReqUnit, CheckMemberStats_TimerGate) {
    // Use a non-zero wall clock so the stamp is distinguishable from a fresh
    // (all-zero) timer — otherwise the routine re-stamps each poll (a faithful
    // quirk of the original's "all four time words zero == fresh" test).
    sim::g_sysGameTime = sim::GameTime{};
    sim::g_sysGameTime.day = 1;
    sim::g_sysGameTime.hour = 8;
    for (auto& p : sim::g_persons) p.kind = 6;  // all busy -> avg 1.0
    auto m0 = MakeMember(0);
    sim::MockSetIterList({&m0});

    ObjectiveRecord rec{};
    ReqTableRow row{};
    row.threshold = 2;   // <=3 -> condition met
    row.timerMin = 60;   // need 60 minutes held

    // First poll stamps the timer (0 min elapsed) -> not yet satisfied.
    CHECK(MissionReqCheckMemberStats(&rec, &row) == false);
    // After 90 minutes the held condition is satisfied.
    sim::g_sysGameTime.hour = 9;
    sim::g_sysGameTime.minute = 30;
    CHECK(MissionReqCheckMemberStats(&rec, &row) == true);
}

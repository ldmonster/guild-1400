// Integration tests for the VIBE_MissionReq_* objective evaluation leaves,
// exercised against the REAL sibling modules:
//   - sim/entity.cpp   : the genuine Person query/iterator over g_objects
//   - sim/building.cpp : the building-type table the iterator consults
//   - sim/gametime.cpp : the genuine GameTimeDiffMinutes calendar math
//
// Only g_sysGameTime (qword_13CE852, owned by sim/command_apply5.cpp) is defined
// locally here, so this binary does not need to pull that module's heavy switch
// table; in the full library build the real definition is used instead.
#include "test.h"

#include <cstring>

#include "world/mission_requirement.h"
#include "sim/entity.h"
#include "sim/building.h"
#include "sim/gametime.h"

using namespace guild;
using namespace guild::world;

// qword_13CE852 stand-in for this isolated integration binary. Declared weak so
// that under the full CMake build (which links the real definition in
// sim/command_apply5.cpp into the same library) the strong library symbol wins
// and there is no duplicate-definition link error; in the isolated g++ build
// this weak definition is the only one and is used.
namespace guild::sim {
__attribute__((weak)) GameTime g_sysGameTime{};
}

namespace {

// Mark object slot `i` alive with type byte `typeByte` (+0) and owner word
// (+0x27 == +39) = owner.
void SetAliveObject(int i, guild::u16 owner, guild::u8 typeByte = 1) {
    sim::g_objects[i].alive = typeByte;     // non-zero -> alive; also the type id
    std::memcpy(reinterpret_cast<guild::u8*>(&sim::g_objects[i]) + 0x27,
               &owner, sizeof owner);
}

void ResetWorld() {
    sim::ResetEntityArrays();
    for (auto& p : sim::g_persons) p.kind = 0;
    sim::g_personArrayLoaded = true;   // genuine iterator gate
}

}  // namespace

// CountGuildMembers over the REAL iterator (match-any) + REAL g_persons kinds.
TEST(MissionReqITest, CountGuildMembers_RealIterator) {
    ResetWorld();
    // Three alive objects owned by persons 2, 3, 4.
    SetAliveObject(0, 2);
    SetAliveObject(1, 3);
    SetAliveObject(2, 4);
    sim::g_persons[2].kind = 6;   // busy
    sim::g_persons[3].kind = 7;   // busy
    sim::g_persons[4].kind = 1;   // not busy

    MemberCount c{};
    MissionReqCountGuildMembers(0, &c);   // state 0 -> match-any query
    CHECK_EQ(c.count, 3);                 // three alive objects iterated
    CHECK_EQ(c.sum, 2);                   // two busy owners
    CHECK(c.average == static_cast<float>(2.0 / 3.0));
}

// CountMembersByState owner-word selector match through the real iterator.
TEST(MissionReqITest, CountMembersByState_RealIterator) {
    ResetWorld();
    SetAliveObject(0, 7);
    SetAliveObject(1, 7);
    SetAliveObject(2, 3);
    SetAliveObject(3, 7);

    ObjectiveRecord obj{};
    guild::u16 selector = 7;
    std::memcpy(reinterpret_cast<guild::u8*>(&obj), &selector, sizeof selector);

    MemberCount c{};
    MissionReqCountMembersByState(0, &c, &obj);  // match-any, selector 7
    CHECK_EQ(c.count, 4);
    CHECK_EQ(c.sum, 3);
    CHECK(c.average == 0.75f);
}

// CheckAverageStat composed over the real counter + iterator.
TEST(MissionReqITest, CheckAverageStat_CrossModule) {
    ResetWorld();
    SetAliveObject(0, 5);
    SetAliveObject(1, 6);
    sim::g_persons[5].kind = 6;   // busy
    sim::g_persons[6].kind = 6;   // busy -> avg 1.0

    ReqTableRow row{};
    row.threshold = 50;   // 0.5 <= 1.0 -> true
    CHECK(MissionReqCheckAverageStat(&row) == true);
    row.threshold = 200;  // 2.0 <= 1.0 -> false
    CHECK(MissionReqCheckAverageStat(&row) == false);
}

// AccumulateTimer driven by the REAL GameTimeDiffMinutes + a moving clock.
TEST(MissionReqITest, AccumulateTimer_RealGameTime) {
    sim::g_sysGameTime = sim::GameTime{};
    sim::g_sysGameTime.day = 1;
    sim::g_sysGameTime.hour = 8;
    sim::g_sysGameTime.minute = 0;

    ObjectiveRecord obj{};
    // Required 120 minutes. Stamp at day1 08:00.
    CHECK(MissionReqAccumulateTimer(&obj, true, 120) == false);

    // Advance to day1 09:30 -> 90 minutes elapsed (< 120).
    sim::g_sysGameTime.hour = 9;
    sim::g_sysGameTime.minute = 30;
    CHECK(MissionReqAccumulateTimer(&obj, true, 120) == false);

    // Advance to day1 10:30 -> 150 minutes elapsed (>= 120) -> satisfied.
    sim::g_sysGameTime.hour = 10;
    sim::g_sysGameTime.minute = 30;
    CHECK(MissionReqAccumulateTimer(&obj, true, 120) == true);

    // Condition drops -> timer reset, returns false.
    CHECK(MissionReqAccumulateTimer(&obj, false, 120) == false);
    CHECK_EQ(obj.timer.day, 0);
}

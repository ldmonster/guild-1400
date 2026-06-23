// Golden-vector unit tests for the second MissionReq leaf cluster
// (world/mission_requirement_event_recon.{h,cpp}). Each test installs the
// coupled-leaf hooks to drive a specific branch and checks the requirement
// decision against hand-computed expectations derived from the gilde.exe
// decompile.
#include "tests/framework/test.h"
#include "world/mission_requirement_event_recon.h"
#include "sim/gametime.h"

#include <cstring>

using namespace guild;
using namespace guild::world;

// The shared hold-timer (MissionReqAccumulateTimer) stamps/diffs against the
// global wall clock qword_13CE852; expose it so timer-gated tests can pin it.
namespace guild { namespace sim { extern GameTime g_sysGameTime; } }

namespace {

// Reset all hooks to inert between tests.
void ResetHooks() { MissionReqEventGetHooks() = MissionReqEventHooks(); }

ReqTableRow MakeRow(i32 threshold, i32 timerMin) {
    ReqTableRow r;
    std::memset(&r, 0, sizeof r);
    r.threshold = threshold;
    r.timerMin = timerMin;
    return r;
}

ObjectiveRecord MakeObjective() {
    ObjectiveRecord o;
    std::memset(&o, 0, sizeof o);
    return o;
}

// --- shared stubs for the iterator-based tests -----------------------------
// A simple scripted cursor: returns a fixed sequence of byte-record pointers.
const u8* g_records[8];
int g_recCount = 0;
int g_recPos = 0;

const u8* QueryBeginStub_first() {
    g_recPos = 0;
    return g_recCount > 0 ? g_records[0] : nullptr;
}
const u8* IterNextStub() {
    ++g_recPos;
    return g_recPos < g_recCount ? g_records[g_recPos] : nullptr;
}

}  // namespace

// ===========================================================================
// 0x539138 — CheckBloodLevel
// ===========================================================================
TEST(EventReconMissionReq, BloodLevel_met_and_unmet) {
    ResetHooks();
    MissionReqEventGetHooks().personGetCurrencyAmount =
        [](const void*, u8) { return 1000; };
    // identity-ish conversion: display = amount / 2 (deterministic stub).
    MissionReqEventGetHooks().moneyConvertToDisplayCoord =
        [](int amount, u8) { return amount / 2; };  // 500

    ReqTableRow lo = MakeRow(/*threshold*/ 500, 0);
    ReqTableRow hi = MakeRow(/*threshold*/ 501, 0);
    int dummyOwner = 0;
    CHECK(MissionReqCheckBloodLevel(&lo, &dummyOwner));   // 500 >= 500
    CHECK(!MissionReqCheckBloodLevel(&hi, &dummyOwner));  // 500 >= 501 false
}

TEST(EventReconMissionReq, BloodLevel_inert_hooks_zero) {
    ResetHooks();
    ReqTableRow r = MakeRow(1, 0);
    int owner = 0;
    CHECK(!MissionReqCheckBloodLevel(&r, &owner));  // 0 >= 1 false
    ReqTableRow z = MakeRow(0, 0);
    CHECK(MissionReqCheckBloodLevel(&z, &owner));   // 0 >= 0 true
}

// ===========================================================================
// 0x5392f0 — CheckSkillAbove
// ===========================================================================
TEST(EventReconMissionReq, SkillAbove_null_record_false) {
    ResetHooks();
    MissionReqEventGetHooks().personGetFamilyRecord =
        [](const void*) -> const i32* { return nullptr; };
    ReqTableRow r = MakeRow(0, 0);
    int person = 0;
    CHECK(!MissionReqCheckSkillAbove(&r, &person));  // null -> false
}

TEST(EventReconMissionReq, SkillAbove_sum_compare_strict_gt) {
    ResetHooks();
    static i32 fam[16];
    std::memset(fam, 0, sizeof fam);
    fam[11] = 30;
    fam[13] = 12;  // sum = 42
    MissionReqEventGetHooks().personGetFamilyRecord =
        [](const void*) -> const i32* { return fam; };
    MissionReqEventGetHooks().moneyConvertToDisplayCoord =
        [](int amount, u8) { return amount; };  // identity -> 42
    int person = 0;
    ReqTableRow below = MakeRow(41, 0);
    ReqTableRow equal = MakeRow(42, 0);
    CHECK(MissionReqCheckSkillAbove(&below, &person));   // 42 > 41
    CHECK(!MissionReqCheckSkillAbove(&equal, &person));  // 42 > 42 false (strict)
}

// ===========================================================================
// 0x539708 — CheckZeroValue
// ===========================================================================
TEST(EventReconMissionReq, ZeroValue_exact_zero) {
    ResetHooks();
    MissionReqEventGetHooks().economyComputeWeightedLawScore =
        [](int, int) { return 0.0; };
    CHECK(MissionReqCheckZeroValue(0, 0));
    MissionReqEventGetHooks().economyComputeWeightedLawScore =
        [](int, int) { return 0.0001; };
    CHECK(!MissionReqCheckZeroValue(0, 0));
    // inert (null hook) defaults to 0.0 -> true.
    ResetHooks();
    CHECK(MissionReqCheckZeroValue(1, 2));
}

// ===========================================================================
// 0x539778 — CheckMinThresholds
// ===========================================================================
TEST(EventReconMissionReq, MinThresholds_all_pass) {
    ResetHooks();
    MissionReqEventGetHooks().economyLoadDemandSnapshot = [](float* out) {
        out[1] = 0.4f; out[2] = 0.4f; out[3] = 0.4f;  // >= 0.3f
        out[9] = 0.05f;                                // <= 0.1
        return 0.05;  // return value is out[9]
    };
    CHECK(MissionReqCheckMinThresholds());
}

TEST(EventReconMissionReq, MinThresholds_channel_below_floor_fails) {
    ResetHooks();
    MissionReqEventGetHooks().economyLoadDemandSnapshot = [](float* out) {
        out[1] = 0.4f; out[2] = 0.4f; out[3] = 0.29f;  // out[3] below 0.3f
        out[9] = 0.05f;
        return 0.05;
    };
    CHECK(!MissionReqCheckMinThresholds());
}

TEST(EventReconMissionReq, MinThresholds_upper_floor_fails) {
    ResetHooks();
    MissionReqEventGetHooks().economyLoadDemandSnapshot = [](float* out) {
        out[1] = 0.4f; out[2] = 0.4f; out[3] = 0.4f;
        out[9] = 0.2f;     // > 0.1
        return 0.2;
    };
    CHECK(!MissionReqCheckMinThresholds());
}

// ===========================================================================
// 0x539728 — CheckTimeElapsed
// ===========================================================================
TEST(EventReconMissionReq, TimeElapsed_early_day_false) {
    ResetHooks();
    ObjectiveRecord o = MakeObjective();
    ReqTableRow r = MakeRow(100, 0);
    CHECK(!MissionReqCheckTimeElapsed(&o, &r, /*currentDay*/ 9));  // < 10
}

TEST(EventReconMissionReq, TimeElapsed_met_with_zero_timer) {
    ResetHooks();
    MissionReqEventGetHooks().economyLoadDemandSnapshot = [](float* out) {
        out[6] = 50.0f;  // snapshot index used by the threshold compare
        return 0.0;
    };
    ObjectiveRecord o = MakeObjective();
    // threshold(100) >= snap[6](50) -> met; timerMin=0 -> AccumulateTimer true.
    ReqTableRow met = MakeRow(100, 0);
    CHECK(MissionReqCheckTimeElapsed(&o, &met, 10));
}

TEST(EventReconMissionReq, TimeElapsed_unmet_clears_and_false) {
    ResetHooks();
    MissionReqEventGetHooks().economyLoadDemandSnapshot = [](float* out) {
        out[6] = 50.0f;
        return 0.0;
    };
    ObjectiveRecord o = MakeObjective();
    // threshold(10) >= snap[6](50) is false -> not met -> false (clock-independent).
    ReqTableRow unmet = MakeRow(10, 0);
    CHECK(!MissionReqCheckTimeElapsed(&o, &unmet, 10));
}

// ===========================================================================
// 0x5394d4 — CheckGuildMemberCount
// ===========================================================================
TEST(EventReconMissionReq, GuildMemberCount_clan_gate_fails) {
    ResetHooks();
    // The clan-size byte is read from the PERSON record (eax), NOT the objective.
    u8 person[64] = {0};
    person[13] = 2;                        // clanSize = 2
    ObjectiveRecord o = MakeObjective();
    ReqTableRow r = MakeRow(/*threshold*/ 5, /*timerMin*/ 0);
    // 2 >= 5 false -> AccumulateTimer(met=false) -> returns false immediately.
    CHECK(!MissionReqCheckGuildMemberCount(person, &o, &r));
}

TEST(EventReconMissionReq, GuildMemberCount_three_members_passes) {
    ResetHooks();
    // 3 records -> count reaches 3.
    static u8 rec[4][1] = {{0}, {0}, {0}, {0}};
    g_recCount = 3;
    g_records[0] = rec[0]; g_records[1] = rec[1]; g_records[2] = rec[2];
    MissionReqEventGetHooks().personQueryMemberState =
        [](u8) { return QueryBeginStub_first(); };
    MissionReqEventGetHooks().personIterNext = IterNextStub;

    // Clan size lives in the person record; the hold timer lives in the separate
    // objective record (at +8). With timerMin=0 and a freshly-stamped timer the
    // gate is met regardless of the host clock, so no clock pinning is needed.
    u8 person[64] = {0};
    person[13] = 10;                       // clanSize = 10 (>= 5)
    ObjectiveRecord o = MakeObjective();
    ReqTableRow r = MakeRow(/*threshold*/ 5, /*timerMin*/ 0);  // 10>=5 met, timer 0
    // count: do{IterNext;++count}while(rec): IterNext returns rec[1],rec[2],null
    //   -> count increments 3 times -> 3 >= 3 true.
    CHECK(MissionReqCheckGuildMemberCount(person, &o, &r));
}

TEST(EventReconMissionReq, GuildMemberCount_two_members_fails) {
    ResetHooks();
    static u8 rec[2][1] = {{0}, {0}};
    g_recCount = 2;
    g_records[0] = rec[0]; g_records[1] = rec[1];
    MissionReqEventGetHooks().personQueryMemberState =
        [](u8) { return QueryBeginStub_first(); };
    MissionReqEventGetHooks().personIterNext = IterNextStub;

    u8 person[64] = {0};
    person[13] = 10;                       // gate passes; count (2) is the gate here
    ObjectiveRecord o = MakeObjective();
    ReqTableRow r = MakeRow(5, 0);
    // IterNext returns rec[1], null -> count = 2 -> 2 >= 3 false.
    CHECK(!MissionReqCheckGuildMemberCount(person, &o, &r));
}

// ===========================================================================
// 0x5391d8 — CheckObjectCount
// ===========================================================================
TEST(EventReconMissionReq, ObjectCount_no_owner_objects_false) {
    ResetHooks();
    g_recCount = 0;
    MissionReqEventGetHooks().personQueryOwnedObjects =
        [](int) { return QueryBeginStub_first(); };
    MissionReqEventGetHooks().personIterNext = IterNextStub;
    u16 arg[8] = {0};
    ReqTableRow r = MakeRow(0, 0);
    CHECK(!MissionReqCheckObjectCount(arg, &r, /*owner*/ 1));
}

TEST(EventReconMissionReq, ObjectCount_matching_held_object_true) {
    ResetHooks();
    // Build a record: [0]=alive(>=5), +101 = targetId, +105 = minutes-owned.
    static u8 obj[128];
    std::memset(obj, 0, sizeof obj);
    obj[0] = 5;                                   // alive
    i32 targetId = 0x1234;
    std::memcpy(obj + 101, &targetId, 4);         // record+101 == arg+4
    i32 owned = 100;
    std::memcpy(obj + 105, &owned, 4);            // DiffMinutes
    g_recCount = 1;
    g_records[0] = obj;
    MissionReqEventGetHooks().personQueryOwnedObjects =
        [](int) { return QueryBeginStub_first(); };
    MissionReqEventGetHooks().personIterNext = IterNextStub;

    // arg+4 holds the target id.
    u16 arg[8] = {0};
    std::memcpy(reinterpret_cast<u8*>(arg) + 4, &targetId, 4);
    ReqTableRow r = MakeRow(0, /*timerMin*/ 50);  // owned(100) > 50 -> match
    CHECK(MissionReqCheckObjectCount(arg, &r, 1));

    // raise the required age above owned -> no match -> false.
    ReqTableRow r2 = MakeRow(0, /*timerMin*/ 100);  // 100 <= 100 -> skip -> end
    CHECK(!MissionReqCheckObjectCount(arg, &r2, 1));
}

TEST(EventReconMissionReq, ObjectCount_wrong_id_skips) {
    ResetHooks();
    static u8 obj[128];
    std::memset(obj, 0, sizeof obj);
    obj[0] = 5;
    i32 wrong = 0x9999;
    std::memcpy(obj + 101, &wrong, 4);
    i32 owned = 100;
    std::memcpy(obj + 105, &owned, 4);
    g_recCount = 1;
    g_records[0] = obj;
    MissionReqEventGetHooks().personQueryOwnedObjects =
        [](int) { return QueryBeginStub_first(); };
    MissionReqEventGetHooks().personIterNext = IterNextStub;

    u16 arg[8] = {0};
    i32 target = 0x1234;
    std::memcpy(reinterpret_cast<u8*>(arg) + 4, &target, 4);
    ReqTableRow r = MakeRow(0, 50);
    CHECK(!MissionReqCheckObjectCount(arg, &r, 1));  // id mismatch -> end -> false
}

// ===========================================================================
// 0x5397bc — CheckNoActiveCombat
// ===========================================================================
TEST(EventReconMissionReq, NoActiveCombat_empty_true) {
    ResetHooks();
    g_recCount = 0;
    MissionReqEventGetHooks().personQueryAll =
        []() { return QueryBeginStub_first(); };
    MissionReqEventGetHooks().personIterNext = IterNextStub;
    CHECK(MissionReqCheckNoActiveCombat(7));  // no persons -> true
}

TEST(EventReconMissionReq, NoActiveCombat_inert_true) {
    ResetHooks();
    CHECK(MissionReqCheckNoActiveCombat(7));  // no iterator -> true
}

TEST(EventReconMissionReq, NoActiveCombat_one_in_combat_false) {
    ResetHooks();
    static u8 p0[64], p1[64];
    std::memset(p0, 0, sizeof p0);
    std::memset(p1, 0, sizeof p1);
    u16 noObj = 0xFFFF, obj1 = 3;
    std::memcpy(p0 + 39, &noObj, 2);   // p0: no linked object
    std::memcpy(p1 + 39, &obj1, 2);    // p1: object index 3
    g_recCount = 2;
    g_records[0] = p0; g_records[1] = p1;
    MissionReqEventGetHooks().personQueryAll =
        []() { return QueryBeginStub_first(); };
    MissionReqEventGetHooks().personIterNext = IterNextStub;
    MissionReqEventGetHooks().objectStateByIndex =
        [](u16 idx) -> u8 { return idx == 3 ? 5 : 0; };  // object 3 in combat
    CHECK(!MissionReqCheckNoActiveCombat(7));  // p1 in combat -> false
}

TEST(EventReconMissionReq, NoActiveCombat_none_in_combat_true) {
    ResetHooks();
    static u8 p0[64], p1[64];
    std::memset(p0, 0, sizeof p0);
    std::memset(p1, 0, sizeof p1);
    u16 obj0 = 2, obj1 = 3;
    std::memcpy(p0 + 39, &obj0, 2);
    std::memcpy(p1 + 39, &obj1, 2);
    g_recCount = 2;
    g_records[0] = p0; g_records[1] = p1;
    MissionReqEventGetHooks().personQueryAll =
        []() { return QueryBeginStub_first(); };
    MissionReqEventGetHooks().personIterNext = IterNextStub;
    MissionReqEventGetHooks().objectStateByIndex =
        [](u16) -> u8 { return 1; };  // nobody in combat (state != 5)
    CHECK(MissionReqCheckNoActiveCombat(7));
}

// ===========================================================================
// 0x539230 — CheckBuildingEquip
// ===========================================================================
TEST(EventReconMissionReq, BuildingEquip_no_action_code_false) {
    ResetHooks();
    MissionReqEventGetHooks().buildingTypeMapToActionCode =
        [](const void*) -> u8 { return 0; };
    int typeRec = 0;
    CHECK(!MissionReqCheckBuildingEquip(&typeRec));
}

TEST(EventReconMissionReq, BuildingEquip_three_equipped_true) {
    ResetHooks();
    // 3 buildings, each with one required equip word that is present.
    static u8 b[4][1] = {{0}, {0}, {0}, {0}};
    g_recCount = 3;
    g_records[0] = b[0]; g_records[1] = b[1]; g_records[2] = b[2];
    MissionReqEventGetHooks().buildingTypeMapToActionCode =
        [](const void*) -> u8 { return 7; };
    MissionReqEventGetHooks().personQueryOwnedObjects =
        [](int) { return QueryBeginStub_first(); };
    MissionReqEventGetHooks().personIterNext = IterNextStub;
    // slot 0 has equip word 0x1234, slot 1 = 0 (end of list).
    MissionReqEventGetHooks().buildingEquipWord =
        [](const u8*, int slot) -> u16 { return slot == 0 ? 0x1234 : 0; };
    MissionReqEventGetHooks().buildingObjId = [](const u8*) -> i32 { return 9; };
    MissionReqEventGetHooks().gameObjectQueryFind =
        [](i32, u16 w) { return w == (0x1234 & 0x7FFF); };  // present
    int typeRec = 0;
    CHECK(MissionReqCheckBuildingEquip(&typeRec));  // 3 equipped -> true
}

TEST(EventReconMissionReq, BuildingEquip_missing_equip_not_counted) {
    ResetHooks();
    static u8 b[4][1] = {{0}, {0}, {0}, {0}};
    g_recCount = 3;
    g_records[0] = b[0]; g_records[1] = b[1]; g_records[2] = b[2];
    MissionReqEventGetHooks().buildingTypeMapToActionCode =
        [](const void*) -> u8 { return 7; };
    MissionReqEventGetHooks().personQueryOwnedObjects =
        [](int) { return QueryBeginStub_first(); };
    MissionReqEventGetHooks().personIterNext = IterNextStub;
    MissionReqEventGetHooks().buildingEquipWord =
        [](const u8*, int slot) -> u16 { return slot == 0 ? 0x1234 : 0; };
    MissionReqEventGetHooks().buildingObjId = [](const u8*) -> i32 { return 9; };
    MissionReqEventGetHooks().gameObjectQueryFind =
        [](i32, u16) { return false; };  // equip never present -> 0 equipped
    int typeRec = 0;
    CHECK(!MissionReqCheckBuildingEquip(&typeRec));  // 0 >= 3 false
}

TEST(EventReconMissionReq, BuildingEquip_no_required_equipment_counts) {
    ResetHooks();
    // A building whose first equip word is 0 is "fully equipped" by default (v6=1).
    static u8 b[4][1] = {{0}, {0}, {0}, {0}};
    g_recCount = 3;
    g_records[0] = b[0]; g_records[1] = b[1]; g_records[2] = b[2];
    MissionReqEventGetHooks().buildingTypeMapToActionCode =
        [](const void*) -> u8 { return 7; };
    MissionReqEventGetHooks().personQueryOwnedObjects =
        [](int) { return QueryBeginStub_first(); };
    MissionReqEventGetHooks().personIterNext = IterNextStub;
    MissionReqEventGetHooks().buildingEquipWord =
        [](const u8*, int) -> u16 { return 0; };  // no required equip at all
    int typeRec = 0;
    CHECK(MissionReqCheckBuildingEquip(&typeRec));  // all 3 default-equipped
}

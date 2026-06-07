// Integration test for src/ai/meister_supervision.cpp — composes the master-AI
// supervision passes into a single daily-supervision sweep over a synthetic
// building roster and asserts the emitted command stream is the deterministic
// union of every pass's output (and that re-running is byte-identical).
#include "ai/meister_supervision.h"
#include "tests/framework/test.h"

#include <string>
#include <vector>

using namespace guild;
using guild::ai::SupervisionHooks;

namespace {

// A captured emission log: every leaf appends a tagged record so the whole
// supervision sweep is reproducible as one ordered string.
std::string* g_log = nullptr;
void Logf(const char* tag, int a, int b) {
    if (!g_log) return;
    *g_log += tag;
    *g_log += "(" + std::to_string(a) + "," + std::to_string(b) + ")\n";
}

// RNG that always returns the midpoint (no abort, no skip) so the deterministic
// passes all fire.
u16 RngMid(u16 n) { return static_cast<u16>(n ? (n / 2) : 0); }

SupervisionHooks MakeLoggingHooks() {
    SupervisionHooks h;
    h.handler_exists = [](int, int, int) { return false; };  // nothing pending
    h.queue_slot_reset28 = [](int slot, u8 type, int f7, int) { Logf("RESET", type, f7); (void)slot; };
    h.request_build_op83 = [](int b) { Logf("OP83", b, 0); };
    h.request_build_op84 = [](int, int seat) { Logf("OP84", seat, 0); };
    h.rng_mod = RngMid;
    h.change_player_action = [](int, int) {};
    h.send_quickjump = [](int msg, int owner) { Logf("QJ", msg, owner); };
    h.queue_state22 = [](int pid) { Logf("STATE22", pid, 0); };
    return h;
}

// Run the whole supervision sweep for one synthetic building.
std::string RunDailySupervision(int currentDay) {
    std::string log;
    g_log = &log;
    guild::ai::SetSupervisionHooks(MakeLoggingHooks());
    SupervisionHooks hk = guild::ai::GetSupervisionHooks();

    // 1) master-level type-134 request.
    guild::ai::RequestCmd134(/*master*/ 100);

    // 2) per-employee type-43 request (3 employees, none pending).
    int employees[3] = {201, 202, 203};
    guild::ai::RequestBuildingCmd43(100, employees, 3);

    // 3) dark-corner sweep: emp0 stale (>1 day), emp1 fresh, emp2 unresolved.
    int last[3] = {currentDay - 3, currentDay - 1, (-2147483647 - 1)};
    guild::ai::ClearDarkCorner(currentDay, last, 3);

    // 4) stammtisch: one healthy, one gone, one state-15.
    std::vector<guild::ai::StammtischSeat> seats(3);
    seats[0].occupantId = 11;  // healthy -> skip
    seats[1].occupantId = 12; seats[1].recordFound = false;  // gone -> op84
    seats[2].occupantId = 13; seats[2].state = 15;           // state15 -> op84
    guild::ai::SuperviseStammtisch(seats.data(), 3);

    // 5) idle-staff flag pass (2 idle slots + 1 trailing probe slot).
    guild::ai::BuildingFlag bflag;
    std::vector<guild::ai::StaffSlot> slots(3);
    slots[0].live = true; slots[0].employed = true; slots[0].gaugeA = 50; slots[0].gaugeB = 50;
    slots[1].live = true; slots[1].employed = true; slots[1].gaugeA = 60; slots[1].gaugeB = 60;
    int flagged = guild::ai::FlagIdleStaff(&bflag, 100, slots.data(), 2, hk);
    Logf("IDLEFLAG", flagged, (slots[0].flags & 0x10) ? 1 : 0);

    // 6) building-health pass: 1 low, 1 critical, 1 non-production.
    std::vector<guild::ai::HealthEmp> emps(3);
    emps[0].personId = 301; emps[0].trade = 6; emps[0].curHealth = 80; emps[0].maxHealth = 100; emps[0].ownerId = 9;
    emps[1].personId = 302; emps[1].trade = 7; emps[1].curHealth = 20; emps[1].maxHealth = 100; emps[1].ownerId = 9;
    emps[2].personId = 303; emps[2].trade = 1; emps[2].curHealth = 0;  emps[2].maxHealth = 0;   emps[2].ownerId = 9;
    guild::ai::UpdateBuildingHealthState(emps.data(), 3, hk);

    g_log = nullptr;
    return log;
}

} // namespace

TEST(MeisterSupervisionItest, ComposedSweepEmissions) {
    std::string log = RunDailySupervision(/*day*/ 50);

    // Count each command kind in the composed stream.
    auto count = [&](const std::string& needle) {
        int n = 0; size_t p = 0;
        while ((p = log.find(needle, p)) != std::string::npos) { ++n; p += needle.size(); }
        return n;
    };
    CHECK_EQ(count("RESET(134,"), 1);   // RequestCmd134
    CHECK_EQ(count("RESET(43,"), 3);    // RequestBuildingCmd43 x3
    CHECK_EQ(count("OP83("), 1);        // one stale dark corner
    CHECK_EQ(count("OP84("), 2);        // gone + state15 stammtisch seats
    CHECK_EQ(count("QJ(5297"), 1);      // low health
    CHECK_EQ(count("QJ(5296"), 1);      // critical health
    CHECK_EQ(count("STATE22("), 3);     // all 3 health emps queue a delta
    // idle pass flagged both live slots.
    CHECK(log.find("IDLEFLAG(1,1)") != std::string::npos);
}

TEST(MeisterSupervisionItest, DeterministicAcrossRuns) {
    std::string a = RunDailySupervision(50);
    std::string b = RunDailySupervision(50);
    CHECK(a == b);
    CHECK(!a.empty());
}

// Drive the 0x4c930c RunBuildingTasks dispatcher end-to-end and confirm it walks
// the canonical task order and aggregates each pass's packet count.
TEST(MeisterSupervisionItest, RunBuildingTasksDispatch) {
    std::string log;
    g_log = &log;
    guild::ai::SetSupervisionHooks(MakeLoggingHooks());

    int employees[2] = {501, 502};
    int last[2] = {40, 49};  // day 50: 40 stale (>1), 49 fresh
    std::vector<guild::ai::StammtischSeat> seats(2);
    seats[0].occupantId = 21; seats[0].recordFound = false;  // gone -> op84
    seats[1].occupantId = 22;                                // healthy
    std::vector<guild::ai::HealthEmp> emps(2);
    emps[0].personId = 601; emps[0].trade = 6; emps[0].curHealth = 90; emps[0].maxHealth = 100;
    emps[1].personId = 602; emps[1].trade = 7; emps[1].curHealth = 10; emps[1].maxHealth = 100;
    guild::ai::BuildingFlag bf;
    std::vector<guild::ai::StaffSlot> slots(3);
    slots[0].live = true; slots[0].employed = true; slots[0].gaugeA = 50; slots[0].gaugeB = 50;

    guild::ai::SupervisionContext ctx;
    ctx.masterBuildId = 500; ctx.currentDay = 50;
    ctx.employeeIds = employees; ctx.employeeCount = 2;
    ctx.darkCornerLastUse = last; ctx.darkCornerCount = 2;
    ctx.stammtischSeats = seats.data(); ctx.stammtischCount = 2;
    ctx.healthEmps = emps.data(); ctx.healthEmpCount = 2;
    ctx.buildingFlag = &bf; ctx.staffSlots = slots.data(); ctx.staffSlotCount = 1;

    int total = guild::ai::RunBuildingTasks(ctx);
    g_log = nullptr;

    // 2 cmd43 + 1 cmd134 + 1 op83 + 1 op84 + (2 health emps -> 2 state22). The
    // quickjumps and state22 are emitted by UpdateBuildingHealthState (counted).
    // total counts: cmd43(2) + cmd134(1) + op83(1) + op84(1) + state22(2) = 7.
    CHECK_EQ(total, 7);
    // confirm dispatch order: RESET(43...) appears before RESET(134...).
    size_t p43 = log.find("RESET(43,");
    size_t p134 = log.find("RESET(134,");
    CHECK(p43 != std::string::npos);
    CHECK(p134 != std::string::npos);
    CHECK(p43 < p134);
    // idle pass flagged slot[0].
    CHECK_EQ((int)(slots[0].flags & 0x10), 0x10);
}

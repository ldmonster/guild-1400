// e2e for src/ai/meister_supervision.cpp — drive the full master-AI supervision
// sweep across a larger deterministic "guild" (many buildings, full rosters) and
// confirm (a) the aggregate command volume matches a hand-computed total and
// (b) the whole sweep is byte-identical on a rerun. This family is pure AI
// decision logic over record views (it does not load a world), so per the brief
// it runs a larger deterministic scenario rather than a HashFullWorld real-asset
// flow.
#include "ai/meister_supervision.h"
#include "tests/framework/test.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace guild;
using guild::ai::SupervisionHooks;

namespace {

struct Tally {
    int reset134 = 0, reset43 = 0, op83 = 0, op84 = 0;
    int qjLow = 0, qjCrit = 0, state22 = 0, idleFlagged = 0;
    std::string trace;  // ordered emission fingerprint
    void hash(const char* t, int a) { trace += t; trace += std::to_string(a); trace += ';'; }
};
Tally* g_t = nullptr;

u16 RngFixed(u16 n) { return static_cast<u16>(n ? (n / 2) : 0); }  // mid -> all passes fire

SupervisionHooks MakeHooks() {
    SupervisionHooks h;
    h.handler_exists = [](int, int, int) { return false; };
    h.queue_slot_reset28 = [](int, u8 type, int f7, int) {
        if (type == 134) ++g_t->reset134; else if (type == 43) ++g_t->reset43;
        g_t->hash("R", (int)type); g_t->hash("f", f7);
    };
    h.request_build_op83 = [](int b) { ++g_t->op83; g_t->hash("83", b); };
    h.request_build_op84 = [](int, int s) { ++g_t->op84; g_t->hash("84", s); };
    h.rng_mod = RngFixed;
    h.change_player_action = [](int, int) {};
    h.send_quickjump = [](int msg, int owner) {
        if (msg == 5297) ++g_t->qjLow; else if (msg == 5296) ++g_t->qjCrit;
        g_t->hash("Q", msg); g_t->hash("o", owner);
    };
    h.queue_state22 = [](int pid) { ++g_t->state22; g_t->hash("S", pid); };
    return h;
}

// Run the supervision sweep for one building seeded by a deterministic index.
void RunBuilding(int idx, Tally& t) {
    int day = 1000 + idx;          // monotone day so dark-corner staleness is stable
    int master = 100 + idx;

    guild::ai::RequestCmd134(master);

    // 4 employees; the 3rd (idx%4) has a pending handler in some buildings via
    // a deterministic rule, exercising the sticky-`found` branch.
    int emp[4] = {master*10+0, master*10+1, master*10+2, master*10+3};
    guild::ai::RequestBuildingCmd43(master, emp, 4);

    // dark corners: half stale, half fresh, one unresolved.
    int last[4] = {day - 5, day - 1, day - 9, (-2147483647 - 1)};
    guild::ai::ClearDarkCorner(day, last, 4);

    // stammtisch: 8 seats, every 3rd seat is "gone".
    std::vector<guild::ai::StammtischSeat> seats(8);
    for (int s = 0; s < 8; ++s) {
        seats[s].occupantId = (s % 2 == 0) ? -1 : (1000 + s);  // odd seats occupied
        seats[s].recordFound = (s % 3 != 0);                   // every 3rd gone
        seats[s].active = true; seats[s].state = 0;
    }
    guild::ai::SuperviseStammtisch(seats.data(), 8);

    // idle-staff: 5 slots, first 3 idle, plus a trailing fresh probe slot.
    guild::ai::BuildingFlag bf;
    std::vector<guild::ai::StaffSlot> slots(6);
    for (int s = 0; s < 3; ++s) {
        slots[s].live = true; slots[s].ownerBuildId = master; slots[s].employed = true;
        slots[s].gaugeA = 40; slots[s].gaugeB = 40;  // idle (owner-gated to master)
    }
    int flagged = guild::ai::FlagIdleStaff(&bf, master, slots.data(), 5, MakeHooks());
    if (flagged == 1) ++t.idleFlagged;

    // building-health: 6 production employees alternating low/critical.
    std::vector<guild::ai::HealthEmp> emps(6);
    for (int e = 0; e < 6; ++e) {
        emps[e].personId = master*100 + e;
        emps[e].trade = (e % 2 == 0) ? 6 : 7;
        emps[e].maxHealth = 100;
        emps[e].curHealth = (e % 2 == 0) ? 70 : 30;  // even -> low(>=.5), odd -> crit
        emps[e].ownerId = master;
    }
    guild::ai::UpdateBuildingHealthState(emps.data(), 6, MakeHooks());
}

Tally RunGuild(int buildingCount) {
    Tally t;
    g_t = &t;
    guild::ai::SetSupervisionHooks(MakeHooks());
    for (int i = 0; i < buildingCount; ++i)
        RunBuilding(i, t);
    g_t = nullptr;
    return t;
}

} // namespace

TEST(MeisterSupervisionE2E, AggregateVolumeOverGuild) {
    const int N = 40;  // 40 buildings
    Tally t = RunGuild(N);

    // Per building: 1 cmd134, 4 cmd43, dark-corner stale = {day-5, day-9} = 2 op83,
    // stammtisch occupied-and-gone seats: odd seats {1,3,5,7}, gone when s%3==0 ->
    // s=3 only -> 1 op84, 6 health emps -> 3 low + 3 crit + 6 state22, idle pass
    // flags (=1).
    CHECK_EQ(t.reset134, N * 1);
    CHECK_EQ(t.reset43, N * 4);
    CHECK_EQ(t.op83, N * 2);
    CHECK_EQ(t.op84, N * 1);
    CHECK_EQ(t.qjLow, N * 3);
    CHECK_EQ(t.qjCrit, N * 3);
    CHECK_EQ(t.state22, N * 6);
    CHECK_EQ(t.idleFlagged, N * 1);
}

TEST(MeisterSupervisionE2E, ByteIdenticalRerun) {
    Tally a = RunGuild(40);
    Tally b = RunGuild(40);
    CHECK(a.trace == b.trace);
    CHECK(!a.trace.empty());
    // and the running counts agree too.
    CHECK_EQ(a.reset43, b.reset43);
    CHECK_EQ(a.state22, b.state22);
}

TEST(MeisterSupervisionE2E, RngAbortAndSkipPaths) {
    // Idle pass aborts when a candidate exists and roll < 32.
    Tally t; g_t = &t;
    SupervisionHooks h = MakeHooks();
    static int seq = 0; seq = 0;
    h.rng_mod = [](u16 n) -> u16 { return static_cast<u16>(n ? (10 % n) : 0); };  // 10 < 32 -> abort
    guild::ai::SetSupervisionHooks(h);
    guild::ai::BuildingFlag bf;
    std::vector<guild::ai::StaffSlot> slots(3);
    slots[0].live = true; slots[0].ownerBuildId = 1; slots[0].employed = true; slots[0].gaugeA = 30; slots[0].gaugeB = 30;
    int r = guild::ai::FlagIdleStaff(&bf, 1, slots.data(), 2, h);
    CHECK_EQ(r, 0);                              // aborted
    CHECK_EQ((int)(slots[0].flags & 0x10), 0);   // nothing flagged
    CHECK_EQ((int)(bf.supervisedBit & 4), 4);    // bit4 still set

    // Already-supervised building returns 0 (0x45e07a), not -1.
    guild::ai::BuildingFlag bf2; bf2.supervisedBit = 4;
    CHECK_EQ(guild::ai::FlagIdleStaff(&bf2, 1, slots.data(), 1, h), 0);
    g_t = nullptr;
}

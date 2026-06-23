// Unit tests for src/ai/meister_supervision.cpp — MeisterAi master-supervision
// pass. Golden expectations hand-derived from the gilde.exe decompiles.
#include "ai/meister_supervision.h"
#include "tests/framework/test.h"

#include <vector>

using namespace guild;
using guild::ai::SupervisionHooks;

namespace {

// Capture state shared by the leaf stubs.
struct Cap {
    int slotResetCount = 0;
    int lastResetSlot = -1, lastResetType = -1, lastResetF7 = -1;
    int op83Count = 0, op84Count = 0;
    int lastOp84Seat = -1;
    int quickjumpCount = 0, lastQjMsg = -1, lastQjOwner = -1;
    int state22Count = 0;
};
Cap* g_cap = nullptr;

// Deterministic RNG replay (clamped to n).
const int* g_rngSeq = nullptr; int g_rngLen = 0, g_rngPos = 0;
u16 RngStub(u16 n) {
    int raw = (g_rngPos < g_rngLen) ? g_rngSeq[g_rngPos] : 0;
    ++g_rngPos;
    return static_cast<u16>(n ? (raw % n) : 0);
}
void SetRng(const int* s, int l) { g_rngSeq = s; g_rngLen = l; g_rngPos = 0; }

SupervisionHooks MakeHooks(Cap& c, bool (*handlerExists)(int,int,int) = nullptr) {
    g_cap = &c;
    SupervisionHooks h;
    h.handler_exists = handlerExists;
    h.queue_slot_reset28 = [](int slot, u8 type, int f7, int) {
        ++g_cap->slotResetCount; g_cap->lastResetSlot = slot;
        g_cap->lastResetType = type; g_cap->lastResetF7 = f7;
    };
    h.request_build_op83 = [](int) { ++g_cap->op83Count; };
    h.request_build_op84 = [](int, int seat) { ++g_cap->op84Count; g_cap->lastOp84Seat = seat; };
    h.rng_mod = RngStub;
    h.send_quickjump = [](int msg, int owner) {
        ++g_cap->quickjumpCount; g_cap->lastQjMsg = msg; g_cap->lastQjOwner = owner;
    };
    h.queue_state22 = [](int) { ++g_cap->state22Count; };
    return h;
}

} // namespace

// --- RequestCmd134 ----------------------------------------------------------
TEST(MeisterSupervision, RequestCmd134) {
    Cap c;
    // pending handler exists -> no emission.
    guild::ai::SetSupervisionHooks(MakeHooks(c, [](int,int,int){ return true; }));
    CHECK_EQ(guild::ai::RequestCmd134(42), false);
    CHECK_EQ(c.slotResetCount, 0);

    // no handler -> emit type 134 tagged with master build id.
    Cap c2;
    guild::ai::SetSupervisionHooks(MakeHooks(c2, [](int,int,int){ return false; }));
    CHECK_EQ(guild::ai::RequestCmd134(42), true);
    CHECK_EQ(c2.slotResetCount, 1);
    CHECK_EQ(c2.lastResetSlot, 42);
    CHECK_EQ(c2.lastResetType, 134);
}

// --- RequestBuildingCmd43 (sticky `found`) ----------------------------------
TEST(MeisterSupervision, RequestBuildingCmd43_AllUnmatched) {
    Cap c;
    guild::ai::SetSupervisionHooks(MakeHooks(c, [](int,int,int){ return false; }));
    int ids[3] = {10, 20, 30};
    CHECK_EQ(guild::ai::RequestBuildingCmd43(/*master*/ 7, ids, 3), 3);
    CHECK_EQ(c.slotResetCount, 3);
    CHECK_EQ(c.lastResetType, 43);
    CHECK_EQ(c.lastResetSlot, 7);
    CHECK_EQ(c.lastResetF7, 30);  // last person id forwarded
}

TEST(MeisterSupervision, RequestBuildingCmd43_StickyStops) {
    // handler_exists true for owner==20 only; once it matches, `found` stays set
    // for the rest of the loop -> only ids[0] (10) emits.
    Cap c;
    guild::ai::SetSupervisionHooks(MakeHooks(c, [](int,int,int owner){ return owner == 20; }));
    int ids[3] = {10, 20, 30};
    CHECK_EQ(guild::ai::RequestBuildingCmd43(7, ids, 3), 1);
    CHECK_EQ(c.lastResetF7, 10);  // only the first person emitted
}

// --- ClearDarkCorner --------------------------------------------------------
TEST(MeisterSupervision, ClearDarkCorner) {
    Cap c;
    guild::ai::SetSupervisionHooks(MakeHooks(c));
    int INTMIN = (-2147483647 - 1);
    // day 100: last=98 -> diff 2 (>1) emit; last=99 -> diff 1 (not >1) skip;
    // last=50 -> emit; unresolved -> skip.
    int last[4] = {98, 99, 50, INTMIN};
    CHECK_EQ(guild::ai::ClearDarkCorner(100, last, 4), 2);
    CHECK_EQ(c.op83Count, 2);
}

// --- SuperviseStammtisch ----------------------------------------------------
TEST(MeisterSupervision, SuperviseStammtisch) {
    Cap c;
    guild::ai::SetSupervisionHooks(MakeHooks(c));
    std::vector<guild::ai::StammtischSeat> seats(5);
    seats[0].occupantId = -1;                                  // empty -> skip
    seats[1].occupantId = 11; seats[1].recordFound = true;
    seats[1].active = true; seats[1].state = 0;                // healthy -> skip
    seats[2].occupantId = 12; seats[2].recordFound = false;    // gone -> emit
    seats[3].occupantId = 13; seats[3].active = false;         // inactive -> emit
    seats[4].occupantId = 14; seats[4].state = 15;             // state 15 -> emit
    CHECK_EQ(guild::ai::SuperviseStammtisch(seats.data(), 5), 3);
    CHECK_EQ(c.op84Count, 3);
    CHECK_EQ(c.lastOp84Seat, 14);
}

// --- FlagIdleStaff ----------------------------------------------------------
TEST(MeisterSupervision, FlagIdleStaff_AlreadySupervised) {
    Cap c;
    guild::ai::BuildingFlag b; b.supervisedBit = 4;  // bit4 already set
    std::vector<guild::ai::StaffSlot> slots(1);
    // already supervised (bit4 set) -> the original returns 0 (0x45e07a), not -1.
    CHECK_EQ(guild::ai::FlagIdleStaff(&b, 1, slots.data(), 1, MakeHooks(c)), 0);
}

TEST(MeisterSupervision, FlagIdleStaff_CandidateRollAbort) {
    Cap c;
    guild::ai::BuildingFlag b;  // bit4 clear
    std::vector<guild::ai::StaffSlot> slots(1);
    slots[0].live = true; slots[0].ownerBuildId = 1; slots[0].employed = true;
    slots[0].hasActionObj = false;
    slots[0].gaugeA = 100; slots[0].gaugeB = 100;  // < 168 -> idle candidate
    // rng_mod(128) = 0 < 32 -> abort, no flag.
    int seq[1] = {0}; SetRng(seq, 1);
    CHECK_EQ(guild::ai::FlagIdleStaff(&b, 1, slots.data(), 1, MakeHooks(c)), 0);
    CHECK_EQ((int)(slots[0].flags & 0x10), 0);
    CHECK_EQ((int)(b.supervisedBit & 4), 4);  // bit4 set regardless
}

TEST(MeisterSupervision, FlagIdleStaff_CandidateFlags) {
    Cap c;
    guild::ai::BuildingFlag b;
    std::vector<guild::ai::StaffSlot> slots(2);
    for (auto& s : slots) { s.live = true; s.ownerBuildId = 1; s.employed = true; s.gaugeA = 100; s.gaugeB = 100; }
    // rng_mod(128) = 64 -> not < 32 -> proceed; both idle slots get bit 0x10.
    int seq[1] = {64}; SetRng(seq, 1);
    CHECK_EQ(guild::ai::FlagIdleStaff(&b, 1, slots.data(), 2, MakeHooks(c)), 1);
    CHECK_EQ((int)(slots[0].flags & 0x10), 0x10);
    CHECK_EQ((int)(slots[1].flags & 0x10), 0x10);
}

TEST(MeisterSupervision, FlagIdleStaff_NoCandidateFreshSkip) {
    Cap c;
    guild::ai::BuildingFlag b;
    // No idle candidate: slot[0] has high gauges. After scanning slot[0] the
    // trailing index v3 == 1, so the "fresh staff" reprieve reads slot[1]'s
    // gauges (the original indexes byte_12CE993[536*v3]). Provide slot[1] fresh
    // (< 252) so the reprieve gate fires and, with roll > 96, the pass skips.
    // Over-allocate by one so the trailing probe (index == scanned count) is in
    // bounds, mirroring the real 768-entry array the original relies on.
    std::vector<guild::ai::StaffSlot> slots(2);
    slots[0].live = true; slots[0].ownerBuildId = 1; slots[0].employed = true;
    slots[0].gaugeA = 200; slots[0].gaugeB = 200;  // not idle (>= 168) -> no candidate
    slots[1].gaugeA = 100; slots[1].gaugeB = 100;  // fresh probe target slot[v3==1] (< 252)
    int seq[1] = {100}; SetRng(seq, 1);            // 100 > 96 -> skip
    // Pass slotCount=1: the loop scans slot[0] (no candidate), v3 becomes 1, the
    // reprieve reads slot[1] from the over-allocated buffer.
    CHECK_EQ(guild::ai::FlagIdleStaff(&b, 1, slots.data(), 1, MakeHooks(c)), 0);
    CHECK_EQ((int)(slots[0].flags & 0x10), 0);
}

// --- CountStaffByType -------------------------------------------------------
TEST(MeisterSupervision, CountStaffByType_Flat) {
    // head (trade 23 = master), two direct members: trade 37 (master) + trade 5
    // (other). No subtrees.
    guild::ai::StaffNode head; head.trade = 23; head.counted = false;
    std::vector<guild::ai::StaffNode> members(2);
    // members live right after head: build a contiguous block.
    std::vector<guild::ai::StaffNode> block(3);
    block[0] = head;
    block[1].trade = 37; block[1].counted = false; block[1].childTree = -1;
    block[2].trade = 5;  block[2].counted = false; block[2].childTree = -1;
    auto r = guild::ai::CountStaffByType(&block[0], /*headChildCount*/ 2,
                                         nullptr, 0, nullptr, 0);
    CHECK_EQ(r.masters, 2);  // head(23) + member(37)
    CHECK_EQ(r.others, 1);   // member(5)
}

TEST(MeisterSupervision, CountStaffByType_HeadAlreadyCounted) {
    std::vector<guild::ai::StaffNode> block(2);
    block[0].trade = 23; block[0].counted = true;   // head pre-counted -> skip
    block[1].trade = 23; block[1].counted = false;  // member master
    auto r = guild::ai::CountStaffByType(&block[0], 1, nullptr, 0, nullptr, 0);
    CHECK_EQ(r.masters, 1);
    CHECK_EQ(r.others, 0);
}

// --- UpdateBuildingHealthState ----------------------------------------------
TEST(MeisterSupervision, UpdateBuildingHealthState) {
    Cap c;
    std::vector<guild::ai::HealthEmp> emps(4);
    // [0] production, ratio 0.6 >= 0.5 -> low (5297) + delta.
    emps[0].personId = 1; emps[0].trade = 6; emps[0].curHealth = 60; emps[0].maxHealth = 100;
    emps[0].ownerId = 50;
    // [1] production, ratio 0.4 < 0.5 -> critical (5296) + delta.
    emps[1].personId = 2; emps[1].trade = 7; emps[1].curHealth = 40; emps[1].maxHealth = 100;
    emps[1].ownerId = 51;
    // [2] non-production trade -> no quickjump, but delta still queued.
    emps[2].personId = 3; emps[2].trade = 2; emps[2].curHealth = 10; emps[2].maxHealth = 100;
    // [3] noUpdate bit -> skipped entirely.
    emps[3].personId = 4; emps[3].trade = 6; emps[3].noUpdate = true; emps[3].maxHealth = 100;
    int queued = guild::ai::UpdateBuildingHealthState(emps.data(), 4, MakeHooks(c));
    CHECK_EQ(queued, 3);           // emps 0,1,2 (3 skipped)
    CHECK_EQ(c.state22Count, 3);
    CHECK_EQ(c.quickjumpCount, 2); // only the two production emps
    CHECK_EQ(c.lastQjMsg, 5296);   // last quickjump was the critical one
    CHECK_EQ(c.lastQjOwner, 51);
}

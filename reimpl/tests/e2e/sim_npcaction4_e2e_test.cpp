// e2e for sim/npcaction4 — drive synthetic NPCs through two full routines
// tick-by-tick and verify the state progression + emitted commands against a
// hand-computed reference:
//   (1) a guard patrol loop: escort-to-target (state 0) -> dispatch/return (state 1)
//       and the empty-squad early-out.
//   (2) a city-watch raid: arm (state -2) -> escort (state 0) -> wait (state 1) ->
//       combat resolve (state 3) -> packet wait (state 4) -> cutscene (state 5) ->
//       home + teardown (state 2 / -1).
#include "sim/npcaction4.h"
#include "sim/npcaction.h"
#include "sim/gametime.h"
#include "tests/framework/test.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

struct FakeObj { i32 id; u8 kind; bool hasChar; u16 owner; bool active; };

struct E2EScene {
    std::vector<std::string> log;
    std::vector<FakeObj> objs;
    bool storableExists = true;
    i32 packetStatus = 1;
    i32 packetSeq = 808;
    bool combatDetect = false;
    bool handlerExists = false;
    FakeObj storable{9001, 0, false, 0xFFFF, false};

    FakeObj* byId(i32 id) {
        for (auto& o : objs) if (o.id == id) return &o;
        return nullptr;
    }
    void add(i32 id, u8 k, bool hc, u16 own = 0xFFFF, bool act = false) {
        objs.push_back({id, k, hc, own, act});
    }
};

E2EScene* g_s = nullptr;
void log(const std::string& s) { g_s->log.push_back(s); }
bool LogHas(const std::string& s) {
    for (auto& e : g_s->log) if (e == s) return true;
    return false;
}

i32 e_q29(int a, HeRecord*) { log("e29(" + std::to_string(a) + ")"); return 4242; }
i32 e_status(i32) { return g_s->packetStatus; }
i32 e_seq(i32) { return g_s->packetSeq; }
i32 e_free(HeRecord*) { log("free"); return 0; }
void e_s49(i32 id) { log("s49(" + std::to_string(id) + ")"); }
void e_n53(i32 p, i32 o, int, i32 t, int f, const char* n) {
    log("n53(" + std::to_string(p) + "," + std::to_string(o) + "," + std::to_string(t) +
        ",f" + std::to_string(f) + "," + std::string(n) + ")");
}
void e_flag55(i32 id, u8 f) { log("flag55(" + std::to_string(id) + "," + std::to_string(f) + ")"); }
i32 e_r39() { log("r39"); return 5050; }
i32 e_cityid(u16 i) { return 1000 + i; }
void* e_qbegin(i32 f) { return g_s->byId(f); }
void* e_find(i32 id) { return g_s->byId(id); }
void* e_storable(void*) { return g_s->storableExists ? &g_s->storable : nullptr; }
i32 e_objid(void* p) { return p ? static_cast<FakeObj*>(p)->id : -1; }
i32 e_pid(void* p) { return p ? static_cast<FakeObj*>(p)->id : -1; }
u16 e_marker(void* p) { return p ? static_cast<u16>(static_cast<FakeObj*>(p)->kind) : 0; }
bool e_haschar(void* p) { return p ? static_cast<FakeObj*>(p)->hasChar : false; }
u16 e_owner(void* p) { return p ? static_cast<FakeObj*>(p)->owner : 0xFFFF; }
u8 e_kind(void* p) { return p ? static_cast<FakeObj*>(p)->kind : 0; }
i32 e_active(void* p) { return (p && static_cast<FakeObj*>(p)->active) ? 1 : 0; }
void e_chg(void*, HeRecord*, u16 kw) { log("chg(" + std::to_string(kw) + ")"); }
void* e_handler(i32) { return g_s->handlerExists ? reinterpret_cast<void*>(0x2) : nullptr; }
i32 e_hrec(void*) { return -1; }
void e_msg(i32 id, int t) { log("msg(" + std::to_string(id) + "," + std::to_string(t) + ")"); }
i32 e_wage(void*) { return 25; }
bool e_combat(int, int, bool a) { log(std::string("combat(") + (a ? "atk" : "raid") + ")"); return g_s->combatDetect; }
i32 e_strength(void*, void*) { return 7; }

NpcAction4Hooks MakeHooks() {
    NpcAction4Hooks H{};
    H.queueRequestEntity29 = e_q29;
    H.packetStatus = e_status;
    H.packetSeq = e_seq;
    H.freeHandlerEntry = e_free;
    H.queueSingle49 = e_s49;
    H.queueNamedObject53 = e_n53;
    H.queueFlag55 = e_flag55;
    H.queueRequest39 = e_r39;
    H.cityIdFromIndex = e_cityid;
    H.personQueryBegin = e_qbegin;
    H.findPersonById = e_find;
    H.findStorableObject = e_storable;
    H.objectIdField = e_objid;
    H.personIdField = e_pid;
    H.personMarkerWord = e_marker;
    H.personHasCharacter = e_haschar;
    H.personOwnerWord = e_owner;
    H.personKind = e_kind;
    H.personActionActive = e_active;
    H.changePlayerAction = e_chg;
    H.findHandlerByFilter = e_handler;
    H.handlerRecordId = e_hrec;
    H.sendMessage = e_msg;
    H.patrolWageRoll = e_wage;
    H.combatAttackRoll = e_combat;
    H.combatStrength = e_strength;
    return H;
}

HeRecord* NewHe() { HeRecord* h = new HeRecord(); std::memset(h, 0, sizeof(HeRecord)); return h; }
void SetClock(int d, int hr, int m, int s) {
    GameTime t{}; t.day = d; t.hour = static_cast<u16>(hr); t.minute = m; t.second = s;
    SetNpcClock(t);
}

} // namespace

// ---------------------------------------------------------------------------
// (1) Patrol loop: state 0 escort -> state 1 dispatch (wage payout).
// ---------------------------------------------------------------------------
TEST(NpcAction4_E2E_Patrol, escort_then_dispatch_payout) {
    E2EScene s; g_s = &s;
    NpcAction4Hooks H = MakeHooks(); SetNpcAction4Hooks(&H);
    s.add(100, 0, true);      // patrol target
    s.add(200, 0, true);      // home
    s.add(11, 5, true);       // guard member
    SetClock(10, 9, 0, 0);

    HeRecord* h = NewHe();
    He_State(h) = 0;
    He_PatrolForce1024(h) = -1;
    He_FilterA4(h) = 100;
    He_FilterB4(h) = 200;
    He_CityIndex(h) = 3;
    He_PatrolMember(h, 0) = 11;
    He_PatrolMember(h, 1) = -1;
    He_PatrolMember(h, 2) = -1;
    He_PatrolMember(h, 3) = -1;

    // Tick 1: state 0 -> escort to target, entity29(2), +2 min.
    NpcAction4_PatrolStep(h);
    CHECK(LogHas("n53(11,100,-1,f1,Streife)"));
    CHECK(LogHas("e29(2)"));
    int m1 = He_ApptTime(h).minute;
    CHECK_EQ(m1, 2);

    // Tick 2: drive state 1 (return-home wage payout). cmd15 not installed here,
    // but the wage message goes to the player id (cityIdFromIndex(3) = 1003) and the
    // routine re-arms entity29(-1) for the next appointment.
    s.log.clear();
    He_State(h) = 1;
    SetClock(10, 18, 0, 0);
    NpcAction4_PatrolStep(h);
    CHECK(LogHas("msg(1003,5751)"));
    CHECK(LogHas("e29(-1)"));
    delete h;
}

// ---------------------------------------------------------------------------
// (2) Raid: arm -> escort -> wait -> resolve -> packet wait -> cutscene -> home.
// ---------------------------------------------------------------------------
TEST(NpcAction4_E2E_Raid, full_raid_cycle) {
    E2EScene s; g_s = &s;
    NpcAction4Hooks H = MakeHooks(); SetNpcAction4Hooks(&H);
    s.combatDetect = false;            // escorts engage -> cmd39 path
    s.add(100, 0, true, 5);            // raid target (owner != 0xFFFF)
    s.add(200, 0, true);               // home/owner building
    s.add(21, 5, true, 0xFFFF, true);  // escort, initially "active" (walking)
    SetClock(12, 8, 0, 0);

    HeRecord* h = NewHe();
    He_State(h) = 0;
    He_Flags(h) = 2;
    He_ReqHandle(h) = -1;
    He_FilterA4(h) = 100;
    He_FilterB4(h) = 200;
    He_CityIndex(h) = 4;
    He_Member8(h, 0) = 21;
    for (int i = 1; i < 8; ++i) He_Member8(h, i) = -1;

    // Tick 1: state 0 (case 1) escort to target -> entity29(2), +2 min.
    NpcAction4_RaidStep(h);
    CHECK(LogHas("e29(2)"));
    CHECK(LogHas("n53(21,100,-1,f0,Razzia)"));

    // Tick 2: state 2 (case 3) arrived-wait — escort still active -> entity29(2).
    s.log.clear();
    He_State(h) = 2;
    He_ReqHandle(h) = -1;
    NpcAction4_RaidStep(h);
    CHECK(LogHas("e29(2)"));

    // Tick 3: escort arrived (not active) -> entity29(3).
    s.log.clear();
    s.byId(21)->active = false;
    He_State(h) = 2;
    He_ReqHandle(h) = -1;
    NpcAction4_RaidStep(h);
    CHECK(LogHas("e29(3)"));

    // Tick 4: state 3 (case 4) combat resolve -> escorts present -> cmd39, state 4.
    s.log.clear();
    He_State(h) = 3;
    He_ReqHandle(h) = -1;
    NpcAction4_RaidStep(h);
    CHECK(LogHas("combat(raid)"));
    CHECK(LogHas("r39"));
    CHECK_EQ(He_CombatPacket(h), 5050);
    CHECK_EQ(He_State(h), 4);

    // Tick 5: state 4 packet wait -> applied -> seq -> +184, state 5.
    s.log.clear();
    s.packetStatus = 1;
    s.packetSeq = 9090;
    He_State(h) = 4;
    He_ReqHandle(h) = -1;
    NpcAction4_RaidStep(h);
    CHECK_EQ(He_CombatSeq(h), 9090);
    CHECK_EQ(He_State(h), 5);

    // Tick 6: state 5 cutscene -> done (handler absent) -> +1s, back to state 1.
    s.log.clear();
    s.handlerExists = false;
    He_State(h) = 5;
    He_ReqHandle(h) = -1;
    NpcAction4_RaidStep(h);
    CHECK_EQ(He_State(h), 1);
    CHECK_EQ((int)He_ApptTime(h).second, 1);

    // Tick 7: state -1 teardown (flag 0x02 set) -> recall escorts to home + free.
    s.log.clear();
    He_State(h) = -1;
    He_ReqHandle(h) = -1;
    NpcAction4_RaidStep(h);
    CHECK(LogHas("free"));
    CHECK(LogHas("s49(21)"));
    CHECK(LogHas("n53(21,200,9001,f0,Razzia)"));   // home obj + storable

    delete h;
}

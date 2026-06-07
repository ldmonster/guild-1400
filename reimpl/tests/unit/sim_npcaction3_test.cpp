// Unit tests for sim/npcaction3 — the big NpcAction state machines (Burglary,
// JailCell, Recruitment). Each test drives the He handler record through the
// expected state transitions with a recording mock for the leaf hooks, asserting
// golden state at each step, timer deltas, and branch coverage of the state switch.
#include "sim/npcaction3.h"
#include "sim/npcaction.h"   // NpcClock / SetNpcClock
#include "sim/gametime.h"
#include "crt/rand.h"
#include "tests/framework/test.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// ---------------------------------------------------------------------------
// Recording mock for NpcAction3Hooks. A "scene" of synthetic persons/objects is
// described by simple maps; emitted commands are captured into a log.
// ---------------------------------------------------------------------------
struct FakeObj { i32 id; u8 kind; bool hasChar; };

struct Recorder {
    // emitted command log entries (one string per emit, in order)
    std::vector<std::string> log;
    // person/object id -> handle (we use the FakeObj* as the opaque handle)
    std::vector<FakeObj> objs;
    // controllable query/proximity results
    i32 queryResult = -2;             // -2 = "use filterId as the obj id" sentinel
    bool storableExists = true;
    bool nearDoorAll = true;          // every member is at the door
    i32 packetStatusResult = 1;       // applied
    i32 packetSeqResult = 777;
    bool jailEscape = false;
    bool burglaryDetect = false;
    i32 loot = 0;
    i32 proximity = 1;
    i32 reciprocate = 0;
    u8 cost = 1;

    FakeObj storable{9001, 0, false};

    FakeObj* byId(i32 id) {
        for (auto& o : objs)
            if (o.id == id)
                return &o;
        return nullptr;
    }
    void add(i32 id, u8 kind, bool hasChar) { objs.push_back({id, kind, hasChar}); }
};

Recorder* g_rec = nullptr;

void log(const std::string& s) { if (g_rec) g_rec->log.push_back(s); }

// hook impls --------------------------------------------------------------
i32 h_queue29(int arg, HeRecord*) { log("entity29(" + std::to_string(arg) + ")"); return 4242; }
i32 h_status(i32) { return g_rec->packetStatusResult; }
i32 h_seq(i32) { return g_rec->packetSeqResult; }
i32 h_free(HeRecord*) { log("free"); return 0; }
void h_single49(i32 id) { log("single49(" + std::to_string(id) + ")"); }
void h_named53(i32 pid, i32 obj, int, i32 tgt, int flag, const char* name) {
    log("named53(" + std::to_string(pid) + "," + std::to_string(obj) + "," +
        std::to_string(tgt) + ",f" + std::to_string(flag) + "," + std::string(name) + ")");
}
void h_chrmove(i32 pid, i32 obj, const char* name, i32 slot) {
    log("chrmove(" + std::to_string(pid) + "," + std::to_string(obj) + "," +
        std::string(name) + "," + std::to_string(slot) + ")");
}
void h_flag55(i32 id, u8 f) { log("flag55(" + std::to_string(id) + "," + std::to_string(f) + ")"); }
void h_pair36(i32 seq, i32 v) { log("pair36(" + std::to_string(seq) + "," + std::to_string(v) + ")"); }
void h_quad43(i32 obj, i32, i32, i32 st) { log("quad43(" + std::to_string(obj) + "," + std::to_string(st) + ")"); }
void h_req16(i32 a, i32 b, i32 amt, u8) { log("req16(" + std::to_string(a) + "," + std::to_string(b) + "," + std::to_string(amt) + ")"); }
void h_setfield(i32 obj, int w, i32 v, int off) {
    log("setfield(" + std::to_string(obj) + ",w" + std::to_string(w) + ",v" +
        std::to_string(v) + ",o" + std::to_string(off) + ")");
}
void h_slot28(i32 a, i32 c) { log("slot28(" + std::to_string(a) + "," + std::to_string(c) + ")"); }
void h_op91(i32 id, int k) { log("op91(" + std::to_string(id) + "," + std::to_string(k) + ")"); }
void h_args25(i32 id, int a, int b, int c, int d) {
    log("args25(" + std::to_string(id) + "," + std::to_string(a) + "," + std::to_string(b) +
        "," + std::to_string(c) + "," + std::to_string(d) + ")");
}
void h_bstart(const char* n) { log("bstart(" + std::string(n) + ")"); }
void h_bend() { log("bend"); }

void* h_qbegin(i32 filterId) {
    if (g_rec->queryResult == -2) {
        FakeObj* o = g_rec->byId(filterId);
        return o;   // resolve the filter id to the matching synthetic obj
    }
    return g_rec->queryResult ? &g_rec->storable : nullptr;
}
void* h_findById(i32 id) { return g_rec->byId(id); }
void* h_findBldg(i32 id) { return g_rec->byId(id); }
void* h_storable(void*) { return g_rec->storableExists ? &g_rec->storable : nullptr; }
bool h_neardoor(void*, void*) { return g_rec->nearDoorAll; }
i32 h_pid(void* p) { return p ? static_cast<FakeObj*>(p)->id : -1; }
u8  h_pkind(void* p) { return p ? static_cast<FakeObj*>(p)->kind : 0; }
u16 h_pmarker(void* p) { return p ? static_cast<u16>(static_cast<FakeObj*>(p)->kind) : 0; }
bool h_phaschar(void* p) { return p ? static_cast<FakeObj*>(p)->hasChar : false; }
i32 h_objid(void* p) { return p ? static_cast<FakeObj*>(p)->id : -1; }
void h_chgaction(void*, HeRecord*, u16 kw) { log("chgaction(kw" + std::to_string(kw) + ")"); }
i32 h_proximity(i32 a, i32 b) { (void)a; (void)b; return g_rec->proximity; }
u8  h_cost(int, i32) { return g_rec->cost; }
void h_wander(HeRecord*, void*) { log("wander"); }
void h_msg(i32 id, int t) { log("msg(" + std::to_string(id) + "," + std::to_string(t) + ")"); }
i32 h_violation(int crime, i32 v, i32 c, i32 p) {
    log("violation(" + std::to_string(crime) + "," + std::to_string(v) + ")"); (void)c; (void)p;
    return 555;
}
bool h_escape(void*, void*) { return g_rec->jailEscape; }
i32  h_loot(void*, void*, int n) { (void)n; return g_rec->loot; }
bool h_detect(void*) { return g_rec->burglaryDetect; }

NpcAction3Hooks MakeHooks() {
    NpcAction3Hooks H{};
    H.queueRequestEntity29 = h_queue29;
    H.packetStatus = h_status;
    H.packetSeq = h_seq;
    H.freeHandlerEntry = h_free;
    H.queueSingle49 = h_single49;
    H.queueNamedObject53 = h_named53;
    H.requestChrMove = h_chrmove;
    H.queueFlag55 = h_flag55;
    H.queuePair36 = h_pair36;
    H.queueQuad43 = h_quad43;
    H.queueRequest16 = h_req16;
    H.setEntityField = h_setfield;
    H.queueSlotReset28 = h_slot28;
    H.requestBuildOp91 = h_op91;
    H.enqueueArgs25 = h_args25;
    H.enqueueBuildingActionStart = h_bstart;
    H.enqueueBuildingActionEnd = h_bend;
    H.personQueryBegin = h_qbegin;
    H.findPersonById = h_findById;
    H.findBuildingById = h_findBldg;
    H.findStorableObject = h_storable;
    H.personNearDoor = h_neardoor;
    H.personId = h_pid;
    H.personKind = h_pkind;
    H.personMarkerWord = h_pmarker;
    H.personHasCharacter = h_phaschar;
    H.objectIdField = h_objid;
    H.changePlayerAction = h_chgaction;
    H.recruitProximity = h_proximity;
    H.recruitCost = h_cost;
    H.computeWanderPath = h_wander;
    H.sendMessage = h_msg;
    H.evaluateViolation = h_violation;
    H.jailEscapeRoll = h_escape;
    H.burglaryLootValuation = h_loot;
    H.burglaryDetectionRoll = h_detect;
    return H;
}

// Allocate a zeroed He record on the heap, large enough for the tail fields.
HeRecord* NewHe() {
    HeRecord* h = new HeRecord();
    std::memset(h, 0, sizeof(HeRecord));
    return h;
}

void SetClock(int day, int hour, int minute, int second) {
    GameTime t{};
    t.day = day; t.hour = static_cast<u16>(hour); t.minute = minute; t.second = second;
    SetNpcClock(t);
}

bool LogHas(const std::string& s) {
    for (auto& e : g_rec->log)
        if (e == s)
            return true;
    return false;
}

} // namespace

// ===========================================================================
// Burglary
// ===========================================================================
TEST(NpcAction3_Burglary, EscortOut_State0_advances4min_to_state1) {
    Recorder rec; g_rec = &rec;
    NpcAction3Hooks H = MakeHooks(); SetNpcAction3Hooks(&H);
    SetClock(10, 9, 0, 0);

    HeRecord* h = NewHe();
    He_State(h) = 0;                 // switch index 2
    He_FilterA(h) = 100;             // victim building
    He_FilterB(h) = 200;
    He_CityId(h) = 7;
    He_MemberId8(h, 0) = 11;
    He_MemberId8(h, 1) = 12;
    He_MemberId8(h, 2) = -1;
    rec.add(100, 0, true);           // victim building
    rec.add(11, 5, true);
    rec.add(12, 6, true);

    NpcAction3_BurglaryStep(h);

    CHECK_EQ(He_State(h), 1);                         // -> state 1
    CHECK_EQ((int)He_ApptTime(h).minute, 4);          // +4 min
    CHECK(LogHas("single49(11)"));
    CHECK(LogHas("single49(12)"));
    CHECK(LogHas("named53(11,100,-1,f1,Einbruch)"));
    delete h;
}

TEST(NpcAction3_Burglary, Approach_State1_notAtDoor_stays_plus4min) {
    Recorder rec; g_rec = &rec;
    NpcAction3Hooks H = MakeHooks(); SetNpcAction3Hooks(&H);
    rec.nearDoorAll = false;
    SetClock(10, 9, 0, 0);

    HeRecord* h = NewHe();
    He_State(h) = 1;
    He_FilterA(h) = 100;
    He_MemberId8(h, 0) = 11;
    rec.add(100, 0, true);
    rec.add(11, 5, true);

    NpcAction3_BurglaryStep(h);

    CHECK_EQ(He_State(h), 1);                  // stays
    CHECK_EQ((int)He_ApptTime(h).minute, 4);   // +4 min
    delete h;
}

TEST(NpcAction3_Burglary, Approach_State1_allAtDoor_to_state2_plus1sec) {
    Recorder rec; g_rec = &rec;
    NpcAction3Hooks H = MakeHooks(); SetNpcAction3Hooks(&H);
    rec.nearDoorAll = true;
    SetClock(10, 9, 0, 0);

    HeRecord* h = NewHe();
    He_State(h) = 1;
    He_FilterA(h) = 100;
    He_MemberId8(h, 0) = 11;
    rec.add(100, 0, true);
    rec.add(11, 5, true);

    NpcAction3_BurglaryStep(h);

    CHECK_EQ(He_State(h), 2);                  // -> state 2
    CHECK_EQ((int)He_ApptTime(h).second, 1);   // +1 sec
    delete h;
}

TEST(NpcAction3_Burglary, BreakIn_State2_violation_to_state3) {
    Recorder rec; g_rec = &rec;
    NpcAction3Hooks H = MakeHooks(); SetNpcAction3Hooks(&H);
    SetClock(10, 9, 0, 0);

    HeRecord* h = NewHe();
    He_State(h) = 2;
    He_FilterA(h) = 100;
    He_FilterB(h) = 200;
    He_CityId(h) = 7;
    rec.add(100, 0, true);

    NpcAction3_BurglaryStep(h);

    CHECK_EQ(He_State(h), 3);
    CHECK_EQ(He_ViolationPk(h), 555);
    CHECK_EQ(He_TargetFilter(h), 100);
    CHECK_EQ(He_TargetPersonId(h), 200);
    CHECK(LogHas("violation(22,100)"));
    delete h;
}

TEST(NpcAction3_Burglary, BreakIn_State2_noTarget_to_state5_plus15min) {
    Recorder rec; g_rec = &rec;
    NpcAction3Hooks H = MakeHooks(); SetNpcAction3Hooks(&H);
    SetClock(10, 9, 0, 0);

    HeRecord* h = NewHe();
    He_State(h) = 2;
    He_FilterA(h) = 999;   // no such building -> query returns null
    NpcAction3_BurglaryStep(h);

    CHECK_EQ(He_State(h), 5);
    CHECK_EQ((int)He_ApptTime(h).minute, 15);
    delete h;
}

TEST(NpcAction3_Burglary, Gate_State3_packetPending_to_state4) {
    Recorder rec; g_rec = &rec;
    NpcAction3Hooks H = MakeHooks(); SetNpcAction3Hooks(&H);
    rec.packetStatusResult = 1;   // applied
    rec.packetSeqResult = 888;
    SetClock(10, 9, 0, 0);

    HeRecord* h = NewHe();
    He_State(h) = 3;
    He_ViolationPk(h) = 555;
    He_TargetPersonId(h) = 200;

    NpcAction3_BurglaryStep(h);

    CHECK_EQ(He_State(h), 4);
    CHECK(LogHas("pair36(888,200)"));
    delete h;
}

TEST(NpcAction3_Burglary, Gate_State3_stillPending_returns_noChange) {
    Recorder rec; g_rec = &rec;
    NpcAction3Hooks H = MakeHooks(); SetNpcAction3Hooks(&H);
    rec.packetStatusResult = 0;   // pending
    SetClock(10, 9, 0, 0);

    HeRecord* h = NewHe();
    He_State(h) = 3;
    He_ViolationPk(h) = 555;

    NpcAction3_BurglaryStep(h);

    CHECK_EQ(He_State(h), 3);   // unchanged
    CHECK(rec.log.empty());
    delete h;
}

TEST(NpcAction3_Burglary, Steal_State4_resolves_to_minus1_and_loot) {
    Recorder rec; g_rec = &rec;
    NpcAction3Hooks H = MakeHooks(); SetNpcAction3Hooks(&H);
    rec.packetSeqResult = 0;   // packet applied (no longer pending)
    rec.loot = 1500;
    SetClock(10, 9, 0, 0);

    HeRecord* h = NewHe();
    He_State(h) = 4;
    He_ViolationPk(h) = -1;    // no pending violation packet
    He_FilterA(h) = 100;
    He_FilterB(h) = 200;
    He_MemberId8(h, 0) = 11;
    rec.add(100, 0, true);
    rec.add(200, 0, true);
    rec.add(11, 5, true);

    NpcAction3_BurglaryStep(h);

    CHECK_EQ(He_State(h), -1);
    CHECK(LogHas("req16(100,200,1500)"));
    delete h;
}

TEST(NpcAction3_Burglary, Teardown_state_minus1_frees) {
    Recorder rec; g_rec = &rec;
    NpcAction3Hooks H = MakeHooks(); SetNpcAction3Hooks(&H);
    SetClock(10, 9, 0, 0);

    HeRecord* h = NewHe();
    He_State(h) = -1;          // switch index 1 -> teardown
    He_FilterB(h) = 200;
    He_MemberId8(h, 0) = 11;
    rec.add(200, 0, true);
    rec.add(11, 5, true);

    NpcAction3_BurglaryStep(h);

    CHECK(LogHas("free"));
    CHECK(LogHas("single49(11)"));
    delete h;
}

// ===========================================================================
// JailCell
// ===========================================================================
TEST(NpcAction3_JailCell, EscortToCell_State0_plus30min_to_state1) {
    Recorder rec; g_rec = &rec;
    NpcAction3Hooks H = MakeHooks(); SetNpcAction3Hooks(&H);
    SetClock(12, 14, 0, 0);

    HeRecord* h = NewHe();
    He_State(h) = 0;
    He_FilterB(h) = 300;        // cell building
    He_MemberId8(h, 0) = 21;
    rec.add(300, 0, true);
    rec.add(21, 5, true);

    NpcAction3_JailCellStep(h);

    CHECK_EQ(He_State(h), 1);
    CHECK_EQ((int)He_ApptTime(h).minute, 30);
    CHECK(LogHas("single49(21)"));
    delete h;
}

TEST(NpcAction3_JailCell, WaitAtDoor_State1_allAtDoor_to_state2) {
    Recorder rec; g_rec = &rec;
    NpcAction3Hooks H = MakeHooks(); SetNpcAction3Hooks(&H);
    rec.nearDoorAll = true;
    SetClock(12, 14, 0, 0);

    HeRecord* h = NewHe();
    He_State(h) = 1;
    He_FilterB(h) = 300;
    He_MemberId8(h, 0) = 21;
    rec.add(300, 0, true);
    rec.add(21, 5, true);

    NpcAction3_JailCellStep(h);

    CHECK_EQ(He_State(h), 2);
    CHECK_EQ((int)He_ApptTime(h).second, 1);
    delete h;
}

TEST(NpcAction3_JailCell, WaitAtDoor_State1_notAll_stays_plus10min) {
    Recorder rec; g_rec = &rec;
    NpcAction3Hooks H = MakeHooks(); SetNpcAction3Hooks(&H);
    rec.nearDoorAll = false;
    SetClock(12, 14, 0, 0);

    HeRecord* h = NewHe();
    He_State(h) = 1;
    He_FilterB(h) = 300;
    He_MemberId8(h, 0) = 21;
    rec.add(300, 0, true);
    rec.add(21, 5, true);

    NpcAction3_JailCellStep(h);

    CHECK_EQ(He_State(h), 1);
    CHECK_EQ((int)He_ApptTime(h).minute, 10);
    delete h;
}

TEST(NpcAction3_JailCell, Resolve_State2_escape_transfers_and_frees) {
    Recorder rec; g_rec = &rec;
    NpcAction3Hooks H = MakeHooks(); SetNpcAction3Hooks(&H);
    rec.jailEscape = true;
    SetClock(12, 14, 0, 0);

    HeRecord* h = NewHe();
    He_State(h) = 2;
    He_FilterA(h) = 21;   // arrestee
    He_FilterB(h) = 300;  // cell
    He_MemberId8(h, 0) = 22;
    rec.add(21, 5, true);
    rec.add(300, 0, true);
    rec.add(22, 5, true);

    NpcAction3_JailCellStep(h);

    CHECK(LogHas("free"));
    CHECK(LogHas("chrmove(21,300,dummy_EINGANG_ZELLE,-1)"));
    CHECK(LogHas("setfield(300,w4,v1,o101)"));
    CHECK(LogHas("op91(21,5)"));
    CHECK(LogHas("slot28(21,300)"));
    delete h;
}

TEST(NpcAction3_JailCell, Resolve_State2_noEscape_fines_and_frees) {
    Recorder rec; g_rec = &rec;
    NpcAction3Hooks H = MakeHooks(); SetNpcAction3Hooks(&H);
    rec.jailEscape = false;
    SetClock(12, 14, 0, 0);

    HeRecord* h = NewHe();
    He_State(h) = 2;
    He_FilterA(h) = 21;
    He_FilterB(h) = 300;
    rec.add(21, 5, true);
    rec.add(300, 0, true);

    NpcAction3_JailCellStep(h);

    CHECK(LogHas("free"));
    CHECK(LogHas("msg(21,5580)"));
    CHECK(!LogHas("chrmove(21,300,dummy_EINGANG_ZELLE,-1)"));
    delete h;
}

// ===========================================================================
// Recruitment
// ===========================================================================
TEST(NpcAction3_Recruit, Teardown_state_minus2_frees) {
    Recorder rec; g_rec = &rec;
    NpcAction3Hooks H = MakeHooks(); SetNpcAction3Hooks(&H);

    HeRecord* h = NewHe();
    He_State(h) = -2;
    He_Flags(h) = 0x02;
    He_FilterA(h) = 1; He_FilterB(h) = 2;
    rec.add(1, 5, false);   // !hasChar -> args25 release
    rec.add(2, 5, false);

    NpcAction3_RecruitmentState(h);

    CHECK(LogHas("free"));
    CHECK(LogHas("args25(2,456,0,4,262144)"));
    CHECK(LogHas("args25(1,456,0,4,262144)"));
    delete h;
}

TEST(NpcAction3_Recruit, Gate_flag04_skips) {
    Recorder rec; g_rec = &rec;
    NpcAction3Hooks H = MakeHooks(); SetNpcAction3Hooks(&H);

    HeRecord* h = NewHe();
    He_State(h) = 0;
    He_Flags(h) = 0x04;
    He_FilterA(h) = 1; He_FilterB(h) = 2;
    rec.add(1, 5, true); rec.add(2, 5, true);

    NpcAction3_RecruitmentState(h);
    CHECK(rec.log.empty());     // gated out entirely
    delete h;
}

TEST(NpcAction3_Recruit, State0_prox1_progressMet_arms_phase1) {
    Recorder rec; g_rec = &rec;
    NpcAction3Hooks H = MakeHooks(); SetNpcAction3Hooks(&H);
    rec.proximity = 1;
    rec.cost = 1;
    rec.packetStatusResult = 1;
    SetClock(20, 10, 0, 0);

    HeRecord* h = NewHe();
    He_State(h) = 0;
    He_ReqHandle(h) = -1;
    He_FilterA(h) = 1; He_FilterB(h) = 2;
    He_RecruitProgress(h) = 0;   // becomes 1 (prox==1), required=1 -> met
    rec.add(1, 5, true); rec.add(2, 5, true);

    NpcAction3_RecruitmentState(h);

    CHECK_EQ((int)He_RecruitRequired(h), 1);
    CHECK_EQ((int)He_RecruitProgress(h), 1);
    CHECK(LogHas("entity29(1)"));        // armed phase 1
    CHECK_EQ((int)He_ApptTime(h).minute, 2);
    delete h;
}

TEST(NpcAction3_Recruit, State0_prox1_progressNotMet_wanders) {
    Recorder rec; g_rec = &rec;
    NpcAction3Hooks H = MakeHooks(); SetNpcAction3Hooks(&H);
    rec.proximity = 1;
    rec.cost = 3;                // required 3, progress will be 1 -> not met
    rec.packetStatusResult = 1;
    SetClock(20, 10, 0, 0);

    HeRecord* h = NewHe();
    He_State(h) = 0;
    He_ReqHandle(h) = -1;
    He_FilterA(h) = 1; He_FilterB(h) = 2;
    rec.add(1, 5, true); rec.add(2, 5, true);

    NpcAction3_RecruitmentState(h);

    CHECK(LogHas("wander"));
    CHECK_EQ((int)He_RecruitMoved(h), 1);
    CHECK_EQ((int)He_ApptTime(h).hour, 8);   // forced to 8:00
    CHECK(LogHas("entity29(0)"));
    delete h;
}

TEST(NpcAction3_Recruit, State0_proxAbort_minus1024_retries) {
    Recorder rec; g_rec = &rec;
    NpcAction3Hooks H = MakeHooks(); SetNpcAction3Hooks(&H);
    rec.proximity = -1024;
    rec.packetStatusResult = 1;
    SetClock(20, 10, 0, 0);

    HeRecord* h = NewHe();
    He_State(h) = 0;
    He_ReqHandle(h) = -1;
    He_FilterA(h) = 1; He_FilterB(h) = 2;
    rec.add(1, 5, true); rec.add(2, 5, true);

    NpcAction3_RecruitmentState(h);

    CHECK(LogHas("entity29(-1)"));
    CHECK_EQ((int)He_ApptTime(h).minute, 2);
    delete h;
}

TEST(NpcAction3_Recruit, State1_emits_advertising_and_arms_phase5) {
    Recorder rec; g_rec = &rec;
    NpcAction3Hooks H = MakeHooks(); SetNpcAction3Hooks(&H);
    rec.packetStatusResult = 1;
    SetClock(20, 10, 0, 0);

    HeRecord* h = NewHe();
    He_State(h) = 1;
    He_ReqHandle(h) = -1;
    He_FilterA(h) = 1; He_FilterB(h) = 2;
    rec.add(1, 5, true); rec.add(2, 5, true);

    NpcAction3_RecruitmentState(h);

    CHECK(LogHas("bstart(werbung)"));
    CHECK(LogHas("bend"));
    CHECK(LogHas("entity29(5)"));
    CHECK_EQ((int)He_ApptTime(h).minute, 1);
    delete h;
}

TEST(NpcAction3_Recruit, State5_reciprocate_seals_and_clampsHour) {
    Recorder rec; g_rec = &rec;
    NpcAction3Hooks H = MakeHooks(); SetNpcAction3Hooks(&H);
    rec.reciprocate = 1;
    rec.proximity = 5;          // recruitProximity(B,A) returns >0 -> reciprocate
    rec.packetStatusResult = 1;
    SetClock(20, 2, 0, 0);      // hour 2 -> < 7 -> forced to 11

    HeRecord* h = NewHe();
    He_State(h) = 5;
    He_ReqHandle(h) = -1;
    He_FilterA(h) = 1; He_FilterB(h) = 2;
    rec.add(1, 5, true); rec.add(2, 5, true);

    NpcAction3_RecruitmentState(h);

    CHECK_EQ((int)He_RecruitPaired(h), 1);
    CHECK_EQ((int)He_ApptTime(h).hour, 11);   // clamped (2 + 2 = 4 < 7 -> 11)
    CHECK_EQ((int)He_ApptTime(h).day, 20);    // +2 hours, no day carry
    CHECK(LogHas("entity29(4)"));
    delete h;
}

TEST(NpcAction3_Recruit, State5_noReciprocate_retries) {
    Recorder rec; g_rec = &rec;
    NpcAction3Hooks H = MakeHooks(); SetNpcAction3Hooks(&H);
    rec.proximity = 0;          // recruitProximity(B,A) returns 0 -> no reciprocate
    rec.packetStatusResult = 1;
    SetClock(20, 10, 0, 0);

    HeRecord* h = NewHe();
    He_State(h) = 5;
    He_ReqHandle(h) = -1;
    He_FilterA(h) = 1; He_FilterB(h) = 2;
    rec.add(1, 5, true); rec.add(2, 5, true);

    NpcAction3_RecruitmentState(h);

    CHECK(LogHas("entity29(-1)"));
    CHECK_EQ((int)He_ApptTime(h).minute, 2);
    delete h;
}

// ===========================================================================
// Registration / dispatch table.
// ===========================================================================
TEST(NpcAction3_Register, bindings_present_and_distinct) {
    int n = RegisterNpcActions3();
    CHECK_EQ(n, 3);
    CHECK(NpcAction3_TableEntry(0x3A) == &NpcAction3_BurglaryStep);
    CHECK(NpcAction3_TableEntry(0x3B) == &NpcAction3_JailCellStep);
    CHECK(NpcAction3_TableEntry(0x2A) == &NpcAction3_RecruitmentState);
    CHECK(NpcAction3_TableEntry(0x44) == nullptr);
}

// Recovered-constant golden checks (byte-for-byte from get_bytes).
TEST(NpcAction3_Const, recovered_floats) {
    CHECK(kBurglaryStockMul == 0.01);
    CHECK(kBurglaryGuardMul2 == 0.35);
    CHECK(kBurglaryItemMul == 0.1);
    CHECK(kJailCrowdMul == 0.1f);
    CHECK(kJailEscapeBias == 0.3);
    CHECK(kJailFineMul == 0.5);
    CHECK(kRecruitMoodCeil == 1.1f);
}

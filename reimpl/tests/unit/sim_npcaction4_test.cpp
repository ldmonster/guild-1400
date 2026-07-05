// Unit tests for sim/npcaction4 — the last big NpcAction/CharAction state machines
// (Patrol, Sabotage, Raid, AttackTarget). Each test drives the He handler record
// through the expected state transitions with a recording mock for the leaf hooks,
// asserting golden state at each step, timer/counter deltas, and branch coverage of
// the state switch.
#include "sim/npcaction4.h"
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

struct FakeObj { i32 id; u8 kind; bool hasChar; u16 owner; bool actionActive; };

struct Recorder {
    std::vector<std::string> log;
    std::vector<FakeObj> objs;
    bool storableExists = true;
    i32 packetStatusResult = 1;
    i32 packetSeqResult = 777;
    bool combatDetect = false;
    bool sabotageSuccess = false;
    i32 gestureTarget = 0;
    i32 handlerRecId = -999;       // returned by handlerRecordId; default no match
    bool handlerExists = true;

    FakeObj storable{9001, 0, false, 0xFFFF, false};

    FakeObj* byId(i32 id) {
        for (auto& o : objs)
            if (o.id == id)
                return &o;
        return nullptr;
    }
    void add(i32 id, u8 kind, bool hasChar, u16 owner = 0xFFFF, bool active = false) {
        objs.push_back({id, kind, hasChar, owner, active});
    }
};

Recorder* g_rec = nullptr;
void log(const std::string& s) { if (g_rec) g_rec->log.push_back(s); }
bool LogHas(const std::string& s) {
    for (auto& e : g_rec->log) if (e == s) return true;
    return false;
}

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
void h_flag55(i32 id, u8 f) { log("flag55(" + std::to_string(id) + "," + std::to_string(f) + ")"); }
void h_pair33(i32 id, i32 v) { log("pair33(" + std::to_string(id) + "," + std::to_string(v) + ")"); }
void h_pair36(i32 s, i32 v) { log("pair36(" + std::to_string(s) + "," + std::to_string(v) + ")"); }
void h_quad43(i32 obj, i32, i32, i32 st) { log("quad43(" + std::to_string(obj) + "," + std::to_string(st) + ")"); }
void h_req16(i32 a, i32 b, i32 amt, u8) { log("req16(" + std::to_string(a) + "," + std::to_string(b) + "," + std::to_string(amt) + ")"); }
void h_cmd15(i32 p, i32 t, i32 amt, u8) { log("cmd15(" + std::to_string(p) + "," + std::to_string(t) + "," + std::to_string(amt) + ")"); }
i32  h_req39() { log("req39"); return 5050; }
i32  h_cityid(u16 idx) { return 1000 + idx; }

void* h_qbegin(i32 filterId) { return g_rec->byId(filterId); }
void* h_findById(i32 id) { return g_rec->byId(id); }
void* h_storable(void*) { return g_rec->storableExists ? &g_rec->storable : nullptr; }
void* h_family(i32) { return reinterpret_cast<void*>(0x1); }
void h_familyadd(void*, i32 add) { log("familyadd(" + std::to_string(add) + ")"); }
i32 h_objid(void* p) { return p ? static_cast<FakeObj*>(p)->id : -1; }
i32 h_personid(void* p) { return p ? static_cast<FakeObj*>(p)->id : -1; }
u16 h_marker(void* p) { return p ? static_cast<u16>(static_cast<FakeObj*>(p)->kind) : 0; }
bool h_haschar(void* p) { return p ? static_cast<FakeObj*>(p)->hasChar : false; }
u16 h_owner(void* p) { return p ? static_cast<FakeObj*>(p)->owner : 0xFFFF; }
u8 h_kind(void* p) { return p ? static_cast<FakeObj*>(p)->kind : 0; }
i32 h_actionactive(void* p) { return (p && static_cast<FakeObj*>(p)->actionActive) ? 1 : 0; }
void h_chgaction(void*, HeRecord*, u16 kw) { log("chgaction(kw" + std::to_string(kw) + ")"); }
i32 h_violation(int crime, i32 v, i32, i32) {
    log("violation(" + std::to_string(crime) + "," + std::to_string(v) + ")"); return 555;
}
void* h_findhandler(i32) { return g_rec->handlerExists ? reinterpret_cast<void*>(0x2) : nullptr; }
i32 h_handlerrec(void*) { return g_rec->handlerRecId; }
void h_msg(i32 id, int t) { log("msg(" + std::to_string(id) + "," + std::to_string(t) + ")"); }
i32 h_wage(void*) { log("wage"); return 17; }
i32 h_gesture(void*, HeRecord*) { return g_rec->gestureTarget; }
bool h_sabroll(void*, void*) { return g_rec->sabotageSuccess; }
void h_sabfx(HeRecord*) { log("fx"); }
bool h_combatroll(int, int, bool a) { log(std::string("combatroll(") + (a ? "atk" : "raid") + ")"); return g_rec->combatDetect; }
i32 h_strength(void*, void*) { return 42; }

NpcAction4Hooks MakeHooks() {
    NpcAction4Hooks H{};
    H.queueRequestEntity29 = h_queue29;
    H.packetStatus = h_status;
    H.packetSeq = h_seq;
    H.freeHandlerEntry = h_free;
    H.queueSingle49 = h_single49;
    H.queueNamedObject53 = h_named53;
    H.queueFlag55 = h_flag55;
    H.queuePair33 = h_pair33;
    H.queuePair36 = h_pair36;
    H.queueQuad43 = h_quad43;
    H.queueRequest16 = h_req16;
    H.enqueueCmd15 = h_cmd15;
    H.queueRequest39 = h_req39;
    H.cityIdFromIndex = h_cityid;
    H.personQueryBegin = h_qbegin;
    H.findPersonById = h_findById;
    H.findStorableObject = h_storable;
    H.getFamilyRecord = h_family;
    H.familyLedgerAdd = h_familyadd;
    H.objectIdField = h_objid;
    H.personIdField = h_personid;
    H.personMarkerWord = h_marker;
    H.personHasCharacter = h_haschar;
    H.personOwnerWord = h_owner;
    H.personKind = h_kind;
    H.personActionActive = h_actionactive;
    H.changePlayerAction = h_chgaction;
    H.evaluateViolation = h_violation;
    H.findHandlerByFilter = h_findhandler;
    H.handlerRecordId = h_handlerrec;
    H.sendMessage = h_msg;
    H.patrolWageRoll = h_wage;
    H.sabotageGestureTarget = h_gesture;
    H.sabotageDamageRoll = h_sabroll;
    H.sabotagePlayFx = h_sabfx;
    H.combatAttackRoll = h_combatroll;
    H.combatStrength = h_strength;
    return H;
}

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

} // namespace

// ===========================================================================
// Patrol
// ===========================================================================
TEST(NpcAction4_Patrol, State_minus1_frees) {
    Recorder rec; g_rec = &rec;
    NpcAction4Hooks H = MakeHooks(); SetNpcAction4Hooks(&H);
    HeRecord* h = NewHe();
    He_State(h) = -1;
    NpcAction4_PatrolStep(h);
    CHECK(LogHas("free"));
    delete h;
}

TEST(NpcAction4_Patrol, EntryScan_noLiveMembers_arms_minus1_and_advances1min) {
    Recorder rec; g_rec = &rec;
    NpcAction4Hooks H = MakeHooks(); SetNpcAction4Hooks(&H);
    SetClock(10, 9, 0, 0);
    HeRecord* h = NewHe();
    He_State(h) = 0;
    He_PatrolForce1024(h) = -1;
    He_PatrolMember(h, 0) = 99;   // no such obj -> not live
    He_PatrolMember(h, 1) = -1;
    He_PatrolMember(h, 2) = -1;
    He_PatrolMember(h, 3) = -1;
    NpcAction4_PatrolStep(h);
    CHECK(LogHas("entity29(-1)"));
    CHECK_EQ((int)He_ApptTime(h).minute, 1);
    delete h;
}

TEST(NpcAction4_Patrol, State0_escortToTarget_armsEntity29_2_plus2min) {
    Recorder rec; g_rec = &rec;
    NpcAction4Hooks H = MakeHooks(); SetNpcAction4Hooks(&H);
    SetClock(10, 9, 0, 0);
    HeRecord* h = NewHe();
    He_State(h) = 0;
    He_PatrolForce1024(h) = -1;
    He_FilterA4(h) = 100;          // patrol target
    He_PatrolMember(h, 0) = 11;
    He_PatrolMember(h, 1) = -1;
    He_PatrolMember(h, 2) = -1;
    He_PatrolMember(h, 3) = -1;
    rec.add(100, 0, true);
    rec.add(11, 5, true);
    NpcAction4_PatrolStep(h);
    CHECK(LogHas("single49(11)"));
    CHECK(LogHas("named53(11,100,-1,f1,Streife)"));
    CHECK(LogHas("entity29(2)"));
    CHECK_EQ((int)He_ApptTime(h).minute, 2);
    delete h;
}

TEST(NpcAction4_Patrol, Force1024_setsLapLoop_noActive_armsEntity29_3) {
    Recorder rec; g_rec = &rec;
    NpcAction4Hooks H = MakeHooks(); SetNpcAction4Hooks(&H);
    SetClock(10, 9, 0, 0);
    HeRecord* h = NewHe();
    He_State(h) = 0;
    He_PatrolForce1024(h) = 7;     // forces state 1024
    He_PatrolMember(h, 0) = 11;
    He_PatrolMember(h, 1) = -1;
    He_PatrolMember(h, 2) = -1;
    He_PatrolMember(h, 3) = -1;
    rec.add(11, 5, true, 0xFFFF, /*active=*/false);
    NpcAction4_PatrolStep(h);
    CHECK_EQ(He_State(h), 1024);         // lap loop stays in 1024
    CHECK(LogHas("entity29(3)"));        // all idle -> arm phase 3
    CHECK_EQ((int)He_ApptTime(h).minute, 2);
    // Disasm 0x4ce4ea: mov dword ptr [eax+82h],-1 with eax=rec+0x52 → the -1
    // store lands at rec+212 (the +212 force-1024 marker), NOT ApptTime.day.
    // Old pin (day == -1) was based on a misread of the decompile; the appt day
    // stays the stamped clock day and +212 is cleared to -1.
    CHECK_EQ((int)He_ApptTime(h).day, 10);
    CHECK_EQ((int)He_PatrolForce1024(h), -1);
    delete h;
}

TEST(NpcAction4_Patrol, State1_payWage_armsEntity29_minus1) {
    Recorder rec; g_rec = &rec;
    NpcAction4Hooks H = MakeHooks(); SetNpcAction4Hooks(&H);
    SetClock(10, 18, 0, 0);
    HeRecord* h = NewHe();
    He_State(h) = 1;
    He_PatrolForce1024(h) = -1;
    He_CityIndex(h) = 3;
    He_FilterB4(h) = 200;          // home building
    He_PatrolMember(h, 0) = 11;
    He_PatrolMember(h, 1) = -1;
    He_PatrolMember(h, 2) = -1;
    He_PatrolMember(h, 3) = -1;
    rec.add(200, 0, true);
    rec.add(11, 5, true);
    NpcAction4_PatrolStep(h);
    CHECK(LogHas("wage"));
    CHECK(LogHas("cmd15(1003,-1,17)"));     // playerId = cityIdFromIndex(3) = 1003
    CHECK(LogHas("entity29(-1)"));
    delete h;
}

// ===========================================================================
// Sabotage
// ===========================================================================
TEST(NpcAction4_Sabotage, Teardown_minus1_pair33_and_free) {
    Recorder rec; g_rec = &rec;
    NpcAction4Hooks H = MakeHooks(); SetNpcAction4Hooks(&H);
    HeRecord* h = NewHe();
    He_State(h) = -1;
    He_SabPacket(h) = -1;
    He_SabTarget(h) = 33;
    NpcAction4_RunSabotage(h);
    CHECK(LogHas("pair33(33,1)"));
    CHECK(LogHas("free"));
    delete h;
}

TEST(NpcAction4_Sabotage, PacketGate_pending_returns_noChange) {
    Recorder rec; g_rec = &rec;
    NpcAction4Hooks H = MakeHooks(); SetNpcAction4Hooks(&H);
    rec.packetStatusResult = 0;   // pending
    HeRecord* h = NewHe();
    He_State(h) = 0;
    He_SabPacket(h) = 555;
    NpcAction4_RunSabotage(h);
    CHECK(rec.log.empty());
    delete h;
}

TEST(NpcAction4_Sabotage, State0_seqResolves_appliesDamage_and_advances) {
    Recorder rec; g_rec = &rec;
    NpcAction4Hooks H = MakeHooks(); SetNpcAction4Hooks(&H);
    rec.packetSeqResult = 808;
    HeRecord* h = NewHe();
    He_State(h) = 0;
    He_SabPacket(h) = 555;
    He_CityIndex(h) = 2;
    He_SabDamage(h) = 500;
    NpcAction4_RunSabotage(h);
    CHECK(LogHas("familyadd(500)"));
    CHECK(LogHas("req16(-1,1002,500)"));
    CHECK_EQ(He_State(h), 1);
    CHECK_EQ(He_SabPacket(h), -1);
    delete h;
}

TEST(NpcAction4_Sabotage, State0_noSeq_teardown) {
    Recorder rec; g_rec = &rec;
    NpcAction4Hooks H = MakeHooks(); SetNpcAction4Hooks(&H);
    rec.packetSeqResult = 0;
    HeRecord* h = NewHe();
    He_State(h) = 0;
    He_SabPacket(h) = 555;
    NpcAction4_RunSabotage(h);
    CHECK_EQ(He_State(h), -1);
    CHECK_EQ(He_SabTarget(h), -1);
    delete h;
}

TEST(NpcAction4_Sabotage, State3_gestureFound_registersViolation_to_state7) {
    Recorder rec; g_rec = &rec;
    NpcAction4Hooks H = MakeHooks(); SetNpcAction4Hooks(&H);
    rec.gestureTarget = 909;
    HeRecord* h = NewHe();
    He_State(h) = 3;
    He_SabPacket(h) = -1;
    He_FilterA4(h) = 100;
    He_CityIndex(h) = 1;
    rec.add(100, 0, true, /*owner=*/5);  // owner != 0xFFFF
    NpcAction4_RunSabotage(h);
    CHECK_EQ(He_State(h), 7);
    CHECK_EQ(He_SabViolationSeq(h), 909);
    CHECK(LogHas("violation(23,100)"));
    delete h;
}

TEST(NpcAction4_Sabotage, State3_noGesture_flag55_to_state4) {
    Recorder rec; g_rec = &rec;
    NpcAction4Hooks H = MakeHooks(); SetNpcAction4Hooks(&H);
    rec.gestureTarget = 0;
    HeRecord* h = NewHe();
    He_State(h) = 3;
    He_SabPacket(h) = -1;
    He_FilterA4(h) = 100;
    He_SabTarget(h) = 33;
    He_ObjId16(h) = 16;
    rec.add(100, 0, true, 5);
    NpcAction4_RunSabotage(h);
    CHECK_EQ(He_State(h), 4);
    CHECK(LogHas("flag55(33,1)"));
    CHECK(LogHas("named53(33,16,-1,f1,Sabotage)"));
    delete h;
}

TEST(NpcAction4_Sabotage, State5_runsFx_then_state6_onRetry) {
    Recorder rec; g_rec = &rec;
    NpcAction4Hooks H = MakeHooks(); SetNpcAction4Hooks(&H);
    *crt::RandStatePtr() = 1;
    SetClock(10, 9, 0, 0);
    HeRecord* h = NewHe();
    He_State(h) = 5;
    He_SabPacket(h) = -1;
    He_SabRetry(h) = 0;
    NpcAction4_RunSabotage(h);
    CHECK(LogHas("fx"));
    CHECK_EQ((int)He_SabRetry(h), 1);
    // second invocation: retry set -> state 6
    rec.log.clear();
    NpcAction4_RunSabotage(h);
    CHECK_EQ(He_State(h), 6);
    delete h;
}

// ===========================================================================
// Raid
// ===========================================================================
TEST(NpcAction4_Raid, Entry_minus2_noFlag_frees) {
    Recorder rec; g_rec = &rec;
    NpcAction4Hooks H = MakeHooks(); SetNpcAction4Hooks(&H);
    HeRecord* h = NewHe();
    He_State(h) = -2;
    He_Flags(h) = 0;
    NpcAction4_RaidStep(h);
    CHECK(LogHas("free"));
    delete h;
}

TEST(NpcAction4_Raid, Entry_minus2_withFlag_arms_state1_escortToStaging) {
    Recorder rec; g_rec = &rec;
    NpcAction4Hooks H = MakeHooks(); SetNpcAction4Hooks(&H);
    SetClock(10, 9, 0, 0);
    HeRecord* h = NewHe();
    He_State(h) = -2;
    He_Flags(h) = 2;
    He_ReqHandle(h) = -1;
    He_FilterB4(h) = 200;       // home/staging (case 2 / state 1)
    He_Member8(h, 0) = 11;
    for (int i = 1; i < 8; ++i) He_Member8(h, i) = -1;
    rec.add(200, 0, true, 5);
    rec.add(11, 5, true);
    NpcAction4_RaidStep(h);
    // -2 arms state 1 (+ firstPass); same tick runs case 2 (escort to staging),
    // emits the escort commands and, since firstPass, frees the handler.
    CHECK(LogHas("single49(11)"));
    CHECK(LogHas("free"));
    delete h;
}

TEST(NpcAction4_Raid, State0_escort_to_target_armsEntity29_2) {
    Recorder rec; g_rec = &rec;
    NpcAction4Hooks H = MakeHooks(); SetNpcAction4Hooks(&H);
    SetClock(10, 9, 0, 0);
    HeRecord* h = NewHe();
    He_State(h) = 0;            // case 1
    He_ReqHandle(h) = -1;
    He_FilterA4(h) = 100;
    He_Member8(h, 0) = 11;
    for (int i = 1; i < 8; ++i) He_Member8(h, i) = -1;
    rec.add(100, 0, true, 5);   // owner != 0xFFFF
    rec.add(11, 5, true);
    NpcAction4_RaidStep(h);
    CHECK(LogHas("entity29(2)"));
    CHECK(LogHas("single49(11)"));
    CHECK_EQ((int)He_ApptTime(h).minute, 2);
    delete h;
}

TEST(NpcAction4_Raid, State0_noTarget_arms_minus1_plus1min) {
    Recorder rec; g_rec = &rec;
    NpcAction4Hooks H = MakeHooks(); SetNpcAction4Hooks(&H);
    SetClock(10, 9, 0, 0);
    HeRecord* h = NewHe();
    He_State(h) = 0;
    He_ReqHandle(h) = -1;
    He_FilterA4(h) = 999;   // no target
    NpcAction4_RaidStep(h);
    CHECK(LogHas("entity29(-1)"));
    CHECK_EQ((int)He_ApptTime(h).minute, 1);
    delete h;
}

TEST(NpcAction4_Raid, State2_wait_allArrived_armsEntity29_3) {
    Recorder rec; g_rec = &rec;
    NpcAction4Hooks H = MakeHooks(); SetNpcAction4Hooks(&H);
    SetClock(10, 9, 0, 0);
    HeRecord* h = NewHe();
    He_State(h) = 2;            // case 3 arrived-wait
    He_ReqHandle(h) = -1;
    He_Member8(h, 0) = 11;
    for (int i = 1; i < 8; ++i) He_Member8(h, i) = -1;
    rec.add(11, 5, true, 0xFFFF, /*active=*/false);   // arrived (not active)
    NpcAction4_RaidStep(h);
    CHECK(LogHas("entity29(3)"));
    delete h;
}

TEST(NpcAction4_Raid, State3_combatResolve_detected_to_stateMinus1) {
    Recorder rec; g_rec = &rec;
    NpcAction4Hooks H = MakeHooks(); SetNpcAction4Hooks(&H);
    rec.combatDetect = true;
    SetClock(10, 9, 0, 0);
    HeRecord* h = NewHe();
    He_State(h) = 3;
    He_Flags(h) = 2;   // 0x4cea84 case 4 gates the resolve on flag 0x02 (binary-proved)
    He_ReqHandle(h) = -1;
    He_FilterA4(h) = 100;
    He_CityIndex(h) = 4;
    rec.add(100, 0, true, 5);
    NpcAction4_RaidStep(h);
    CHECK(LogHas("combatroll(raid)"));
    // 0x4cf456 / 0x4cf46f: Raid "caught" branch sets state -1 (Attack sets 5).
    CHECK_EQ(He_State(h), -1);
    CHECK(LogHas("msg(1004,3502)"));
    delete h;
}

TEST(NpcAction4_Raid, State3_combatResolve_escorts_queues_cmd39) {
    Recorder rec; g_rec = &rec;
    NpcAction4Hooks H = MakeHooks(); SetNpcAction4Hooks(&H);
    rec.combatDetect = false;
    SetClock(10, 9, 0, 0);
    HeRecord* h = NewHe();
    He_State(h) = 3;
    He_Flags(h) = 2;   // 0x4cea84 case 4 gates the resolve on flag 0x02 (binary-proved)
    He_ReqHandle(h) = -1;
    He_FilterA4(h) = 100;
    He_Member8(h, 0) = 11;
    for (int i = 1; i < 8; ++i) He_Member8(h, i) = -1;
    rec.add(100, 0, true, 5);
    NpcAction4_RaidStep(h);
    CHECK(LogHas("req39"));
    CHECK_EQ(He_CombatPacket(h), 5050);
    CHECK_EQ(He_State(h), 4);
    delete h;
}

// ===========================================================================
// AttackTarget
// ===========================================================================
TEST(NpcAction4_Attack, State_minus2_teardown_frees) {
    Recorder rec; g_rec = &rec;
    NpcAction4Hooks H = MakeHooks(); SetNpcAction4Hooks(&H);
    HeRecord* h = NewHe();
    He_State(h) = -2;             // switch index 0
    He_FilterB4(h) = 200;
    He_Member8(h, 0) = 11;
    for (int i = 1; i < 8; ++i) He_Member8(h, i) = -1;
    rec.add(200, 0, true);
    rec.add(11, 5, true);
    NpcAction4_AttackTargetStep(h);
    CHECK(LogHas("free"));
    CHECK(LogHas("single49(11)"));
    CHECK(LogHas("named53(11,200,9001,f0,Angriff)"));
    delete h;
}

TEST(NpcAction4_Attack, State0_escort_to_target_plus10min_state1) {
    Recorder rec; g_rec = &rec;
    NpcAction4Hooks H = MakeHooks(); SetNpcAction4Hooks(&H);
    SetClock(10, 9, 0, 0);
    HeRecord* h = NewHe();
    He_State(h) = 0;              // switch index 2
    He_FilterA4(h) = 100;
    He_FilterB4(h) = 200;
    He_Member8(h, 0) = 11;
    for (int i = 1; i < 8; ++i) He_Member8(h, i) = -1;
    rec.add(100, 0, true);
    rec.add(200, 0, true);
    rec.add(11, 5, true);
    NpcAction4_AttackTargetStep(h);
    CHECK_EQ(He_State(h), 1);
    CHECK_EQ((int)He_ApptTime(h).minute, 10);
    CHECK(LogHas("named53(11,100,-1,f0,Angriff)"));
    delete h;
}

TEST(NpcAction4_Attack, State1_wait_allArrived_to_state2_plus1sec) {
    Recorder rec; g_rec = &rec;
    NpcAction4Hooks H = MakeHooks(); SetNpcAction4Hooks(&H);
    SetClock(10, 9, 0, 0);
    HeRecord* h = NewHe();
    He_State(h) = 1;
    He_Member8(h, 0) = 11;
    for (int i = 1; i < 8; ++i) He_Member8(h, i) = -1;
    rec.add(11, 5, true, 0xFFFF, false);
    NpcAction4_AttackTargetStep(h);
    CHECK_EQ(He_State(h), 2);
    CHECK_EQ((int)He_ApptTime(h).second, 1);
    delete h;
}

TEST(NpcAction4_Attack, State2_combatResolve_detected_to_state5) {
    Recorder rec; g_rec = &rec;
    NpcAction4Hooks H = MakeHooks(); SetNpcAction4Hooks(&H);
    rec.combatDetect = true;
    SetClock(10, 9, 0, 0);
    HeRecord* h = NewHe();
    He_State(h) = 2;
    He_FilterA4(h) = 100;
    He_CityIndex(h) = 2;
    rec.add(100, 0, true, 5);
    NpcAction4_AttackTargetStep(h);
    CHECK(LogHas("combatroll(atk)"));
    CHECK_EQ(He_State(h), 5);
    delete h;
}

TEST(NpcAction4_Attack, State4_cutsceneActive_plus5min) {
    Recorder rec; g_rec = &rec;
    NpcAction4Hooks H = MakeHooks(); SetNpcAction4Hooks(&H);
    rec.handlerExists = true;
    SetClock(10, 9, 0, 0);
    HeRecord* h = NewHe();
    He_State(h) = 4;             // switch index 6
    He_CombatSeq(h) = 5050;
    NpcAction4_AttackTargetStep(h);
    CHECK_EQ((int)He_ApptTime(h).minute, 5);
    CHECK_EQ(He_State(h), 4);    // stays while active
    delete h;
}

TEST(NpcAction4_Attack, State4_cutsceneDone_plus1sec_to_state5) {
    Recorder rec; g_rec = &rec;
    NpcAction4Hooks H = MakeHooks(); SetNpcAction4Hooks(&H);
    rec.handlerExists = false;
    SetClock(10, 9, 0, 0);
    HeRecord* h = NewHe();
    He_State(h) = 4;
    He_CombatSeq(h) = 5050;
    NpcAction4_AttackTargetStep(h);
    CHECK_EQ((int)He_ApptTime(h).second, 1);
    CHECK_EQ(He_State(h), 5);
    delete h;
}

// ===========================================================================
// Registration / constants
// ===========================================================================
TEST(NpcAction4_Register, bindings_present_and_distinct) {
    int n = RegisterNpcActions4();
    CHECK_EQ(n, 4);
    CHECK(NpcAction4_TableEntry(0x3C) == &NpcAction4_PatrolStep);
    CHECK(NpcAction4_TableEntry(0x3D) == &NpcAction4_RunSabotage);
    CHECK(NpcAction4_TableEntry(0x3E) == &NpcAction4_RaidStep);
    CHECK(NpcAction4_TableEntry(0x3F) == &NpcAction4_AttackTargetStep);
    CHECK(NpcAction4_TableEntry(0x10) == nullptr);
}

TEST(NpcAction4_Const, recovered_floats) {
    CHECK(kRaidWorkstationMul == 0.01);
    CHECK(kRaidSecurityBias == 100.0);
    CHECK(kRaidDefenderWeight == 100.0f);
    CHECK(kRaidAttackerWeight == 125.0f);
    CHECK(kAttackWorkstationMul == 0.01);
    CHECK(kAttackAttackerWeight == 125.0f);
    // flt_61EA24 = 1/42
    CHECK(kPatrolWageCoordMul > 0.0238f && kPatrolWageCoordMul < 0.02382f);
}

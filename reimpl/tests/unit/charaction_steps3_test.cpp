// Unit tests for charaction_steps3 — batch 3 of the self-contained CharAction
// step leaves. The GameTimeAdvance golden vectors were computed with python3
// mirroring src/sim/gametime.cpp (GameTimeAdvance). The cross-cluster leaves
// (entity/person resolve, cmd25/cmd29/cmd61/buildop73, notify message) are routed
// through recording mocks installed via SetCharActionStep3Hooks / SetNpcLeafHooks.
// Suite prefix: CharActionY.
#include "test.h"

#include "sim/charaction_steps3.h"
#include "sim/he.h"
#include "sim/npcaction.h"   // NpcClock / SetNpcClock, SetNpcLeafHooks

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// A fresh, zeroed He record large enough for the +200 field accesses.
struct RecBuf {
    HeRecord rec{};
    RecBuf() { std::memset(&rec, 0, sizeof(rec)); }
    HeRecord* operator->() { return &rec; }
    HeRecord* get() { return &rec; }
};

GameTime TestClock() {
    GameTime t{};
    t.day = 10; t.hour = 8; t.minute = 30; t.second = 15;
    return t;
}
void SetClock() { SetNpcClock(TestClock()); }

bool TimeEq(const GameTime& t, i32 day, u16 hour, i32 minute, i32 second) {
    return t.day == day && t.hour == hour && t.minute == minute && t.second == second;
}

// --- recording NpcLeafHooks ------------------------------------------------
struct LeafRec {
    int freeCalls = 0;
    int cmd29Calls = 0;
    std::vector<int> cmd29Args;
    i32 nextHandle = 4242;
};
LeafRec g_leaf;

i32 RecFree(HeRecord*) { ++g_leaf.freeCalls; return 0; }
i32 RecCmd29(int arg, HeRecord*) {
    ++g_leaf.cmd29Calls; g_leaf.cmd29Args.push_back(arg); return g_leaf.nextHandle;
}
i32 RecPacketStatus(i32) { return 0; }

NpcLeafHooks MakeLeafHooks() {
    NpcLeafHooks h{};
    h.freeHandlerEntry = RecFree;
    h.queueRequestEntity29 = RecCmd29;
    h.packetStatus = RecPacketStatus;
    return h;
}

// --- recording CharActionStep3Hooks ----------------------------------------
struct Step3Rec {
    // resolveEntityById: id -> record (0 == absent)
    std::vector<std::pair<i32, HeRecord*>> entities;
    // personQueryBegin: queued results per call (in order)
    std::vector<HeRecord*> queryResults;
    size_t queryCursor = 0;
    // findPersonById: id -> record
    std::vector<std::pair<i32, HeRecord*>> persons;
    // find-by-filter scan list
    std::vector<HeRecord*> scanList;
    size_t scanCursor = 0;

    int guard61Calls = 0; HeRecord* guard61Entity = nullptr; i32 guard61Handle = 7777;
    int args25Calls = 0;
    struct Args25 { i32 id; int sel; int mask; int mode; };
    std::vector<Args25> args25;
    int buildop73Calls = 0; i32 buildop73Person = 0; i32 buildop73Handle = 9999;
    bool fastTime = false;
    int actionMinutes = 45;
    int notifyCalls = 0;
    struct Notify { i32 to; int text; u16 name; };
    std::vector<Notify> notify;
};
Step3Rec g_s3;

HeRecord* RecResolve(i32 id) {
    for (auto& e : g_s3.entities) if (e.first == id) return e.second;
    return nullptr;
}
HeRecord* RecQuery(int, int, int) {
    if (g_s3.queryCursor >= g_s3.queryResults.size()) return nullptr;
    return g_s3.queryResults[g_s3.queryCursor++];
}
HeRecord* RecFindPerson(i32 id) {
    for (auto& p : g_s3.persons) if (p.first == id) return p.second;
    return nullptr;
}
HeRecord* RecFindFirst(int, const int*, const int*) {
    g_s3.scanCursor = 0;
    if (g_s3.scanList.empty()) return nullptr;
    return g_s3.scanList[g_s3.scanCursor++];
}
HeRecord* RecFindNext() {
    if (g_s3.scanCursor >= g_s3.scanList.size()) return nullptr;
    return g_s3.scanList[g_s3.scanCursor++];
}
i32 RecGuard61(HeRecord* e) { ++g_s3.guard61Calls; g_s3.guard61Entity = e; return g_s3.guard61Handle; }
void RecArgs25(i32 id, int sel, int mask, int mode) {
    ++g_s3.args25Calls; g_s3.args25.push_back({id, sel, mask, mode});
}
i32 RecBuildOp73(i32 person) { ++g_s3.buildop73Calls; g_s3.buildop73Person = person; return g_s3.buildop73Handle; }
bool RecFastTime() { return g_s3.fastTime; }
int RecActionMinutes(i32) { return g_s3.actionMinutes; }
void RecNotify(i32 to, int text, u16 name) { ++g_s3.notifyCalls; g_s3.notify.push_back({to, text, name}); }

CharActionStep3Hooks MakeStep3Hooks() {
    CharActionStep3Hooks h{};
    h.resolveEntityById = RecResolve;
    h.personQueryBegin = RecQuery;
    h.findPersonById = RecFindPerson;
    h.findFirstByFilter = RecFindFirst;
    h.findNextMatching = RecFindNext;
    h.queueRequestGuardTarget61 = RecGuard61;
    h.queueRequestArgs25 = RecArgs25;
    h.requestBuildOp73Sabotage = RecBuildOp73;
    h.fastTimeEnabled = RecFastTime;
    h.targetActionMinutes = RecActionMinutes;
    h.sendNotifyMessage = RecNotify;
    return h;
}

void ResetHooks() {
    g_leaf = LeafRec{};
    g_s3 = Step3Rec{};
    static NpcLeafHooks lh; lh = MakeLeafHooks(); SetNpcLeafHooks(&lh);
    static CharActionStep3Hooks sh; sh = MakeStep3Hooks(); SetCharActionStep3Hooks(&sh);
    SetClock();
}

} // namespace

// ===========================================================================
// PatrolFindTarget
// ===========================================================================
TEST(CharActionY, PatrolFindTarget_FlagClearReturnsZero) {
    ResetHooks();
    RecBuf r;
    He_Flags(r.get()) = 0;   // bit 2 clear
    CHECK_EQ(PatrolFindTarget(r.get()), 0);
    CHECK_EQ(g_leaf.cmd29Calls, 0);
}

TEST(CharActionY, PatrolFindTarget_QueryMissArmsTwice) {
    ResetHooks();
    RecBuf r;
    He_Flags(r.get()) = 2;
    Cas3_Counter(r.get()) = 55;
    // no query result queued -> miss -> two cmd29 (-1 then 0)
    i32 ret = PatrolFindTarget(r.get());
    CHECK_EQ(ret, 4242);
    CHECK_EQ(g_leaf.cmd29Calls, 2);
    CHECK_EQ(g_leaf.cmd29Args[0], -1);
    CHECK_EQ(g_leaf.cmd29Args[1], 0);
    CHECK_EQ(Cas3_Packet(r.get()), 4242);
    CHECK(TimeEq(He_ApptTime(r.get()), 10, 8, 30, 16));   // +1 second
}

TEST(CharActionY, PatrolFindTarget_QueryHitArmsOnce) {
    ResetHooks();
    RecBuf r, found;
    He_Flags(r.get()) = 2;
    g_s3.queryResults.push_back(found.get());   // query hit
    PatrolFindTarget(r.get());
    CHECK_EQ(g_leaf.cmd29Calls, 1);
    CHECK_EQ(g_leaf.cmd29Args[0], 0);
}

// ===========================================================================
// GuardRequestTarget
// ===========================================================================
TEST(CharActionY, GuardRequestTarget_Found) {
    ResetHooks();
    RecBuf r, ent;
    Cas3_TargetId(r.get()) = 321;
    g_s3.entities.push_back({321, ent.get()});
    int hourResult = GuardRequestTarget(r.get());
    CHECK_EQ(g_leaf.freeCalls, 0);
    CHECK_EQ(g_s3.guard61Calls, 1);
    CHECK_EQ(g_s3.guard61Entity, ent.get());
    CHECK_EQ(Cas3_GuardPacket(r.get()), 7777);
    CHECK_EQ(hourResult, 8);
    CHECK(TimeEq(He_ApptTime(r.get()), 10, 8, 36, 15));   // +6 minutes
}

TEST(CharActionY, GuardRequestTarget_NotFoundFrees) {
    ResetHooks();
    RecBuf r;
    Cas3_TargetId(r.get()) = 999;   // not registered
    GuardRequestTarget(r.get());
    CHECK_EQ(g_leaf.freeCalls, 1);   // freed (then falls through, per the original)
}

// ===========================================================================
// IsAnimalTargetBusy
// ===========================================================================
TEST(CharActionY, IsAnimalTargetBusy_NullReturnsOne) {
    ResetHooks();
    CHECK_EQ(IsAnimalTargetBusy(nullptr), 1);
}

TEST(CharActionY, IsAnimalTargetBusy_NoMatchReturnsOne) {
    ResetHooks();
    RecBuf r;
    // scan empty -> 1
    CHECK_EQ(IsAnimalTargetBusy(r.get()), 1);
}

TEST(CharActionY, IsAnimalTargetBusy_BusyReturnsZero) {
    ResetHooks();
    RecBuf r, m;
    // key = *(int*)(r+1). Make a handler match.
    i32 key = *reinterpret_cast<i32*>(HeBytes(r.get()) + 1);
    *reinterpret_cast<i32*>(HeBytes(m.get()) + 176) = key;
    *reinterpret_cast<i32*>(HeBytes(m.get()) + 184) = 1;
    g_s3.scanList.push_back(m.get());
    CHECK_EQ(IsAnimalTargetBusy(r.get()), 0);
}

TEST(CharActionY, IsAnimalTargetBusy_NonMatchingHandlerSkipped) {
    ResetHooks();
    RecBuf r, m;
    // handler present but field +184 != 1 -> not busy -> scan ends -> 1
    i32 key = *reinterpret_cast<i32*>(HeBytes(r.get()) + 1);
    *reinterpret_cast<i32*>(HeBytes(m.get()) + 176) = key;
    *reinterpret_cast<i32*>(HeBytes(m.get()) + 184) = 0;
    g_s3.scanList.push_back(m.get());
    CHECK_EQ(IsAnimalTargetBusy(r.get()), 1);
}

// ===========================================================================
// FindInteractionPartner
// ===========================================================================
TEST(CharActionY, FindInteractionPartner_OnlySelfClearsState) {
    ResetHooks();
    RecBuf r;
    He_State(r.get()) = 9;
    *reinterpret_cast<u8*>(HeBytes(r.get()) + 186) = 1;
    g_s3.scanList.push_back(r.get());   // only self matches
    HeRecord* ret = FindInteractionPartner(r.get());
    CHECK(ret == nullptr);
    CHECK_EQ(He_State(r.get()), 0);
    CHECK_EQ(*reinterpret_cast<u8*>(HeBytes(r.get()) + 186), 0);
    CHECK_EQ(g_leaf.cmd29Calls, 0);
}

TEST(CharActionY, FindInteractionPartner_OtherArmsCmd29) {
    ResetHooks();
    RecBuf r, other;
    g_s3.scanList.push_back(r.get());      // self (skipped)
    g_s3.scanList.push_back(other.get());  // a different partner
    HeRecord* ret = FindInteractionPartner(r.get());
    CHECK(ret != nullptr);
    CHECK_EQ(g_leaf.cmd29Calls, 1);
    CHECK_EQ(g_leaf.cmd29Args[0], -1);
    CHECK_EQ(Cas3_Packet(r.get()), 4242);
}

// ===========================================================================
// FindBeggarTarget
// ===========================================================================
TEST(CharActionY, FindBeggarTarget_FlagSetReturnsZero) {
    ResetHooks();
    RecBuf r;
    He_Flags(r.get()) = 4;
    CHECK_EQ(FindBeggarTarget(r.get()), 0);
    CHECK_EQ(g_leaf.cmd29Calls, 0);
}

TEST(CharActionY, FindBeggarTarget_NoneRestoresPose) {
    ResetHooks();
    RecBuf r;
    He_Flags(r.get()) = 0;
    He_SavedTime(r.get()) = TestClock();   // saved pose to restore
    // scan empty -> "no other" branch
    i32 ret = FindBeggarTarget(r.get());
    CHECK_EQ(ret, 5);
    CHECK_EQ(He_State(r.get()), 5);
    CHECK_EQ(Cas3_Packet(r.get()), -1);
    CHECK_EQ(g_leaf.cmd29Calls, 1);
    CHECK_EQ(g_leaf.cmd29Args[0], 5);
    CHECK(TimeEq(He_ApptTime(r.get()), 10, 8, 30, 15));   // saved pose copied
}

TEST(CharActionY, FindBeggarTarget_OtherArmsCmd29) {
    ResetHooks();
    RecBuf r, other;
    He_Flags(r.get()) = 0;
    g_s3.scanList.push_back(r.get());      // self (skipped)
    g_s3.scanList.push_back(other.get());  // another beggar handler
    i32 ret = FindBeggarTarget(r.get());
    CHECK_EQ(ret, 4242);
    CHECK_EQ(g_leaf.cmd29Calls, 1);
    CHECK_EQ(g_leaf.cmd29Args[0], -1);
}

// ===========================================================================
// GroupGatherInit
// ===========================================================================
TEST(CharActionY, GroupGatherInit_NoMembersFrees) {
    ResetHooks();
    RecBuf r;
    for (int i = 0; i < 24; i += 4)
        *reinterpret_cast<i32*>(HeBytes(r.get()) + 140 + i) = -1;
    GroupGatherInit(r.get());
    CHECK_EQ(g_leaf.freeCalls, 1);
    CHECK_EQ(*reinterpret_cast<u8*>(HeBytes(r.get()) + 216), 0);
}

TEST(CharActionY, GroupGatherInit_CountsAndArms) {
    ResetHooks();
    RecBuf r;
    for (int i = 0; i < 24; i += 4)
        *reinterpret_cast<i32*>(HeBytes(r.get()) + 140 + i) = -1;
    *reinterpret_cast<i32*>(HeBytes(r.get()) + 140) = 100;   // slot 0 used
    *reinterpret_cast<i32*>(HeBytes(r.get()) + 152) = 200;   // slot 3 used
    int hr = GroupGatherInit(r.get());
    CHECK_EQ(g_leaf.freeCalls, 0);
    CHECK_EQ(*reinterpret_cast<u8*>(HeBytes(r.get()) + 216), 2);
    CHECK_EQ(*reinterpret_cast<u8*>(HeBytes(r.get()) + 210), 0);
    // +172..+192 reset to -1
    for (int off = 172; off <= 192; off += 4)
        CHECK_EQ(*reinterpret_cast<i32*>(HeBytes(r.get()) + off), -1);
    CHECK(TimeEq(He_ApptTime(r.get()), 10, 8, 32, 15));   // +2 minutes
    CHECK(TimeEq(*reinterpret_cast<GameTime*>(HeBytes(r.get()) + 196), 10, 8, 30, 15)); // clock
    CHECK_EQ(hr, 8);
}

// ===========================================================================
// InitTargetState
// ===========================================================================
TEST(CharActionY, InitTargetState_FastTime) {
    ResetHooks();
    RecBuf r;
    g_s3.fastTime = true;
    int hr = InitTargetState(r.get());
    CHECK(TimeEq(He_ApptTime(r.get()), 10, 8, 30, 16));   // +1 second
    CHECK_EQ(hr, 8);
}

TEST(CharActionY, InitTargetState_Duration) {
    ResetHooks();
    RecBuf r;
    g_s3.fastTime = false;
    g_s3.actionMinutes = 45;
    int hr = InitTargetState(r.get());
    CHECK(TimeEq(He_ApptTime(r.get()), 10, 9, 15, 15));   // +45 minutes
    CHECK_EQ(hr, 9);
}

TEST(CharActionY, InitTargetState_TypeIdShift) {
    ResetHooks();
    RecBuf r;
    g_s3.fastTime = false;
    // typeId = (dword@+170) >> 16 ; set high word = 7
    *reinterpret_cast<i32*>(HeBytes(r.get()) + 170) = (7 << 16);
    InitTargetState(r.get());
    // (mock ignores typeId, but the shift must not crash / mis-read)
    CHECK_EQ(g_s3.actionMinutes, 45);
}

// ===========================================================================
// InitLagerErweitern
// ===========================================================================
TEST(CharActionY, InitLagerErweitern_NoObjectClamps) {
    ResetHooks();
    RecBuf r;
    He_SavedTime(r.get()) = TestClock();
    Cas3_Mult(r.get()) = 3;
    Cas3_TargetId(r.get()) = 7;     // sub-record advance = 3*7 = 21 min
    Cas3_Counter(r.get()) = 50;     // object id (not registered -> obj == null)
    int result = InitLagerErweitern(r.get());
    CHECK_EQ(g_leaf.freeCalls, 0);
    CHECK_EQ(g_s3.args25Calls, 0);
    CHECK_EQ(result, 7);            // 0 (no obj cap) + 7
    CHECK_EQ(Cas3_Extent(r.get()), 7);
    CHECK(TimeEq(Cas3_SubTime192(r.get()), 10, 8, 51, 15));   // +21 minutes
    CHECK(TimeEq(He_ApptTime(r.get()), 10, 8, 30, 16));       // +1 second
}

TEST(CharActionY, InitLagerErweitern_ObjectBlockedFrees) {
    ResetHooks();
    RecBuf r, obj;
    He_SavedTime(r.get()) = TestClock();
    Cas3_Mult(r.get()) = 1;
    Cas3_TargetId(r.get()) = 2;
    Cas3_Counter(r.get()) = 60;
    *reinterpret_cast<u8*>(HeBytes(obj.get()) + 19) = 0x20;   // blocked bit
    g_s3.entities.push_back({60, obj.get()});
    InitLagerErweitern(r.get());
    CHECK_EQ(g_leaf.freeCalls, 1);
    CHECK_EQ(g_s3.args25Calls, 0);
}

TEST(CharActionY, InitLagerErweitern_ObjectArgs25AndCap) {
    ResetHooks();
    RecBuf r, obj;
    He_SavedTime(r.get()) = TestClock();
    Cas3_Mult(r.get()) = 1;
    Cas3_TargetId(r.get()) = 40;
    Cas3_Counter(r.get()) = 61;
    *reinterpret_cast<i32*>(HeBytes(obj.get()) + 2) = 555;   // entity id @ obj+2
    *reinterpret_cast<u8*>(HeBytes(obj.get()) + 18) = 80;    // cap @ obj+18
    g_s3.entities.push_back({61, obj.get()});
    int result = InitLagerErweitern(r.get());
    CHECK_EQ(g_s3.args25Calls, 1);
    CHECK_EQ(g_s3.args25[0].id, 555);
    CHECK_EQ(g_s3.args25[0].sel, 19);
    CHECK_EQ(g_s3.args25[0].mask, 32);
    CHECK_EQ(g_s3.args25[0].mode, 1);
    CHECK_EQ(result, 100);   // 40 + 80 = 120 -> clamp to 100
    CHECK_EQ(Cas3_Extent(r.get()), 100);
}

// ===========================================================================
// InitSabotage
// ===========================================================================
TEST(CharActionY, InitSabotage_SlotPresent) {
    ResetHooks();
    RecBuf r;
    Cas3_Slot16(r.get()) = 123;   // already resolved
    i32 ret = InitSabotage(r.get());
    CHECK_EQ(g_s3.buildop73Calls, 1);
    CHECK_EQ(g_s3.buildop73Person, 123);
    CHECK_EQ(ret, 9999);
    CHECK_EQ(*reinterpret_cast<i32*>(HeBytes(r.get()) + 196), -1);
    CHECK_EQ(*reinterpret_cast<i32*>(HeBytes(r.get()) + 200), 9999);
    CHECK(TimeEq(He_ApptTime(r.get()), 10, 8, 30, 16));   // +1 second
}

TEST(CharActionY, InitSabotage_QueryHitFirst) {
    ResetHooks();
    RecBuf r, person;
    Cas3_Slot16(r.get()) = -1;
    *reinterpret_cast<i32*>(HeBytes(person.get()) + 1) = 777;   // person id @ +1
    g_s3.queryResults.push_back(person.get());   // first query (1,5,22) hits
    InitSabotage(r.get());
    CHECK_EQ(Cas3_Slot16(r.get()), 777);
    CHECK_EQ(g_s3.buildop73Person, 777);
}

TEST(CharActionY, InitSabotage_QueryBothMiss) {
    ResetHooks();
    RecBuf r;
    Cas3_Slot16(r.get()) = -1;
    // no query results -> both miss -> slot stays -1
    InitSabotage(r.get());
    CHECK_EQ(Cas3_Slot16(r.get()), -1);
    CHECK_EQ(g_s3.buildop73Person, -1);
}

// ===========================================================================
// DuelIntroMessage
// ===========================================================================
TEST(CharActionY, DuelIntroMessage_BothCombatantsDisarmedAndMessaged) {
    ResetHooks();
    RecBuf h, a, b;
    *reinterpret_cast<i32*>(HeBytes(a.get()) + 4) = 11;   // A id
    *reinterpret_cast<i32*>(HeBytes(b.get()) + 4) = 22;   // B id
    *reinterpret_cast<u8*>(HeBytes(a.get()) + 2) = 6;     // A is player-kind
    *reinterpret_cast<u8*>(HeBytes(b.get()) + 2) = 7;     // B is player-kind
    *reinterpret_cast<u16*>(a.get()) = 101;               // A name word @0
    *reinterpret_cast<u16*>(b.get()) = 202;               // B name word @0
    i32 ret = DuelIntroMessage(h.get(), a.get(), b.get());
    // two disarms
    CHECK_EQ(g_s3.args25Calls, 2);
    CHECK_EQ(g_s3.args25[0].id, 11);
    CHECK_EQ(g_s3.args25[0].sel, 456);
    CHECK_EQ(g_s3.args25[1].id, 22);
    // two notify messages
    CHECK_EQ(g_s3.notifyCalls, 2);
    CHECK_EQ(g_s3.notify[0].to, 11);   CHECK_EQ(g_s3.notify[0].text, 6531); CHECK_EQ(g_s3.notify[0].name, 202);
    CHECK_EQ(g_s3.notify[1].to, 22);   CHECK_EQ(g_s3.notify[1].text, 6530); CHECK_EQ(g_s3.notify[1].name, 101);
    CHECK_EQ(g_leaf.cmd29Calls, 1);
    CHECK_EQ(g_leaf.cmd29Args[0], 2);
    CHECK_EQ(ret, 4242);
}

TEST(CharActionY, DuelIntroMessage_NonPlayerNoMessage) {
    ResetHooks();
    RecBuf h, a, b;
    *reinterpret_cast<u8*>(HeBytes(a.get()) + 2) = 1;   // not 6/7
    *reinterpret_cast<u8*>(HeBytes(b.get()) + 2) = 1;
    DuelIntroMessage(h.get(), a.get(), b.get());
    CHECK_EQ(g_s3.args25Calls, 2);   // disarms still happen
    CHECK_EQ(g_s3.notifyCalls, 0);   // no messages
    CHECK_EQ(g_leaf.cmd29Calls, 1);
}

// ===========================================================================
// NotifyMessageInit
// ===========================================================================
TEST(CharActionY, NotifyMessageInit_FlagSetReturnsZero) {
    ResetHooks();
    RecBuf r;
    He_Flags(r.get()) = 4;
    CHECK_EQ(NotifyMessageInit(r.get()), 0);
    CHECK_EQ(Cas3_Packet(r.get()), -1);
    CHECK_EQ(g_leaf.cmd29Calls, 0);
}

TEST(CharActionY, NotifyMessageInit_NoNeedsFlagArmsZero) {
    ResetHooks();
    RecBuf r;
    He_Flags(r.get()) = 0;   // bit2 clear, bit1 clear
    i32 ret = NotifyMessageInit(r.get());
    CHECK_EQ(ret, 4242);
    CHECK_EQ(g_leaf.cmd29Calls, 1);
    CHECK_EQ(g_leaf.cmd29Args[0], 0);
    CHECK(TimeEq(He_ApptTime(r.get()), 11, 8, 30, 15));   // +24 days
}

TEST(CharActionY, NotifyMessageInit_BothResolveSendsMessage) {
    ResetHooks();
    RecBuf r, p176, p172;
    He_Flags(r.get()) = 2;
    Cas3_TargetId(r.get()) = 176;   // id at +176 -> p176
    Cas3_Counter(r.get()) = 172;    // id at +172 -> p172
    *reinterpret_cast<u8*>(HeBytes(p176.get()) + 2) = 6;   // p176 kind 6
    *reinterpret_cast<i32*>(HeBytes(p176.get()) + 4) = 88; // p176 id
    *reinterpret_cast<u16*>(p172.get()) = 303;             // p172 name word
    g_s3.persons.push_back({176, p176.get()});
    g_s3.persons.push_back({172, p172.get()});
    i32 ret = NotifyMessageInit(r.get());
    CHECK_EQ(g_s3.notifyCalls, 1);
    CHECK_EQ(g_s3.notify[0].to, 88);
    CHECK_EQ(g_s3.notify[0].text, 6500);
    CHECK_EQ(g_s3.notify[0].name, 303);
    CHECK_EQ(g_leaf.cmd29Args[0], 0);   // arm 0 after message
    CHECK_EQ(ret, 4242);
}

TEST(CharActionY, NotifyMessageInit_ResolveFailArmsMinusOne) {
    ResetHooks();
    RecBuf r;
    He_Flags(r.get()) = 2;
    Cas3_TargetId(r.get()) = 176;   // unresolved
    Cas3_Counter(r.get()) = 172;
    i32 ret = NotifyMessageInit(r.get());
    CHECK_EQ(g_s3.notifyCalls, 0);
    CHECK_EQ(g_leaf.cmd29Calls, 1);
    CHECK_EQ(g_leaf.cmd29Args[0], -1);
    CHECK_EQ(ret, 4242);
}

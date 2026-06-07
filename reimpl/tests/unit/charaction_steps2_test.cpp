// Unit tests for charaction_steps2 — the remaining self-contained CharAction step
// leaves. Golden vectors for the GameTimeAdvance deltas were computed with python3
// mirroring src/sim/gametime.cpp (GameTimeAdvance) and the CRT LCG in
// src/crt/rand.cpp (Srand + RandNext + RandomModulo). Suite prefix: CharActionX.
#include "test.h"

#include "sim/charaction_steps2.h"
#include "sim/he.h"
#include "sim/npcaction.h"   // NpcClock / SetNpcClock, SetNpcLeafHooks
#include "crt/rand.h"        // Srand for deterministic RNG

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// A fresh, zeroed He record large enough for the +188 field accesses.
struct RecBuf {
    HeRecord rec{};
    RecBuf() { std::memset(&rec, 0, sizeof(rec)); }
    HeRecord* operator->() { return &rec; }
    HeRecord* get() { return &rec; }
};

// The test clock used across the GameTimeAdvance golden vectors.
GameTime TestClock() {
    GameTime t{};
    t.day = 10; t.hour = 8; t.minute = 30; t.second = 15;
    return t;
}

void SetClock() { SetNpcClock(TestClock()); }

// --- recording leaf-hook state ---------------------------------------------
struct LeafRec {
    int freeCalls = 0;
    int cmd29Calls = 0;
    int lastCmd29Arg = 0;
    i32 nextHandle = 0;
};
LeafRec g_leaf;

i32 RecFree(HeRecord*) { ++g_leaf.freeCalls; return 0; }
i32 RecCmd29(int arg, HeRecord*) { ++g_leaf.cmd29Calls; g_leaf.lastCmd29Arg = arg; return g_leaf.nextHandle; }
i32 RecPacketStatus(i32) { return 0; }

NpcLeafHooks MakeLeafHooks() {
    NpcLeafHooks h{};
    h.freeHandlerEntry = RecFree;
    h.queueRequestEntity29 = RecCmd29;
    h.packetStatus = RecPacketStatus;
    return h;
}

// --- recording step2-hook state --------------------------------------------
struct Step2Rec {
    int changeCalls = 0;
    std::vector<u16> changeMarkers;
    int cmd15Calls = 0; i32 cmd15City = 0; i32 cmd15Val = 0;
    int apCalls = 0; u16 apIndex = 0; i32 apNeg = 0;
    // person table: id -> marker word (0 == absent)
    std::vector<std::pair<i32, u16>> persons;
    // find-by-filter scan: a fixed list of records returned in order
    std::vector<HeRecord*> scanList;
    size_t scanCursor = 0;
};
Step2Rec g_s2;

HeRecord* RecFindFirst(int, const int*, const int*) {
    g_s2.scanCursor = 0;
    if (g_s2.scanList.empty()) return nullptr;
    return g_s2.scanList[g_s2.scanCursor++];
}
HeRecord* RecFindNext() {
    if (g_s2.scanCursor >= g_s2.scanList.size()) return nullptr;
    return g_s2.scanList[g_s2.scanCursor++];
}
HeRecord* RecFindPerson(i32 id) {
    static HeRecord persona;
    for (auto& p : g_s2.persons)
        if (p.first == id && p.second != 0) {
            std::memset(&persona, 0, sizeof(persona));
            *reinterpret_cast<u16*>(&persona) = p.second; // marker at +0
            return &persona;
        }
    return nullptr;
}
void RecChange(HeRecord*, u16 marker) { ++g_s2.changeCalls; g_s2.changeMarkers.push_back(marker); }
void RecCmd15(i32 city, i32 val) { ++g_s2.cmd15Calls; g_s2.cmd15City = city; g_s2.cmd15Val = val; }
void RecAp(u16 idx, i32 neg) { ++g_s2.apCalls; g_s2.apIndex = idx; g_s2.apNeg = neg; }
i32 RecResolveCity(u16 index) { return 1000 + index; }

CharActionStep2Hooks MakeStep2Hooks() {
    CharActionStep2Hooks h{};
    h.findFirstByFilter = RecFindFirst;
    h.findNextMatching = RecFindNext;
    h.findPersonById = RecFindPerson;
    h.changePlayerAction = RecChange;
    h.enqueueCmd15 = RecCmd15;
    h.registerApEvent = RecAp;
    h.resolveCityId = RecResolveCity;
    return h;
}

void ResetHooks() {
    g_leaf = LeafRec{};
    g_s2 = Step2Rec{};
    static NpcLeafHooks lh; lh = MakeLeafHooks(); SetNpcLeafHooks(&lh);
    static CharActionStep2Hooks sh; sh = MakeStep2Hooks(); SetCharActionStep2Hooks(&sh);
    SetClock();
}

bool TimeEq(const GameTime& t, i32 day, u16 hour, i32 minute, i32 second) {
    return t.day == day && t.hour == hour && t.minute == minute && t.second == second;
}

} // namespace

// ===========================================================================
// Timestamp / appointment reset leaves (golden GameTimeAdvance vectors).
// ===========================================================================

TEST(CharActionX, StateReset24) {
    ResetHooks();
    RecBuf r;
    int hr = StateReset24(r.get());
    CHECK_EQ(hr, 8);
    CHECK(TimeEq(Cas2_ApptTime(r.get()), 11, 8, 30, 15));
}

TEST(CharActionX, StateAdvancePos) {
    ResetHooks();
    RecBuf r;
    Cas2_Counter(r.get()) = 7;             // +172
    int hr = StateAdvancePos(r.get());
    CHECK_EQ(hr, 8);
    CHECK_EQ(Cas2_RemIter(r.get()), 8);    // +176 := +172 + 1
    CHECK(TimeEq(Cas2_ApptTime(r.get()), 11, 8, 30, 15));
}

TEST(CharActionX, StateReset96) {
    ResetHooks();
    RecBuf r;
    int hr = StateReset96(r.get());
    CHECK_EQ(hr, 8);
    CHECK(TimeEq(Cas2_ApptTime(r.get()), 14, 8, 30, 15));
}

TEST(CharActionX, StateReset0) {
    ResetHooks();
    RecBuf r;
    int hr = StateReset0(r.get());
    CHECK_EQ(hr, 8);
    CHECK(TimeEq(Cas2_ApptTime(r.get()), 10, 8, 35, 15));
}

TEST(CharActionX, StateReset0Alt2) {
    ResetHooks();
    RecBuf r;
    int hr = StateReset0Alt2(r.get());
    CHECK_EQ(hr, 8);
    CHECK(TimeEq(Cas2_ApptTime(r.get()), 10, 8, 32, 15));
}

TEST(CharActionX, StateCopyPos3) {
    ResetHooks();
    RecBuf r;
    Cas2_SavedTime(r.get()) = TestClock();   // +68 source
    int hr = StateCopyPos3(r.get());
    CHECK_EQ(hr, 11);
    CHECK(TimeEq(Cas2_ApptTime(r.get()), 10, 11, 30, 15));
}

TEST(CharActionX, CopyGoalToTarget) {
    ResetHooks();
    RecBuf r;
    Cas2_SavedTime(r.get()) = TestClock();
    int hr = CopyGoalToTarget(r.get());
    CHECK_EQ(hr, 8);
    CHECK(TimeEq(Cas2_ApptTime(r.get()), 10, 8, 45, 15));
}

TEST(CharActionX, CopyGoalToTargetState2) {
    ResetHooks();
    RecBuf r;
    Cas2_SavedTime(r.get()) = TestClock();
    int hr = CopyGoalToTargetState2(r.get());
    CHECK_EQ(hr, 10);
    CHECK(TimeEq(Cas2_ApptTime(r.get()), 10, 10, 30, 15));
}

TEST(CharActionX, ArrestReset) {
    ResetHooks();
    RecBuf r;
    int hr = ArrestReset(r.get());
    CHECK_EQ(hr, 10);
    // both saved (+68) and appt (+82) get the clock; appt advanced +2d.
    CHECK(TimeEq(Cas2_SavedTime(r.get()), 10, 8, 30, 15));
    CHECK(TimeEq(Cas2_ApptTime(r.get()), 10, 10, 30, 15));
}

// ===========================================================================
// Restore-pose / branch / finish leaves (RNG golden).
// ===========================================================================

TEST(CharActionX, RestorePosAndBranch) {
    ResetHooks();
    crt::Srand(12345);   // RandomModulo(4) == 0 -> r = 2
    RecBuf r;
    Cas2_SavedTime(r.get()) = TestClock();  // restored into +82
    Cas2_State(r.get()) = 99;
    int rv = RestorePosAndBranch(r.get());
    CHECK_EQ(rv, 2);
    CHECK_EQ(Cas2_State(r.get()), 0);
    CHECK_EQ(Cas2_RemIter(r.get()), -1);              // +176 := -1
    // +86 (appt hour) nudged by r (== 2) over the restored hour 8 -> 10
    CHECK_EQ(static_cast<int>(Cas2_ApptHour(r.get())), 10);
}

TEST(CharActionX, RestorePosFinish_NoFree) {
    ResetHooks();
    crt::Srand(1);   // RandomModulo(2) == 0 -> no free
    RecBuf r;
    Cas2_SavedTime(r.get()) = TestClock();
    int rv = RestorePosFinish(r.get());
    CHECK_EQ(rv, 0);
    CHECK_EQ(g_leaf.freeCalls, 0);
    CHECK(TimeEq(Cas2_ApptTime(r.get()), 10, 8, 30, 15));  // restored from +68
}

TEST(CharActionX, RestorePosFinish_Free) {
    ResetHooks();
    crt::Srand(3);   // RandomModulo(2) == 1 -> free
    RecBuf r;
    int rv = RestorePosFinish(r.get());
    CHECK_EQ(rv, 1);
    CHECK_EQ(g_leaf.freeCalls, 1);
}

TEST(CharActionX, ClearStateAndTimer) {
    ResetHooks();
    RecBuf r;
    Cas2_State(r.get()) = 7;
    Cas2_Counter(r.get()) = 9;
    HeRecord* out = ClearStateAndTimer(r.get());
    CHECK(out == r.get());
    CHECK_EQ(Cas2_State(r.get()), 0);
    CHECK_EQ(Cas2_Counter(r.get()), 0);
}

TEST(CharActionX, RetZero) {
    CHECK_EQ(RetZero(), 0);
}

// ===========================================================================
// Terminal-state finalizers.
// ===========================================================================

TEST(CharActionX, FinishIfTerminal) {
    ResetHooks();
    RecBuf r;
    // state == -2 -> free
    Cas2_State(r.get()) = -2;
    FinishIfTerminal(r.get());
    CHECK_EQ(g_leaf.freeCalls, 1);
    // state == 0 -> free (predicate true: state>=-2 && !state)
    Cas2_State(r.get()) = 0;
    FinishIfTerminal(r.get());
    CHECK_EQ(g_leaf.freeCalls, 2);
    // state == 1 -> no free
    Cas2_State(r.get()) = 1;
    FinishIfTerminal(r.get());
    CHECK_EQ(g_leaf.freeCalls, 2);
    // state == -1 -> not in predicate (state<=-2 is false, !state is false) -> no free
    Cas2_State(r.get()) = -1;
    FinishIfTerminal(r.get());
    CHECK_EQ(g_leaf.freeCalls, 2);
}

TEST(CharActionX, FinalizeEntityStep_Terminal) {
    ResetHooks();
    RecBuf r;
    Cas2_State(r.get()) = -2;
    CHECK_EQ(FinalizeEntityStep(r.get()), 0);   // free returns 0
    CHECK_EQ(g_leaf.freeCalls, 1);
    Cas2_State(r.get()) = -1;
    FinalizeEntityStep(r.get());
    CHECK_EQ(g_leaf.freeCalls, 2);
    Cas2_State(r.get()) = -3;
    CHECK_EQ(FinalizeEntityStep(r.get()), -3);  // state < -1, != -2 -> return state
    CHECK_EQ(g_leaf.freeCalls, 2);
}

TEST(CharActionX, FinalizeEntityStep_Rearm) {
    ResetHooks();
    g_leaf.nextHandle = 555;
    RecBuf r;
    Cas2_State(r.get()) = 0;
    Cas2_Flags(r.get()) = 2;     // needs-cmd29
    i32 rv = FinalizeEntityStep(r.get());
    CHECK_EQ(rv, 555);
    CHECK_EQ(g_leaf.cmd29Calls, 1);
    CHECK_EQ(g_leaf.lastCmd29Arg, -1);
    CHECK_EQ(Cas2_Packet(r.get()), 555);
    CHECK(TimeEq(Cas2_ApptTime(r.get()), 10, 8, 32, 15));   // +2 minutes
}

TEST(CharActionX, RequestEntityFinish) {
    ResetHooks();
    g_leaf.nextHandle = 77;
    RecBuf r;
    Cas2_Flags(r.get()) = 0;
    i32 rv = RequestEntityFinish(r.get());
    CHECK_EQ(rv, 77);
    CHECK_EQ(g_leaf.cmd29Calls, 1);
    CHECK_EQ(g_leaf.lastCmd29Arg, 0);
    CHECK_EQ(Cas2_Packet(r.get()), 77);
    CHECK(TimeEq(Cas2_ApptTime(r.get()), 11, 8, 30, 15));   // +24 days
    // gate set -> no request
    ResetHooks();
    RecBuf r2;
    Cas2_Flags(r2.get()) = 4;
    CHECK_EQ(RequestEntityFinish(r2.get()), 0);
    CHECK_EQ(g_leaf.cmd29Calls, 0);
}

TEST(CharActionX, RequestEntityIfValid) {
    ResetHooks();
    g_leaf.nextHandle = 88;
    RecBuf r;
    Cas2_State(r.get()) = 5;     // not terminal
    Cas2_Flags(r.get()) = 0;
    i32 rv = RequestEntityIfValid(r.get());
    CHECK_EQ(rv, 88);
    CHECK_EQ(g_leaf.lastCmd29Arg, -1);
    CHECK(TimeEq(Cas2_ApptTime(r.get()), 10, 8, 30, 15));   // clock, no advance
    // terminal -> free
    ResetHooks();
    RecBuf r2;
    Cas2_State(r2.get()) = -1;
    RequestEntityIfValid(r2.get());
    CHECK_EQ(g_leaf.freeCalls, 1);
}

TEST(CharActionX, ExtortInit) {
    ResetHooks();
    g_leaf.nextHandle = 42;
    RecBuf r;
    Cas2_Flags(r.get()) = 0;
    i32 rv = ExtortInit(r.get());
    CHECK_EQ(rv, 42);
    CHECK_EQ(g_leaf.lastCmd29Arg, 1);
    CHECK_EQ(Cas2_State(r.get()), 1);
    CHECK_EQ(Cas2_SuccId(r.get()), -1);
    CHECK_EQ(Cas2_Packet(r.get()), 42);
    CHECK(TimeEq(Cas2_ApptTime(r.get()), 10, 8, 32, 15));   // +2 minutes
    // gate set -> only +132 := -1
    ResetHooks();
    RecBuf r2;
    Cas2_Flags(r2.get()) = 4;
    CHECK_EQ(ExtortInit(r2.get()), 0);
    CHECK_EQ(Cas2_Packet(r2.get()), -1);
    CHECK_EQ(g_leaf.cmd29Calls, 0);
}

// ===========================================================================
// Repeat emitters.
// ===========================================================================

TEST(CharActionX, RepeatCommandStep_Rearm) {
    ResetHooks();
    RecBuf r;
    Cas2_State(r.get()) = 0;
    He_CityIndex(r.get()) = 3;     // +8
    Cas2_Counter(r.get()) = 250;   // +172 value
    Cas2_RemIter(r.get()) = 2;     // +176 -> becomes 1, re-arm
    int hr = RepeatCommandStep(r.get());
    CHECK_EQ(g_s2.cmd15Calls, 1);
    CHECK_EQ(g_s2.cmd15City, 1003);   // resolveCityId(3) == 1000+3
    CHECK_EQ(g_s2.cmd15Val, 250);
    CHECK_EQ(Cas2_RemIter(r.get()), 1);
    CHECK_EQ(hr, 8);                   // re-armed +24d
    CHECK_EQ(g_leaf.freeCalls, 0);
}

TEST(CharActionX, RepeatCommandStep_Done) {
    ResetHooks();
    RecBuf r;
    Cas2_State(r.get()) = 0;
    Cas2_RemIter(r.get()) = 1;     // -> 0, free
    RepeatCommandStep(r.get());
    CHECK_EQ(g_s2.cmd15Calls, 1);
    CHECK_EQ(g_leaf.freeCalls, 1);
}

TEST(CharActionX, RepeatCommandStep_Terminal) {
    ResetHooks();
    RecBuf r;
    Cas2_State(r.get()) = -1;
    RepeatCommandStep(r.get());
    CHECK_EQ(g_leaf.freeCalls, 1);
    CHECK_EQ(g_s2.cmd15Calls, 0);
}

TEST(CharActionX, RepeatTalkStep) {
    ResetHooks();
    RecBuf r;
    Cas2_State(r.get()) = 0;
    He_CityIndex(r.get()) = 9;
    Cas2_Counter(r.get()) = 40;
    Cas2_RemIter(r.get()) = 2;
    RepeatTalkStep(r.get());
    CHECK_EQ(g_s2.apCalls, 1);
    CHECK_EQ(static_cast<int>(g_s2.apIndex), 9);
    CHECK_EQ(g_s2.apNeg, -40);
    CHECK_EQ(Cas2_RemIter(r.get()), 1);
}

// ===========================================================================
// Group-action retargeters.
// ===========================================================================

TEST(CharActionX, ChangeGroupAction_Some) {
    ResetHooks();
    g_s2.persons = {{100, 11}, {200, 22}};
    RecBuf r;
    *reinterpret_cast<u8*>(reinterpret_cast<u8*>(r.get()) + 172) = 2;   // count
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(r.get()) + 140) = 100;
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(r.get()) + 144) = 200;
    int hr = ChangeGroupAction(r.get());
    CHECK_EQ(g_s2.changeCalls, 2);
    CHECK_EQ(static_cast<int>(g_s2.changeMarkers[0]), 11);
    CHECK_EQ(static_cast<int>(g_s2.changeMarkers[1]), 22);
    CHECK_EQ(Cas2_State(r.get()), 1);
    CHECK_EQ(hr, 8);                  // +5 minutes -> hour stays 8
    CHECK_EQ(g_leaf.freeCalls, 0);
}

TEST(CharActionX, ChangeGroupAction_None) {
    ResetHooks();
    g_s2.persons = {};   // nobody resolves
    RecBuf r;
    *reinterpret_cast<u8*>(reinterpret_cast<u8*>(r.get()) + 172) = 1;
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(r.get()) + 140) = 100;
    ChangeGroupAction(r.get());
    CHECK_EQ(g_s2.changeCalls, 0);
    CHECK_EQ(g_leaf.freeCalls, 1);    // none found -> free
}

TEST(CharActionX, ChangeGroupActionAndGoal) {
    ResetHooks();
    g_s2.persons = {{5, 55}};
    RecBuf r;
    Cas2_SavedTime(r.get()) = TestClock();
    *reinterpret_cast<u8*>(reinterpret_cast<u8*>(r.get()) + 172) = 1;
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(r.get()) + 140) = 5;
    int hr = ChangeGroupActionAndGoal(r.get());
    CHECK_EQ(g_s2.changeCalls, 1);
    CHECK_EQ(static_cast<int>(g_s2.changeMarkers[0]), 55);
    CHECK_EQ(hr, 8);                  // +1 minute
    CHECK(TimeEq(Cas2_ApptTime(r.get()), 10, 8, 31, 15));
    CHECK_EQ(g_leaf.freeCalls, 0);    // never frees
}

// ===========================================================================
// Find-by-filter scans.
// ===========================================================================

TEST(CharActionX, FindPairedEntityForward_Found) {
    ResetHooks();
    RecBuf a, b, m;
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(a.get()) + 4) = 700;  // a->id
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(b.get()) + 4) = 800;  // b->id
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(m.get()) + 172) = 800; // == b->id
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(m.get()) + 176) = 700; // == a->id
    g_s2.scanList = {m.get()};
    CHECK_EQ(FindPairedEntityForward(a.get(), b.get()), 0);   // found -> 0
}

TEST(CharActionX, FindPairedEntityForward_NotFound) {
    ResetHooks();
    RecBuf a, b, m;
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(a.get()) + 4) = 700;
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(b.get()) + 4) = 800;
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(m.get()) + 172) = 999;  // mismatch
    g_s2.scanList = {m.get()};
    CHECK_EQ(FindPairedEntityForward(a.get(), b.get()), 1);
    // empty scan -> 1
    g_s2.scanList = {};
    CHECK_EQ(FindPairedEntityForward(a.get(), b.get()), 1);
}

TEST(CharActionX, FindActionByActor_Found) {
    ResetHooks();
    RecBuf rec, m;
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(rec.get()) + 4) = 321;   // rec->id
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(m.get()) + 188) = 321;   // match
    g_s2.scanList = {m.get()};
    u16 marker = 5;
    i32 count = -123;
    CHECK_EQ(FindActionByActor(&marker, rec.get(), &count), 0);   // found -> 0
}

TEST(CharActionX, FindActionByActor_NotFound) {
    ResetHooks();
    RecBuf rec, m1, m2;
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(rec.get()) + 4) = 321;
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(m1.get()) + 188) = 1;   // mismatch
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(m2.get()) + 188) = 2;   // mismatch
    g_s2.scanList = {m1.get(), m2.get()};
    u16 marker = 5;
    i32 count = 0;
    CHECK_EQ(FindActionByActor(&marker, rec.get(), &count), 1);
    // count = number of FindNext increments before scan ends: 2 (m1 mismatch ->
    // FindNext->m2, ++count; m2 mismatch -> FindNext->null, ++count).
    CHECK_EQ(count, 2);
    // empty scan -> count stays at its incoming inc path: first==null so loop body
    // never runs -> count 0.
    g_s2.scanList = {};
    count = 99;
    CHECK_EQ(FindActionByActor(&marker, rec.get(), &count), 1);
    CHECK_EQ(count, 0);
}

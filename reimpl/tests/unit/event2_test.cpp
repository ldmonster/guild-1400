// Unit tests for src/world/event2.cpp — VIBE_Event_* action bodies.
// Golden vectors computed with python3 against the faithful GameTime arithmetic
// (VIBE_GameTime_Advance) and the CRT LCG (state*1103515245+12345; bits 16..30).
#include "test.h"

#include "world/event2.h"
#include "sim/npcaction.h"   // SetNpcClock
#include "crt/rand.h"        // Srand

#include <cstring>

using namespace guild;
using guild::world::HeRecord;
using guild::sim::GameTime;

namespace {

// Allocate a zeroed He record on the heap (it is large; ~0x202 bytes) so the
// raw-offset accessors stay in bounds.
HeRecord* MakeRecord() {
    HeRecord* h = new HeRecord();
    std::memset(h, 0, sizeof(*h));
    return h;
}

void SetClock(int day, int hour, int minute, int second) {
    GameTime t{};
    t.day = day; t.hour = static_cast<u16>(hour); t.minute = minute; t.second = second;
    guild::sim::SetNpcClock(t);
}

}  // namespace

// ---------------------------------------------------------------------------
// Action initializers that stamp the clock and advance the appointment time.
// Clock fixed at day=10, hour=8, minute=30, second=0.
// ---------------------------------------------------------------------------
TEST(EventSys, ResetActionState_Plus1Second) {
    HeRecord* h = MakeRecord();
    SetClock(10, 8, 30, 0);
    int r = world::ResetActionState(h);
    GameTime& a = guild::sim::He_ApptTime(h);
    // Golden: (day,hour,minute,second,result) = (10,8,30,1,8)
    CHECK_EQ(a.day, 10);
    CHECK_EQ((int)a.hour, 8);
    CHECK_EQ(a.minute, 30);
    CHECK_EQ(a.second, 1);
    CHECK_EQ(r, 8);
    delete h;
}

TEST(EventSys, ResetActionStateZero_Plus20Minutes) {
    HeRecord* h = MakeRecord();
    SetClock(10, 8, 30, 0);
    int r = world::ResetActionStateZero(h);
    GameTime& a = guild::sim::He_ApptTime(h);
    // Golden: (10,8,50,0,8)
    CHECK_EQ(a.day, 10);
    CHECK_EQ((int)a.hour, 8);
    CHECK_EQ(a.minute, 50);
    CHECK_EQ(a.second, 0);
    CHECK_EQ(r, 8);
    delete h;
}

TEST(EventSys, SetActionAnim7_WritesAnimFields) {
    HeRecord* h = MakeRecord();
    SetClock(10, 8, 30, 0);
    HeRecord* ret = world::SetActionAnim7(h);
    CHECK(ret == h);
    GameTime& a = guild::sim::He_ApptTime(h);
    CHECK_EQ(a.day, 10);          // clock stamped
    // +86 (anim word == hour slot) set to 7, +88 (anim param == minute slot) 0.
    CHECK_EQ((int)a.hour, 7);
    CHECK_EQ(a.minute, 0);
    delete h;
}

TEST(EventSys, AllocActionResetFields_Plus1MinuteAndZeroes) {
    HeRecord* h = MakeRecord();
    // saved time (+68) = day=10,hour=8,min=30,sec=0
    GameTime& saved = guild::sim::He_SavedTime(h);
    saved.day = 10; saved.hour = 8; saved.minute = 30; saved.second = 0;
    // poison +188/+192/+196 to verify they are zeroed
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(h) + 188) = 7;
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(h) + 192) = 7;
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(h) + 196) = 7;
    int r = world::AllocActionResetFields(h);
    GameTime& a = guild::sim::He_ApptTime(h);
    // Golden: (10,8,31,0,8)
    CHECK_EQ(a.day, 10);
    CHECK_EQ((int)a.hour, 8);
    CHECK_EQ(a.minute, 31);
    CHECK_EQ(r, 8);
    CHECK_EQ(*reinterpret_cast<i32*>(reinterpret_cast<u8*>(h) + 188), 0);
    CHECK_EQ(*reinterpret_cast<i32*>(reinterpret_cast<u8*>(h) + 192), 0);
    CHECK_EQ(*reinterpret_cast<i32*>(reinterpret_cast<u8*>(h) + 196), 0);
    // scratch copy at +96 mirrors the appointment.
    GameTime& sc = *reinterpret_cast<GameTime*>(reinterpret_cast<u8*>(h) + 96);
    // scratch was copied from +82 BEFORE the +1 minute advance -> minute 30.
    CHECK_EQ(sc.minute, 30);
    delete h;
}

TEST(EventSys, AllocActionResetFlags_ZeroesOtherFields) {
    HeRecord* h = MakeRecord();
    GameTime& saved = guild::sim::He_SavedTime(h);
    saved.day = 10; saved.hour = 8; saved.minute = 30; saved.second = 0;
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(h) + 172) = 9;
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(h) + 176) = 9;
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(h) + 180) = 9;
    int r = world::AllocActionResetFlags(h);
    GameTime& a = guild::sim::He_ApptTime(h);
    CHECK_EQ(a.minute, 31);
    CHECK_EQ(r, 8);
    CHECK_EQ(*reinterpret_cast<i32*>(reinterpret_cast<u8*>(h) + 172), 0);
    CHECK_EQ(*reinterpret_cast<i32*>(reinterpret_cast<u8*>(h) + 176), 0);
    CHECK_EQ(*reinterpret_cast<i32*>(reinterpret_cast<u8*>(h) + 180), 0);
    delete h;
}

// ---------------------------------------------------------------------------
// RNG-driven initializers. Seed the LCG to 12345 so RandomModulo is determined.
// ---------------------------------------------------------------------------
TEST(EventSys, AllocActionAnim4_RngWait) {
    HeRecord* h = MakeRecord();
    GameTime& saved = guild::sim::He_SavedTime(h);
    saved.day = 10; saved.hour = 8; saved.minute = 30; saved.second = 0;
    crt::Srand(12345);  // first RandomModulo(60) -> 48 (golden)
    int r = world::AllocActionAnim4(h);
    GameTime& a = guild::sim::He_ApptTime(h);
    // Golden: rng=48 -> (10,13,18,0,13)
    CHECK_EQ(a.day, 10);
    CHECK_EQ((int)a.hour, 13);
    CHECK_EQ(a.minute, 18);
    CHECK_EQ(r, 13);
    delete h;
}

TEST(EventSys, AllocActionAnim7_RngWait) {
    HeRecord* h = MakeRecord();
    GameTime& saved = guild::sim::He_SavedTime(h);
    saved.day = 10; saved.hour = 8; saved.minute = 30; saved.second = 0;
    crt::Srand(12345);  // first RandomModulo(30) -> 18 (golden)
    int r = world::AllocActionAnim7(h);
    GameTime& a = guild::sim::He_ApptTime(h);
    // Golden: rng=18, advance(7,0,18+30=48) -> (10,16,18,0,16)
    CHECK_EQ(a.day, 10);
    CHECK_EQ((int)a.hour, 16);
    CHECK_EQ(a.minute, 18);
    CHECK_EQ(r, 16);
    delete h;
}

// ---------------------------------------------------------------------------
// AppendCollectedHandle: writes into the +4 array, bumps +260, gates at 64.
// ---------------------------------------------------------------------------
TEST(EventSys, AppendCollectedHandle_PushAndCount) {
    HeRecord* h = MakeRecord();
    // count at +260 starts 0
    CHECK(world::AppendCollectedHandle(0x111, h));  // count->1, <64 true
    CHECK(world::AppendCollectedHandle(0x222, h));  // count->2
    i32* base = reinterpret_cast<i32*>(reinterpret_cast<u8*>(h) + 4);
    CHECK_EQ(base[0], 0x111);  // *(h + 4*0 + 4)
    CHECK_EQ(base[1], 0x222);  // *(h + 4*1 + 4)
    CHECK_EQ(*reinterpret_cast<i32*>(reinterpret_cast<u8*>(h) + 260), 2);
    delete h;
}

TEST(EventSys, AppendCollectedHandle_GateAt64) {
    HeRecord* h = MakeRecord();
    // Drive count to 62 -> 63 (still <64), then 63 -> 64 (returns false).
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(h) + 260) = 62;
    CHECK(world::AppendCollectedHandle(1, h) == true);   // count 63
    CHECK(world::AppendCollectedHandle(2, h) == false);  // count 64 -> not < 64
    delete h;
}

// ---------------------------------------------------------------------------
// Trivial stubs.
// ---------------------------------------------------------------------------
TEST(EventSys, Stubs) {
    CHECK_EQ(world::RetZero(), 0);
    world::NullSub7();
    world::NullSub8();
    CHECK(true);
}

// ---------------------------------------------------------------------------
// Phase-machine teardown branches: state -2/-1 free the handler.
// ---------------------------------------------------------------------------
static int g_freeCalls = 0;
static guild::world::HeRecord* g_lastFreed = nullptr;
static int g_sendCalls = 0;
static i32 RecFree(guild::world::HeRecord* h) { ++g_freeCalls; g_lastFreed = h; return 0xABCD; }
static void RecSend(i32, i32, i32, const void*, i32, i32) { ++g_sendCalls; }

TEST(EventSys, HelpTextPlayback_TeardownStates) {
    guild::world::EventHooks hooks{ &RecFree, &RecSend };
    world::SetEventHooks(&hooks);
    for (int state : {-2, -1}) {
        HeRecord* h = MakeRecord();
        guild::sim::He_State(h) = state;
        g_freeCalls = 0;
        int r = world::HelpTextPlaybackRun(h, /*helpEnabled*/ true);
        CHECK_EQ(r, 0xABCD);
        CHECK_EQ(g_freeCalls, 1);
        CHECK(g_lastFreed == h);
        delete h;
    }
    world::SetEventHooks(nullptr);
}

TEST(EventSys, HelpTextPlayback_IdlePassthrough) {
    HeRecord* h = MakeRecord();
    guild::sim::He_State(h) = 5;  // state+2 = 7 -> default branch
    int r = world::HelpTextPlaybackRun(h, true);
    CHECK_EQ(r, 7);
    delete h;
}

TEST(EventSys, HelpAdviceLoop_TeardownAndReschedule) {
    guild::world::EventHooks hooks{ &RecFree, &RecSend };
    world::SetEventHooks(&hooks);
    // teardown
    HeRecord* h = MakeRecord();
    guild::sim::He_State(h) = -2;
    g_freeCalls = 0;
    CHECK_EQ(world::HelpAdviceLoopRun(h, true), 0xABCD);
    CHECK_EQ(g_freeCalls, 1);
    delete h;

    // body, advice disabled -> +1 day, no send
    HeRecord* h2 = MakeRecord();
    SetClock(10, 8, 30, 0);
    guild::sim::He_State(h2) = 0;
    g_sendCalls = 0;
    int r = world::HelpAdviceLoopRun(h2, /*adviceEnabled*/ false);
    GameTime& a = guild::sim::He_ApptTime(h2);
    // advance(1,0,0) -> hour 8 -> result 8 ; +1 to hour doesn't roll a day here
    CHECK_EQ(a.day, 10);
    CHECK_EQ((int)a.hour, 9);  // 8 + 1 hour
    CHECK_EQ(r, 9);
    CHECK_EQ(g_sendCalls, 0);
    delete h2;
    world::SetEventHooks(nullptr);
}

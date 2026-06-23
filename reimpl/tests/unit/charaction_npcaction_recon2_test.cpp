// Golden-vector unit tests for the recovered VIBE_CharAction_DrinkInit (0x4d21ec)
// in src/sim/charaction_npcaction_recon2.{h,cpp}. Each test drives the routine with
// an instrumented CharActionRecon2Hooks table and asserts the exact control-flow
// observable (early-outs, action-type/state/duration stamping, clock advance) against
// the gilde.exe decompilation. Self-contained; uses only the shared test framework
// and the public recon2 API.
#include "tests/framework/test.h"

#include "sim/charaction_npcaction_recon2.h"
#include "sim/he.h"
#include "sim/npcaction.h"   // NpcClock / SetNpcClock

#include <cstring>
#include <vector>
#include <string>

using namespace guild;
using namespace guild::sim;

namespace {

// A heap-backed He record (>= 256 bytes covers every offset DrinkInit touches).
struct FakeHe {
    u8 buf[320];
    FakeHe() { std::memset(buf, 0, sizeof(buf)); }
    HeRecord* he() { return reinterpret_cast<HeRecord*>(buf); }
};

// Recording / programmable harness for the recon2 hook bridge.
struct Rec2 {
    std::vector<std::string> log;

    // statTableByte programmability: returns `statVal` for any (row, sub).
    u8  statVal = 0;
    // handler-pool scan: returns the queued sequence then null.
    std::vector<HeRecord*> filterSeq;
    size_t filterPos = 0;
    // real-time-mode global.
    i32 realTime = 0;
    // freeHandlerEntry sentinel.
    i32 freeResult = 0x4f;
};
Rec2* g = nullptr;

HeRecord* hFirst(int a, int b, i32 row) {
    g->log.push_back("first:" + std::to_string(a) + ":" + std::to_string(b) + ":" + std::to_string(row));
    g->filterPos = 0;
    return g->filterPos < g->filterSeq.size() ? g->filterSeq[g->filterPos++] : nullptr;
}
HeRecord* hNext() {
    g->log.push_back("next");
    return g->filterPos < g->filterSeq.size() ? g->filterSeq[g->filterPos++] : nullptr;
}
i32 hFree(HeRecord*) { g->log.push_back("free"); return g->freeResult; }
u8  hStat(i32 row, int sub) {
    g->log.push_back("stat:" + std::to_string(row) + ":" + std::to_string(sub));
    return g->statVal;
}
i32 hRealTime() { g->log.push_back("rt"); return g->realTime; }

CharActionRecon2Hooks MakeHooks() {
    return CharActionRecon2Hooks{ hFirst, hNext, hFree, hStat, hRealTime };
}

void SetClock(int day, int hour, int minute, int second) {
    GameTime t;
    t.day = day; t.hour = static_cast<u16>(hour); t.minute = minute; t.second = second;
    SetNpcClock(t);
}

// Field readers (byte-faithful offsets DrinkInit writes).
u8  ActionType(FakeHe& f) { return f.buf[172]; }
u8  Duration(FakeHe& f)   { return f.buf[173]; }
i32 State(FakeHe& f)      { return *reinterpret_cast<i32*>(f.buf + 112); }

} // namespace

// ---------------------------------------------------------------------------
// Gate 1: already-saturated NPC frees the entry and bails. No other work.
// ---------------------------------------------------------------------------
TEST(CharAction2Recon2Drink, SaturatedFreesAndBails) {
    Rec2 r; g = &r;
    auto hooks = MakeHooks(); SetCharActionRecon2Hooks(&hooks);
    SetClock(10, 12, 0, 0);

    FakeHe f;
    *reinterpret_cast<u16*>(f.buf + 8) = 7;          // row index
    *reinterpret_cast<i32*>(f.buf + 171) = 3 << 24;  // sub-method = 3
    r.statVal = 252;                                 // == cap (>= triggers)

    i32 ret = CharRecon2DrinkInit(f.he());

    CHECK_EQ(ret, 0x4f);                             // freeHandlerEntry result
    // Reads the table once with the right (row,sub), then frees. No scan/stamp.
    CHECK_EQ(r.log.size(), static_cast<size_t>(2));
    CHECK_EQ(r.log[0], std::string("stat:7:3"));
    CHECK_EQ(r.log[1], std::string("free"));
    CHECK_EQ(ActionType(f), static_cast<u8>(0));     // never stamped
    SetCharActionRecon2Hooks(nullptr);
}

// 251.999... < 252 -> NOT saturated, proceeds (boundary just below cap).
TEST(CharAction2Recon2Drink, JustBelowCapProceeds) {
    Rec2 r; g = &r;
    auto hooks = MakeHooks(); SetCharActionRecon2Hooks(&hooks);
    SetClock(0, 0, 0, 0);

    FakeHe f;
    *reinterpret_cast<u16*>(f.buf + 8) = 0;
    *reinterpret_cast<i32*>(f.buf + 171) = 0;
    r.statVal = 251;                                 // < 252 -> proceed
    r.filterSeq = {};                                // no other handler

    CharRecon2DrinkInit(f.he());

    CHECK_EQ(ActionType(f), static_cast<u8>(21));    // stamped -> proceeded
    SetCharActionRecon2Hooks(nullptr);
}

// ---------------------------------------------------------------------------
// Gate 2: another drinker (a DIFFERENT handler) present -> free + bail.
// ---------------------------------------------------------------------------
TEST(CharAction2Recon2Drink, OtherDrinkerBails) {
    Rec2 r; g = &r;
    auto hooks = MakeHooks(); SetCharActionRecon2Hooks(&hooks);
    SetClock(5, 8, 30, 0);

    FakeHe self, other;
    *reinterpret_cast<u16*>(self.buf + 8) = 3;
    *reinterpret_cast<i32*>(self.buf + 171) = 0;
    r.statVal = 10;
    r.filterSeq = { other.he() };                    // first match is NOT self

    i32 ret = CharRecon2DrinkInit(self.he());

    CHECK_EQ(ret, 0x4f);
    CHECK_EQ(ActionType(self), static_cast<u8>(0));  // never stamped
    // filter(2,2,row) issued, then free.
    CHECK_EQ(r.log[1], std::string("first:2:2:3"));
    CHECK_EQ(r.log.back(), std::string("free"));
    SetCharActionRecon2Hooks(nullptr);
}

// First match is self, second is another -> skip self, find other -> bail.
TEST(CharAction2Recon2Drink, SelfThenOtherBails) {
    Rec2 r; g = &r;
    auto hooks = MakeHooks(); SetCharActionRecon2Hooks(&hooks);
    SetClock(5, 8, 30, 0);

    FakeHe self, other;
    *reinterpret_cast<u16*>(self.buf + 8) = 3;
    r.statVal = 10;
    r.filterSeq = { self.he(), other.he() };         // self first, then a peer

    i32 ret = CharRecon2DrinkInit(self.he());

    CHECK_EQ(ret, 0x4f);
    CHECK_EQ(ActionType(self), static_cast<u8>(0));
    SetCharActionRecon2Hooks(nullptr);
}

// Only match is self -> next() returns null -> proceed.
TEST(CharAction2Recon2Drink, SelfOnlyProceeds) {
    Rec2 r; g = &r;
    auto hooks = MakeHooks(); SetCharActionRecon2Hooks(&hooks);
    SetClock(2, 6, 0, 0);

    FakeHe self;
    *reinterpret_cast<u16*>(self.buf + 8) = 9;
    r.statVal = 0;
    r.filterSeq = { self.he() };                     // only ourselves in the pool

    CharRecon2DrinkInit(self.he());

    CHECK_EQ(ActionType(self), static_cast<u8>(21)); // proceeded
    SetCharActionRecon2Hooks(nullptr);
}

// ---------------------------------------------------------------------------
// Duration math: duration = 2*(int)(statByte*0.0238095 + 1.0) + 4.
//   statByte=0   -> (int)(1.0)        = 1  -> 2*1+4 = 6
//   statByte=42  -> (int)(2.0)        = 2  -> 2*2+4 = 8
//   statByte=84  -> (int)(3.0)        = 3  -> 2*3+4 = 10
//   statByte=251 -> (int)(6.976...)   = 6  -> 2*6+4 = 16
// ---------------------------------------------------------------------------
TEST(CharAction2Recon2Drink, DurationGoldenVectors) {
    auto run = [](u8 stat) -> u8 {
        Rec2 r; g = &r;
        auto hooks = MakeHooks(); SetCharActionRecon2Hooks(&hooks);
        SetClock(0, 0, 0, 0);
        FakeHe f;
        r.statVal = stat;
        r.filterSeq = {};
        CharRecon2DrinkInit(f.he());
        u8 d = f.buf[173];
        SetCharActionRecon2Hooks(nullptr);
        return d;
    };
    CHECK_EQ(run(0),   static_cast<u8>(6));
    CHECK_EQ(run(42),  static_cast<u8>(8));
    CHECK_EQ(run(84),  static_cast<u8>(10));
    CHECK_EQ(run(251), static_cast<u8>(16));
}

// ---------------------------------------------------------------------------
// State stamping + clock advance. VIBE_GameTime_Advance(rec, a2, a3, a4) adds
// a3 to seconds, a4 to minutes, and a2 to the HOUR accumulator (result = a2+hour,
// carrying into days only at >=24). DrinkInit calls Advance(a1+82, v9, v8, 0):
//   real-time mode (dword_63C7B8 != 0): v8=1,v9=0 -> +1 second.
//   paused mode    (dword_63C7B8 == 0): v9=5,v8=0 -> +5 HOURS.
// The saved image at a1+68 is the *unchanged* system clock (qword_13CE852), NOT
// the advanced appt. Action-type byte = 21, state = 0 in both.
// ---------------------------------------------------------------------------
TEST(CharAction2Recon2Drink, RealTimeModeAdvancesSecond) {
    Rec2 r; g = &r;
    auto hooks = MakeHooks(); SetCharActionRecon2Hooks(&hooks);
    SetClock(3, 10, 20, 30);

    FakeHe f;
    r.statVal = 0;
    r.filterSeq = {};
    r.realTime = 1;                                  // dword_63C7B8 != 0

    CharRecon2DrinkInit(f.he());

    CHECK_EQ(ActionType(f), static_cast<u8>(21));
    CHECK_EQ(State(f), 0);
    // appointment clock at +82: stamped to NpcClock then advanced +1 second.
    GameTime* appt = reinterpret_cast<GameTime*>(f.buf + 82);
    CHECK_EQ(appt->day, 3);
    CHECK_EQ(static_cast<int>(appt->hour), 10);
    CHECK_EQ(appt->minute, 20);
    CHECK_EQ(appt->second, 31);                      // +1 second
    SetCharActionRecon2Hooks(nullptr);
}

TEST(CharAction2Recon2Drink, PausedModeAdvancesFiveHours) {
    Rec2 r; g = &r;
    auto hooks = MakeHooks(); SetCharActionRecon2Hooks(&hooks);
    SetClock(3, 10, 20, 30);

    FakeHe f;
    r.statVal = 0;
    r.filterSeq = {};
    r.realTime = 0;                                  // dword_63C7B8 == 0

    CharRecon2DrinkInit(f.he());

    GameTime* appt = reinterpret_cast<GameTime*>(f.buf + 82);
    CHECK_EQ(appt->day, 3);                          // 10+5=15 < 24, no day carry
    CHECK_EQ(static_cast<int>(appt->hour), 15);      // +5 hours
    CHECK_EQ(appt->minute, 20);
    CHECK_EQ(appt->second, 30);
    // saved clock at +68 is the UNCHANGED system clock (qword_13CE852), not appt.
    GameTime* saved = reinterpret_cast<GameTime*>(f.buf + 68);
    CHECK_EQ(saved->day, 3);
    CHECK_EQ(static_cast<int>(saved->hour), 10);
    CHECK_EQ(saved->minute, 20);
    CHECK_EQ(saved->second, 30);
    SetCharActionRecon2Hooks(nullptr);
}

// ---------------------------------------------------------------------------
// Default (no hooks installed) exercises the inert path headlessly: stat 0,
// empty pool -> proceeds, duration 6, state 0, action-type 21.
// ---------------------------------------------------------------------------
TEST(CharAction2Recon2Drink, InertDefaultHeadless) {
    SetCharActionRecon2Hooks(nullptr);
    SetClock(1, 0, 0, 0);
    FakeHe f;
    CharRecon2DrinkInit(f.he());
    CHECK_EQ(ActionType(f), static_cast<u8>(21));
    CHECK_EQ(State(f), 0);
    CHECK_EQ(Duration(f), static_cast<u8>(6));       // stat 0 -> 2*1+4
}

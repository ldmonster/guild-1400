#include "test.h"
#include "crt/time.h"
#include "crt/mbcs.h"

#include <ctime>

using namespace guild;

namespace {
class MockClock : public shim::IPlatform {
public:
    bool createMainWindow(const char*, int, int, bool) override { return true; }
    void destroyMainWindow() override {}
    bool pumpMessages() override { return true; }
    u32  timeMs() override { return now_; }
    void sleepMs(u32 ms) override { now_ += ms; }
    void getMouse(shim::MouseState&) override {}
    bool keyDown(int) override { return false; }
    void advance(u32 ms) { now_ += ms; }
private:
    u32 now_ = 0;
};

int g_count = 0;
void onTick() { ++g_count; }
} // namespace

// Full calendar flow: epoch -> tm -> epoch identity across a wide range,
// cross-checked against libc gmtime for the broken-down form.
TEST(CrtTimeE2E, EpochTmRoundTripRange) {
    // ~40 years, stepping by a non-aligned amount to hit varied times of day.
    for (u32 t = 0; t < 1262304000U; t += 90061U) {
        crt::TmFields f = crt::Gmtime(t);

        // Cross-check the decomposition against libc.
        std::time_t tt = static_cast<std::time_t>(t);
        std::tm g{};
#if defined(_WIN32)
        gmtime_s(&g, &tt);
#else
        gmtime_r(&tt, &g);
#endif
        CHECK_EQ(f.sec, g.tm_sec);
        CHECK_EQ(f.min, g.tm_min);
        CHECK_EQ(f.hour, g.tm_hour);
        CHECK_EQ(f.mday, g.tm_mday);
        CHECK_EQ(f.mon, g.tm_mon);
        CHECK_EQ(f.year, g.tm_year);
        CHECK_EQ(f.wday, g.tm_wday);
        CHECK_EQ(f.yday, g.tm_yday);

        // Inverse must reproduce the original epoch exactly.
        f.isdst = 0;
        CHECK_EQ(crt::MakeTimeUtc(&f), static_cast<i32>(t));
    }
}

// MBCS flow: walk a Shift-JIS string counting logical characters.
TEST(CrtTimeE2E, MbcsWalkShiftJisString) {
    crt::Mbcs().SetCp932LeadBytes();
    // "AあBいC" approx: 'A'(1) 'あ'(82 A0) 'B'(1) 'い'(82 A2) 'C'(1) NUL
    const u8 s[] = {'A', 0x82, 0xA0, 'B', 0x82, 0xA2, 'C', 0x00};

    int chars = 0;
    const u8* p = s;
    while (!crt::IsStringEnd(p)) {
        ++chars;
        p = crt::AdvanceChar(p);
    }
    CHECK_EQ(chars, 5);          // 3 ASCII + 2 double-byte
    CHECK(p == s + 7);           // landed on the NUL
    CHECK_EQ(crt::IsStringEnd(p), 1);

    crt::Mbcs() = crt::MbcsState{}; // restore inactive locale
}

// TimeBase flow: schedule periodic procs at different intervals and drive them
// from the mock clock, verifying fire counts.
TEST(CrtTimeE2E, TimeBasePeriodicScheduling) {
    g_count = 0;
    MockClock clk;
    crt::TimeBase tb(&clk);
    tb.RegisterProc(onTick, 3); // fire every 3rd tick
    CHECK_EQ(tb.StartTimer(5 /*ms per tick*/, 1 /*periodic*/), 1);

    // Run for 300 ms => 60 ticks => counter 0..59 => fires when counter%3==0
    // => {0,3,...,57} = 20 fires.
    clk.advance(300);
    tb.PumpFromClock();
    CHECK_EQ(tb.MasterTicks(), 60u);
    CHECK_EQ(g_count, 20);
}

// StartTimer's second argument selects the re-arm MECHANISM, not whether the
// timer repeats: a2 == 0 arms a TIME_PERIODIC winmm timer (@0x44e28c
// fuEvent = (a2 == 0)); a2 != 0 arms a one-shot that fptc re-arms each tick
// (@0x44e209, dword_62EB50). Both modes tick continuously until StopTimer —
// the app boots with StartTimer(0xE, 0) @0x527e52 and runs forever on it.
TEST(CrtTimeE2E, TimeBaseModeZeroIsContinuous) {
    g_count = 0;
    MockClock clk;
    crt::TimeBase tb(&clk);
    tb.RegisterProc(onTick, 1);
    CHECK_EQ(tb.StartTimer(10, 0 /*winmm TIME_PERIODIC mode*/), 1);
    clk.advance(100);
    tb.PumpFromClock();
    CHECK_EQ(g_count, 10);         // keeps ticking — 100 ms / 10 ms = 10 fires
    CHECK_EQ(tb.MasterTicks(), 10u);
    tb.StopTimer();                // 0x44e2c4 — the only way ticking ends
    clk.advance(100);
    tb.PumpFromClock();
    CHECK_EQ(tb.MasterTicks(), 10u);
}

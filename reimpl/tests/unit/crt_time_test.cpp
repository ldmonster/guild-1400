#include "test.h"
#include "crt/time.h"
#include "crt/mbcs.h"

#include <ctime>
#include <vector>

using namespace guild;

// ---------------------------------------------------------------------------
// Mock IPlatform: a controllable monotonic millisecond clock for TimeBase.
// ---------------------------------------------------------------------------
namespace {
class MockPlatform : public shim::IPlatform {
public:
    bool createMainWindow(const char*, int, int, bool) override { return true; }
    void destroyMainWindow() override {}
    bool pumpMessages() override { return true; }
    u32  timeMs() override { return now_; }
    void sleepMs(u32 ms) override { now_ += ms; }
    void getMouse(shim::MouseState&) override {}
    bool keyDown(int) override { return false; }

    void advance(u32 ms) { now_ += ms; }
    void set(u32 ms) { now_ = ms; }
private:
    u32 now_ = 0;
};

// gmtime oracle via libc (force UTC by passing through C gmtime on a copy).
crt::TmFields oracleGmtime(u32 t) {
    std::time_t tt = static_cast<std::time_t>(t);
    std::tm g{};
#if defined(_WIN32)
    gmtime_s(&g, &tt);
#else
    gmtime_r(&tt, &g);
#endif
    crt::TmFields f{};
    f.sec = g.tm_sec; f.min = g.tm_min; f.hour = g.tm_hour;
    f.mday = g.tm_mday; f.mon = g.tm_mon; f.year = g.tm_year;
    f.wday = g.tm_wday; f.yday = g.tm_yday; f.isdst = 0;
    return f;
}
} // namespace

// ---------------------------------------------------------------------------
// Leap year
// ---------------------------------------------------------------------------
TEST(CrtTime, IsLeapYear) {
    CHECK(crt::IsLeapYear(2000));   // div by 400
    CHECK(crt::IsLeapYear(1972));
    CHECK(crt::IsLeapYear(2020));
    CHECK(!crt::IsLeapYear(1900));  // div by 100 not 400
    CHECK(!crt::IsLeapYear(2100));
    CHECK(!crt::IsLeapYear(1970));
    CHECK(!crt::IsLeapYear(2019));
    CHECK(crt::IsLeapYear(2400));
    // Cross-check against the standard rule for a wide range.
    for (u32 y = 1601; y <= 2400; ++y) {
        bool ref = (y % 4 == 0) && (y % 100 != 0 || y % 400 == 0);
        CHECK_EQ(crt::IsLeapYear(y), ref);
    }
}

// ---------------------------------------------------------------------------
// gmtime golden vectors (computed with the C oracle, see generator in report)
// ---------------------------------------------------------------------------
TEST(CrtTime, GmtimeGoldenVectors) {
    struct V { u32 t; int sec, min, hour, mday, mon, year, wday, yday; };
    static const V vecs[] = {
        {0U, 0,0,0,1,0,70,4,0},
        {1U, 1,0,0,1,0,70,4,0},
        {86399U, 59,59,23,1,0,70,4,0},
        {86400U, 0,0,0,2,0,70,5,1},
        {951782400U, 0,0,0,29,1,100,2,59},   // 2000-02-29 (leap day)
        {951868800U, 0,0,0,1,2,100,3,60},    // 2000-03-01
        {68256000U, 0,0,0,1,2,72,3,60},      // after 1972-02-29
        {1234567890U, 30,31,23,13,1,109,5,43},
        {1000000000U, 40,46,1,9,8,101,0,251},
        {2147483647U, 7,14,3,19,0,138,2,18}, // near 2038 limit
        {946684799U, 59,59,23,31,11,99,5,364}, // 1999-12-31 23:59:59
        {946684800U, 0,0,0,1,0,100,6,0},     // 2000-01-01
        {31536000U, 0,0,0,1,0,71,5,0},       // 1971-01-01
        {1583020800U, 0,0,0,1,2,120,0,60},   // 2020-03-01
        {1582934400U, 0,0,0,29,1,120,6,59},  // 2020-02-29 (leap day)
    };
    for (const V& v : vecs) {
        crt::TmFields f = crt::Gmtime(v.t);
        CHECK_EQ(f.sec, v.sec);
        CHECK_EQ(f.min, v.min);
        CHECK_EQ(f.hour, v.hour);
        CHECK_EQ(f.mday, v.mday);
        CHECK_EQ(f.mon, v.mon);
        CHECK_EQ(f.year, v.year);
        CHECK_EQ(f.wday, v.wday);
        CHECK_EQ(f.yday, v.yday);
    }
}

// gmtime vs libc oracle across a dense sweep, including every day boundary in a
// few interesting years and leap-day crossings.
TEST(CrtTime, GmtimeMatchesLibcSweep) {
    std::vector<u32> samples;
    // Every 6 hours across the first ~10 years (catches day/month/year rollovers).
    for (u32 t = 0; t < 315360000U; t += 21600U)
        samples.push_back(t);
    // Around several leap days and year boundaries.
    u32 anchors[] = {68256000U, 951782400U, 1582934400U, 946684800U,
                     1483228800U /*2017*/, 2082758399U /*2035*/};
    for (u32 a : anchors)
        for (int d = -2; d <= 2; ++d)
            samples.push_back(a + static_cast<u32>(d) * 86400U);

    for (u32 t : samples) {
        crt::TmFields got = crt::Gmtime(t);
        crt::TmFields exp = oracleGmtime(t);
        CHECK_EQ(got.sec, exp.sec);
        CHECK_EQ(got.min, exp.min);
        CHECK_EQ(got.hour, exp.hour);
        CHECK_EQ(got.mday, exp.mday);
        CHECK_EQ(got.mon, exp.mon);
        CHECK_EQ(got.year, exp.year);
        CHECK_EQ(got.wday, exp.wday);
        CHECK_EQ(got.yday, exp.yday);
    }
}

// ---------------------------------------------------------------------------
// MakeTimeUtc: inverse of gmtime, vs libc timegm-equivalent
// ---------------------------------------------------------------------------
TEST(CrtTime, MakeTimeUtcRoundTrip) {
    // Round-trip epoch -> tm -> epoch across the sweep.
    for (u32 t = 0; t < 315360000U; t += 86400U + 3661U) {
        crt::TmFields f = crt::Gmtime(t);
        f.isdst = 0;
        i32 back = crt::MakeTimeUtc(&f);
        CHECK_EQ(back, static_cast<i32>(t));
    }
}

TEST(CrtTime, MakeTimeUtcKnownValues) {
    CHECK_EQ(crt::MakeTimeUtc(1970, 1, 1, 0, 0, 0), 0);
    CHECK_EQ(crt::MakeTimeUtc(1970, 1, 1, 0, 0, 1), 1);
    CHECK_EQ(crt::MakeTimeUtc(2000, 1, 1, 0, 0, 0), 946684800);
    CHECK_EQ(crt::MakeTimeUtc(2000, 2, 29, 0, 0, 0), 951782400);
    CHECK_EQ(crt::MakeTimeUtc(2020, 2, 29, 0, 0, 0), 1582934400);
    CHECK_EQ(crt::MakeTimeUtc(2009, 2, 13, 23, 31, 30), 1234567890);
    // Field normalization: month 13 wraps to next year's January.
    CHECK_EQ(crt::MakeTimeUtc(1999, 13, 1, 0, 0, 0), crt::MakeTimeUtc(2000, 1, 1, 0, 0, 0));
    // Below-epoch year is rejected.
    CHECK_EQ(crt::MakeTimeUtc(1969, 12, 31, 0, 0, 0), -1);
}

TEST(CrtTime, RecoveredDstConstants) {
    crt::DstConstants c = crt::RecoveredDstConstants();
    CHECK_EQ(c.timezoneSecs, -3600);
    CHECK_EQ(c.daylight, 1);
    CHECK_EQ(c.dstBiasSecs, -3600);
}

// ---------------------------------------------------------------------------
// MBCS lead-byte classification
// ---------------------------------------------------------------------------
TEST(CrtTime, MbcsInactiveLocale) {
    // Reset to inactive C locale.
    crt::Mbcs() = crt::MbcsState{};
    const u8 s[] = {0x81, 0x40, 0x41, 0x00};
    CHECK_EQ(crt::IsLeadByte(0x81), 0);
    CHECK_EQ(crt::CharByteLength(s), 1);          // not active => single byte
    CHECK(crt::AdvanceChar(s) == s + 1);
    CHECK_EQ(crt::IsStringEnd(s), 0);
}

TEST(CrtTime, MbcsCp932LeadBytes) {
    crt::Mbcs().SetCp932LeadBytes();

    // Range checks: 0x81-0x9F and 0xE0-0xFC are lead bytes.
    CHECK(crt::IsLeadByte(0x81));
    CHECK(crt::IsLeadByte(0x9F));
    CHECK(crt::IsLeadByte(0xE0));
    CHECK(crt::IsLeadByte(0xFC));
    CHECK(!crt::IsLeadByte(0x80));
    CHECK(!crt::IsLeadByte(0xA0));
    CHECK(!crt::IsLeadByte(0xDF));
    CHECK(!crt::IsLeadByte(0xFD));
    CHECK(!crt::IsLeadByte('A'));
    CHECK(!crt::IsLeadByte(0x00));

    // _mbclen / advance on a full double-byte char.
    const u8 dbl[] = {0x82, 0xA0, 0x41, 0x00}; // SJIS 'あ' + 'A' + NUL
    CHECK_EQ(crt::CharByteLength(dbl), 2);
    CHECK(crt::AdvanceChar(dbl) == dbl + 2);
    CHECK_EQ(crt::CharByteLength(dbl + 2), 1);
    CHECK(crt::AdvanceChar(dbl + 2) == dbl + 3);

    // Truncated lead byte at end of string: IsStringEnd returns 2, advance is 1.
    const u8 trunc[] = {0x82, 0x00};
    CHECK_EQ(crt::IsStringEnd(trunc), 2);
    CHECK(crt::AdvanceChar(trunc) == trunc + 1);

    // Normal terminator.
    const u8 end[] = {0x00};
    CHECK_EQ(crt::IsStringEnd(end), 1);

    crt::Mbcs() = crt::MbcsState{}; // restore
}

// ---------------------------------------------------------------------------
// TimeBase against the mock clock
// ---------------------------------------------------------------------------
namespace {
int g_fireA = 0, g_fireB = 0, g_fireC = 0;
void procA() { ++g_fireA; }
void procB() { ++g_fireB; }
void procC() { ++g_fireC; }
} // namespace

TEST(CrtTime, TimeBaseRegisterAndQuery) {
    MockPlatform plat;
    crt::TimeBase tb(&plat);
    CHECK_EQ(tb.RegisterProc(procA, 1), 1);
    CHECK_EQ(tb.RegisterProc(procB, 2), 1);
    CHECK_EQ(tb.IsProcPaused(procA), 0);
    CHECK_EQ(tb.SetProcPaused(procA, 1), 1);
    CHECK_EQ(tb.IsProcPaused(procA), 1);
    CHECK_EQ(tb.SetProcPaused(procA, 0), 1);
    CHECK_EQ(tb.UnregisterProc(procB), 1);
    CHECK_EQ(tb.UnregisterProc(procB), 0); // already gone
    CHECK_EQ(tb.SetProcPaused(procC, 1), 0); // not registered
}

TEST(CrtTime, TimeBaseRegistryFull) {
    MockPlatform plat;
    crt::TimeBase tb(&plat);
    // Fill all 32 slots (RegisterProc keys on the first free slot, not identity).
    for (int i = 0; i < crt::TimeBase::kMaxProcs; ++i)
        CHECK_EQ(tb.RegisterProc(procA, 1), 1);
    // When full, the original's scan stops at the last slot and OVERWRITES it,
    // still returning 1 (the too-many error path is unreachable). Reproduce that.
    CHECK_EQ(tb.RegisterProc(procB, 7), 1);
}

TEST(CrtTime, TimeBaseTickIntervals) {
    g_fireA = g_fireB = g_fireC = 0;
    MockPlatform plat;
    crt::TimeBase tb(&plat);
    tb.RegisterProc(procA, 1); // every tick
    tb.RegisterProc(procB, 2); // every 2 ticks
    tb.RegisterProc(procC, 5); // every 5 ticks

    // Tick counter starts at 0; on tick k the counter value seen is k-1.
    // Fire when (counter % interval)==0, i.e. counter in {0,1,2,...}.
    for (int i = 0; i < 20; ++i)
        tb.Tick();

    // 20 ticks => counter values 0..19.
    // A (interval 1): fires on all 20.
    CHECK_EQ(g_fireA, 20);
    // B (interval 2): counter % 2 == 0 -> {0,2,4,...,18} = 10 fires.
    CHECK_EQ(g_fireB, 10);
    // C (interval 5): {0,5,10,15} = 4 fires.
    CHECK_EQ(g_fireC, 4);
    CHECK_EQ(tb.MasterTicks(), 20u);
    CHECK_EQ(tb.ThrottledTicks(), 20u);
}

TEST(CrtTime, TimeBasePausedProcDoesNotFire) {
    g_fireA = 0;
    MockPlatform plat;
    crt::TimeBase tb(&plat);
    tb.RegisterProc(procA, 1);
    tb.SetProcPaused(procA, 1);
    for (int i = 0; i < 10; ++i)
        tb.Tick();
    CHECK_EQ(g_fireA, 0);
    tb.SetProcPaused(procA, 0);
    for (int i = 0; i < 10; ++i)
        tb.Tick();
    CHECK_EQ(g_fireA, 10);
}

TEST(CrtTime, TimeBasePumpFromMockClock) {
    g_fireA = 0;
    MockPlatform plat;
    crt::TimeBase tb(&plat);
    tb.RegisterProc(procA, 1);
    CHECK_EQ(tb.StartTimer(10 /*ms*/, 1 /*periodic*/), 1);

    plat.advance(95); // 9 whole 10ms intervals
    tb.PumpFromClock();
    CHECK_EQ(g_fireA, 9);

    plat.advance(15); // crosses 100 and 110 -> 2 more
    tb.PumpFromClock();
    CHECK_EQ(g_fireA, 11);
}

TEST(CrtTime, TimeBaseStartTimerRejectsTooFast) {
    MockPlatform plat;
    crt::TimeBase tb(&plat);
    CHECK_EQ(tb.StartTimer(0, 1), 0);
    CHECK_EQ(tb.StartTimer(1, 1), 1);
}

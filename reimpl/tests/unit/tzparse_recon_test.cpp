// Golden-vector tests for the CRT POSIX-TZ parsing + DST helpers reconstructed
// in src/crt/tzparse_recon.{h,cpp}. Vectors follow the gilde.exe pseudocode
// (0x6070b0, 0x6070dc, 0x6071f4, 0x606f10, 0x606f2c, 0x60693c, 0x606a34).
#include "tests/framework/test.h"
#include "crt/tzparse_recon.h"

#include <cstring>

using namespace guild;
using namespace guild::crt;

// 0x6070b0 — ParseDecimal: consumes digit run, returns pointer past it.
TEST(GameTimeReconTzDecimal, Basic) {
    i32 out = -999;
    const char* s = "1234abc";
    const char* p = TimeParseDecimal(s, &out);
    CHECK_EQ(out, 1234);
    CHECK_EQ(*p, 'a');
}
TEST(GameTimeReconTzDecimal, NoDigits) {
    i32 out = -999;
    const char* s = "x9";
    const char* p = TimeParseDecimal(s, &out);
    CHECK_EQ(out, 0);   // accumulator never advanced
    CHECK_EQ(p, s);     // pointer unchanged
}

// 0x6070dc — ParseTzName: name then signed hh:mm:ss offset (seconds).
TEST(GameTimeReconTzName, NameAndPositiveOffset) {
    char name[129];
    i32 off = -1;
    // "EST5..." : name EST, +5h offset
    const char* p = TimeParseTzName("EST5EDT", name, &off);
    CHECK_EQ(std::strcmp(name, "EST"), 0);
    CHECK_EQ(off, 5 * 3600);
    CHECK_EQ(*p, 'E');   // stops at start of next name
}
TEST(GameTimeReconTzName, NegativeOffsetHhMmSs) {
    char name[129];
    i32 off = 0;
    const char* p = TimeParseTzName("CET-1:30:15", name, &off);
    CHECK_EQ(std::strcmp(name, "CET"), 0);
    CHECK_EQ(off, -(1 * 3600 + 30 * 60 + 15));
    CHECK_EQ(*p, '\0');
}
TEST(GameTimeReconTzName, NameOnlyNoOffset) {
    char name[129];
    i32 off = 1234;  // left unchanged when no digit follows
    TimeParseTzName("GMT", name, &off);
    CHECK_EQ(std::strcmp(name, "GMT"), 0);
    CHECK_EQ(off, 1234);
}
TEST(GameTimeReconTzName, LeadingColonStripped) {
    char name[129];
    i32 off = 0;
    TimeParseTzName(":UTC", name, &off);
    CHECK_EQ(std::strcmp(name, "UTC"), 0);
}

// 0x6071f4 — ParseTzRule: J / n / Mm.w.d forms with optional /time.
TEST(GameTimeReconTzRule, JulianRule) {
    TzRule r;
    const char* p = TimeParseTzRule("J60/2", &r);
    CHECK_EQ(r.type, 1);     // 'J'
    CHECK_EQ(r.jday, 60);
    CHECK_EQ(r.hour, 2);
    CHECK_EQ(r.min, 0);
    CHECK_EQ(r.sec, 0);
    CHECK_EQ(*p, '\0');
}
TEST(GameTimeReconTzRule, MRuleFull) {
    TzRule r;
    // M3.2.0 = month March (parsed 3 -> stored 2), week 2, weekday 0 (Sunday)
    const char* p = TimeParseTzRule("M3.2.0", &r);
    CHECK_EQ(r.type, 0);     // 'M'
    CHECK_EQ(r.month, 2);    // 3 - 1
    CHECK_EQ(r.week, 2);
    CHECK_EQ(r.wday, 0);
    CHECK_EQ(r.jday, 0);
    CHECK_EQ(r.hour, 2);     // default time 02:00:00
    CHECK_EQ(*p, '\0');
}
TEST(GameTimeReconTzRule, MRuleWithTime) {
    TzRule r;
    const char* p = TimeParseTzRule("M11.1.0/1:30:45", &r);
    CHECK_EQ(r.type, 0);
    CHECK_EQ(r.month, 10);   // 11 - 1
    CHECK_EQ(r.week, 1);
    CHECK_EQ(r.wday, 0);
    CHECK_EQ(r.hour, 1);
    CHECK_EQ(r.min, 30);
    CHECK_EQ(r.sec, 45);
    CHECK_EQ(*p, '\0');
}
TEST(GameTimeReconTzRule, NRuleNoPrefix) {
    TzRule r;
    const char* p = TimeParseTzRule("100", &r);
    CHECK_EQ(r.type, -1);    // no 'J'/'M' prefix
    CHECK_EQ(r.jday, 100);
    CHECK_EQ(*p, '\0');
}

// 0x606f10 / 0x606f2c — TZ init bit flags on dword_64AFD0.
TEST(GameTimeReconTzFlag, SetThenClear) {
    u32 word = 0;
    // set: returns previous bit0 (0), leaves bit0 set
    CHECK_EQ(TimeSetTzInitFlag(&word), 0);
    CHECK_EQ(word & 1u, 1u);
    // set again: returns previous bit0 (1)
    CHECK_EQ(TimeSetTzInitFlag(&word), 1);
    // clear: returns previous bit0 (1), leaves bit0 clear
    CHECK_EQ(TimeClearTzInitFlag(&word), 1);
    CHECK_EQ(word & 1u, 0u);
}
TEST(GameTimeReconTzFlag, PreservesUpperBytes) {
    u32 word = 0xABCDEF00u;  // bit0 = 0, upper bytes meaningful
    TimeSetTzInitFlag(&word);
    // low byte becomes (0x00 & 0xFC)|1 = 1; upper 3 bytes preserved.
    CHECK_EQ(word, 0xABCDEF01u);
    TimeClearTzInitFlag(&word);
    CHECK_EQ(word, 0xABCDEF00u);
}

// 0x60693c — DstTransitionDayOfYear for J and n rules (table-free paths).
TEST(GameTimeReconDst, JulianRuleDayMinusOne) {
    TzRule r;
    r.type = 1;     // J
    r.jday = 60;
    CHECK_EQ(TimeDstTransitionDayOfYear(&r, 124), 59);  // jday - 1
}
TEST(GameTimeReconDst, NRuleDayAsIs) {
    TzRule r;
    r.type = -1;    // n (no prefix) -> not 1, not 0 -> returns jday
    r.jday = 60;
    CHECK_EQ(TimeDstTransitionDayOfYear(&r, 124), 60);
}

// 0x60693c — M-rule: 2nd Sunday of March 2024. March 10, 2024 is the 2nd
// Sunday (the actual US DST-start date that year). tm_yday for 2024-03-10:
// Jan(31)+Feb(29 leap)+9 = 69. Cross-check the reconstructed value.
TEST(GameTimeReconDst, MRuleSecondSundayMarch2024) {
    TzRule r;
    const char* p = TimeParseTzRule("M3.2.0", &r);
    (void)p;
    int doy = TimeDstTransitionDayOfYear(&r, 2024 - 1900);  // yearMinus1900=124
    CHECK_EQ(doy, 69);  // 0-based yday of 2024-03-10
}

// 0x60693c — M-rule week 5 clamps to the last occurrence: last Sunday of
// October 2024 = Oct 27. tm_yday(2024-10-27) = 31+29+31+30+31+30+31+31+30+26
// = 300.
TEST(GameTimeReconDst, MRuleLastSundayOctober2024) {
    TzRule r;
    TimeParseTzRule("M10.5.0", &r);
    int doy = TimeDstTransitionDayOfYear(&r, 2024 - 1900);
    CHECK_EQ(doy, 300);
}

// 0x606a34 — IsAfterDstStart: northern-hemisphere US rules => end (Nov) is after
// start (Mar).
TEST(GameTimeReconDst, IsAfterDstStartNorthern) {
    TzRule start, end;
    TimeParseTzRule("M3.2.0", &start);   // DST starts March
    TimeParseTzRule("M11.1.0", &end);    // DST ends November
    CHECK(TimeIsAfterDstStart(&end, &start, 2024 - 1900) == true);
    // And the reverse ordering is false.
    CHECK(TimeIsAfterDstStart(&start, &end, 2024 - 1900) == false);
}

// 0x606a34 — both M-type, different months: short-circuits on the month
// comparison without resolving day-of-year.
TEST(GameTimeReconDst, IsAfterDstStartMonthShortCircuit) {
    TzRule start, end;
    TimeParseTzRule("M3.1.0", &start);   // month index 2
    TimeParseTzRule("M9.1.0", &end);     // month index 8 > 2 -> true
    CHECK(TimeIsAfterDstStart(&end, &start, 2024 - 1900) == true);
}

// ---------------------------------------------------------------------------
// Wave-11 hardening: malformed / boundary TZ inputs. The output buffer is sized
// exactly to the documented contract (name field is at most 128 bytes + NUL =
// 129); these drive the truncation cap, empty/garbage rules, and a long digit
// run (which must not trip signed-overflow UB). ASAN red-zones catch any write
// past nameOut[128].
// ---------------------------------------------------------------------------

// Empty TZ string: ParseTzName produces an empty name, offset untouched.
TEST(GameTimeReconTzHarden, EmptyString) {
    char name[129];
    i32 off = 777;
    const char* p = TimeParseTzName("", name, &off);
    CHECK_EQ(name[0], '\0');
    CHECK_EQ(off, 777); // no digit -> untouched
    CHECK_EQ(*p, '\0');
}

// Name longer than the 128-byte cap: must truncate to exactly 128 and NUL at
// [128]. A 200-char name array; verify no over-write and exact truncation.
TEST(GameTimeReconTzHarden, NameLengthCapBoundary) {
    char src[201];
    for (int i = 0; i < 200; ++i) src[i] = 'A';
    src[200] = '\0';
    char name[129];
    // sentinel after the array is owned by ASAN; just ensure we cap at 128.
    i32 off = 0;
    TimeParseTzName(src, name, &off);
    CHECK_EQ(std::strlen(name), 128u); // capped
    for (int i = 0; i < 128; ++i)
        CHECK_EQ(name[i], 'A');
    CHECK_EQ(name[128], '\0');
}

// Name with exactly 128 chars then a digit offset — boundary fits without cap.
TEST(GameTimeReconTzHarden, NameExactly128) {
    char src[160];
    for (int i = 0; i < 128; ++i) src[i] = 'B';
    src[128] = '3'; src[129] = '\0'; // a +3h offset follows
    char name[129];
    i32 off = -1;
    TimeParseTzName(src, name, &off);
    CHECK_EQ(std::strlen(name), 128u);
    CHECK_EQ(off, 3 * 3600);
}

// Garbage rule: no J/M/digit. type stays -1, offsets default (hour 2).
TEST(GameTimeReconTzHarden, GarbageRule) {
    TzRule r;
    const char* p = TimeParseTzRule("@@@", &r);
    CHECK_EQ(r.type, -1);
    CHECK_EQ(r.hour, 2); // default
    CHECK_EQ(r.min, 0);
    CHECK_EQ(r.sec, 0);
    CHECK_EQ(*p, '@');   // stopped at first non-digit
}

// Long digit run through ParseDecimal must not trip signed-overflow UB.
TEST(GameTimeReconTzHarden, ParseDecimalLongRun) {
    i32 out = -1;
    const char* s = "99999999999999999999x";
    const char* p = TimeParseDecimal(s, &out);
    CHECK_EQ(*p, 'x'); // consumed all digits, stopped at 'x'
    // value is the 2's-complement wrap; we only assert no crash/UB here.
    volatile i32 sink = out;
    (void)sink;
}

// Truncated M-rule "M3." — month parsed, then a dot with no week digits.
TEST(GameTimeReconTzHarden, TruncatedMRule) {
    TzRule r;
    const char* p = TimeParseTzRule("M3.", &r);
    CHECK_EQ(r.type, 0);
    CHECK_EQ(r.month, 2); // 3 - 1
    CHECK_EQ(*p, '\0');
}

// Golden tests for the CRT float-string scanner + tick-seed helpers.
//   gilde.exe 0x5d3fe0 ParseDoubleString (scanner), 0x1414ae0 GetTickSeed,
//   0x1422144 RandSetSeed.
#include "test.h"
#include "util/misc_recon4_parse.h"

#include <cstring>

using namespace guild::util;

static int scan(const char* s, ParseScanResult* r, const char** end) {
    const guild::u8* e = nullptr;
    int rc = ParseDoubleStringScan(reinterpret_cast<const guild::u8*>(s), r, &e);
    if (end) *end = reinterpret_cast<const char*>(e);
    return rc;
}

TEST(MiscRecon4, ParseSimpleInteger) {
    ParseScanResult r{};
    const char* end = nullptr;
    int rc = scan("123", &r, &end);
    CHECK_EQ(rc, 1);
    CHECK(!r.negative);
    CHECK_EQ(r.digitCount, 3);
    CHECK_EQ(std::strcmp(reinterpret_cast<const char*>(r.mantissa), "123"), 0);
    CHECK_EQ(r.decExponent, 0);
    CHECK_EQ(*end, '\0');
}

TEST(MiscRecon4, ParseLeadingWhitespaceAndSign) {
    ParseScanResult r{};
    int rc = scan("   -42", &r, nullptr);
    CHECK_EQ(rc, 1);
    CHECK(r.negative);
    CHECK_EQ(std::strcmp(reinterpret_cast<const char*>(r.mantissa), "42"), 0);
    CHECK_EQ(r.decExponent, 0);
}

TEST(MiscRecon4, ParseLeadingZerosSuppressed) {
    // v7 gate: leading zeros are NOT stored; "007" -> mantissa "7".
    ParseScanResult r{};
    int rc = scan("007", &r, nullptr);
    CHECK_EQ(rc, 1);
    CHECK_EQ(r.digitCount, 1);
    CHECK_EQ(std::strcmp(reinterpret_cast<const char*>(r.mantissa), "7"), 0);
}

TEST(MiscRecon4, ParseFraction) {
    // "12.5": v21 (fractional digit count) = 1 -> decExponent = 0 - 1 = -1.
    ParseScanResult r{};
    int rc = scan("12.5", &r, nullptr);
    CHECK_EQ(rc, 1);
    CHECK_EQ(std::strcmp(reinterpret_cast<const char*>(r.mantissa), "125"), 0);
    CHECK_EQ(r.decExponent, -1);
}

TEST(MiscRecon4, ParseTrailingZeroAdjust) {
    // "100": trailing zeros stripped from mantissa, exponent bumped.
    // mantissa stored "100" -> strip 2 zeros -> "1", decExp += 2.
    ParseScanResult r{};
    int rc = scan("100", &r, nullptr);
    CHECK_EQ(rc, 1);
    CHECK_EQ(std::strcmp(reinterpret_cast<const char*>(r.mantissa), "1"), 0);
    CHECK_EQ(r.digitCount, 1);
    CHECK_EQ(r.decExponent, 2);
}

TEST(MiscRecon4, ParseExponentPositive) {
    // "1e3": exp=3, no fraction -> decExp = 3.
    ParseScanResult r{};
    const char* end = nullptr;
    int rc = scan("1e3", &r, &end);
    CHECK_EQ(rc, 1);
    CHECK_EQ(r.decExponent, 3);
    CHECK_EQ(*end, '\0');
}

TEST(MiscRecon4, ParseExponentNegativeAndFraction) {
    // "2.50e-2": mantissa digits "250"->strip trailing 0 ->"25", v21=2 (frac),
    // exp=-2 -> decExp = -2 - 2 + 1(trailing strip) = -3.
    ParseScanResult r{};
    int rc = scan("2.50e-2", &r, nullptr);
    CHECK_EQ(rc, 1);
    CHECK_EQ(std::strcmp(reinterpret_cast<const char*>(r.mantissa), "25"), 0);
    CHECK_EQ(r.decExponent, -3);
}

TEST(MiscRecon4, ParseExponentRollbackNoDigits) {
    // "5e+": 'e' then '+' then no digits -> roll back to before 'e'; endptr at 'e'.
    ParseScanResult r{};
    const char* end = nullptr;
    int rc = scan("5e+", &r, &end);
    CHECK_EQ(rc, 1);
    CHECK_EQ(std::strcmp(reinterpret_cast<const char*>(r.mantissa), "5"), 0);
    CHECK_EQ(*end, 'e');
}

TEST(MiscRecon4, ParseZeroResult) {
    // "0" -> classification 0, empty mantissa.
    ParseScanResult r{};
    int rc = scan("0", &r, nullptr);
    CHECK_EQ(rc, 0);
    CHECK_EQ(r.digitCount, 0);
}

TEST(MiscRecon4, ParseOverflowUnderflowClassification) {
    ParseScanResult r{};
    // big exponent -> overflow (v16 > 308)
    int rc = scan("1e400", &r, nullptr);
    CHECK_EQ(rc, 3);
    // tiny exponent -> underflow (v16 < -308)
    ParseScanResult r2{};
    int rc2 = scan("1e-400", &r2, nullptr);
    CHECK_EQ(rc2, 2);
}

TEST(MiscRecon4, ParseNineteenDigitCap) {
    // 21 significant digits: only 19 stored, exponent gains (rawDigits-19).
    ParseScanResult r{};
    int rc = scan("123456789012345678901", &r, nullptr);  // 21 digits
    CHECK_EQ(rc, 1);
    CHECK_EQ(r.rawDigitCount, 21);
    // stored digits <= 19; decExponent should include the +2 from the cap.
    CHECK(r.digitCount <= 19);
    CHECK_EQ(r.decExponent, 2);  // (21-19) with no trailing zeros in the kept window
}

TEST(MiscRecon4, RandSetSeedStoresAndReturns) {
    CHECK_EQ(RandSetSeed(0x12345678), 0x12345678);
    CHECK_EQ(RandCurrentSeed(), 0x12345678);
    CHECK_EQ(RandSetSeed(-1), -1);
    CHECK_EQ(RandCurrentSeed(), -1);
}

static guild::i32 g_fakeTime = 0;
static guild::i32 fakeLocalTime() { return g_fakeTime; }

TEST(MiscRecon4, GetTickSeedSeedsFromClock) {
    TickSeedHooks h;
    h.getLocalTime = &fakeLocalTime;
    g_fakeTime = 0x0BADF00D;
    guild::i32 r = GetTickSeed(h);
    CHECK_EQ(r, 0x0BADF00D);
    CHECK_EQ(RandCurrentSeed(), 0x0BADF00D);
}

TEST(MiscRecon4, GetTickSeedInertDefault) {
    // No hook -> time source is 0 (deterministic headless).
    guild::i32 r = GetTickSeed();
    CHECK_EQ(r, 0);
    CHECK_EQ(RandCurrentSeed(), 0);
}

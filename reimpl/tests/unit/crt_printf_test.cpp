#include "crt/printf.h"
#include "test.h"

#include <cstdio>
#include <cstring>
#include <string>

// Golden oracle: every case compares guild::crt::Sprintf output to C's snprintf.
namespace {

// Compare our Sprintf against snprintf for a format with the given args.
#define ORACLE(fmt, ...)                                                       \
    do {                                                                       \
        char ours[256];                                                        \
        char gold[256];                                                        \
        int rn = ::guild::crt::Sprintf(ours, fmt, __VA_ARGS__);                \
        int gn = std::snprintf(gold, sizeof(gold), fmt, __VA_ARGS__);          \
        CHECK_EQ(std::string(ours), std::string(gold));                        \
        CHECK_EQ(rn, gn);                                                      \
    } while (0)

// Plain-text / no-arg format.
#define ORACLE0(fmt)                                                           \
    do {                                                                       \
        char ours[256];                                                        \
        char gold[256];                                                        \
        int rn = ::guild::crt::Sprintf(ours, fmt);                             \
        int gn = std::snprintf(gold, sizeof(gold), fmt);                       \
        CHECK_EQ(std::string(ours), std::string(gold));                        \
        CHECK_EQ(rn, gn);                                                      \
    } while (0)

} // namespace

TEST(CrtPrintf, DigitTable) {
    CHECK_EQ(std::string(::guild::crt::kDigitTable), std::string("0123456789abcdef"));
}

TEST(CrtPrintf, PlainAndPercent) {
    ORACLE0("hello world");
    ORACLE0("100%% done");
    // Empty format: both produce "" with length 0.
    char e_ours[8];
    int e_n = ::guild::crt::Sprintf(e_ours, "%s", "");
    CHECK_EQ(std::string(e_ours), std::string(""));
    CHECK_EQ(e_n, 0);
}

TEST(CrtPrintf, DecimalBasic) {
    ORACLE("%d", 0);
    ORACLE("%d", 42);
    ORACLE("%d", -42);
    ORACLE("%i", 12345);
    ORACLE("%d", -2147483647 - 1); // INT_MIN
    ORACLE("%d", 2147483647);      // INT_MAX
}

TEST(CrtPrintf, DecimalFlags) {
    ORACLE("%+d", 42);
    ORACLE("%+d", -42);
    ORACLE("% d", 42);
    ORACLE("% d", -42);
    ORACLE("%5d", 42);
    ORACLE("%-5d|", 42);
    ORACLE("%05d", 42);
    ORACLE("%05d", -42);
    ORACLE("%+05d", 42);
    ORACLE("%-+5d|", 42);
    ORACLE("%8.4d", 42);
    ORACLE("%.0d", 0);
    ORACLE("%.5d", 42);
}

TEST(CrtPrintf, Unsigned) {
    ORACLE("%u", 0u);
    ORACLE("%u", 4294967295u);
    ORACLE("%10u", 12345u);
    ORACLE("%-10u|", 12345u);
}

TEST(CrtPrintf, HexOctal) {
    ORACLE("%x", 0xdeadbeefu);
    ORACLE("%X", 0xdeadbeefu);
    ORACLE("%#x", 0xabcu);
    ORACLE("%#X", 0xabcu);
    ORACLE("%08x", 0x1fu);
    ORACLE("%#010x", 0x1fu);
    ORACLE("%o", 0755u);
    ORACLE("%#o", 0755u);
    ORACLE("%x", 0u);
    ORACLE("%#x", 0u);
    ORACLE("%.4x", 0xabu);
}

TEST(CrtPrintf, Char) {
    ORACLE("%c", 'A');
    ORACLE("%5c|", 'A');
    ORACLE("%-5c|", 'A');
    ORACLE("[%c]", '\t');
}

TEST(CrtPrintf, Strings) {
    ORACLE("%s", "hello");
    ORACLE("%10s|", "hi");
    ORACLE("%-10s|", "hi");
    ORACLE("%.3s", "hello");
    ORACLE("%5.3s|", "hello");
    ORACLE("%-5.3s|", "hello");
    ORACLE("%s", "");
}

TEST(CrtPrintf, StarWidthPrecision) {
    ORACLE("%*d", 6, 42);
    ORACLE("%-*d|", 6, 42);
    ORACLE("%.*d", 4, 42);
    ORACLE("%*.*d", 8, 4, 42);
    ORACLE("%*d", -6, 42); // negative width => left justify
}

TEST(CrtPrintf, LengthShort) {
    ORACLE("%hd", (short)-5);
    ORACLE("%hu", (unsigned short)65535);
    ORACLE("%hx", (unsigned short)0xbeef);
}

TEST(CrtPrintf, ReturnValueLength) {
    char buf[64];
    int n = ::guild::crt::Sprintf(buf, "%d-%s", 12345, "abc");
    CHECK_EQ(n, (int)std::strlen(buf));
    CHECK_EQ(std::string(buf), std::string("12345-abc"));
}

TEST(CrtPrintf, SnprintfTruncation) {
    char buf[8];
    int n = ::guild::crt::Snprintf(buf, sizeof(buf), "%d", 1234567890);
    // Full length reported, output NUL-terminated within cap.
    CHECK_EQ(n, 10);
    // Matches C99 snprintf: cap-1 chars written then NUL ("1234567").
    CHECK_EQ(std::string(buf), std::string("1234567"));
}

// ---------------------------------------------------------------------------
// Wave-11 hardening edge cases (ASAN/UBSAN). These drive the bounds of the
// Sink and the format-spec parser; the guard byte after each tiny buffer must
// survive (ASAN red-zone catches any over-write).
// ---------------------------------------------------------------------------

// snprintf into a 1-byte buffer: only the NUL fits, full length is reported.
TEST(CrtPrintf, CapOneOnlyNul) {
    char buf[1] = {'\x7f'};
    int n = ::guild::crt::Snprintf(buf, 1, "%s", "abcdef");
    CHECK_EQ(n, 6);
    CHECK_EQ(buf[0], '\0');
}

// snprintf with cap 0 must not touch the buffer at all but still count.
TEST(CrtPrintf, CapZeroNoWrite) {
    char buf[1] = {'\x55'};
    int n = ::guild::crt::Snprintf(buf, 0, "%d", 42);
    CHECK_EQ(n, 2);
    CHECK_EQ(buf[0], '\x55'); // untouched
}

// %s of a string whose only NUL is at the very end of a tight buffer, copied
// out via a too-small destination — exercises the truncation bound.
TEST(CrtPrintf, StringTruncatedExactBoundary) {
    char buf[4];
    int n = ::guild::crt::Snprintf(buf, sizeof(buf), "%s", "wxyz");
    CHECK_EQ(n, 4);
    CHECK_EQ(std::string(buf), std::string("wxy")); // cap-1 then NUL
}

// %s with precision over a NUL-less character array: precision bounds the read
// so no over-read past the array (ASAN would flag an over-read otherwise).
TEST(CrtPrintf, StringPrecisionNulless) {
    char src[4] = {'A', 'B', 'C', 'D'}; // deliberately NOT NUL-terminated
    char out[16];
    int n = ::guild::crt::Snprintf(out, sizeof(out), "%.4s", src);
    CHECK_EQ(n, 4);
    CHECK_EQ(std::string(out), std::string("ABCD"));
}

// Extreme width: large but bounded by the sink. Compare against the C oracle
// for a width that still fits in a 256-byte buffer.
TEST(CrtPrintf, WidthExtremeBounded) {
    char ours[256], gold[256];
    int rn = ::guild::crt::Sprintf(ours, "%200d", 7);
    int gn = std::snprintf(gold, sizeof(gold), "%200d", 7);
    CHECK_EQ(std::string(ours), std::string(gold));
    CHECK_EQ(rn, gn);
}

// Huge precision on a numeric: the zero-pad goes through the sink, not a fixed
// body buffer. Use a 4096-byte buffer and compare to the oracle.
TEST(CrtPrintf, PrecisionExtremeNumeric) {
    char ours[4096], gold[4096];
    int rn = ::guild::crt::Snprintf(ours, sizeof(ours), "%.300d", 5);
    int gn = std::snprintf(gold, sizeof(gold), "%.300d", 5);
    CHECK_EQ(std::string(ours), std::string(gold));
    CHECK_EQ(rn, gn);
}

// Huge precision on %p — previously widened a fixed 80-byte stack buffer and
// overflowed; now emitted as zero-pad. Just exercise it without over-running.
// (Output format of %p is platform-specific, so we only assert the length is
// at least the requested precision and that nothing crashes under ASAN.)
TEST(CrtPrintf, PointerHugePrecisionNoOverflow) {
    char buf[512];
    int dummy = 0;
    int n = ::guild::crt::Snprintf(buf, sizeof(buf), "%.200p", (void*)&dummy);
    CHECK(n >= 200);
}

// %p via %.*p with a large runtime precision — the previous OOB trigger.
TEST(CrtPrintf, PointerStarHugePrecision) {
    char buf[1024];
    int dummy = 0;
    int n = ::guild::crt::Snprintf(buf, sizeof(buf), "%.*p", 400, (void*)&dummy);
    CHECK(n >= 400);
}

// Many arguments interleaved — keeps the va_list walk honest.
TEST(CrtPrintf, ManyArgs) {
    char ours[256], gold[256];
    int rn = ::guild::crt::Sprintf(ours, "%d/%s/%x/%c/%u/%d/%s/%o",
                                   1, "two", 0x33u, '4', 5u, 6, "seven", 8u);
    int gn = std::snprintf(gold, sizeof(gold), "%d/%s/%x/%c/%u/%d/%s/%o",
                           1, "two", 0x33u, '4', 5u, 6, "seven", 8u);
    CHECK_EQ(std::string(ours), std::string(gold));
    CHECK_EQ(rn, gn);
}

// Trailing lone '%' at end of format: must stop cleanly (no read past NUL).
TEST(CrtPrintf, TrailingPercent) {
    char buf[16];
    int n = ::guild::crt::Sprintf(buf, "ab%");
    CHECK_EQ(std::string(buf), std::string("ab"));
    CHECK_EQ(n, 2);
}

// INT_MIN through %d (the safe-negate path).
TEST(CrtPrintf, IntMinDecimal) {
    ORACLE("%d", -2147483647 - 1);
    ORACLE("%i", -2147483647 - 1);
    ORACLE("%+d", -2147483647 - 1);
}

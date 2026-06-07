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

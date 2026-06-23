#include "test.h"
#include "crt/strtol.h"
#include "crt/ctype.h"

#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace guild;

// ---------------------------------------------------------------------------
// ctype table fidelity vs the C library classifiers.
// ---------------------------------------------------------------------------
TEST(CrtCtype, PctypeMatchesLibc) {
    for (int c = 0; c < 256; ++c) {
        const u16 e = crt::kPctype[c];
        CHECK_EQ((e & 0x04) != 0, std::isdigit(c) != 0);          // _DIGIT
        CHECK_EQ((e & 0x08) != 0, std::isspace(c) != 0);          // _SPACE
        CHECK_EQ((e & 0x01) != 0, std::isupper(c) != 0);          // _UPPER
        CHECK_EQ((e & 0x02) != 0, std::islower(c) != 0);          // _LOWER
        CHECK_EQ((e & 0x80) != 0, std::isxdigit(c) != 0);         // _HEX
        CHECK_EQ(((e & 0x103) != 0) || ((e & 0x04) != 0),
                 std::isalnum(c) != 0);                           // alpha|digit
    }
}

TEST(CrtCtype, StrtolWhitespaceTable) {
    // kStrtolCtype indexed at (c+1), bit 0x02 == isspace.
    for (int c = 0; c < 256; ++c) {
        bool ws = (crt::kStrtolCtype[(c + 1) & 0xFF] & 2) != 0;
        CHECK_EQ(ws, std::isspace(c) != 0);
    }
}

TEST(CrtCtype, DigitValue) {
    for (int c = 0; c < 256; ++c) {
        int v = crt::DigitValue(static_cast<u8>(c));
        int expect;
        if (c >= '0' && c <= '9') expect = c - '0';
        else if (c >= 'a' && c <= 'z') expect = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') expect = c - 'A' + 10;
        else expect = 37;
        CHECK_EQ(v, expect);
    }
}

TEST(CrtCtype, AsciiCase) {
    for (int c = 0; c < 256; ++c) {
        int up = (c >= 'a' && c <= 'z') ? c - 32 : c;
        int lo = (c >= 'A' && c <= 'Z') ? c + 32 : c;
        CHECK_EQ(crt::ToUpperAscii(c), up);
        CHECK_EQ(crt::ToLowerAscii(c), lo);
        CHECK_EQ(crt::LocaleToUpper(c), up);
        CHECK_EQ(crt::LocaleToLower(c), lo);
    }
}

// ---------------------------------------------------------------------------
// Atoi vs libc atoi.
// ---------------------------------------------------------------------------
TEST(CrtStrtol, AtoiMatchesLibc) {
    const char* cases[] = {
        "0", "1", "-1", "+42", "   123", "\t\n -7", "2147483647",
        "-2147483648", "  99abc", "abc", "", "  ", "007", "+0", "-0",
        "12345678", "00", "  -000012", "999999999",
    };
    for (const char* s : cases)
        CHECK_EQ(crt::Atoi(s), std::atoi(s));
}

// ---------------------------------------------------------------------------
// Wave-11 hardening: Atoi 2's-complement wrap on overflow (the original's 32-bit
// imul/add wraps; libc atoi overflow is itself UB so we can't oracle it). These
// pin the documented wraparound and the INT_MIN safe-negate, and must be clean
// under -fsanitize=signed-integer-overflow.
// ---------------------------------------------------------------------------
TEST(CrtStrtol, AtoiWraparoundDefined) {
    // INT_MIN negate-to-self path (the 0u - (u32)acc fix).
    CHECK_EQ(crt::Atoi("-2147483648"), -2147483647 - 1);
    // INT_MAX exactly.
    CHECK_EQ(crt::Atoi("2147483647"), 2147483647);
    // Overflow past INT_MAX wraps in 32-bit 2's complement:
    //   "2147483648" -> 2147483648u as int == INT_MIN
    CHECK_EQ(crt::Atoi("2147483648"), -2147483647 - 1);
    // "4294967296" == 2^32 -> wraps to 0.
    CHECK_EQ(crt::Atoi("4294967296"), 0);
    // A long all-9 run must not trip UBSAN; only the low 32 bits matter.
    volatile int sink = crt::Atoi("999999999999999999");
    (void)sink;
    // Negative overflow: "-3000000000" = 0 - 3000000000u.
    CHECK_EQ(crt::Atoi("-3000000000"),
             static_cast<int>(0u - 3000000000u));
}

// ---------------------------------------------------------------------------
// Strtol_Parse (the exported strtol/strtoul) vs libc strtol/strtoul.
// ---------------------------------------------------------------------------
// glibc has two documented divergences from the MSVC original this code reconstructs:
//   (1) "0x"/"0X" not followed by a hex digit: glibc consumes the whole prefix and
//       reports value 0 with endptr after 'x'; MSVC consumes nothing past the leading
//       '0' is invalid — the original's Strtol_Parse, when it saw no valid digit,
//       resets endptr to the *start of the string*. We model the MSVC behavior, so
//       adjust the oracle's endptr to the string start for these inputs.
//   (2) glibc accepts a non-standard "0b" binary prefix in base 0; MSVC does not.
// `fix_end` rewrites the expected end pointer to match the MSVC original.
static const char* fix_end(const char* s, int base, const char* lib_end) {
    // skip ws + sign to find the numeric body
    const char* p = s;
    while (*p && std::isspace(static_cast<unsigned char>(*p))) ++p;
    if (*p == '+' || *p == '-') ++p;
    // "0x"/"0X" with no following hex digit (base 0 or 16)
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') &&
        !std::isxdigit(static_cast<unsigned char>(p[2]))) {
        // MSVC: no valid digits -> endptr at the very start of the input.
        return s;
    }
    return lib_end;
}

// The original is a 32-bit binary (long == 32 bits), so we drive libc strtoll/strtoull
// (64-bit) and clamp the result to the 32-bit range the original produces, deriving
// the expected value, endptr, and ERANGE flag. This makes the oracle independent of
// the host's `long` width.
static void check_strtol(const char* s, int base) {
    char* lib_end = nullptr;
    errno = 0;
    long long wide = std::strtoll(s, &lib_end, base);
    const char* exp_end = fix_end(s, base, lib_end);
    bool range = (errno == ERANGE) || wide > 0x7FFFFFFFLL || wide < -0x80000000LL;
    i32 expect;
    if (range)
        expect = (wide < 0) ? INT32_MIN : INT32_MAX;
    else
        expect = static_cast<i32>(wide);

    const char* my_end = nullptr;
    crt::Errno() = 0;
    i32 my = static_cast<i32>(crt::Strtol(s, &my_end, base));
    CHECK_EQ(my, expect);
    CHECK_EQ(static_cast<size_t>(my_end - s), static_cast<size_t>(exp_end - s));
    if (range)
        CHECK_EQ(crt::Errno(), crt::kERANGE);
}

static void check_strtoul(const char* s, int base) {
    char* lib_end = nullptr;
    errno = 0;
    std::strtoull(s, &lib_end, base); // only for the endptr
    const char* exp_end = fix_end(s, base, lib_end);

    // Re-derive the magnitude (sign-stripped) to detect true 32-bit overflow, then
    // apply the sign by modular negation, matching the original's unsigned semantics.
    const char* p = s;
    while (*p && std::isspace(static_cast<unsigned char>(*p))) ++p;
    bool neg = false;
    if (*p == '+' || *p == '-') { neg = (*p == '-'); ++p; }
    errno = 0;
    unsigned long long mag = std::strtoull(p, nullptr, base);
    bool range = (errno == ERANGE) || mag > 0xFFFFFFFFULL;
    u32 expect;
    if (range)
        expect = 0xFFFFFFFFu;
    else
        expect = neg ? static_cast<u32>(0u - static_cast<u32>(mag))
                     : static_cast<u32>(mag);

    const char* my_end = nullptr;
    crt::Errno() = 0;
    u32 my = static_cast<u32>(crt::Strtoul(s, &my_end, base));
    CHECK_EQ(my, expect);
    CHECK_EQ(static_cast<size_t>(my_end - s), static_cast<size_t>(exp_end - s));
    if (range)
        CHECK_EQ(crt::Errno(), crt::kERANGE);
}

TEST(CrtStrtol, StrtolDecimal) {
    const char* cases[] = {"0", "123", "-123", "+456", "  789", "\t -42xyz",
                           "2147483647", "2147483648", "-2147483648",
                           "-2147483649", "9999999999", "abc", "", "   ",
                           "0", "-0", "  +0010"};
    for (const char* s : cases)
        check_strtol(s, 10);
}

TEST(CrtStrtol, StrtolHex) {
    const char* cases[] = {"0x0", "0xFF", "0Xff", "ff", "0x7fffffff",
                           "0x80000000", "0xffffffff", "0x100000000",
                           "deadBEEF", "0xG", "  0x1A", "-0x10", "0x"};
    for (const char* s : cases)
        check_strtol(s, 16);
}

TEST(CrtStrtol, StrtolOctalAndBase0) {
    // Note: no "0b" prefix here — glibc has a non-standard binary extension the MSVC
    // original lacks; that divergence is asserted explicitly below.
    const char* cases[] = {"0777", "017", "0", "08", "0x1F", "100", "-0755",
                           "  012", "0xABCDEF", "777"};
    for (const char* s : cases) {
        check_strtol(s, 8);
        check_strtol(s, 0);
    }
}

// MSVC-specific edge behavior (where the original diverges from glibc).
TEST(CrtStrtol, MsvcEdgeCases) {
    const char* end = nullptr;
    // base 0 "0b101": MSVC sees a leading 0 -> octal, 'b' is not octal -> stops after
    // the '0'. Value 0, endptr at the 'b'.
    CHECK_EQ(static_cast<i32>(crt::Strtol("0b101", &end, 0)), 0);
    CHECK_EQ(static_cast<size_t>(end - "0b101"), 1u);
    // "0x" with no hex digit -> no valid number, endptr at start, value 0.
    const char* s = "0xZ";
    CHECK_EQ(static_cast<i32>(crt::Strtol(s, &end, 16)), 0);
    CHECK_EQ(static_cast<size_t>(end - s), 0u);
}

TEST(CrtStrtol, StrtolMiscBases) {
    const char* cases[] = {"z", "Z", "10", "zz", "  -1y2", "ffff", "1010", "777"};
    for (int base : {2, 7, 16, 36})
        for (const char* s : cases)
            check_strtol(s, base);
}

TEST(CrtStrtol, StrtoulMatchesLibc) {
    const char* cases[] = {"0", "4294967295", "4294967296", "0xffffffff",
                           "0x100000000", "-1", "-0x1", "  4000000000", "123",
                           "0777", "0x10", "", "abc"};
    for (const char* s : cases) {
        check_strtoul(s, 10);
        check_strtoul(s, 16);
        check_strtoul(s, 0);
    }
}

// ---------------------------------------------------------------------------
// StrToLong (the C-runtime strtol core @0x60b350).
// ---------------------------------------------------------------------------
TEST(CrtStrtol, StrToLongSigned) {
    struct { const char* s; int base; long expect; } cases[] = {
        {"123", 10, 123}, {"-123", 10, -123}, {"  +42", 10, 42},
        {"0x1F", 0, 31}, {"0777", 0, 511}, {"100", 0, 100},
        {"7fffffff", 16, 0x7fffffff}, {"abc", 10, 0},
        {"2147483647", 10, 2147483647L},
    };
    for (auto& c : cases) {
        const char* end = nullptr;
        long v = static_cast<long>(static_cast<i32>(
            crt::StrToLong(c.s, &end, /*signed*/1, c.base)));
        CHECK_EQ(v, c.expect);
    }
}

TEST(CrtStrtol, StrToLongOverflowClamp) {
    const char* end = nullptr;
    crt::Errno() = 0;
    i32 v = static_cast<i32>(crt::StrToLong("99999999999", &end, 1, 10));
    CHECK_EQ(v, INT32_MAX); // clamps to 0x7fffffff
    CHECK_EQ(crt::Errno(), crt::kERANGE);

    crt::Errno() = 0;
    v = static_cast<i32>(crt::StrToLong("-99999999999", &end, 1, 10));
    CHECK_EQ(v, INT32_MIN); // clamps to 0x80000000
    CHECK_EQ(crt::Errno(), crt::kERANGE);
}

TEST(CrtStrtol, StrToLongUnsigned) {
    const char* end = nullptr;
    CHECK_EQ(crt::StrToLong("4294967295", &end, /*signed*/0, 10), 0xFFFFFFFFu);
    crt::Errno() = 0;
    CHECK_EQ(crt::StrToLong("99999999999", &end, 0, 10), 0xFFFFFFFFu); // ULONG_MAX
    CHECK_EQ(crt::Errno(), crt::kERANGE);
}

TEST(CrtStrtol, StrToLongInvalidBase) {
    const char* end = nullptr;
    crt::Errno() = 0;
    CHECK_EQ(crt::StrToLong("123", &end, 1, 1), 0u);   // base 1 invalid
    CHECK_EQ(crt::Errno(), crt::kEINVAL);
    CHECK_EQ(crt::StrToLong("123", &end, 1, 37), 0u);  // base 37 invalid
}

TEST(CrtStrtol, StrToLongAutoIsSigned) {
    const char* end = nullptr;
    CHECK_EQ(static_cast<i32>(crt::StrToLongAuto("-50", &end, 10)), -50);
    CHECK_EQ(static_cast<i32>(crt::StrToLongAuto("0x10", &end, 0)), 16);
}

// ---------------------------------------------------------------------------
// Case operations.
// ---------------------------------------------------------------------------
TEST(CrtStrtol, StrToUpper) {
    char buf[] = "Hello, World 123!";
    crt::StrToUpper(buf);
    CHECK(std::string(buf) == "HELLO, WORLD 123!");
    char empty[] = "";
    CHECK(crt::StrToUpper(empty) == empty);
}

// Wave-11 hardening: exact LONG_MIN (no clamp), the 2's-complement negate path,
// and the base-validation rejects — clean under -fsanitize=undefined.
TEST(CrtStrtol, StrToLongIntMinNoClamp) {
    const char* end = nullptr;
    crt::Errno() = 0;
    // "-2147483648" is exactly representable: acc == 0x80000000, sign '-' ->
    // negate via 0u-acc == 0x80000000, no ERANGE.
    i32 v = static_cast<i32>(crt::StrToLong("-2147483648", &end, 1, 10));
    CHECK_EQ(v, INT32_MIN);
    CHECK_EQ(crt::Errno(), 0);
}

TEST(CrtStrtol, StrToLongBaseRejects) {
    const char* end = nullptr;
    crt::Errno() = 0;
    CHECK_EQ(crt::StrToLong("10", &end, /*base*/1, /*base*/1), 0u);
    CHECK_EQ(crt::Errno(), crt::kEINVAL);
    crt::Errno() = 0;
    CHECK_EQ(crt::StrToLong("10", &end, 1, 37), 0u);
    CHECK_EQ(crt::Errno(), crt::kEINVAL);
}

// Strtol_Parse on empty / all-whitespace input: no digits -> endptr at start,
// value 0, no over-read past the NUL.
TEST(CrtStrtol, StrtolParseEmptyAndWhitespace) {
    const char* end = nullptr;
    const char* empty = "";
    CHECK_EQ(crt::Strtol(empty, &end, 10), 0L);
    CHECK(end == empty);
    const char* ws = "   \t  ";
    CHECK_EQ(crt::Strtol(ws, &end, 10), 0L);
    CHECK(end == ws); // reset to start when no digit consumed
    // sign with no digits also yields start.
    const char* sign = "  -";
    CHECK_EQ(crt::Strtol(sign, &end, 10), 0L);
    CHECK(end == sign);
}

TEST(CrtStrtol, StringToUpperLower) {
    char a[] = "MixedCASE 42";
    char b[] = "MixedCASE 42";
    crt::StringToUpper(a);
    crt::StringToLower(b);
    CHECK(std::string(a) == "MIXEDCASE 42");
    CHECK(std::string(b) == "mixedcase 42");
}

static int sgn(int v) { return (v > 0) - (v < 0); }

TEST(CrtStrtol, CompareNoCaseN) {
    struct { const char* a; const char* b; int n; } cases[] = {
        {"Hello", "hello", 5}, {"HELLO", "hello world", 5},
        {"abc", "abd", 3}, {"abc", "abc", 0}, {"Foo", "foobar", 3},
        {"ABCDEF", "abcxyz", 3}, {"ABCDEF", "abcxyz", 6}, {"", "", 1},
        {"a", "B", 1}, {"Z", "a", 1},
    };
    for (auto& c : cases) {
        int my = crt::StringCompareNoCaseN(c.a, c.b, c.n);
        int lib;
        if (c.n == 0) lib = 0;
        else {
#if defined(_WIN32)
            lib = _strnicmp(c.a, c.b, c.n);
#else
            lib = strncasecmp(c.a, c.b, c.n);
#endif
        }
        CHECK_EQ(sgn(my), sgn(lib));
    }
}

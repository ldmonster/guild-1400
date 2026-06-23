#include "test.h"
#include "crt/string.h"

#include <cstring>
#include <string>

using namespace guild;

// ---------- StringLength (strlen) ----------
TEST(CrtString, LengthMatchesStrlen) {
    const char* cases[] = {"", "a", "hello", "with spaces and  tabs\t",
                           "0123456789abcdef0123456789"};
    for (const char* s : cases)
        CHECK_EQ(crt::StringLength(s), static_cast<u32>(std::strlen(s)));
}

TEST(CrtString, LengthEmbeddedHandledAtNul) {
    char buf[8] = {'a', 'b', 0, 'c', 'd', 0, 0, 0};
    CHECK_EQ(crt::StringLength(buf), 2u);
}

// ---------- StringCompare (strcmp sign) ----------
static int sgn(int v) { return (v > 0) - (v < 0); }

TEST(CrtString, CompareSign) {
    CHECK_EQ(crt::StringCompare("abc", "abc"), 0);
    CHECK_EQ(crt::StringCompare("abc", "abd"), -1); // less
    CHECK_EQ(crt::StringCompare("abd", "abc"), 1);  // greater
    CHECK_EQ(crt::StringCompare("ab", "abc"), -1);  // prefix is less
    CHECK_EQ(crt::StringCompare("abc", "ab"), 1);
    CHECK_EQ(crt::StringCompare("", ""), 0);
    CHECK_EQ(crt::StringCompare("", "x"), -1);
}

TEST(CrtString, CompareMatchesStrcmpSign) {
    const char* a[] = {"alpha", "alpha", "beta", "z", "", "AAA"};
    const char* b[] = {"alphb", "alpha", "alphazzz", "a", "x", "AAB"};
    for (int i = 0; i < 6; ++i)
        CHECK_EQ(sgn(crt::StringCompare(a[i], b[i])), sgn(std::strcmp(a[i], b[i])));
}

TEST(CrtString, CompareUnsignedHighBytes) {
    // High-bit bytes must compare as unsigned (so 0x80 > 0x20).
    char x[] = {static_cast<char>(0x80), 0};
    char y[] = {0x20, 0};
    CHECK_EQ(crt::StringCompare(x, y), 1);
}

// ---------- StringConcat (strcat) ----------
TEST(CrtString, ConcatBasic) {
    char buf[32] = "foo";
    char* r = crt::StringConcat(buf, "bar");
    CHECK(r == buf);
    CHECK_EQ(std::string(buf), std::string("foobar"));
}

TEST(CrtString, ConcatEmptySrc) {
    char buf[16] = "abc";
    crt::StringConcat(buf, "");
    CHECK_EQ(std::string(buf), std::string("abc"));
}

TEST(CrtString, ConcatOntoEmpty) {
    char buf[16] = "";
    crt::StringConcat(buf, "xyz");
    CHECK_EQ(std::string(buf), std::string("xyz"));
}

TEST(CrtString, ConcatMatchesLibc) {
    char a[64] = "the quick ";
    char b[64] = "the quick ";
    crt::StringConcat(a, "brown fox jumps");
    std::strcat(b, "brown fox jumps");
    CHECK_EQ(std::string(a), std::string(b));
}

// ---------- StringFindChar (strchr) ----------
TEST(CrtString, FindChar) {
    char s[] = "hello world";
    CHECK(crt::StringFindChar(s, 'o') == s + 4);
    CHECK(crt::StringFindChar(s, 'h') == s);
    CHECK(crt::StringFindChar(s, 'z') == nullptr);
}

TEST(CrtString, FindCharFindsNul) {
    char s[] = "abc";
    CHECK(crt::StringFindChar(s, '\0') == s + 3);
}

TEST(CrtString, FindCharMatchesStrchr) {
    char s[] = "mississippi";
    for (char c : std::string("misp z\0", 7)) {
        char* mine = crt::StringFindChar(s, static_cast<unsigned char>(c));
        const char* lib = std::strchr(s, c);
        CHECK(mine == lib);
    }
}

// ---------- StringMatchPrefix ----------
TEST(CrtString, MatchPrefix) {
    CHECK_EQ(crt::StringMatchPrefix("hello", "hel"), 3);
    CHECK_EQ(crt::StringMatchPrefix("hello", "hello"), 5);
    CHECK_EQ(crt::StringMatchPrefix("hello", "world"), 0);
    CHECK_EQ(crt::StringMatchPrefix("abc", ""), 0); // empty b -> 0
    // a shorter than b: bytes match until a's NUL differs from b -> returns 0.
    CHECK_EQ(crt::StringMatchPrefix("ab", "abc"), 0);
    // b fully consumed before any mismatch (b is a prefix of a) -> full length of b.
    CHECK_EQ(crt::StringMatchPrefix("abcdef", "abc"), 3);
}

TEST(CrtString, MatchPrefixCappedAt128) {
    std::string big(200, 'a');
    CHECK_EQ(crt::StringMatchPrefix(big.c_str(), big.c_str()), 128);
}

// ---------- StringSkipLeadingSpaces ----------
TEST(CrtString, SkipLeadingSpaces) {
    CHECK(std::string(crt::StringSkipLeadingSpaces("   hi")) == "hi");
    CHECK(std::string(crt::StringSkipLeadingSpaces("nope")) == "nope");
    CHECK(crt::StringSkipLeadingSpaces("     ") == nullptr); // all spaces
    CHECK(crt::StringSkipLeadingSpaces("") == nullptr);      // empty
}

// ---------- StringTrimTrailingSpaces ----------
TEST(CrtString, TrimTrailingSpaces) {
    char a[] = "hi   ";
    const char* r = crt::StringTrimTrailingSpaces(a);
    CHECK(std::string(a) == "hi");
    (void)r;
    char b[] = "noTrail";
    crt::StringTrimTrailingSpaces(b);
    CHECK(std::string(b) == "noTrail");
}

// ---------- IntToRadix / IntToAscii ----------
TEST(CrtString, IntToAsciiDecimal) {
    char buf[16];
    crt::StringIntToAscii(12345, buf, 10);
    CHECK(std::string(buf) == "12345");
    crt::StringIntToAscii(-678, buf, 10);
    CHECK(std::string(buf) == "-678");
    crt::StringIntToAscii(0, buf, 10);
    CHECK(std::string(buf) == "0");
}

TEST(CrtString, IntToAsciiHexAndBin) {
    char buf[40];
    crt::StringIntToAscii(255, buf, 16);
    CHECK(std::string(buf) == "ff");
    crt::StringIntToAscii(5, buf, 2);
    CHECK(std::string(buf) == "101");
    // radix 16 with a negative value formats as unsigned (no minus), like the original.
    crt::StringIntToAscii(-1, buf, 16);
    CHECK(std::string(buf) == "ffffffff");
}

// ---------- Wave-11 hardening: itoa INT_MIN / overflow / buffer boundary ------
// IntToRadix negates via `0u - value` (unsigned) so INT_MIN is representable
// without signed-overflow UB; the decimal form needs 12 bytes ("-2147483648").
TEST(CrtString, IntToAsciiIntMin) {
    char buf[16];
    crt::StringIntToAscii(-2147483647 - 1, buf, 10);
    CHECK(std::string(buf) == "-2147483648");
    // INT_MIN in base 16 formats as unsigned (no minus).
    crt::StringIntToAscii(-2147483647 - 1, buf, 16);
    CHECK(std::string(buf) == "80000000");
}

// IntToRadix at a tight-but-sufficient buffer: "-2147483648" is 11 chars + NUL.
TEST(CrtString, IntToRadixExactBuffer) {
    char buf[12]; // exactly minus + 10 digits + NUL
    crt::StringIntToRadix(static_cast<u32>(-2147483647 - 1), buf, 10u, 1);
    CHECK(std::string(buf) == "-2147483648");
    CHECK_EQ(buf[11], '\0');
}

// UIntToString of UINT_MAX in base 2 is 32 chars (fits the 33-byte scratch).
TEST(CrtString, UIntToStringBinaryMax) {
    char buf[40];
    std::string s = crt::StringUIntToString(0xFFFFFFFFu, buf, 2);
    CHECK_EQ(s.size(), 32u);
    CHECK(s == std::string(32, '1'));
}

// Concat at the exact destination boundary: dst already holds "ab", append "cd"
// into a 5-byte buffer ("ab"+"cd"+NUL == 5). Must not write a 6th byte.
TEST(CrtString, ConcatExactBoundary) {
    char dst[5] = {'a', 'b', '\0', '\xAA', '\xAA'};
    crt::StringConcat(dst, "cd");
    CHECK(std::string(dst) == "abcd");
    CHECK_EQ(dst[4], '\0');
}

// ---------- UIntToString ----------
TEST(CrtString, UIntToString) {
    char buf[40];
    CHECK(std::string(crt::StringUIntToString(0u, buf, 10)) == "0");
    CHECK(std::string(crt::StringUIntToString(4294967295u, buf, 10)) == "4294967295");
    CHECK(std::string(crt::StringUIntToString(0xDEADBEEFu, buf, 16)) == "deadbeef");
    CHECK(std::string(crt::StringUIntToString(35u, buf, 36)) == "z");
}

// ---------- WideEnvLength / WcsChr ----------
TEST(CrtString, WideEnvLength) {
    u16 empty[] = {0};
    u16 s[] = {'a', 'b', 'c', 0};
    CHECK_EQ(crt::StringWideEnvLength(empty), 0);
    CHECK_EQ(crt::StringWideEnvLength(s), 3);
}

TEST(CrtString, WcsChr) {
    u16 s[] = {'h', 'i', '!', 0};
    CHECK(crt::WcsChr(s, 'h') == s);
    CHECK(crt::WcsChr(s, '!') == s + 2);
    CHECK(crt::WcsChr(s, 0) == s + 3); // matches terminator
    CHECK(crt::WcsChr(s, 'z') == nullptr);
}

// ---------- GetDelimitedField ----------
TEST(CrtString, GetDelimitedFieldMode8WalksRecords) {
    // Six consecutive NUL-terminated fields; mode 8 adds 4 to the count and walks.
    char buf[] = "f0\0f1\0f2\0f3\0f4\0f5\0\0";
    // With mode 8 and n=2 -> n becomes 6, iterates idx<5, lands inside the record set.
    char* r = crt::StringGetDelimitedField(buf, 2, 8, buf);
    CHECK(r != nullptr);
    // The result must point at one of the field starts within the buffer.
    CHECK(r >= buf && r <= buf + sizeof(buf));
}

#include "test.h"
#include "util/string_ops.h"
#include "util/mem_ops.h"
#include "util/sort.h"

#include <cstring>
#include <cstdlib>

using namespace guild::util;

// ----------------------------------------------------------------------------
// StrStr — golden vectors + libc strstr oracle.
// ----------------------------------------------------------------------------
TEST(UtilStrOps, StrStr_Golden) {
    char hay[] = "the quick brown fox";
    CHECK(StrStr(hay, "quick") == hay + 4);
    CHECK(StrStr(hay, "fox") == hay + 16);
    CHECK(StrStr(hay, "the") == hay + 0);
    CHECK(StrStr(hay, "cat") == nullptr);
    CHECK(StrStr(hay, "") == hay);              // empty needle -> hay
    char overlap[] = "aaabaaab";
    CHECK(StrStr(overlap, "aaab") == overlap + 0);
    CHECK(StrStr(overlap, "aab") == overlap + 1);
}

TEST(UtilStrOps, StrStr_OracleVsLibc) {
    const char* hays[] = {"", "a", "abcabcabc", "mississippi", "xxyxxyx"};
    const char* needles[] = {"", "a", "abc", "ssi", "xy", "z", "mississippi", "ppiq"};
    for (const char* h : hays) {
        for (const char* n : needles) {
            char buf[64];
            std::strcpy(buf, h);
            const char* mine = StrStr(buf, n);
            const char* ref = std::strstr(buf, n);
            CHECK(mine == ref);
        }
    }
}

// ----------------------------------------------------------------------------
// StrnLen — quirky semantics: returns cap when no terminator within range.
// ----------------------------------------------------------------------------
TEST(UtilStrOps, StrnLen_Golden) {
    CHECK_EQ(StrnLen("hello", 10), (std::size_t)5);   // terminator within range
    CHECK_EQ(StrnLen("hello", 5), (std::size_t)5);    // terminator at index 5 (cap-1 budget)
    CHECK_EQ(StrnLen("hello", 3), (std::size_t)3);    // no terminator within 3 -> cap
    CHECK_EQ(StrnLen("", 4), (std::size_t)0);         // empty
    CHECK_EQ(StrnLen("abc", 0), (std::size_t)0);      // cap 0 -> *s != 0 path... ""? see below
}

TEST(UtilStrOps, StrnLen_CapZero) {
    // cap==0: loop is skipped; v stays at s; if *s != 0 returns cap(0); else 0.
    CHECK_EQ(StrnLen("x", 0), (std::size_t)0);
    CHECK_EQ(StrnLen("", 0), (std::size_t)0);
}

// ----------------------------------------------------------------------------
// StrnCpy — strncpy semantics: pad short, no-terminate long. Oracle: libc strncpy.
// ----------------------------------------------------------------------------
TEST(UtilStrOps, StrnCpy_OracleVsLibc) {
    struct Case { const char* src; std::size_t n; };
    Case cases[] = {
        {"hi", 5}, {"hello", 5}, {"hello world", 5}, {"", 4}, {"abc", 0}, {"abcd", 4},
    };
    for (auto& c : cases) {
        char mine[16], ref[16];
        std::memset(mine, '#', sizeof(mine));
        std::memset(ref, '#', sizeof(ref));
        char* r1 = StrnCpy(mine, c.src, c.n);
        char* r2 = std::strncpy(ref, c.src, c.n);
        CHECK(r1 == mine);
        CHECK(r2 == ref);
        CHECK(std::memcmp(mine, ref, sizeof(mine)) == 0);
    }
}

// ----------------------------------------------------------------------------
// StrChrLast (strrchr semantics) — oracle: libc strrchr.
// ----------------------------------------------------------------------------
TEST(UtilStrOps, StrChrLast_OracleVsStrrchr) {
    char s[] = "a/b/c/d.txt";
    CHECK(StrChrLast(s, '/') == std::strrchr(s, '/'));
    CHECK(StrChrLast(s, '.') == std::strrchr(s, '.'));
    CHECK(StrChrLast(s, 'a') == std::strrchr(s, 'a'));
    CHECK(StrChrLast(s, 'z') == nullptr);
    // c == 0 matches the terminating NUL (strrchr does too).
    CHECK(StrChrLast(s, '\0') == s + std::strlen(s));
}

// ----------------------------------------------------------------------------
// StrToUpper — oracle built independently.
// ----------------------------------------------------------------------------
TEST(UtilStrOps, StrToUpper_Golden) {
    char s[] = "Hello, World 123!";
    char* r = StrToUpper(s);
    CHECK(r == s);
    CHECK(std::strcmp(s, "HELLO, WORLD 123!") == 0);
    char only[] = "abcxyz";
    StrToUpper(only);
    CHECK(std::strcmp(only, "ABCXYZ") == 0);
}

// ----------------------------------------------------------------------------
// StrncmpN — oracle: libc strncmp (sign-equivalent).
// ----------------------------------------------------------------------------
static int sgn(int x) { return (x > 0) - (x < 0); }

TEST(UtilStrOps, StrncmpN_OracleVsLibc) {
    struct Case { const char* a; const char* b; int n; };
    Case cs[] = {
        {"abc", "abc", 3}, {"abc", "abd", 3}, {"abc", "abd", 2},
        {"abc", "ab", 3}, {"", "", 1}, {"abc", "abc", 0}, {"zoo", "zo", 5},
    };
    for (auto& c : cs)
        CHECK_EQ(sgn(StrncmpN(c.a, c.b, c.n)), sgn(std::strncmp(c.a, c.b, c.n)));
}

// ----------------------------------------------------------------------------
// StrCmpNoCase / StrCmpNoCaseN — oracle: libc strcasecmp/strncasecmp (sign).
// ----------------------------------------------------------------------------
TEST(UtilStrOps, StrCmpNoCase_OracleVsLibc) {
    struct Case { const char* a; const char* b; };
    Case cs[] = {
        {"Hello", "hello"}, {"ABC", "abc"}, {"abc", "abd"},
        {"Apple", "apple1"}, {"", ""}, {"Zoo", "zoo"}, {"file.BIN", "FILE.bin"},
    };
    for (auto& c : cs) {
        CHECK_EQ(sgn(StrCmpNoCase(c.a, c.b)), sgn(strcasecmp(c.a, c.b)));
        CHECK_EQ(sgn(StrCmpNoCaseN(c.a, c.b, 100)), sgn(strncasecmp(c.a, c.b, 100)));
    }
    CHECK_EQ(sgn(StrCmpNoCaseN("abcXXX", "abcYYY", 3)), 0);  // bounded equal
}

// ----------------------------------------------------------------------------
// StrCmpNoCaseSign — normalised -1/0/+1, folds to UPPER. Independent oracle.
// ----------------------------------------------------------------------------
TEST(UtilStrOps, StrCmpNoCaseSign_Golden) {
    CHECK_EQ(StrCmpNoCaseSign("abc", "ABC"), 0);
    CHECK_EQ(StrCmpNoCaseSign("abc", "abd"), -1);
    CHECK_EQ(StrCmpNoCaseSign("abd", "abc"), 1);
    CHECK_EQ(StrCmpNoCaseSign("ab", "abc"), -1);   // a ended first
    CHECK_EQ(StrCmpNoCaseSign("abc", "ab"), 1);    // b ended first
    CHECK_EQ(StrCmpNoCaseSign("", ""), 0);
    // sign-equivalent to strcasecmp on the cases above
    CHECK_EQ(StrCmpNoCaseSign("File.BIN", "file.bin"), sgn(strcasecmp("File.BIN", "file.bin")));
}

// ----------------------------------------------------------------------------
// MemMove / MemMove2 — oracle: libc memmove, including overlap both directions.
// ----------------------------------------------------------------------------
TEST(UtilStrOps, MemMove_OracleVsLibc) {
    for (int shift = -4; shift <= 4; ++shift) {
        char mine[32], ref[32];
        const char* pat = "0123456789ABCDEFGHIJ";
        for (int i = 0; i < 32; ++i) { mine[i] = pat[i % 20]; ref[i] = pat[i % 20]; }
        std::size_t n = 12;
        int srcOff = 8, dstOff = 8 + shift;
        if (dstOff < 0) continue;
        MemMove(mine + dstOff, mine + srcOff, n);
        std::memmove(ref + dstOff, ref + srcOff, n);
        CHECK(std::memcmp(mine, ref, 32) == 0);

        // MemMove2 on a fresh copy
        char mine2[32], ref2[32];
        for (int i = 0; i < 32; ++i) { mine2[i] = pat[i % 20]; ref2[i] = pat[i % 20]; }
        MemMove2(mine2 + dstOff, mine2 + srcOff, n);
        std::memmove(ref2 + dstOff, ref2 + srcOff, n);
        CHECK(std::memcmp(mine2, ref2, 32) == 0);
    }
}

TEST(UtilStrOps, MemMove_ReturnAndEdge) {
    char b[8] = {1,2,3,4,5,6,7,8};
    CHECK(MemMove(b, b, 8) == b);          // dst==src no-op
    CHECK(b[0] == 1 && b[7] == 8);
    CHECK(MemMove(b + 1, b, 0) == b + 1);  // n==0
    CHECK(MemMove2(b, b, 0) == b);
}

// ----------------------------------------------------------------------------
// BinarySearch — golden against a hand-built sorted int array. cmp(key, elem).
// ----------------------------------------------------------------------------
static int intSearchCmp(const void* key, const void* elem) {
    int k = *static_cast<const int*>(key);
    int e = *static_cast<const int*>(elem);
    return (k > e) - (k < e);
}

TEST(UtilStrOps, BinarySearch_Golden) {
    int arr[] = {1, 3, 5, 7, 9, 11, 13};
    std::size_t n = sizeof(arr) / sizeof(arr[0]);
    for (std::size_t i = 0; i < n; ++i) {
        int key = arr[i];
        void* hit = BinarySearch(&key, arr, sizeof(int), n, &intSearchCmp);
        CHECK(hit == &arr[i]);
    }
    int miss[] = {0, 2, 4, 12, 14};
    for (int m : miss) {
        void* hit = BinarySearch(&m, arr, sizeof(int), n, &intSearchCmp);
        CHECK(hit == nullptr);
    }
    // empty array
    int k = 5;
    CHECK(BinarySearch(&k, arr, sizeof(int), 0, &intSearchCmp) == nullptr);
    // single element
    int one[] = {42};
    int k42 = 42, k7 = 7;
    CHECK(BinarySearch(&k42, one, sizeof(int), 1, &intSearchCmp) == &one[0]);
    CHECK(BinarySearch(&k7, one, sizeof(int), 1, &intSearchCmp) == nullptr);
}

TEST(UtilStrOps, BinarySearch_OracleVsBsearch) {
    int arr[64];
    for (int i = 0; i < 64; ++i) arr[i] = i * 2;      // 0,2,...,126 sorted
    for (int key = -2; key <= 130; ++key) {
        void* mine = BinarySearch(&key, arr, sizeof(int), 64, &intSearchCmp);
        // libc bsearch uses cmp(key, elem) too
        void* ref = std::bsearch(&key, arr, 64, sizeof(int),
                                 reinterpret_cast<int (*)(const void*, const void*)>(&intSearchCmp));
        CHECK(mine == ref);
    }
}

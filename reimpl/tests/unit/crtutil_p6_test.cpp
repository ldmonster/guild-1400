// Unit tests for the P6 CRT/util gap-closing batch: exact-behavior checks against
// host libc (memcmp/memmove/memset/strcpy) and against the recovered IDA semantics
// (CRC-16 CCITT, asctime layout, byte-rotate, deterministic Fisher-shuffle).
#include "test.h"
#include "util/util_misc.h"
#include "crt/mem_block.h"
#include "crt/asctime.h"
#include "crt/rand.h"
#include <cstring>
#include <ctime>

using namespace guild;

// ---------------- crt::MemSet / MemCompare / MemMoveOverlapping vs libc ----------
TEST(crtutil_p6, mem_set_matches_libc) {
    unsigned char a[64], b[64];
    std::memset(b, 0xAB, sizeof b);
    void* r = crt::MemSet(a, 0xAB, sizeof a);
    CHECK(r == a);
    CHECK(std::memcmp(a, b, sizeof a) == 0);
    // zero length is a no-op returning dst
    CHECK(crt::MemSet(a, 0x00, 0) == a);
}

TEST(crtutil_p6, mem_compare_matches_libc_sign) {
    const char* x = "alphabet";
    const char* y = "alphaZet";
    int got = crt::MemCompare(x, y, 8);
    int ref = std::memcmp(x, y, 8);
    CHECK((got < 0) == (ref < 0));
    CHECK((got > 0) == (ref > 0));
    CHECK(crt::MemCompare(x, x, 8) == 0);
    CHECK(crt::MemCompare("", "", 0) == 0);
    // sign clamped to {-1,0,+1}
    CHECK(crt::MemCompare("a", "b", 1) == -1);
    CHECK(crt::MemCompare("b", "a", 1) == 1);
}

TEST(crtutil_p6, mem_move_overlapping_matches_libc) {
    char ref[16] = "0123456789ABCDE";
    char got[16] = "0123456789ABCDE";
    // overlap, dst > src: shift right by 3
    std::memmove(ref + 3, ref, 10);
    crt::MemMoveOverlapping(got + 3, got, 10);
    CHECK(std::memcmp(ref, got, 16) == 0);
    // overlap, dst < src: shift left by 2
    char ref2[16] = "0123456789ABCDE";
    char got2[16] = "0123456789ABCDE";
    std::memmove(ref2, ref2 + 2, 10);
    crt::MemMoveOverlapping(got2, got2 + 2, 10);
    CHECK(std::memcmp(ref2, got2, 16) == 0);
    // non-overlapping
    char src[8] = "WXYZ", dst[8] = {0};
    CHECK(crt::MemMoveOverlapping(dst, src, 5) == dst);
    CHECK(std::strcmp(dst, "WXYZ") == 0);
}

TEST(crtutil_p6, mem_compare_bounded_stops_at_nul) {
    // a has a NUL at index 3, so only 4 bytes (incl. NUL) are compared.
    char a[8] = {'a','b','c',0,'X','X','X',0};
    char b[8] = {'a','b','c',0,'Y','Y','Y',0};
    CHECK(crt::MemCompareBounded(a, b, 8) == 0); // differ only past the NUL
    char c[8] = {'a','b','Z',0,0,0,0,0};
    // a[2]='c'(99) > c[2]='Z'(90) -> a greater -> +1 (matches memcmp sign)
    CHECK(crt::MemCompareBounded(a, c, 8) == 1);
    int ref = std::memcmp(a, c, 3);
    CHECK((crt::MemCompareBounded(a, c, 8) > 0) == (ref > 0));
    CHECK(crt::MemCompareBounded(a, a, 8) == 0);
    CHECK(crt::MemCompareBounded(a, b, 0) == 0);
    // symmetric: c vs a -> c less -> -1
    CHECK(crt::MemCompareBounded(c, a, 8) == -1);
}

// ---------------- util::RotateByte / AlignTo8 ------------------------------------
TEST(crtutil_p6, rotate_byte_8bit_lane) {
    // value 0x01 << 1 = 0x02 within byte lane
    CHECK_EQ(util::RotateByte(0x01, 1), 0x02);
    // 0x80 << 1 = 0x100 -> low byte 0x00 | (0x0100>>8)=0x01 -> 0x01 (rotate wrap)
    CHECK_EQ(util::RotateByte(0x80, 1), 0x01);
    // shift % 8 wraps
    CHECK_EQ(util::RotateByte(0x01, 9), util::RotateByte(0x01, 1));
    // rotate by 0 is identity on the low byte
    CHECK_EQ(util::RotateByte(0x5A, 0), 0x5A);
    // AlignTo8 is the inverse rotation: rot(v, 8 - s%8). rot by 8 == identity.
    CHECK_EQ(util::AlignTo8(0xC3, 0), util::RotateByte(0xC3, 8)); // == 0xC3
    // a full round trip: AlignTo8(RotateByte(v,s),s) returns v's low byte
    for (int s = 0; s < 8; ++s) {
        int v = 0xB7;
        int rot = util::RotateByte(v, s);
        CHECK_EQ(util::AlignTo8(rot, s), v & 0xFF);
    }
}

// ---------------- util::BuildCrc16Table vs an independent CCITT computation ------
TEST(crtutil_p6, crc16_table_ccitt) {
    u16 table[256];
    util::BuildCrc16Table(table);
    // Independent reference: CRC-16/CCITT-FALSE table generation.
    for (int i = 0; i < 256; ++i) {
        u16 c = static_cast<u16>(i << 8);
        for (int b = 0; b < 8; ++b)
            c = (c & 0x8000) ? static_cast<u16>((c << 1) ^ 0x1021)
                             : static_cast<u16>(c << 1);
        CHECK_EQ(static_cast<int>(table[i]), static_cast<int>(c));
    }
    // a couple of well-known anchors
    CHECK_EQ(static_cast<int>(table[0]), 0x0000);
    CHECK_EQ(static_cast<int>(table[1]), 0x1021);
}

// ---------------- util::MemFindPattern ------------------------------------------
TEST(crtutil_p6, mem_find_pattern) {
    const unsigned char hay[] = "abcdeabcXY";
    const unsigned char need[] = "abcX";
    const void* p = util::MemFindPattern(hay, need, 10, 4);
    CHECK(p == hay + 5);
    // not found
    const unsigned char need2[] = "zzz";
    CHECK(util::MemFindPattern(hay, need2, 10, 3) == nullptr);
    // empty needle -> returns haystack start (faithful quirk)
    CHECK(util::MemFindPattern(hay, need, 10, 0) == hay);
}

// ---------------- util::RetZero / RetOne -----------------------------------------
TEST(crtutil_p6, const_returners) {
    CHECK_EQ(util::RetZero(), 0);
    CHECK_EQ(util::RetOne(), 1);
}

// ---------------- util::StrCopyChecked -------------------------------------------
TEST(crtutil_p6, str_copy_checked) {
    char dst[32];
    CHECK_EQ(util::StrCopyChecked(dst, "hello world"), 1);
    CHECK(std::strcmp(dst, "hello world") == 0);
    CHECK_EQ(util::StrCopyChecked(nullptr, "x"), 0);
    // empty string copies the lone NUL
    CHECK_EQ(util::StrCopyChecked(dst, ""), 1);
    CHECK_EQ(dst[0], '\0');
}

// ---------------- util::RandomMod (MSVC 214013 LCG, seeded) ----------------------
TEST(crtutil_p6, random_mod_deterministic) {
    util::SetMsvcRandSeed(0);
    // Reference: replicate VIBE_Rand_Next exactly.
    auto ref = [](i32& s) { s = 214013 * s + 2531011; return (s >> 16) & 0x7FFF; };
    i32 s = 0;
    for (int k = 0; k < 5; ++k) {
        int hi = ref(s);
        int lo = ref(s);
        u32 expect = (static_cast<u32>(lo) | (static_cast<u32>(hi) << 16)) % 1000u;
        u32 got = util::RandomMod(1000u);
        CHECK_EQ(static_cast<int>(got), static_cast<int>(expect));
    }
    // bound is respected
    util::SetMsvcRandSeed(12345);
    for (int k = 0; k < 100; ++k)
        CHECK(util::RandomMod(7u) < 7u);
}

// ---------------- util::InitAndShuffle* are permutations & deterministic ---------
TEST(crtutil_p6, shuffle_byte_is_permutation) {
    crt::Srand(99);
    u8 arr[16];
    util::InitAndShuffleByteArray(16, arr);
    // result is a permutation of 0..15
    int seen[16] = {0};
    for (int i = 0; i < 16; ++i) {
        CHECK(arr[i] < 16);
        seen[arr[i]]++;
    }
    for (int i = 0; i < 16; ++i)
        CHECK_EQ(seen[i], 1);
    // deterministic given the same seed
    crt::Srand(99);
    u8 arr2[16];
    util::InitAndShuffleByteArray(16, arr2);
    CHECK(std::memcmp(arr, arr2, 16) == 0);
    // count < 2 only fills (no swaps)
    u8 one[1] = {0xFF};
    util::InitAndShuffleByteArray(1, one);
    CHECK_EQ(one[0], 0);
}

TEST(crtutil_p6, shuffle_dword_is_permutation) {
    crt::Srand(7);
    u32 a[12];
    util::InitAndShuffleDwordArray(12, a);
    int seen[12] = {0};
    for (int i = 0; i < 12; ++i) { CHECK(a[i] < 12u); seen[a[i]]++; }
    for (int i = 0; i < 12; ++i) CHECK_EQ(seen[i], 1);

    // ShuffleDwordArray on an already-filled array preserves the multiset.
    crt::Srand(7);
    u32 b[12];
    for (int i = 0; i < 12; ++i) b[i] = 100u + i;
    util::ShuffleDwordArray(12, b);
    long sum = 0;
    for (int i = 0; i < 12; ++i) sum += b[i];
    CHECK_EQ(static_cast<int>(sum), 12 * 100 + (0 + 11) * 12 / 2);
}

// ---------------- crt asctime / FormatTwoDigits ----------------------------------
TEST(crtutil_p6, format_two_digits) {
    char buf[8] = {0};
    crt::FormatTwoDigits(7, 0, buf);
    CHECK_EQ(buf[0], '0'); CHECK_EQ(buf[1], '7');
    crt::FormatTwoDigits(42, 0, buf);
    CHECK_EQ(buf[0], '4'); CHECK_EQ(buf[1], '2');
}

TEST(crtutil_p6, asctime_layout_vs_libc) {
    // tm for "Wed Jun 07 13:05:09 2000": year(since1900)=100, mon=5(Jun), wday=3(Wed)
    crt::TmRec tm{ /*sec*/9, /*min*/5, /*hour*/13, /*mday*/7,
                   /*mon*/5, /*year*/100, /*wday*/3, /*yday*/158, /*isdst*/0 };
    char buf[32] = {0};
    crt::FormatAsctime(&tm, buf);
    CHECK(std::strcmp(buf, "Wed Jun  7 13:05:09 2000\n") == 0);
    CHECK_EQ(buf[25], '\0');

    // Cross-check the exact byte layout against the host's std::asctime for the
    // same broken-down time.
    std::tm htm{};
    htm.tm_sec = 9; htm.tm_min = 5; htm.tm_hour = 13; htm.tm_mday = 7;
    htm.tm_mon = 5; htm.tm_year = 100; htm.tm_wday = 3; htm.tm_yday = 158;
    const char* host = std::asctime(&htm);
    if (host)
        CHECK(std::strcmp(buf, host) == 0);
}

TEST(crtutil_p6, compare_date_fields) {
    int a[3] = {/*day*/7, /*mon*/6, /*year*/2000};
    int b[3] = {/*day*/8, /*mon*/6, /*year*/2000};
    CHECK_EQ(crt::CompareDateFields(a, b), 1); // a before b
    CHECK_EQ(crt::CompareDateFields(b, a), 0);
    CHECK_EQ(crt::CompareDateFields(a, a), 0); // equal is not "before"
    int c[3] = {31, 12, 1999};
    CHECK_EQ(crt::CompareDateFields(c, a), 1); // earlier year
}

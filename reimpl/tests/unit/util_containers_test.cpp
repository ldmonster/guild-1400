#include "test.h"

#include "util/bitset.h"
#include "util/buffer.h"
#include "util/color.h"
#include "util/guid.h"
#include "util/sort.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

using namespace guild;
using namespace guild::util;

namespace {

int CompareInt(const void* a, const void* b) {
    int x = *static_cast<const int*>(a);
    int y = *static_cast<const int*>(b);
    return (x > y) - (x < y); // total order on int
}

bool IsPermutation(std::vector<int> a, std::vector<int> b) {
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    return a == b;
}

} // namespace

// ----- Sort (qsort core) -----

TEST(UtilContainers, QuickSortMatchesStdSortRandom) {
    std::mt19937 rng(0xC0FFEEu);
    for (int trial = 0; trial < 200; ++trial) {
        std::uniform_int_distribution<int> sizeDist(0, 300);
        int n = sizeDist(rng);
        std::vector<int> data(n);
        std::uniform_int_distribution<int> valDist(-1000, 1000);
        for (int& v : data) v = valDist(rng);

        std::vector<int> original = data;
        std::vector<int> expected = data;
        std::sort(expected.begin(), expected.end());

        if (n > 0)
            QuickSort(data.data(), data.size(), sizeof(int), CompareInt);

        // Total order on int: final order must equal std::sort exactly.
        CHECK(data == expected);
        // And must be a permutation of the input.
        CHECK(IsPermutation(original, data));
    }
}

TEST(UtilContainers, QuickSortSmallSizes) {
    // Exercise the insertion-sort cutoff (n <= 8) and boundary cases.
    for (int n = 0; n <= 9; ++n) {
        std::vector<int> data;
        for (int i = 0; i < n; ++i) data.push_back((n - i) * 3 % 7);
        std::vector<int> expected = data;
        std::sort(expected.begin(), expected.end());
        if (n >= 1) QuickSort(data.data(), data.size(), sizeof(int), CompareInt);
        CHECK(data == expected);
    }
}

TEST(UtilContainers, QuickSortDuplicatesAndSorted) {
    std::vector<int> dups(100, 5);
    QuickSort(dups.data(), dups.size(), sizeof(int), CompareInt);
    CHECK(std::is_sorted(dups.begin(), dups.end()));

    std::vector<int> already(100);
    for (int i = 0; i < 100; ++i) already[i] = i;
    std::vector<int> expected = already;
    QuickSort(already.data(), already.size(), sizeof(int), CompareInt);
    CHECK(already == expected);

    std::vector<int> reversed(100);
    for (int i = 0; i < 100; ++i) reversed[i] = 99 - i;
    QuickSort(reversed.data(), reversed.size(), sizeof(int), CompareInt);
    CHECK(reversed == expected);
}

TEST(UtilContainers, InsertionSortDirect) {
    std::vector<int> data = {5, 1, 4, 2, 8, 3, 7, 0};
    std::vector<int> expected = data;
    std::sort(expected.begin(), expected.end());
    // lo = first, hi = last element.
    InsertionSort(data.data(), data.data() + (data.size() - 1), sizeof(int),
                  CompareInt);
    CHECK(data == expected);
}

TEST(UtilContainers, SwapElements) {
    int a = 0x11223344, b = 0x55667788;
    SwapElements(&a, &b, sizeof(int));
    CHECK_EQ(a, 0x55667788);
    CHECK_EQ(b, 0x11223344);
    // Self-swap is a no-op.
    int c = 7;
    SwapElements(&c, &c, sizeof(int));
    CHECK_EQ(c, 7);
    // Zero width is a no-op.
    int d = 1, e = 2;
    SwapElements(&d, &e, 0);
    CHECK_EQ(d, 1);
    CHECK_EQ(e, 2);
}

// ----- bsearch (std oracle over the sorted output) -----
// The util module ships qsort but not a separate bsearch routine; verify the
// sort feeds a correct binary search (std::bsearch) for hits and misses.
TEST(UtilContainers, SortThenBsearchHitsAndMisses) {
    std::vector<int> data;
    std::mt19937 rng(1234);
    std::uniform_int_distribution<int> dist(0, 500);
    for (int i = 0; i < 200; ++i) data.push_back(dist(rng) * 2); // even values
    QuickSort(data.data(), data.size(), sizeof(int), CompareInt);

    // Hits: every element must be found.
    for (int v : data) {
        const void* p = std::bsearch(&v, data.data(), data.size(), sizeof(int),
                                     CompareInt);
        CHECK(p != nullptr);
        CHECK_EQ(*static_cast<const int*>(p), v);
    }
    // Misses: odd values are never present.
    for (int v = 1; v < 1000; v += 2) {
        const void* p = std::bsearch(&v, data.data(), data.size(), sizeof(int),
                                     CompareInt);
        CHECK(p == nullptr);
    }
}

// ----- BitSet -----

TEST(UtilContainers, BitSetTestTailVectors) {
    u32 z[3] = {0, 0, 0};
    CHECK_EQ(BitSetTestTail(z, 0), 1);

    u32 v1[3] = {0x80000000u, 0, 0};
    CHECK_EQ(BitSetTestTail(v1, 0), 1); // MSB set but excluded by mask

    u32 v2[3] = {0x00000001u, 0, 0};
    CHECK_EQ(BitSetTestTail(v2, 0), 0); // LSB inside mask

    u32 v3[3] = {0, 0, 1};
    CHECK_EQ(BitSetTestTail(v3, 0), 0); // following word nonzero

    u32 v4[3] = {0, 0, 0};
    CHECK_EQ(BitSetTestTail(v4, 95), 1);

    u32 v5[3] = {0, 0, 2};
    CHECK_EQ(BitSetTestTail(v5, 95), 1); // bit95 excluded, no following word

    u32 v6[3] = {0, 0, 0xFFFFFFFFu};
    CHECK_EQ(BitSetTestTail(v6, 64), 0); // tail bits of word2 are set
}

TEST(UtilContainers, BitSetAddRoundCarryVectors) {
    u32 a[3] = {0, 0, 0};
    CHECK_EQ(BitSetAddRoundCarry(a, 95), 0);
    CHECK_EQ(a[0], 0u); CHECK_EQ(a[1], 0u); CHECK_EQ(a[2], 1u);

    u32 b[3] = {0, 0, 0xFFFFFFFFu};
    CHECK_EQ(BitSetAddRoundCarry(b, 95), 0);
    CHECK_EQ(b[0], 0u); CHECK_EQ(b[1], 1u); CHECK_EQ(b[2], 0u);

    u32 c[3] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    CHECK_EQ(BitSetAddRoundCarry(c, 95), 1); // total overflow
    CHECK_EQ(c[0], 0u); CHECK_EQ(c[1], 0u); CHECK_EQ(c[2], 0u);

    u32 d[3] = {0, 0, 0};
    CHECK_EQ(BitSetAddRoundCarry(d, 0), 0);
    CHECK_EQ(d[0], 0x80000000u);
}

// ----- Color -----

TEST(UtilContainers, ColorSetAndCompare) {
    u8 c1[3];
    ColorSetRgb(c1, 0x10, 0x20, 0x30); // a=0x10,b=0x20,c=0x30
    // Faithful slot order: [0]=a, [1]=c, [2]=b.
    CHECK_EQ(c1[0], (u8)0x10);
    CHECK_EQ(c1[1], (u8)0x30);
    CHECK_EQ(c1[2], (u8)0x20);

    u8 c2[3];
    ColorSetRgb(c2, 0x10, 0x20, 0x30);
    CHECK_EQ(ColorNotEqualRgb(c1, c2), 0); // identical

    u8 c3[3];
    ColorSetRgb(c3, 0x10, 0x20, 0x31);
    CHECK(ColorNotEqualRgb(c1, c3) != 0); // differs in one byte

    u8 c4[3] = {1, 2, 3};
    u8 c5[3] = {1, 2, 4};
    CHECK(ColorNotEqualRgb(c4, c5) != 0);
    u8 c6[3] = {1, 2, 3};
    CHECK_EQ(ColorNotEqualRgb(c4, c6), 0);
}

// ----- Guid -----

TEST(UtilContainers, GuidFormatKnown) {
    Guid g{};
    g.data1 = 0x12345678u;
    g.data2 = 0x9abc;
    g.data3 = 0xdef0;
    u8 d4[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    std::memcpy(g.data4, d4, 8);

    char buf[kGuidTextSize];
    CHECK(GuidFormat(&g, buf) != 0);
    CHECK_EQ(std::strcmp(buf, "12345678-9abc-def0-0102030405060708"), 0);

    // Null guards.
    CHECK_EQ(GuidFormat(nullptr, buf), 0);
    CHECK_EQ(GuidFormat(&g, nullptr), 0);
}

TEST(UtilContainers, GuidParseRoundtrip) {
    const char* text = "deadbeef-cafe-1234-aabbccddeeff0011";
    Guid g{};
    CHECK(GuidParse(text, &g));
    CHECK_EQ(g.data1, 0xdeadbeefu);
    CHECK_EQ(g.data2, (u16)0xcafe);
    CHECK_EQ(g.data3, (u16)0x1234);
    CHECK_EQ(g.data4[0], (u8)0xaa);
    CHECK_EQ(g.data4[7], (u8)0x11);

    char buf[kGuidTextSize];
    CHECK(GuidFormat(&g, buf) != 0);
    CHECK_EQ(std::strcmp(buf, text), 0);

    // Null/empty guards.
    Guid g2{};
    CHECK(!GuidParse(nullptr, &g2));
    CHECK(!GuidParse("", &g2));
    CHECK(!GuidParse(text, nullptr));
}

// ----- Buffer -----

TEST(UtilContainers, BufferPutChar) {
    char storage[8] = {0};
    OutputBuffer ob{};
    ob.cursor = storage;
    ob.count = 0;
    BufferPutChar(&ob, 'H');
    BufferPutChar(&ob, 'i');
    BufferPutChar(&ob, '!');
    CHECK_EQ(ob.count, 3u);
    CHECK_EQ(ob.cursor, storage + 3);
    CHECK_EQ(storage[0], 'H');
    CHECK_EQ(storage[1], 'i');
    CHECK_EQ(storage[2], '!');
}

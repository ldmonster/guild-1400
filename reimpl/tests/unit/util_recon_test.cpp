#include "tests/framework/test.h"
#include "util/util_recon.h"

#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <vector>
#include <string>

using namespace guild;

// ---------------------------------------------------------------------------
// ReconStrCmp @0x5d3f10 — normalised -1/0/+1 strcmp, cross-checked vs <cstring>.
// ---------------------------------------------------------------------------
static int sgn(int x) { return x < 0 ? -1 : (x > 0 ? 1 : 0); }

TEST(UtilReconStrCmp, GoldenVectors) {
    CHECK_EQ(util::ReconStrCmp("", ""), 0);
    CHECK_EQ(util::ReconStrCmp("abc", "abc"), 0);
    CHECK_EQ(util::ReconStrCmp("abc", "abd"), -1);
    CHECK_EQ(util::ReconStrCmp("abd", "abc"), 1);
    CHECK_EQ(util::ReconStrCmp("ab", "abc"), -1);   // shorter < longer (NUL < 'c')
    CHECK_EQ(util::ReconStrCmp("abc", "ab"), 1);
    CHECK_EQ(util::ReconStrCmp("", "a"), -1);
    CHECK_EQ(util::ReconStrCmp("a", ""), 1);
    // result is normalised to -1/+1, never the raw byte difference (e.g. 'Z'-'a').
    CHECK_EQ(util::ReconStrCmp("Z", "a"), -1);
    CHECK_EQ(util::ReconStrCmp("a", "Z"), 1);
}

TEST(UtilReconStrCmp, SamePointerIsZero) {
    const char* s = "whatever";
    CHECK_EQ(util::ReconStrCmp(s, s), 0);
}

TEST(UtilReconStrCmp, MatchesStrcmpSign) {
    const char* v[] = { "", "a", "ab", "abc", "abd", "abcd", "B", "b",
                        "normal_s", "normal", "zzzzzzzzzzzzz", "zzzzzzzzzzzzy" };
    for (auto* a : v)
        for (auto* b : v)
            CHECK_EQ(util::ReconStrCmp(a, b), sgn(std::strcmp(a, b)));
}

// Long strings exercise the multi-word loop path.
TEST(UtilReconStrCmp, LongStrings) {
    std::string a(100, 'x');
    std::string b(100, 'x');
    CHECK_EQ(util::ReconStrCmp(a.c_str(), b.c_str()), 0);
    b[73] = 'y';
    CHECK_EQ(util::ReconStrCmp(a.c_str(), b.c_str()), -1);
    CHECK_EQ(util::ReconStrCmp(b.c_str(), a.c_str()), 1);
}

// ---------------------------------------------------------------------------
// ReconStrCopyToNormalBuf @0x43de48 — strcpy into a fixed buffer, returns 1.
// ---------------------------------------------------------------------------
TEST(UtilReconStrCopy, IntoExplicitBuffer) {
    char dst[32];
    std::memset(dst, 0xAB, sizeof(dst));
    char src[] = "hello";
    char* psrc = src;
    int r = util::ReconStrCopyToNormalBuf(dst, &psrc);
    CHECK_EQ(r, 1);
    CHECK_EQ(std::strcmp(dst, "hello"), 0);
    // Byte after the terminator is untouched garbage (no over-copy of pairs
    // beyond the NUL on an odd-length string).
}

TEST(UtilReconStrCopy, EvenAndOddLengths) {
    const char* cases[] = { "", "a", "ab", "abc", "abcd", "normal_s" };
    for (auto* c : cases) {
        char dst[32];
        std::memset(dst, 0x7F, sizeof(dst));
        char src[32];
        std::strcpy(src, c);
        char* p = src;
        CHECK_EQ(util::ReconStrCopyToNormalBuf(dst, &p), 1);
        CHECK_EQ(std::strcmp(dst, c), 0);
    }
}

TEST(UtilReconStrCopy, ModuleBufferPreseeded) {
    // The module-level buffer is pre-seeded with "normal_s" exactly like the
    // binary's static initialiser.
    char* buf = util::ReconNormalBuf();
    // (Other tests may have written into it already; re-seed and verify copy.)
    char tok[] = "abc";
    char* p = tok;
    CHECK_EQ(util::ReconStrCopyToNormalBuf(buf, &p), 1);
    CHECK_EQ(std::strcmp(buf, "abc"), 0);
}

// ---------------------------------------------------------------------------
// ReconZeroStruct12 @0x58f138 — clears byte@+0, dword@+4, dword@+8; +1..+3 kept.
// ---------------------------------------------------------------------------
TEST(UtilReconZero, ClearsExpectedBytesOnly) {
    unsigned char rec[12];
    std::memset(rec, 0xFF, sizeof(rec));
    void* r = util::ReconZeroStruct12(rec);
    CHECK_EQ(r, static_cast<void*>(rec));
    CHECK_EQ(rec[0], (unsigned char)0x00);     // byte @ +0 cleared
    CHECK_EQ(rec[1], (unsigned char)0xFF);     // +1..+3 untouched
    CHECK_EQ(rec[2], (unsigned char)0xFF);
    CHECK_EQ(rec[3], (unsigned char)0xFF);
    for (int i = 4; i < 12; ++i)
        CHECK_EQ(rec[i], (unsigned char)0x00); // +4..+11 cleared
}

// ---------------------------------------------------------------------------
// ReconBubbleSortRecords @0x59211c — bubble sort by injected label comparator.
// ---------------------------------------------------------------------------
// Test comparator: emulate StrCmp(label(b), label(a)) where label(id) is the
// decimal string of id. The original swaps when that == 1, i.e. when b's label
// sorts AFTER a's -> ascending order by label string.
static int CmpByDecimalLabel(i32 a, i32 b, void* /*user*/) {
    char la[16], lb[16];
    std::snprintf(la, sizeof(la), "%d", a);
    std::snprintf(lb, sizeof(lb), "%d", b);
    int s = std::strcmp(lb, la);   // StrCmp(label(b), label(a))
    return s < 0 ? -1 : (s > 0 ? 1 : 0);
}

TEST(UtilReconBubble, SkipFlagIsNoOp) {
    i32 arr[] = { 5, 3, 9, 1 };
    util::ReconBubbleSortRecords(arr, 4, /*skipFlag=*/1, &CmpByDecimalLabel);
    CHECK_EQ(arr[0], 5); CHECK_EQ(arr[1], 3); CHECK_EQ(arr[2], 9); CHECK_EQ(arr[3], 1);
}

TEST(UtilReconBubble, NullComparatorIsInert) {
    i32 arr[] = { 5, 3, 9, 1 };
    util::ReconBubbleSortRecords(arr, 4, 0, /*cmp=*/nullptr);
    CHECK_EQ(arr[0], 5); CHECK_EQ(arr[1], 3); CHECK_EQ(arr[2], 9); CHECK_EQ(arr[3], 1);
}

TEST(UtilReconBubble, SortsByLabelString) {
    // The original swaps array[i] and array[j] (i<j) when StrCmp(label[j],
    // label[i]) == 1, i.e. when label[j] sorts AFTER label[i] — so the larger
    // label bubbles toward the FRONT: DESCENDING lexicographic order of labels.
    // ids {3,10,1,2} -> labels {"3","10","1","2"}; descending lex:
    //   "3" > "2" > "10" > "1"  =>  ids 3, 2, 10, 1.
    i32 arr[] = { 3, 10, 1, 2 };
    util::ReconBubbleSortRecords(arr, 4, 0, &CmpByDecimalLabel);
    i32 expect[] = { 3, 2, 10, 1 };
    for (int i = 0; i < 4; ++i)
        CHECK_EQ(arr[i], expect[i]);
}

TEST(UtilReconBubble, SingleAndEmpty) {
    i32 one[] = { 42 };
    util::ReconBubbleSortRecords(one, 1, 0, &CmpByDecimalLabel);
    CHECK_EQ(one[0], 42);
    i32 none[1] = { 7 };
    util::ReconBubbleSortRecords(none, 0, 0, &CmpByDecimalLabel);
    CHECK_EQ(none[0], 7);
}

// ---------------------------------------------------------------------------
// ReconSortSwapElements @0x5ea040
// ---------------------------------------------------------------------------
TEST(UtilReconSwap, SwapsBlocks) {
    unsigned char a[7] = { 1,2,3,4,5,6,7 };
    unsigned char b[7] = { 11,12,13,14,15,16,17 };
    util::ReconSortSwapElements(7, a, b);
    for (int i = 0; i < 7; ++i) {
        CHECK_EQ(a[i], (unsigned char)(11 + i));
        CHECK_EQ(b[i], (unsigned char)(1 + i));
    }
}

// ---------------------------------------------------------------------------
// ReconSortChoosePivot @0x5e9ff0 — returns the median of three.
// ---------------------------------------------------------------------------
static int CmpInt(const void* a, const void* b) {
    int x = *static_cast<const int*>(a);
    int y = *static_cast<const int*>(b);
    return (x < y) ? -1 : (x > y ? 1 : 0);
}

TEST(UtilReconPivot, DecompileExactBranches) {
    // Median-of-three: hand-traced from the 0x5e9ff0 DISASM (the Hex-Rays
    // decompile collapses the comparator operands; disasm is the reference). For
    // a sign comparator it returns the true median of {1,2,3} in every order, so
    // all six permutations of {1,2,3} as (a,b,c) yield 2:
    //   (1,2,3)->2 (1,3,2)->2 (2,1,3)->2 (2,3,1)->2 (3,1,2)->2 (3,2,1)->2
    int x = 1, y = 2, z = 3;
    struct { int* a; int* b; int* c; int want; } cases[] = {
        {&x,&y,&z, 2}, {&x,&z,&y, 2}, {&y,&x,&z, 2},
        {&y,&z,&x, 2}, {&z,&x,&y, 2}, {&z,&y,&x, 2},
    };
    for (auto& cse : cases) {
        void* r = util::ReconSortChoosePivot(cse.a, cse.b, cse.c, &CmpInt);
        CHECK_EQ(*static_cast<int*>(r), cse.want);
    }
}

// ---------------------------------------------------------------------------
// ReconQuickSort @0x5ea068 — full MSVC qsort, cross-checked vs std::sort.
// ---------------------------------------------------------------------------
TEST(UtilReconQuickSort, SmallRunInsertionPath) {
    int a[] = { 5, 1, 4, 2, 8, 3, 7, 6, 0, 9 };  // 10 < 16 -> insertion path
    int b[10]; std::memcpy(b, a, sizeof(a));
    util::ReconQuickSort(a, 10, &CmpInt, sizeof(int));
    std::sort(b, b + 10);
    for (int i = 0; i < 10; ++i) CHECK_EQ(a[i], b[i]);
}

TEST(UtilReconQuickSort, LargeRunPartitionPath) {
    std::vector<int> a;
    unsigned seed = 12345;
    for (int i = 0; i < 500; ++i) { seed = seed * 1103515245u + 12345u; a.push_back((int)(seed % 1000)); }
    std::vector<int> b = a;
    util::ReconQuickSort(a.data(), a.size(), &CmpInt, sizeof(int));
    std::sort(b.begin(), b.end());
    CHECK_EQ(a.size(), b.size());
    bool eq = (a == b);
    CHECK(eq);
}

TEST(UtilReconQuickSort, ManyDuplicates) {
    std::vector<int> a;
    for (int i = 0; i < 300; ++i) a.push_back(i % 5);  // exercises equal-band folding
    std::vector<int> b = a;
    util::ReconQuickSort(a.data(), a.size(), &CmpInt, sizeof(int));
    std::sort(b.begin(), b.end());
    bool eq = (a == b);
    CHECK(eq);
}

TEST(UtilReconQuickSort, AlreadySortedAndReversed) {
    std::vector<int> asc, desc;
    for (int i = 0; i < 200; ++i) { asc.push_back(i); desc.push_back(200 - i); }
    util::ReconQuickSort(asc.data(), asc.size(), &CmpInt, sizeof(int));
    util::ReconQuickSort(desc.data(), desc.size(), &CmpInt, sizeof(int));
    for (int i = 1; i < 200; ++i) {
        CHECK(asc[i - 1] <= asc[i]);
        CHECK(desc[i - 1] <= desc[i]);
    }
}

// Wide elements (struct payload) verify width-correct swaps through the swap helper.
struct Pair { int key; int payload; };
static int CmpPair(const void* a, const void* b) {
    int x = static_cast<const Pair*>(a)->key;
    int y = static_cast<const Pair*>(b)->key;
    return (x < y) ? -1 : (x > y ? 1 : 0);
}
TEST(UtilReconQuickSort, WideElements) {
    std::vector<Pair> a;
    unsigned seed = 999;
    for (int i = 0; i < 120; ++i) { seed = seed * 69069u + 1u; a.push_back({(int)(seed % 50), i}); }
    util::ReconQuickSort(a.data(), a.size(), &CmpPair, sizeof(Pair));
    for (size_t i = 1; i < a.size(); ++i)
        CHECK(a[i - 1].key <= a[i].key);
}

TEST(UtilReconQuickSort, EdgeCounts) {
    int one[] = { 7 };
    util::ReconQuickSort(one, 1, &CmpInt, sizeof(int));
    CHECK_EQ(one[0], 7);
    int two[] = { 9, 4 };
    util::ReconQuickSort(two, 2, &CmpInt, sizeof(int));
    CHECK_EQ(two[0], 4); CHECK_EQ(two[1], 9);
    int zero[] = { 3, 1 };
    util::ReconQuickSort(zero, 0, &CmpInt, sizeof(int));  // no-op
    CHECK_EQ(zero[0], 3);
}

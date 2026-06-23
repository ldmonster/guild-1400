#include "render/scene_recon5_raster.h"
#include "test.h"
#include <vector>

using namespace guild::render;

// 8x8 16-bit framebuffer helper.
static std::vector<guild::u16> makeFb(int w, int h) {
    return std::vector<guild::u16>((size_t)w * h, 0);
}

// --- DrawHLine: horizontal run (y0 == y1) -----------------------------------
// stride=8, y0=y1=2, x0=1, x1=5, color=0xABCD. count = x1-x0 = 4 pixels,
// starting at index stride*y + x0 = 2*8+1 = 17 -> pixels 17,18,19,20 (x=1..4).
TEST(SceneRecon5, DrawHLineHorizontal) {
    auto fb = makeFb(8, 8);
    Framebuffer16 f{fb.data(), 8};
    DrawHLine(f, /*x0*/1, /*y0*/2, /*y1*/2, /*x1*/5, 0xABCD);
    CHECK_EQ(fb[2 * 8 + 1], (guild::u16)0xABCD);
    CHECK_EQ(fb[2 * 8 + 2], (guild::u16)0xABCD);
    CHECK_EQ(fb[2 * 8 + 3], (guild::u16)0xABCD);
    CHECK_EQ(fb[2 * 8 + 4], (guild::u16)0xABCD);
    CHECK_EQ(fb[2 * 8 + 5], (guild::u16)0);   // x1 endpoint exclusive
    CHECK_EQ(fb[2 * 8 + 0], (guild::u16)0);
}

// --- DrawHLine: vertical run (x0 == x1) -------------------------------------
// stride=8, x0=x1=3, y0=1, y1=4, color=0x1234. count = y1-y0 = 3, start index
// x0 + stride*y0 = 3 + 8 = 11, step +stride -> 11,19,27 (rows 1,2,3 at col 3).
TEST(SceneRecon5, DrawHLineVertical) {
    auto fb = makeFb(8, 8);
    Framebuffer16 f{fb.data(), 8};
    DrawHLine(f, /*x0*/3, /*y0*/1, /*y1*/4, /*x1*/3, 0x1234);
    CHECK_EQ(fb[1 * 8 + 3], (guild::u16)0x1234);
    CHECK_EQ(fb[2 * 8 + 3], (guild::u16)0x1234);
    CHECK_EQ(fb[3 * 8 + 3], (guild::u16)0x1234);
    CHECK_EQ(fb[4 * 8 + 3], (guild::u16)0);   // endpoint exclusive
    CHECK_EQ(fb[0 * 8 + 3], (guild::u16)0);
}

// --- DrawHLine: perfect diagonal (Bresenham, dx==dy) ------------------------
// (0,0)->(4,4), stride=8. Bresenham plots (0,0),(1,1),(2,2),(3,3),(4,4).
TEST(SceneRecon5, DrawHLineDiagonal) {
    auto fb = makeFb(8, 8);
    Framebuffer16 f{fb.data(), 8};
    DrawHLine(f, 0, 0, 4, 4, 0x7FFF);
    for (int i = 0; i <= 4; ++i)
        CHECK_EQ(fb[i * 8 + i], (guild::u16)0x7FFF);
    // an off-diagonal cell stays clear
    CHECK_EQ(fb[1 * 8 + 0], (guild::u16)0);
}

// --- DrawHLine: shallow Bresenham (x-major, dx > dy) ------------------------
// (0,0)->(4,1), stride=16. dx=4, dy=1 -> x-major. Endpoints + monotone rows.
TEST(SceneRecon5, DrawHLineShallow) {
    auto fb = makeFb(16, 4);
    Framebuffer16 f{fb.data(), 16};
    DrawHLine(f, 0, 0, 1, 4, 0x0042);
    // first and last endpoints set
    CHECK_EQ(fb[0 * 16 + 0], (guild::u16)0x0042);   // (0,0)
    CHECK_EQ(fb[1 * 16 + 4], (guild::u16)0x0042);   // (4,1)
    // exactly 5 pixels written (one per x column 0..4)
    int n = 0;
    for (auto v : fb) if (v) ++n;
    CHECK_EQ(n, 5);
}

// --- DrawLineLocked: lock failure short-circuits, returns 0 -----------------
static bool g_lockOk = true;
static bool LockHook(void*) { return g_lockOk; }
static int  g_unlocks = 0;
static void UnlockHook(void*) { ++g_unlocks; }

TEST(SceneRecon5, DrawLineLockedLockFail) {
    auto fb = makeFb(8, 8);
    Framebuffer16 f{fb.data(), 8};
    DrawLineLockedHooks h{};
    h.beginFrameLock = &LockHook;
    g_lockOk = false;
    int r = DrawLineLocked(f, 0, 2, 2, 5, 0x1111, h, /*mode*/1, /*dirty*/true);
    CHECK_EQ(r, 0);
    // nothing plotted on lock failure
    CHECK_EQ(fb[2 * 8 + 1], (guild::u16)0);
}

TEST(SceneRecon5, DrawLineLockedUnlockMode) {
    auto fb = makeFb(8, 8);
    Framebuffer16 f{fb.data(), 8};
    DrawLineLockedHooks h{};
    h.beginFrameLock = &LockHook;
    h.endUnlock = &UnlockHook;
    g_lockOk = true;
    g_unlocks = 0;
    // mode 1 with dirty -> plots, calls endUnlock, returns 0.
    int r = DrawLineLocked(f, 1, 2, 2, 4, 0x2222, h, /*mode*/1, /*dirty*/true);
    CHECK_EQ(r, 0);
    CHECK_EQ(g_unlocks, 1);
    CHECK_EQ(fb[2 * 8 + 1], (guild::u16)0x2222);
    // mode 0 (no unlock dispatch) still clears dirty -> returns 0, no unlock call.
    g_unlocks = 0;
    int r2 = DrawLineLocked(f, 1, 3, 3, 4, 0x3333, h, /*mode*/0, /*dirty*/true);
    CHECK_EQ(r2, 0);
    CHECK_EQ(g_unlocks, 0);
}

// --- Snow: grow allocates kind+100 records and RNG-inits the tail -----------
// Deterministic RNG: feed a fixed sequence so init values are golden.
static const guild::i32 kSeq[] = {0, 32768 / 2, 32768, 0, 16384, 8192};
static int g_seqIdx = 0;
static guild::i32 SeqRand(void*) {
    guild::i32 v = kSeq[g_seqIdx % (int)(sizeof(kSeq) / sizeof(kSeq[0]))];
    ++g_seqIdx;
    return v;
}
static void* TestAlloc(size_t n, void*) { return ::operator new(n); }
static void  TestFree(void* p, void*) { ::operator delete(p); }

TEST(SceneRecon5, SnowGrowAllocatesAndInits) {
    SnowState st{};
    SnowRng rng{&SeqRand, nullptr};
    SnowAllocHooks mem{&TestAlloc, &TestFree, nullptr};
    g_seqIdx = 0;
    // kind=1, count=1 -> grow to capacity 101, activeCount=capacityUsed(0).
    SnowGrowFlakeList(st, /*count*/1, /*kind*/1, /*arg4*/0, rng, mem);
    CHECK_EQ(st.capacity, 101);
    CHECK(st.flakes != nullptr);
    CHECK_EQ(st.activeCount, 0);
    CHECK_EQ(st.pendingKind, 1);
    // first flake posX = (rand*1/32768 - 0.5)*2 ; rand seq[0]=0 -> (0-0.5)*2 = -1.
    CHECK(st.flakes[0].posX == -1.0f);
    // posY uses rand seq[1]=16384 -> 16384/32768=0.5 ; (0.5-0.5)*2 = 0.
    CHECK(st.flakes[0].posY == 0.0f);
    TestFree(st.flakes, nullptr);
}

// Snow kind 3 = gust max.
TEST(SceneRecon5, SnowGustMax) {
    SnowState st{};
    SnowRng rng{&SeqRand, nullptr};
    SnowAllocHooks mem{&TestAlloc, &TestFree, nullptr};
    st.gust = 1000.0f;
    // kind 3, count 1: v21 = 3*2500 = 7500 > 1000 -> gust becomes 7500.
    SnowGrowFlakeList(st, 1, 3, 0, rng, mem);
    CHECK(st.gust == 7500.0f);
    // a smaller kind leaves gust unchanged (max). kind 0 path is the alloc path,
    // so use a separate state for the "no-grow" wind case instead.
}

// Snow kind 2 = wind apply.
TEST(SceneRecon5, SnowWindApply) {
    SnowState st{};
    SnowRng rng{&SeqRand, nullptr};
    SnowAllocHooks mem{&TestAlloc, &TestFree, nullptr};
    st.windSrcA = 11; st.windSrcB = 22; st.timeBase = 5;
    // kind 2, arg4 = 100: windScaledX = 2*0.001 = 0.002, windScaledY = 0.001*100.
    SnowGrowFlakeList(st, 1, 2, 100, rng, mem);
    CHECK(st.windScaledX == (float)(2.0 * 0.001));
    CHECK(st.windScaledY == (float)(0.001 * 100.0));
    CHECK_EQ(st.windX0, 11);
    CHECK_EQ(st.windX1, 22);
    CHECK_EQ(st.t2, 5);
    CHECK_EQ(st.t3, 105);
}

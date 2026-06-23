// Golden tests for VIBE_Rain_GrowDropList (rain_grow_misc_recon).
#include "tests/framework/test.h"
#include "render/rain_grow_misc_recon.h"
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace guild;
using guild::render::RainGrowSys;
using guild::render::RainGrowDrop;
using guild::render::Rain_GrowDropList;

namespace {
int g_allocs = 0, g_frees = 0;
void* TestAlloc(unsigned bytes, const char* /*tag*/) {
    ++g_allocs;
    return std::malloc(bytes);
}
void TestFree(void* p) { ++g_frees; std::free(p); }

int g_rndVal = 1000;
int TestRand() { return g_rndVal; }

float f32(double d) { float f = static_cast<float>(d); return f; }
} // namespace

TEST(MiscReconRainGrow, Op0SetsHeadNoGrowWhenWithinCapacity) {
    RainGrowSys sys{};
    sys.capacity = 200;
    sys.drops = reinterpret_cast<RainGrowDrop*>(std::malloc(200 * sizeof(RainGrowDrop)));
    g_allocs = g_frees = 0;
    i32 args[] = {0, 50}; // op=0, count=50 (< capacity)
    Rain_GrowDropList(sys, 1, args, 2, TestAlloc, TestFree, TestRand);
    CHECK_EQ(g_allocs, 0); // no grow
    CHECK_EQ(sys.head, 50); // head is a raw int (mov [ebx],eax)
    std::free(sys.drops);
}

TEST(MiscReconRainGrow, GrowAllocatesPlus100AndSeedsNewSlots) {
    RainGrowSys sys{};
    sys.capacity = 1;
    sys.head = 1; // old count (raw int)
    sys.drops = reinterpret_cast<RainGrowDrop*>(std::calloc(1, sizeof(RainGrowDrop)));
    sys.drops[0].f00 = 12345.0f; // sentinel to confirm copy
    g_allocs = g_frees = 0;
    g_rndVal = 1000;
    i32 args[] = {0, 10}; // op=0, count=10 (>= capacity 1) -> grow to 110
    Rain_GrowDropList(sys, 1, args, 2, TestAlloc, TestFree, TestRand);
    CHECK_EQ(g_allocs, 1);
    CHECK_EQ(g_frees, 1);
    CHECK_EQ(sys.capacity, 110); // 10 + 100
    CHECK_EQ(sys.head, 10);      // op 0 sets head = count after grow
    // copied old slot preserved.
    CHECK_EQ(sys.drops[0].f00, 12345.0f);
    // new slot at index 1 (oldCount=1) seeded by the RNG formula (rnd()=1000).
    // pos = (1000 * 3.0518509447574615e-05 + -0.5) * 2.0
    const float kRN = 3.0518509447574615e-05f;
    float expPos = f32((static_cast<double>(1000) * kRN + -0.5) * 2.0);
    float expVelA = f32(static_cast<double>(1000) * kRN * 0.25 + 0.25);
    float expVelB = f32(static_cast<double>(1000) * kRN * 0.05000000074505806 + 0.25);
    CHECK_EQ(sys.drops[1].f00, expPos);
    CHECK_EQ(sys.drops[1].f04, expPos);
    CHECK_EQ(sys.drops[1].f08, expPos);
    CHECK_EQ(sys.drops[1].f0c, expVelA);
    CHECK_EQ(sys.drops[1].f10, expVelB);
    CHECK_EQ(sys.drops[1].f14, expVelB);
    std::free(sys.drops);
}

TEST(MiscReconRainGrow, Op2BurstSeedsAnchorFields) {
    RainGrowSys sys{};
    sys.capacity = 10;
    sys.drops = reinterpret_cast<RainGrowDrop*>(std::malloc(10 * sizeof(RainGrowDrop)));
    sys.f2c = 7;
    sys.f1c = 11;
    sys.f20 = 22;
    g_allocs = g_frees = 0;
    i32 args[] = {2, 5000, 3000}; // op=2, a=5000, b=3000
    Rain_GrowDropList(sys, 1, args, 3, TestAlloc, TestFree, TestRand);
    CHECK_EQ(g_allocs, 0);
    // f24 = 5000 * 0.001 = 5.0 ; f28 = 3000 * 0.001 = 3.0
    CHECK_EQ(sys.f24, f32(5000.0 * 0.0010000000474974513));
    CHECK_EQ(sys.f28, f32(3000.0 * 0.0010000000474974513));
    CHECK_EQ(sys.f38, 7);          // = f2c
    CHECK_EQ(sys.f14, 11);         // = f1c
    CHECK_EQ(sys.f18, 22);         // = f20
    CHECK_EQ(sys.f3c, 7 + 3000);   // = f2c + b
    std::free(sys.drops);
}

TEST(MiscReconRainGrow, OpGreaterThanTwoIsNoOp) {
    RainGrowSys sys{};
    sys.capacity = 5;
    sys.head = 3;
    sys.drops = reinterpret_cast<RainGrowDrop*>(std::malloc(5 * sizeof(RainGrowDrop)));
    g_allocs = 0;
    i32 args[] = {7}; // op=7 -> nothing
    Rain_GrowDropList(sys, 1, args, 1, TestAlloc, TestFree, TestRand);
    CHECK_EQ(g_allocs, 0);
    CHECK_EQ(sys.head, 3);
    std::free(sys.drops);
}

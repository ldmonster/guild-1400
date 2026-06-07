#include "test.h"

#include "render/heightmap.h"
#include "render/terrain.h"
#include "render/floorwater.h"
#include "render/present.h"
#include "shim/IGraphicsDevice.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::render;

// ---------------------------------------------------------------------------
// Shared synthetic 4x4 heightmap (matches the Python oracle in the build).
// ---------------------------------------------------------------------------
namespace {
u8 g_heights[16] = {
    0, 2, 4, 6,
    1, 3, 5, 7,
    2, 4, 6, 8,
    3, 5, 7, 9,
};

Heightmap MakeHeightmap() {
    Heightmap hm{};
    hm.originX = 10.0f; hm.originY = 5.0f; hm.originZ = 20.0f;
    hm.scaleX = 2.0f;   hm.scaleY = 0.5f;  hm.scaleZ = 3.0f;
    hm.size = 4;
    hm.heights = g_heights;
    return hm;
}

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
} // namespace

// ---------------------------------------------------------------------------
// TileToWorld: integer tile -> world point (golden from Python).
// ---------------------------------------------------------------------------
TEST(RenderTerrainHeightmap, TileToWorldGolden) {
    Heightmap hm = MakeHeightmap();
    float w[3];
    CHECK(TileToWorld(&hm, 0, 0, w));
    CHECK(near(w[0], 10.0f) && near(w[1], 5.0f) && near(w[2], 20.0f));
    CHECK(TileToWorld(&hm, 2, 1, w));
    CHECK(near(w[0], 14.0f) && near(w[1], 7.5f) && near(w[2], 23.0f));
    CHECK(TileToWorld(&hm, 3, 3, w));
    CHECK(near(w[0], 16.0f) && near(w[1], 9.5f) && near(w[2], 29.0f));
}

TEST(RenderTerrainHeightmap, TileToWorldBounds) {
    Heightmap hm = MakeHeightmap();
    float w[3];
    CHECK(!TileToWorld(nullptr, 0, 0, w));
    CHECK(!TileToWorld(&hm, -1, 0, w));
    CHECK(!TileToWorld(&hm, 0, -1, w));
    CHECK(!TileToWorld(&hm, 4, 0, w));
    CHECK(!TileToWorld(&hm, 0, 4, w));
}

// ---------------------------------------------------------------------------
// WorldToTileWithHeight: bilinear sample vs the Python reference path.
// ---------------------------------------------------------------------------
TEST(RenderTerrainHeightmap, BilinearSampleGolden) {
    Heightmap hm = MakeHeightmap();
    struct Case { float wx, wz, h; int tx, tz; };
    Case cases[] = {
        {10.0f, 20.0f, 5.25f,            0, 0},
        {11.0f, 21.5f, 6.0f,             0, 0},
        {13.0f, 23.0f, 7.25f,            1, 1},
        {14.5f, 25.5f, 8.4166666667f,    2, 1},
        {16.99f,28.99f,8.2633333333f,    3, 2},
    };
    for (const auto& c : cases) {
        float world[3] = {c.wx, 0.0f, c.wz};
        int tx = -1, tz = -1; float h = 0.0f;
        CHECK(WorldToTileWithHeight(&hm, world, &tx, &tz, &h));
        CHECK_EQ(tx, c.tx);
        CHECK_EQ(tz, c.tz);
        CHECK(near(h, c.h, 1e-3f));
    }
}

TEST(RenderTerrainHeightmap, WorldToTileBounds) {
    Heightmap hm = MakeHeightmap();
    float world[3] = {0.0f, 0.0f, 0.0f};   // far below origin -> off map
    int tx = 0, tz = 0; float h = 0.0f;
    CHECK(!WorldToTileWithHeight(&hm, world, &tx, &tz, &h));
    float in[3] = {11.0f, 0.0f, 21.0f};
    CHECK(!WorldToTileWithHeight(&hm, in, nullptr, &tz, &h)); // null out ptr -> false
}

// ---------------------------------------------------------------------------
// AverageAreaHeight: 8x8 box average (golden from Python).
// ---------------------------------------------------------------------------
TEST(RenderTerrainHeightmap, AreaAverageGolden) {
    Heightmap hm = MakeHeightmap();
    float world[3] = {13.0f, 0.0f, 23.0f};
    double a = AverageAreaHeight(&hm, world);
    CHECK(std::fabs(a - 7.25) <= 1e-4);
}

// ---------------------------------------------------------------------------
// FloodFillTileType: retype interior cells whose 4-neighbours match.
// entries grid is 24-byte stride; type byte at +0.
// ---------------------------------------------------------------------------
TEST(RenderTerrainHeightmap, FloodFillTileType) {
    const int N = 4;
    std::vector<u8> entries(N * N * 24, 0);
    // Fill all cells with type 5.
    for (int i = 0; i < N * N; ++i) entries[i * 24] = 5;
    Heightmap hm{};
    hm.size = N;
    hm.entries = entries.data();
    hm.heights = nullptr;
    int r = FloodFillTileType(&hm, 5, 9);
    CHECK_EQ(r, N - 1);
    // Interior cell (1,1) and (2,2) have all 4-neighbours == 5 -> retyped to 9.
    CHECK_EQ((int)entries[(1 * N + 1) * 24], 9);
    CHECK_EQ((int)entries[(2 * N + 2) * 24], 9);
    // Edge cells untouched (loop is [1, N-1)).
    CHECK_EQ((int)entries[(0 * N + 0) * 24], 5);
}

// ---------------------------------------------------------------------------
// TileGrid indexing + TileIsUniform.
// ---------------------------------------------------------------------------
TEST(RenderTerrainGrid, TileTypeIndexAndUniform) {
    const int N = 4;
    u8 types[N * N];
    for (int i = 0; i < N * N; ++i) types[i] = 7; // all uniform
    TileGrid g{N, N - 1, types};
    CHECK_EQ(TileTypeIndex(g, 2, 3), 3 * N + 2);
    // wrap: x=5 -> mask 3 -> 1
    CHECK_EQ(TileTypeIndex(g, 5, 4), (4 & 3) * N + (5 & 3));
    CHECK(TileIsUniform(&g, 0, 2, 0));
    // Break uniformity inside the region.
    types[1 * N + 1] = 8;
    CHECK(!TileIsUniform(&g, 0, 2, 0));
    // A region not covering the changed cell stays uniform.
    types[1 * N + 1] = 7;
    CHECK(TileIsUniform(&g, 0, 1, 0));
}

// ---------------------------------------------------------------------------
// Water: gradient fill + wave grid.
// ---------------------------------------------------------------------------
TEST(RenderTerrainWater, HeightGradientGolden) {
    u8 dst[16] = {0};
    FillHeightGradient(dst, 2, 8, 10, 40);
    int expect[] = {10, 15, 20, 25, 30, 35, 40};
    for (int i = 0; i < 7; ++i) CHECK_EQ((int)dst[2 + i], expect[i]);
    // Degenerate (lo > hi): no writes.
    u8 d2[4] = {1, 2, 3, 4};
    FillHeightGradient(d2, 3, 1, 0, 9);
    CHECK_EQ((int)d2[1], 2);
}

TEST(RenderTerrainWater, FloodFillMask) {
    const int S = 4;
    u8 mask[S * S];
    std::memset(mask, 0, sizeof(mask)); // 0 = "fillable"
    // put a wall of 1s splitting the grid: column x=2 all set to 1.
    for (int y = 0; y < S; ++y) mask[y * S + 2] = 1;
    FloodFillMask(mask, S, 0, 0, /*from=*/0, /*to=*/9);
    // Left half (x<2) reachable -> 9; the wall stays 1; right half (x>2) not reached.
    for (int y = 0; y < S; ++y) {
        CHECK_EQ((int)mask[y * S + 0], 9);
        CHECK_EQ((int)mask[y * S + 1], 9);
        CHECK_EQ((int)mask[y * S + 2], 1);
        CHECK_EQ((int)mask[y * S + 3], 0);
    }
}

TEST(RenderTerrainWater, WaveGridGolden) {
    float out[64];
    float amp[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    float phase[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    AnimateWaterWaveGrid(out, amp, phase, 6.28318530718);
    struct G { int c; float x, y, z, w; };
    G g[] = {
        {0,  0.09983342f,  1.96013316f,  1.79541643f, -1.22933148f},
        {1,  0.99130904f,  1.33622830f,  2.33318560f, -2.82732622f},
        {5,  0.55368351f, -0.50993985f,  2.82811608f,  3.45851922f},
        {15, 0.24430468f, -1.79537064f, -2.82284106f,  3.79319578f},
    };
    for (const auto& e : g) {
        float* o = out + 4 * e.c;
        CHECK(near(o[0], e.x, 1e-3f));
        CHECK(near(o[1], e.y, 1e-3f));
        CHECK(near(o[2], e.z, 1e-3f));
        CHECK(near(o[3], e.w, 1e-3f));
    }
}

// ---------------------------------------------------------------------------
// Present dispatch: a mock IGraphicsDevice records the call sequence per mode.
// ---------------------------------------------------------------------------
namespace {
class MockDevice : public shim::IGraphicsDevice {
public:
    std::vector<std::string> calls;
    std::vector<u8> bbpixels;
    shim::Surface surf{};
    bool backbufferNull = false;

    bool init(int, int, int, bool) override { calls.push_back("init"); return true; }
    void shutdown() override { calls.push_back("shutdown"); }
    shim::Surface* backbuffer() override {
        calls.push_back("backbuffer");
        if (backbufferNull) return nullptr;
        bbpixels.assign(16, 0);
        surf.pixels = bbpixels.data();
        surf.width = 4; surf.height = 1; surf.pitch = 16; surf.bpp = 32;
        return &surf;
    }
    void present() override { calls.push_back("present"); }
    void setPalette(const std::uint32_t*) override { calls.push_back("setPalette"); }
};
} // namespace

TEST(RenderPresent, BlitModesPresentOnly) {
    for (PresentMode m : {PresentMode::GdiBitBlt, PresentMode::DDrawBlt,
                          PresentMode::DDrawFlip}) {
        MockDevice dev;
        PresentState st{};
        CHECK(PresentFrame(dev, m, st));
        CHECK_EQ(dev.calls.size(), (size_t)1);
        CHECK_EQ(dev.calls[0], std::string("present"));
    }
}

TEST(RenderPresent, LockModesCopyThenPresent) {
    u8 fb[16];
    for (int i = 0; i < 16; ++i) fb[i] = (u8)(i + 1);
    for (PresentMode m : {PresentMode::DDrawLockBlt, PresentMode::DDrawLockFlip}) {
        MockDevice dev;
        PresentState st{};
        st.framebuffer = fb;
        st.copyBytes = 16;
        CHECK(PresentFrame(dev, m, st));
        // Sequence: backbuffer (lock) then present (unlock+blit/flip).
        CHECK_EQ(dev.calls.size(), (size_t)2);
        CHECK_EQ(dev.calls[0], std::string("backbuffer"));
        CHECK_EQ(dev.calls[1], std::string("present"));
        // Framebuffer copied into the locked backbuffer.
        for (int i = 0; i < 16; ++i) CHECK_EQ((int)dev.bbpixels[i], i + 1);
    }
}

TEST(RenderPresent, LockFailureReportsFalse) {
    MockDevice dev;
    dev.backbufferNull = true;
    PresentState st{};
    CHECK(!PresentFrame(dev, PresentMode::DDrawLockBlt, st));
    CHECK_EQ(dev.calls.size(), (size_t)1);
    CHECK_EQ(dev.calls[0], std::string("backbuffer"));
}

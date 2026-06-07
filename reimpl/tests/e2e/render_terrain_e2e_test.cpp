#include "test.h"

#include "render/heightmap.h"
#include "render/terrain.h"
#include "render/present.h"
#include "shim/IGraphicsDevice.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::render;

// End-to-end terrain flow:
//   1. Build a synthetic NxN heightmap (a ramp).
//   2. Sample a world-space path across it (bilinear) and check vs a local oracle.
//   3. Generate the terrain tile world positions for a sub-region (TileToWorld walk).
//   4. Present the resulting framebuffer through a mock IGraphicsDevice in a
//      Lock+memcpy+Flip mode and verify the device call sequence + copied pixels.

namespace {
constexpr int N = 8;

float g_eps = 1e-3f;
bool near(float a, float b) { return std::fabs(a - b) <= g_eps; }

// Local Python-equivalent oracle for the bilinear sampler.
struct Oracle {
    const Heightmap& hm;
    float hgt(int x, int z) const {
        return (float)((double)hm.heights[x + hm.size * z] + 0.5) * hm.scaleY + hm.originY;
    }
    bool sample(float wx, float wz, float& out) const {
        double fx = ((double)wx - hm.originX) / hm.scaleX;
        double fz = ((double)wz - hm.originZ) / hm.scaleZ;
        int tx = (int)std::trunc(fx), tz = (int)std::trunc(fz);
        if (tx < 0 || tx >= hm.size || tz < 0 || tz >= hm.size) return false;
        double fracX = ((double)wx - ((double)tx * hm.scaleX + hm.originX)) / hm.scaleX;
        double fracZ = ((double)wz - ((double)tz * hm.scaleZ + hm.originZ)) / hm.scaleZ;
        int nx = (hm.size - 1) & (tx + 1);
        int nz = (hm.size - 1) & (tz + 1);
        double h00 = hgt(tx, tz), hX = hgt(nx, tz), hZ = hgt(tx, nz);
        out = (float)((hZ - h00) * fracZ + fracX * (hX - h00) + h00);
        return true;
    }
};

class MockDevice : public shim::IGraphicsDevice {
public:
    std::vector<std::string> calls;
    std::vector<u8> bb;
    shim::Surface surf{};
    bool init(int, int, int, bool) override { calls.push_back("init"); return true; }
    void shutdown() override { calls.push_back("shutdown"); }
    shim::Surface* backbuffer() override {
        calls.push_back("backbuffer");
        bb.assign(N * N, 0);
        surf.pixels = bb.data(); surf.pitch = N; surf.width = N; surf.height = N; surf.bpp = 8;
        return &surf;
    }
    void present() override { calls.push_back("present"); }
    void setPalette(const std::uint32_t*) override {}
};
} // namespace

TEST(RenderTerrainE2E, SamplePathAndPresent) {
    // 1. Synthetic heightmap: height(x,z) = x + 2*z (a diagonal ramp).
    std::vector<u8> heights(N * N);
    for (int z = 0; z < N; ++z)
        for (int x = 0; x < N; ++x)
            heights[z * N + x] = (u8)(x + 2 * z);

    Heightmap hm{};
    hm.originX = 0.0f; hm.originY = 1.0f; hm.originZ = 0.0f;
    hm.scaleX = 1.0f;  hm.scaleY = 0.25f; hm.scaleZ = 1.0f;
    hm.size = N;
    hm.heights = heights.data();

    Oracle oracle{hm};

    // 2. Sample a path of world points across the map; compare to the oracle.
    int pathChecks = 0;
    for (float t = 0.0f; t <= 1.0f + 1e-6f; t += 0.1f) {
        float wx = 0.5f + t * 6.0f;
        float wz = 0.5f + t * 6.0f;
        float world[3] = {wx, 0.0f, wz};
        int tx = -1, tz = -1; float h = 0.0f;
        bool ok = WorldToTileWithHeight(&hm, world, &tx, &tz, &h);
        float ref = 0.0f;
        bool okRef = oracle.sample(wx, wz, ref);
        CHECK_EQ(ok, okRef);
        if (ok) {
            CHECK(near(h, ref));
            CHECK_EQ(tx, (int)std::trunc(wx));
            CHECK_EQ(tz, (int)std::trunc(wz));
            ++pathChecks;
        }
    }
    CHECK(pathChecks >= 8);

    // 3. Generate terrain tile world positions for a 4x4 region and rasterize the
    //    elevation into an N*N grayscale framebuffer (proxy for the tile walk).
    std::vector<u8> fb(N * N, 0);
    for (int tz = 1; tz <= 4; ++tz) {
        for (int tx = 1; tx <= 4; ++tx) {
            float w[3];
            CHECK(TileToWorld(&hm, tx, tz, w));
            // World Y must equal height(tx,tz)*scaleY + originY.
            float expectY = (float)heights[tz * N + tx] * hm.scaleY + hm.originY;
            CHECK(near(w[1], expectY));
            CHECK(near(w[0], (float)tx));
            CHECK(near(w[2], (float)tz));
            fb[tz * N + tx] = heights[tz * N + tx]; // store elevation byte
        }
    }

    // 4. Present the framebuffer via the mock device in Lock+Flip mode.
    MockDevice dev;
    PresentState st{};
    st.framebuffer = fb.data();
    st.copyBytes = (i32)fb.size();
    st.width = N; st.height = N;
    CHECK(PresentFrame(dev, PresentMode::DDrawLockFlip, st));

    // Verify the device call sequence and the copied pixels.
    std::vector<std::string> expectSeq = {"backbuffer", "present"};
    CHECK_EQ(dev.calls.size(), expectSeq.size());
    for (size_t i = 0; i < expectSeq.size() && i < dev.calls.size(); ++i)
        CHECK_EQ(dev.calls[i], expectSeq[i]);
    for (int i = 0; i < N * N; ++i) CHECK_EQ((int)dev.bb[i], (int)fb[i]);
}

TEST(RenderTerrainE2E, GridScaleDerivation) {
    // Derive grid scale from a world AABB and check the X/Z mapping is consistent.
    Heightmap hm{};
    hm.size = 16;
    // World X spans [100, 200], Z origin 300 down to min 50.
    DeriveGridScaleXZ(&hm, /*minX=*/100.0f, /*maxX=*/200.0f,
                      /*originZ=*/300.0f, /*minZ=*/50.0f);
    CHECK(std::fabs(hm.originX - 100.0f) <= 1e-3f);
    CHECK(std::fabs(hm.originZ - 300.0f) <= 1e-3f);
    // scaleX = (200-100)/(16-1.75) = 100/14.25
    CHECK(std::fabs(hm.scaleX - (100.0f / 14.25f)) <= 1e-3f);
    // scaleZ = (50-300)/(-1.75+16) = -250/14.25
    CHECK(std::fabs(hm.scaleZ - (-250.0f / 14.25f)) <= 1e-3f);
}

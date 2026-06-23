// End-to-end: a whole-frame water animation flow across the real render water
// modules (driver -> texture advance -> phase propagation -> wave grid), plus a
// GUARDED real-asset presence check (skip-pass if the Resources BINs are absent).
#include "test.h"

#include "render/water_vertices.h"
#include "render/floorwater.h"
#include "render/water_anim.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace guild;

namespace {
constexpr double kTwoPi = 6.283185307179586;
bool finiteGrid(const float* g, int n) {
    for (int i = 0; i < n; ++i)
        if (!std::isfinite(g[i])) return false;
    return true;
}
u32 BankFind(i32 groupId, u8 frameByte, void*) {
    return (u32)(((groupId & 0xFF) << 8) | frameByte);
}
} // namespace

// ---------------------------------------------------------------------------
// Multi-frame integration: advance a small pond of water meshes over several
// frames and confirm the animation is deterministic, bounded by amplitude, and
// that the phase accumulators stay reduced into [0, 2π).
// ---------------------------------------------------------------------------
TEST(WaterVerticesE2E, MultiFramePondFlow) {
    const int N = 4;
    std::vector<render::WaterMesh> pond(N);
    std::memset(pond.data(), 0, N * sizeof(render::WaterMesh));
    for (int i = 0; i < N; ++i) {
        render::WaterMesh& m = pond[i];
        m.hasTexture = true;
        m.groupId = i + 1;
        m.texMemberCount = (u8)(4 + i);
        m.texSpeedNibble = (u8)(1 + i);   // distinct speeds
        m.texRateA = 0.1f * (i + 1);
        m.texAccumA = 0.0f;
        for (int k = 0; k < 4; ++k) {
            m.waveSpeed[k] = 0.13f * (k + 1) + 0.01f * i;
            m.amp[k] = 1.0f + 0.25f * k;
            m.phase[k] = 0.02f * (k + 1);
        }
        m.lastTime = 0;
    }

    // Run 30 frames at +2 ticks each.
    i32 t = 0;
    for (int frame = 0; frame < 30; ++frame) {
        t += 2;
        render::AnimateWaterVertices(pond.data(), N, t, BankFind, nullptr);
    }

    for (int i = 0; i < N; ++i) {
        const render::WaterMesh& m = pond[i];
        CHECK(finiteGrid(m.waveOut, 64));
        CHECK_EQ(m.lastTime, t);
        // each grid component is bounded by its axis amplitude (|sin|,|cos| <= 1):
        for (int c = 0; c < 16; ++c)
            for (int k = 0; k < 4; ++k)
                CHECK(std::fabs(m.waveOut[4 * c + k]) <= m.amp[k] + 1e-3f);
        // all four phase accumulators reduced into [0, 2π) by the in-place
        // propagation Fmod (0x5be428: phase[k]=Fmod(speed[k]*dt+phase[k],2π)):
        for (int k = 0; k < 4; ++k) {
            CHECK(m.phase[k] >= 0.0f);
            CHECK(m.phase[k] < (float)kTwoPi + 1e-3f);
        }
        // texture accumulator stays in [0,1):
        CHECK(m.texAccumA >= 0.0f && m.texAccumA < 1.0f + 1e-3f);
    }
}

// Determinism: identical inputs -> identical grids across two independent runs.
TEST(WaterVerticesE2E, DeterministicReplay) {
    auto seed = [](render::WaterMesh& m) {
        std::memset(&m, 0, sizeof(m));
        m.hasTexture = false;
        m.lastTime = 0;
        for (int k = 0; k < 4; ++k) {
            m.waveSpeed[k] = 0.3f * (k + 1);
            m.amp[k] = 2.0f - 0.3f * k;
            m.phase[k] = 0.11f * (k + 1);
        }
    };
    render::WaterMesh a, b;
    seed(a); seed(b);
    for (i32 t = 3; t <= 30; t += 3) {
        render::AnimateWaterVertices(&a, 1, t, BankFind, nullptr);
        render::AnimateWaterVertices(&b, 1, t, BankFind, nullptr);
    }
    for (int i = 0; i < 64; ++i)
        CHECK_EQ(a.waveOut[i], b.waveOut[i]);
    CHECK_EQ(a.phase[0], b.phase[0]);
}

// ---------------------------------------------------------------------------
// GUARDED real-asset presence (skip-pass when the originals are absent).
// ---------------------------------------------------------------------------
TEST(WaterVerticesE2E, RealAssetsPresent) {
    const char* paths[] = {
        "europe_guild_1400_original/Resources/forms.BIN",
        "europe_guild_1400_original/Resources/Objects.BIN",
    };
    bool any = false;
    for (const char* p : paths) {
        std::FILE* f = std::fopen(p, "rb");
        if (!f) continue;
        any = true;
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fclose(f);
        CHECK(sz > 0);
    }
    if (!any)
        std::printf("    [skip] real asset BINs absent — skip-pass\n");
    CHECK(true);
}

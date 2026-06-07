#include "test.h"

// Golden-vector unit tests for the distance-fog cluster (src/render/fog.cpp).
// All expected values were computed with an independent Python reference (32-bit
// float / 64-bit double promotion matched to the gilde.exe arithmetic).
#include "render/fog.h"

#include <cmath>
#include <cstdint>

using namespace guild;
using namespace guild::render;

namespace {
bool feq(float a, float b) { return std::fabs(a - b) <= 1e-4f * (1.0f + std::fabs(b)); }
}

// ---- SetFogRange: slope = 255/(far-near), nearSq = near^2 ------------------
TEST(RenderFog, SetFogRangeMath) {
    FogState s{};
    bool changed = SetFogRange(s, 10.0f, 110.0f);
    CHECK(changed);
    CHECK(feq(s.nearPlane, 10.0f));
    CHECK(feq(s.farPlane, 110.0f));
    CHECK(feq(s.nearSq, 100.0f));
    CHECK(feq(s.densitySlope, 2.549999952316284f)); // 255/100
    // Re-applying the identical range is a no-op (the != || != early-out).
    CHECK(!SetFogRange(s, 10.0f, 110.0f));
    // Changing only far recomputes the slope.
    CHECK(SetFogRange(s, 10.0f, 60.0f));
    CHECK(feq(s.densitySlope, 5.1f)); // 255/50
}

// ---- ComputeFogFactor: per-distance fog byte ------------------------------
TEST(RenderFog, FactorPerDistance) {
    FogState s{};
    SetFogRange(s, 10.0f, 110.0f); // slope 2.55, nearSq 100
    // d2 <= nearSq -> full (255).
    CHECK_EQ(ComputeFogFactor(s, 0.0f), 255);
    CHECK_EQ(ComputeFogFactor(s, 100.0f), 255);
    // mid-range distances (golden).
    CHECK_EQ(ComputeFogFactor(s, 400.0f), 229);
    CHECK_EQ(ComputeFogFactor(s, 3600.0f), 127);
    // exactly at the far plane (d2 = 110^2): factor 0.
    CHECK_EQ(ComputeFogFactor(s, 12100.0f), 0);
    // beyond far: the deliberate wrap 255-256 = -1 (low byte 0xFF).
    CHECK_EQ(ComputeFogFactor(s, 40000.0f), -1);
    CHECK_EQ((std::uint8_t)ComputeFogFactor(s, 40000.0f), 0xFF);
}

// ---- ConfigureFog: gating + latches ---------------------------------------
TEST(RenderFog, ConfigureGating) {
    FogState s{};
    // Gate closed (fog disabled globally) -> no-op.
    CHECK(!ConfigureFog(s, 10.0f, 110.0f, 0x123456, false, true));
    CHECK(!ConfigureFog(s, 10.0f, 110.0f, 0x123456, true, false));
    // Gate open, near < far -> applies range + latches.
    CHECK(ConfigureFog(s, 10.0f, 110.0f, 0x123456, true, true));
    CHECK(s.enabled);
    CHECK(feq(s.nearPlane, 10.0f));
    CHECK(feq(s.densitySlope, 2.549999952316284f));
    CHECK_EQ(s.color, 0x123456);
    CHECK(feq(s.blendNear, 10.0f));
    CHECK(feq(s.blendFar, 110.0f));
    // near >= far -> enabled cleared, range NOT recomputed (slope unchanged).
    CHECK(ConfigureFog(s, 200.0f, 50.0f, 0x654321, true, true));
    CHECK(!s.enabled);
    CHECK(feq(s.densitySlope, 2.549999952316284f)); // unchanged
    CHECK_EQ(s.color, 0x654321);
}

// ---- ApplyAmbientBlend: time-of-day cross-band fog lerp -------------------
TEST(RenderFog, AmbientBlendGolden) {
    FogBand bands[6] = {};
    bands[0] = FogBand{200, 100, 50, 20.0f, 120.0f};
    bands[1] = FogBand{100, 200, 250, 40.0f, 200.0f};

    auto check = [](FogState& s, std::int32_t color, float n, float f, float slope) {
        CHECK_EQ(s.color, color);
        CHECK(feq(s.blendNear, n));
        CHECK(feq(s.blendFar, f));
        CHECK(s.enabled);
        CHECK(feq(s.densitySlope, slope));
    };

    FogState s{};
    CHECK(ApplyAmbientBlend(s, bands, 0, 1, 0.0f, 1.0f, true, true));
    check(s, 0xC86432, 20.0f, 120.0f, 2.549999952316284f);

    CHECK(ApplyAmbientBlend(s, bands, 0, 1, 0.5f, 1.0f, true, true));
    check(s, 0x969696, 30.0f, 160.0f, 1.9615384340286255f);

    CHECK(ApplyAmbientBlend(s, bands, 0, 1, 1.0f, 1.0f, true, true));
    check(s, 0x64C8FA, 40.0f, 200.0f, 1.59375f);

    CHECK(ApplyAmbientBlend(s, bands, 0, 1, 0.25f, 1.0f, true, true));
    check(s, 0xAF7D64, 25.0f, 140.0f, 2.2173912525177f);

    // Guards: band index >= 6 or t out of [0,1] -> no change.
    FogState g{};
    CHECK(!ApplyAmbientBlend(g, bands, 6, 1, 0.5f, 1.0f, true, true));
    CHECK(!ApplyAmbientBlend(g, bands, 0, 6, 0.5f, 1.0f, true, true));
    CHECK(!ApplyAmbientBlend(g, bands, 0, 1, -0.1f, 1.0f, true, true));
    CHECK(!ApplyAmbientBlend(g, bands, 0, 1, 1.1f, 1.0f, true, true));
    CHECK(!g.enabled);
}

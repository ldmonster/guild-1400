// Golden vectors for render::BuildFalloffLUT — gilde.exe 0x5c88f8
// (VIBE_Light_InitFalloffTable): the 1024-entry angular falloff LUT the per-vertex
// light accumulation indexes by the negated cosine. (The 7-band ambient blend
// render::BlendBandLighting @0x5b85e4 is covered by render_effects_e2e_test.)
#include "render/light.h"
#include "tests/framework/test.h"

#include <cmath>
#include <vector>

using namespace guild::render;

namespace {
bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e; }
}

// out[k] = 1 - (2/pi)*acos(k/1024): a softened Lambert rising 0 -> ~1 (facing).
TEST(LightFalloff, FalloffLUT) {
    static float lut[1024];
    BuildFalloffLUT(lut);
    // k=0  -> acos(0)=pi/2 -> 1 - (2/pi)(pi/2) = 0 (grazing).
    CHECK(Near(lut[0], 0.0f));
    // k=1023 -> acos(0.999)~0.0442 -> 1 - (2/pi)(0.0442) ~ 0.9719 (near-facing).
    CHECK(Near(lut[1023], 1.0f - 0.63661975f * std::acos(1023.0f / 1024.0f)));
    CHECK(lut[1023] > 0.95f && lut[1023] < 1.0f);
    // k=512 (x = 0.5): acos(0.5)=pi/3 -> 1 - (2/pi)(pi/3) = 1 - 2/3 = 0.3333.
    CHECK(Near(lut[512], 1.0f / 3.0f, 1e-3f));
    // strictly increasing across the table (larger cosine -> more facing -> brighter).
    bool increasing = true;
    for (int k = 1; k < 1024; ++k)
        if (lut[k] < lut[k - 1] - 1e-6f) increasing = false;
    CHECK(increasing);
    // all entries in [0, 1].
    bool inRange = true;
    for (int k = 0; k < 1024; ++k)
        if (lut[k] < -1e-4f || lut[k] > 1.0001f) inRange = false;
    CHECK(inRange);
}

// Point light directly above a floor vertex facing up: NdotL = -1 (full), in range.
TEST(LightAccum, PointLightFacing) {
    static float lut[1024]; BuildFalloffLUT(lut);
    const float vpos[3] = {0, 0, 0}, vn[3] = {0, 1, 0};
    const float lpos[3] = {0, 10, 0}, col[3] = {255, 255, 255};
    float acc[3] = {0, 0, 0};
    // distSq=100<range^2=400; NdotL=-1; atten=1*10/(1*100)=0.1; f=0.1*LUT[1023]~0.0972.
    AccumulatePointLight(vpos, vn, lpos, col, /*range=*/20, /*intensity=*/1,
                         /*rangeParam=*/1, /*objScale=*/1, lut, acc);
    const float want = 255.0f * 0.1f * (1.0f - 0.63661975f * std::acos(1023.0f / 1024.0f));
    CHECK(Near(acc[0], want, 0.05f));
    CHECK(Near(acc[0], acc[1]) && Near(acc[1], acc[2]));
    CHECK(acc[0] > 20.0f && acc[0] < 26.0f);
}

// Out of range -> no contribution; facing away -> no contribution.
TEST(LightAccum, PointLightGated) {
    static float lut[1024]; BuildFalloffLUT(lut);
    const float vpos[3] = {0, 0, 0}, vn[3] = {0, 1, 0}, col[3] = {255, 255, 255};
    float acc[3] = {0, 0, 0};
    const float far[3] = {0, 100, 0};            // distSq=10000 > 400
    AccumulatePointLight(vpos, vn, far, col, 20, 1, 1, 1, lut, acc);
    CHECK(Near(acc[0], 0.0f) && Near(acc[1], 0.0f) && Near(acc[2], 0.0f));
    const float vnDown[3] = {0, -1, 0};          // faces away from the light above
    const float near[3] = {0, 10, 0};
    AccumulatePointLight(vpos, vnDown, near, col, 20, 1, 1, 1, lut, acc);
    CHECK(Near(acc[0], 0.0f));
}

// Closer point light -> larger contribution (inverse-square attenuation).
TEST(LightAccum, PointLightInverseSquare) {
    static float lut[1024]; BuildFalloffLUT(lut);
    const float vpos[3] = {0, 0, 0}, vn[3] = {0, 1, 0}, col[3] = {255, 255, 255};
    float near[3] = {0, 0, 0}, far[3] = {0, 0, 0};
    const float lNear[3] = {0, 5, 0}, lFar[3] = {0, 15, 0};
    AccumulatePointLight(vpos, vn, lNear, col, 40, 1, 1, 1, lut, near);
    AccumulatePointLight(vpos, vn, lFar, col, 40, 1, 1, 1, lut, far);
    CHECK(near[0] > far[0] * 2.0f);              // 1/25 vs 1/225 -> ~9x
}

// Directional (sun): no distance term, intensity*0.001 scale.
TEST(LightAccum, Directional) {
    static float lut[1024]; BuildFalloffLUT(lut);
    const float vn[3] = {0, 1, 0}, dir[3] = {0, -1, 0}, col[3] = {255, 255, 255};
    float acc[3] = {0, 0, 0};
    // NdotL=-1; f = 1000*0.001*LUT[1023] ~ 0.972; acc ~ 247.8.
    AccumulateDirectionalLight(vn, dir, col, /*intensity=*/1000, /*objScale=*/1, lut, acc);
    CHECK(acc[0] > 240.0f && acc[0] < 252.0f);
    // facing away (normal flipped) -> nothing.
    const float vnDown[3] = {0, -1, 0};
    float acc2[3] = {0, 0, 0};
    AccumulateDirectionalLight(vnDown, dir, col, 1000, 1, lut, acc2);
    CHECK(Near(acc2[0], 0.0f));
}

// =============================================================================
// WAVE-10 HARDENING — degenerate / edge / index-bounds coverage (ASAN+UBSAN).
// =============================================================================

// (W10-a) Falloff LUT INDEX BOUNDS: the consumer index is (int)(NdotL * -1023). A
// NON-UNIT (over-long) normal can push NdotL below -1, giving a raw index > 1023; it
// MUST be clamped to [0,1023] or lut[idx] is a hard OOB read. Drive both Accumulate
// kernels with a length-3 normal so the raw index would be 3*1023 == 3069.
TEST(LightHarden, FalloffIndexClampedForOverlongNormal) {
    static float lut[1024]; BuildFalloffLUT(lut);
    // Point light directly above; normal length 3, facing it -> NdotL == -3.
    const float vpos[3] = {0, 0, 0}, lpos[3] = {0, 10, 0}, col[3] = {1, 1, 1};
    const float vnLong[3] = {0, 3, 0};
    float acc[3] = {0, 0, 0};
    AccumulatePointLight(vpos, vnLong, lpos, col, 20, 1, 1, 1, lut, acc);
    CHECK(acc[0] >= 0.0f);           // no crash == index clamped to 1023
    // Directional, same over-long normal -> raw index -3*-1023 == 3069, clamp 1023.
    const float dir[3] = {0, -1, 0};
    float acc2[3] = {0, 0, 0};
    AccumulateDirectionalLight(vnLong, dir, col, 1000, 1, lut, acc2);
    CHECK(acc2[0] >= 0.0f);
}

// (W10-b) Directional ZERO DIRECTION: |dir| < 1e-6 -> early-out (no normalize-by-zero,
// no NaN), accumulator untouched.
TEST(LightHarden, DirectionalZeroDir) {
    static float lut[1024]; BuildFalloffLUT(lut);
    const float vn[3] = {0, 1, 0}, dir[3] = {0, 0, 0}, col[3] = {255, 255, 255};
    float acc[3] = {1.0f, 2.0f, 3.0f};
    AccumulateDirectionalLight(vn, dir, col, 1000, 1, lut, acc);
    CHECK(Near(acc[0], 1.0f)); CHECK(Near(acc[1], 2.0f)); CHECK(Near(acc[2], 3.0f));
}

// (W10-c) Point light at the SAME position as the vertex (distSq == 0) and with a
// zero rangeParam: both degenerate denominators must be guarded (no divide-by-zero /
// no inf), leaving the accumulator unchanged. distSq==0 < range^2 but the normalize
// uses the 1e-12 floor; rangeParam==0 returns before the divide.
TEST(LightHarden, PointLightDegenerateDenominators) {
    static float lut[1024]; BuildFalloffLUT(lut);
    const float vn[3] = {0, 1, 0}, col[3] = {255, 255, 255};
    // Coincident light: distSq == 0. ndotl from the (1e-12-floored) direction.
    const float vpos[3] = {0, 0, 0}, lpos[3] = {0, 0, 0};
    float acc[3] = {0, 0, 0};
    AccumulatePointLight(vpos, vn, lpos, col, 20, 1, 1, 1, lut, acc);
    CHECK(acc[0] == acc[0]);         // finite (not NaN)
    // rangeParam == 0: guarded -> no contribution, accumulator untouched.
    const float lpos2[3] = {0, 10, 0};
    float acc2[3] = {5.0f, 5.0f, 5.0f};
    AccumulatePointLight(vpos, vn, lpos2, col, 20, 1, /*rangeParam*/0.0f, 1, lut, acc2);
    CHECK(Near(acc2[0], 5.0f));
}

// (W10-d) BuildFalloffLUT writes exactly 1024 entries, none NaN/Inf, all finite — the
// full table is filled in-bounds (any over-write of out[1024] trips ASAN on a sized
// buffer here).
TEST(LightHarden, FalloffTableFullyFilledFinite) {
    std::vector<float> lut(1024, -1.0f);
    BuildFalloffLUT(lut.data());
    for (int k = 0; k < 1024; ++k) {
        CHECK(lut[k] == lut[k]);                       // not NaN
        CHECK(lut[k] >= -1e-4f && lut[k] <= 1.0001f);  // in [0,1]
    }
}

// (W10-e) ComputeRayFalloff smoothstep + clamp branches (the t<=0 / t>=1 guards). The
// span is non-zero here (the engine itself does not guard the divide; a zero span is
// not reached by its callers, so we exercise only the in-domain branches).
TEST(LightHarden, RayFalloffSmoothstepAndClamps) {
    using guild::render::ComputeRayFalloff;
    // Normal span: t in [0,1] smoothstep. t=0 -> 0, t=1 -> 1, t=0.5 -> 0.5.
    CHECK(Near(ComputeRayFalloff(0.0f, 1.0f, 0.0f), 0.0f));
    CHECK(Near(ComputeRayFalloff(0.0f, 1.0f, 1.0f), 1.0f));
    CHECK(Near(ComputeRayFalloff(0.0f, 1.0f, 0.5f), 0.5f));
    // t below 0 clamps to 0; t above 1 clamps to 1.
    CHECK(Near(ComputeRayFalloff(0.0f, 1.0f, -5.0f), 0.0f));
    CHECK(Near(ComputeRayFalloff(0.0f, 1.0f, 5.0f), 1.0f));
}

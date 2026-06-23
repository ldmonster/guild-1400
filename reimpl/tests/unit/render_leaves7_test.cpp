#include "test.h"

#include "render/render_leaves7.h"
#include "util/math.h"

#include <cmath>

using namespace guild;
using guild::render::ShadowClipRect;
using guild::render::GroundVertex;

namespace {
bool approx(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps * (1.0f + std::fabs(b));
}
bool approxd(double a, double b, double eps = 1e-9) {
    return std::fabs(a - b) <= eps * (1.0 + std::fabs(b));
}
} // namespace

// ---------------------------------------------------------------------------
// 0x5f3048 — ComputeShadowClipRect.
// ---------------------------------------------------------------------------
TEST(RenderLeaves7_Shadow, ClipRectClampsAndDerivesParams) {
    ShadowClipRect r;
    int ok = render::ComputeShadowClipRect(/*dim*/64, /*minX*/-4, /*maxX*/20,
                                           /*minY*/10, /*maxY*/40, &r);
    CHECK_EQ(ok, 1);
    if (ok) {
        CHECK(r.valid);
        CHECK_EQ(r.x0, 0);          // minX clamped from -4 to 0
        CHECK_EQ(r.x1, 20);
        CHECK_EQ(r.y0, 10);
        CHECK_EQ(r.y1, 40);
        CHECK(approx(r.uStep, 1.0f / 24.0f));
        CHECK(approx(r.uOff,  4.0f / 24.0f));   // -minX * uStep
        CHECK(approx(r.vStep, 1.0f / 30.0f));
        CHECK(approx(r.vOff,  0.0f));
    }
}

TEST(RenderLeaves7_Shadow, ClipRectClampsMaxToDimMinusOne) {
    ShadowClipRect r;
    int ok = render::ComputeShadowClipRect(64, 0, 200, 0, 200, &r);
    CHECK_EQ(ok, 1);
    if (ok) {
        CHECK_EQ(r.x1, 63);
        CHECK_EQ(r.y1, 63);
    }
}

TEST(RenderLeaves7_Shadow, ClipRectEarlyOuts) {
    ShadowClipRect r;
    // dim <= minX
    CHECK_EQ(render::ComputeShadowClipRect(10, 10, 20, 0, 5, &r), 0);
    CHECK(!r.valid);
    // maxX < 0
    CHECK_EQ(render::ComputeShadowClipRect(64, -20, -1, 0, 5, &r), 0);
    // maxX - minX <= 0
    CHECK_EQ(render::ComputeShadowClipRect(64, 30, 30, 0, 5, &r), 0);
    // maxY - minY <= 0
    CHECK_EQ(render::ComputeShadowClipRect(64, 0, 10, 20, 20, &r), 0);
    // maxY < 0
    CHECK_EQ(render::ComputeShadowClipRect(64, 0, 10, -30, -1, &r), 0);
}

TEST(RenderLeaves7_Shadow, ClipRectNegativeMinYDerivesVOff) {
    ShadowClipRect r;
    int ok = render::ComputeShadowClipRect(64, 0, 10, -6, 24, &r);
    CHECK_EQ(ok, 1);
    if (ok) {
        CHECK_EQ(r.y0, 0);
        // vOff = -minY / (maxY - minY) = 6 / 30
        CHECK(approx(r.vOff, 6.0f / 30.0f));
        // vStep = 1/(maxY - minY) = 1/30
        CHECK(approx(r.vStep, 1.0f / 30.0f));
    }
}

// ---------------------------------------------------------------------------
// 0x5f216c — ProjectGroundVertex.
// ---------------------------------------------------------------------------
TEST(RenderLeaves7_Shadow, ProjectGroundVertexMath) {
    GroundVertex v = render::ProjectGroundVertex(
        /*col*/5, /*row*/3, /*h*/128,
        /*xBase*/10.0f, /*xStep*/2.0f, /*zBase*/20.0f, /*zStep*/3.0f,
        /*yScale*/0.25f, /*groundY*/7.0f);
    CHECK(approx(v.x, 20.0f));   // 5*2 + 10
    CHECK(approx(v.z, 29.0f));   // 3*3 + 20
    CHECK(approx(v.y, 39.5f));   // 128*0.25 + (7 + 0.5)
}

// ---------------------------------------------------------------------------
// 0x5f2a58 — RasterizeHeightField helpers.
// ---------------------------------------------------------------------------
TEST(RenderLeaves7_Shadow, HeightFieldBiasSelector) {
    CHECK(approx(render::HeightFieldBias(true),  1.5f));
    CHECK(approx(render::HeightFieldBias(false), 1.0f));
}

TEST(RenderLeaves7_Shadow, RasterizeHeightVertexYMath) {
    // (200 + 1.5)*0.5 + (4 + 1.5) = 100.75 + 5.5 = 106.25
    CHECK(approx(render::RasterizeHeightVertexY(200, 1.5f, 0.5f, 4.0f), 106.25f));
}

TEST(RenderLeaves7_Shadow, EdgeDiscontinuity) {
    CHECK(!render::HeightEdgeDiscontinuous(100.0f, 110.0f, 90.0f)); // both <= 25
    CHECK(render::HeightEdgeDiscontinuous(100.0f, 130.0f, 90.0f));  // |100-130|=30>25
    CHECK(render::HeightEdgeDiscontinuous(100.0f, 110.0f, 60.0f));  // |100-60|=40>25
    CHECK(!render::HeightEdgeDiscontinuous(0.0f, 25.0f, -25.0f));   // exactly 25 -> not >
}

// ---------------------------------------------------------------------------
// 0x5c6f90 — Light_ApplyToCachedVertices kernels.
// ---------------------------------------------------------------------------
TEST(RenderLeaves7_Light, PointAttenuationInRange) {
    float vp[3] = {1.0f, 2.0f, 3.0f};
    float lp[3] = {0.0f, 0.0f, 0.0f};
    // d2 = 1+4+9 = 14; range2 = 100; atten = (1/(2*14))*5
    float a = render::PointLightAttenuation(vp, lp, 100.0f, 2.0f, 5.0f);
    CHECK(approx(a, static_cast<float>((1.0 / (2.0 * 14.0)) * 5.0)));
}

TEST(RenderLeaves7_Light, PointAttenuationOutOfRange) {
    float vp[3] = {20.0f, 0.0f, 0.0f};
    float lp[3] = {0.0f, 0.0f, 0.0f};
    // d2 = 400 >= range2 (100) -> 0
    CHECK(approx(render::PointLightAttenuation(vp, lp, 100.0f, 2.0f, 5.0f), 0.0f));
}

TEST(RenderLeaves7_Light, DirectionalFalloffSelectsIndex) {
    // Build a recognisable LUT where lut[i] = i, so we can read back the index.
    static float lut[1024];
    for (int i = 0; i < 1024; ++i) lut[i] = static_cast<float>(i);
    // NdotL = -0.5 -> idx = (int)(-0.5 * -1023) = 511; scale = 2.0 * 511
    float s = render::DirectionalFalloffScale(-0.5f, 2.0f, lut);
    CHECK(approx(s, 2.0f * 511.0f));
    // NdotL >= 0 -> 0
    CHECK(approx(render::DirectionalFalloffScale(0.25f, 2.0f, lut), 0.0f));
}

// wave-12 boundary: NdotL == -1 (a normalised back-facing dot) gives the LARGEST
// valid index, idx = (int)(-1 * -1023) = 1023, which is exactly the last entry of
// the documented 1024-entry falloff LUT — must NOT overrun (ASAN-clean read).
TEST(RenderLeaves7_Light, DirectionalFalloffMaxIndexInBounds) {
    static float lut[1024];
    for (int i = 0; i < 1024; ++i) lut[i] = static_cast<float>(i);
    float s = render::DirectionalFalloffScale(-1.0f, 3.0f, lut);
    CHECK(approx(s, 3.0f * 1023.0f));   // lut[1023], the final valid entry
    // The smallest negative just past 0 -> idx 0.
    CHECK(approx(render::DirectionalFalloffScale(-0.0009f, 1.0f, lut), 0.0f)); // lut[0]==0
    // NdotL exactly 0 (not < 0) -> early 0 branch, LUT not touched.
    CHECK(approx(render::DirectionalFalloffScale(0.0f, 9.0f, lut), 0.0f));
}

// ---------------------------------------------------------------------------
// 0x5ef19c — ComputeSkyFlarePositions kernels.
// ---------------------------------------------------------------------------
TEST(RenderLeaves7_Flare, RingAndSegSteps) {
    CHECK(approx(render::FlareRingT(0), 0.0f));
    CHECK(approx(render::FlareRingT(3), 3.0f * 0.20000000298023224f));
    CHECK(approx(render::FlareSegT(0), 0.0f));
    CHECK(approx(render::FlareSegT(7), 7.0f * 0.1428571492433548f));
}

TEST(RenderLeaves7_Flare, SunAngleFmod) {
    float up[3]  = {0.0f, 0.0f, 1.0f};   // flt_5CA2B0 = (0,0,1)
    float dir[3] = {0.0f, 0.0f, -1.0f};
    double ang = render::FlareSunAngle(up, dir);
    // VectorAngleBetween returns -pi for opposite XZ-projections (0,1)/(0,-1);
    // fmod(-pi, 2pi) = -pi (the faithful result).
    CHECK(approxd(ang, -M_PI, 1e-4));
}

TEST(RenderLeaves7_Flare, VertexAlphaBands) {
    // band > 255 -> 255 (behind-near-plane path)
    CHECK(approx(render::FlareVertexAlpha(300.0f, 50.0f), 255.0f));
    // band in [0,255] -> max(0, raw)
    CHECK(approx(render::FlareVertexAlpha(100.0f, 50.0f), 50.0f));
    CHECK(approx(render::FlareVertexAlpha(100.0f, -3.0f), 0.0f));
    // band < 0 -> still the clamped-raw path
    CHECK(approx(render::FlareVertexAlpha(-5.0f, 42.0f), 42.0f));
}

TEST(RenderLeaves7_Flare, GridIsNormalisedBilerp) {
    float c30[3] = {1.0f, 0.0f, 0.0f};   // top
    float c31[3] = {0.0f, 1.0f, 0.0f};   // right
    float c32[3] = {0.0f, 0.0f, 1.0f};   // bottom
    float c33[3] = {0.0f, -1.0f, 0.0f};  // left
    GroundVertex grid[48];
    render::ComputeFlareGrid(c30, c31, c32, c33, grid);

    // Every produced vertex must be unit length (VectorNormalize).
    for (int k = 0; k < 48; ++k) {
        float len = std::sqrt(grid[k].x * grid[k].x + grid[k].y * grid[k].y
                              + grid[k].z * grid[k].z);
        CHECK(approx(len, 1.0f, 1e-3f));
    }
    // ring 0 row: rowA = lerp(c30,c32,0) = c30 = (1,0,0);
    //             rowB = lerp(c31,c33,0) = c31 = (0,1,0).
    // seg 0 -> lerp(rowA,rowB,0) = (1,0,0) normalised.
    CHECK(approx(grid[0].x, 1.0f));
    CHECK(approx(grid[0].y, 0.0f));
    CHECK(approx(grid[0].z, 0.0f));
    // ring 0, seg 7 -> lerp((1,0,0),(0,1,0), 7/7=1) = (0,1,0).
    CHECK(approx(grid[7].x, 0.0f));
    CHECK(approx(grid[7].y, 1.0f));
}

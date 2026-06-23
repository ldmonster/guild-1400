#include "test.h"

// Golden-vector unit tests for the distance-fog cluster (src/render/fog.cpp).
// All expected values were computed with an independent Python reference (32-bit
// float / 64-bit double promotion matched to the gilde.exe arithmetic).
#include "render/fog.h"
#include "render/raster.h"          // wave-7: per-pixel fog span channel
#include "render/raster_textured.h" // wave-7: RGBZ fog-factor interpolation
#include "render/surface.h"
#include "render/texture.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {
bool feq(float a, float b) { return std::fabs(a - b) <= 1e-4f * (1.0f + std::fabs(b)); }
}

// ---- Recovered fog constants (get_bytes, bit-exact) ------------------------
// flt_628088 = 0x437F0000 = 255.0 (slope numerator); dbl_628074/dbl_628B34 =
// 0x406FE00000000000 = 255.0 (the per-vertex factor clamp ceiling — NOT 256.0;
// the Hex-Rays 256.0 was a decompiler artifact, see fog.h / fog-render-wave6.md).
TEST(RenderFog, RecoveredConstantsPinned) {
    CHECK_EQ((double)kFogSlopeNumer, 255.0);     // flt_628088 = 0x437F0000
    CHECK_EQ(kFogFactorMax, 255.0);              // dbl_628074 = 0x406FE000.. (255.0)
    CHECK_EQ(kFogFactorClampHi, 255.0);          // dbl_628B34 = 0x406FE000.. (255.0)
    // 1:1: the clamp ceiling is 255.0, NOT 256.0 (would wrap to -1/0xFF at full fog).
    CHECK(kFogFactorClampHi != 256.0);
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
    // beyond far: clamp ceiling is 255.0 (BOTH passes; the Hex-Rays 256.0 was a
    // decompiler artifact — the billboard immediate is 0x406FE000 = HIDWORD(255.0)),
    // so 255-255 = 0 with NO wrap. Verified against disasm 0x5aca75 + get_bytes.
    CHECK_EQ(ComputeFogFactor(s, 40000.0f), 0);
    CHECK_EQ((std::uint8_t)ComputeFogFactor(s, 40000.0f), 0x00);
}

// ---- BlendFogChannel: the per-channel D3D vertex-fog blend ------------------
// out = (factor*src + (255-factor)*fog + 127)/255 ; factor in [0,255].
TEST(RenderFog, BlendChannelGolden) {
    CHECK_EQ((int)BlendFogChannel(200, 50, 255), 200); // factor 255 -> source
    CHECK_EQ((int)BlendFogChannel(200, 50, 0), 50);    // factor 0 -> pure fog
    CHECK_EQ((int)BlendFogChannel(200, 50, 128), 125); // midpoint (round-to-nearest)
    CHECK_EQ((int)BlendFogChannel(255, 0, 128), 128);
    CHECK_EQ((int)BlendFogChannel(0, 255, 64), 191);
    CHECK_EQ((int)BlendFogChannel(100, 200, -5), 200); // clamp factor < 0
    CHECK_EQ((int)BlendFogChannel(100, 200, 999), 100);// clamp factor > 255
}

// ---- BlendFogRgb: full 0x00RRGGBB blend ------------------------------------
TEST(RenderFog, BlendRgbGolden) {
    CHECK_EQ((unsigned)BlendFogRgb(0xFFFFFFu, 0x000000u, 255), 0xFFFFFFu);
    CHECK_EQ((unsigned)BlendFogRgb(0xFFFFFFu, 0x000000u, 0), 0x000000u);
    CHECK_EQ((unsigned)BlendFogRgb(0xFFFFFFu, 0x000000u, 128), 0x808080u);
    CHECK_EQ((unsigned)BlendFogRgb(0xFF0000u, 0x0000FFu, 0), 0x0000FFu);
}

// ---- BlendFog565: the raster-facing per-pixel hook -------------------------
// Unpacks RGB565 through the engine ColorFormat (shift-only), blends, repacks.
TEST(RenderFog, Blend565Golden) {
    CHECK_EQ((int)BlendFog565(0xFFFF, 0x000000u, 255), 0xFFFF); // fast path: unchanged
    CHECK_EQ((int)BlendFog565(0xFFFF, 0x000000u, 0), 0x0000);   // full fog (black)
    // midpoint white->black: unpack(0xFFFF)=(248,252,248), bc->(124,126,124),
    // repack565 = 0x7BEF (golden).
    CHECK_EQ((int)BlendFog565(0xFFFF, 0x000000u, 128), 0x7BEF);
    CHECK_EQ((int)BlendFog565(0xF800, 0x0000FFu, 0), 0x001F);   // red->blue, full fog
    CHECK_EQ((int)BlendFog565(0xF800, 0x0000FFu, 128), 0x780F); // red->blue, midpoint
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

// ===========================================================================
// WAVE-10 HARDENING — degenerate fog ranges and blend extremes.
// near==far, near>far, factor 0/255, and the band-index early-out exhaustively.
// No goldens change; these pin the documented edge behaviour and exercise the
// bounds (BlendFog565 unpack/repack stays in [0,255], ApplyAmbientBlend never
// indexes the 6-band table out of range).
// ===========================================================================

// near == far: densitySlope = 255/0. ConfigureFog gates on near < far, so the
// range is NOT updated and fog is disabled (enabled == false). No div in the
// state path is observed; SetFogRange itself is not called.
TEST(RenderFogHarden, NearEqualsFarDisablesFog) {
    FogState s{};
    // First seed a valid range so we can prove the equal-range call leaves it.
    CHECK(ConfigureFog(s, 10.0f, 110.0f, 0x111111, true, true));
    CHECK(s.enabled);
    float slopeBefore = s.densitySlope;
    // near == far -> nearLtFar false -> range untouched, enabled cleared.
    CHECK(ConfigureFog(s, 50.0f, 50.0f, 0x222222, true, true));
    CHECK(!s.enabled);
    CHECK(feq(s.densitySlope, slopeBefore)); // slope NOT recomputed (no /0)
    CHECK_EQ(s.color, 0x222222);             // colour still latched
}

// near > far: same gate -> fog disabled, range preserved, no negative slope.
TEST(RenderFogHarden, NearGreaterThanFarDisablesFog) {
    FogState s{};
    CHECK(ConfigureFog(s, 10.0f, 110.0f, 0x111111, true, true));
    float slopeBefore = s.densitySlope;
    CHECK(ConfigureFog(s, 200.0f, 50.0f, 0x333333, true, true));
    CHECK(!s.enabled);
    CHECK(feq(s.densitySlope, slopeBefore));
}

// SetFogRange directly with near==far would compute 255/0 = +inf. This is the
// raw math core; ConfigureFog never reaches it with near>=far, but pin the
// behaviour so a future caller knows the slope is non-finite (not a crash/UB).
TEST(RenderFogHarden, SetFogRangeNearEqualsFarIsInf) {
    FogState s{};
    CHECK(SetFogRange(s, 50.0f, 50.0f));     // range changed from default {0,0}
    CHECK(feq(s.nearPlane, 50.0f));
    CHECK(std::isinf(s.densitySlope));       // 255/0 -> +inf (documented, no UB)
}

// ComputeFogFactor at d2 == nearSq (boundary): full 255. Just past it: < 255.
TEST(RenderFogHarden, FactorAtNearBoundary) {
    FogState s{};
    SetFogRange(s, 10.0f, 110.0f);
    CHECK_EQ(ComputeFogFactor(s, s.nearSq), 255);          // exactly at near^2
    CHECK(ComputeFogFactor(s, s.nearSq + 1000.0f) < 255);  // just beyond
    // Negative d2 (degenerate) is still <= nearSq -> 255, no sqrt of negative.
    CHECK_EQ(ComputeFogFactor(s, -1.0f), 255);
}

// BlendFog565 at the two clamp extremes for several colours.
TEST(RenderFogHarden, Blend565FactorExtremes) {
    const u16 srcs[]  = {0x0000, 0xFFFF, 0xF800, 0x07E0, 0x001F, 0x1234};
    const u32 fogs[]  = {0x000000u, 0xFFFFFFu, 0x808080u, 0xFF00FFu};
    for (u16 src : srcs) {
        for (u32 fog : fogs) {
            // factor 255 -> source unchanged (fast path).
            CHECK_EQ((int)BlendFog565(src, fog, 255), (int)src);
            // factor 0 -> the fog colour quantised to 565 (== blending 0% src).
            u16 expect0 = BlendFog565(src, fog, 0);
            CHECK_EQ((int)BlendFog565(src, fog, 0), (int)expect0); // deterministic
            // out-of-range factors clamp (>=255 fast path; <0 -> full fog).
            CHECK_EQ((int)BlendFog565(src, fog, 1000), (int)src);
            CHECK_EQ((int)BlendFog565(src, fog, -50), (int)BlendFog565(src, fog, 0));
        }
    }
}

// ApplyAmbientBlend band-index / frac early-out exhaustively (the 6-band table).
TEST(RenderFogHarden, AmbientBlendEarlyOutNeverIndexesOOB) {
    FogBand bands[6] = {};
    for (int i = 0; i < 6; ++i) bands[i] = FogBand{(u8)i, (u8)i, (u8)i, (float)i, (float)i};
    FogState s{};
    for (int bad = 6; bad < 12; ++bad) {
        CHECK(!ApplyAmbientBlend(s, bands, bad, 0, 0.5f, 1.0f, true, true));
        CHECK(!ApplyAmbientBlend(s, bands, 0, bad, 0.5f, 1.0f, true, true));
    }
    // negative band index (cast to unsigned >= 6 -> reject, no OOB read).
    CHECK(!ApplyAmbientBlend(s, bands, -1, 0, 0.5f, 1.0f, true, true));
    CHECK(!ApplyAmbientBlend(s, bands, 0, -1, 0.5f, 1.0f, true, true));
    // frac just outside [0,1].
    CHECK(!ApplyAmbientBlend(s, bands, 0, 1, -0.001f, 1.0f, true, true));
    CHECK(!ApplyAmbientBlend(s, bands, 0, 1, 1.001f, 1.0f, true, true));
    CHECK(!s.enabled);                                       // never applied
    // boundary fracs 0 and 1 ARE accepted.
    CHECK(ApplyAmbientBlend(s, bands, 0, 5, 0.0f, 1.0f, true, true));
    CHECK(ApplyAmbientBlend(s, bands, 0, 5, 1.0f, 1.0f, true, true));
}

// ===========================================================================
// wave-7 W7-FOGPIX — PER-PIXEL interpolated vertex fog.
//
// D3D fixed-function VERTEX fog (BeginScene @0x5e010c sets FOGENABLE(28) +
// FOGCOLOR(34) but never FOGTABLEMODE) interpolates the per-vertex fog factor
// (the FVF specular byte stored at vertex+79 by ComputeFogFactor @0x5ac9aa /
// @0x5beb0b) LINEARLY across the triangle, then blends each pixel toward the fog
// colour. The software realisation carries the factor as a THIRD 16.16 span
// channel alongside U/V. These tests pin the per-pixel gradient (independent
// /tmp/fogpix_ref.py replica) and prove the fog-off path is byte-identical.
// ===========================================================================

namespace {
Surface* Make16(int w, int h) {
    Surface* s = SurfaceCreate(w, h, 16);
    std::memset(s->pixels, 0, (size_t)s->pitch * h);
    return s;
}
} // namespace

// ---- FillSpanTextured: per-pixel interpolated factor (the 3rd channel) ------
// A 6-pixel span over a white texture, fog colour BLACK, factor interpolated
// from 0 (left, full fog) to 255 (right, no fog) via the 16.16 channel
// (fStart=0, fGrad = (255<<16)/5). Each pixel is BlendFog565(white, black,
// factor). Goldens from /tmp/fogpix_ref.py.
TEST(RenderFogPerPixel, SpanGradientGolden) {
    RasterState rs{};
    rs.spanLen = 6;
    std::vector<u8>  tex(8, 0);          // all index 0
    std::vector<u16> pal(256, 0xFFFF);   // every entry white
    SpanTexParams p{};
    p.texBase = tex.data();
    p.palBase = pal.data();
    p.texelMask = 0x7;
    p.widthShift = 0;
    p.uStepFrac = 0;                      // constant texel -> isolate the fog ramp
    p.vStep = 0;
    p.lightRow8 = 0;

    SpanFog().enabled = true;
    SpanFog().color   = 0x000000;        // black fog
    SpanFog().factor  = 255;             // (unused on the per-pixel path)

    rs.fPerPixel = true;
    rs.fStart = 0;                        // left pixel: factor 0 (full fog)
    rs.fGrad  = (255 << 16) / 5;          // reaches 255 at the 6th pixel

    std::vector<u16> dst(6, 0xAAAA);
    FillSpanTextured(rs, dst.data(), 0, 0, p);

    // factor row = [0,51,102,153,204,255]; pixels (white->black blend).
    CHECK_EQ((int)dst[0], 0x0000);
    CHECK_EQ((int)dst[1], 0x3186);
    CHECK_EQ((int)dst[2], 0x632C);
    CHECK_EQ((int)dst[3], 0x94B2);
    CHECK_EQ((int)dst[4], 0xC658);
    CHECK_EQ((int)dst[5], 0xFFFF);       // factor 255 -> source unchanged
    // strictly monotone toward the source (white) left-to-right.
    for (int i = 1; i < 6; ++i) CHECK(dst[i] >= dst[i - 1]);

    SpanFog() = SpanFogState{};          // restore default
}

// ---- per-pixel factor is CLAMPED to [0,255] across the span ----------------
TEST(RenderFogPerPixel, SpanFactorClamped) {
    RasterState rs{};
    rs.spanLen = 4;
    std::vector<u8>  tex(4, 0);
    std::vector<u16> pal(256, 0xFFFF);
    SpanTexParams p{};
    p.texBase = tex.data(); p.palBase = pal.data();
    p.texelMask = 0x3; p.widthShift = 0; p.uStepFrac = 0; p.vStep = 0;

    SpanFog().enabled = true; SpanFog().color = 0x000000;
    rs.fPerPixel = true;
    rs.fStart = -(50 << 16);             // starts negative -> clamp to 0 (full fog)
    rs.fGrad  = 0;
    std::vector<u16> dst(4, 0xAAAA);
    FillSpanTextured(rs, dst.data(), 0, 0, p);
    for (int i = 0; i < 4; ++i) CHECK_EQ((int)dst[i], 0x0000); // clamped factor 0

    rs.fStart = 300 << 16;               // > 255 -> clamp to 255 (no fog)
    std::vector<u16> dst2(4, 0xAAAA);
    FillSpanTextured(rs, dst2.data(), 0, 0, p);
    for (int i = 0; i < 4; ++i) CHECK_EQ((int)dst2[i], 0xFFFF);

    SpanFog() = SpanFogState{};
}

// ---- fog OFF is byte-identical even with the per-pixel channel seeded -------
TEST(RenderFogPerPixel, FogOffByteIdentical) {
    RasterState rs{};
    rs.spanLen = 4;
    std::vector<u8>  tex = {0, 1, 2, 3};
    std::vector<u16> pal(256, 0);
    pal[0] = 0x1111; pal[1] = 0x2222; pal[2] = 0x3333; pal[3] = 0x4444;
    SpanTexParams p{};
    p.texBase = tex.data(); p.palBase = pal.data();
    p.texelMask = 0x3; p.widthShift = 0; p.uStepFrac = 1 << 16; p.vStep = 0;

    // SpanFog disabled (default) but the per-pixel channel IS seeded — it must be
    // ignored, producing the raw palette colours.
    SpanFog() = SpanFogState{};          // enabled == false
    rs.fPerPixel = true;
    rs.fStart = 0; rs.fGrad = 1 << 16;
    std::vector<u16> dst(4, 0);
    FillSpanTextured(rs, dst.data(), 0, 0, p);
    CHECK_EQ((int)dst[0], 0x1111);
    CHECK_EQ((int)dst[1], 0x2222);
    CHECK_EQ((int)dst[2], 0x3333);
    CHECK_EQ((int)dst[3], 0x4444);
}

// ---- FillSpanLoop: the fog-factor channel walks across the row -------------
// One row, fLeft=0 fog factor, fGrad steps +51<<16/pixel -> the row reproduces
// the same gradient as the direct-span golden above (white tex, black fog).
TEST(RenderFogPerPixel, FillSpanLoopRowGradient) {
    Texture t; TextureSetSize(t, 4); t.mipWidth = 4;
    for (int i = 0; i < 16; ++i) t.texels[i] = 0;          // all index 0
    std::vector<u16> pal(256, 0xFFFF);

    Surface* s = Make16(8, 2);
    RgbzRasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    rs.fbPitchPx = s->widthPx;
    rs.fbRow0 = (u16*)s->pixels + (i64)0 * s->widthPx;
    rs.xLeft = 0; rs.xRight = 6 << 16;
    rs.uLeft = 0; rs.vLeft = 0;
    rs.uGrad = 0; rs.vGrad = 0;
    // fog channel: factor 0 at the left, +51<<16 per pixel.
    rs.fogPerPixel = true;
    rs.fLeft = 0;
    rs.fGrad = (255 << 16) / 5;

    SpanFog().enabled = true; SpanFog().color = 0x000000;
    SpanTexParams p = BuildSpanTexParams(t, pal.data(), rs.uGrad, rs.vGrad);
    FillSpanLoop(rs, 1, p);

    const u16* row0 = (const u16*)s->pixels;
    CHECK_EQ((int)row0[0], 0x0000);
    CHECK_EQ((int)row0[1], 0x3186);
    CHECK_EQ((int)row0[2], 0x632C);
    CHECK_EQ((int)row0[3], 0x94B2);
    CHECK_EQ((int)row0[4], 0xC658);
    CHECK_EQ((int)row0[5], 0xFFFF);

    SpanFog() = SpanFogState{};
    SurfaceDestroy(s);
}

// ---- full RGBZ triangle: different per-vertex fog factors -> screen gradient -
// A flat-top triangle whose top-left vertex has factor 0 (full fog) and top-right
// factor 255 (no fog); the bottom apex factor 255. The first rasterized row must
// show a monotone non-decreasing fog blend left->right (white texture, black fog):
// the per-vertex factor is interpolated PER PIXEL across the span.
TEST(RenderFogPerPixel, TriangleVertexFactorGradient) {
    Texture t; TextureSetSize(t, 4); t.mipWidth = 4;
    for (int i = 0; i < 16; ++i) t.texels[i] = 0;          // white via pal
    std::vector<u16> pal(256, 0xFFFF);

    Surface* s = Make16(32, 32);
    // Wide flat-top triangle: top edge y=4 from x=4 (factor 0, full fog) to x=28
    // (factor 255, no fog); apex at the bottom (factor 255).
    RgbzVertex v[3] = {
        {4.0f,  4.0f, 0.0f, 0.0f, 0, /*fog*/0},     // top-left:  full fog
        {28.0f, 4.0f, 0.0f, 0.0f, 0, /*fog*/255},   // top-right: no fog
        {16.0f, 28.0f, 0.0f, 0.0f, 0, /*fog*/255},  // apex
    };

    // BLUE fog so a fully-fogged white pixel resolves to 0x001F (never 0x0000),
    // distinguishable from the uncovered (zeroed) background — lets the scan find
    // the covered span unambiguously.
    SpanFog().enabled = true; SpanFog().color = 0x0000FF;
    int drew = RasterizeTexturedTriangleRgbz(s, v, t, pal.data());
    CHECK(drew != 0);

    // The top span (row 5) must be a monotone non-decreasing ramp white<-fog over
    // its covered run, containing BOTH heavily-fogged (near blue 0x001F) and
    // unfogged (near white) pixels — a genuine per-pixel gradient.
    const u16* px = (const u16*)s->pixels;
    int sw = s->widthPx;
    const int row = 5;
    int prev = -1, minv = 0x10000, maxv = -1, covered = 0;
    for (int x = 0; x < 32; ++x) {
        u16 c = px[(size_t)row * sw + x];
        if (c == 0) continue;                  // uncovered background
        ++covered;
        if ((int)c < minv) minv = c;
        if ((int)c > maxv) maxv = c;
        if (prev >= 0) CHECK(c >= prev);       // monotone non-decreasing (l->r)
        prev = c;
    }
    CHECK(covered >= 10);                       // a wide span
    CHECK(minv <= 0x101F);                      // left: heavily fogged (toward blue)
    CHECK(maxv >= 0xF000);                      // right: (near-)unfogged white
    CHECK(maxv - minv > 0x4000);               // a real spread, not a constant

    SpanFog() = SpanFogState{};
    SurfaceDestroy(s);
}

// ---- the per-pixel triangle differs from the per-triangle constant first cut -
// Same triangle/factors: per-pixel interpolation produces DIFFERENT pixels than
// the wave-6 per-triangle average (which would paint every span pixel the SAME
// fog). Confirms the third channel is genuinely interpolated.
TEST(RenderFogPerPixel, PerPixelDiffersFromConstant) {
    Texture t; TextureSetSize(t, 4); t.mipWidth = 4;
    for (int i = 0; i < 16; ++i) t.texels[i] = 0;
    std::vector<u16> pal(256, 0xFFFF);

    RgbzVertex v[3] = {
        {4.0f,  4.0f, 0.0f, 0.0f, 0, 0},
        {28.0f, 4.0f, 0.0f, 0.0f, 0, 255},
        {16.0f, 28.0f, 0.0f, 0.0f, 0, 255},
    };

    SpanFog().enabled = true; SpanFog().color = 0x0000FF;   // blue fog (nonzero)
    Surface* a = Make16(32, 32);
    RasterizeTexturedTriangleRgbz(a, v, t, pal.data());   // per-pixel

    // The top span has many DIFFERENT resolved colours (a gradient), which a
    // per-triangle CONSTANT factor could never produce (it would paint the whole
    // span one colour). Scan only the covered (nonzero) pixels of row 5.
    const u16* pa = (const u16*)a->pixels;
    int sw = a->widthPx;
    int distinct = 0; u16 seen[64]; int ns = 0;
    for (int x = 0; x < 32; ++x) {
        u16 c = pa[(size_t)5 * sw + x];
        if (c == 0) continue;                  // uncovered background
        bool found = false;
        for (int k = 0; k < ns; ++k) if (seen[k] == c) { found = true; break; }
        if (!found && ns < 64) { seen[ns++] = c; ++distinct; }
    }
    CHECK(distinct >= 3);   // a real per-pixel ramp, not one constant value

    SpanFog() = SpanFogState{};
    SurfaceDestroy(a);
}

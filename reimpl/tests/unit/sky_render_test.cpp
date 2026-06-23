// Golden-vector unit tests for the sky backdrop reconstruction.
//
// Covers:
//   * VIBE_SkyColor_BlendBandLighting  @0x5b85e4 (ambient triple + luma)
//   * VIBE_SkyColor_ApplyScaledBlend   @0x43f460 (brightness -> [0,1] alpha)
//   * VIBE_SkyColor_ApplyAmbientBlend  @0x5b8b04 (cross-band sky/fog colour)
//   * RenderSky surface-fill (the engine's pre-terrain clear to dword_649DD4)
//
// Vectors are derived directly from the gilde.exe decompile/disasm (reference of
// record). The packed-byte channel lerp uses VIBE_Coord_ConvertX (chop toward
// zero); the byte values are reconstructed by hand and checked here.
#include "tests/framework/test.h"
#include "render/sky.h"
#include "render/skycolor_recon.h"
#include "render/scene_load.h"
#include "render/types.h"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {
bool close(float a, float b) { return std::fabs(a - b) < 1e-5f; }

u32 Pack(int b2, int b1, int b0) {
    return ((u32)(b0 & 0xFF)) | ((u32)(b1 & 0xFF) << 8) | ((u32)(b2 & 0xFF) << 16);
}
} // namespace

// --- Recovered constant pins (get_bytes values; must stay byte-identical) ----
// These literal-value goldens pin the recovered IEEE-754 constants the band/fog
// blends @0x5b85e4 / @0x5b8b04 / @0x43f460 use, independent of the lerp math that
// merely *consumes* them. A drift in any of these would silently rescale the
// time-of-day sky/fog colour without tripping the lerp tests above.
TEST(SkyRender, RecoveredLumaWeightConstants) {
    // luma = g*flt_628728 + r*flt_62872C + b*flt_628730 (the band-lighting luma
    // weights, recovered byte-for-byte: 0.59 / 0.30 / 0.11).
    CHECK(close(kSkyLumaG, 0.5899999737739563f));   // flt_628728
    CHECK(close(kSkyLumaR, 0.30000001192092896f));  // flt_62872C
    CHECK(close(kSkyLumaB, 0.10999999940395355f));  // flt_628730
    CHECK(close(kSkyBandMid, 0.5f));                // flt_628724
    CHECK(close(kBrightScale, 0.009999999776482582f)); // flt_61752C = 1/100
    CHECK(close(kFogRangeScale, 1.0f));            // flt_64A018 = 0x3F800000
}

// --- ScaledBlendAlpha (0x43f460) --------------------------------------------
TEST(SkyRender, ScaledBlendClampsLow) {
    CHECK(close(ScaledBlendAlpha(0), 0.0f));
    CHECK(close(ScaledBlendAlpha(-5), 0.0f));
}
TEST(SkyRender, ScaledBlendClampsHigh) {
    CHECK(close(ScaledBlendAlpha(100), 1.0f));
    CHECK(close(ScaledBlendAlpha(250), 1.0f));
}
TEST(SkyRender, ScaledBlendMidpoint) {
    CHECK(close(ScaledBlendAlpha(50), 0.5f));
    CHECK(close(ScaledBlendAlpha(37), 0.37f));
}

// --- BlendBandLighting (0x5b85e4) -------------------------------------------
TEST(SkyRender, BandLightingRejectsOutOfRange) {
    SkyBandColor bands[kSkyBands] = {};
    CHECK(BlendBandLighting(bands, 7, 0.5f, 1.0f).luma == 0.0f);
    CHECK(BlendBandLighting(bands, 0, -0.1f, 1.0f).r == 0.0f);
    CHECK(BlendBandLighting(bands, 0, 1.1f, 1.0f).b == 0.0f);
}
TEST(SkyRender, BandLightingLerpAndLuma) {
    SkyBandColor bands[kSkyBands] = {};
    bands[0] = {0.2f, 0.4f, 0.6f};
    bands[1] = {0.6f, 0.8f, 1.0f};
    // t=0.5, scale=2.0 -> midpoint *2
    SkyAmbient a = BlendBandLighting(bands, 0, 0.5f, 2.0f);
    CHECK(close(a.r, 0.4f * 2.0f));
    CHECK(close(a.g, 0.6f * 2.0f));
    CHECK(close(a.b, 0.8f * 2.0f));
    // luma = g*0.59 + r*0.30 + b*0.11 (flt_628728/2C/30)
    float expect = a.g * kSkyLumaG + a.r * kSkyLumaR + a.b * kSkyLumaB;
    CHECK(close(a.luma, expect));
}
TEST(SkyRender, BandLightingWrapsModSeven) {
    // a=6 -> b=(6+1)%7=0 : lerp band6 -> band0
    SkyBandColor bands[kSkyBands] = {};
    bands[6] = {1.0f, 0.0f, 0.0f};
    bands[0] = {0.0f, 0.0f, 1.0f};
    SkyAmbient a = BlendBandLighting(bands, 6, 1.0f, 1.0f); // full -> band0
    CHECK(close(a.r, 0.0f));
    CHECK(close(a.b, 1.0f));
}

// --- BlendAmbientFog (0x5b8b04) ---------------------------------------------
TEST(SkyRender, AmbientFogRejectsOutOfRange) {
    SkyFogBand bands[6] = {};
    CHECK(!BlendAmbientFog(bands, 6, 0, 0.5f).applied);
    CHECK(!BlendAmbientFog(bands, 0, 6, 0.5f).applied);
    CHECK(!BlendAmbientFog(bands, 0, 1, -0.01f).applied);
    CHECK(!BlendAmbientFog(bands, 0, 1, 1.01f).applied);
    CHECK_EQ(BlendAmbientFog(bands, 7, 0, 0.5f).color, 0u);
}

TEST(SkyRender, AmbientFogEndpointsAreExact) {
    SkyFogBand bands[6] = {};
    bands[0] = {Pack(10, 20, 30), 4.0f, 400.0f};
    bands[3] = {Pack(200, 100, 50), 8.0f, 800.0f};
    // frac=0 -> exactly band a
    SkyFog at0 = BlendAmbientFog(bands, 0, 3, 0.0f);
    CHECK(at0.applied);
    CHECK_EQ(at0.color, Pack(10, 20, 30));
    CHECK(close(at0.near_, 4.0f));
    CHECK(close(at0.far_, 400.0f));
    // frac=1 -> exactly band b
    SkyFog at1 = BlendAmbientFog(bands, 0, 3, 1.0f);
    CHECK_EQ(at1.color, Pack(200, 100, 50));
    CHECK(close(at1.near_, 8.0f));
    CHECK(close(at1.far_, 800.0f));
}

TEST(SkyRender, AmbientFogMidpointChannelTruncates) {
    SkyFogBand bands[6] = {};
    // Choose bytes whose midpoint is fractional so chop-toward-zero is observable.
    // B2: (10 + 13)/2 = 11.5 -> trunc 11
    // B1: (20 + 25)/2 = 22.5 -> trunc 22
    // B0: (30 + 31)/2 = 30.5 -> trunc 30
    bands[1] = {Pack(10, 20, 30), 2.0f, 200.0f};
    bands[2] = {Pack(13, 25, 31), 6.0f, 600.0f};
    SkyFog m = BlendAmbientFog(bands, 1, 2, 0.5f);
    CHECK_EQ(m.color, Pack(11, 22, 30));
    CHECK(close(m.near_, 4.0f));    // (2+6)/2 * 1.0
    CHECK(close(m.far_, 400.0f));   // (200+600)/2 * 1.0
    // SkyFogColor convenience must match
    CHECK_EQ(SkyFogColor(bands, 1, 2, 0.5f), m.color);
}

// 1:1 algebraic-form pin (the load-bearing case). 0x5b8b04 computes the colour
// channels as (B-A)*frac + A — NOT a*(1-frac)+b*frac. For equal endpoints those
// forms truncate differently in IEEE float: with A==B the (B-A)*frac+A form is
// EXACTLY A (the difference is 0), whereas a*(1-t)+b*t can underflow to A-1
// (e.g. 1*0.95 + 1*0.05 == 0.9999.. -> trunc 0). The engine never loses a flat
// channel, so the colour must come out unchanged whenever the two bands match.
TEST(SkyRender, AmbientFogEqualEndpointsNoLoss) {
    SkyFogBand bands[6] = {};
    bands[0] = {Pack(1, 2, 3), 4.0f, 400.0f};
    bands[1] = {Pack(1, 2, 3), 4.0f, 400.0f}; // identical colour to band 0
    // Every frac in (0,1) must reproduce the colour exactly (no -1 underflow).
    for (float t = 0.05f; t < 1.0f; t += 0.05f) {
        SkyFog r = BlendAmbientFog(bands, 0, 1, t);
        CHECK(r.applied);
        CHECK_EQ(r.color, Pack(1, 2, 3));
    }
    // And a genuinely fractional cross with the binary's (B-A)*frac+A rounding:
    // A=1,B=11,t=0.1 -> (10)*0.1+1 = 2.0 -> 2 (the a*(1-t)+b*t form yields 1).
    SkyFogBand b2[6] = {};
    b2[0] = {Pack(1, 1, 1), 0.0f, 0.0f};
    b2[1] = {Pack(11, 11, 11), 0.0f, 0.0f};
    CHECK_EQ(BlendAmbientFog(b2, 0, 1, 0.1f).color, Pack(2, 2, 2));
}

// --- RenderSky surface fill --------------------------------------------------
namespace {
Surface MakeSurface(std::vector<u8>& buf, int w, int h, int bpp) {
    Surface s{};
    s.width = w; s.height = h; s.bpp = (u8)bpp;
    s.widthPx = w;
    s.pitch = (bpp >> 3) * w;
    s.clipX0 = 0; s.clipY0 = 0; s.clipX1 = w; s.clipY1 = h;
    buf.assign((size_t)s.pitch * h, 0xAB);
    s.pixels = buf.data();
    s.fmt = Format565();
    return s;
}
} // namespace

TEST(SkyRender, RenderSkyFills16bpp) {
    std::vector<u8> buf;
    Surface s = MakeSurface(buf, 8, 4, 16);
    RenderSky(&s, 0x1234FFFFu); // only low 16 bits used
    u16* px = reinterpret_cast<u16*>(s.pixels);
    for (int i = 0; i < 8 * 4; ++i) CHECK_EQ(px[i], (u16)0xFFFF);
}

TEST(SkyRender, RenderSkyFills32bpp) {
    std::vector<u8> buf;
    Surface s = MakeSurface(buf, 5, 3, 32);
    RenderSky(&s, 0x00112233u);
    u32* px = reinterpret_cast<u32*>(s.pixels);
    for (int i = 0; i < 5 * 3; ++i) CHECK_EQ(px[i], 0x00112233u);
}

TEST(SkyRender, RenderSkyRespectsClipRect) {
    std::vector<u8> buf;
    Surface s = MakeSurface(buf, 6, 6, 16);
    s.clipX0 = 1; s.clipY0 = 1; s.clipX1 = 4; s.clipY1 = 5; // inner box
    RenderSky(&s, 0x00007777u);
    u16* px = reinterpret_cast<u16*>(s.pixels);
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 6; ++x) {
            bool inside = (x >= 1 && x < 4 && y >= 1 && y < 5);
            u16 expect = inside ? (u16)0x7777 : (u16)0xABAB;
            CHECK_EQ(px[y * 6 + x], expect);
        }
    }
}

TEST(SkyRender, RenderSkyNullSafe) {
    RenderSky(nullptr, 0x1234u);
    Surface s{};
    s.pixels = nullptr;
    RenderSky(&s, 0x1234u); // no crash
    CHECK(true);
}

// =============================================================================
// WAVE-10 HARDENING edge cases (ASAN/UBSAN bounds exercise).
// Degenerate surfaces (zero/1-px/odd-pitch), out-of-range band/frac early-outs,
// and the per-channel blend extremes. No goldens change — these only assert the
// reconstruction stays in-bounds and honours the documented early-outs.
// =============================================================================

// RenderSky on a zero-size surface must touch nothing (no OOB write past a
// zero-length buffer). The clip is left at the Create default (x1=w=0).
TEST(SkyRender, RenderSkyZeroSize) {
    std::vector<u8> buf;
    Surface s = MakeSurface(buf, 0, 0, 16);   // empty buffer
    RenderSky(&s, 0x00001234u);               // must not write a single byte
    CHECK(buf.empty());
}

// Width 0 but height > 0 (an empty column): the row stride is 0, no pixel exists.
TEST(SkyRender, RenderSkyZeroWidthNonZeroHeight) {
    Surface s{};
    s.width = 0; s.height = 4; s.bpp = 16; s.widthPx = 0; s.pitch = 0;
    s.clipX0 = 0; s.clipY0 = 0; s.clipX1 = 0; s.clipY1 = 4;
    u8 sentinel = 0xCD;
    s.pixels = &sentinel;                     // 1-byte buffer, must stay untouched
    s.fmt = Format565();
    RenderSky(&s, 0x0000ABCDu);
    CHECK_EQ((int)sentinel, 0xCD);
}

// A single 1x1 pixel surface — the smallest in-bounds fill.
TEST(SkyRender, RenderSkyOnePixel16) {
    std::vector<u8> buf;
    Surface s = MakeSurface(buf, 1, 1, 16);
    RenderSky(&s, 0x0000BEEFu);
    CHECK_EQ(*reinterpret_cast<u16*>(s.pixels), (u16)0xBEEF);
}
TEST(SkyRender, RenderSkyOnePixel32) {
    std::vector<u8> buf;
    Surface s = MakeSurface(buf, 1, 1, 32);
    RenderSky(&s, 0x0A0B0C0Du);
    CHECK_EQ(*reinterpret_cast<u32*>(s.pixels), 0x0A0B0C0Du);
}

// Odd pixel-stride: widthPx (the row stride) is wider than the drawn width, so
// the fill must stop at `width` and never spill into the padding columns. ASAN
// would flag a write past the row if the loop used widthPx as the bound.
TEST(SkyRender, RenderSkyOddPitchStopsAtWidth) {
    const int w = 5, h = 3, strideP = 7;      // 2 padding pixels per row
    Surface s{};
    s.width = w; s.height = h; s.bpp = 16;
    s.widthPx = strideP; s.pitch = strideP * 2;
    s.clipX0 = 0; s.clipY0 = 0; s.clipX1 = w; s.clipY1 = h;
    std::vector<u16> px((size_t)strideP * h, 0xABAB);
    s.pixels = reinterpret_cast<u8*>(px.data());
    s.fmt = Format565();
    RenderSky(&s, 0x00007777u);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < strideP; ++x) {
            u16 expect = (x < w) ? (u16)0x7777 : (u16)0xABAB; // padding untouched
            CHECK_EQ(px[(size_t)y * strideP + x], expect);
        }
    }
}

// 24bpp fill: three bytes per pixel, clipped, must not run past width*3.
TEST(SkyRender, RenderSky24bpp) {
    std::vector<u8> buf;
    Surface s = MakeSurface(buf, 3, 2, 24);
    RenderSky(&s, 0x00112233u);               // b0=0x33 b1=0x22 b2=0x11
    for (int i = 0; i < 3 * 2; ++i) {
        CHECK_EQ((int)buf[i * 3 + 0], 0x33);
        CHECK_EQ((int)buf[i * 3 + 1], 0x22);
        CHECK_EQ((int)buf[i * 3 + 2], 0x11);
    }
}

// Clip rect entirely outside the surface bounds -> clamped away -> no write.
TEST(SkyRender, RenderSkyClipOutOfBounds) {
    std::vector<u8> buf;
    Surface s = MakeSurface(buf, 4, 4, 16);
    s.clipX0 = 10; s.clipY0 = 10; s.clipX1 = 20; s.clipY1 = 20; // off-surface
    RenderSky(&s, 0x00007777u);
    u16* px = reinterpret_cast<u16*>(s.pixels);
    for (int i = 0; i < 4 * 4; ++i) CHECK_EQ(px[i], (u16)0xABAB); // untouched
}

// Negative clip origin is clamped to 0 (no negative indexing under ASAN).
TEST(SkyRender, RenderSkyNegativeClipClamped) {
    std::vector<u8> buf;
    Surface s = MakeSurface(buf, 4, 4, 16);
    s.clipX0 = -5; s.clipY0 = -5; s.clipX1 = 2; s.clipY1 = 2;
    RenderSky(&s, 0x00005555u);
    u16* px = reinterpret_cast<u16*>(s.pixels);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            bool inside = (x < 2 && y < 2);
            CHECK_EQ(px[y * 4 + x], inside ? (u16)0x5555 : (u16)0xABAB);
        }
}

// --- BlendAmbientFog band-index / frac early-out exhaustively ----------------
// The documented reject path (a>=6 || b>=6 || frac<0 || frac>1) must never index
// the 6-entry band table out of range. Drive a battery of bad indices/fracs.
TEST(SkyRender, AmbientFogEarlyOutNeverIndexesOOB) {
    SkyFogBand bands[6] = {};
    for (int i = 0; i < 6; ++i) bands[i] = {Pack(i, i, i), (float)i, (float)i};
    // a or b out of [0,6): rejected, color 0, applied false.
    for (unsigned bad = 6; bad < 12; ++bad) {
        CHECK(!BlendAmbientFog(bands, bad, 0, 0.5f).applied);
        CHECK(!BlendAmbientFog(bands, 0, bad, 0.5f).applied);
        CHECK_EQ(BlendAmbientFog(bands, bad, bad, 0.5f).color, 0u);
    }
    // huge index (would be a massive OOB if not guarded).
    CHECK(!BlendAmbientFog(bands, 0xFFFFFFFFu, 0, 0.5f).applied);
    // frac just outside [0,1] on both ends.
    CHECK(!BlendAmbientFog(bands, 0, 1, -0.0001f).applied);
    CHECK(!BlendAmbientFog(bands, 0, 1, 1.0001f).applied);
    // exact boundary fracs 0 and 1 ARE accepted (inclusive range).
    CHECK(BlendAmbientFog(bands, 0, 1, 0.0f).applied);
    CHECK(BlendAmbientFog(bands, 0, 1, 1.0f).applied);
}

// BlendBandLighting (7-band table): every season-style index 0..6 stays in
// bounds (the wrap to (a+1)%7 must read band 0 when a==6, not band 7).
TEST(SkyRender, BandLightingAllIndicesInBounds) {
    SkyBandColor bands[kSkyBands];
    for (int i = 0; i < kSkyBands; ++i)
        bands[i] = {(float)i, (float)(i + 1), (float)(i + 2)};
    for (int a = 0; a < kSkyBands; ++a) {
        SkyAmbient r = BlendBandLighting(bands, a, 0.5f, 1.0f);
        // luma is the weighted sum of the three lerped channels — finite, sane.
        CHECK(r.luma == r.luma); // not NaN
    }
    // a == 7 is the documented reject (returns zero-filled, no band[7] read).
    CHECK_EQ(BlendBandLighting(bands, 7, 0.5f, 1.0f).luma, 0.0f);
    CHECK_EQ(BlendBandLighting(bands, 1000, 0.5f, 1.0f).r, 0.0f);
}

// LerpChannelTrunc at the blend extremes (factor 0 / 1) returns the endpoints
// exactly (truncate-toward-zero of an integer is the integer).
TEST(SkyRender, LerpChannelTruncEndpoints) {
    CHECK_EQ(LerpChannelTrunc(10, 200, 0.0f), 10);
    CHECK_EQ(LerpChannelTrunc(10, 200, 1.0f), 200);
    CHECK_EQ(LerpChannelTrunc(-5, 5, 0.0f), -5);   // signed-16 channel
    // negative blended value truncates toward zero, not floor.
    CHECK_EQ(LerpChannelTrunc(0, -3, 0.5f), -1);   // -1.5 -> -1 (toward zero)
}

// --- LoadSkyBands degenerate scenes -----------------------------------------
// 0 bands, > max bands (clamped to 7), and a truncated keyframe list (< 6).
TEST(SkyBands, LoadSkyBandsZeroBands) {
    SceneHeader h; h.tag = 0x3A6C00BBu;          // no lights at all
    SkySceneTable t = LoadSkyBands(h);
    CHECK_EQ(t.band_count, 0);
    // Every band stays zero-initialised (the BSS zero-init the loader leaves).
    for (int i = 0; i < kSkyBandCount; ++i)
        for (int k = 0; k < kSkyKeyframes; ++k)
            CHECK_EQ(t.fog[i][k].packed, 0u);
}
TEST(SkyBands, LoadSkyBandsClampsMaxBands) {
    SceneHeader h; h.tag = 0x3A6C00BBu;
    h.lights.resize(20);                          // more than the 7-band table
    for (int i = 0; i < 20; ++i) {
        h.lights[i].pos = {(float)i, 0.0f, 0.0f};
        h.lights[i].keyframes.push_back({(u32)i, (float)i, (float)i});
    }
    SkySceneTable t = LoadSkyBands(h);
    CHECK_EQ(t.band_count, kSkyBandCount);        // clamped to 7
    CHECK(close(t.ambient[6][0], 6.0f));          // last in-range band kept
    CHECK_EQ(t.fog[6][0].packed, 6u);
}
TEST(SkyBands, LoadSkyBandsTruncatedKeyframes) {
    SceneHeader h; h.tag = 0x3A6C00BBu;
    h.lights.resize(1);
    // Only 2 keyframes provided (the file truncated) — the other 4 stay zero.
    h.lights[0].keyframes.push_back({0x111u, 1.0f, 10.0f});
    h.lights[0].keyframes.push_back({0x222u, 2.0f, 20.0f});
    SkySceneTable t = LoadSkyBands(h);
    CHECK_EQ(t.band_count, 1);
    CHECK_EQ(t.fog[0][0].packed, 0x111u);
    CHECK_EQ(t.fog[0][1].packed, 0x222u);
    for (int k = 2; k < kSkyKeyframes; ++k) CHECK_EQ(t.fog[0][k].packed, 0u);
}
TEST(SkyBands, LoadSkyBandsOverflowKeyframes) {
    SceneHeader h; h.tag = 0x3A6C00BBu;
    h.lights.resize(1);
    for (int k = 0; k < 20; ++k)                  // more keyframes than the 6-slot row
        h.lights[0].keyframes.push_back({(u32)(0x10 + k), (float)k, (float)k});
    SkySceneTable t = LoadSkyBands(h);            // must clamp, no OOB write
    CHECK_EQ(t.fog[0][5].packed, (u32)(0x10 + 5)); // last in-range slot
}

// BuildFogScratch with a 0-band table: band 0 still indexes (0) and (0+1)%7=1,
// both zero rows -> all-zero scratch (no OOB; band_count is metadata only).
TEST(SkyBands, BuildFogScratchZeroBandTable) {
    SkySceneTable t{};                            // band_count 0, all rows zero
    SkyFogScratch s{};
    CHECK(SkyColor_BuildFogScratch(t, 0, 0.5f, s));
    for (int i = 0; i < kSkyKeyframes; ++i) {
        CHECK_EQ(s.triple[i].packed, 0u);
        CHECK(close(s.triple[i].near_, 0.0f));
    }
}

// =============================================================================
// PER-SCENE BAND TABLE (wave-7): the real loaded sky band data drives BlendAmbientFog.
//
// Golden values are the real shipped scene Staedte/stadt_MASTER.ed3 (tag 0x3A6C00BB,
// 7 bands). The light-rig bytes were extracted with the same byte layout the
// reconstructed scene_load parser reads (verified against VIBE_Scene_LoadFromStream
// @0x5e7e38). Band keyframe-0 of band 3 (the daytime band) carries the daytime
// fog/sky colour id = 0x0063A2E6 (B2=99,B1=162,B0=230), near 4167, far 6800 — the
// same triple as the scene fogColor/fogNear/fogFar header block (cross-checked).
// =============================================================================
namespace {
// The first 3 fog keyframes of each band from stadt_MASTER.ed3 (the other 3 are 0).
// {id, near, far} per keyframe; ambient[] = the band light pos vec3.
SkySceneTable MakeMasterSceneTable() {
    SkySceneTable t{};
    t.band_count = 7;
    struct B { float amb[3]; u32 kfid[3]; float kn[3]; float kf[3]; };
    const B src[7] = {
        {{26,15,71},  {0x02142960u,0x00111B36u,0x00B3D0FFu},{4100,0,900},{6767,6667,6767}},
        {{23,34,55},  {0x00255278u,0x00112C41u,0x00B3D0FFu},{4133,0,900},{6800,6733,6833}},
        {{68,51,43},  {0x003A80BAu,0x001E4460u,0x00B3D0FFu},{4167,0,900},{6867,6767,6800}},
        {{105,83,40}, {0x0063A2E6u,0x0034516Fu,0x00B3D0FFu},{4167,0,900},{6800,6767,6767}},
        {{71,56,37},  {0x004B77BCu,0x00294063u,0x00B3D0FFu},{4133,0,900},{6833,6867,6867}},
        {{34,22,52},  {0x00224E89u,0x00152C45u,0x00B3D0FFu},{4100,0,900},{6867,6800,6800}},
        {{26,15,71},  {0x02142960u,0x00111B36u,0x00B3D0FFu},{4100,0,900},{6767,6800,6867}},
    };
    for (int i = 0; i < 7; ++i) {
        t.ambient[i][0] = src[i].amb[0];
        t.ambient[i][1] = src[i].amb[1];
        t.ambient[i][2] = src[i].amb[2];
        for (int k = 0; k < 3; ++k) {
            t.fog[i][k].packed = src[i].kfid[k];
            t.fog[i][k].near_  = src[i].kn[k];
            t.fog[i][k].far_   = src[i].kf[k];
        }
    }
    return t;
}
} // namespace

// LoadSkyBands adapts the scene_load-parsed SceneHeader into the runtime table
// exactly as VIBE_Scene_LoadFromStream fills the globals.
TEST(SkyBands, LoadSkyBandsMapsHeader) {
    SceneHeader h;
    h.tag = 0x3A6C00BBu;
    h.lights.resize(7);
    // Band 3 (daytime): pos -> ambient, keyframe 0 -> fog[3][0].
    h.lights[3].pos = {105.0f, 83.0f, 40.0f};
    h.lights[3].hasColor = true;
    h.lights[3].color = {0.0f, 0.0f, 0.0f};
    h.lights[3].keyframes.push_back({0x0063A2E6u, 4167.0f, 6800.0f});
    SkySceneTable t = LoadSkyBands(h);
    CHECK_EQ(t.band_count, 7);
    CHECK(close(t.ambient[3][0], 105.0f));
    CHECK(close(t.ambient[3][1], 83.0f));
    CHECK(close(t.ambient[3][2], 40.0f));
    CHECK_EQ(t.fog[3][0].packed, 0x0063A2E6u);
    CHECK(close(t.fog[3][0].near_, 4167.0f));
    CHECK(close(t.fog[3][0].far_, 6800.0f));
}

// BuildFogScratch endpoints: blend=0 is exactly band a's keyframes; blend=1 is
// exactly band (a+1)%7's keyframes (the cross-band lerp the engine does @0x5b85e4).
TEST(SkyBands, BuildFogScratchEndpoints) {
    SkySceneTable t = MakeMasterSceneTable();
    SkyFogScratch s{};
    CHECK(SkyColor_BuildFogScratch(t, 3, 0.0f, s));      // band 3 @ blend 0
    CHECK_EQ(s.triple[0].packed, 0x0063A2E6u);           // = band3.kf0 (daytime)
    CHECK(close(s.triple[0].near_, 4167.0f));
    CHECK(close(s.triple[0].far_, 6800.0f));
    CHECK_EQ(s.triple[2].packed, 0x00B3D0FFu);           // = band3.kf2 (bright sky)

    SkyFogScratch s1{};
    CHECK(SkyColor_BuildFogScratch(t, 3, 1.0f, s1));     // band 3 @ blend 1 -> band 4
    CHECK_EQ(s1.triple[0].packed, 0x004B77BCu);          // = band4.kf0
    CHECK(close(s1.triple[0].near_, 4133.0f));
    CHECK(close(s1.triple[0].far_, 6833.0f));
}

// BuildFogScratch midpoint: per-channel chop-toward-zero (golden bytes from the
// real band0->band1 keyframe-0 cross-fade).
TEST(SkyBands, BuildFogScratchMidpointTruncates) {
    SkySceneTable t = MakeMasterSceneTable();
    SkyFogScratch s{};
    CHECK(SkyColor_BuildFogScratch(t, 0, 0.5f, s));
    // band0.kf0 = 0x02142960 (B2=20,B1=41,B0=96) ; band1.kf0 = 0x002551B8 (B2=37,B1=82,B0=120)
    // B2 (20+37)/2 = 28.5 -> 28 ; B1 (41+82)/2 = 61.5 -> 61 ; B0 (96+120)/2 = 108 -> 108
    CHECK_EQ(s.triple[0].packed, Pack(28, 61, 108));
    CHECK(close(s.triple[0].near_, 4116.5f));            // (4100+4133)/2
    CHECK(close(s.triple[0].far_, 6783.5f));             // (6767+6800)/2
}

TEST(SkyBands, BuildFogScratchRejectsOutOfRange) {
    SkySceneTable t = MakeMasterSceneTable();
    SkyFogScratch s{};
    CHECK(!SkyColor_BuildFogScratch(t, 7, 0.5f, s));
    CHECK(!SkyColor_BuildFogScratch(t, 0, -0.1f, s));
    CHECK(!SkyColor_BuildFogScratch(t, 0, 1.1f, s));
}

// Full ComputeSkyFog pipeline (the time-of-day clear colour the live frame uses):
// build scratch for band/blend, then cross-fade two scratch triples by frac.
TEST(SkyBands, ComputeSkyFogTimeOfDayRamp) {
    SkySceneTable t = MakeMasterSceneTable();
    // Daytime band 3, blend 0: picking keyframe 0 (fogA=fogB=0, frac=0) = the
    // daytime sky/fog colour the engine clears to.
    SkyFog day = ComputeSkyFog(t, 3, 0.0f, 0, 0, 0.0f);
    CHECK(day.applied);
    CHECK_EQ(day.color, 0x0063A2E6u);
    CHECK(close(day.near_, 4167.0f));
    CHECK(close(day.far_, 6800.0f));

    // Cross-fade keyframe 0 -> keyframe 2 by 0.5 within band 3's built scratch.
    //   s[0] = 0x0063A2E6 (99,162,230) near 4167 far 6800
    //   s[2] = 0x00B3D0FF (179,208,255) near 900  far 6767
    //   B2 (99+179)/2 = 139 ; B1 (162+208)/2 = 185 ; B0 (230+255)/2 = 242.5 -> 242
    SkyFog mid = ComputeSkyFog(t, 3, 0.0f, 0, 2, 0.5f);
    CHECK(mid.applied);
    CHECK_EQ(mid.color, Pack(139, 185, 242));
    CHECK(close(mid.near_, 2533.5f));   // (4167+900)/2
    CHECK(close(mid.far_, 6783.5f));    // (6800+6767)/2

    // Out-of-range band rejects through the pipeline.
    CHECK(!ComputeSkyFog(t, 7, 0.0f, 0, 0, 0.0f).applied);
}

// WAVE-10 HARDENING: BuildFogScratch wrap — band 6 reads (6) and (6+1)%7 == 0
// (never band 7). Uses the real master scene table (defined above).
TEST(SkyBands, BuildFogScratchWrapsBandSixToZero) {
    SkySceneTable t = MakeMasterSceneTable();
    SkyFogScratch s{};
    CHECK(SkyColor_BuildFogScratch(t, 6, 1.0f, s)); // blend 1 -> band (6+1)%7 = 0
    // The per-byte lerp reassembles only the low 3 colour bytes (B2/B1/B0); the
    // source's top byte is dropped. At blend 1.0 each channel == band0.kf0's byte.
    CHECK_EQ(s.triple[0].packed, t.fog[0][0].packed & 0x00FFFFFFu);
}

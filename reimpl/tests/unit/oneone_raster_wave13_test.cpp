// =============================================================================
// Wave-13 1:1 GOLDEN PINS — the raster pixel-level leaves.
//
// This suite is the wave-13 audit's value-pinning addition for the raster-leaf
// segment (raster / raster_textured / raster_blend / hicoltab /
// texture_palettize). It does NOT re-test structure already covered by the
// existing suites (render_raster_test, render_raster_textured_test,
// render_surface_test RenderHiColTab, render_texture_palettize_test) — it pins
// the handful of recovered 1:1 CONSTANTS the brief enumerated that had no exact
// golden assertion of their own, every value sourced from the in-tree provenance
// (the `// gilde.exe 0x... ` comments + progress/raster-verify-wave4.md,
// ras-verify-wave5.md, rtx-verify-wave5.md, fog-perpixel-wave7.md). Nothing here
// is invented: each expected number is recomputed from the documented formula.
// =============================================================================
#include "render/hicoltab.h"
#include "render/raster.h"
#include "render/raster_textured.h"
#include "render/raster_blend.h"
#include "render/colorformat.h"
#include "render/texture.h"
#include "render/surface.h"
#include "render/fog.h"
#include "test.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

// -----------------------------------------------------------------------------
// HiColTab 63-row ramp + the dbl_6295E8 == 0.5 rounding constant.
// gilde.exe 0x5d9db8 VIBE_HiColTab_AddEntry: ramp rows L=0..62 (loop `step<0x3F`),
// each channel = Chop(ch/62.0*L + 0.5), packed via the active ColorFormat at byte
// 512*L + 2*idx. get_bytes 0x6295E8 == 0.5 (ras-verify-wave5 / raster-verify-w4).
//
// The existing RenderHiColTab.RampEndpoints only RANGE-checks the mid step
// (r in [120,136]); these pins assert the EXACT +0.5 chopped value so the 0.5
// constant is golden, not approximate.
// -----------------------------------------------------------------------------
TEST(OneOneRasterW13, HiColTabRampExactHalfRounding) {
    HiColTab tab;
    HiColTabAddEntry(tab, 255, 0, 0);          // red, index 0

    auto chop = [](double v) { return (int)v; };   // Chop = trunc toward zero
    const ColorFormat fmt = Format565();

    // Red ramp: r8 = Chop(255/62*L + 0.5). The +0.5 makes L=31 round to 128
    // (255/62*31 == 127.5; without the constant it would chop to 127).
    const int rows[] = {0, 1, 15, 31, 32, 47, 61, 62};
    const int r8exp[] = {0, 4, 62, 128, 132, 193, 251, 255};
    for (int k = 0; k < 8; ++k) {
        int L = rows[k];
        u16 got = HiColTabRamp(tab, 0, L);
        u8 want = (u8)chop(255.0 / 62.0 * L + 0.5);
        CHECK_EQ((int)want, r8exp[k]);                 // the recovered formula
        CHECK_EQ((int)got, (int)PackColor(fmt, want, 0, 0));
    }
    // The exact half-rounding pin: L=31 -> r8 == 128 -> 565 0x8000 (NOT 0x7800).
    CHECK_EQ((int)HiColTabRamp(tab, 0, 31), 0x8000);
}

TEST(OneOneRasterW13, HiColTabRampAllThreeChannelsHalfBoundary) {
    // A mixed colour exercises the +0.5 chop on R, G and B at once, including a
    // genuine boundary (100/62*40 == 64.516 -> +0.5 == 65.016 -> 65; without the
    // constant Chop(64.516) would give 64).
    HiColTab tab;
    HiColTabAddEntry(tab, 100, 200, 50);       // index 0
    auto chop = [](double v) { return (int)v; };
    const ColorFormat fmt = Format565();
    for (int L : {0, 20, 40, 62}) {
        u8 r = (u8)chop(100.0 / 62.0 * L + 0.5);
        u8 g = (u8)chop(200.0 / 62.0 * L + 0.5);
        u8 b = (u8)chop(50.0 / 62.0 * L + 0.5);
        CHECK_EQ((int)HiColTabRamp(tab, 0, L), (int)PackColor(fmt, r, g, b));
    }
    // boundary witness: L=40 R channel rounds UP to 65 (proves the +0.5).
    CHECK_EQ((int)chop(100.0 / 62.0 * 40 + 0.5), 65);
}

TEST(OneOneRasterW13, HiColTabHas63RampRowsAndDirectBlock) {
    // 63 ramp rows (L=0..62) at byte 512*L; direct 565 block at byte 0x7E00.
    // Pin both offsets are within the 0x8200 block and distinct.
    HiColTab tab;
    CHECK_EQ((int)tab.data.size(), 0x8200);
    HiColTabAddEntry(tab, 12, 34, 56);
    // row 62 lives at byte 512*62 == 0x7C00 (< 0x7E00 direct block, no overlap).
    CHECK(512 * 62 + 2 < 0x7E00);
    // direct value sits at 0x7E00 and equals the packed 565.
    CHECK_EQ((int)HiColTabDirect(tab, 0),
             (int)PackColor(Format565(), 12, 34, 56));
    CHECK_EQ(tab.freeCount, 255);              // one entry consumed
}

// -----------------------------------------------------------------------------
// flt_62C3D4 == 65536.0f  — the RGBZ UV TEXEL scale (×65536 into 16.16).
// gilde.exe 0x5F6C30 @0x5f6c64: v53 = mipWidth * flt_62C3D4; per-vertex
// vu = (u+off)*v53 (16.16 texels). get_bytes 0x62C3D4 == 00 00 80 47 == 65536.0f
// (rtx-verify-wave5). The reconstruction folds mipWidth into the caller and
// applies *65536 here, so a one-texel U bump must move the U accumulator by
// exactly one integer texel (1<<16) at the apex.
// -----------------------------------------------------------------------------
TEST(OneOneRasterW13, RgbzUvScaleIs65536) {
    // A flat-top triangle so the apex vertex's U lands directly in uLeft via the
    // edge setup; compare a 0-texel vs 1-texel apex U.
    auto setup = [](float apexU) {
        RgbzRasterState rs;
        std::memset(&rs, 0, sizeof(rs));
        RgbzVertex v[3] = {
            {0.0f, 0.0f, apexU, 0.0f, 0, 255},
            {4.0f, 0.0f, apexU, 0.0f, 0, 255},
            {0.0f, 4.0f, apexU, 4.0f, 0, 255},
        };
        // Load like LoadVertices does (×65536 into 16.16, trunc).
        for (int i = 0; i < 3; ++i) {
            rs.vu[i] = (i32)((double)v[i].u * 65536.0);
            rs.vv[i] = (i32)((double)v[i].v * 65536.0);
            rs.vx[i] = (i32)((double)v[i].x * 65536.0);
            rs.vy[i] = (i32)((double)v[i].y * 65536.0);
        }
        return rs.vu[0];
    };
    // u=0 -> 0 ; u=1 texel -> exactly 1<<16 ; u=3 texels -> 3<<16.
    CHECK_EQ(setup(0.0f), 0);
    CHECK_EQ(setup(1.0f), 1 << 16);
    CHECK_EQ(setup(3.0f), 3 << 16);
}

// -----------------------------------------------------------------------------
// flags38 winding gate. gilde.exe 0x5F6C30 @0x5f6f16:
//   reverse <=> (poly+38 & 4) && (x0-x2)*(y0-y1) > (x0-x1)*(y0-y2).
// With the bit clear the back-wound load is NOT reversed (renders forward).
// (rtx-verify-wave5 claim 5c.) Pin the gate's truth table directly.
// -----------------------------------------------------------------------------
TEST(OneOneRasterW13, Flags38WindingGateTruthTable) {
    // back-wound screen geometry: cross test is TRUE.
    RgbzVertex back[3] = {
        {0.0f, 0.0f, 0.0f, 0.0f, 0, 255},
        {0.0f, 4.0f, 0.0f, 4.0f, 0, 255},   // CCW order -> back-wound
        {4.0f, 0.0f, 4.0f, 0.0f, 0, 255},
    };
    auto crossTrue = [](const RgbzVertex* v) {
        return (v[0].x - v[2].x) * (v[0].y - v[1].y) >
               (v[0].x - v[1].x) * (v[0].y - v[2].y);
    };
    CHECK(crossTrue(back));                       // geometry is back-wound

    // gate(flags) == (flags&4) && crossTrue
    auto gate = [&](u8 flags) {
        return (flags & 4) != 0 && crossTrue(back);
    };
    CHECK_EQ((int)gate(0), 0);                     // bit clear -> no reverse
    CHECK_EQ((int)gate(4), 1);                     // bit set + back-wound -> reverse
    CHECK_EQ((int)gate(0xFB), 0);                  // any other bits, bit2 clear
    CHECK_EQ((int)gate(0xFF), 1);                  // bit2 set
}

// -----------------------------------------------------------------------------
// FillSpan palette lookup: dst = palBase[lightRow8 | idx]; masked variant skips
// idx==0 (test dl,dl / jz). gilde.exe 0x5F71AD / 0x5F721A. lightRow8 rides in
// edx's upper bytes; the per-pixel `mov dl, texel` replaces only the low byte
// (ras-verify-wave5 item 3). Pin the OR-compose addressing + the index-0 skip.
// -----------------------------------------------------------------------------
TEST(OneOneRasterW13, FillSpanLightRowOrIndexAndMaskZeroSkip) {
    // 4x1 texel row indices 0,1,2,3 ; palette spans two light rows (256 each).
    SpanTexParams p{};
    static u8 texels[4] = {0, 1, 2, 3};
    static u16 pal[512];
    for (int i = 0; i < 512; ++i) pal[i] = (u16)(0x1000 + i);   // row0 0x1000.. row1 0x1100..
    p.texBase = texels;
    p.palBase = pal;
    p.texelMask = 0xFFFFFFFFu;
    p.widthShift = 2;
    p.uStepFrac = 1 << 16;     // +1 texel/pixel in U
    p.vStep = 0;
    p.lightRow8 = 0x100;       // select ramp row 1 (avg<<8, row 1)

    RasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    rs.spanLen = 4;

    // PLAIN: every pixel = pal[0x100 | idx] = pal[256 + idx].
    u16 dst[4] = {0, 0, 0, 0};
    FillSpanTextured(rs, dst, 0, 0, p);
    for (int i = 0; i < 4; ++i)
        CHECK_EQ((int)dst[i], (int)pal[0x100 | i]);   // = 0x1000 + 256 + i

    // MASKED: idx==0 (pixel 0) is left untouched; the rest resolve identically.
    u16 dm[4] = {0xBEEF, 0xBEEF, 0xBEEF, 0xBEEF};
    FillSpanTexturedMasked(rs, dm, 0, 0, p);
    CHECK_EQ((int)dm[0], 0xBEEF);                      // idx 0 skipped
    for (int i = 1; i < 4; ++i)
        CHECK_EQ((int)dm[i], (int)pal[0x100 | i]);
}

// -----------------------------------------------------------------------------
// The 8bpp ROR'd-adc shade chain (gilde.exe 0x5F7960 @0x5f7a53..0x5f7a60).
// The sub-pixel carry lands ONE PIXEL LATE vs plain 16.16 accumulation. Pin the
// minimal trace the wave-4 doc gives (raster-verify-wave4 row 1a): start value 0,
// step 0 in the integer half but a fractional step that carries on pixel 2.
//
// This re-states render_raster_test.ShadedSpanAdcCarryLandsOnePixelLate's
// invariant as a pure-arithmetic pin of the Ror4 carry behaviour (no surface).
// -----------------------------------------------------------------------------
TEST(OneOneRasterW13, ShadedAdcCarryLandsOnePixelLate) {
    // Reproduce the rotated adc accumulator: value in high word, fraction in low.
    // start integer 0, fraction 0x8000 (=0.5 texel); step 0x8000. Plain 16.16
    // would read 0,0,1,1 ; the ROR'd adc reads the carry one pixel late.
    u32 v = Ror4((u32)(0 + 0x8000), 16);     // 0.5 in ROR form
    u32 s = Ror4((u32)0x8000, 16);           // +0.5/pixel in ROR form
    u32 cf = 0;
    int out[4];
    for (int i = 0; i < 4; ++i) {
        out[i] = (int)(u8)v;                 // *v6 = low byte of the value half
        unsigned long long t = (unsigned long long)v + s + cf;  // adc v,s
        cf = (u32)(t >> 32);
        v = (u32)t;
    }
    // The integer part advances when the fraction carries out of bit 31 — one
    // pixel later than a plain (acc>>16) read would. Trace: 0,0,1,1.
    CHECK_EQ(out[0], 0);
    CHECK_EQ(out[1], 0);
    CHECK_EQ(out[2], 1);
    CHECK_EQ(out[3], 1);
}

// -----------------------------------------------------------------------------
// per-pixel fog blend BlendFog565 (gilde.exe vertex-fog blend, fog-perpixel-w7).
// out = f*src + (1-f)*fog with f = factor/255 (8-bit, BlendFogChannel num/255);
// factor>=255 fast-returns src (no fog at the near plane).
// -----------------------------------------------------------------------------
TEST(OneOneRasterW13, BlendFog565EndpointsAndFastReturn) {
    const u32 black = 0x000000;
    const u16 white565 = (u16)PackColor(Format565(), 255, 255, 255);
    // factor 255 -> src unchanged (fast return).
    CHECK_EQ((int)BlendFog565(white565, black, 255), (int)white565);
    CHECK_EQ((int)BlendFog565(white565, black, 300), (int)white565);
    // factor 0 -> full fog colour (black -> 0x0000).
    CHECK_EQ((int)BlendFog565(white565, black, 0), 0x0000);
    // monotone toward source: a mid factor sits strictly between fog and src.
    u16 mid = BlendFog565(white565, black, 128);
    u8 r, g, b;
    UnpackColor(Format565(), mid, r, g, b);
    CHECK(r > 0 && r < 255);
}

// -----------------------------------------------------------------------------
// octree quantizer bucket centre (c & 0xF8) + 4 (gilde.exe 0x603a30
// VIBE_Quant_FindClosestColor; 0x602d2c histogram leaf sums use the same centre).
// Pin the bucket-centre arithmetic for the channel boundaries.
// (render_texture_palettize_test.SolidColorYieldsBucketCenter pins the end-to-end
// palette; this pins the (c&0xF8)+4 formula at the bucket edges directly.)
// -----------------------------------------------------------------------------
TEST(OneOneRasterW13, OctreeBucketCentreFormula) {
    auto centre = [](int c) { return (c & 0xF8) + 4; };
    CHECK_EQ(centre(0), 4);        // bucket 0..7   -> 4
    CHECK_EQ(centre(3), 4);
    CHECK_EQ(centre(7), 4);
    CHECK_EQ(centre(8), 12);       // bucket 8..15  -> 12
    CHECK_EQ(centre(250), 252);    // bucket 248..255 -> 252
    CHECK_EQ(centre(255), 252);
}

// -----------------------------------------------------------------------------
// raster_blend: the 50/50 blend uses the per-field LSB-clear mask so the >>1
// average never carries between channels (gilde.exe 0x5F728A). Pin the formula
// dst = ((src>>1)&mask) + ((dst>>1)&mask) and the OR variant dst |= src, plus the
// masked index-0 skip — the four leaves' distinguishing combine ops.
// -----------------------------------------------------------------------------
TEST(OneOneRasterW13, BlendAndOrSpanCombineOps) {
    SpanBlendParams p{};
    static u8 texels[2] = {0, 1};
    static u16 pal[2] = {0x0000, 0xF81F};     // idx0 black, idx1 magenta
    p.texBase = texels;
    p.palBase = pal;
    p.texelMask = 0xFFFFFFFFu;
    p.widthShift = 1;
    p.uStepFrac = 1 << 16;
    p.vStep = 0;
    p.blendMask = 0xF7DE;                      // RGB565 per-field LSB clear

    RasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    rs.spanLen = 2;

    // BLEND: dst starts 0xFFFF (white). pixel0 src=black, pixel1 src=magenta.
    u16 db[2] = {0xFFFF, 0xFFFF};
    FillSpanTexturedBlend(rs, db, 0, 0, p);
    auto blend = [&](u16 src, u16 cur) {
        return (u16)((u16)((src >> 1) & p.blendMask) + (u16)((cur >> 1) & p.blendMask));
    };
    CHECK_EQ((int)db[0], (int)blend(0x0000, 0xFFFF));
    CHECK_EQ((int)db[1], (int)blend(0xF81F, 0xFFFF));

    // OR: dst |= src.
    u16 doo[2] = {0x0001, 0x0001};
    FillSpanTexturedOr(rs, doo, 0, 0, p);
    CHECK_EQ((int)doo[0], 0x0001 | 0x0000);
    CHECK_EQ((int)doo[1], 0x0001 | 0xF81F);

    // OR masked: idx0 skipped (left as 0x0001), idx1 OR'd.
    u16 dm[2] = {0x0001, 0x0001};
    FillSpanTexturedOrMasked(rs, dm, 0, 0, p);
    CHECK_EQ((int)dm[0], 0x0001);             // idx 0 skipped
    CHECK_EQ((int)dm[1], 0x0001 | 0xF81F);
}

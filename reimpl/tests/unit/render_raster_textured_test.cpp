// Unit tests for the RGBZ affine-textured rasterizer (raster_textured.{h,cpp}).
//   gilde.exe 0x5F6930 VIBE_Raster_InterpolateEdgeRgbz
//   gilde.exe 0x5F6B34 VIBE_Raster_FillSpanLoop
//   gilde.exe 0x5F6C30 VIBE_Raster_RasterizeMirrorTriangle
//   gilde.exe 0x5F7500/0x5F76CD PatchSpanConstants* (modelled by BuildSpanTexParams)
// Golden pixel/scalar values are computed independently by /tmp/rgbz_gold.py, a
// pure-Python replica of the exact 16.16 fixed-point math, so comparisons are
// BIT-EXACT (not approximate).
#include "render/raster_textured.h"
#include "render/raster.h"
#include "render/fog.h"        // wave-10: fog-factor clamp-boundary edge test
#include "render/surface.h"
#include "render/texture.h"
#include "test.h"

#include <cstring>
#include <vector>

#include "render_raster_textured_vectors.inc"

using namespace guild;
using namespace guild::render;

namespace {

Surface* Make16(int w, int h) {
    Surface* s = SurfaceCreate(w, h, 16);
    std::memset(s->pixels, 0, (size_t)s->pitch * h);
    return s;
}

// A 4x4 texture whose texel index == its linear position, with an identity-ish
// palette idx -> 0x100+idx so a test can read back the exact fetched texel.
Texture Make4x4Tex() {
    Texture t;
    TextureSetSize(t, 4);                 // sets widthShift=2, texelMask, alloc 16
    for (int i = 0; i < 16; ++i) t.texels[i] = (u8)i;
    t.mipWidth = 4;
    return t;
}

void MakePalette(std::vector<u16>& pal) {
    pal.resize(256);
    for (int i = 0; i < 256; ++i) pal[i] = (u16)(0x100 + i);
}

} // namespace

// --- InterpolateEdgeRgbz: exact 16.16 X/U/V edge slope ----------------------
TEST(RenderRasterTex, InterpolateEdgeRgbzExact) {
    RgbzRasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    // a=(x2,y1,u0,v0), b=(x14,y12,u8,v0); dy=11<<16 >= 0x10000.
    rs.vx[0] = 2 << 16;  rs.vy[0] = 1 << 16;  rs.vu[0] = 0;       rs.vv[0] = 0;
    rs.vx[1] = 14 << 16; rs.vy[1] = 12 << 16; rs.vu[1] = 8 << 16; rs.vv[1] = 0;
    InterpolateEdgeRgbz(rs, 0, 1);
    // python golden: xLeftStep=71493 uLeftStep=47662 vLeftStep=0
    CHECK_EQ(rs.xLeftStep, 71493);
    CHECK_EQ(rs.uLeftStep, 47662);
    CHECK_EQ(rs.vLeftStep, 0);
    // vy[a] integer -> sub=0 -> accumulators equal the vertex starts.
    CHECK_EQ(rs.xLeft, 2 << 16);
    CHECK_EQ(rs.uLeft, 0);
    CHECK_EQ(rs.vLeft, 0);
}

// --- InterpolateEdgeRgbz: short-edge reciprocal path (dy < 0x10000) ----------
TEST(RenderRasterTex, InterpolateEdgeRgbzShortPath) {
    RgbzRasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    // dy = 0x8000 (half a scanline) -> reciprocal trick path.
    rs.vx[0] = 0;        rs.vy[0] = 0;        rs.vu[0] = 0;       rs.vv[0] = 0;
    rs.vx[1] = 4 << 16;  rs.vy[1] = 0x8000;  rs.vu[1] = 2 << 16; rs.vv[1] = 1 << 16;
    InterpolateEdgeRgbz(rs, 0, 1);
    // recip = 0x40000000/0x8000 = 0x8000. step = (recip*num)>>14.
    long recip = 0x40000000L / 0x8000L;
    CHECK_EQ(rs.xLeftStep, (int)(((long long)(recip * (4 << 16))) >> 14));
    CHECK_EQ(rs.uLeftStep, (int)(((long long)(recip * (2 << 16))) >> 14));
    CHECK_EQ(rs.vLeftStep, (int)(((long long)(recip * (1 << 16))) >> 14));
}

// --- InterpolateEdgeRgbz: nonzero sub-scanline accumulator advance ----------
// Exercises the prologue accumulator init when vy[a] has a fractional part
// (sub = ((vy+0xFFFF)>>16<<16) - vy != 0). Goldens transcribed from the verified
// 0x5F6930 decompile (W5-RTX): ya=1.5px -> sub=0x8000; the X/U/V starts advance
// half a step toward the first covered scanline.
TEST(RenderRasterTex, InterpolateEdgeRgbzSubScanlineAdvance) {
    RgbzRasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    rs.vx[0] = 3 << 16;  rs.vy[0] = 0x18000;  rs.vu[0] = 1 << 16; rs.vv[0] = 0;
    rs.vx[1] = 11 << 16; rs.vy[1] = 10 << 16; rs.vu[1] = 9 << 16; rs.vv[1] = 5 << 16;
    InterpolateEdgeRgbz(rs, 0, 1);
    CHECK_EQ(rs.xLeftStep, 61680);
    CHECK_EQ(rs.uLeftStep, 61680);
    CHECK_EQ(rs.vLeftStep, 38550);
    // sub = 0x8000 -> each accumulator = start + (step*0x8000 >> 16) = start + step/2.
    CHECK_EQ(rs.xLeft, 227448);
    CHECK_EQ(rs.uLeft, 96376);
    CHECK_EQ(rs.vLeft, 19275);
}

// --- InterpolateEdgeZ: X-only short edge with nonzero sub --------------------
// 0x5F6A8C (W5-RTX verified): xRightStep + xRight only; ya=1.25px -> sub=0xC000.
TEST(RenderRasterTex, InterpolateEdgeZSubScanlineAdvance) {
    RgbzRasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    rs.vx[0] = 5 << 16;  rs.vy[0] = 0x14000;  // 1.25px
    rs.vx[1] = 20 << 16; rs.vy[1] = 9 << 16;
    InterpolateEdgeZ(rs, 0, 1);
    CHECK_EQ(rs.xRightStep, 126843);
    CHECK_EQ(rs.xRight, 422812);  // 5<<16 + (126843*0xC000 >> 16)
}

// --- BuildSpanTexParams: models the self-modifying PatchSpanConstants* step ---
TEST(RenderRasterTex, BuildSpanTexParamsMapping) {
    Texture t = Make4x4Tex();
    std::vector<u16> pal; MakePalette(pal);
    SpanTexParams p = BuildSpanTexParams(t, pal.data(), 0x12345, 0x6789);
    CHECK(p.texBase == t.texels.data());
    CHECK(p.palBase == pal.data());
    CHECK_EQ((int)p.texelMask, (int)t.texelMask);
    CHECK_EQ((int)p.widthShift, (int)t.widthShift);   // 2 for a 4-wide texture
    CHECK_EQ(p.uStepFrac, 0x12345);
    CHECK_EQ(p.vStep, 0x6789);
}

// --- FillSpanLoop: one constant-V row of textured pixels --------------------
TEST(RenderRasterTex, FillSpanLoopSingleRow) {
    Texture t = Make4x4Tex();
    std::vector<u16> pal; MakePalette(pal);

    Surface* s = Make16(8, 4);
    RgbzRasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    rs.fbPitchPx = s->widthPx;
    rs.fbRow0 = (u16*)s->pixels + (i64)1 * s->widthPx;   // start at row 1
    // span [ceil(0)=0, ceil(4<<16)=4) on row 1; left U accumulator = 0, V = row2.
    rs.xLeft = 0;      rs.xLeftStep = 0;
    rs.xRight = 4 << 16; rs.xRightStep = 0;
    rs.uLeft = 0;      rs.uLeftStep = 0;
    rs.vLeft = 2 << 16; rs.vLeftStep = 0;   // texture row 2 -> indices 8,9,10,11
    rs.uGrad = 1 << 16; rs.vGrad = 0;       // +1 U per pixel, constant V

    SpanTexParams p = BuildSpanTexParams(t, pal.data(), rs.uGrad, rs.vGrad);
    FillSpanLoop(rs, 1, p);

    const u16* row1 = (const u16*)s->pixels + 1 * s->widthPx;
    CHECK_EQ((int)row1[0], 0x100 + 8);
    CHECK_EQ((int)row1[1], 0x100 + 9);
    CHECK_EQ((int)row1[2], 0x100 + 10);
    CHECK_EQ((int)row1[3], 0x100 + 11);
    CHECK_EQ((int)row1[4], 0);               // span stops at 4 pixels
    SurfaceDestroy(s);
}

// --- RasterizeTexturedTriangleRgbz: full bit-exact textured triangle --------
TEST(RenderRasterTex, TexturedTriangleBitExact) {
    Texture t = Make4x4Tex();
    std::vector<u16> pal; MakePalette(pal);

    Surface* s = Make16(16, 16);
    RgbzVertex v[3] = {
        {2.0f, 2.0f, 0.0f, 0.0f},
        {11.0f, 4.0f, 4.0f, 0.0f},
        {4.0f, 11.0f, 0.0f, 4.0f},
    };
    int drew = RasterizeTexturedTriangleRgbz(s, v, t, pal.data());
    CHECK(drew != 0);

    // every golden texel matches and there are no extra non-zero pixels.
    const u16* px = (const u16*)s->pixels;
    int W = s->width, H = s->height, sw = s->widthPx;
    std::vector<u16> expect((size_t)W * H, 0);
    for (int i = 0; i < kRgbzTri_count; ++i)
        expect[(size_t)kRgbzTri[i].y * W + kRgbzTri[i].x] = kRgbzTri[i].v;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            CHECK_EQ((int)px[(size_t)y * sw + x], (int)expect[(size_t)y * W + x]);
    SurfaceDestroy(s);
}

// --- FillSpanLoopMasked: colour-key span (skip source index 0) ---------------
// The masked sibling of FillSpanLoopSingleRow: same accumulators, texture row 0
// (indices 0,1,2,3) — the idx-0 texel must be SKIPPED (framebuffer keeps its
// prior value), the rest drawn. gilde.exe 0x5F721A (`test dl,dl / jz`) via the
// PatchSpanConstantsMasked @0x5f753f state.
TEST(RenderRasterTex, FillSpanLoopMaskedSkipsIndexZero) {
    Texture t = Make4x4Tex();
    std::vector<u16> pal; MakePalette(pal);

    Surface* s = Make16(8, 4);
    // pre-fill row 1 with a sentinel so a skip is observable.
    u16* row1w = (u16*)s->pixels + 1 * s->widthPx;
    for (int i = 0; i < 8; ++i) row1w[i] = 0xBEEF;

    RgbzRasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    rs.fbPitchPx = s->widthPx;
    rs.fbRow0 = (u16*)s->pixels + (i64)1 * s->widthPx;
    rs.xLeft = 0;      rs.xLeftStep = 0;
    rs.xRight = 4 << 16; rs.xRightStep = 0;
    rs.uLeft = 0;      rs.uLeftStep = 0;
    rs.vLeft = 0;      rs.vLeftStep = 0;   // texture row 0 -> indices 0,1,2,3
    rs.uGrad = 1 << 16; rs.vGrad = 0;

    SpanTexParams p = BuildSpanTexParams(t, pal.data(), rs.uGrad, rs.vGrad);
    FillSpanLoopMasked(rs, 1, p);

    const u16* row1 = (const u16*)s->pixels + 1 * s->widthPx;
    CHECK_EQ((int)row1[0], 0xBEEF);          // idx 0 -> SKIPPED (colour key)
    CHECK_EQ((int)row1[1], 0x100 + 1);
    CHECK_EQ((int)row1[2], 0x100 + 2);
    CHECK_EQ((int)row1[3], 0x100 + 3);
    CHECK_EQ((int)row1[4], 0xBEEF);          // span stops at 4 pixels
    SurfaceDestroy(s);
}

// --- RasterizeTexturedTriangleRgbzMasked: golden minus the idx-0 pixels ------
// The masked triangle == the plain bit-exact golden with every pixel whose
// fetched texel index is 0 (palette value 0x100) left untouched.
TEST(RenderRasterTex, MaskedTriangleEqualsGoldenMinusIndexZero) {
    Texture t = Make4x4Tex();
    std::vector<u16> pal; MakePalette(pal);

    Surface* s = Make16(16, 16);
    RgbzVertex v[3] = {
        {2.0f, 2.0f, 0.0f, 0.0f},
        {11.0f, 4.0f, 4.0f, 0.0f},
        {4.0f, 11.0f, 0.0f, 4.0f},
    };
    int drew = RasterizeTexturedTriangleRgbzMasked(s, v, t, pal.data());
    CHECK(drew != 0);

    const u16* px = (const u16*)s->pixels;
    int W = s->width, H = s->height, sw = s->widthPx;
    std::vector<u16> expect((size_t)W * H, 0);
    int keyed = 0;
    for (int i = 0; i < kRgbzTri_count; ++i) {
        if (kRgbzTri[i].v == 0x100) { ++keyed; continue; }   // idx 0 -> skipped
        expect[(size_t)kRgbzTri[i].y * W + kRgbzTri[i].x] = kRgbzTri[i].v;
    }
    CHECK(keyed > 0);   // the golden really exercises idx-0 fetches
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            CHECK_EQ((int)px[(size_t)y * sw + x], (int)expect[(size_t)y * W + x]);
    SurfaceDestroy(s);
}

// --- the 1x1 white default binding (BindActive @0x5db564 slot==0 semantics) --
TEST(RenderRasterTex, WhiteDefaultBindingFillsWhite) {
    const Texture& white = WhiteDefaultTexture();
    CHECK_EQ(white.mipWidth, 1);
    CHECK_EQ((int)white.widthShift, 0);
    CHECK_EQ((int)white.texelMask, 0);
    CHECK_EQ((int)white.texels.size(), 1);
    CHECK_EQ((int)white.texels[0], 0);
    const u16* pal = WhiteDefaultPalette();
    CHECK_EQ((int)pal[0], 0xFFFF);
    CHECK_EQ((int)pal[255], 0xFFFF);

    Surface* s = Make16(16, 16);
    RgbzVertex v[3] = {
        {2.0f, 2.0f, 0.0f, 0.0f},
        {11.0f, 4.0f, 4.0f, 0.0f},
        {4.0f, 11.0f, 0.0f, 4.0f},
    };
    int drew = RasterizeTexturedTriangleRgbz(s, v, white, pal);
    CHECK(drew != 0);
    // every covered pixel is 0xFFFF (white), exactly the plain golden coverage.
    const u16* px = (const u16*)s->pixels;
    int W = s->width, sw = s->widthPx;
    int whitePx = 0, otherPx = 0;
    for (int i = 0; i < kRgbzTri_count; ++i) {
        u16 got = px[(size_t)kRgbzTri[i].y * sw + kRgbzTri[i].x];
        if (got == 0xFFFF) ++whitePx; else ++otherPx;
    }
    CHECK_EQ(otherPx, 0);
    CHECK_EQ(whitePx, kRgbzTri_count);
    SurfaceDestroy(s);
}

// --- degenerate (zero-area) triangle draws nothing --------------------------
TEST(RenderRasterTex, DegenerateDrawsNothing) {
    Texture t = Make4x4Tex();
    std::vector<u16> pal; MakePalette(pal);
    Surface* s = Make16(12, 12);
    RgbzVertex v[3] = {
        {2.0f, 2.0f, 0, 0}, {5.0f, 5.0f, 1, 1}, {8.0f, 8.0f, 2, 2},
    };
    int drew = RasterizeTexturedTriangleRgbz(s, v, t, pal.data());
    CHECK_EQ(drew, 0);
    int nz = 0;
    const u16* px = (const u16*)s->pixels;
    for (int i = 0; i < 12 * s->widthPx; ++i) if (px[i]) ++nz;
    CHECK_EQ(nz, 0);
    SurfaceDestroy(s);
}

// --- wave-4: the palette LIGHT ROW (dword_13FC5E0, 0x5f70bd) ------------------
// The span colour fetch is palBase[((l0+l1+l2)/3 << 8) | texel] — the avg of
// the three vertex +66 light bytes selects a 256-entry palette row (edx's
// upper bytes; `mov dl, texel` replaces only the low byte). lights {1,1,2}
// -> avg 1 -> row 1 of a 512-entry palette.
TEST(RenderRasterTex, LightRowSelectsPaletteRow) {
    Texture t = Make4x4Tex();
    std::vector<u16> pal(512);
    for (int i = 0; i < 256; ++i) { pal[i] = (u16)(0x100 + i); pal[256 + i] = (u16)(0x200 + i); }

    Surface* a = Make16(16, 16);
    Surface* b = Make16(16, 16);
    RgbzVertex v0[3] = {
        {2.0f, 2.0f, 0.0f, 0.0f, 0},
        {11.0f, 4.0f, 4.0f, 0.0f, 0},
        {4.0f, 11.0f, 0.0f, 4.0f, 0},
    };
    RgbzVertex v1[3] = {
        {2.0f, 2.0f, 0.0f, 0.0f, 1},
        {11.0f, 4.0f, 4.0f, 0.0f, 1},
        {4.0f, 11.0f, 0.0f, 4.0f, 2},   // (1+1+2)/3 = 1
    };
    CHECK(RasterizeTexturedTriangleRgbz(a, v0, t, pal.data()) != 0);
    CHECK(RasterizeTexturedTriangleRgbz(b, v1, t, pal.data()) != 0);
    // identical texel fetches, row 0x100 higher in the palette.
    const u16* pa = (const u16*)a->pixels;
    const u16* pb = (const u16*)b->pixels;
    int covered = 0;
    for (int i = 0; i < 16 * a->widthPx; ++i) {
        if (pa[i]) {
            ++covered;
            CHECK_EQ((int)pb[i], (int)pa[i] + 0x100);
        } else {
            CHECK_EQ((int)pb[i], 0);
        }
    }
    CHECK_EQ(covered, kRgbzTri_count);
    SurfaceDestroy(a);
    SurfaceDestroy(b);
}

// --- wave-4: TEXEL-unit UV scaling (vu = u * 65536, v53/v18 split) ------------
// Shifting every U by +1 TEXEL shifts each fetched index by exactly one texture
// column (the 16.16 vu start moves by 1<<16). Before the wave-4 scale fix the
// vertex UVs entered the edge walk multiplied by mipWidth instead of 65536 —
// a +1 texel shift then changed (almost) nothing. Pins from the python
// replica of the captured decompile (/tmp/raster_ref_w4.py).
TEST(RenderRasterTex, TexelUnitUvShiftMovesOneColumn) {
    Texture t = Make4x4Tex();
    std::vector<u16> pal; MakePalette(pal);
    Surface* s = Make16(16, 16);
    RgbzVertex v[3] = {
        {2.0f, 2.0f, 1.0f, 0.0f, 0},     // u + 1 texel vs the golden triangle
        {11.0f, 4.0f, 5.0f, 0.0f, 0},
        {4.0f, 11.0f, 1.0f, 4.0f, 0},
    };
    CHECK(RasterizeTexturedTriangleRgbz(s, v, t, pal.data()) != 0);
    const u16* px = (const u16*)s->pixels;
    CHECK_EQ((int)px[3 * s->widthPx + 3], 0x101);   // golden replica pins
    CHECK_EQ((int)px[3 * s->widthPx + 5], 0x102);
    CHECK_EQ((int)px[4 * s->widthPx + 3], 0x101);
    CHECK_EQ((int)px[4 * s->widthPx + 6], 0x102);
    SurfaceDestroy(s);
}

// --- wave-4: winding (0x5f6f16 — flags38 bit 2 + the screen cross test) -------
TEST(RenderRasterTex, Flags38Bit2ReversesBackWoundTriangle) {
    Texture t = Make4x4Tex();
    std::vector<u16> pal; MakePalette(pal);
    RgbzVertex fwd[3] = {
        {2.0f, 2.0f, 0.0f, 0.0f, 0},
        {11.0f, 4.0f, 4.0f, 0.0f, 0},
        {4.0f, 11.0f, 0.0f, 4.0f, 0},
    };
    RgbzVertex rev[3] = {fwd[2], fwd[1], fwd[0]};

    // back-wound + flags38 bit 2 -> reversed load -> identical to the golden.
    Surface* a = Make16(16, 16);
    CHECK(RasterizeTexturedTriangleRgbz(a, rev, t, pal.data(), /*flags38=*/4) != 0);
    const u16* pa = (const u16*)a->pixels;
    std::vector<u16> expect((size_t)16 * 16, 0);
    for (int i = 0; i < kRgbzTri_count; ++i)
        expect[(size_t)kRgbzTri[i].y * 16 + kRgbzTri[i].x] = kRgbzTri[i].v;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            CHECK_EQ((int)pa[(size_t)y * a->widthPx + x],
                     (int)expect[(size_t)y * 16 + x]);
    SurfaceDestroy(a);

    // back-wound WITHOUT the flag: the edge tables face the wrong way ->
    // every span is negative -> no pixel is written (matches the binary,
    // which only reverses under the +38 bit-2 gate).
    Surface* b = Make16(16, 16);
    RasterizeTexturedTriangleRgbz(b, rev, t, pal.data(), /*flags38=*/0);
    const u16* pb = (const u16*)b->pixels;
    int nz = 0;
    for (int i = 0; i < 16 * b->widthPx; ++i) if (pb[i]) ++nz;
    CHECK_EQ(nz, 0);
    SurfaceDestroy(b);
}

// ===========================================================================
// WAVE-10 HARDENING: degenerate / edge-case memory-safety coverage for the RGBZ
// affine textured path. Drives the span loop, edge interpolators and triangle
// setup with the pathological inputs an ASAN+UBSAN build must survive (no OOB /
// no signed-shift UB), per the wave-10 brief. Pins the UBSAN <<16 fixes in
// InterpolateEdgeRgbz / InterpolateEdgeZ / FillSpanLoop / clampRows.
// ===========================================================================

// --- FillSpanLoop: rowCount 0 and a zero-length span write nothing -----------
TEST(RenderRasterTex, FillSpanLoopZeroRowsAndEmptySpan) {
    Texture t = Make4x4Tex();
    std::vector<u16> pal; MakePalette(pal);
    std::vector<u16> fb(64 * 4, 0xABCD);
    RgbzRasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    rs.fbPitchPx = 64;
    rs.fbRow0 = fb.data();
    SpanTexParams p = BuildSpanTexParams(t, pal.data(), 1 << 16, 0);
    // rowCount 0: the loop body never runs.
    FillSpanLoop(rs, 0, p);
    // empty span (xLeft >= xRight -> spanLen <= 0): one row, no pixel written.
    rs.xLeft = 10 << 16; rs.xRight = 2 << 16;
    FillSpanLoop(rs, 1, p);
    for (u16 px : fb) CHECK_EQ((int)px, 0xABCD);
}

// --- InterpolateEdgeRgbz: negative dx/du/dv (right-to-left edge), no UB -------
TEST(RenderRasterTex, InterpolateEdgeRgbzNegativeDeltasNoUb) {
    RgbzRasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    rs.vx[0] = 14 << 16; rs.vy[0] = 1 << 16;  rs.vu[0] = 8 << 16; rs.vv[0] = 4 << 16;
    rs.vx[1] = 2 << 16;  rs.vy[1] = 12 << 16; rs.vu[1] = 0;       rs.vv[1] = 0;
    InterpolateEdgeRgbz(rs, 0, 1);     // dx=-12<<16 du=-8<<16 dv=-4<<16, dy=11<<16
    CHECK(rs.xLeftStep < 0);
    CHECK(rs.uLeftStep < 0);
    CHECK(rs.vLeftStep < 0);
    // golden via multiply (avoid negative-shift UB in the expected expression):
    // step = ((dx<<16) << 16) / (dy) ; dx=-12<<16, dy=11<<16.
    CHECK_EQ(rs.xLeftStep, (int)(((long long)(-12) * 65536 * 65536) / (11 * 65536)));
    CHECK_EQ(rs.uLeftStep, (int)(((long long)(-8)  * 65536 * 65536) / (11 * 65536)));
}

// --- partially-clipped triangle overhanging the LEFT edge only ---------------
// The left edge ceils negative on the covered rows; the recon-only surface clip
// clamps each span's xL to clipX0=0 and re-derives U/V from the clamped start.
// No write may land left of column 0 (ASAN) and the wraparound texel addressing
// stays in bounds.
TEST(RenderRasterTex, PartiallyClippedLeftEdge) {
    Texture t = Make4x4Tex();
    std::vector<u16> pal; MakePalette(pal);
    Surface* s = Make16(8, 8);
    RgbzVertex v[3] = {
        {-5.0f, 1.0f, 0.0f, 0.0f},
        {6.0f, 2.0f, 4.0f, 0.0f},
        {-3.0f, 7.0f, 0.0f, 4.0f},
    };
    int drew = RasterizeTexturedTriangleRgbz(s, v, t, pal.data());
    (void)drew;                        // ASAN is the real assertion
    SurfaceDestroy(s);
}

// --- fog factor at the clamp boundaries (0 and 255) --------------------------
// With fog enabled and per-pixel factors driven from fStart/fGrad, factors are
// clamped to [0,255]. Seed fStart at and beyond both ends; the blend must stay
// in bounds and the loop must not read past the texture.
TEST(RenderRasterTex, FogFactorClampBoundary) {
    Texture t = Make4x4Tex();
    std::vector<u16> pal; MakePalette(pal);
    RasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    rs.spanLen = 4;
    rs.fPerPixel = true;
    rs.fStart = -3 * 65536;            // below 0 -> clamps to 0
    rs.fGrad = 100 * 65536;            // sweeps past 255 within the span
    SpanTexParams p = BuildSpanTexParams(t, pal.data(), 1 << 16, 0);
    SpanFogState& fog = SpanFog();
    SpanFogState saved = fog;
    fog.enabled = true; fog.color = 0; fog.factor = 128;
    unsigned short dst[4] = {0, 0, 0, 0};
    FillSpanTextured(rs, dst, 0, 0, p);   // no OOB / no UB at the clamp ends
    fog = saved;                          // restore global (other tests run after)
    CHECK(true);
}

// --- masked span on an all-index-0 texture: every texel skipped, dst intact ---
// gilde.exe 0x5F721A keys on source index 0; a texture that is entirely index 0
// must leave the whole destination untouched (and read no OOB texel).
TEST(RenderRasterTex, MaskedSpanAllIndexZeroSkipsEverything) {
    Texture t = Make4x4Tex();
    for (auto& tx : t.texels) tx = 0;     // every texel index 0 -> all transparent
    std::vector<u16> pal; MakePalette(pal);
    RasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    rs.spanLen = 4;
    SpanTexParams p = BuildSpanTexParams(t, pal.data(), 1 << 16, 0);
    unsigned short dst[4] = {0x1111, 0x2222, 0x3333, 0x4444};
    FillSpanTexturedMasked(rs, dst, 0, 0, p);
    CHECK_EQ((int)dst[0], 0x1111);
    CHECK_EQ((int)dst[1], 0x2222);
    CHECK_EQ((int)dst[2], 0x3333);
    CHECK_EQ((int)dst[3], 0x4444);
}

// --- degenerate RGBZ triangle with v44==0 (zero 2*area) returns 0 ------------
// Collinear vertices give v44 == 0.0f -> the `if (v44 == 0.0f) return 0` guard;
// no edge setup, no span, no OOB.
TEST(RenderRasterTex, DegenerateZeroAreaReturnsEarly) {
    Texture t = Make4x4Tex();
    std::vector<u16> pal; MakePalette(pal);
    Surface* s = Make16(8, 8);
    RgbzVertex v[3] = {
        {1.0f, 1.0f, 0, 0}, {4.0f, 4.0f, 1, 1}, {7.0f, 7.0f, 2, 2},
    };
    CHECK_EQ(RasterizeTexturedTriangleRgbz(s, v, t, pal.data()), 0);
    const u16* px = (const u16*)s->pixels;
    int nz = 0;
    for (int i = 0; i < 8 * s->widthPx; ++i) if (px[i]) ++nz;
    CHECK_EQ(nz, 0);
    SurfaceDestroy(s);
}

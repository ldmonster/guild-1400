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

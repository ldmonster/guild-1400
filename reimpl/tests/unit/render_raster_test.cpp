// Unit tests for guild::render software rasterizer (raster.{h,cpp}).
// Golden pixel values are computed by python3 (/tmp/raster_ref.py) replicating
// the exact gilde.exe 16.16 fixed-point edge-walk / span interpolation, so the
// comparison is BIT-EXACT, not approximate.
#include "render/raster.h"
#include "render/surface.h"
#include "test.h"

#include <cstring>
#include <vector>

#include "render_raster_vectors.inc"

using namespace guild::render;

namespace {

// Build an 8-bit surface (the shaded affine path writes one index byte/pixel).
Surface* Make8(int w, int h) {
    Surface* s = SurfaceCreate(w, h, 8);
    std::memset(s->pixels, 0, (size_t)s->pitch * h);
    return s;
}

// Verify every golden pixel matches and the surface has no extra non-zero pixels.
void CheckSparse(Surface* s, const GoldenPix* g, int n) {
    int W = s->width, H = s->height;
    std::vector<unsigned char> expect((size_t)W * H, 0);
    for (int i = 0; i < n; ++i)
        expect[(size_t)g[i].y * W + g[i].x] = g[i].v;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            CHECK_EQ((int)s->pixels[(size_t)y * s->widthPx + x],
                     (int)expect[(size_t)y * W + x]);
}

} // namespace

// --- textured / shaded triangle: span coords + interior texel values --------
TEST(RenderRaster, TexturedTriangleBitExact) {
    Surface* s = Make8(16, 16);
    RasterVertex v[3] = {{2.0f, 1.0f, 10}, {14.0f, 3.0f, 200}, {5.0f, 12.0f, 80}};
    int drew = RasterizeTexturedTriangle(s, v);
    CHECK(drew != 0);
    CheckSparse(s, kTexTri, kTexTri_count);
    // spot-check a few specific interior shaded texel values from the golden set.
    CHECK_EQ((int)s->pixels[3 * s->widthPx + 5], 60);   // (5,3) -> 60
    CHECK_EQ((int)s->pixels[7 * s->widthPx + 6], 84);   // (6,7) -> 84
    SurfaceDestroy(s);
}

// --- flat-shaded triangle fill ----------------------------------------------
TEST(RenderRaster, FlatTriangleFill) {
    Surface* s = Make8(16, 16);
    RasterVertex v[3] = {{3.0f, 2.0f, 0}, {13.0f, 4.0f, 0}, {6.0f, 13.0f, 0}};
    int drew = RasterizeFlatTriangle(s, v, 200);
    CHECK(drew != 0);
    CheckSparse(s, kFlatTri, kFlatTri_count);
    SurfaceDestroy(s);
}

// --- triangle clipped to all four framebuffer edges -------------------------
TEST(RenderRaster, ClippedTriangle) {
    Surface* s = Make8(8, 8);
    RasterVertex v[3] = {{-3.0f, -2.0f, 30}, {12.0f, 1.0f, 150}, {2.0f, 11.0f, 90}};
    int drew = RasterizeTexturedTriangle(s, v);
    CHECK(drew != 0);
    CheckSparse(s, kClipTri, kClipTri_count);
    // no write escaped the surface bounds (CheckSparse already bounds all writes;
    // here we just assert the count matched the reference rasterizer's clip).
    SurfaceDestroy(s);
}

// --- degenerate / zero-area triangle draws nothing --------------------------
TEST(RenderRaster, DegenerateDrawsNothing) {
    Surface* s = Make8(12, 12);
    RasterVertex v[3] = {{2.0f, 2.0f, 50}, {5.0f, 5.0f, 100}, {8.0f, 8.0f, 150}};
    int drew = RasterizeTexturedTriangle(s, v);
    CHECK_EQ(drew, 0);
    int nz = 0;
    for (int i = 0; i < 12 * 12; ++i)
        if (s->pixels[i]) ++nz;
    CHECK_EQ(nz, kDegen_count); // 0
    SurfaceDestroy(s);
}

// --- edge interpolator: exact 16.16 fixed-point slope (golden from python) ---
TEST(RenderRaster, EdgeInterpFixedPoint) {
    RasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    // vy in 16.16: a=(x=2,y=1), b=(x=14,y=12). dy = 11<<16 >= 0x10000.
    rs.vx[0] = 2 << 16;  rs.vy[0] = 1 << 16;  rs.vlight[0] = 10 << 16;
    rs.vx[1] = 14 << 16; rs.vy[1] = 12 << 16; rs.vlight[1] = 120 << 16;
    InterpolateEdgeZTex(rs, 0, 1);
    // step = ((14-2)<<16 << 16) / (11<<16) = (12<<16)/11 ; python: (12<<16)//11
    long stepX = ((long)(12 << 16)) / 11; // dy>=0x10000 path: ((dx<<16)/dy) since dy carries <<16
    // Our EdgeSlope uses ((dx<<16)/dy); here dx=12<<16, dy=11<<16 -> (12<<16<<16)/(11<<16) = (12<<16)/11
    CHECK_EQ(rs.xLeftStep, (int)(((long long)((12 << 16)) << 16) / (11 << 16)));
    // light step similarly: ((120-10)<<16 << 16)/(11<<16) = (110<<16)/11
    CHECK_EQ(rs.uLeftStep, (int)(((long long)((110 << 16)) << 16) / (11 << 16)));
    (void)stepX;
    // at vy[a] already integer (1<<16) -> sub = 0 -> xLeft == vx[a], uLeft==vlight[a]
    CHECK_EQ(rs.xLeft, 2 << 16);
    CHECK_EQ(rs.uLeft, 10 << 16);
}

// --- FillSpanTextured: self-modifying-span translation, affine texel fetch ---
TEST(RenderRaster, FillSpanTexturedAffine) {
    // 4x4 texture, widthShift=2 (row stride 4), index i = (v*4+u). Palette maps
    // index k -> 0x1000+k so we can read back the exact fetched texel index.
    unsigned char tex[16];
    for (int i = 0; i < 16; ++i) tex[i] = (unsigned char)i;
    unsigned short pal[256];
    for (int i = 0; i < 256; ++i) pal[i] = (unsigned short)(0x1000 + i);

    RasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    rs.spanLen = 4;
    SpanTexParams p{};
    p.texBase = tex; p.palBase = pal; p.texelMask = 0xF; p.widthShift = 2;
    p.uStepFrac = 1 << 16;  // +1 U per pixel
    p.vStep = 0;            // constant row
    unsigned short dst[4] = {0, 0, 0, 0};
    // start u=0, v=2<<16 -> row 2 -> indices 8,9,10,11
    FillSpanTextured(rs, dst, 0, 2 << 16, p);
    CHECK_EQ((int)dst[0], 0x1000 + 8);
    CHECK_EQ((int)dst[1], 0x1000 + 9);
    CHECK_EQ((int)dst[2], 0x1000 + 10);
    CHECK_EQ((int)dst[3], 0x1000 + 11);
}

// --- FillSpanTexturedMasked: colour-key index 0 is skipped ------------------
TEST(RenderRaster, FillSpanTexturedMaskedColorKey) {
    unsigned char tex[4] = {0, 5, 0, 7}; // index 0 = transparent
    unsigned short pal[256];
    for (int i = 0; i < 256; ++i) pal[i] = (unsigned short)(0x2000 + i);
    RasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    rs.spanLen = 4;
    SpanTexParams p{};
    p.texBase = tex; p.palBase = pal; p.texelMask = 0x3; p.widthShift = 2;
    p.uStepFrac = 1 << 16; p.vStep = 0;
    unsigned short dst[4] = {0xAAAA, 0xAAAA, 0xAAAA, 0xAAAA};
    FillSpanTextured(rs, dst, 0, 0, p);              // opaque writes all
    CHECK_EQ((int)dst[0], 0x2000 + 0);
    unsigned short dst2[4] = {0xAAAA, 0xBBBB, 0xCCCC, 0xDDDD};
    FillSpanTexturedMasked(rs, dst2, 0, 0, p);
    CHECK_EQ((int)dst2[0], 0xAAAA);                  // index 0 skipped
    CHECK_EQ((int)dst2[1], 0x2000 + 5);
    CHECK_EQ((int)dst2[2], 0xCCCC);                  // index 0 skipped
    CHECK_EQ((int)dst2[3], 0x2000 + 7);
}

// --- Ror4 helper sanity (matches x86 ror imm) -------------------------------
TEST(RenderRaster, Ror4Matches) {
    CHECK_EQ((int)Ror4(0x00012345u, 16), (int)0x23450001u);
    CHECK_EQ((int)Ror4(0x80000001u, 1), (int)0xC0000000u);
    CHECK_EQ((int)Ror4(0x12345678u, 0), (int)0x12345678u);
}

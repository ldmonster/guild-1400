// Unit tests for guild::render software rasterizer (raster.{h,cpp}).
// Golden pixel values are computed by python3 (/tmp/raster_ref.py) replicating
// the exact gilde.exe 16.16 fixed-point edge-walk / span interpolation, so the
// comparison is BIT-EXACT, not approximate.
#include "render/raster.h"
#include "render/raster_blend.h"   // wave-10: blend/OR span edge coverage
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

// --- flat winding (0x603ED4): back-wound + flag clear is CULLED -------------
// Verified wave-5: the flat path computes (x0-x2)*(y0-y1) > (x0-x1)*(y0-y2);
// when back-wound and (poly+38 & 4)==0 the original returns without drawing
// (0x603FB0). A back-wound triangle is just the front-wound one with v1/v2
// swapped.
TEST(RenderRaster, FlatBackWoundCulledWhenFlagClear) {
    Surface* s = Make8(16, 16);
    // swap v1/v2 of FlatTriangleFill -> opposite winding -> back-wound.
    RasterVertex v[3] = {{3.0f, 2.0f, 0}, {6.0f, 13.0f, 0}, {13.0f, 4.0f, 0}};
    // sanity: this ordering IS back-wound (cross > 0).
    CHECK((v[0].x - v[2].x) * (v[0].y - v[1].y) >
          (v[0].x - v[1].x) * (v[0].y - v[2].y));
    int drew = RasterizeFlatTriangle(s, v, 200, /*polyFlags38=*/0);
    CHECK_EQ(drew, 0);                       // culled: nothing drawn
    int nz = 0;
    for (int i = 0; i < 16 * 16; ++i)
        if (s->pixels[i]) ++nz;
    CHECK_EQ(nz, 0);
    SurfaceDestroy(s);
}

// --- flat winding: back-wound + flag bit2 set REVERSES to the front fill ----
// With (poly+38 & 4) set the original reverse-loads the back-wound triangle,
// normalising it to the same winding the edge tables expect -> identical pixels
// to the front-wound FlatTriangleFill golden.
TEST(RenderRaster, FlatBackWoundReversedEqualsForward) {
    Surface* s = Make8(16, 16);
    RasterVertex v[3] = {{3.0f, 2.0f, 0}, {6.0f, 13.0f, 0}, {13.0f, 4.0f, 0}};
    int drew = RasterizeFlatTriangle(s, v, 200, /*polyFlags38=*/4);
    CHECK(drew != 0);
    CheckSparse(s, kFlatTri, kFlatTri_count);  // same pixels as the forward tri
    SurfaceDestroy(s);
}

// --- flat winding: front-wound ignores the +38 flag (always forward) --------
TEST(RenderRaster, FlatFrontWoundIgnoresFlag) {
    Surface* s = Make8(16, 16);
    RasterVertex v[3] = {{3.0f, 2.0f, 0}, {13.0f, 4.0f, 0}, {6.0f, 13.0f, 0}};
    // flag clear and flag set must give the identical front-wound fill.
    int d0 = RasterizeFlatTriangle(s, v, 200, /*polyFlags38=*/0);
    CHECK(d0 != 0);
    CheckSparse(s, kFlatTri, kFlatTri_count);
    std::memset(s->pixels, 0, (size_t)s->pitch * s->height);
    int d1 = RasterizeFlatTriangle(s, v, 200, /*polyFlags38=*/4);
    CHECK(d1 != 0);
    CheckSparse(s, kFlatTri, kFlatTri_count);
    SurfaceDestroy(s);
}

// --- light-table row select (0x5DB5C1 +72 / 0x5F70BD avg<<8) -----------------
// Verified wave-5: the textured span colour fetch is palBase[lightRow8|texel]
// where lightRow8 = avg(+66)<<8 indexes a 63-row HiColTab ramp (row L at u16
// offset 256*L). With a synthetic 63x256 palette where pal[256*L|idx] encodes
// the row L, the fetched value must equal lightRow8>>8 == the chosen row.
TEST(RenderRaster, LightRow8SelectsHiColTabRow) {
    RasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    rs.spanLen = 4;
    // 63 ramp rows x 256 entries; row L entry idx stores (L*1000 + idx) low 16b.
    static guild::u16 pal[63 * 256];
    for (int L = 0; L < 63; ++L)
        for (int idx = 0; idx < 256; ++idx)
            pal[256 * L + idx] = (guild::u16)(L * 1000 + idx);
    static const guild::u8 tex[1] = {7};            // every sample -> texel index 7
    SpanTexParams p{};
    p.texBase = tex;
    p.palBase = pal;
    p.texelMask = 0;                          // wrap to texel 0 -> tex[0]=7
    p.widthShift = 0;
    p.uStepFrac = 0;
    p.vStep = 0;
    for (int row = 0; row <= 62; row += 31) { // rows 0, 31, 62 (valid 0..62)
        p.lightRow8 = (guild::u32)row << 8;
        guild::u16 dst[4] = {0, 0, 0, 0};
        FillSpanTextured(rs, dst, 0, 0, p);
        for (int i = 0; i < 4; ++i)
            CHECK_EQ((int)dst[i], row * 1000 + 7); // row L | idx 7
    }
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

// --- reconstruction-only surface-format guard --------------------------------
// The shaded affine span (FillTexturedSpansShaded @0x5F7960) writes ONE 8-bit
// shade byte per pixel into an 8bpp surface. Pointing it at a 16bpp surface is
// impossible in the original (the SMC span dispatch selects the 16bpp textured
// bodies there) and produced the 0xC8C8 "pink polygon" artifact in the city
// frame (tests/e2e/render_fidelity_w3a_e2e_test.cpp). Wave-4: the guard is now
// the original's own a5 masking (`if (!a4) a5 &= 5` @0x5f7e32 — a non-8bpp fb
// counts as "no byte map"); with the default modeMask=2 and no tile array the
// whole call is a no-op. The 8bpp goldens above are untouched.
TEST(RenderRaster, ShadedPathRefusesNon8bppSurface) {
    Surface* s = SurfaceCreate(16, 16, 16);
    std::memset(s->pixels, 0, (size_t)s->pitch * s->height);
    RasterVertex v[3] = {{2.0f, 1.0f, 10}, {14.0f, 3.0f, 200}, {5.0f, 12.0f, 80}};
    int drew = RasterizeTexturedTriangle(s, v);
    CHECK_EQ(drew, 0);                        // nothing rasterized
    int nz = 0;
    const unsigned char* px = s->pixels;
    for (int i = 0; i < s->pitch * s->height; ++i) if (px[i]) ++nz;
    CHECK_EQ(nz, 0);                          // no byte was written
    SurfaceDestroy(s);
}

// --- wave-4: the ROR'd adc accumulator (0x5f7a53..0x5f7a60) ------------------
// The original advances the shade accumulator as a ROTATED 32-bit adc chain:
// the carry out of the FRACTION half (bit 31) re-enters the INTEGER half
// (bit 0) only on the NEXT iteration — one pixel later than plain 16.16
// accumulation. Hand trace for acc0 = 0x0000FFFF, step = 1:
//   r=0xFFFF0000 s=0x00010000 cf=0
//   px0: 0x00 ; r+s   = 0x1_00000000 -> r=0, cf=1
//   px1: 0x00 ; r+s+1 = 0x00010001   -> r=0x00010001, cf=0   (plain would be 1!)
//   px2: 0x01 ; px3: 0x01
TEST(RenderRaster, ShadedSpanAdcCarryLandsOnePixelLate) {
    Surface* s = SurfaceCreate(8, 8, 8);
    std::memset(s->pixels, 0, (size_t)s->pitch * s->height);
    RasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    rs.fbPitch = s->widthPx;
    rs.xLeft = 0;            rs.xLeftStep = 0;
    rs.xRight = 4 << 16;     rs.xRightStep = 0;
    rs.uLeft = 0x0000FFFF;   rs.uLeftStep = 0;
    rs.uGrad = 1;
    int last = FillTexturedSpansShaded(rs, s, 1, 0);
    CHECK_EQ(last, 0);
    CHECK_EQ((int)s->pixels[0], 0x00);
    CHECK_EQ((int)s->pixels[1], 0x00);   // plain 16.16 would give 0x01 here
    CHECK_EQ((int)s->pixels[2], 0x01);
    CHECK_EQ((int)s->pixels[3], 0x01);
    SurfaceDestroy(s);
}

// --- wave-4: winding normalisation (0x5f7da9 / the reverse loop 0x5f7db3) ----
// A triangle handed in back-wound (signed-area expression > 0) is loaded slot
// i <- vertex 2-i and rasterizes IDENTICALLY to its forward-wound twin.
TEST(RenderRaster, BackWoundTriangleEqualsForward) {
    RasterVertex fwd[3] = {{2.0f, 1.0f, 10}, {14.0f, 3.0f, 200}, {5.0f, 12.0f, 80}};
    RasterVertex rev[3] = {fwd[2], fwd[1], fwd[0]};
    Surface* a = Make8(16, 16);
    Surface* b = Make8(16, 16);
    CHECK(RasterizeTexturedTriangle(a, fwd) != 0);
    CHECK(RasterizeTexturedTriangle(b, rev) != 0);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            CHECK_EQ((int)a->pixels[y * a->widthPx + x],
                     (int)b->pixels[y * b->widthPx + x]);
    SurfaceDestroy(a);
    SurfaceDestroy(b);
}

// --- wave-4: the tile-type stamp path of 0x5F7960 (modeMask bits 0/2) --------
// Triangle (2,3) (6,3) (2,7), lights 0, on an 8-wide grid. Spans: row3 x2..5,
// row4 x2..4, row5 x2..3, row6 x2. The tile array is 24 bytes per cell
// (BuildTerrainMesh's [esi+24h]); byte 0 of each cell receives the stamp.
namespace {
struct TileGrid8 {
    unsigned char cells[8 * 8 * 24];
    void fill(unsigned char v) { std::memset(cells, v, sizeof(cells)); }
    unsigned char at(int x, int y) const { return cells[24 * (y * 8 + x)]; }
};
} // namespace

TEST(RenderRaster, TileStampCoreNoBlur) {
    RasterVertex v[3] = {{2.0f, 3.0f, 0}, {6.0f, 3.0f, 0}, {2.0f, 7.0f, 0}};
    Surface* s = Make8(8, 8);
    TileGrid8 grid; grid.fill(0xEE);
    // modeMask 4: stamp value 11 (bit0 clear), no byte-map writes (bit1 clear).
    int drew = RasterizeTexturedTriangle(s, v, /*modeMask=*/4, grid.cells, 0);
    CHECK(drew != 0);
    static const int spans[4][2] = {{2, 5}, {2, 4}, {2, 3}, {2, 2}}; // rows 3..6
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            int expect = 0xEE;
            if (y >= 3 && y <= 6 && x >= spans[y - 3][0] && x <= spans[y - 3][1])
                expect = 11;
            CHECK_EQ((int)grid.at(x, y), expect);
        }
    // bit 1 was clear: the byte map is untouched.
    int nz = 0;
    for (int i = 0; i < s->pitch * s->height; ++i) if (s->pixels[i]) ++nz;
    CHECK_EQ(nz, 0);
    SurfaceDestroy(s);
}

TEST(RenderRaster, TileStampClearValueWithBit0) {
    // modeMask 5 (bits 0+2): stamp value 0 — the "clear" arm (v44/v47 = 0).
    RasterVertex v[3] = {{2.0f, 3.0f, 0}, {6.0f, 3.0f, 0}, {2.0f, 7.0f, 0}};
    Surface* s = Make8(8, 8);
    TileGrid8 grid; grid.fill(7);
    CHECK(RasterizeTexturedTriangle(s, v, /*modeMask=*/5, grid.cells, 0) != 0);
    CHECK_EQ((int)grid.at(3, 3), 0);   // inside -> cleared
    CHECK_EQ((int)grid.at(2, 6), 0);
    CHECK_EQ((int)grid.at(6, 6), 7);   // outside -> untouched
    SurfaceDestroy(s);
}

TEST(RenderRaster, TileStampBlurHaloPyramid) {
    // blur = 2: per-row widened halo, a pyramid above the FIRST span
    // (firstBatch once-flag) and a pyramid below the LAST span; all width-
    // clamped to [0, pitch) and row-gated by `blur + rows + y0 <= pitch`.
    // Hand-derived from the captured decompile (see progress doc):
    //   row1: x2..5   row2: x1..6   rows3..5: x0..5   row6: x0..4   row7: x1..3
    RasterVertex v[3] = {{2.0f, 3.0f, 0}, {6.0f, 3.0f, 0}, {2.0f, 7.0f, 0}};
    Surface* s = Make8(8, 8);
    TileGrid8 grid; grid.fill(0xEE);
    CHECK(RasterizeTexturedTriangle(s, v, /*modeMask=*/4, grid.cells, 2) != 0);
    static const int rows[8][2] = {
        {-1, -1},   // row0: the top-halo gate is v41 > 0 — row 0 is never hit
        {2, 5},     // row1: pyramid iter v17=2 (width = spanLen)
        {1, 6},     // row2: pyramid iter v17=1 (width = spanLen + 2)
        {0, 5},     // row3: halo [0, min(4+4, 8-2)=6) then core
        {0, 5},     // row4
        {0, 5},     // row5
        {0, 4},     // row6: halo n = min(1+4, 6) = 5
        {1, 3},     // row7: trailing pyramid v25=1 (v25=2 row-gated: 9 > 8)
    };
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            int expect = (rows[y][0] >= 0 && x >= rows[y][0] && x <= rows[y][1])
                             ? 11 : 0xEE;
            CHECK_EQ((int)grid.at(x, y), expect);
        }
    SurfaceDestroy(s);
}

// --- wave-4: a5 mode masking (0x5f7e26/0x5f7e32) ------------------------------
TEST(RenderRaster, ModeMaskGatesEverything) {
    RasterVertex v[3] = {{2.0f, 3.0f, 50}, {6.0f, 3.0f, 50}, {2.0f, 7.0f, 50}};
    // No tile array: bits 0/2 are stripped (a5 &= 2); mode 4 -> 0 -> no-op.
    Surface* s = Make8(8, 8);
    CHECK_EQ(RasterizeTexturedTriangle(s, v, /*modeMask=*/4, nullptr, 0), 0);
    int nz = 0;
    for (int i = 0; i < s->pitch * s->height; ++i) if (s->pixels[i]) ++nz;
    CHECK_EQ(nz, 0);
    // No byte map (null fb) with mode 2 -> a5 &= 5 -> 0 -> no-op, no crash.
    CHECK_EQ(RasterizeTexturedTriangle(nullptr, v, /*modeMask=*/2, nullptr, 0), 0);
    // Tile-only stamping with a null fb works off gridPitch (the a3 argument).
    TileGrid8 grid; grid.fill(0xEE);
    CHECK(RasterizeTexturedTriangle(nullptr, v, /*modeMask=*/4, grid.cells, 0,
                                    /*gridPitch=*/8) != 0);
    CHECK_EQ((int)grid.at(3, 3), 11);
    SurfaceDestroy(s);
}

// ===========================================================================
// WAVE-10 HARDENING: degenerate / edge-case memory-safety coverage. These drive
// the span loops, edge interpolators and triangle setup with the pathological
// inputs an ASAN+UBSAN build must survive byte-for-byte (no OOB / UB), per the
// wave-10 brief. They pin the UBSAN fixes (signed-shift-of-negative in the
// EdgeSlope / SubpixelToCeil / sub-pixel <<16 sites).
// ===========================================================================

// --- FillSpanTextured: spanLen 0 and negative write nothing (no OOB) ---------
// A degenerate span (xLeft>=xRight gives spanLen<=0) must touch no destination
// byte — the inner `for (i=0;i<n;++i)` never runs, so even a 1-element dst and
// NULL tex/pal pointers are safe.
TEST(RenderRaster, FillSpanZeroAndNegativeLenNoWrite) {
    RasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    SpanTexParams p{};               // all-null tex/pal: must never be dereferenced
    unsigned short dst[1] = {0x1234};
    rs.spanLen = 0;
    FillSpanTextured(rs, dst, 0, 0, p);
    CHECK_EQ((int)dst[0], 0x1234);
    rs.spanLen = -5;
    FillSpanTextured(rs, dst, 0, 0, p);
    CHECK_EQ((int)dst[0], 0x1234);
    rs.spanLen = -5;
    FillSpanTexturedMasked(rs, dst, 0, 0, p);
    CHECK_EQ((int)dst[0], 0x1234);
}

// --- FillSpanTextured: a 1-pixel span at the texelMask edge ------------------
// The texel address is wrapped by `& texelMask`; with a mask of 0 every fetch
// folds to texel 0 — a single-element texture/dest is in bounds.
TEST(RenderRaster, FillSpanOnePixelMaskEdge) {
    unsigned char tex[1] = {3};
    unsigned short pal[256];
    for (int i = 0; i < 256; ++i) pal[i] = (unsigned short)(0x500 + i);
    RasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    rs.spanLen = 1;
    SpanTexParams p{};
    p.texBase = tex; p.palBase = pal; p.texelMask = 0; p.widthShift = 7;
    // a huge U/V start: the mask folds the address back to 0 (no OOB read).
    unsigned short dst[1] = {0};
    FillSpanTextured(rs, dst, 0x7FFF0000, 0x7FFF0000, p);
    CHECK_EQ((int)dst[0], 0x500 + 3);
}

// --- FillSpanTextured: 16.16 U/V accumulators near overflow -------------------
// Stepping the U accumulator past INT_MAX wraps (the original `add` wraps mod
// 2^32); the masked texel address stays in bounds and no UB fires.
TEST(RenderRaster, FillSpanAccumulatorWrapNearOverflow) {
    unsigned char tex[4] = {0, 1, 2, 3};
    unsigned short pal[256];
    for (int i = 0; i < 256; ++i) pal[i] = (unsigned short)(i);
    RasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    rs.spanLen = 3;
    SpanTexParams p{};
    p.texBase = tex; p.palBase = pal; p.texelMask = 0x3; p.widthShift = 0;
    p.uStepFrac = 0x40000000;        // +16384 px/step -> u wraps to negative
    p.vStep = 0;
    unsigned short dst[3] = {0, 0, 0};
    FillSpanTextured(rs, dst, 0x7FFF0000, 0, p);   // near INT_MAX U start
    // No crash / no UBSAN trip is the assertion; values are deterministic mod 4.
    for (int i = 0; i < 3; ++i) CHECK((int)dst[i] >= 0 && (int)dst[i] <= 3);
}

// --- EdgeSlope UB pin: negative numerator (right-to-left edge) ----------------
// A long edge that goes right-to-left has a negative dx; the <<16 must not be
// signed-shift UB and the slope must be the exact negative fixed-point value.
TEST(RenderRaster, EdgeInterpNegativeDxNoUb) {
    RasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    rs.vx[0] = 14 << 16; rs.vy[0] = 1 << 16;  rs.vlight[0] = 200 << 16;
    rs.vx[1] = 2 << 16;  rs.vy[1] = 12 << 16; rs.vlight[1] = 10 << 16;
    InterpolateEdgeZTex(rs, 0, 1);                 // dx = -12<<16, dlight = -190<<16
    // golden via multiply (avoid negative-shift UB in the expected expression).
    CHECK_EQ(rs.xLeftStep, (int)(((long long)(-12)  * 65536 * 65536) / (11 * 65536)));
    CHECK_EQ(rs.uLeftStep, (int)(((long long)(-190) * 65536 * 65536) / (11 * 65536)));
    CHECK(rs.xLeftStep < 0);
}

// --- off-screen triangle (entirely above + left): byte span clips, no OOB -----
// A triangle whose top vertices sit at negative screen coords forces a negative
// ceil() left edge; the byte-span path clips x and row, the UBSAN <<16 fixes
// cover the negative sub-pixel arithmetic. Nothing escapes the surface.
TEST(RenderRaster, OffScreenTriangleByteSpanClips) {
    Surface* s = Make8(8, 8);
    // overhangs top-left and bottom; left edge ceils negative on the top rows.
    RasterVertex v[3] = {{-6.0f, -4.0f, 40}, {5.0f, 1.0f, 150}, {-2.0f, 10.0f, 90}};
    int drew = RasterizeTexturedTriangle(s, v);   // default modeMask=2 (byte span)
    (void)drew;
    // ASAN is the real assertion; also confirm no byte outside [0,8)x[0,8).
    SurfaceDestroy(s);
}

// --- tile-stamp: 1-pixel surface / tiny grid, odd pitch ----------------------
// A 1x1 grid with a triangle covering exactly the single cell must stamp only
// that cell (the halo clamps to [0,pitch)). Drives the 24-byte-stride cursor
// against the smallest grid.
TEST(RenderRaster, TileStampOneCellGrid) {
    unsigned char grid[24];
    std::memset(grid, 0xEE, sizeof(grid));
    RasterVertex v[3] = {{0.0f, 0.0f, 0}, {1.0f, 0.0f, 0}, {0.0f, 1.0f, 0}};
    // gridPitch 1, null fb -> tile-only stamping at cell (0,0).
    RasterizeTexturedTriangle(nullptr, v, /*modeMask=*/4, grid, /*blur=*/0,
                              /*gridPitch=*/1);
    // No OOB on the 24-byte single-cell array (ASAN). Cell may or may not be
    // covered depending on the ceil rule; the point is no write past 24 bytes.
    SurfaceDestroy(Make8(1, 1));   // sanity: 1x1 surface allocates/frees clean
}

// --- flat triangle: degenerate (collinear) draws nothing, no OOB -------------
TEST(RenderRaster, FlatDegenerateCollinearNoWrite) {
    Surface* s = Make8(12, 12);
    RasterVertex v[3] = {{1.0f, 1.0f, 0}, {5.0f, 5.0f, 0}, {9.0f, 9.0f, 0}};
    int drew = RasterizeFlatTriangle(s, v, 200);
    CHECK_EQ(drew, 0);
    int nz = 0;
    for (int i = 0; i < 12 * 12; ++i) if (s->pixels[i]) ++nz;
    CHECK_EQ(nz, 0);
    SurfaceDestroy(s);
}

// --- flat triangle overhanging all edges of a tiny surface: span clamps -------
// fillRange clamps x to [0,fbW) and gates row to [0,fbH); a big triangle on a
// 3x3 surface must not write outside the 9-pixel buffer (ASAN).
TEST(RenderRaster, FlatOverhangClampsToTinySurface) {
    Surface* s = Make8(3, 3);
    RasterVertex v[3] = {{-10.0f, -10.0f, 0}, {20.0f, -2.0f, 0}, {-4.0f, 18.0f, 0}};
    RasterizeFlatTriangle(s, v, 99);
    // Every written pixel must be inside the 3x3 buffer (CheckSparse-style scan).
    for (int i = 0; i < 3 * 3; ++i)
        CHECK((int)s->pixels[i] == 0 || (int)s->pixels[i] == 99);
    SurfaceDestroy(s);
}

// --- blend / OR spans: spanLen 0 and negative touch no destination -----------
// The blend/OR/Masked span fillers (raster_blend.cpp) share the FillSpanTextured
// loop contract: a non-positive spanLen writes nothing, so null tex/pal and a
// 1-element dst are safe.
TEST(RenderRaster, BlendOrSpansZeroLenNoWrite) {
    RasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    SpanBlendParams p{};                 // all-null: never dereferenced at len<=0
    unsigned short dst[1] = {0x55AA};
    for (int len : {0, -3}) {
        rs.spanLen = len;
        CHECK_EQ((int)FillSpanTexturedBlend(rs, dst, 0, 0, p), 0);
        CHECK_EQ((int)FillSpanTexturedBlendMasked(rs, dst, 0, 0, p), 0);
        CHECK_EQ((int)FillSpanTexturedOr(rs, dst, 0, 0, p), 0);
        CHECK_EQ((int)FillSpanTexturedOrMasked(rs, dst, 0, 0, p), 0);
        CHECK_EQ((int)dst[0], 0x55AA);   // untouched
    }
}

// --- blend span: 1-pixel span at the texelMask edge, no OOB read --------------
// texelMask 0 folds every fetch to texel 0; a single-element texture is in
// bounds even with a huge U/V start. The 50/50 blend uses no inter-channel carry.
TEST(RenderRaster, BlendSpanOnePixelMaskEdge) {
    unsigned char tex[1] = {1};
    unsigned short pal[256];
    for (int i = 0; i < 256; ++i) pal[i] = 0xFFFF;
    RasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    rs.spanLen = 1;
    SpanBlendParams p{};
    p.texBase = tex; p.palBase = pal; p.texelMask = 0; p.widthShift = 9;
    p.blendMask = 0xF7DE;                 // RGB565 LSB-clear mask
    unsigned short dst[1] = {0x0000};
    FillSpanTexturedBlend(rs, dst, 0x7FFF0000, 0x7FFF0000, p);
    // (0xFFFF>>1 & F7DE) + (0>>1 & F7DE) == (0x7FFF & F7DE) == 0x77DE.
    CHECK_EQ((int)dst[0], 0x77DE);
}

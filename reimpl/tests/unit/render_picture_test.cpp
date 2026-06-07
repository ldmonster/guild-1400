#include "tests/framework/test.h"
#include "render/picture_io.h"
#include "render/paintbox_shape.h"
#include "render/quant_octree.h"
#include "render/raster_blend.h"
#include "render/raster.h"
#include "render/mirror_project.h"
#include "render/colorformat.h"
#include <vector>
#include <cstring>
#include <algorithm>

using namespace guild;
using namespace guild::render;

namespace {
inline u16 pack565(u8 r, u8 g, u8 b) {
    return (u16)((((u16)g >> 2) << 5) | (((u16)r >> 3) << 11) | ((u16)b >> 3));
}
inline u16 pack555(u8 r, u8 g, u8 b) {
    return (u16)((((int)b >> 3) & 0x1F) | (32 * (((int)g >> 3) & 0x1F)) | ((((int)r >> 3) & 0x1F) << 10));
}
}

// ---------------------------------------------------------------------------
// Picture: SwapRowBytes is a pure byte-pair swap.
// ---------------------------------------------------------------------------
TEST(RenderPicture, SwapRowBytes) {
    u16 px[4] = {0x1234, 0xABCD, 0x00FF, 0xFF00};
    PictureSwapRowBytes(px, 4);
    CHECK_EQ(px[0], (u16)0x3412);
    CHECK_EQ(px[1], (u16)0xCDAB);
    CHECK_EQ(px[2], (u16)0xFF00);
    // Faithful off-by-one: the loop bound is 2*count-2, so the LAST u16 is NOT
    // swapped (the original leaves the final pixel untouched).
    CHECK_EQ(px[3], (u16)0xFF00);
    // swapping the first three back restores them (last stays put).
    PictureSwapRowBytes(px, 4);
    CHECK_EQ(px[0], (u16)0x1234);
    CHECK_EQ(px[3], (u16)0xFF00);
}

// ---------------------------------------------------------------------------
// Picture: 24bpp TGA-style load packs B,G,R triples into RGB555 (top-down).
// ---------------------------------------------------------------------------
TEST(RenderPicture, LoadBmp24Packs555) {
    // 2x2 image, descriptor=32 (top-down). Header 18 bytes then BGR rows.
    std::vector<u8> file(18, 0);
    file[12] = 2; file[13] = 0;          // width = 2
    file[14] = 2; file[15] = 0;          // height = 2
    file[16] = 24;                        // bpp
    file[17] = 32;                        // descriptor: top-down
    // pixels (B,G,R): (255,0,0)=red, (0,255,0)=green, (0,0,255)=blue, (255,255,255)
    u8 pix[] = { 0,0,255,  0,255,0,   255,0,0,  255,255,255 };
    file.insert(file.end(), pix, pix + sizeof(pix));

    u16 fb[2 * 2] = {0};
    int w, h;
    CHECK(PictureLoadBmp24(file, fb, 2, w, h));
    CHECK_EQ(w, 2); CHECK_EQ(h, 2);
    CHECK_EQ(fb[0], pack555(255, 0, 0));
    CHECK_EQ(fb[1], pack555(0, 255, 0));
    CHECK_EQ(fb[2], pack555(0, 0, 255));
    CHECK_EQ(fb[3], pack555(255, 255, 255));
}

// ---------------------------------------------------------------------------
// Picture: 16bpp TGA load/save roundtrip through the framebuffer.
// ---------------------------------------------------------------------------
TEST(RenderPicture, TgaRoundtrip) {
    const int W = 3, H = 2;
    u16 fb[W * H];
    for (int i = 0; i < W * H; ++i) fb[i] = (u16)(0x1000 * (i + 1) + i);
    PictureFile saved = PictureSaveTga(fb, W, W, H);
    CHECK(saved.size() == 18u + (size_t)2 * W * H);

    u16 fb2[W * H] = {0};
    int w, h;
    CHECK(PictureLoadTga(saved, fb2, W, w, h));
    CHECK_EQ(w, W); CHECK_EQ(h, H);
    // SaveTga byte-swaps each pixel on disk (and LoadTga reads straight), so the
    // reloaded pixels are the byte-swapped reference — this is faithful to the
    // original (SwapRowBytes on save, no swap on load).
    for (int i = 0; i < W * H; ++i) {
        // last u16 of the written region is left unswapped (2*count-2 bound).
        u16 sw = (i == W * H - 1) ? fb[i] : (u16)((fb[i] >> 8) | (fb[i] << 8));
        CHECK_EQ(fb2[i], sw);
    }
}

// ---------------------------------------------------------------------------
// Picture: SaveBmp24 header + B,G,R pixel order (top-down, negative height).
// ---------------------------------------------------------------------------
TEST(RenderPicture, SaveBmp24Header) {
    const int W = 2, H = 1;
    u8 rgb[] = { 10, 20, 30,  40, 50, 60 };   // R,G,B per pixel
    PictureFile f = PictureSaveBmp24(W, H, rgb);
    CHECK_EQ(f[0], (u8)'B'); CHECK_EQ(f[1], (u8)'M');
    // dataOffset at +10 = 58
    u32 off = f[10] | (f[11] << 8) | (f[12] << 16) | (f[13] << 24);
    CHECK_EQ(off, 58u);
    CHECK(f.size() == 58u + (size_t)3 * W * H);
    // info header height at +0x16 (=22) = -1
    i32 ih = (i32)(f[22] | (f[23] << 8) | (f[24] << 16) | (f[25] << 24));
    CHECK_EQ(ih, -1);
    // first pixel on disk: B,G,R = 30,20,10
    CHECK_EQ(f[58], (u8)30);
    CHECK_EQ(f[59], (u8)20);
    CHECK_EQ(f[60], (u8)10);
}

// ---------------------------------------------------------------------------
// Picture: BlitRegion / FillRows / DrawBorder.
// ---------------------------------------------------------------------------
TEST(RenderPicture, BlitFillBorder) {
    const int FW = 8, FH = 8;
    std::vector<u16> fb(FW * FH, 0);
    // fill a 4x4 source region top-left with 0xABCD then blit to (3,3)
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) fb[y * FW + x] = 0xABCD;
    PictureBlitRegion(fb.data(), 0, 0, 2, 2, fb.data(), 3, 3, FW);
    CHECK_EQ(fb[3 * FW + 3], (u16)0xABCD);
    CHECK_EQ(fb[4 * FW + 4], (u16)0xABCD);

    // FillRows zeroes the blitted block
    PictureFillRows(fb.data(), 3, 3, 2, 2, FW);
    CHECK_EQ(fb[3 * FW + 3], (u16)0);
    CHECK_EQ(fb[4 * FW + 4], (u16)0);

    // DrawBorder: outline a 3x3 rect at (1,1) -> white edges (255)
    std::fill(fb.begin(), fb.end(), 0);
    PictureDrawBorder(fb.data(), 1, 1, 3, 3, FW, FH);
    CHECK_EQ(fb[1 * FW + 1], (u16)255);            // top-left
    CHECK_EQ(fb[1 * FW + 4], (u16)255);            // top-right (x+w)
    CHECK_EQ(fb[4 * FW + 1], (u16)255);            // bottom-left (y+h)
}

// ---------------------------------------------------------------------------
// Paintbox shape: palette-indexed blit -> 565 framebuffer.
// ---------------------------------------------------------------------------
TEST(RenderPicture, PaletteBlit) {
    // palette: 4 entries (R,G,B,X), rest zero
    u8 pal[256 * 4] = {0};
    auto setpal = [&](int i, u8 r, u8 g, u8 b) {
        pal[4 * i + 0] = r; pal[4 * i + 1] = g; pal[4 * i + 2] = b;
    };
    setpal(1, 255, 0, 0); setpal(2, 0, 255, 0); setpal(3, 0, 0, 255);
    u8 src[] = { 1, 2, 3, 0 };   // 2x2 indices
    u16 fb[2 * 2] = {0};
    Surface_BlitPaletteToPixels(fb, 2, src, 2, 2, pal, Format565());
    CHECK_EQ(fb[0], pack565(255, 0, 0));
    CHECK_EQ(fb[1], pack565(0, 255, 0));
    CHECK_EQ(fb[2], pack565(0, 0, 255));
    CHECK_EQ(fb[3], pack565(0, 0, 0));
}

// ---------------------------------------------------------------------------
// Octree quantizer: a 4-colour image must reduce to a palette containing those
// exact colours (top-5-bit centres), and every pixel maps back to its colour.
// ---------------------------------------------------------------------------
TEST(RenderPicture, OctreeQuantizeFourColors) {
    // colours chosen on 8-bit-bucket centres so the centroid is exact.
    struct C { u8 r, g, b; };
    C cols[4] = { {252, 4, 4}, {4, 252, 4}, {4, 4, 252}, {252, 252, 252} };
    const int W = 4, H = 4;   // 16 pixels, 4 each
    std::vector<u8> img(W * H * 3);
    for (int i = 0; i < W * H; ++i) {
        const C& c = cols[i % 4];
        img[3 * i + 0] = c.r; img[3 * i + 1] = c.g; img[3 * i + 2] = c.b;
    }
    std::vector<u8> idx(W * H);
    u8 palOut[768] = {0};
    CHECK(QuantBuildPalette(img.data(), W, H, 16, idx.data(), palOut));

    // Each pixel's palette colour must match its source colour (5-bit accurate).
    for (int i = 0; i < W * H; ++i) {
        u8 pi = idx[i];
        u8 pr = palOut[pi], pg = palOut[pi + 256], pb = palOut[pi + 512];
        const C& c = cols[i % 4];
        CHECK_EQ((int)(pr & 0xF8), (int)(c.r & 0xF8));
        CHECK_EQ((int)(pg & 0xF8), (int)(c.g & 0xF8));
        CHECK_EQ((int)(pb & 0xF8), (int)(c.b & 0xF8));
    }
    // distinct colours -> at least 4 distinct palette indices.
    bool seen[256] = {false};
    int distinct = 0;
    for (int i = 0; i < W * H; ++i) if (!seen[idx[i]]) { seen[idx[i]] = true; ++distinct; }
    CHECK(distinct >= 4);
}

// ---------------------------------------------------------------------------
// Raster blend / OR span variants: golden pixels.
// ---------------------------------------------------------------------------
TEST(RenderPicture, BlendOrSpans) {
    // 1x1 texture: index 1 -> palette colour red (565). texelMask masks to index.
    u8 tex[2] = { 0, 0 };  // index 0 = transparent key, index 1 used
    u16 palLut[2];
    palLut[0] = 0x0000;
    palLut[1] = pack565(255, 0, 0);  // 0xF800
    tex[0] = 1;  // always fetch index 1 (addr 0 after mask)

    RasterState rs{};
    rs.spanLen = 3;
    SpanBlendParams p{};
    p.texBase = tex;
    p.palBase = palLut;
    p.texelMask = 0;          // addr always 0 -> tex[0]=1
    p.widthShift = 0;
    p.uStepFrac = 0;
    p.vStep = 0;
    p.blendMask = 0xF7DE;     // 565 LSB-clear mask

    u16 blue = pack565(0, 0, 255);  // 0x001F
    // Blend: each dst = (red>>1 & m) + (dst>>1 & m)
    u16 dstB[3] = { blue, blue, blue };
    FillSpanTexturedBlend(rs, dstB, 0, 0, p);
    u16 expBlend = (u16)((u16)((0xF800u >> 1) & 0xF7DE) + (u16)((0x001Fu >> 1) & 0xF7DE));
    CHECK_EQ(dstB[0], expBlend);
    CHECK_EQ(dstB[2], expBlend);

    // Or: dst |= red
    u16 dstO[3] = { blue, blue, blue };
    FillSpanTexturedOr(rs, dstO, 0, 0, p);
    CHECK_EQ(dstO[0], (u16)(blue | 0xF800));   // 0xF81F

    // OrMasked with index 0 (transparent) leaves dst unchanged.
    u8 texZero[1] = { 0 };
    p.texBase = texZero;
    u16 dstM[3] = { blue, blue, blue };
    FillSpanTexturedOrMasked(rs, dstM, 0, 0, p);
    CHECK_EQ(dstM[0], blue);
    CHECK_EQ(dstM[1], blue);
}

// ---------------------------------------------------------------------------
// Mirror: reflect a point across the z=0 plane (normal +z, d=0) and project.
// ---------------------------------------------------------------------------
TEST(RenderPicture, MirrorReflectProject) {
    MirrorVertex v{};
    v.x = 1.0f; v.y = 2.0f; v.z = 5.0f;   // in front of a z=0 mirror
    MirrorPlane plane{0.0f, 0.0f, 1.0f, 0.0f};   // n=+z, d=0
    ProjectionParams proj{2.0f, 3.0f, 100.0f, 50.0f};

    ReflectAndProjectVertices(&v, 1, plane, proj);
    // t = -(P·n - d)*2 = -(5)*2 = -10 ; z' = z + t*nz = 5 - 10 = -5
    CHECK(v.t == -10.0f);
    CHECK(v.z == -5.0f);
    CHECK(v.x == 1.0f);    // nx=0 -> x unchanged
    CHECK(v.y == 2.0f);
    // screenX = (projX*x') * (1/z') + offX = (2*1)*(1/-5) + 100 = -0.4 + 100
    CHECK(v.screenX > 99.5f && v.screenX < 99.7f);
    // screenY = (1/z')*(projY*y') + offY = (1/-5)*(3*2) + 50 = -1.2 + 50
    CHECK(v.screenY > 48.7f && v.screenY < 48.9f);
}

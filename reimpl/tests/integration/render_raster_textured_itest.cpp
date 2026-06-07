// Cross-module integration: drive the RGBZ affine-textured rasterizer
// (render/raster_textured) across its REAL siblings — the BMP encoder
// (render/bmp BmpSaveIndexed), the by-name texture decode (render/texture_asset
// DecodeBmpIntoTexture, which uses the real BmpLoadBuffer + TextureSetSize), the
// colour packer (render/colorformat PackColor), and the software Surface
// (render/surface). This wires them exactly as the engine's textured-poly path
// does: decode a texture into a record, build its 16bpp palette LUT, then
// rasterize a triangle that samples it into a real surface.
#include "render/raster_textured.h"
#include "render/raster.h"
#include "render/surface.h"
#include "render/texture.h"
#include "render/texture_asset.h"
#include "render/bmp.h"
#include "render/colorformat.h"
#include "test.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

// texture_asset.cpp's (unused-here) LoadByName cache references VfsSlurp, owned
// by render/mesh_asset.cpp. This test exercises DecodeBmpIntoTexture on an
// in-memory BMP and never touches the VFS. In the full CMake build the real
// VfsSlurp (mesh_asset.cpp) links in; this WEAK fallback only satisfies an
// isolated link (the strong real definition overrides it, so no ODR clash).
namespace guild::render {
__attribute__((weak)) bool VfsSlurp(const char*, std::vector<u8>&) { return false; }
}

namespace {

// Build an 8x8 indexed BMP whose index == (y*8+x)%256 modulated so each texel is
// distinct, with a palette index k -> RGB(k,k,k) gradient, via the REAL encoder.
std::vector<u8> MakeIndexedBmp8x8() {
    u8 pixels[64];
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            pixels[y * 8 + x] = (u8)(y * 8 + x);     // 0..63
    // 256-entry palette as 256*3 RGB triples: index k -> (R=k, G=k/2, B=255-k).
    u8 palette[256 * 3];
    for (int k = 0; k < 256; ++k) {
        palette[k * 3 + 0] = (u8)k;                  // R
        palette[k * 3 + 1] = (u8)(k / 2);            // G
        palette[k * 3 + 2] = (u8)(255 - k);          // B
    }
    return BmpSaveIndexed(8, 8, pixels, palette);
}

} // namespace

// --- decode a real BMP into a Texture, then render it through the rasterizer --
TEST(RenderRasterTexIntegration, RealTextureDecodeAndRender) {
    std::vector<u8> bmp = MakeIndexedBmp8x8();

    // Decode through the REAL texture-asset path (BmpLoadBuffer + TextureSetSize).
    Texture rec;
    TextureDecode dec = DecodeBmpIntoTexture(bmp, rec);
    CHECK(dec.ok);
    CHECK_EQ(dec.width, 8);
    CHECK_EQ((int)rec.mipWidth, 8);
    CHECK_EQ((int)rec.widthShift, 3);                // log2(8)
    CHECK_EQ((int)rec.texelMask, (int)TexelMask(8)); // (8-1)|(64-1) = 63
    CHECK_EQ((int)rec.texels.size(), 64);

    // Build the 16bpp palette LUT (index -> packed RGB565) using the REAL packer,
    // from the source palette the decoder recovered.
    ColorFormat fmt = Format565();
    std::vector<u16> pal16(256, 0);
    CHECK(dec.palette.size() >= 64u * 3u);
    for (int k = 0; k < 256 && (size_t)(k * 3 + 2) < dec.palette.size(); ++k) {
        u8 r = dec.palette[k * 3 + 0];
        u8 g = dec.palette[k * 3 + 1];
        u8 b = dec.palette[k * 3 + 2];
        pal16[k] = (u16)PackColor(fmt, r, g, b);
    }

    // Render a triangle into a real 16bpp surface.
    Surface* s = SurfaceCreate(24, 24, 16);
    std::memset(s->pixels, 0, (size_t)s->pitch * s->height);
    RgbzVertex v[3] = {
        {3.0f, 3.0f, 0.0f, 0.0f},
        {18.0f, 6.0f, 1.0f, 0.0f},   // UVs normalised; texScale=mipWidth scales up
        {6.0f, 18.0f, 0.0f, 1.0f},
    };
    int drew = RasterizeTexturedTriangleRgbz(s, v, rec, pal16.data());
    CHECK(drew != 0);

    // The drawn pixels must all be palette entries that actually exist (i.e. each
    // written pixel is a real texel fetched from rec.texels through pal16). Build
    // the set of valid packed colours and assert every non-zero pixel is in it.
    std::vector<bool> valid(0x10000, false);
    valid[0] = true;                                  // background stays 0
    for (int i = 0; i < 64; ++i)
        valid[pal16[rec.texels[i]]] = true;
    const u16* px = (const u16*)s->pixels;
    int drawn = 0;
    for (int i = 0; i < s->height * s->widthPx; ++i) {
        CHECK(valid[px[i]]);
        if (px[i] != 0) ++drawn;
    }
    CHECK(drawn > 0);
    std::printf("[RasterTexIntegration] rendered %d textured pixels from a real "
                "8x8 BMP-decoded texture\n", drawn);
    SurfaceDestroy(s);
}

// --- the rasterizer's texel addressing matches TexelAt on the same record -----
TEST(RenderRasterTexIntegration, FillSpanLoopMatchesTexelAt) {
    std::vector<u8> bmp = MakeIndexedBmp8x8();
    Texture rec;
    TextureDecode dec = DecodeBmpIntoTexture(bmp, rec);
    CHECK(dec.ok);

    std::vector<u16> pal16(256, 0);
    for (int k = 0; k < 256; ++k) pal16[k] = (u16)(0x4000 + k);

    Surface* s = SurfaceCreate(16, 4, 16);
    std::memset(s->pixels, 0, (size_t)s->pitch * s->height);

    RgbzRasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    rs.fbPitchPx = s->widthPx;
    rs.fbRow0 = (u16*)s->pixels;                      // row 0
    rs.xLeft = 0; rs.xRight = 8 << 16;                // 8-pixel span
    rs.uLeft = 0; rs.vLeft = 3 << 16;                 // texture row 3
    rs.uGrad = 1 << 16; rs.vGrad = 0;                 // walk U 0..7

    SpanTexParams p = BuildSpanTexParams(rec, pal16.data(), rs.uGrad, rs.vGrad);
    FillSpanLoop(rs, 1, p);

    // Each emitted pixel must equal pal16[ TexelAt(rec, u, v=3) ] — the same
    // addressing the standalone texel fetch (texture.h TexelAt) uses.
    const u16* row0 = (const u16*)s->pixels;
    for (int u = 0; u < 8; ++u) {
        u8 idx = TexelAt(rec, u, 3);
        CHECK_EQ((int)row0[u], (int)pal16[idx]);
    }
    SurfaceDestroy(s);
}

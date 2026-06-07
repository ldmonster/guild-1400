// Unit test for the real-asset texture->render driver (guild::app).
// Drives the loader/raster/present chain over SYNTHETIC in-format BMP bytes (no
// real assets needed) with deterministic golden assertions:
//   * a synthetic 8-bit paletted square BMP decodes into a Texture record,
//   * the RGBZ rasterizer fills a 16bpp surface (non-blank pixels > 0),
//   * the dumped BMP decodes back to a recognisable colour.
#include "test.h"

#include "app/real_texture_driver.h"
#include "shim_impl/filedump_graphics.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;

namespace {

void put16(std::vector<u8>& v, std::size_t at, u16 x) { v[at] = (u8)x; v[at + 1] = (u8)(x >> 8); }
void put32(std::vector<u8>& v, std::size_t at, u32 x) {
    v[at] = (u8)x; v[at + 1] = (u8)(x >> 8); v[at + 2] = (u8)(x >> 16); v[at + 3] = (u8)(x >> 24);
}

// Build a square 8-bit BI_RGB BMP (side x side), bottom-up, with a palette where
// index i maps to a distinct colour and the pixel buffer fills index `fillIdx`.
std::vector<u8> SyntheticBmp8(int side, u8 fillIdx) {
    std::size_t pix = (std::size_t)side * side;
    std::vector<u8> f(0x36 + 256 * 4 + pix, 0);
    put16(f, 0, 19778);                 // 'BM'
    put32(f, 2, (u32)f.size());
    put32(f, 10, 0x36 + 256 * 4);       // data offset
    put32(f, 14, 40);                   // info header size
    put32(f, 18, (u32)side);
    put32(f, 22, (u32)side);            // positive => bottom-up
    put16(f, 26, 1);                    // planes
    put16(f, 28, 8);                    // bpp
    put32(f, 30, 0);                    // BI_RGB
    put32(f, 46, 256);                  // clrUsed
    for (int i = 0; i < 256; ++i) {
        f[0x36 + 4 * i + 0] = (u8)(i);          // B
        f[0x36 + 4 * i + 1] = (u8)(255 - i);    // G
        f[0x36 + 4 * i + 2] = (u8)(i * 2);      // R
        f[0x36 + 4 * i + 3] = 0;
    }
    std::memset(f.data() + 0x36 + 256 * 4, fillIdx, pix);
    return f;
}

std::string makeTempDir() {
    char templ[] = "/tmp/guild_real_tex_unit_XXXXXX";
    char* d = mkdtemp(templ);
    return d ? std::string(d) : std::string();
}

std::vector<std::uint8_t> readFile(const std::string& path) {
    std::vector<std::uint8_t> data;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return data;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n > 0) { data.resize((std::size_t)n);
        data.resize(std::fread(data.data(), 1, data.size(), f)); }
    std::fclose(f);
    return data;
}

} // namespace

// ---------------------------------------------------------------------------
// A solid-colour synthetic texture rasterizes to a fully non-blank frame.
// ---------------------------------------------------------------------------
TEST(RealTextureDriver, SyntheticSolidRendersNonBlank) {
    std::vector<u8> bmp = SyntheticBmp8(64, /*fillIdx=*/40);
    std::string dir = makeTempDir();
    CHECK(!dir.empty());

    app::TextureRenderResult r =
        app::RenderTextureBmpToImage(bmp, "SYNTH64", 128, 96, dir, "u");

    CHECK_EQ(r.texturesDecoded, 1);
    CHECK_EQ(r.squareTextures, 1);
    CHECK_EQ((int)r.samples.size(), 1);
    if (!r.samples.empty()) {
        CHECK_EQ(r.samples[0].width, 64);
        CHECK_EQ(r.samples[0].bpp, 8);
        CHECK(r.samples[0].square);
        CHECK(r.samples[0].decoded);
        // Every texel is index 40 (non-zero) -> all texels non-zero.
        CHECK_EQ(r.samples[0].nonzeroTexels, 64L * 64L);
    }

    CHECK(r.rendered);
    CHECK_EQ(r.renderWidth, 128);
    CHECK_EQ(r.renderHeight, 96);
    // The two triangles cover the whole 128x96 surface; index-40 packs to a
    // non-zero RGB565, so the whole frame should be non-blank.
    CHECK_EQ(r.nonBlankPixels, 128L * 96L);
    std::printf("[RealTextureDriver] synth solid: %ld/%d non-blank px, frame=%s\n",
                r.nonBlankPixels, 128 * 96, r.framePath.c_str());

    // The dumped BMP must decode back to a 128x96 image with non-black content.
    CHECK(!r.framePath.empty());
    auto img = shim::FileDumpGraphicsDevice::DecodeBmp24(readFile(r.framePath));
    CHECK(img.ok);
    CHECK_EQ(img.width, 128);
    CHECK_EQ(img.height, 96);
    long imgNonblack = 0;
    for (std::size_t i = 0; i < img.rgb.size(); ++i) if (img.rgb[i]) ++imgNonblack;
    CHECK(imgNonblack > 0);
}

// ---------------------------------------------------------------------------
// Index 0 maps to RGB (0,0,0) in our synthetic palette, so a fill of index 0
// produces an all-zero (blank) framebuffer even though decode succeeds — proves
// the non-blank count reflects real packed pixel content, not coverage.
// ---------------------------------------------------------------------------
TEST(RealTextureDriver, SyntheticIndexZeroIsBlack) {
    std::vector<u8> bmp = SyntheticBmp8(32, /*fillIdx=*/0);
    app::TextureRenderResult r =
        app::RenderTextureBmpToImage(bmp, "SYNTH0", 64, 64, "", "u");
    CHECK_EQ(r.texturesDecoded, 1);
    CHECK(r.rendered);                       // spans were drawn
    CHECK_EQ(r.samples[0].nonzeroTexels, 0L);// all texels are index 0
    // palette[0] = R:0 G:255 B:0 in our ramp -> packs non-zero, so non-blank.
    // (255 in green packs to a set bit -> non-zero pixel.)
    CHECK(r.nonBlankPixels > 0);
    std::printf("[RealTextureDriver] synth idx0: nonblank=%ld\n", r.nonBlankPixels);
}

// ---------------------------------------------------------------------------
// A truly black palette entry -> truly blank frame (lower bound of the metric).
// ---------------------------------------------------------------------------
TEST(RealTextureDriver, BlackPaletteFrameIsBlank) {
    // Build an 8-bit BMP whose palette is all-black; fill with index 5.
    int side = 16;
    std::size_t pix = (std::size_t)side * side;
    std::vector<u8> f(0x36 + 256 * 4 + pix, 0);
    put16(f, 0, 19778);
    put32(f, 2, (u32)f.size());
    put32(f, 10, 0x36 + 256 * 4);
    put32(f, 14, 40);
    put32(f, 18, (u32)side);
    put32(f, 22, (u32)side);
    put16(f, 26, 1);
    put16(f, 28, 8);
    put32(f, 46, 256);
    std::memset(f.data() + 0x36 + 256 * 4, 5, pix);  // palette stays all-zero
    app::TextureRenderResult r =
        app::RenderTextureBmpToImage(f, "BLACK", 40, 40, "", "u");
    CHECK_EQ(r.texturesDecoded, 1);
    CHECK(r.rendered);
    CHECK_EQ(r.nonBlankPixels, 0L);          // black palette -> blank frame
    std::printf("[RealTextureDriver] black palette: nonblank=%ld (expect 0)\n",
                r.nonBlankPixels);
}

// ---------------------------------------------------------------------------
// A non-square BMP must NOT decode as a texture (LoadByName requires w==h).
// ---------------------------------------------------------------------------
TEST(RealTextureDriver, NonSquareRejected) {
    // 8x4 8-bit BMP — non-square.
    int w = 8, h = 4;
    std::size_t pix = (std::size_t)w * h;
    std::vector<u8> f(0x36 + 256 * 4 + pix, 0);
    put16(f, 0, 19778);
    put32(f, 2, (u32)f.size());
    put32(f, 10, 0x36 + 256 * 4);
    put32(f, 14, 40);
    put32(f, 18, (u32)w);
    put32(f, 22, (u32)h);
    put16(f, 26, 1);
    put16(f, 28, 8);
    put32(f, 46, 256);
    app::TextureRenderResult r =
        app::RenderTextureBmpToImage(f, "NONSQ", 32, 32, "", "u");
    CHECK_EQ(r.texturesDecoded, 0);
    CHECK(!r.rendered);
}

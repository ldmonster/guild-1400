// tests/unit/texture_bin_test.cpp — texture_bin codec + name-resolution unit.
//
// Hand-builds tiny BMP buffers (via the reconstructed render::BmpSave* writers),
// decodes them through render::DecodeBmpBuffer / render::TextureBin::DecodeBuffer,
// and asserts dimensions + known pixel values. Also verifies the bare-name /
// extension-flexible / case-insensitive resolution (TextureNameStem + ResolveName
// + Decode) the material-name -> BMP lookup relies on.
#include "test.h"

#include "render/texture_bin.h"
#include "render/bmp.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace guild;

namespace {

// Build a 4x4 24-bit BMP with a distinct color per quadrant (top-down input).
std::vector<u8> Make24() {
    const int w = 4, h = 4;
    std::vector<u8> rgb((std::size_t)w * h * 3, 0);
    auto put = [&](int x, int y, u8 r, u8 g, u8 b) {
        std::size_t i = ((std::size_t)y * w + x) * 3;
        rgb[i] = r; rgb[i + 1] = g; rgb[i + 2] = b;
    };
    // top row (y=0) = red, row y=3 = blue; mark (1,2) green.
    for (int x = 0; x < w; ++x) put(x, 0, 200, 0, 0);
    for (int x = 0; x < w; ++x) put(x, 3, 0, 0, 200);
    put(1, 2, 0, 222, 0);
    return render::BmpSave24Bit(w, h, rgb.data());
}

inline void w16(std::vector<u8>& v, uint16_t x) { v.push_back((u8)x); v.push_back((u8)(x >> 8)); }
inline void w32(std::vector<u8>& v, uint32_t x) {
    v.push_back((u8)x); v.push_back((u8)(x >> 8)); v.push_back((u8)(x >> 16)); v.push_back((u8)(x >> 24));
}

// Build a STANDARD on-disk 2x2 8-bit indexed BMP (the exact format shipped in
// Textures.BIN: 14B file header + 40B info + 1024B BGRA palette @0x36 + bottom-up
// rows). pal[] receives the 256*3 RGB triples we wrote.
std::vector<u8> Make8(u8 pal[256 * 3]) {
    const int w = 2, h = 2;
    for (int i = 0; i < 256 * 3; ++i) pal[i] = 0;
    // index 0 = black, 1 = (10,20,30), 2 = (40,50,60), 3 = (255,255,255)
    pal[1 * 3 + 0] = 10; pal[1 * 3 + 1] = 20; pal[1 * 3 + 2] = 30;
    pal[2 * 3 + 0] = 40; pal[2 * 3 + 1] = 50; pal[2 * 3 + 2] = 60;
    pal[3 * 3 + 0] = 255; pal[3 * 3 + 1] = 255; pal[3 * 3 + 2] = 255;

    std::vector<u8> out;
    out.push_back('B'); out.push_back('M');
    uint32_t dataOff = 14 + 40 + 1024;
    w32(out, dataOff + (uint32_t)(w * h));  // file size
    w32(out, 0);                            // reserved
    w32(out, dataOff);                      // bfOffBits
    w32(out, 40);                           // biSize
    w32(out, (uint32_t)w);                  // width
    w32(out, (uint32_t)h);                  // height (>0 = bottom-up)
    w16(out, 1);                            // planes
    w16(out, 8);                            // bpp
    w32(out, 0);                            // BI_RGB
    w32(out, (uint32_t)(w * h));            // image size
    w32(out, 0); w32(out, 0);               // ppm x/y
    w32(out, 0); w32(out, 0);               // clrUsed / clrImportant
    // palette: BGRA (B,G,R,0) per the on-disk layout the loader reads.
    for (int i = 0; i < 256; ++i) {
        out.push_back(pal[i * 3 + 2]);      // B
        out.push_back(pal[i * 3 + 1]);      // G
        out.push_back(pal[i * 3 + 0]);      // R
        out.push_back(0);
    }
    // pixels: top-down logical [1 2 / 3 0]; emit bottom-up (row 1 first).
    u8 rows[2][2] = {{1, 2}, {3, 0}};       // [y][x], y=0 top
    for (int r = h - 1; r >= 0; --r)
        for (int x = 0; x < w; ++x) out.push_back(rows[r][x]);
    return out;
}

} // namespace

TEST(TextureBin, Decode24Bit) {
    std::vector<u8> bmp = Make24();
    render::DecodedBmp d = render::DecodeBmpBuffer(bmp);
    CHECK(d.ok);
    CHECK_EQ(d.width, 4);
    CHECK_EQ(d.height, 4);
    CHECK_EQ(d.bpp, 24);
    CHECK(d.square);
    CHECK_EQ((int)d.rgba.size(), 4 * 4 * 4);
    if (d.rgba.size() >= 16 * 4) {
        // top-left (0,0) red
        CHECK_EQ((int)d.rgba[0], 200);
        CHECK_EQ((int)d.rgba[1], 0);
        CHECK_EQ((int)d.rgba[2], 0);
        CHECK_EQ((int)d.rgba[3], 255);
        // (1,2) green : addr = (2*4+1)*4
        std::size_t a = (2 * 4 + 1) * 4;
        CHECK_EQ((int)d.rgba[a + 1], 222);
        // (0,3) blue
        std::size_t b = (3 * 4 + 0) * 4;
        CHECK_EQ((int)d.rgba[b + 2], 200);
    }
}

TEST(TextureBin, Decode8BitIndexed) {
    u8 pal[256 * 3];
    std::vector<u8> bmp = Make8(pal);
    render::DecodedBmp d = render::DecodeBmpBuffer(bmp);
    CHECK(d.ok);
    CHECK_EQ(d.width, 2);
    CHECK_EQ(d.height, 2);
    CHECK_EQ(d.bpp, 8);
    CHECK(d.square);
    CHECK_EQ((int)d.indices.size(), 4);
    CHECK_EQ((int)d.palette.size(), 256 * 3);
    if (d.indices.size() == 4) {
        CHECK_EQ((int)d.indices[0], 1);
        CHECK_EQ((int)d.indices[1], 2);
        CHECK_EQ((int)d.indices[2], 3);
        CHECK_EQ((int)d.indices[3], 0);
    }
    if (d.rgba.size() >= 16) {
        // index 1 -> (10,20,30)
        CHECK_EQ((int)d.rgba[0], 10);
        CHECK_EQ((int)d.rgba[1], 20);
        CHECK_EQ((int)d.rgba[2], 30);
        // index 3 -> white at pixel 2
        CHECK_EQ((int)d.rgba[2 * 4 + 0], 255);
    }
}

TEST(TextureBin, NameStemAndResolve) {
    CHECK(render::TextureNameStem("Stoffe/st_LEDE_04c.bmp") == "ST_LEDE_04C");
    CHECK(render::TextureNameStem("st_LEDE_04c") == "ST_LEDE_04C");
    CHECK(render::TextureNameStem("DIR\\sub\\Foo.BMP") == "FOO");

    // Seed a synthetic decoded texture; resolve it by bare/ext/case variants.
    render::TextureBin bin;
    std::vector<u8> bmp = Make24();
    const render::DecodedBmp* d = bin.DecodeBuffer("Group/MyTex.BMP", bmp);
    CHECK(d != nullptr);
    CHECK(bin.ResolveName("MyTex") == "Group/MyTex.BMP");        // bare stem
    CHECK(bin.ResolveName("mytex") == "Group/MyTex.BMP");        // case-insensitive
    CHECK(bin.ResolveName("MyTex.bmp") == "Group/MyTex.BMP");    // ext-flex
    CHECK(bin.ResolveName("Group/MyTex.BMP") == "Group/MyTex.BMP"); // full path
    CHECK(bin.ResolveName("Missing").empty());                  // absent -> ""

    const render::DecodedBmp* viaName = bin.Decode("mytex");     // resolve+decode
    CHECK(viaName != nullptr);
    if (viaName) CHECK_EQ(viaName->width, 4);
}

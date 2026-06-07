#include "tests/framework/test.h"
#include "render/picture_io.h"
#include "render/paintbox_shape.h"
#include "render/quant_octree.h"
#include "render/colorformat.h"
#include <vector>
#include <cstring>

using namespace guild;
using namespace guild::render;

namespace {
inline u16 pack555(u8 r, u8 g, u8 b) {
    return (u16)((((int)b >> 3) & 0x1F) | (32 * (((int)g >> 3) & 0x1F)) | ((((int)r >> 3) & 0x1F) << 10));
}
inline u16 pack565(u8 r, u8 g, u8 b) {
    return (u16)((((u16)g >> 2) << 5) | (((u16)r >> 3) << 11) | ((u16)b >> 3));
}
}

// =============================================================================
// E2E: full picture pipeline.
//   1. Build a synthetic 24-bit RGB image with a handful of distinct colours.
//   2. Quantize it to a <=256 palette + indexed image (octree quantizer).
//   3. Blit the indexed image (through its palette) into a 565 framebuffer.
//   4. Build a 24bpp picture file from a 555 framebuffer, save->reload it (TGA),
//      and verify the reloaded pixels match the reference framebuffer exactly.
// =============================================================================
TEST(RenderPictureE2E, QuantizeBlitSaveReload) {
    const int W = 8, H = 8;

    // --- 1. synthetic image: 4 colours in a tiled pattern -------------------
    struct C { u8 r, g, b; };
    C cols[4] = { {200, 8, 8}, {8, 200, 8}, {8, 8, 200}, {200, 200, 8} };
    std::vector<u8> img(W * H * 3);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const C& c = cols[((x / 2) + (y / 2)) % 4];
            int i = y * W + x;
            img[3 * i + 0] = c.r; img[3 * i + 1] = c.g; img[3 * i + 2] = c.b;
        }

    // --- 2. quantize --------------------------------------------------------
    std::vector<u8> idx(W * H);
    u8 palPlanar[768] = {0};
    CHECK(QuantBuildPalette(img.data(), W, H, 32, idx.data(), palPlanar));

    // every pixel maps back to its source colour to 5-bit accuracy.
    for (int i = 0; i < W * H; ++i) {
        u8 pi = idx[i];
        CHECK_EQ((int)(palPlanar[pi] & 0xF8), (int)(img[3 * i + 0] & 0xF8));
        CHECK_EQ((int)(palPlanar[pi + 256] & 0xF8), (int)(img[3 * i + 1] & 0xF8));
        CHECK_EQ((int)(palPlanar[pi + 512] & 0xF8), (int)(img[3 * i + 2] & 0xF8));
    }

    // --- 3. blit the indexed image into a 565 framebuffer -------------------
    // Build a 256x4 BGRX palette from the planar palette for the blitter.
    u8 palBgrx[256 * 4] = {0};
    for (int i = 0; i < 256; ++i) {
        palBgrx[4 * i + 0] = palPlanar[i];        // R
        palBgrx[4 * i + 1] = palPlanar[i + 256];  // G
        palBgrx[4 * i + 2] = palPlanar[i + 512];  // B
    }
    std::vector<u16> fb565(W * H, 0);
    Surface_BlitPaletteToPixels(fb565.data(), W, idx.data(), W, H, palBgrx, Format565());
    // spot-check: pixel 0's colour equals pack565 of its palette entry.
    {
        u8 pi = idx[0];
        CHECK_EQ(fb565[0], pack565(palPlanar[pi], palPlanar[pi + 256], palPlanar[pi + 512]));
    }

    // --- 4. build a 555 reference framebuffer, save TGA, reload, compare ----
    std::vector<u16> fbRef(W * H);
    for (int i = 0; i < W * H; ++i)
        fbRef[i] = pack555(img[3 * i + 0], img[3 * i + 1], img[3 * i + 2]);

    PictureFile tga = PictureSaveTga(fbRef.data(), W, W, H);
    std::vector<u16> fbReload(W * H, 0);
    int w, h;
    CHECK(PictureLoadTga(tga, fbReload.data(), W, w, h));
    CHECK_EQ(w, W); CHECK_EQ(h, H);
    // SaveTga byte-swaps on disk, LoadTga reads straight: faithful to gilde.exe,
    // so reloaded pixels equal the byte-swapped reference.
    for (int i = 0; i < W * H; ++i) {
        // final u16 of the written region is left unswapped (2*count-2 bound).
        u16 sw = (i == W * H - 1) ? fbRef[i] : (u16)((fbRef[i] >> 8) | (fbRef[i] << 8));
        CHECK_EQ(fbReload[i], sw);
    }

    // round-trip a 24bpp picture: pack the reference into a BGR file, load it,
    // and confirm it matches the 555 reference (load packs the same way).
    std::vector<u8> bgr(18, 0);
    bgr[12] = (u8)W; bgr[14] = (u8)H; bgr[16] = 24; bgr[17] = 32; // top-down
    for (int i = 0; i < W * H; ++i) {
        bgr.push_back(img[3 * i + 2]); // B
        bgr.push_back(img[3 * i + 1]); // G
        bgr.push_back(img[3 * i + 0]); // R
    }
    std::vector<u16> fb24(W * H, 0);
    CHECK(PictureLoadBmp24(bgr, fb24.data(), W, w, h));
    for (int i = 0; i < W * H; ++i)
        CHECK_EQ(fb24[i], fbRef[i]);
}

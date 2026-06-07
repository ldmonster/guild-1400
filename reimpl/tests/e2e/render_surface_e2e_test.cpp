#include "test.h"
#include "render/types.h"
#include "render/surface.h"
#include "render/bmp.h"
#include "render/hicoltab.h"
#include "render/quant.h"
#include "render/paintbox.h"
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

// Full flow: build a 24-bit surface, draw primitives + blit a sub-image, save to
// BMP bytes, reload, and verify the round-trip pixels match.
TEST(RenderE2E, DrawSaveReloadRoundtrip24) {
    const int W = 24, H = 16;
    Surface* s = SurfaceCreate(W, H, 24);
    CHECK(s != nullptr);

    // 1. fill background mid-gray, draw a red rect outline and a green diagonal,
    //    and a blue horizontal span.
    SurfaceColorFill(s, 40, 40, 40);
    // rect outline gated on the blue channel (a7 != 0) -> use a colour with b>0.
    SurfaceDrawRectOutline(s, 1, 1, 20, 12, 255, 0, 8);
    SurfaceDrawLine(s, 2, 2, 14, 14, 0, 255, 0);
    SurfaceDrawHLine(s, 3, 8, 10, 0, 0, 255);

    // 2. blit a 4x4 sub-image (a small checkerboard) into the surface at (16,2)
    //    by copying pixels through the surface API.
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            u8 v = ((x ^ y) & 1) ? 200 : 60;
            SurfaceSetPixelRgb(s, 16 + x, 2 + y, v, v, v);
        }

    // capture the surface as a flat top-down R,G,B buffer for save.
    std::vector<u8> rgb((size_t)W * H * 3);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            u8 px[3];
            SurfaceGetPixelRgb(s, x, y, px);
            rgb[(size_t)(y * W + x) * 3 + 0] = px[0];
            rgb[(size_t)(y * W + x) * 3 + 1] = px[1];
            rgb[(size_t)(y * W + x) * 3 + 2] = px[2];
        }

    // 3. save to BMP bytes, 4. reload.
    std::vector<u8> file = BmpSave24Bit(W, H, rgb.data());
    int w, h;
    std::vector<u8> back = BmpLoadBuffer(file, 24, w, h);
    CHECK_EQ(w, W);
    CHECK_EQ(h, H);
    CHECK_EQ((int)back.size(), W * H * 3);

    // 5. verify round-trip pixels match the surface exactly.
    bool allMatch = true;
    for (size_t i = 0; i < rgb.size(); ++i)
        if (back[i] != rgb[i]) { allMatch = false; break; }
    CHECK(allMatch);

    // spot checks of specific drawn features survive the round-trip.
    auto at = [&](int x, int y, int c) { return (int)back[(size_t)(y * W + x) * 3 + c]; };
    CHECK_EQ(at(1, 1, 0), 255);  // rect corner red
    CHECK_EQ(at(8, 8, 2), 255);  // hline blue
    CHECK_EQ(at(5, 5, 1), 255);  // diagonal green

    SurfaceDestroy(s);
}

// Flow: render an 8-bit indexed image, save, reload with palette, verify.
TEST(RenderE2E, Indexed8SaveReload) {
    const int W = 8, H = 6;
    std::vector<u8> idx((size_t)W * H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            idx[y * W + x] = (u8)((x * 31 + y * 17) & 0xFF);

    std::vector<u8> file = BmpSaveIndexed(W, H, idx.data(), nullptr);
    int w, h; u8 pal[768];
    std::vector<u8> back = BmpLoadBuffer(file, 8, w, h, pal);
    CHECK_EQ(w, W); CHECK_EQ(h, H);
    CHECK_EQ((int)back.size(), W * H);
    for (size_t i = 0; i < idx.size(); ++i)
        CHECK_EQ((int)back[i], (int)idx[i]);
    // Grayscale palette (faithful to the original's R/B cross-bug): loaded G is
    // exact, loaded B equals the saved R (= index).
    CHECK_EQ((int)pal[3 * 77 + 1], 77);
    CHECK_EQ((int)pal[3 * 77 + 2], 77);
}

// Flow: quantize a known 3-colour image to palette indices via the nearest-colour
// search using a fixed palette, mirroring the tail of VIBE_Quant_MapImageToPalette.
TEST(RenderE2E, QuantizeKnownImage) {
    QuantState q;
    QuantInitLookupTables(q);
    q.palCount = 3;
    u8 R[3] = {255, 0, 0};
    u8 G[3] = {0, 255, 0};
    u8 B[3] = {0, 0, 255};
    std::memcpy(q.palR, R, 3); std::memcpy(q.palG, G, 3); std::memcpy(q.palB, B, 3);

    // image of 6 pixels: red-ish, green-ish, blue-ish, red, green, blue
    u8 img[18] = {
        250, 10, 10,
        10, 250, 10,
        10, 10, 250,
        255, 0, 0,
        0, 255, 0,
        0, 0, 255,
    };
    u8 expected[6] = {0, 1, 2, 0, 1, 2};
    for (int i = 0; i < 6; ++i) {
        unsigned idx = QuantFindClosestColor(q, img[3*i], img[3*i+1], img[3*i+2]);
        CHECK_EQ(idx, (unsigned)expected[i]);
    }
}

// Flow: build a HiColTab from a small palette, then "blit" via the ramp table
// (simulate a light-table sprite blit by writing ramp values into a 16bpp surface).
TEST(RenderE2E, HiColTabLightRampBlit) {
    HiColTab tab;
    u8 idxRed   = HiColTabAddEntry(tab, 255, 0, 0);
    u8 idxWhite = HiColTabAddEntry(tab, 255, 255, 255);

    Surface* s = SurfaceCreate(8, 1, 16);
    // write a horizontal gradient of red at increasing light steps.
    for (int x = 0; x < 8; ++x) {
        int light = x * 8;            // 0,8,...,56
        if (light > 62) light = 62;
        ((u16*)s->pixels)[x] = HiColTabRamp(tab, idxRed, light);
    }
    // leftmost = black, rightmost approaches full red
    CHECK_EQ(((u16*)s->pixels)[0], (u16)0x0000);
    u8 r, g, b;
    UnpackColor(tab.fmt, ((u16*)s->pixels)[7], r, g, b);
    CHECK(r > 200);
    CHECK_EQ((int)g, 0);
    CHECK_EQ((int)b, 0);

    // direct color path
    CHECK_EQ(HiColTabDirect(tab, idxWhite), (u16)0xFFFF);
    SurfaceDestroy(s);
}

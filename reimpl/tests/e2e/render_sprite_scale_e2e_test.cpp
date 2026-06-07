#include "test.h"
#include "render/sprite_scale.h"
#include "render/shape_blit.h"
#include "render/animation_decode.h"
#include "render/colorformat.h"
#include "render/surface.h"
#include "render/types.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

// =============================================================================
// End-to-end sprite compositor flow, GUARDED on real assets.
//
// Without GUILD_SPRITE_E2E_BANK set, runs a SYNTHETIC full flow: build a shape
// bank in memory, allocate a real software Surface (surface.cpp), then drive the
// whole dispatch chain ShapeShowFromBankScaled -> ShapeBlitRleScaled compositing
// onto the live surface pixels — exercising every module boundary together.
//
// With GUILD_SPRITE_E2E_BANK=<path> set, the file is loaded as a raw shape-bank
// blob and shape index GUILD_SPRITE_E2E_INDEX (default 0) is composited; the test
// asserts only that the call is well-formed (no crash, in-bounds), since the exact
// golden pixels depend on the asset.
// =============================================================================

namespace {
void putU16(std::vector<u8>& b, u16 v) { b.push_back((u8)v); b.push_back((u8)(v >> 8)); }
void putU32(std::vector<u8>& b, u32 v) {
    for (int i = 0; i < 4; ++i) b.push_back((u8)(v >> (8 * i)));
}

// Build a 1-shape bank: count@+0x2A, offset table@+0x45, a 6x3 depth-1 RLE shape.
std::vector<u8> BuildBank() {
    const u16 R = 63488, G = 2016, B = 31, Y = 65504, W = 65535, M = 63519;
    // ---- the shape ----
    std::vector<u8> shape(0x32, 0);
    shape[0x06] = 6; shape[0x0A] = 3; shape[0x0C] = 1;   // width/height/depth
    auto run = [&](u32 skipPixels, std::vector<u16> px) {
        putU32(shape, (u32)(skipPixels << 1));
        putU32(shape, (u32)px.size());
        for (u16 p : px) putU16(shape, p);
    };
    putU32(shape, 1); run(0, {R, G});       // row 0: 1 run
    putU32(shape, 1); run(1, {B});          // row 1
    putU32(shape, 1); run(0, {Y, W, M});    // row 2
    for (int i = 0; i < 16; ++i) shape.push_back(0);  // bank padding

    // ---- the bank ----
    const u32 shapeOff = 0x45 + 4 * 2;       // offset table holds 2 entries
    std::vector<u8> bank(shapeOff, 0);
    bank[0x2A] = 5;                           // shapeCount
    std::memcpy(&bank[0x45 + 4], &shapeOff, 4);  // offset[1] -> shape
    bank.insert(bank.end(), shape.begin(), shape.end());
    return bank;
}
} // namespace

TEST(RenderSpriteScaleE2E, SyntheticFullCompositeFlow) {
    auto bank = BuildBank();

    // A real software surface (gfx.c VIBE_Surface_Create) as the composite target.
    Surface* surf = SurfaceCreate(8, 6, 16, Format565());
    CHECK(surf != nullptr);
    if (!surf) return;
    std::memset(surf->pixels, 0, (size_t)surf->pitch * surf->height);

    ColorBlitTarget16 dst{surf->widthPx, reinterpret_cast<u16*>(surf->pixels)};
    FrameBlitState st;
    st.clipX0 = 0; st.clipX1 = surf->widthPx; st.clipY0 = 0; st.clipY1 = surf->height;

    int rv = ShapeShowFromBankScaled(2, 1, bank.data(), 1, /*scale=*/1,
                                     /*doScaledBlit=*/true, /*lightTable=*/false, dst, st);
    CHECK_EQ(rv, 1);
    CHECK_EQ(st.destStridePx, surf->widthPx);   // stride installed onto blit state

    // Verify the composited image landed in the live surface pixels.
    const u16* px = reinterpret_cast<const u16*>(surf->pixels);
    static const u16 gold[48] = {
        0,0,0,0,0,0,0,0, 0,0,63488,2016,0,0,0,0, 0,0,0,31,0,0,0,0,
        0,0,65504,65535,63519,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0 };
    for (int i = 0; i < 48; ++i) CHECK_EQ((int)px[i], (int)gold[i]);

    // Same flow, scale=2: down-sampled image, still in-bounds on the real surface.
    std::memset(surf->pixels, 0, (size_t)surf->pitch * surf->height);
    int rv2 = ShapeShowFromBankScaled(0, 0, bank.data(), 1, /*scale=*/2,
                                      /*doScaledBlit=*/true, false, dst, st);
    CHECK_EQ(rv2, 1);

    SurfaceDestroy(surf);
}

// GUARDED: composite a real shape-bank asset if one is provided.
TEST(RenderSpriteScaleE2E, RealBankAssetGuarded) {
    const char* path = std::getenv("GUILD_SPRITE_E2E_BANK");
    if (!path) {
        std::printf("    [skip] set GUILD_SPRITE_E2E_BANK=<bank blob> to run\n");
        return;
    }
    std::FILE* f = std::fopen(path, "rb");
    CHECK(f != nullptr);
    if (!f) return;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<u8> bank(sz > 0 ? (size_t)sz : 0);
    if (sz > 0) { size_t got = std::fread(bank.data(), 1, (size_t)sz, f); (void)got; }
    std::fclose(f);
    CHECK(bank.size() > 0x4Du);
    if (bank.size() <= 0x4Du) return;

    int index = 0;
    if (const char* iv = std::getenv("GUILD_SPRITE_E2E_INDEX")) index = std::atoi(iv);

    Surface* surf = SurfaceCreate(256, 256, 16, Format565());
    CHECK(surf != nullptr);
    if (!surf) return;
    std::memset(surf->pixels, 0, (size_t)surf->pitch * surf->height);

    ColorBlitTarget16 dst{surf->widthPx, reinterpret_cast<u16*>(surf->pixels)};
    FrameBlitState st;
    st.clipX0 = 0; st.clipX1 = surf->widthPx; st.clipY0 = 0; st.clipY1 = surf->height;

    int rv = ShapeShowFromBankScaled(0, 0, bank.data(), index, 1, true, false, dst, st);
    std::printf("    real bank '%s' index %d -> %d\n", path, index, rv);
    CHECK(rv == 0 || rv == 1);   // well-formed result

    SurfaceDestroy(surf);
}

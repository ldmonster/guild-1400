#include "test.h"

#include "render/texture.h"
#include "render/texture_cache.h"
#include "render/light.h"
#include "render/shadow.h"
#include "compress/crc.h"

#include <vector>

using namespace guild;
using namespace guild::render;

// ===========================================================================
// Texture record + texel addressing (ties to raster.cpp's inner span loop).
// ===========================================================================
TEST(RenderTexture, MaskAndShift) {
    // (w-1)|(w*w-1) for power-of-two width == w*w-1; shift == log2(w).
    CHECK_EQ(TexelMask(8), 63u);
    CHECK_EQ((int)WidthShift(8), 3);
    CHECK_EQ(TexelMask(16), 255u);
    CHECK_EQ((int)WidthShift(16), 4);
    CHECK_EQ(TexelMask(1), 0u);
    CHECK_EQ((int)WidthShift(1), 0);
}

TEST(RenderTexture, TexelFetchMatchesRaster) {
    Texture t;
    TextureSetSize(t, 8);                 // 8x8, mask 63, shift 3
    CHECK_EQ(t.texelMask, 63u);
    CHECK_EQ((int)t.widthShift, 3);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            t.texels[(size_t)(y * 8 + x)] = (u8)(y * 8 + x);
    // Golden vectors (python: addr = ((v<<shift)+u)&mask).
    CHECK_EQ((int)TexelAt(t, 0, 0), 0);
    CHECK_EQ((int)TexelAt(t, 3, 5), 43);
    CHECK_EQ((int)TexelAt(t, 7, 7), 63);
    CHECK_EQ((int)TexelAt(t, 8, 0), 8);    // U wraps within the row stride
    CHECK_EQ((int)TexelAt(t, 0, 8), 0);    // V wraps to row 0
    CHECK_EQ((int)TexelAt(t, 9, 9), 17);
}

// ===========================================================================
// Colour-key flag — record +104 bit 2 (gilde.exe 0x5da714 writer @0x5dad52 /
// 0x5db234 consumer @0x5db319). A >8bpp/24-bit source is colour-keyed; an
// 8-bit indexed source and a name-flag ("_NM", bit 3) source are NOT. Golden
// bit-by-bit predicate.
// ===========================================================================
TEST(RenderTexture, ColourKeyFlagBit2) {
    Texture t;
    TextureSetSize(t, 8);

    // Fresh record: no flags -> not keyed (a decoded 8-bit source's default).
    t.flags = 0;
    CHECK(!TextureIsColourKeyed(t));

    // The 24-bit / transparency load (0x5dad52: *v59 = (4*1)|... sets bit 2).
    t.flags = kTexFlagColourKey;             // 0x04
    CHECK(TextureIsColourKeyed(t));

    // An 8-bit indexed source carries bit 5 (0x20) but NOT bit 2 -> not keyed.
    t.flags = kTexFlagIndexed8;              // 0x20
    CHECK(!TextureIsColourKeyed(t));

    // A "_NM" source carries bit 3 (0x08, the mip-bias select) but NOT bit 2.
    // This is the bit the wave-3/4 code wrongly treated as the colour key.
    t.flags = kTexFlagNameNM;                // 0x08
    CHECK(!TextureIsColourKeyed(t));

    // Bit 3 alone never implies the colour key; both bits set is keyed by bit 2.
    t.flags = (u8)(kTexFlagNameNM | kTexFlagColourKey);
    CHECK(TextureIsColourKeyed(t));

    // The fresh-load companion bits (alias bit1, no-downscale bit6) don't key.
    t.flags = (u8)(kTexFlagAlias | kTexFlagNoDownscale | kTexFlagArg0Low);
    CHECK(!TextureIsColourKeyed(t));

    // A real 24-bit load record: alias(2)|colourkey(4)|indexed cleared. Keyed.
    t.flags = (u8)(kTexFlagAlias | kTexFlagColourKey);   // 0x06
    CHECK(TextureIsColourKeyed(t));

    // Constant values are the exact +104 bits.
    CHECK_EQ((int)kTexFlagArg0Low, 0x01);
    CHECK_EQ((int)kTexFlagAlias, 0x02);
    CHECK_EQ((int)kTexFlagColourKey, 0x04);
    CHECK_EQ((int)kTexFlagNameNM, 0x08);
    CHECK_EQ((int)kTexFlagIndexed8, 0x20);
    CHECK_EQ((int)kTexFlagNoDownscale, 0x40);
}

// ===========================================================================
// Texture slot manager — load into a slot, lookup, ref-count, recycle.
// ===========================================================================
TEST(RenderTexture, SlotLoadLookupRecycle) {
    TextureSet set(4);
    int a = set.CreateRecord("brick", 16);
    CHECK(a >= 0);
    CHECK_EQ(set.records[(size_t)a].refCount, 1);
    CHECK_EQ((int)set.records[(size_t)a].mipWidth, 16);
    CHECK_EQ(set.records[(size_t)a].texels.size(), (size_t)256);

    // Lookup by name returns the same slot.
    CHECK_EQ(set.FindActive("brick"), a);
    CHECK_EQ(set.FindActive("stone"), -1);

    // Ref-count bump and release recycle.
    set.IncrementRef(a);
    CHECK_EQ(set.records[(size_t)a].refCount, 2);
    set.ReleaseEntry(a);
    CHECK_EQ(set.records[(size_t)a].refCount, 1);
    set.ReleaseEntry(a);                  // -> 0 frees the slot
    CHECK_EQ(set.records[(size_t)a].refCount, 0);
    CHECK_EQ(set.records[(size_t)a].texels.size(), (size_t)0);

    // Freed slot is reused by the next create.
    int b = set.CreateRecord("stone", 8);
    CHECK_EQ(b, a);
    CHECK_EQ(set.FindActive("brick"), -1);
}

TEST(RenderTexture, SlotFull) {
    TextureSet set(2);
    CHECK(set.CreateRecord("a", 4) >= 0);
    CHECK(set.CreateRecord("b", 4) >= 0);
    CHECK_EQ(set.CreateRecord("c", 4), -1);  // full
}

// ===========================================================================
// LRU tile cache — signature, load into a slot, hit, recycle.
// ===========================================================================
TEST(RenderTexture, SignatureCrc) {
    const int W = 8;
    std::vector<u8> src((size_t)(W * W));
    for (int y = 0; y < W; ++y)
        for (int x = 0; x < W; ++x)
            src[(size_t)(y * W + x)] = (u8)(y * W + x);
    u8 sig[25];
    int n = BuildSignature(sig, src.data(), W, 0, 0, 5);
    CHECK_EQ(n, 25);
    // python golden first 5 bytes 0,1,2,3,4 then next row 8,9,10,11,12 ...
    CHECK_EQ((int)sig[0], 0);
    CHECK_EQ((int)sig[5], 8);
    CHECK_EQ((int)sig[24], 36);
    u32 crc = compress::CrcCompute(0, sig, 25);
    CHECK_EQ(crc, 1273329001u);            // python zlib.crc32
}

TEST(RenderTexture, TileCacheLookupBuildRecycle) {
    const int W = 8;
    std::vector<u8> src((size_t)(W * W));
    for (int i = 0; i < W * W; ++i) src[(size_t)i] = (u8)i;

    TileCache cache(2);
    cache.salt = 0x1234;
    cache.frame = 1;

    // Miss -> build into slot 0.
    int s0 = cache.GetOrBuildTile(src.data(), W, 0, 0, 5, /*size*/16, /*id*/100);
    CHECK(s0 >= 0);
    CHECK(cache.slots[(size_t)s0].data != 0);
    u32 key0 = cache.slots[(size_t)s0].key;
    CHECK_EQ(key0, (1273329001u + 0x1234u));

    // Same region -> hit, same slot, no new allocation.
    cache.NextFrame();
    u32 outKey = 0;
    int hit = cache.LookupTile(src.data(), W, 0, 0, 5, 16, &outKey);
    CHECK_EQ(hit, s0);
    CHECK_EQ(outKey, key0);
    CHECK_EQ(cache.slots[(size_t)hit].stamp, cache.frame);  // LRU refreshed

    // Different region -> miss -> second free slot.
    int s1 = cache.GetOrBuildTile(src.data(), W, 2, 2, 5, 16, 200);
    CHECK(s1 >= 0 && s1 != s0);

    // Cache full, both occupied; a third distinct tile evicts the LRU slot.
    cache.NextFrame();
    // Touch s1 so s0 is the LRU.
    cache.LookupTile(src.data(), W, 2, 2, 5, 16, &outKey);
    cache.NextFrame();
    int s2 = cache.GetOrBuildTile(src.data(), W, 4, 4, 5, 16, 300);
    CHECK_EQ(s2, s0);                       // evicted the least-recently-used
    CHECK_EQ(cache.slots[(size_t)s2].data, 300u);
}

// ===========================================================================
// Lighting — gray shade index (golden), colour-mode normalise, 768 ramp.
// ===========================================================================
TEST(RenderLight, GrayShadeGolden) {
    auto gray = [](float r, float g, float b) {
        LightVertex lv{r, g, b, 0, 0, 0};
        return (int)ComputeGrayShade(lv);
    };
    CHECK_EQ(gray(100, 150, 50), 123);
    CHECK_EQ(gray(255, 255, 255), 255);
    CHECK_EQ(gray(0, 0, 0), 0);
    CHECK_EQ(gray(200, 200, 200), 200);
    CHECK_EQ(gray(255, 0, 0), 76);
    CHECK_EQ(gray(0, 255, 0), 150);
    CHECK_EQ(gray(0, 0, 255), 28);
    CHECK_EQ(gray(300, 300, 300), 255);     // saturates
}

TEST(RenderLight, ColorShadeNormalise) {
    // Below threshold: pass-through (truncated).
    LightVertex lo{10.f, 20.f, 30.f, 0, 0, 0};
    ComputeColorShade(lo);
    CHECK_EQ((int)lo.outR, 10);
    CHECK_EQ((int)lo.outG, 20);
    CHECK_EQ((int)lo.outB, 30);
    // Above threshold: scaled by 255/max so the max channel becomes 255.
    LightVertex hi{510.f, 255.f, 127.5f, 0, 0, 0};
    ComputeColorShade(hi);
    CHECK_EQ((int)hi.outR, 255);            // 510 * 255/510
    CHECK_EQ((int)hi.outG, 127);            // 255 * 0.5
    CHECK_EQ((int)hi.outB, 63);             // 127.5 * 0.5 -> 63 (trunc)
}

TEST(RenderLight, BroadcastGray) {
    CHECK_EQ(BroadcastGrayDword(0x00), 0x00000000u);
    CHECK_EQ(BroadcastGrayDword(0xFF), 0xFFFFFFFFu);
    CHECK_EQ(BroadcastGrayDword(0x7F), 0x7F7F7F7Fu);
}

TEST(RenderLight, ShadeRamp768) {
    CHECK_EQ(ShadeRampOffset(0), 0u);
    CHECK_EQ(ShadeRampOffset(1), 768u);
    CHECK_EQ(ShadeRampOffset(5), 3840u);    // matches mesh sort-key 768*idx

    std::vector<u8> pal((size_t)(256 * 3));
    for (int P = 0; P < 256; ++P) {
        pal[(size_t)(3 * P + 0)] = (u8)(P);
        pal[(size_t)(3 * P + 1)] = (u8)(2 * P);
        pal[(size_t)(3 * P + 2)] = (u8)(3 * P);
    }
    std::vector<u8> ramp((size_t)(256 * 768));
    BuildShadeRamp(ramp.data(), pal.data());
    // python goldens.
    auto at = [&](int L, int P, int c) {
        return (int)ramp[(size_t)(ShadeRampOffset(L) + 3 * P + c)];
    };
    CHECK_EQ(at(0, 10, 0), 0);
    CHECK_EQ(at(255, 10, 0), 10);
    CHECK_EQ(at(255, 10, 1), 20);
    CHECK_EQ(at(255, 10, 2), 30);
    CHECK_EQ(at(128, 200, 0), 100);
    CHECK_EQ(at(128, 200, 1), 72);
    CHECK_EQ(at(128, 200, 2), 44);
    CHECK_EQ(at(64, 5, 2), 3);
}

TEST(RenderLight, RayFalloffSmoothstep) {
    CHECK(ComputeRayFalloff(0, 10, 0) == 0.0f);
    CHECK(ComputeRayFalloff(0, 10, 10) == 1.0f);
    CHECK(ComputeRayFalloff(0, 10, 5) == 0.5f);
    CHECK(ComputeRayFalloff(0, 10, 2.5f) == 0.15625f);
    CHECK(ComputeRayFalloff(0, 10, -3) == 0.0f);
}

// ===========================================================================
// Shadow caster bookkeeping.
// ===========================================================================
TEST(RenderShadow, CasterClearAndRemove) {
    ShadowCaster tbl[kCasterSlots];
    for (int i = 0; i < kCasterSlots; ++i) {
        tbl[i].entry = (u32)(i + 1);
        tbl[i].lightId = (u32)(10 + i);
        tbl[i].stateA = 99;
    }
    // Remove only the one cast by light 12 (slot 2).
    int r = RemoveCasterByLight(tbl, true, 12);
    CHECK_EQ(r, 1);
    CHECK_EQ(tbl[2].entry, 0u);
    CHECK_EQ(tbl[2].stateA, 0u);
    CHECK(tbl[0].entry != 0u);
    CHECK(tbl[1].entry != 0u);
    CHECK(tbl[3].entry != 0u);

    // Disabled gate: no-op.
    RemoveCasterByLight(tbl, false, 10);
    CHECK(tbl[0].entry != 0u);

    // Clear all.
    ClearAllCasters(tbl, true);
    for (int i = 0; i < kCasterSlots; ++i) {
        CHECK_EQ(tbl[i].entry, 0u);
        CHECK_EQ(tbl[i].stateA, 0u);
    }
}

TEST(RenderShadow, SurfaceCacheAcquire) {
    ShadowSurfaceCache c(3);
    c.minSpread = 1;
    // All free: first acquire takes the first free slot (phase-1 break on id==0).
    int s0 = c.AcquireCacheSlot(/*id*/5, /*type*/1, /*need*/64, /*handle*/0xAB);
    CHECK_EQ(s0, 0);
    CHECK_EQ(c.slots[0].id, 5u);
    CHECK_EQ(c.slots[0].size, 64u);
    CHECK_EQ(c.slots[0].bound, 0xABu);

    int s1 = c.AcquireCacheSlot(7, 1, 128, 0xCD);
    CHECK_EQ(s1, 1);
    int s2 = c.AcquireCacheSlot(9, 2, 32, 0xEF);
    CHECK_EQ(s2, 2);

    // Full now. A new request (id 11,type 1,need 200) finds no free slot; phase-2
    // evicts the smaller-than-need matching-type slot (slot 0 size 64 over slot 1
    // size 128 since 64<128, v8 tracks the smallest). need-64=136 > 1 -> evict 0.
    int s3 = c.AcquireCacheSlot(11, 1, 200, 0x11);
    CHECK_EQ(s3, 0);
    CHECK_EQ(c.slots[0].id, 11u);
    CHECK_EQ(c.slots[0].size, 200u);
}

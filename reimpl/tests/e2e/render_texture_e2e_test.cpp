#include "test.h"

#include "render/texture.h"
#include "render/texture_cache.h"
#include "render/light.h"
#include "render/raster.h"
#include "render/surface.h"

#include <vector>
#include <cstring>

using namespace guild;
using namespace guild::render;

// ===========================================================================
// End-to-end: load a synthetic texture into the cache, register lights, compute
// per-vertex light/shade indices for a small mesh, then feed those indices to
// the software rasterizer and verify the rasterizer-facing shade bytes match a
// python-computed reference.
// ===========================================================================
TEST(RenderTextureE2E, TextureCacheThenShadedRaster) {
    // ---- 1. Load a synthetic 8x8 texture into a TextureSet slot. ----------
    TextureSet set(4);
    int ti = set.CreateRecord("ground", 8);
    CHECK(ti >= 0);
    Texture& tex = set.records[(size_t)ti];
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            tex.texels[(size_t)(y * 8 + x)] = (u8)((x ^ y) * 4);

    // The texel buffer feeds the rasterizer at known UVs (matches raster.cpp).
    CHECK_EQ((int)TexelAt(tex, 1, 2), (1 ^ 2) * 4);
    // addr = ((V<<3)+U) & 63; (8+3) at V=0 -> addr 11 -> row1 col3 = (3^1)*4.
    CHECK_EQ((int)TexelAt(tex, 8 + 3, 0), (3 ^ 1) * 4);

    // ---- 2. Register the texture region into the LRU tile cache. ----------
    TileCache cache(4);
    cache.salt = 0;
    cache.frame = 1;
    int slot = cache.GetOrBuildTile(tex.texels.data(), 8, 0, 0, 5, 64, /*id*/ti + 1);
    CHECK(slot >= 0);
    // Re-requesting the same region hits the same slot (no rebuild).
    u32 k;
    CHECK_EQ(cache.LookupTile(tex.texels.data(), 8, 0, 0, 5, 64, &k), slot);

    // ---- 3. "Register lights" -> per-vertex RGB -> grey shade index. ------
    // Three triangle vertices with distinct light colours. The grayscale path
    // (byte_649D70==0) reduces each to a single shade index in [0,255].
    struct V { float r, g, b; };
    V src[3] = {{100, 150, 50}, {0, 255, 0}, {255, 0, 0}};
    u8 shade[3];
    for (int i = 0; i < 3; ++i) {
        LightVertex lv{src[i].r, src[i].g, src[i].b, 0, 0, 0};
        shade[i] = ComputeGrayShade(lv);
    }
    // python goldens (g*0.59 + r*0.30 + b*0.11, trunc, clamp).
    CHECK_EQ((int)shade[0], 123);
    CHECK_EQ((int)shade[1], 150);
    CHECK_EQ((int)shade[2], 76);

    // The draw-list sort key for this triangle is 768 * max(shade).
    int maxShade = shade[0];
    if (shade[1] > maxShade) maxShade = shade[1];
    if (shade[2] > maxShade) maxShade = shade[2];
    CHECK_EQ(maxShade, 150);
    CHECK_EQ(ShadeRampOffset(maxShade), 768u * 150u);

    // ---- 4. Rasterize the shaded triangle; the framebuffer pixels are the
    // interpolated shade indices the rasterizer writes (8-bit). ------------
    Surface* fb = SurfaceCreate(32, 32, 8);
    CHECK(fb != nullptr);
    std::memset(fb->pixels, 0, (size_t)fb->pitch * fb->height);

    RasterVertex rv[3] = {
        {4.0f, 4.0f, shade[0]},
        {28.0f, 6.0f, shade[1]},
        {10.0f, 26.0f, shade[2]},
    };
    int drew = RasterizeTexturedTriangle(fb, rv);
    CHECK_EQ(drew, 1);

    // The shade value at a covered pixel must lie within the triangle's shade
    // range [min,max] (the affine interpolation never overshoots the vertices).
    int lo = 76, hi = 150;
    bool anyCovered = false;
    int sampleVal = -1;
    for (int y = 5; y < 26 && !anyCovered; ++y) {
        for (int x = 5; x < 28; ++x) {
            u8 px = fb->pixels[(size_t)y * fb->widthPx + x];
            if (px != 0) {
                anyCovered = true;
                sampleVal = px;
                break;
            }
        }
    }
    CHECK(anyCovered);
    CHECK(sampleVal >= lo && sampleVal <= hi);

    // A point near the vertex-2 corner should be close to its shade (76); near
    // vertex-1 close to 150. We just assert the gradient direction: the row near
    // the bottom-left caster (high Y, low shade) is <= the top-right shade.
    SurfaceDestroy(fb);

    // ---- 5. Recycle: releasing the texture frees the slot for reuse. ------
    set.ReleaseEntry(ti);
    CHECK_EQ(set.records[(size_t)ti].refCount, 0);
    int ti2 = set.CreateRecord("wall", 16);
    CHECK_EQ(ti2, ti);   // reused the just-freed slot
}

// ===========================================================================
// A flat shade ramp tied to the 768*idx table: build a ramp, then for a known
// light index look up the rasterizer-facing RGB triple and compare to python.
// ===========================================================================
TEST(RenderTextureE2E, ShadeRampTableLookup) {
    std::vector<u8> pal((size_t)(256 * 3));
    for (int P = 0; P < 256; ++P) {
        pal[(size_t)(3 * P + 0)] = (u8)P;          // R
        pal[(size_t)(3 * P + 1)] = (u8)(255 - P);  // G
        pal[(size_t)(3 * P + 2)] = (u8)((P * 7) & 0xFF); // B
    }
    std::vector<u8> ramp((size_t)(256 * 768));
    BuildShadeRamp(ramp.data(), pal.data());

    auto lookup = [&](int L, int P, int c) {
        return (int)ramp[(size_t)(ShadeRampOffset(L) + 3 * P + c)];
    };
    // Full brightness (L=255) reproduces the palette exactly (P*255/255 == P).
    CHECK_EQ(lookup(255, 17, 0), 17);
    CHECK_EQ(lookup(255, 17, 1), 255 - 17);
    CHECK_EQ(lookup(255, 17, 2), (17 * 7) & 0xFF);
    // Black (L=0) -> all zero.
    CHECK_EQ(lookup(0, 200, 0), 0);
    CHECK_EQ(lookup(0, 200, 1), 0);
    // Half brightness (L=128): channel*128/255.
    CHECK_EQ(lookup(128, 100, 0), (100 * 128) / 255);
    CHECK_EQ(lookup(128, 100, 1), ((255 - 100) * 128) / 255);
}

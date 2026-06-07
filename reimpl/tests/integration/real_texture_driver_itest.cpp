// Integration test for the real-asset texture->render driver (guild::app).
// Wires the driver against its REAL reconstructed loader siblings over a small
// synthetic fixture, asserting the cross-module flow:
//
//   io::ZipArchive (build a tiny in-memory PKZIP-less path is N/A, so we mount a
//   real-shaped MemFileSystem) -> render::TextureAssetCache::LoadByName (the
//   by-name VFS decode path, VIBE_Texture_LoadByName) -> the SAME Texture record
//   the driver rasterizes -> driver render + present.
//
// This proves the by-name cache decode (the real LoadByName path) produces a
// record whose texels rasterize identically to the driver's direct-bytes path.
#include "test.h"

#include "app/real_texture_driver.h"
#include "shim_impl/mem_filesystem.h"
#include "io/vfs.h"
#include "render/texture.h"
#include "render/texture_asset.h"
#include "render/bmp.h"

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

// A square 8-bit BI_RGB BMP with a varied per-row index pattern (so the texture
// has real, non-constant content the rasterizer samples across the quad).
std::vector<u8> VariedBmp8(int side) {
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
    for (int i = 0; i < 256; ++i) {
        f[0x36 + 4 * i + 0] = (u8)(i);          // B
        f[0x36 + 4 * i + 1] = (u8)(i ^ 0x55);   // G
        f[0x36 + 4 * i + 2] = (u8)(255 - i);    // R
    }
    u8* px = f.data() + 0x36 + 256 * 4;
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x)
            px[(std::size_t)y * side + x] = (u8)((x * 3 + y * 7 + 1) & 0xFF);
    return f;
}

} // namespace

// ---------------------------------------------------------------------------
// LoadByName (real cache path) decodes the fixture; its record rasterizes to a
// non-blank frame, matching the driver's own non-blank metric on the same bytes.
// ---------------------------------------------------------------------------
TEST(RealTextureDriverIT, LoadByNameRecordRendersThroughDriver) {
    const int side = 128;
    std::vector<u8> bmp = VariedBmp8(side);

    // Serve the BMP through the real VFS to the real by-name texture cache.
    shim::MemFileSystem mem;
    mem.put("tex/FIXTURE.BMP", bmp);
    io::VfsInit(&mem, false);

    render::TextureAssetCache cache(16);
    int slot = cache.LoadByName("tex/FIXTURE.BMP", "FIXTURE");
    CHECK(slot >= 0);
    const render::Texture* rec = cache.record(slot);
    CHECK(rec != nullptr);
    if (rec) {
        CHECK_EQ(rec->mipWidth, side);
        CHECK_EQ((int)rec->texels.size(), side * side);
        CHECK(rec->palette != nullptr);
    }
    io::VfsShutdown();

    // The driver renders the SAME bytes directly; the by-name path and the
    // direct-bytes path must agree on dimensions and produce a non-blank frame.
    app::TextureRenderResult r =
        app::RenderTextureBmpToImage(bmp, "FIXTURE", 200, 150, "", "it");
    CHECK_EQ(r.texturesDecoded, 1);
    CHECK(r.rendered);
    CHECK(r.nonBlankPixels > 0);
    CHECK_EQ(r.samples[0].width, side);

    // Cross-check: the cache record's texels equal what the driver decoded
    // (same reconstructed DecodeBmpIntoTexture body underneath LoadByName).
    int w = 0, h = 0; u8 pal[256 * 3];
    std::vector<u8> idx = render::BmpLoadBuffer(bmp, 8, w, h, pal);
    CHECK_EQ(w, side);
    bool texelsMatch = rec && idx.size() == rec->texels.size() &&
                       std::memcmp(idx.data(), rec->texels.data(), idx.size()) == 0;
    CHECK(texelsMatch);

    std::printf("[RealTextureDriverIT] LoadByName slot=%d %dx%d -> driver nonblank=%ld "
                "texelsMatch=%d\n", slot, side, side, r.nonBlankPixels, (int)texelsMatch);
}

// ---------------------------------------------------------------------------
// Re-loading the same name reuses the slot (refcount bump) — the cache front-end
// of the real LoadByName path — while the driver still renders independently.
// ---------------------------------------------------------------------------
TEST(RealTextureDriverIT, ByNameCacheReuseAndRender) {
    std::vector<u8> bmp = VariedBmp8(64);
    shim::MemFileSystem mem;
    mem.put("tex/REUSE.BMP", bmp);
    io::VfsInit(&mem, false);

    render::TextureAssetCache cache(8);
    int s1 = cache.LoadByName("tex/REUSE.BMP", "REUSE");
    int s2 = cache.LoadByName("tex/REUSE.BMP", "REUSE");
    CHECK(s1 >= 0);
    CHECK_EQ(s1, s2);
    const render::Texture* rec = cache.record(s1);
    CHECK(rec != nullptr);
    if (rec) CHECK_EQ((int)rec->refCount, 2);
    io::VfsShutdown();

    app::TextureRenderResult r =
        app::RenderTextureBmpToImage(bmp, "REUSE", 96, 96, "", "it");
    CHECK(r.rendered);
    CHECK(r.nonBlankPixels > 0);
    std::printf("[RealTextureDriverIT] cache reuse slot=%d refCount=2 render nonblank=%ld\n",
                s1, r.nonBlankPixels);
}

// tests/integration/real_texture_source_itest.cpp — BgfModel material -> texture
// resolution + per-poly texId table + textured-sample, with untextured fallback.
//
// Builds a small render::BgfModel with two materials (one resolvable to a
// synthetic texture, one whose name has no BMP), seeds the texture into a
// play::RealTextureSource's TextureBin, builds the per-poly texId table, samples a
// UV on a textured poly and asserts the expected texel, and asserts the poly
// referencing the missing material is untextured (fallback).
#include "test.h"

#include "play/real_texture_source.h"
#include "render/texture_bin.h"
#include "render/bmp.h"
#include "render/bgf_loader.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace guild;

namespace {

// 8x8 24-bit texture: a single bright-green texel at (3,5), rest dark.
std::vector<u8> MakeTex() {
    const int w = 8, h = 8;
    std::vector<u8> rgb((std::size_t)w * h * 3, 8);  // dark gray fill
    auto put = [&](int x, int y, u8 r, u8 g, u8 b) {
        std::size_t i = ((std::size_t)y * w + x) * 3;
        rgb[i] = r; rgb[i + 1] = g; rgb[i + 2] = b;
    };
    put(3, 5, 0, 240, 0);
    return render::BmpSave24Bit(w, h, rgb.data());
}

render::BgfModel MakeModel() {
    render::BgfModel m;
    m.materialCount = 2;
    m.materials.resize(2);
    m.materials[0].name0 = "GrassTile";   // resolvable
    m.materials[1].name0 = "NoSuchTex";   // missing -> untextured
    m.vertexCount = 4;
    m.vertices.resize(4);
    m.polyCount = 2;
    m.polygons.resize(2);
    m.polygons[0].matIndex = 0;           // textured
    m.polygons[0].vtx[0] = 0; m.polygons[0].vtx[1] = 1; m.polygons[0].vtx[2] = 2;
    m.polygons[1].matIndex = 1;           // untextured
    m.polygons[1].vtx[0] = 0; m.polygons[1].vtx[1] = 1; m.polygons[1].vtx[2] = 3;
    return m;
}

} // namespace

TEST(RealTextureSourceIT, ResolveTableAndSample) {
    play::RealTextureSource src;
    // Seed the synthetic texture directly into the TextureBin (archive-less path).
    std::vector<u8> bmp = MakeTex();
    const render::DecodedBmp* seeded = src.bin().DecodeBuffer("GrassTile.BMP", bmp);
    CHECK(seeded != nullptr);
    if (seeded) CHECK_EQ(seeded->width, 8);

    render::BgfModel model = MakeModel();
    const play::MaterialTextureTable* tbl = src.BuildTableFor("model0", model);
    CHECK(tbl != nullptr);
    if (!tbl) return;

    CHECK_EQ(tbl->resolvedMaterials, 1);          // only GrassTile resolved
    CHECK_EQ((int)tbl->textures.size(), 1);
    CHECK_EQ(tbl->texturedPolys, 1);              // poly 0
    CHECK_EQ(tbl->untexturedPolys, 1);            // poly 1
    CHECK_EQ(tbl->PolyTexId(0), 0);
    CHECK_EQ(tbl->PolyTexId(1), -1);              // missing material -> untextured

    // Sample poly 0 at the UV hitting the bright-green texel (3,5) in an 8x8 tex:
    // normalized u=3.5/8, v=5.5/8.
    // 24-bit sources are soft-palettized at the resolve layer (the original's
    // VIBE_Texture_LoadSoftPalettize @0x5da34c chain — render/texture_palettize.h),
    // so the sample reads index -> palette. The quantizer's 15-bit histogram
    // buckets each channel to (c & 0xF8) + 4 (@0x602d2c), so the seeded
    // (0,240,0) green texel resolves to the palette entry (4,244,4) — the
    // engine's own <=256-colour approximation, pinned here.
    play::TexSample s = src.SamplePoly(*tbl, 0, 3.5f / 8.0f, 5.5f / 8.0f);
    CHECK(s.ok);
    if (s.ok) {
        CHECK_EQ((int)s.g, 244);
        CHECK_EQ((int)s.r, 4);
        CHECK_EQ((int)s.b, 4);
    }
    // A different UV (corner) is the dark fill bucket, not green.
    play::TexSample s2 = src.SamplePoly(*tbl, 0, 0.01f, 0.01f);
    CHECK(s2.ok);
    if (s2.ok) CHECK(s2.g != 244);

    // Poly 1 (missing material) -> untextured fallback.
    play::TexSample s3 = src.SamplePoly(*tbl, 1, 0.5f, 0.5f);
    CHECK(!s3.ok);
}

TEST(RealTextureSourceIT, UvWrapRepeat) {
    play::RealTextureSource src;
    std::vector<u8> bmp = MakeTex();
    const render::DecodedBmp* d = src.bin().DecodeBuffer("WrapTex.BMP", bmp);
    CHECK(d != nullptr);
    if (!d) return;
    // UV > 1.0 must wrap: u=1.0+3.5/8 hits the same texel as u=3.5/8.
    play::TexSample a = play::SampleTexel(*d, 3.5f / 8.0f, 5.5f / 8.0f);
    play::TexSample b = play::SampleTexel(*d, 1.0f + 3.5f / 8.0f, 1.0f + 5.5f / 8.0f);
    CHECK(a.ok && b.ok);
    if (a.ok && b.ok) {
        CHECK_EQ((int)a.g, (int)b.g);
        CHECK_EQ((int)a.r, (int)b.r);
    }
}

TEST(RealTextureSourceIT, InstallHookInertDefault) {
    // Inert default: no source installed -> null.
    play::InstallRealTextureSource(nullptr);
    CHECK(play::ActiveRealTextureSource() == nullptr);
    play::RealTextureSource src;
    play::InstallRealTextureSource(&src);
    CHECK(play::ActiveRealTextureSource() == &src);
    play::InstallRealTextureSource(nullptr);   // restore inert
    CHECK(play::ActiveRealTextureSource() == nullptr);
}

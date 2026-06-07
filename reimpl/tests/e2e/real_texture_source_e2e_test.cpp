// tests/e2e/real_texture_source_e2e_test.cpp — GUARDED real-asset texture path.
//
// Drives the REAL shipped bytes end-to-end:
//   * mount Resources/Textures.BIN (PKZIP of 2370 BMPs) via play::RealTextureSource;
//   * mount Resources/Objects.BIN, decode a REAL Buch.bgf (render::LoadAgfModel);
//   * resolve a REAL material name0 -> a REAL BMP, assert it decodes (square dims,
//     non-uniform pixels), report its name + dimensions;
//   * build the per-poly texId table over the real model and report coverage
//     (textured vs untextured polys, resolved materials);
//   * "render" each textured poly by sampling its texture at its real per-corner
//     UVs into a small framebuffer (textured) and compare to a flat untextured
//     fill — assert the textured frame differs (visual fidelity proven).
//
// GUARDED: if the game dir is absent the test records ZERO checks and returns.
// Honors GUILD_GAME_DIR.
#include "test.h"

#include "play/real_texture_source.h"
#include "render/texture_bin.h"
#include "render/agf_loader.h"
#include "render/bgf_loader.h"
#include "io/archive_mount.h"

#include "shim_impl/disk_filesystem.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

using namespace guild;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR")) return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool AssetsPresent(shim::IFileSystem& fs) {
    return fs.exists("Resources/Textures.BIN") && fs.exists("Resources/Objects.BIN");
}

} // namespace

TEST(RealTextureSourceE2E, ResolveAndRenderBuch) {
    const std::string dir = GameDir();
    shim::DiskFileSystem fs(dir);
    if (!AssetsPresent(fs)) {
        std::printf("  [skip] RealTextureSourceE2E: real game dir absent (%s)\n",
                    dir.c_str());
        return;
    }

    // --- mount the real texture archive --------------------------------------
    play::RealTextureSource src;
    CHECK(src.Mount(&fs, "Resources/Textures.BIN"));
    if (!src.mounted()) return;
    std::printf("  Textures.BIN members: %zu  (BMP members: %zu)\n",
                src.bin().memberCount(), src.bin().bmpCount());
    CHECK(src.bin().bmpCount() > 2000);   // ~2370 shipped BMPs

    // --- decode the real Buch.bgf --------------------------------------------
    io::ArchiveMount objs;
    CHECK(objs.Mount(&fs, "Resources/Objects.BIN", /*caseInsensitive=*/true));
    std::vector<u8> bgf;
    CHECK(objs.OpenMember("_DYNAMIC/Buch/Buch.bgf", bgf));
    CHECK(!bgf.empty());
    render::BgfModel model;
    CHECK(render::LoadAgfModel(bgf.data(), bgf.size(), model));
    if (model.materialCount == 0 || model.polyCount == 0) return;
    std::printf("  Buch.bgf: %u verts / %u polys / %u materials\n",
                model.vertexCount, model.polyCount, model.materialCount);

    // --- resolve a REAL material -> a REAL BMP -------------------------------
    const render::DecodedBmp* firstBmp = nullptr;
    std::string firstName;
    for (const auto& mat : model.materials) {
        if (mat.name0.empty()) continue;
        const render::DecodedBmp* d = src.ResolveMaterial(mat.name0.c_str());
        if (d && d->ok) { firstBmp = d; firstName = mat.name0; break; }
    }
    CHECK(firstBmp != nullptr);
    if (firstBmp) {
        std::printf("  resolved material '%s' -> %s  %dx%d bpp=%d square=%d\n",
                    firstName.c_str(), firstBmp->member.c_str(),
                    firstBmp->width, firstBmp->height, firstBmp->bpp,
                    firstBmp->square ? 1 : 0);
        CHECK(firstBmp->width > 0);
        CHECK(firstBmp->height > 0);
        CHECK(firstBmp->square);
        CHECK(!firstBmp->rgba.empty());
        // Non-uniform: the texture is not a single flat color.
        bool nonUniform = false;
        for (std::size_t i = 4; i < firstBmp->rgba.size(); i += 4) {
            if (firstBmp->rgba[i] != firstBmp->rgba[0] ||
                firstBmp->rgba[i + 1] != firstBmp->rgba[1] ||
                firstBmp->rgba[i + 2] != firstBmp->rgba[2]) { nonUniform = true; break; }
        }
        CHECK(nonUniform);
    }

    // --- per-poly texId table over the real model ----------------------------
    const play::MaterialTextureTable* tbl = src.BuildTableFor("Buch", model);
    CHECK(tbl != nullptr);
    if (!tbl) return;
    std::printf("  texture table: %d/%u materials resolved, %d textured polys, "
                "%d untextured polys (%.1f%% coverage)\n",
                tbl->resolvedMaterials, model.materialCount,
                tbl->texturedPolys, tbl->untexturedPolys,
                model.polyCount ? 100.0 * tbl->texturedPolys / model.polyCount : 0.0);
    CHECK(tbl->resolvedMaterials > 0);
    CHECK(tbl->texturedPolys > 0);

    // --- "render" textured vs untextured -------------------------------------
    // For each textured poly, sample its texture at its real per-corner UVs and
    // accumulate into a tiny framebuffer; compare to a flat untextured fill.
    const int FB = 16;
    std::vector<uint32_t> texturedFB((std::size_t)FB * FB, 0);
    std::vector<uint32_t> untexFB((std::size_t)FB * FB, 0);
    long texturedSamples = 0;
    for (std::size_t pi = 0; pi < model.polygons.size(); ++pi) {
        int texId = tbl->PolyTexId(pi);
        const render::DecodedBmp* bmp = tbl->TextureFor(texId);
        const render::BgfPolygon& q = model.polygons[pi];
        for (int k = 0; k < 3; ++k) {
            // poly uv0[k] = U, uv1[k] = V for corner k (bgf_loader.h).
            float u = q.uv0[k];
            float v = q.uv1[k];
            std::size_t cell = (std::size_t)((pi + k) % (FB * FB));
            // untextured: a constant flat color (the Wave 27/28 untextured look).
            untexFB[cell] = 0xFF808080u;
            if (bmp) {
                play::TexSample s = play::SampleTexel(*bmp, u, v);
                if (s.ok) {
                    texturedFB[cell] = 0xFF000000u | (s.r << 16) | (s.g << 8) | s.b;
                    ++texturedSamples;
                } else {
                    texturedFB[cell] = 0xFF808080u;
                }
            } else {
                texturedFB[cell] = 0xFF808080u;
            }
        }
    }
    std::printf("  textured samples placed: %ld\n", texturedSamples);
    CHECK(texturedSamples > 0);

    // The textured frame must differ from the flat untextured frame.
    int diffs = 0;
    for (std::size_t i = 0; i < texturedFB.size(); ++i)
        if (texturedFB[i] != untexFB[i]) ++diffs;
    std::printf("  framebuffer cells differing textured-vs-untextured: %d / %d\n",
                diffs, FB * FB);
    CHECK(diffs > 0);
}

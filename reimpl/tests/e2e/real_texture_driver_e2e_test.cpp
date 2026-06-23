// End-to-end (GUARDED) validation of the real-asset texture->render driver
// against the REAL shipped game bytes:
//   europe_guild_1400_original/Resources/Textures.BIN — PKZIP of ~2370 BMPs.
//
// Drives the driver over the real archive: mount Textures.BIN -> decode every
// real BMP member through the reconstructed texture/BMP loader -> rasterize one
// chosen real texture into a 16-bit surface with the real RGBZ rasterizer ->
// present into a FileDumpGraphicsDevice (a real 24-bit BMP on disk). Asserts the
// real decode counts and that real texture bytes reach a non-blank image.
//
// GUARDED: if the asset folder is absent every test passes trivially. Honors
// GUILD_GAME_DIR (see app_real_run_e2e_test.cpp).
#include "test.h"

#include "app/real_texture_driver.h"
#include "shim_impl/disk_filesystem.h"
#include "shim_impl/filedump_graphics.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace guild;

namespace {

std::string gameDir() {
    const char* env = std::getenv("GUILD_GAME_DIR");
    if (env && *env) return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool assetsPresent(const std::string& root) {
    shim::DiskFileSystem fs(root);
    return fs.exists("Resources/Textures.BIN");
}

std::string makeTempDir() {
    char templ[] = "/tmp/guild_real_tex_e2e_XXXXXX";
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
// Full real run: decode the whole Textures.BIN through the driver, rasterize a
// chosen real square texture, dump one frame BMP. Assert the real counts.
// ---------------------------------------------------------------------------
TEST(RealTextureDriverE2E, DriveRealTexturesBinToImage) {
    std::string root = gameDir();
    if (!assetsPresent(root)) {
        std::printf("[skip] Textures.BIN absent under %s\n", root.c_str());
        CHECK(true);
        return;
    }

    std::string dir = makeTempDir();
    CHECK(!dir.empty());

    shim::DiskFileSystem fs(root);
    // Landkarte2 is a known real 256x256 8-bit square texture in the archive.
    app::TextureRenderResult r = app::RenderRealTexturesFromArchive(
        &fs, "Resources/Textures.BIN", "_DYNAMIC/2D/Landkarte2.bmp",
        256, 256, dir, /*maxDecode=*/0);

    // ---- decode tier (real archive inventory) ----
    CHECK_EQ(r.archiveMembers, 2422);   // PKZIP central-dir total (dirs + leaves)
    CHECK_EQ(r.bmpMembers, 2370);       // real BMP leaves
    // RECALIBRATED (wave-5 W5-TX): was 1949 (8-bit square BMPs only). The ground
    // tile textures (_DYNAMIC/Boden/*.bmp) are 24-bit, so DecodeBmpIntoTexture now
    // runs the engine's 24-bit software-palettize arm (VIBE_Texture_LoadSoftPalettize
    // @0x5da34c -> VIBE_Quant_BuildPalette @0x6029f0, render/texture_palettize.cpp)
    // when the 8-bit BmpLoadBuffer yields nothing — exactly what the engine's
    // VIBE_Texture_LoadByName software branch does for a 24-bit source. All 2370
    // square BMPs now decode (the 8-bit + the 24-bit ones), not just the 8-bit
    // subset. The old pin under-counted by skipping the 24-bit textures the engine
    // loads. (squareTextures still == texturesDecoded: the square check gates both.)
    CHECK_EQ(r.texturesDecoded, 2370);  // all square BMPs (8-bit + palettized 24-bit)
    CHECK_EQ(r.squareTextures, r.texturesDecoded);  // square check gates decode
    CHECK(!r.samples.empty());

    std::printf("[RealTextureDriverE2E] Textures.BIN: %d members, %d BMPs, "
                "%d decoded (%d square)\n",
                r.archiveMembers, r.bmpMembers, r.texturesDecoded, r.squareTextures);
    for (const auto& s : r.samples)
        std::printf("    sample %-34s %dx%d bpp=%d nonzeroTexels=%ld\n",
                    s.member.c_str(), s.width, s.width, s.bpp, s.nonzeroTexels);

    // ---- render tier (real texture bytes -> image) ----
    CHECK(r.rendered);
    CHECK_EQ(r.renderWidth, 256);
    CHECK_EQ(r.renderHeight, 256);
    CHECK(r.nonBlankPixels > 0);        // real texture content reached the frame
    CHECK(!r.renderedTexture.empty());
    CHECK(!r.framePath.empty());

    std::printf("[RealTextureDriverE2E] rasterized '%s' -> %dx%d, %ld/%d non-blank px\n"
                "    frame dumped: %s\n",
                r.renderedTexture.c_str(), r.renderWidth, r.renderHeight,
                r.nonBlankPixels, r.renderWidth * r.renderHeight, r.framePath.c_str());

    // The dumped BMP must decode back to a 256x256 image with real content.
    auto img = shim::FileDumpGraphicsDevice::DecodeBmp24(readFile(r.framePath));
    CHECK(img.ok);
    CHECK_EQ(img.width, 256);
    CHECK_EQ(img.height, 256);
    long imgNonblack = 0;
    for (std::size_t i = 0; i < img.rgb.size(); ++i) if (img.rgb[i]) ++imgNonblack;
    CHECK(imgNonblack > 0);
    std::printf("[RealTextureDriverE2E] decoded dumped BMP: %dx%d, nonblack bytes=%ld\n",
                img.width, img.height, imgNonblack);
}

// ---------------------------------------------------------------------------
// Default render target (first square texture decoded) also produces a frame —
// proves the driver does not depend on a hard-coded member name.
// ---------------------------------------------------------------------------
TEST(RealTextureDriverE2E, DefaultRenderTargetNonBlank) {
    std::string root = gameDir();
    if (!assetsPresent(root)) {
        std::printf("[skip] Textures.BIN absent under %s\n", root.c_str());
        CHECK(true);
        return;
    }
    shim::DiskFileSystem fs(root);
    // Cap decode to keep this case fast; no dump.
    app::TextureRenderResult r = app::RenderRealTexturesFromArchive(
        &fs, "Resources/Textures.BIN", /*renderMember=*/"",
        256, 256, /*dumpDir=*/"", /*maxDecode=*/64);

    CHECK_EQ(r.archiveMembers, 2422);
    CHECK_EQ(r.bmpMembers, 2370);
    CHECK_EQ(r.texturesDecoded, 64);    // capped
    CHECK(r.rendered);
    CHECK(r.nonBlankPixels > 0);
    std::printf("[RealTextureDriverE2E] default target '%s' nonblank=%ld (cap=64 decoded)\n",
                r.renderedTexture.c_str(), r.nonBlankPixels);
}

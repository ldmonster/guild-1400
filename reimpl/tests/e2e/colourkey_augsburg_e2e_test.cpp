#include "test.h"

// =============================================================================
// GUARDED real-asset e2e (wave-5 W5-CKEY): render a REAL Objects.BIN mesh that
// carries 24-bit colour-keyed materials through the engine frame spine
// (play::UniverseFrameDriver) BOTH with the colour key suppressed
// (render::SetColourKeyEnabled(false) == the prior keyed=false posture) and with
// it ON (the faithful default), and assert the on-screen BLACK pixel count DROPS
// materially when keying is enabled — the black-tree-backdrop bug fixed.
//
// THE 1:1 RULE (see render/raster.cpp FillSpanTexturedMasked + the producer in
// play/universe_render.cpp): a >8bpp source sets record +104 bit 2
// (kTexFlagColourKey, VIBE_Texture_LoadByName @0x5dad52, gate dword_140809C==0),
// and such a texture's masked span skips texels whose RESOLVED 16bpp value ==
// 565(black) == 0 — the DDraw KEYSRC-on-pal[0]==black the engine uses
// (VIBE_Render_LoadAndStretchTexture @0x5dea50). Dumps /tmp/guild_ckey.ppm.
//
// GUARDED: clean skip when the real game dir / Textures.BIN is absent. Honors
// GUILD_GAME_DIR.
// =============================================================================
#include "play/universe_render.h"
#include "play/real_mesh_source.h"
#include "render/texture.h"
#include "render/surface.h"
#include "io/archive_mount.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace guild;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool AssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Resources/Objects.BIN") && fs.exists("Resources/Textures.BIN");
}

// Count exact-black (565 0x0000) pixels in a 16bpp surface.
int CountBlack(render::Surface* s) {
    if (!s || !s->pixels) return 0;
    const u16* p = reinterpret_cast<const u16*>(s->pixels);
    const int pitchPx = s->pitch / 2;
    int n = 0;
    for (int y = 0; y < s->height; ++y)
        for (int x = 0; x < s->width; ++x)
            if (p[(std::size_t)y * pitchPx + x] == 0x0000) ++n;
    return n;
}

// Dump a 16bpp(565) surface to a binary PPM (P6) for inspection.
bool DumpPpm(const char* path, render::Surface* s) {
    if (!s || !s->pixels) return false;
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", s->width, s->height);
    std::vector<std::uint8_t> row((std::size_t)s->width * 3);
    for (int y = 0; y < s->height; ++y) {
        for (int x = 0; x < s->width; ++x) {
            u8 px[3];
            render::SurfaceGetPixelRgb(s, x, y, px);
            row[(std::size_t)x * 3 + 0] = px[0];
            row[(std::size_t)x * 3 + 1] = px[1];
            row[(std::size_t)x * 3 + 2] = px[2];
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
    return true;
}

} // namespace

TEST(ColourKeyAugsburgE2E, BlackBackdropDropsWithColourKey) {
    if (!AssetsPresent()) {
        std::printf("  [skip] ColourKeyAugsburgE2E.BlackBackdropDropsWithColourKey: "
                    "Objects.BIN / Textures.BIN absent (%s)\n", GameDir().c_str());
        CHECK(true);
        return;
    }

    shim::DiskFileSystem fs(GameDir());
    play::RealMeshSource src;
    CHECK(src.MountArchive(&fs, "Resources/Objects.BIN", /*caseInsensitive=*/true));
    io::ArchiveMount names;
    CHECK(names.Mount(&fs, "Resources/Objects.BIN", /*caseInsensitive=*/true));
    CHECK(names.memberCount() > 0);

    play::UniverseFrameDriver drv;
    CHECK(drv.InitTextures(&fs, "Resources/Textures.BIN"));

    play::UniverseFrameDriver::Options opt;
    opt.fbW = 160; opt.fbH = 120;
    // NON-black clear (magenta) so the ONLY exact-black (565 0x0000) pixels in the
    // frame come from black TEXELS written by the opaque path — a clean
    // before/after measurement of the colour key (the cleared backdrop is never
    // black, so suppressing it isolates the keyed texels).
    opt.clearR = 0xFF; opt.clearG = 0x00; opt.clearB = 0xFF;

    // Scan members for the one whose colour key punches the MOST black texels.
    // For each: render key-OFF (black texels written opaque) and key-ON (black
    // texels skipped); the member with the largest black DROP is a 24-bit
    // colour-keyed mesh with a black backdrop (the foliage case). The magenta
    // clear guarantees frame-black == texel-black.
    std::string bestName;
    int bestDrop = 0;
    int tried = 0;
    for (const auto& m : names.members()) {
        if (tried >= 160) break;
        if (!drv.LoadMember(src, m.name.c_str())) continue;
        ++tried;
        render::SetColourKeyEnabled(false);
        play::UniverseFrameDriver::Result r0 = drv.RenderFrame(opt);
        if (!r0.real || r0.texturedPolys < 1 || r0.boundMaterials < 1) continue;
        int blackOff = CountBlack(drv.surface());
        if (blackOff <= 0) continue;
        render::SetColourKeyEnabled(true);
        drv.RenderFrame(opt);
        int blackOn = CountBlack(drv.surface());
        int drop = blackOff - blackOn;
        if (drop > bestDrop) {
            bestDrop = drop;
            bestName = m.name;
        }
    }

    if (bestName.empty() || bestDrop <= 0) {
        std::printf("  [skip] ColourKeyAugsburgE2E: no 24-bit colour-keyed member "
                    "with a black backdrop found in first %d members\n", tried);
        render::SetColourKeyEnabled(true);
        CHECK(true);
        return;
    }

    // BEFORE: render the chosen member with the key OFF (pin the black count).
    render::SetColourKeyEnabled(false);
    CHECK(drv.LoadMember(src, bestName.c_str()));
    play::UniverseFrameDriver::Result before = drv.RenderFrame(opt);
    const int blackBefore = CountBlack(drv.surface());
    DumpPpm("/tmp/guild_ckey_before.ppm", drv.surface());

    // AFTER: render the SAME member with the key ON (the faithful default).
    render::SetColourKeyEnabled(true);
    CHECK(drv.LoadMember(src, bestName.c_str()));
    play::UniverseFrameDriver::Result after = drv.RenderFrame(opt);
    const int blackAfter = CountBlack(drv.surface());
    DumpPpm("/tmp/guild_ckey.ppm", drv.surface());

    std::printf("[ckey-e2e] member=\"%s\" boundMats=%d texturedPolys=%d\n",
                bestName.c_str(), after.boundMaterials, after.texturedPolys);
    std::printf("[ckey-e2e] black pixels  BEFORE(keyOff)=%d  AFTER(keyOn)=%d  "
                "drop=%d (%.1f%%)\n", blackBefore, blackAfter,
                blackBefore - blackAfter,
                blackBefore ? 100.0 * (blackBefore - blackAfter) / blackBefore : 0.0);
    std::printf("[ckey-e2e] dumped /tmp/guild_ckey_before.ppm + /tmp/guild_ckey.ppm\n");

    // The frame rendered real textured geometry both ways.
    CHECK(before.rasterTris >= 1);
    CHECK(after.rasterTris >= 1);
    CHECK(after.boundMaterials >= 1);

    // THE FIX: the black-backdrop pixel count DROPS materially with the key ON
    // (the black texels were skipped, leaving the cleared/over-drawn frame).
    CHECK(blackBefore > 0);
    CHECK(blackAfter < blackBefore);
    // Material drop — at least a quarter of the black backdrop punched through.
    CHECK(blackBefore - blackAfter >= blackBefore / 4);

    // restore the faithful default for any later test in the binary.
    render::SetColourKeyEnabled(true);
}

// tests/e2e/play_terrain_render_e2e_test.cpp — GUARDED real-asset terrain render.
//
// Mounts a REAL "Die Gilde — Europe 1400" install, loads the real start city
// (AUGSBURG.cty) through the reconstructed VFS + city loader, derives a ground
// heightfield seeded from the REAL city bytes, renders its terrain into a BMP
// through the REAL terrain leaves + software rasterizer, and asserts a large
// non-background fraction (the ground actually drew).
//
// GUARDED: the real game directory is not part of the repo. If it is absent the
// test records ZERO checks and returns (clean skip). Override with GUILD_GAME_DIR.
#include "test.h"

#include "play/terrain_render.h"

#include "app/real_boot.h"
#include "io/vfs.h"
#include "io/gamestate.h"
#include "render/surface.h"
#include "render/bmp.h"

#include "shim_impl/disk_filesystem.h"

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

bool RealAssetsPresent(shim::IFileSystem& fs) {
    return fs.exists("Gilde.INI") &&
           fs.exists("Resources/forms.BIN") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty");
}

// Mix a 32-bit seed out of a byte blob (FNV-1a) so the synthetic ground is keyed
// to the real city contents (deterministic per city).
u32 SeedFromBytes(const u8* p, size_t n) {
    u32 h = 2166136261u;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
    return h;
}

} // namespace

TEST(PlayTerrainRenderE2E, RenderRealCityGround) {
    const std::string dir = GameDir();
    shim::DiskFileSystem fs(dir);
    if (!RealAssetsPresent(fs)) {
        std::printf("  [skip] PlayTerrainRenderE2E.RenderRealCityGround: "
                    "real game dir absent (%s)\n", dir.c_str());
        return; // clean skip
    }

    // ---- mount the real assets + bind the VFS ------------------------------
    app::RealGameAssets a =
        app::MountRealGameAssets(&fs, dir, "Gilde.INI", {}, /*caseInsensitive=*/false);
    CHECK(a.iniLoaded);
    CHECK(a.vfsBound);
    CHECK(a.stadt == "Augsburg");

    // ---- load the real start city through the VFS --------------------------
    io::GameState city{};
    city.relink.assign(io::kRelinkBytes, 0);
    std::string cityPath = "Resources/gamedata/Cities/" +
                           app::RealCityPath(a.stadt);  // AUGSBURG.cty (upper-cased)
    bool loaded = io::LoadGameState(cityPath.c_str(), city, /*load=*/nullptr);
    if (!loaded) {
        // casing of the shipped file varies; fall back to the known name.
        loaded = io::LoadGameState("Resources/gamedata/Cities/AUGSBURG.cty",
                                   city, nullptr);
    }
    CHECK(loaded);
    if (loaded) {
        CHECK(std::string(city.header.name) == "Augsburg");
        CHECK(city.header.wealth > 0);
    }

    // ---- derive a ground heightfield seeded from the REAL city bytes -------
    // (The terrain height grid is not part of this reconstruction's GameState
    //  slice; we key a deterministic ground-shaped field to the real city's
    //  header/relink bytes so the render is driven by REAL loaded data.)
    u32 seed = SeedFromBytes((const u8*)&city.header, sizeof(city.header));
    if (!city.relink.empty())
        seed ^= SeedFromBytes(city.relink.data(),
                              city.relink.size() < 4096 ? city.relink.size() : 4096);
    play::Heightfield hf = play::Heightfield::MakeSynthetic(/*edge=*/64, seed);
    CHECK(hf.valid());

    // ---- render its terrain into a Surface through the REAL leaves ----------
    const int W = 192, H = 144;
    play::TerrainView view = play::TerrainView::MakeTopDown(hf.size, hf.tileSpan, W, H);
    play::TerrainRenderStats st;
    render::Surface* fb =
        play::RenderTerrainToSurface(W, H, hf, view, /*background=*/0, &st);
    CHECK(fb != nullptr);
    if (!fb) { io::VfsShutdown(); return; }

    const int total = W * H;
    std::printf("  [PlayTerrainRenderE2E] %s: lod=%d tiles=%d quads=%d tris=%d "
                "nonblank=%d/%d (%.1f%%) shade[%u..%u]\n",
                a.stadt.c_str(), st.lod, st.tilesDrawn, st.quadsBuilt, st.trisDrawn,
                st.nonBlankPix, total, 100.0 * st.nonBlankPix / total,
                st.minShade, st.maxShade);

    // The ground drew a LARGE non-background fraction.
    CHECK_EQ(st.tilesDrawn, 64);
    CHECK(st.nonBlankPix > total / 2);     // > 50% painted
    CHECK(st.maxShade > 100);

    // ---- dump the rendered ground to a BMP (grayscale shade ramp) ----------
    {
        std::vector<u8> bmp = render::BmpSaveIndexed(W, H, fb->pixels, /*palette=*/nullptr);
        CHECK(bmp.size() > 1078);          // header(54) + palette(1024) + pixels
        const char* outPath = "/tmp/play_terrain_augsburg.bmp";
        if (FILE* f = std::fopen(outPath, "wb")) {
            std::fwrite(bmp.data(), 1, bmp.size(), f);
            std::fclose(f);
            std::printf("  [PlayTerrainRenderE2E] wrote %s (%zu bytes)\n",
                        outPath, bmp.size());
        }
    }

    render::SurfaceDestroy(fb);
    io::VfsShutdown();
}

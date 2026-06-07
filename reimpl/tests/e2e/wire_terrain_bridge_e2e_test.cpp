// tests/e2e/wire_terrain_bridge_e2e_test.cpp — GUARDED P6 live AUGSBURG terrain.
//
// Mounts a REAL "Die Gilde — Europe 1400" install, loads AUGSBURG.cty through the
// reconstructed VFS + city loader, derives a ground heightfield seeded from the REAL
// city bytes, then DE-INERTS the live terrain hook and renders the floor through the
// WHOLE live frame entry (render::RenderMainViewFrame -> BeginUniverseFrame, the same
// orchestration the running game takes). Asserts real terrain pixels show up on the
// live path (vs blank when the hook is left inert) and dumps the frame to a BMP.
//
// GUARDED: the real game directory is not in the repo. Absent => ZERO checks, clean
// return. Override the path with GUILD_GAME_DIR.
#include "test.h"

#include "play/wire_terrain_bridge.h"
#include "play/terrain_render.h"

#include "app/real_boot.h"
#include "io/vfs.h"
#include "io/gamestate.h"
#include "render/frame.h"
#include "render/surface.h"
#include "render/bmp.h"

#include "shim_impl/disk_filesystem.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR")) return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool RealAssetsPresent(shim::IFileSystem& fs) {
    return fs.exists("Gilde.INI") &&
           fs.exists("Resources/forms.BIN") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty");
}

u32 SeedFromBytes(const u8* p, size_t n) {
    u32 h = 2166136261u;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
    return h;
}

int NonBg(const render::Surface* fb, u8 bg) {
    if (!fb || !fb->pixels) return 0;
    int n = 0;
    for (int y = 0; y < fb->height; ++y) {
        const u8* row = fb->pixels + (size_t)y * fb->widthPx;
        for (int x = 0; x < fb->width; ++x) if (row[x] != bg) ++n;
    }
    return n;
}

render::Surface* MakeFb(int W, int H, u8 bg) {
    render::Surface* fb = render::SurfaceCreate(W, H, 8);
    if (fb) std::memset(fb->pixels, bg, (size_t)fb->pitch * fb->height);
    return fb;
}

} // namespace

TEST(WireTerrainBridgeE2E, LiveAugsburgGroundDeInerted) {
    const std::string dir = GameDir();
    shim::DiskFileSystem fs(dir);
    if (!RealAssetsPresent(fs)) {
        std::printf("  [skip] WireTerrainBridgeE2E.LiveAugsburgGroundDeInerted: "
                    "real game dir absent (%s)\n", dir.c_str());
        return; // clean skip
    }

    // ---- mount the real assets + bind the VFS ------------------------------
    app::RealGameAssets a =
        app::MountRealGameAssets(&fs, dir, "Gilde.INI", {}, /*caseInsensitive=*/false);
    CHECK(a.iniLoaded);
    CHECK(a.vfsBound);

    // ---- load the real start city ------------------------------------------
    io::GameState city{};
    city.relink.assign(io::kRelinkBytes, 0);
    std::string cityPath = "Resources/gamedata/Cities/" + app::RealCityPath(a.stadt);
    bool loaded = io::LoadGameState(cityPath.c_str(), city, nullptr);
    if (!loaded)
        loaded = io::LoadGameState("Resources/gamedata/Cities/AUGSBURG.cty", city, nullptr);
    CHECK(loaded);

    // ---- derive a ground heightfield seeded from the REAL city bytes -------
    u32 seed = SeedFromBytes((const u8*)&city.header, sizeof(city.header));
    if (!city.relink.empty())
        seed ^= SeedFromBytes(city.relink.data(),
                              city.relink.size() < 4096 ? city.relink.size() : 4096);

    const int W = 192, H = 144;

    // ---- INERT live frame (hook left as the no-op): floor stays blank -------
    play::TerrainBridgeContext inertCtx{};
    render::Surface* inertFb = MakeFb(W, H, 0);
    CHECK(inertFb != nullptr);
    inertCtx.fb = inertFb;
    inertCtx.hf = play::Heightfield::MakeSynthetic(/*edge=*/64, seed);
    inertCtx.view = play::TerrainView::MakeTopDown(inertCtx.hf.size, inertCtx.hf.tileSpan, W, H);
    inertCtx.background = 0;
    {
        render::FrameState fs0{}; render::FrameHooks hk0{};
        play::InstallInertTerrainBridge(fs0, hk0, &inertCtx);
        render::RenderMainViewFrame(fs0, hk0);
    }
    int inertNB = NonBg(inertFb, 0);

    // ---- REAL live frame (de-inert the terrain hook) -----------------------
    play::TerrainBridgeContext realCtx{};
    render::Surface* realFb = MakeFb(W, H, 0);
    CHECK(realFb != nullptr);
    realCtx.fb = realFb;
    realCtx.hf = play::Heightfield::MakeSynthetic(/*edge=*/64, seed);
    realCtx.view = play::TerrainView::MakeTopDown(realCtx.hf.size, realCtx.hf.tileSpan, W, H);
    realCtx.background = 0;
    play::TerrainBridgeInstall ins;
    {
        render::FrameState fs1{}; render::FrameHooks hk1{};
        ins = play::InstallRealTerrainBridge(fs1, hk1, &realCtx);
        render::RenderMainViewFrame(fs1, hk1);
    }
    int realNB = NonBg(realFb, 0);
    const int total = W * H;

    std::printf("  [WireTerrainBridgeE2E] %s: deInert=%d wasInert=%d | "
                "inert nonblank=%d  real nonblank=%d/%d (%.1f%%) "
                "lod=%d tiles=%d quads=%d tris=%d shade[%u..%u]\n",
                a.stadt.c_str(), (int)ins.installed, (int)ins.wasInert,
                inertNB, realNB, total, 100.0 * realNB / total,
                realCtx.lastStats.lod, realCtx.lastStats.tilesDrawn,
                realCtx.lastStats.quadsBuilt, realCtx.lastStats.trisDrawn,
                realCtx.lastStats.minShade, realCtx.lastStats.maxShade);

    // The de-inert took effect; the live walk reached the real leaf.
    CHECK(ins.installed == true);
    CHECK(ins.wasInert == true);
    CHECK(realCtx.drewReal == true);
    CHECK(inertCtx.drewReal == false);

    // Inert live floor is blank; the de-inerted live floor shows REAL terrain pixels.
    CHECK_EQ(inertNB, 0);
    CHECK_EQ(realCtx.lastStats.tilesDrawn, 64);
    CHECK(realNB > total / 2);            // > 50% of the live frame is ground now
    CHECK(realNB > inertNB);
    CHECK(realCtx.lastStats.maxShade > 100);

    // ---- dump the live de-inerted ground to a BMP --------------------------
    {
        std::vector<u8> bmp =
            render::BmpSaveIndexed(W, H, realFb->pixels, /*palette=*/nullptr);
        CHECK(bmp.size() > 1078);
        const char* outPath = "/tmp/wire_terrain_augsburg_live.bmp";
        if (FILE* f = std::fopen(outPath, "wb")) {
            std::fwrite(bmp.data(), 1, bmp.size(), f);
            std::fclose(f);
            std::printf("  [WireTerrainBridgeE2E] wrote %s (%zu bytes)\n",
                        outPath, bmp.size());
        }
    }

    render::SurfaceDestroy(inertFb);
    render::SurfaceDestroy(realFb);
    io::VfsShutdown();
}

#include "test.h"

// GUARDED real-asset e2e: render the REAL loaded AUGSBURG world to a BMP.
//
//   1. mount the real game assets (Gilde.INI + Resources/*.BIN) via the real boot
//      helper, bind the VFS to the real install,
//   2. load the shipped Resources/gamedata/Cities/AUGSBURG.cty through the real
//      world-load driver (io::LoadWorld) — populates sim::g_objects / g_persons,
//   3. build a renderable draw list from the REAL live entity arrays
//      (play::WorldRenderer) and render ONE frame through the REAL pipeline
//      (ProjectVerticesToScreen -> RadixSortDrawList -> RasterizeMeshList ->
//      PresentFrame) into a headless FileDumpGraphicsDevice -> a BMP artifact,
//   4. assert N>0 scene objects were drawn and a meaningful non-background pixel
//      fraction (the real frame is not blank).
//
// GUARDED: skip cleanly (trivial pass) when the real game dir is absent. Honors
// GUILD_GAME_DIR.
#include "app/real_boot.h"
#include "io/save_world_load.h"
#include "io/save.h"
#include "io/vfs.h"
#include "play/world_render.h"
#include "render/colorformat.h"
#include "render/surface.h"
#include "sim/entity.h"
#include "shim_impl/disk_filesystem.h"
#include "shim_impl/filedump_graphics.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace guild;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Gilde.INI") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty");
}

} // namespace

TEST(WorldRenderE2E, RenderRealAugsburgToBmp) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] WorldRenderE2E.RenderRealAugsburgToBmp: real game dir "
                    "absent (%s)\n", GameDir().c_str());
        CHECK(true);   // clean skip
        return;
    }

    // 1. Mount the real assets + bind the VFS to the real install.
    shim::DiskFileSystem fs(GameDir());
    app::RealGameAssets assets =
        app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI");
    CHECK(assets.vfsBound);
    std::printf("[wre2e] iniLoaded=%d stadt=%s archives=%zu members=%zu\n",
                (int)assets.iniLoaded, assets.stadt.c_str(),
                assets.archives.size(), assets.totalMembers());

    // 2. Load the real AUGSBURG.cty into the live entity arrays.
    sim::ResetEntityArrays();
    io::WorldState world{};
    bool loaded = io::LoadWorld("Resources/gamedata/Cities/AUGSBURG.cty", world);
    std::printf("[wre2e] LoadWorld -> %s  objects=%u sceneTiles=%u cityRecs=%u\n",
                loaded ? "true" : "false", world.objectCount,
                world.sceneTileCount, world.cityRecCount);
    CHECK(loaded);

    // Count the real live entities the loader populated.
    int liveObjects = 0;
    for (int i = 0; i < sim::kObjectCapacity; ++i)
        if (sim::g_objects[i].alive) ++liveObjects;
    int livePersons = 0;
    for (int i = 0; i < sim::kPersonCapacity; ++i)
        if (sim::g_persons[i].marker != -1) ++livePersons;
    std::printf("[wre2e] live g_objects=%d  live g_persons=%d  g_sceneNodes=%d\n",
                liveObjects, livePersons, sim::g_sceneNodeCount);
    CHECK(liveObjects > 0);   // the real city populated the object array

    // 3. Build the draw list from the REAL entities + render through the real
    //    pipeline into a headless file-dump device (-> BMP).
    play::WorldRenderer wr;
    play::WorldRenderer::Options opt;
    opt.fbW = 128; opt.fbH = 96;
    opt.clearR = 0; opt.clearG = 0; opt.clearB = 64;   // dark-blue background
    opt.emitTerrain = true;
    opt.scanObjects = true;
    opt.scanScene = true;
    opt.scanPersons = true;

    shim::FileDumpGraphicsDevice dev;
    CHECK(dev.init(opt.fbW, opt.fbH, 16, /*fullscreen=*/false));
    dev.configureDump("/tmp", "guild_augsburg", shim::FileDumpGraphicsDevice::kBmp);

    play::RenderStats st = wr.render(opt, dev);
    const play::WorldDrawList& dl = wr.lastBuild();

    std::printf("[wre2e] draw list: terrain=%d sceneObjects=%d "
                "(obj=%d person=%d scene=%d)  appendedPolys=%d rasterTris=%d\n",
                (int)dl.hasTerrain, dl.sceneObjects(), dl.objectQuads,
                dl.personQuads, dl.sceneQuads, st.appendedPolys, st.rasterTris);

    // 4a. N>0 scene objects drawn from the real world.
    CHECK(dl.sceneObjects() > 0);
    // The real pipeline projected + rasterized real triangles.
    CHECK(st.appendedPolys > 0);
    CHECK(st.rasterTris > 0);
    CHECK(st.presented);

    // 4b. A meaningful non-background pixel fraction (the frame is not blank).
    int changed = wr.binder().nonClearPixels();
    int total = opt.fbW * opt.fbH;
    double frac = total ? (double)changed / (double)total : 0.0;
    std::printf("[wre2e] non-background pixels = %d / %d (%.1f%%)\n",
                changed, total, frac * 100.0);
    CHECK(changed > 0);
    CHECK(frac > 0.02);   // terrain + objects cover > 2% of the frame

    // The frame was dumped to a real BMP artifact.
    std::string bmp = dev.framePath(0, shim::FileDumpGraphicsDevice::kBmp);
    std::printf("[wre2e] dumped frame -> %s\n", bmp.c_str());
    shim::RgbImage img = dev.snapshotRgb();
    CHECK(img.width == opt.fbW);
    CHECK(img.height == opt.fbH);

    sim::ResetEntityArrays();
    io::VfsShutdown();
}

#include "test.h"

// GUARDED real-asset e2e: render REAL AUGSBURG with REAL mesh geometry, end-to-end.
//
//   1. mount the real game assets (Gilde.INI + Resources/*.BIN) + bind the VFS,
//   2. load Resources/gamedata/Cities/AUGSBURG.cty -> live sim::g_objects,
//   3. RealCityRenderer::Init mounts Resources/Objects.BIN and harvests the REAL
//      .bgf member names (the AGF meshes), installs the REAL mesh source as the
//      live object_mesh_render MeshResolver, and renders every live object as its
//      ACTUAL decoded geometry (NOT a quad) to a headless FileDumpGraphicsDevice
//      -> a BMP in /tmp,
//   4. assert >0 real meshes resolved (vertex count > 4), non-clear pixels, and a
//      screen click picks a real object. Report the real counts.
//
// GUARDED: clean skip when the real game dir is absent. Honors GUILD_GAME_DIR.
#include "app/real_boot.h"
#include "io/save_world_load.h"
#include "io/vfs.h"
#include "play/real_city_render.h"
#include "play/scene_pick.h"
#include "sim/entity.h"
#include "shim/IGraphicsDevice.h"
#include "shim_impl/disk_filesystem.h"
#include "shim_impl/filedump_graphics.h"

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
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty") &&
           fs.exists("Resources/Objects.BIN");
}

bool TexturesPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Resources/Textures.BIN");
}

} // namespace

TEST(RealCityRenderE2E, RenderRealAugsburgRealMeshesToBmp) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] RealCityRenderE2E.RenderRealAugsburgRealMeshesToBmp: "
                    "real game dir absent (%s)\n", GameDir().c_str());
        CHECK(true);
        return;
    }

    // 1. Mount real assets + bind the VFS.
    shim::DiskFileSystem fs(GameDir());
    app::RealGameAssets assets = app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI");
    CHECK(assets.vfsBound);

    // 2. Load the real AUGSBURG city into the live arrays.
    sim::ResetEntityArrays();
    io::WorldState world{};
    bool loaded = io::LoadWorld("Resources/gamedata/Cities/AUGSBURG.cty", world);
    CHECK(loaded);
    int liveObjects = 0;
    for (int i = 0; i < sim::kObjectCapacity; ++i)
        if (sim::g_objects[i].alive) ++liveObjects;
    std::printf("[rcr-e2e] LoadWorld=%d live g_objects=%d\n", (int)loaded, liveObjects);
    CHECK(liveObjects > 0);

    // 3. Init the REAL mesh source over Objects.BIN + render the live world as real
    //    meshes to a headless file-dump device -> a BMP.
    play::RealCityRenderer rc;
    bool inited = rc.Init(&fs, "Resources/Objects.BIN");
    std::printf("[rcr-e2e] Objects.BIN mounted=%d .bgf members harvested=%zu\n",
                (int)inited, rc.meshNames().size());
    CHECK(inited);
    CHECK(rc.mounted());
    CHECK(!rc.meshNames().empty());

    play::RealCityRenderer::Options opt;
    opt.fbW = 160; opt.fbH = 120;
    opt.maxObjects = 32;
    opt.pixelsPerUnit = 0.18f;

    shim::FileDumpGraphicsDevice dev;
    CHECK(dev.init(opt.fbW, opt.fbH, 16, /*fullscreen=*/false));
    dev.configureDump("/tmp", "guild_augsburg_realmesh",
                      shim::FileDumpGraphicsDevice::kBmp);

    play::RealCityRenderer::Result r = rc.Render(opt, dev);
    std::printf("[rcr-e2e] usedRealResolver=%d meshObjects=%d quadFallbacks=%d "
                "meshTris=%d appendedPolys=%d rasterTris=%d\n",
                (int)r.usedRealResolver, r.meshObjects, r.quadFallbacks,
                r.meshTris, r.appendedPolys, r.rasterTris);
    std::printf("[rcr-e2e] distinctMeshes=%d maxMeshVerts=%d nonClearPixels=%d "
                "presented=%d bmp=%s\n",
                r.distinctMeshes, r.maxMeshVerts, r.nonClearPixels,
                (int)r.presented, r.bmpPath.c_str());

    // 4a. REAL meshes resolved through the real AGF decode (vertex count > 4).
    CHECK(r.usedRealResolver);
    CHECK(r.meshObjects > 0);            // >0 objects drew as a RESOLVED real mesh
    CHECK(r.distinctMeshes > 0);         // distinct shipped .bgf meshes resolved
    CHECK(r.maxMeshVerts > 4);           // real geometry, not a 2-tri quad
    CHECK(r.meshTris > 2 * r.meshObjects); // more tris than a quad render
    CHECK(r.appendedPolys > 0);
    CHECK(r.rasterTris > 0);

    // 4b. The frame is not blank, and the BMP was written.
    CHECK(r.nonClearPixels > 100);
    CHECK(r.presented);
    CHECK(!r.bmpPath.empty());

    // 4c. A screen click picks a real object. Project a known object's placement to
    //     a screen point (the same camera the renderer used) and pick there.
    {
        // Pick the first live object's grid placement.
        int firstSlot = -1;
        for (int i = 0; i < sim::kObjectCapacity; ++i)
            if (sim::g_objects[i].alive) { firstSlot = i; break; }
        CHECK(firstSlot >= 0);
        float world3[3] = {
            opt.originX + (float)(firstSlot % opt.gridCols) * opt.cellSize, 0.0f,
            opt.originZ + (float)(firstSlot / opt.gridCols) * opt.cellSize
        };
        float ppu = opt.pixelsPerUnit;
        float eye[3] = {
            opt.eyeX - ((float)opt.fbW * 0.5f) / ppu, 0.0f,
            opt.eyeZ - ((float)opt.fbH * 0.5f) / ppu
        };
        play::CityViewCamera cam =
            play::MakeCityViewCamera(eye, ppu, opt.fbW, opt.fbH);
        float sx = 0.0f, sy = 0.0f;
        bool on = play::ProjectWorldToScreen(cam, world3, &sx, &sy);
        std::printf("[rcr-e2e] pick at screen (%.1f,%.1f) onScreen=%d\n", sx, sy, (int)on);

        play::ScenePickResult pick = rc.Pick(opt, sx, sy, /*pickRadius=*/24.0f);
        std::printf("[rcr-e2e] picked index=%d id=%d dist=%.2f (expected id=%d)\n",
                    pick.index, pick.id, pick.screenDist, sim::g_objects[firstSlot].id);
        CHECK(pick.index >= 0);
        CHECK_EQ(pick.id, sim::g_objects[firstSlot].id);
    }

    sim::ResetEntityArrays();
    io::VfsShutdown();
}

// =============================================================================
// GUARDED: render REAL AUGSBURG TEXTURED (real Textures.BIN) vs untextured, prove
// the textured frame differs, real texels appear, and it is deterministic.
// =============================================================================
TEST(RealCityRenderE2E, RenderRealAugsburgTexturedToBmp) {
    if (!RealAssetsPresent() || !TexturesPresent()) {
        std::printf("  [skip] RealCityRenderE2E.RenderRealAugsburgTexturedToBmp: "
                    "real game dir / Textures.BIN absent (%s)\n", GameDir().c_str());
        CHECK(true);
        return;
    }

    shim::DiskFileSystem fs(GameDir());
    app::RealGameAssets assets = app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI");
    CHECK(assets.vfsBound);

    sim::ResetEntityArrays();
    io::WorldState world{};
    bool loaded = io::LoadWorld("Resources/gamedata/Cities/AUGSBURG.cty", world);
    CHECK(loaded);
    int liveObjects = 0;
    for (int i = 0; i < sim::kObjectCapacity; ++i)
        if (sim::g_objects[i].alive) ++liveObjects;
    CHECK(liveObjects > 0);

    play::RealCityRenderer rc;
    CHECK(rc.Init(&fs, "Resources/Objects.BIN"));
    CHECK(rc.mounted());
    bool texMounted = rc.InitTextures(&fs, "Resources/Textures.BIN");
    std::printf("[rcr-tex-e2e] Textures.BIN mounted=%d bmpCount=%zu\n",
                (int)texMounted, rc.textures().bin().bmpCount());
    CHECK(texMounted);

    play::RealCityRenderer::Options opt;
    opt.fbW = 160; opt.fbH = 120;
    opt.maxObjects = 32;
    opt.pixelsPerUnit = 0.18f;

    // --- UNTEXTURED frame ---
    shim::FileDumpGraphicsDevice devFlat;
    CHECK(devFlat.init(opt.fbW, opt.fbH, 16, false));
    devFlat.configureDump("/tmp", "guild_augsburg_flat",
                          shim::FileDumpGraphicsDevice::kBmp);
    play::RealCityRenderer::Result rFlat = rc.Render(opt, devFlat);
    CHECK(!rFlat.textured);

    // --- TEXTURED frame ---
    opt.textured = true;
    shim::FileDumpGraphicsDevice devTex;
    CHECK(devTex.init(opt.fbW, opt.fbH, 16, false));
    devTex.configureDump("/tmp", "guild_augsburg_textured",
                         shim::FileDumpGraphicsDevice::kBmp);
    play::RealCityRenderer::Result rTex = rc.Render(opt, devTex);

    std::printf("[rcr-tex-e2e] flat: nonClear=%d distinctColors=%d  "
                "tex: textured=%d texturedPolys=%d nonClear=%d distinctColors=%d\n",
                rFlat.nonClearPixels, rFlat.distinctColors,
                (int)rTex.textured, rTex.texturedPolys,
                rTex.nonClearPixels, rTex.distinctColors);
    std::printf("[rcr-tex-e2e] flatBmp=%s  texBmp=%s\n",
                rFlat.bmpPath.c_str(), rTex.bmpPath.c_str());

    // Textured path was used and bound real-texture polys.
    CHECK(rTex.textured);
    CHECK(rTex.texturedPolys > 0);
    CHECK(rTex.nonClearPixels > 100);
    // Real texels -> MANY more distinct colours than the flat-shade frame.
    CHECK(rTex.distinctColors > rFlat.distinctColors);
    CHECK(rTex.distinctColors > 8);
    CHECK(!rTex.bmpPath.empty());

    // DETERMINISM: a second textured render yields the SAME counts.
    shim::FileDumpGraphicsDevice devTex2;
    CHECK(devTex2.init(opt.fbW, opt.fbH, 16, false));
    devTex2.configureDump("/tmp", "guild_augsburg_textured2",
                          shim::FileDumpGraphicsDevice::kBmp);
    play::RealCityRenderer::Result rTex2 = rc.Render(opt, devTex2);
    CHECK_EQ(rTex2.nonClearPixels, rTex.nonClearPixels);
    CHECK_EQ(rTex2.distinctColors, rTex.distinctColors);
    CHECK_EQ(rTex2.texturedPolys, rTex.texturedPolys);

    sim::ResetEntityArrays();
    io::VfsShutdown();
}

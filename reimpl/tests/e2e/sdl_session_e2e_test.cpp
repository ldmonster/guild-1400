#include "test.h"

// GUARDED e2e for play::RunSdlSession — a full scripted NATIVE-PLAY session over the
// REAL AUGSBURG city, backend-agnostic (MemoryGraphicsDevice + ScriptedPlatform so it
// runs in the portable suite; the SAME loop drives the real Vulkan+SDL window in
// guild_run --play). The scripted session: render+present several frames -> a
// left-click that picks a real object and issues an order -> SPACE advances a real
// game-day -> the window closes (quitAfterPumps). Asserts real frames presented, the
// action mutated the world (hashEnd != hashStart), a clean window quit, and full
// determinism across reruns. Clean skip without assets.
#include "play/sdl_session.h"
#include "play/real_city_render.h"
#include "play/scene_pick.h"
#include "shim_impl/disk_filesystem.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/scripted_platform.h"
#include "app/real_boot.h"
#include "io/save_world_load.h"
#include "io/vfs.h"
#include "sim/entity.h"

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

// Screen point the first live object projects to under the renderer's Pick camera.
bool FirstObjectScreen(const play::RealCityRenderer::Options& opt,
                       float* outSX, float* outSY, i32* outId) {
    shim::DiskFileSystem fs(GameDir());
    app::RealGameAssets assets = app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI");
    if (!assets.vfsBound) { io::VfsShutdown(); return false; }
    sim::ResetEntityArrays();
    io::WorldState world{};
    if (!io::LoadWorld("Resources/gamedata/Cities/AUGSBURG.cty", world)) {
        io::VfsShutdown(); return false;
    }
    int slot = -1;
    for (int i = 0; i < sim::kObjectCapacity; ++i)
        if (sim::g_objects[i].alive) { slot = i; break; }
    if (slot < 0) { sim::ResetEntityArrays(); io::VfsShutdown(); return false; }
    if (outId) *outId = sim::g_objects[slot].id;
    int cols = opt.gridCols > 0 ? opt.gridCols : 8;
    float world3[3] = {
        opt.originX + (float)(slot % cols) * opt.cellSize, 0.0f,
        opt.originZ + (float)(slot / cols) * opt.cellSize
    };
    float ppu = opt.pixelsPerUnit > 0.0f ? opt.pixelsPerUnit : 1.0f;
    float eye[3] = {
        opt.eyeX - ((float)opt.fbW * 0.5f) / ppu, 0.0f,
        opt.eyeZ - ((float)opt.fbH * 0.5f) / ppu
    };
    play::CityViewCamera cam = play::MakeCityViewCamera(eye, ppu, opt.fbW, opt.fbH);
    bool on = play::ProjectWorldToScreen(cam, world3, outSX, outSY);
    sim::ResetEntityArrays();
    io::VfsShutdown();
    return on;
}

} // namespace

TEST(SdlSessionE2E, RealAugsburgScriptedPlaySession) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] SdlSessionE2E.RealAugsburgScriptedPlaySession: "
                    "real game dir absent (%s)\n", GameDir().c_str());
        CHECK(true);
        return;
    }

    play::SdlSessionConfig cfg;
    cfg.gameDir = GameDir();
    cfg.cityPath = "Resources/gamedata/Cities/AUGSBURG.cty";
    cfg.fbW = 320; cfg.fbH = 240;     // a bigger real-city frame for the e2e
    cfg.textured = true;              // sample the real Textures.BIN if present
    cfg.frameCapMs = 0;
    cfg.clickCursorMode = 6;          // kConquer (WARE)

    play::RealCityRenderer::Options opt;
    opt.fbW = cfg.fbW; opt.fbH = cfg.fbH;
    float sx = 0.0f, sy = 0.0f; i32 wantId = 0;
    bool projected = FirstObjectScreen(opt, &sx, &sy, &wantId);
    CHECK(projected);
    std::printf("[sdl-e2e] AUGSBURG first object id=%d click@(%.1f,%.1f)\n",
                wantId, sx, sy);

    // The scripted session: a click on the real object + SPACE held (one day), and
    // the window closes after 6 pumps -> a clean window quit.
    auto run = [&]() {
        shim::DiskFileSystem fs(GameDir());
        shim::MemoryGraphicsDevice dev;
        dev.init(cfg.fbW, cfg.fbH, 16, false);
        shim::ScriptedPlatform plat;
        plat.setMouse((int)sx, (int)sy, /*left=*/true);  // one click edge
        plat.pressKey(0x20);                              // SPACE -> one game-day
        plat.quitAfterPumps(6);                           // window closes after 6 frames
        play::SdlSessionTrace tr = play::RunSdlSession(fs, dev, plat, cfg);
        sim::ResetEntityArrays();
        io::VfsShutdown();
        return tr;
    };

    play::SdlSessionTrace tr = run();
    std::printf("[sdl-e2e] mounted=%d loaded=%d liveObjects=%d persons=%d\n",
                (int)tr.mounted, (int)tr.loaded, tr.liveObjects, tr.persons);
    std::printf("[sdl-e2e] framesPresented=%d clicks=%d picksHit=%d orders=%d "
                "days=%d lastPicked=%d\n",
                tr.framesPresented, tr.clicksHandled, tr.picksHit, tr.ordersIssued,
                tr.daysAdvanced, tr.lastPickedId);
    std::printf("[sdl-e2e] quitByWindow=%d cleanQuit=%d hashStart=0x%llx "
                "hashEnd=0x%llx changed=%d\n",
                (int)tr.quitByWindow, (int)tr.cleanQuit,
                (unsigned long long)tr.hashStart, (unsigned long long)tr.hashEnd,
                (int)(tr.hashStart != tr.hashEnd));

    // Real city loaded + rendered + presented.
    CHECK(tr.mounted);
    CHECK(tr.loaded);
    CHECK(tr.liveObjects > 0);
    CHECK(tr.framesPresented > 0);    // real frames presented through the device
    // present() happens before pumpMessages(): quitAfterPumps(6) gives 6 true pumps
    // then false on the 7th, so frame 0..6 each present -> 7 presents, then quit.
    CHECK_EQ(tr.framesPresented, 7);

    // The scripted action exercised the real interaction layer.
    CHECK_EQ(tr.clicksHandled, 1);
    CHECK_EQ(tr.picksHit, 1);
    CHECK_EQ(tr.lastPickedId, wantId);
    CHECK(tr.ordersIssued >= 1);
    CHECK_EQ(tr.daysAdvanced, 1);

    // The window closed cleanly, and the click+day mutated the world.
    CHECK(tr.quitByWindow);
    CHECK(tr.cleanQuit);
    CHECK(tr.hashStart != tr.hashEnd);

    // DETERMINISM: the whole scripted session is byte-reproducible.
    play::SdlSessionTrace tr2 = run();
    CHECK_EQ(tr2.framesPresented, tr.framesPresented);
    CHECK_EQ(tr2.clicksHandled, tr.clicksHandled);
    CHECK_EQ(tr2.picksHit, tr.picksHit);
    CHECK_EQ(tr2.ordersIssued, tr.ordersIssued);
    CHECK_EQ(tr2.daysAdvanced, tr.daysAdvanced);
    CHECK_EQ(tr2.lastPickedId, tr.lastPickedId);
    CHECK_EQ(tr2.hashStart, tr.hashStart);
    CHECK_EQ(tr2.hashEnd, tr.hashEnd);
}

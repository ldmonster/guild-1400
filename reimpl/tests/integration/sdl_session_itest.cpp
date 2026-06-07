#include "test.h"

// INTEGRATION tier for play::RunSdlSession — drive the REAL session over the real
// loaded city (backend-agnostic: MemoryGraphicsDevice + ScriptedPlatform), scripting
// a LEFT-CLICK on a KNOWN object's projected screen point so the click resolves to a
// live object, issues the cfg.clickCursorMode order through the REAL CommandQueue, and
// MUTATES the world (HashFullWorld changes). Then prove the scripted session is
// deterministic across reruns. GUARDED on the real game dir (clean skip if absent).
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

// Compute the screen point the FIRST live object projects to under the renderer's
// Pick camera (same math RealCityRenderer::Pick uses), so a scripted click there
// resolves to that object. Loads the city to find the first live slot, then leaves
// the arrays reset (the session reloads deterministically).
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

TEST(SdlSessionItest, ScriptedClickIssuesOrderAndMutates) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] SdlSessionItest.ScriptedClickIssuesOrderAndMutates: "
                    "real game dir absent\n");
        CHECK(true);
        return;
    }

    play::SdlSessionConfig cfg;
    cfg.gameDir = GameDir();
    cfg.cityPath = "Resources/gamedata/Cities/AUGSBURG.cty";
    cfg.fbW = 160; cfg.fbH = 120;
    cfg.textured = false;
    cfg.frameCapMs = 0;
    cfg.maxFrames = 4;
    cfg.clickCursorMode = 6;          // kConquer (WARE) — the record-mutating order

    // The renderer Options the session derives from cfg (defaults for grid/eye/ppu).
    play::RealCityRenderer::Options opt;
    opt.fbW = cfg.fbW; opt.fbH = cfg.fbH;

    float sx = 0.0f, sy = 0.0f; i32 wantId = 0;
    bool projected = FirstObjectScreen(opt, &sx, &sy, &wantId);
    std::printf("[sdl-itest] first object id=%d projects to (%.1f,%.1f) onScreen=%d\n",
                wantId, sx, sy, (int)projected);
    CHECK(projected);

    auto run = [&](float clickX, float clickY) {
        shim::DiskFileSystem fs(GameDir());
        shim::MemoryGraphicsDevice dev;
        dev.init(cfg.fbW, cfg.fbH, 16, false);
        shim::ScriptedPlatform plat;
        plat.setMouse((int)clickX, (int)clickY, /*left=*/true);  // one click edge
        play::SdlSessionTrace tr = play::RunSdlSession(fs, dev, plat, cfg);
        sim::ResetEntityArrays();
        io::VfsShutdown();
        return tr;
    };

    play::SdlSessionTrace tr = run(sx, sy);
    std::printf("[sdl-itest] frames=%d clicks=%d picksHit=%d orders=%d lastPicked=%d "
                "hashStart=0x%llx hashEnd=0x%llx\n",
                tr.framesPresented, tr.clicksHandled, tr.picksHit, tr.ordersIssued,
                tr.lastPickedId, (unsigned long long)tr.hashStart,
                (unsigned long long)tr.hashEnd);

    CHECK_EQ(tr.framesPresented, 4);
    CHECK_EQ(tr.clicksHandled, 1);    // exactly one left-click edge
    CHECK_EQ(tr.picksHit, 1);         // it resolved to a live object
    CHECK_EQ(tr.lastPickedId, wantId);
    CHECK(tr.ordersIssued >= 1);      // an order went through the real CommandQueue
    CHECK(tr.hashStart != tr.hashEnd); // the order mutated the live world

    // DETERMINISM: the same scripted click yields the identical trace + hashes.
    play::SdlSessionTrace tr2 = run(sx, sy);
    CHECK_EQ(tr2.framesPresented, tr.framesPresented);
    CHECK_EQ(tr2.picksHit, tr.picksHit);
    CHECK_EQ(tr2.ordersIssued, tr.ordersIssued);
    CHECK_EQ(tr2.lastPickedId, tr.lastPickedId);
    CHECK_EQ(tr2.hashStart, tr.hashStart);
    CHECK_EQ(tr2.hashEnd, tr.hashEnd);

    // A click on EMPTY space (far corner) hits nothing -> no order, no mutation.
    play::SdlSessionTrace trMiss = run((float)(cfg.fbW - 1), (float)(cfg.fbH - 1));
    std::printf("[sdl-itest] empty-click: clicks=%d picksHit=%d orders=%d changed=%d\n",
                trMiss.clicksHandled, trMiss.picksHit, trMiss.ordersIssued,
                (int)(trMiss.hashStart != trMiss.hashEnd));
    CHECK_EQ(trMiss.clicksHandled, 1);
    CHECK_EQ(trMiss.picksHit, 0);
    CHECK_EQ(trMiss.ordersIssued, 0);
    CHECK_EQ(trMiss.hashStart, trMiss.hashEnd);
}

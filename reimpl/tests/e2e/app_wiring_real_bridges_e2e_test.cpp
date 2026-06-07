#include "test.h"

// E2E: the APP SPINE's LIVE frame with the REAL render bridges DE-INERTED
// (PLAYABLE_PLAN P6). RealSubsystems::EnableRealRenderBridges() points the live
// renderMainViewFrame's INERT terrain/scene-walk/HUD leaves at their REAL
// reconstructed code, so a frame driven through the app spine (RunFrameLoop with
// kRenderWorld) now draws:
//   * the REAL floor leaf      (play::RenderTerrain) composited into the live fb,
//   * a REAL multi-tri mesh     dispatched via render::ProcessSceneNodeAppend
//                               (the real scene-graph node dispatch),
//   * a REAL HUD sprite         via render::ShapeShowFromBank.
// Asserts the live framebuffer gains content the inert frame lacked, and stays
// deterministic across two engines.
#include "app/wiring.h"
#include "app/gamelogic.h"
#include "config/ini.h"
#include "shim_impl/loopback_socket.h"
#include "shim_impl/mem_filesystem.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/null_audio.h"
#include "shim_impl/null_platform.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;
using guild::app::RealSubsystems;

namespace {

// Count non-clear pixels in the live 16bpp framebuffer (clear == 0x0040 packed).
int LiveNonClear(const RealSubsystems& sub) {
    int w = 0, h = 0, pitch = 0;
    const void* px = sub.frameBufferPixels(&w, &h, &pitch);
    if (!px || w <= 0 || h <= 0) return -1;
    const u16* p = reinterpret_cast<const u16*>(px);
    int widthPx = pitch / 2;
    int n = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (p[y * widthPx + x] != 0) ++n;   // any non-zero pixel (incl. clear blue)
    return n;
}

std::vector<std::uint8_t> LiveFb(const RealSubsystems& sub) {
    int w = 0, h = 0, pitch = 0;
    const void* px = sub.frameBufferPixels(&w, &h, &pitch);
    std::vector<std::uint8_t> out;
    if (!px) return out;
    out.assign((const std::uint8_t*)px, (const std::uint8_t*)px + (size_t)pitch * h);
    return out;
}

} // namespace

// --- the live frame de-inerts to real terrain + mesh + HUD content -------------
TEST(AppWiringRealBridgesE2E, LiveFrameGainsRealContent) {
    shim::NullPlatform plat;
    shim::MemoryGraphicsDevice gfx;
    shim::NullAudioDevice audio;
    shim::MemFileSystem fs;
    auto sockPair = shim::LoopbackSocket::makePair();
    config::IniFile ini;

    RealSubsystems sub(&plat, &gfx, &audio, &fs, sockPair.first.get(), &ini);
    app::GameApp appObj(plat, gfx, audio, sub);

    CHECK(appObj.CreateMainWindow(1));
    CHECK(appObj.InitSubsystemsAndMovieDll());
    CHECK(appObj.InitDisplayAndPaths(1));
    CHECK(appObj.InitEngineAndScriptCommands());

    // Drive one INERT world frame first (bridges OFF) -> the legacy fake-quad frame.
    const std::uint32_t mask = app::mask::kRenderWorld | app::mask::kHeadlessSuppress;
    CHECK(!sub.realRenderBridges());
    appObj.RunFrameLoop(mask);
    int inertNonClear = LiveNonClear(sub);
    CHECK_EQ(sub.liveTerrainTris(), 0);
    CHECK_EQ(sub.liveSceneDispatched(), 0);
    CHECK_EQ(sub.liveMeshTris(), 0);
    CHECK_EQ(sub.liveHudSprites(), 0);

    // De-inert the REAL render bridges and drive another world frame.
    sub.EnableRealRenderBridges();
    CHECK(sub.realRenderBridges());
    appObj.RunFrameLoop(mask);

    // The live frame now drew REAL terrain + a REAL mesh + a REAL HUD sprite.
    CHECK(sub.liveTerrainTris() > 0);
    CHECK(sub.liveTerrainPixels() > 0);
    CHECK(sub.liveSceneDispatched() > 0);   // ProcessSceneNodeAppend dispatched a node
    CHECK(sub.liveMeshTris() > 2);          // real multi-tri mesh (not a 2-tri quad)
    CHECK_EQ(sub.liveHudSprites(), 1);      // ShapeShowFromBank drew the HUD sprite

    // The bridged frame painted MORE of the live framebuffer than the inert frame.
    int realNonClear = LiveNonClear(sub);
    CHECK(realNonClear > 0);
    CHECK(realNonClear > inertNonClear);

    appObj.Shutdown();
}

// --- the bridged live frame is deterministic across two engines ----------------
TEST(AppWiringRealBridgesE2E, LiveBridgedFrameDeterministic) {
    auto runOne = [](std::vector<std::uint8_t>& fbOut, int& meshTris, int& terrTris) {
        shim::NullPlatform plat;
        shim::MemoryGraphicsDevice gfx;
        shim::NullAudioDevice audio;
        shim::MemFileSystem fs;
        auto sockPair = shim::LoopbackSocket::makePair();
        config::IniFile ini;
        RealSubsystems sub(&plat, &gfx, &audio, &fs, sockPair.first.get(), &ini);
        app::GameApp appObj(plat, gfx, audio, sub);
        CHECK(appObj.CreateMainWindow(1));
        CHECK(appObj.InitSubsystemsAndMovieDll());
        CHECK(appObj.InitDisplayAndPaths(1));
        CHECK(appObj.InitEngineAndScriptCommands());
        sub.EnableRealRenderBridges();
        const std::uint32_t mask =
            app::mask::kRenderWorld | app::mask::kHeadlessSuppress;
        appObj.RunFrameLoop(mask);
        fbOut = LiveFb(sub);
        meshTris = sub.liveMeshTris();
        terrTris = sub.liveTerrainTris();
        appObj.Shutdown();
    };

    std::vector<std::uint8_t> fa, fb;
    int ma = 0, mb = 0, ta = 0, tb = 0;
    runOne(fa, ma, ta);
    runOne(fb, mb, tb);

    CHECK(!fa.empty());
    CHECK(fa.size() == fb.size());
    CHECK(fa == fb);            // byte-identical live framebuffers
    CHECK_EQ(ma, mb);
    CHECK_EQ(ta, tb);
    CHECK(ma > 2);
    CHECK(ta > 0);
}

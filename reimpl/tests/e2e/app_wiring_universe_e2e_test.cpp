// End-to-end: a full init -> frames -> 13-step shutdown lifecycle that exercises
// the UNIVERSE (scene-slot) hooks wired STUB->REAL in this pass:
//   universeCreateDefaultCameras -> sim::UniverseCreateDefaultCameras @0x5b5f48
//   tdUniverseSwitchActiveSlot0  -> sim::UniverseSwitchActiveSlot(0, quiet) @0x5b4a24
// Builds GameApp on RealSubsystems + headless shims, runs the documented init
// steps, drives a batch of frames under the live mask, then runs the 13-step
// shutdown and asserts both newly-wired hooks reached real code and the universe
// module's observable state (MegaCam handle, active-slot id) is correct.
#include "app/wiring.h"
#include "config/ini.h"
#include "sim/universe.h"
#include "shim_impl/loopback_socket.h"
#include "shim_impl/mem_filesystem.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/null_audio.h"
#include "shim_impl/null_platform.h"
#include "test.h"

using namespace guild;
using guild::app::RealSubsystems;

TEST(AppWiringUniverseE2E, FullLifecycleExercisesUniverseHooks) {
    shim::NullPlatform plat;
    shim::MemoryGraphicsDevice gfx;
    shim::NullAudioDevice audio;
    shim::MemFileSystem fs;
    auto sockPair = shim::LoopbackSocket::makePair();
    config::IniFile ini;

    RealSubsystems sub(&plat, &gfx, &audio, &fs, sockPair.first.get(), &ini);
    app::GameApp appObj(plat, gfx, audio, sub);

    // ---- init lifecycle ----------------------------------------------------
    CHECK(appObj.CreateMainWindow(1));
    CHECK(appObj.InitSubsystemsAndMovieDll());
    CHECK(appObj.InitDisplayAndPaths(1));        // -> universeCreateDefaultCameras
    CHECK(appObj.InitEngineAndScriptCommands());

    // The camera-bootstrap init hook reached the REAL universe module.
    CHECK(sub.firedReal("universeCreateDefaultCameras"));
    unsigned mega = sub.universeMegaCam();
    CHECK(mega != 0u);
    if (mega != 0u)
        CHECK_EQ(mega, sim::g_megaCam);

    // ---- frames ------------------------------------------------------------
    const std::uint32_t live =
        app::mask::kWidgetMouse | app::mask::kHudMouse |
        app::mask::kGameObjects | app::mask::kTooltips |
        app::mask::kRenderWorld | app::mask::kScripts |
        app::mask::kNetworkCommand | app::mask::kDayCycleMusic;
    for (int i = 0; i < 12; ++i)
        appObj.RunFrameLoop(live);
    CHECK_EQ(sub.frameCount(), 12);

    // ---- 13-step shutdown --------------------------------------------------
    appObj.Shutdown();                            // -> tdUniverseSwitchActiveSlot0
    CHECK(sub.firedReal("tdUniverseSwitchActiveSlot0"));
    CHECK(sub.universeSwitchedToSlot0());          // real swap returned true
    CHECK_EQ(sim::g_activeUniverseId, 0);          // module global settled at slot 0
}

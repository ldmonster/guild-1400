// End-to-end: a fuller init -> ~20 frame -> shutdown lifecycle that exercises
// the SECOND wave of wired hooks. Builds GameApp on RealSubsystems + headless
// shims, runs the documented init steps, then drives 20 frames under a mask that
// enables the newly-wired per-frame steps (widget mouse, HUD mouse, gameobjects,
// tooltips) alongside the existing live steps, and asserts the real calls fired
// and state advanced; finally runs the 13-step shutdown cleanly.
#include "app/wiring.h"
#include "config/ini.h"
#include "shim_impl/loopback_socket.h"
#include "shim_impl/mem_filesystem.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/null_audio.h"
#include "shim_impl/null_platform.h"
#include "tests/framework/test.h"

using namespace guild;
using guild::app::RealSubsystems;

TEST(AppWiring2E2E, FullLifecycleExercisesNewlyWiredHooks) {
    shim::NullPlatform plat;
    shim::MemoryGraphicsDevice gfx;
    shim::NullAudioDevice audio;
    shim::MemFileSystem fs;
    auto sockPair = shim::LoopbackSocket::makePair();
    config::IniFile ini;

    RealSubsystems sub(&plat, &gfx, &audio, &fs, sockPair.first.get(), &ini);
    app::GameApp appObj(plat, gfx, audio, sub);

    // ---- init lifecycle (drives the wired init hooks) ----------------------
    CHECK(appObj.CreateMainWindow(1));
    CHECK(appObj.InitSubsystemsAndMovieDll());   // file dir + timebase timer
    CHECK(appObj.InitDisplayAndPaths(1));        // engine init + input + gfx load
    CHECK(appObj.InitEngineAndScriptCommands()); // text def + script commands

    // The init hooks reached real reconstructed code.
    CHECK(sub.firedReal("fileCreateDirectory"));
    CHECK(sub.firedReal("timeBaseStartTimer"));
    CHECK(sub.firedReal("renderInitEngineDevice"));
    CHECK(sub.firedReal("inputDirectInputInit"));
    CHECK(sub.firedReal("guiLoadGfxFile"));
    CHECK(sub.firedReal("textLoadDefinitionFile"));
    CHECK(sub.firedReal("scriptRegisterCommands"));
    CHECK(sub.timerRunning());
    CHECK_EQ(sub.shapeBankCount(), 1);
    CHECK_EQ(sub.scriptCmdResult(), 6);
    CHECK(sub.textDbCount() >= 4);

    // ---- 20 frames under a mask enabling the new per-frame steps -----------
    const std::uint32_t live =
        app::mask::kWidgetMouse | app::mask::kHudMouse |
        app::mask::kGameObjects | app::mask::kTooltips |
        app::mask::kRenderWorld | app::mask::kScripts |
        app::mask::kNetworkCommand | app::mask::kDayCycleMusic;

    for (int i = 0; i < 20; ++i)
        appObj.RunFrameLoop(live);

    CHECK_EQ(appObj.lastFeatureMask(), live);
    CHECK_EQ(sub.frameCount(), 20);
    // Newly-wired per-frame steps reached real code without crashing.
    CHECK(sub.firedReal("widgetDispatchMouseClick"));
    CHECK(sub.firedReal("hudHandleMouseClick"));
    CHECK(sub.firedReal("gameObjectDispatchInteractions"));
    CHECK(sub.firedReal("tooltipDispatch"));
    // 20 GameObjects-block frames advanced the fade past its 30-tick duration?
    // Not yet (only 20 < 30), but the present block ran each frame.
    CHECK_EQ(sub.presentCount(), 20 + 1); // +1 from renderInitEngineDevice

    // ---- 13-step shutdown (drives the wired teardown hooks) ----------------
    appObj.Shutdown();
    CHECK(sub.firedReal("tdGameShutdownSubsystems"));
    CHECK(sub.firedReal("tdGameStateFreeAllResources"));
    CHECK(sub.firedReal("tdTimeBaseStopTimer"));
    CHECK(!sub.timerRunning());
}

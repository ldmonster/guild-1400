// Unit tests for the app-spine wiring (guild::app::RealSubsystems).
// Verifies that RealSubsystems forwards the [real]-marked hooks to actual
// reconstructed module entry points (observed via recorded events + module
// state), that the frame loop runs against a representative mask without
// crashing, and that shutdown runs the teardown hooks cleanly.
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

namespace {

// Bundle of headless shims + a RealSubsystems wired to them, for test reuse.
struct Fixture {
    shim::NullPlatform plat;
    shim::MemoryGraphicsDevice gfx;
    shim::NullAudioDevice audio;
    shim::MemFileSystem fs;
    std::pair<std::unique_ptr<shim::LoopbackSocket>, std::unique_ptr<shim::LoopbackSocket>> sockPair
        = shim::LoopbackSocket::makePair();
    config::IniFile ini;
    RealSubsystems sub{&plat, &gfx, &audio, &fs, sockPair.first.get(), &ini};
};

} // namespace

// ---- init wiring -----------------------------------------------------------

TEST(AppWiring, InitWiresRealEntryPoints) {
    Fixture f;
    f.sub.errorLogInit();
    f.sub.memoryInitTracker(32678);
    f.sub.memPoolStartupStack(0x80);
    f.sub.vfsInit("\\project\\gfx\\");

    // These hooks must reach real reconstructed code (not stub).
    CHECK(f.sub.firedReal("errorLogInit"));
    CHECK(f.sub.firedReal("memoryInitTracker"));
    CHECK(f.sub.firedReal("memPoolStartupStack"));
    CHECK(f.sub.firedReal("vfsInit"));

    // Observable real-module state changes.
    CHECK(f.sub.memoryTrackerInited());
    CHECK(f.sub.vfsInited());
}

TEST(AppWiring, ConfigReadIsReal) {
    Fixture f;
    f.sub.configReadGfxAndSound();
    CHECK(f.sub.firedReal("configReadGfxAndSound"));
    // Defaults from the empty INI: character_detail default is 1.
    CHECK_EQ(f.sub.gfx().characterDetail, (u8)1);
    CHECK_EQ(f.sub.game().stadt, std::string("Augsburg"));
}

TEST(AppWiring, EngineInitWiresNetCommandWorldSoundReal) {
    Fixture f;
    f.sub.configReadGfxAndSound();
    f.sub.netConnectToServer("", 0);
    f.sub.commandQueueInitAndSync();
    f.sub.worldLoadBuildingAndObjectData("\\project\\game\\data\\");
    f.sub.audioStartupMilesDriver();
    bool ok = f.sub.soundLibInit(48, 2, 44100);
    f.sub.audioApplyVolumeSettings();

    CHECK(f.sub.firedReal("netConnectToServer"));
    CHECK(f.sub.firedReal("commandQueueInitAndSync"));
    CHECK(f.sub.firedReal("worldLoadBuildingAndObjectData"));
    CHECK(f.sub.firedReal("soundLibInit"));
    CHECK(f.sub.firedReal("audioApplyVolumeSettings"));
    CHECK(ok);
    CHECK(f.sub.soundInited());
    // The command queue Init set standalone mode (single-player).
    CHECK(f.sub.commandQueue().standalone());
}

TEST(AppWiring, NullDeviceDegradesToStub) {
    // No gfx/net/fs/audio injected -> the dependent hooks become recorded stubs,
    // but still must not crash.
    shim::NullPlatform plat;
    config::IniFile ini;
    RealSubsystems sub(&plat, nullptr, nullptr, nullptr, nullptr, &ini);
    sub.vfsInit("x");
    sub.netConnectToServer("", 0);
    sub.soundLibInit(48, 2, 44100);
    sub.presentFrame();
    CHECK(sub.fired("vfsInit"));
    CHECK(!sub.firedReal("vfsInit"));
    CHECK(!sub.firedReal("soundLibInit"));
    CHECK(!sub.firedReal("presentFrame"));
}

// ---- frame loop ------------------------------------------------------------

TEST(AppWiring, FrameLoopRunsRepresentativeMask) {
    Fixture f;
    app::GameApp appObj(f.plat, f.gfx, f.audio, f.sub);

    // A live single-player frame mask (render + present + script + net + day).
    const std::uint32_t live =
        app::mask::kInputCommandPoll | app::mask::kRenderWorld |
        app::mask::kGameObjects | app::mask::kScripts |
        app::mask::kNetworkCommand | app::mask::kDayCycleMusic;

    for (int i = 0; i < 5; ++i)
        appObj.RunFrameLoop(live);

    CHECK_EQ(appObj.lastFeatureMask(), live);
    CHECK_EQ(f.sub.frameCount(), 5);          // inputLatchAndPump ran each frame
    CHECK(f.sub.firedReal("inputLatchAndPump"));
    CHECK(f.sub.firedReal("renderMainViewFrame"));
    CHECK(f.sub.firedReal("presentFrame"));
    CHECK(f.sub.firedReal("scriptStepAllActive"));
    CHECK(f.sub.firedReal("commandNetworkPump"));
    CHECK(f.sub.firedReal("dayCycleAndOutdoorMusic"));
    CHECK_EQ(f.sub.presentCount(), 5);
    // Brightness was computed from the advancing wall clock (0..600).
    CHECK(f.sub.brightness() >= 0 && f.sub.brightness() <= 600);
}

TEST(AppWiring, HeadlessSuppressSkipsWorldButPumps) {
    Fixture f;
    app::GameApp appObj(f.plat, f.gfx, f.audio, f.sub);
    int r = appObj.RunFrameLoop(app::mask::kHeadlessSuppress | app::mask::kRenderWorld);
    CHECK_EQ(r, 0);                       // headless frame returns 0
    CHECK(f.sub.firedReal("inputLatchAndPump"));
    // renderMainViewFrame is gated by kRenderWorld which is set, so it runs, but
    // the day-cycle sub-block inside it is suppressed by headless.
    CHECK(f.sub.firedReal("renderMainViewFrame"));
}

// ---- shutdown --------------------------------------------------------------

TEST(AppWiring, ShutdownRunsTeardownHooksClean) {
    Fixture f;
    app::GameApp appObj(f.plat, f.gfx, f.audio, f.sub);

    // Bring real subsystems up first so their teardown is "real".
    f.sub.memoryInitTracker(1024);
    f.sub.vfsInit("x");
    f.sub.memPoolStartupStack(0x80);

    appObj.Shutdown();

    CHECK(f.sub.firedReal("tdWidgetShutdownSystem"));
    CHECK(f.sub.firedReal("tdRenderShutdownEngine"));
    CHECK(f.sub.firedReal("tdVfsShutdown"));
    CHECK(f.sub.firedReal("tdMemPoolShutdownStack"));
    CHECK(f.sub.firedReal("tdMemoryShutdownTracker"));
    CHECK(f.sub.firedReal("tdErrorLogShutdown"));
    // Tracker/VFS are back down.
    CHECK(!f.sub.memoryTrackerInited());
    CHECK(!f.sub.vfsInited());
}

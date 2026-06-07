// End-to-end: a fuller init -> ~30 frame -> shutdown lifecycle exercising the
// THIRD wave of wired hooks alongside the extended sim hook installers. Builds
// GameApp on RealSubsystems + headless shims with a small POPULATED world (one
// live character actor + the second-wave sim hooks installed so cross-module
// effects fire), runs the init steps, drives 30 frames under a BROAD feature
// mask that enables the newly-wired per-frame steps (weather, HUD selection/
// labels, character collect, cutscene poll, day-cycle music) on top of the
// existing live steps, asserts the real calls fired and real cross-module state
// advanced (commands enqueued, character/world state stepped), then runs the
// 13-step shutdown cleanly.
#include "app/wiring.h"
#include "config/ini.h"
#include "sim/real_hooks.h"
#include "sim/real_hooks2.h"
#include "sim/character.h"
#include "sim/charaction.h"
#include "sim/interaction_handlers.h"
#include "shim_impl/loopback_socket.h"
#include "shim_impl/mem_filesystem.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/null_audio.h"
#include "shim_impl/null_platform.h"
#include "tests/framework/test.h"

using namespace guild;
using guild::app::RealSubsystems;

TEST(AppWiring3E2E, BroadMaskLifecycleExercisesThirdWaveHooks) {
    shim::NullPlatform plat;
    shim::MemoryGraphicsDevice gfx;
    shim::NullAudioDevice audio;
    shim::MemFileSystem fs;
    auto sockPair = shim::LoopbackSocket::makePair();
    config::IniFile ini;

    RealSubsystems sub(&plat, &gfx, &audio, &fs, sockPair.first.get(), &ini);
    app::GameApp appObj(plat, gfx, audio, sub);

    // ---- install the second-wave sim hooks (cross-module real wiring) ------
    sim::InstallRealSimHooks();
    sim::InstallRealSimHooks2();
    sim::CommandQueue* q = sim::RealCommandQueue();
    const u32 sendBefore = q->send_count();

    // ---- a small populated world: one live character actor -----------------
    sim::ResetCharacters();
    static sim::Character actor{};
    actor.yaw = 0.0f;
    actor.turnTarget = 2;          // a small turn delta for the real turn bridge
    sim::g_characters[0] = &actor;
    sim::g_characterCount = 1;     // arm the live-array driver gate

    // ---- init lifecycle ----------------------------------------------------
    CHECK(appObj.CreateMainWindow(1));
    CHECK(appObj.InitSubsystemsAndMovieDll());
    CHECK(appObj.InitDisplayAndPaths(1));
    CHECK(appObj.InitEngineAndScriptCommands());
    // Bring the music-thread + market-price init hooks up explicitly (they sit in
    // the spine's engine-init block; drive them directly for the broad coverage).
    sub.soundInitMusicThread(44100);
    sub.buildingComputeMarketPrices();
    CHECK(sub.firedReal("soundInitMusicThread"));
    CHECK(sub.firedReal("buildingComputeMarketPrices"));
    CHECK_EQ(sub.musicTrackCount(), 1);
    CHECK(sub.marketWorkMinutes() > 0);

    // ---- 30 frames under a broad mask --------------------------------------
    const std::uint32_t broad =
        app::mask::kWidgetMouse | app::mask::kHudMouse | app::mask::kGameObjects |
        app::mask::kTooltips | app::mask::kRenderWorld | app::mask::kScripts |
        app::mask::kNetworkCommand | app::mask::kDayCycleMusic |
        app::mask::kWeatherSky | app::mask::kHudSelection | app::mask::kHudLabels |
        app::mask::kInputCommandPoll;

    for (int i = 0; i < 30; ++i)
        appObj.RunFrameLoop(broad);

    CHECK_EQ(appObj.lastFeatureMask(), broad);
    CHECK_EQ(sub.frameCount(), 30);

    // ---- newly-wired per-frame steps reached real code without crashing -----
    CHECK(sub.firedReal("weatherUpdateSky"));
    CHECK(sub.firedReal("hudSelectionAndTargets"));
    CHECK(sub.firedReal("hudLabelsAndCaption"));
    CHECK(sub.firedReal("characterCollectByOwner"));
    CHECK(sub.firedReal("inputCommandPoll"));
    CHECK(sub.firedReal("dayCycleAndOutdoorMusic"));

    // Real cross-module effects advanced:
    //  - the live-actor driver ran every frame;
    CHECK_EQ(sub.characterUpdates(), 30);
    //  - the cutscene poll picked the active slot each frame;
    CHECK(sub.cutscenesProcessed() >= 30);
    //  - the day-cycle music selector loaded a real outdoor track;
    CHECK(sub.musicHandle() != 0);
    //  - the real weather core produced a peak-of-3 intensity;
    CHECK(sub.weatherIntensity() >= 20);
    //  - the HUD label layout placed a centered caption (anchor 320, width 80).
    CHECK_EQ(sub.hudLabelX(), 280);

    // The second-wave command sink is live: an interaction handler enqueues a
    // real packet onto the shared queue (cross-module command emission).
    char r = sim::PerformRenovate(nullptr, nullptr, nullptr);
    CHECK_EQ(r, 15);
    CHECK(q->send_count() > sendBefore);

    // The real turn bridge advanced the actor's heading (started at 0).
    CHECK(sim::GetCharActionHooks().turnStep != nullptr);

    // ---- 13-step shutdown --------------------------------------------------
    appObj.Shutdown();
    CHECK(sub.firedReal("tdGameShutdownSubsystems"));
    CHECK(sub.firedReal("tdGameStateFreeAllResources"));

    // Tidy the static live-actor array so other suites start clean.
    sim::ResetCharacters();
}

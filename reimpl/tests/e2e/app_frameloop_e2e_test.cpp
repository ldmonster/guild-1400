// E2E: full init -> frames -> shutdown lifecycle through the app spine, exercising
// the SIM-LEAF wiring this pass added (commandQueueInitAndSync now installs the four
// real sim-hook waves + the apply-3 jump table, so the per-frame/per-turn command-
// apply dispatch reaches the real reconstructed sim leaves). We boot the spine
// directly (CreateMainWindow -> init -> session frames -> 13-step Shutdown) and
// assert the real per-turn command-apply step dispatched a packet through the wired
// real entity resolver DURING the live frame loop, then the lifecycle tore down
// cleanly.
#include "app/wiring.h"
#include "config/ini.h"
#include "shim_impl/loopback_socket.h"
#include "shim_impl/mem_filesystem.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/null_audio.h"
#include "shim_impl/null_platform.h"
#include "test.h"

using namespace guild;
using guild::app::RealSubsystems;

namespace {

struct SpineFixture {
    shim::NullPlatform plat;
    shim::MemoryGraphicsDevice gfx;
    shim::NullAudioDevice audio;
    shim::MemFileSystem fs;
    std::pair<std::unique_ptr<shim::LoopbackSocket>, std::unique_ptr<shim::LoopbackSocket>>
        sockPair = shim::LoopbackSocket::makePair();
    config::IniFile ini;
    RealSubsystems sub{&plat, &gfx, &audio, &fs, sockPair.first.get(), &ini};
    app::GameApp appObj{plat, gfx, audio, sub};
};

} // namespace

// Full lifecycle driven step-by-step: init wires the real sim hooks; a windowed
// frame applies the per-turn command through the real entity resolver (observed
// live, before shutdown); Shutdown tears everything down cleanly.
TEST(AppFrameLoopE2E, InitFramesShutdownAppliesRealCommand) {
    SpineFixture f;
    CHECK(f.appObj.CreateMainWindow(1));
    CHECK(f.appObj.InitSubsystemsAndMovieDll());
    CHECK(f.appObj.InitDisplayAndPaths(1));
    CHECK(f.appObj.InitEngineAndScriptCommands());

    CHECK(f.sub.simHooksInstalled()); // the four installers ran at command init

    // Run a window of frames with the game-object/command-apply bit set. The first
    // such frame applies the per-turn command through the REAL wired sim dispatch.
    const std::uint32_t mask =
        app::mask::kGameObjects | app::mask::kScripts | app::mask::kHeadlessSuppress;
    for (int i = 0; i < 5; ++i)
        f.appObj.RunFrameLoop(mask);

    // Observe the live cross-module effect BEFORE shutdown resets state: the wired
    // apply path dispatched through the real resolver, which found no live record.
    CHECK(f.sub.cmdApplyReachedReal());
    CHECK_EQ(f.sub.cmdApplyResult(), 1);
    // scriptStepAllActive (kScripts) also ran each frame -> the script slot table
    // was scanned by the real sim::StepAllActive driver.
    CHECK(f.sub.firedReal("scriptStepAllActive"));

    // Clean teardown.
    f.appObj.Shutdown();
    CHECK(f.sub.firedReal("tdGameStateFreeAllResources"));
    CHECK(f.sub.memoryTrackerInited() == false);
    CHECK(f.sub.vfsInited() == false);
}

// The convenience RunHeadless full lifecycle returns a clean exit with the wiring
// in place and the expected number of session frames having run.
TEST(AppFrameLoopE2E, RunHeadlessFullLifecycleClean) {
    app::HeadlessResult r = app::RunHeadless(/*displayMode=*/1, /*showIntro=*/false,
                                             /*networkClient=*/false, /*frames=*/10);
    CHECK_EQ(r.exitCode, 0);
    CHECK(r.frameCount >= 10);
    CHECK(r.presentCount > 0);
    CHECK(r.vfsInited == false);  // VFS shut down at teardown
    CHECK(r.soundInited == false); // sound shut down at teardown
}

// Integration: the SIM-LEAF wiring moved STUB->REAL in this pass. The four
// reconstructed cross-module installers (InstallRealSimHooks{,2,3,4}) were never
// invoked by the app spine, so the per-frame/per-turn command-apply / entity-query
// dispatch ran against INERT default sim hooks. commandQueueInitAndSync now calls
// all four installers + RegisterApplyHandlers3 on the owned CommandQueue, so an
// applied packet reaches the REAL reconstructed sim leaves over the SHARED entity
// arrays (NOT a mock):
//   commandQueueInitAndSync     -> sim::InstallRealSimHooks{,2,3,4} + RegisterApplyHandlers3
//   gameObjectDispatchInteractions (per-turn) -> a real object-resolve command (op 0x4A)
//        routes through the wired sim::GameObjectResolveEntityById entity-query leaf.
//
// We boot RunHeadless (full init -> N frames -> 13-step shutdown) and assert the
// real cross-module side effects are observable: the installers ran, and the
// per-turn command-apply step really dispatched a packet through the wired real
// entity resolver (a pure query that mutates no shared state, so it composes with
// every other suite's entity-array expectations).
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

struct FrameLoopFixture {
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

// commandQueueInitAndSync forwards into the four REAL sim-hook installers: after
// InitEngineAndScriptCommands the installers ran and the command queue is in its
// standalone (single-player) init state.
TEST(AppFrameLoopSimHooks, InitWiresRealSimHooks) {
    FrameLoopFixture f;
    CHECK(f.appObj.CreateMainWindow(1));
    CHECK(f.appObj.InitSubsystemsAndMovieDll());
    CHECK(f.appObj.InitDisplayAndPaths(1));
    CHECK(f.appObj.InitEngineAndScriptCommands()); // runs commandQueueInitAndSync

    CHECK(f.sub.firedReal("commandQueueInitAndSync"));
    CHECK(f.sub.simHooksInstalled());
    CHECK(f.sub.commandQueue().standalone());
}

// Driving the frame loop with the kGameObjects bit set runs the per-turn command-
// apply step: an object-resolve command (opcode 0x4A) routes through the wired REAL
// sim::GameObjectResolveEntityById entity-query leaf.
TEST(AppFrameLoopSimHooks, PerTurnApplyReachesRealEntityResolver) {
    FrameLoopFixture f;
    CHECK(f.appObj.CreateMainWindow(1));
    CHECK(f.appObj.InitSubsystemsAndMovieDll());
    CHECK(f.appObj.InitDisplayAndPaths(1));
    CHECK(f.appObj.InitEngineAndScriptCommands());

    // The interactions/command-apply block runs when the kGameObjects (0x80) bit is
    // set. Drive a few frames with it set so the per-turn command-apply runs.
    const std::uint32_t mask =
        app::mask::kGameObjects | app::mask::kHeadlessSuppress;
    for (int i = 0; i < 4; ++i)
        f.appObj.RunFrameLoop(mask);

    CHECK(f.sub.firedReal("gameObjectDispatchInteractions"));
    // The wired apply path dispatched the packet through the real resolver ...
    CHECK(f.sub.cmdApplyReachedReal());
    // ... which reported "no live record at the queried id" in the cold headless
    // world (return 1) — the real reconstructed resolver ran and found nothing.
    CHECK_EQ(f.sub.cmdApplyResult(), 1);
}

// Full RunHeadless lifecycle: the sim-hook installers ran and the per-turn command-
// apply step dispatched through real reconstructed sim code, end to end, clean exit.
TEST(AppFrameLoopSimHooks, RunHeadlessAppliesRealPerTurnCommand) {
    app::HeadlessResult r = app::RunHeadless(/*displayMode=*/1, /*showIntro=*/false,
                                             /*networkClient=*/false, /*frames=*/6);
    CHECK_EQ(r.exitCode, 0);
    CHECK(r.frameCount >= 6);
    // The session frames run with kGameObjects set, so the per-turn command-apply
    // step ran through the real wired sim dispatch each session. We assert the
    // lifecycle ran cleanly with the wiring in place (the per-turn real-dispatch is
    // asserted in the step-driven test above).
    CHECK(r.memoryTrackerInited == false); // tracker shut down cleanly
}

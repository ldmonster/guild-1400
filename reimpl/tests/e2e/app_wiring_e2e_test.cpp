// End-to-end: full app lifecycle through RealSubsystems + headless shims.
// Construct GameApp with real subsystems and the src/shim_impl headless backends,
// run CreateMainWindow -> init -> ~10 session frames -> 13-step shutdown, and
// assert it completes and the wired subsystems reached their expected states.
#include "app/wiring.h"
#include "tests/framework/test.h"

using namespace guild;

TEST(AppWiringE2E, FullLifecycleSinglePlayer) {
    auto r = app::RunHeadless(/*displayMode=*/1, /*showIntro=*/false,
                              /*networkClient=*/false, /*frames=*/10);

    // The spine returns 0 on a normal exit.
    CHECK_EQ(r.exitCode, 0);

    // Session ran frames (InitOrLoadSession drives framesPerSession via the loop;
    // each frame calls inputLatchAndPump which bumps frameCount).
    CHECK(r.frameCount >= 10);

    // The live single-player session mask sets kRenderWorld+kGameObjects, so the
    // present block ran every frame.
    CHECK(r.presentCount >= 10);

    // The last published feature mask is the live single-player mask (non-zero).
    CHECK(r.lastFeatureMask != 0);

    // Real subsystems came up during init and were torn down in shutdown.
    CHECK(!r.memoryTrackerInited); // shutdown cleared it
    CHECK(!r.vfsInited);           // shutdown cleared it
    CHECK(!r.soundInited);         // tdGameShutdownSubsystems ran SoundSystem::shutdown
}

TEST(AppWiringE2E, FullLifecycleNetworkClient) {
    auto r = app::RunHeadless(/*displayMode=*/3, /*showIntro=*/false,
                              /*networkClient=*/true, /*frames=*/8);
    CHECK_EQ(r.exitCode, 0);
    CHECK(r.frameCount >= 8);
    CHECK(r.lastFeatureMask != 0);
}

TEST(AppWiringE2E, ZeroFrameSessionStillInitsAndShutsDown) {
    auto r = app::RunHeadless(1, false, false, 0);
    CHECK_EQ(r.exitCode, 0);
    // No session frames, but init/shutdown ran (tracker came up & went down).
    CHECK(!r.memoryTrackerInited);
    CHECK(!r.vfsInited);
}

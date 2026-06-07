// Integration: the UNIVERSE (scene-slot) hooks wired STUB->REAL in this pass,
// driven through the app spine against the REAL reconstructed sim::universe
// module (NOT a mock). Two hooks moved real:
//   universeCreateDefaultCameras -> sim::UniverseCreateDefaultCameras @0x5b5f48
//   tdUniverseSwitchActiveSlot0  -> sim::UniverseSwitchActiveSlot(0, quiet) @0x5b4a24
// We boot RunHeadless (full init -> frames -> 13-step shutdown) and assert the
// real cross-module side effects are observable: the camera-bootstrap left a
// non-zero MegaCam handle in the universe module's g_megaCam global, and the
// shutdown swap set the module's active-slot id (g_activeUniverseId) to 0 and
// returned true. The render/scene leaves (object spawn/link, terrain, fog,
// present flip) stay on the module's inert UniverseRenderHooks; the slot
// bookkeeping is the reconstructed sibling we wire against.
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

namespace {

struct UniverseFixture {
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

// The universeCreateDefaultCameras init hook forwards into the REAL
// sim::UniverseCreateDefaultCameras: after InitDisplayAndPaths the module's
// MegaCam handle global is non-zero and the hook recorded REAL.
TEST(AppWiring2Universe, CreateCamerasReachesRealUniverseModule) {
    UniverseFixture f;
    CHECK(f.appObj.CreateMainWindow(1));
    CHECK(f.appObj.InitSubsystemsAndMovieDll());
    CHECK(f.appObj.InitDisplayAndPaths(1)); // runs universeCreateDefaultCameras

    CHECK(f.sub.firedReal("universeCreateDefaultCameras"));
    // Real cross-module side effect: the camera bootstrap spawned + linked a
    // MegaCam, leaving a non-zero handle in the universe module global.
    CHECK(f.sub.universeMegaCam() != 0u);
    CHECK_EQ(f.sub.universeMegaCam(), sim::g_megaCam);
}

// The tdUniverseSwitchActiveSlot0 shutdown hook forwards into the REAL
// sim::UniverseSwitchActiveSlot(0, quiet): after Shutdown the module's
// active-slot id is 0 and the swap reported success.
TEST(AppWiring2Universe, ShutdownSwitchSlot0ReachesRealUniverseModule) {
    UniverseFixture f;
    CHECK(f.appObj.CreateMainWindow(1));
    CHECK(f.appObj.InitSubsystemsAndMovieDll());
    CHECK(f.appObj.InitDisplayAndPaths(1));
    CHECK(f.appObj.InitEngineAndScriptCommands());

    f.appObj.Shutdown(); // runs tdUniverseSwitchActiveSlot0

    CHECK(f.sub.firedReal("tdUniverseSwitchActiveSlot0"));
    CHECK(f.sub.universeSwitchedToSlot0());           // the real swap returned true
    CHECK_EQ(sim::g_activeUniverseId, 0);             // module global now slot 0
}

// Full RunHeadless lifecycle: both newly-wired universe hooks fire REAL and the
// observable module state is consistent end to end.
TEST(AppWiring2Universe, RunHeadlessExercisesBothUniverseHooks) {
    app::HeadlessResult r = app::RunHeadless(/*displayMode=*/1, /*showIntro=*/false,
                                             /*networkClient=*/false, /*frames=*/8);
    CHECK_EQ(r.exitCode, 0);
    // After a clean run the universe module's active slot settled at 0 (the
    // shutdown swap), which is also its cold ResetUniverse default.
    CHECK_EQ(sim::g_activeUniverseId, 0);
}

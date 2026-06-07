// Unit tests for the SECOND wave of app-spine wiring (guild::app::RealSubsystems).
// Verifies the hooks moved STUB->REAL in this pass now forward to real
// reconstructed module entry points (observed via recorded events + module
// state): file-create-directory (shim fs), timebase timer (crt::TimeBase),
// render engine/scene init, DirectInput-init (gui input reset), gui gfx load
// (shape bank + text DB), text definition load (text DB), script command
// register/invoke (script VM ABI), per-frame widget/HUD/gameobject/tooltip
// dispatch, and the audio/sim/timer teardown steps.
#include "app/wiring.h"
#include "config/ini.h"
#include "gui/input.h"
#include "shim_impl/loopback_socket.h"
#include "shim_impl/mem_filesystem.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/null_audio.h"
#include "shim_impl/null_platform.h"
#include "tests/framework/test.h"

using namespace guild;
using guild::app::RealSubsystems;

namespace {

struct Fixture2 {
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

// ---- init: file/timer/engine/input/gfx/text -------------------------------

TEST(AppWiring2, FileCreateDirectoryIsReal) {
    Fixture2 f;
    f.sub.fileCreateDirectory("\\project\\Screenshots");
    CHECK(f.sub.firedReal("fileCreateDirectory"));
    // The shim now reports the directory marker exists.
    CHECK(f.fs.exists("\\project\\Screenshots\\.dir"));
}

TEST(AppWiring2, TimeBaseStartTimerIsReal) {
    Fixture2 f;
    f.sub.timeBaseStartTimer(0x0E, 0);
    CHECK(f.sub.firedReal("timeBaseStartTimer"));
    CHECK(f.sub.timerRunning());
}

TEST(AppWiring2, RenderEngineInitIsReal) {
    Fixture2 f;
    bool ok = f.sub.renderInitEngineDevice(800, 600, 16, false);
    CHECK(ok);
    CHECK(f.sub.firedReal("renderInitEngineDevice"));
    // The scene-init frame presented once during bring-up.
    CHECK_EQ(f.sub.presentCount(), 1);
}

TEST(AppWiring2, InputInitIsReal) {
    Fixture2 f;
    // Dirty the input state, then ensure the real reset clears it.
    gui::g_mouseClick = 7;
    gui::g_lastClickedId = 42;
    f.sub.inputDirectInputInit(6);
    CHECK(f.sub.firedReal("inputDirectInputInit"));
    CHECK_EQ(gui::g_mouseClick, 0);
    CHECK_EQ(gui::g_lastClickedId, -1);
}

TEST(AppWiring2, GuiLoadGfxFileBuildsShapeBankAndText) {
    Fixture2 f;
    bool ok = f.sub.guiLoadGfxFile("gilde.gfx");
    CHECK(ok);
    CHECK(f.sub.firedReal("guiLoadGfxFile"));
    // One shape was appended through the real ShapeBankAddShape path.
    CHECK_EQ(f.sub.shapeBankCount(), 1);
    // Two label entries were seeded into the real text DB.
    CHECK(f.sub.textDbCount() >= 2);
}

TEST(AppWiring2, TextDefinitionLoadIsReal) {
    Fixture2 f;
    f.sub.textLoadDefinitionFile("\\project\\gilde_text.def");
    CHECK(f.sub.firedReal("textLoadDefinitionFile"));
    CHECK(f.sub.textDbCount() >= 4);
}

TEST(AppWiring2, ScriptRegisterCommandsRunsInvokeAbi) {
    Fixture2 f;
    f.sub.scriptRegisterCommands();
    CHECK(f.sub.firedReal("scriptRegisterCommands"));
    // The registered-command body summed the by-pointer args {1,2,3}.
    CHECK_EQ(f.sub.scriptCmdResult(), 6);
}

// ---- per-frame: widget/HUD/gameobject/tooltip dispatch ---------------------

TEST(AppWiring2, PerFrameDispatchHooksAreReal) {
    Fixture2 f;
    f.sub.widgetInitSystem(); // bring up GUI tables first
    f.sub.widgetDispatchMouseClick();
    f.sub.hudHandleMouseClick();
    f.sub.gameObjectDispatchInteractions();
    f.sub.tooltipDispatch();

    CHECK(f.sub.firedReal("widgetDispatchMouseClick"));
    CHECK(f.sub.firedReal("hudHandleMouseClick"));
    CHECK(f.sub.firedReal("gameObjectDispatchInteractions"));
    CHECK(f.sub.firedReal("tooltipDispatch"));
}

TEST(AppWiring2, GameObjectBlockAdvancesFade) {
    Fixture2 f;
    // The fade reaches t==1 (done) after `duration` frames of the GameObjects block.
    for (int i = 0; i < 40; ++i)
        f.sub.gameObjectDispatchInteractions();
    CHECK(f.sub.fadeDone());
}

// ---- shutdown: audio/sim/timer teardown ------------------------------------

TEST(AppWiring2, ShutdownTeardownHooksAreReal) {
    Fixture2 f;
    // Bring real subsystems up so their teardown is "real".
    f.sub.soundLibInit(48, 2, 44100);
    f.sub.timeBaseStartTimer(0x0E, 0);

    f.sub.tdGameShutdownSubsystems();    // SoundSystem::shutdown
    f.sub.tdGameStateFreeAllResources(); // ResetEntityArrays
    f.sub.tdTimeBaseStopTimer();         // TimeBase::StopTimer

    CHECK(f.sub.firedReal("tdGameShutdownSubsystems"));
    CHECK(f.sub.firedReal("tdGameStateFreeAllResources"));
    CHECK(f.sub.firedReal("tdTimeBaseStopTimer"));
    CHECK(!f.sub.soundInited()); // sound torn down
    CHECK(!f.sub.timerRunning()); // timer stopped
}

TEST(AppWiring2, NullDeviceNewHooksDegradeToStub) {
    // No fs/audio/plat -> the new fs/timer/audio hooks become recorded stubs.
    config::IniFile ini;
    RealSubsystems sub(nullptr, nullptr, nullptr, nullptr, nullptr, &ini);
    sub.fileCreateDirectory("x");
    sub.timeBaseStartTimer(14, 0);
    sub.tdGameShutdownSubsystems();
    CHECK(sub.fired("fileCreateDirectory"));
    CHECK(!sub.firedReal("fileCreateDirectory"));
    CHECK(!sub.firedReal("timeBaseStartTimer"));
    CHECK(!sub.firedReal("tdGameShutdownSubsystems"));
}

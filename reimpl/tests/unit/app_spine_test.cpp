// Unit tests for the app spine (guild::app): init order, frame-loop feature
// mask dispatch, and the 13-step shutdown teardown order. Uses recording mock
// shims (IPlatform/IGraphicsDevice/IAudioDevice) and a recording ISubsystems
// hook. Everything is self-contained; no real OS/subsystem code is linked.
#include "app/app_init.h"
#include "app/gamelogic.h"
#include "tests/framework/test.h"

#include <string>
#include <vector>

using namespace guild;

// ----------------------------------------------------------------------------
// Recording mocks
// ----------------------------------------------------------------------------
namespace {

struct CallLog {
    std::vector<std::string> calls;
    void rec(const char* s) { calls.emplace_back(s); }
    int index(const std::string& s) const {
        for (size_t i = 0; i < calls.size(); ++i)
            if (calls[i] == s) return static_cast<int>(i);
        return -1;
    }
    int count(const std::string& s) const {
        int n = 0;
        for (auto& c : calls) if (c == s) ++n;
        return n;
    }
};

class MockPlatform : public shim::IPlatform {
public:
    CallLog& log;
    bool windowOk = true;
    explicit MockPlatform(CallLog& l) : log(l) {}
    bool createMainWindow(const char*, int, int, bool fs) override {
        log.rec(fs ? "plat.createWindow.fs" : "plat.createWindow.win");
        return windowOk;
    }
    void destroyMainWindow() override { log.rec("plat.destroyWindow"); }
    bool pumpMessages() override { return true; }
    std::uint32_t timeMs() override { return 0; }
    void sleepMs(std::uint32_t) override {}
    void getMouse(shim::MouseState&) override {}
    bool keyDown(int) override { return false; }
};

class MockGfx : public shim::IGraphicsDevice {
public:
    CallLog& log;
    bool initOk = true;
    explicit MockGfx(CallLog& l) : log(l) {}
    bool init(int, int, int, bool) override { log.rec("gfx.init"); return initOk; }
    void shutdown() override { log.rec("gfx.shutdown"); }
    shim::Surface* backbuffer() override { return nullptr; }
    void present() override { log.rec("gfx.present"); }
    void setPalette(const std::uint32_t*) override {}
};

class MockAudio : public shim::IAudioDevice {
public:
    CallLog& log;
    explicit MockAudio(CallLog& l) : log(l) {}
    bool init(int, int, int) override { return true; }
    void shutdown() override {}
    shim::VoiceHandle allocVoice() override { return 0; }
    void freeVoice(shim::VoiceHandle) override {}
    void playSample(shim::VoiceHandle, const void*, std::size_t, int, int) override {}
    void stop(shim::VoiceHandle) override {}
    void setVolume(shim::VoiceHandle, int) override {}
    void setPan(shim::VoiceHandle, int) override {}
    void setMasterVolume(int) override {}
};

class MockSubsystems : public app::ISubsystems {
public:
    CallLog& log;
    bool movieDllLoads = true;
    bool enumModesOk = true;
    bool engineDeviceOk = true;
    bool gfxFileOk = true;
    bool textDefOk = true;
    bool soundLibOk = true;
    explicit MockSubsystems(CallLog& l) : log(l) {}

    // init: InitSubsystemsAndMovieDll
    void errorLogInit() override { log.rec("errorLogInit"); }
    void memoryInitTracker(int) override { log.rec("memoryInitTracker"); }
    void memPoolStartupStack(int) override { log.rec("memPoolStartupStack"); }
    bool loadMovieDll() override { log.rec("loadMovieDll"); return movieDllLoads; }
    void fileCreateDirectory(const std::string&) override { log.rec("fileCreateDirectory"); }
    void vfsInit(const std::string&) override { log.rec("vfsInit"); }
    void timeBaseStartTimer(int, int) override { log.rec("timeBaseStartTimer"); }

    // init: InitDisplayAndPaths
    void configReadGfxAndSound() override { log.rec("configReadGfxAndSound"); }
    bool renderEnumDisplayModes() override { log.rec("renderEnumDisplayModes"); return enumModesOk; }
    bool renderInitEngineDevice(int, int, int, bool) override { log.rec("renderInitEngineDevice"); return engineDeviceOk; }
    void universeCreateDefaultCameras() override { log.rec("universeCreateDefaultCameras"); }
    void inputDirectInputInit(int mode) override { log.rec(mode == 1 ? "inputDInput.fs" : "inputDInput.win"); }
    void renderSetAssetPaths() override { log.rec("renderSetAssetPaths"); }
    void renderApplyGfxSettings() override { log.rec("renderApplyGfxSettings"); }
    bool guiLoadGfxFile(const std::string&) override { log.rec("guiLoadGfxFile"); return gfxFileOk; }
    void widgetInitSystem() override { log.rec("widgetInitSystem"); }

    // init: InitEngineAndScriptCommands
    bool textLoadDefinitionFile(const std::string&) override { log.rec("textLoadDefinitionFile"); return textDefOk; }
    void netConnectToServer(const std::string&, int) override { log.rec("netConnectToServer"); }
    void commandQueueInitAndSync() override { log.rec("commandQueueInitAndSync"); }
    void worldLoadBuildingAndObjectData(const std::string&) override { log.rec("worldLoadBuildingAndObjectData"); }
    void buildingComputeMarketPrices() override { log.rec("buildingComputeMarketPrices"); }
    void audioStartupMilesDriver() override { log.rec("audioStartupMilesDriver"); }
    bool soundLibInit(int, int, int) override { log.rec("soundLibInit"); return soundLibOk; }
    void sound3dInitPool(int) override { log.rec("sound3dInitPool"); }
    void soundWaveInitSineTables() override { log.rec("soundWaveInitSineTables"); }
    void soundLoadSampleBank(const std::string&) override { log.rec("soundLoadSampleBank"); }
    void soundPreloadIncludeFile(const std::string&) override { log.rec("soundPreloadIncludeFile"); }
    void soundInitMusicThread(int) override { log.rec("soundInitMusicThread"); }
    void audioApplyVolumeSettings() override { log.rec("audioApplyVolumeSettings"); }
    void scriptRegisterCommands() override { log.rec("scriptRegisterCommands"); }

    // intro
    void moviePlayIntroSequence() override { log.rec("moviePlayIntroSequence"); }
    void movieDllExit() override { log.rec("movieDllExit"); }

    // frame steps
    void inputLatchAndPump() override { log.rec("f.inputLatchAndPump"); }
    void widgetDispatchMouseClick() override { log.rec("f.widgetMouse"); }
    void hudHandleMouseClick() override { log.rec("f.hudMouse"); }
    void inputCommandPoll() override { log.rec("f.inputCommandPoll"); }
    void commandNetworkPump() override { log.rec("f.networkCommand"); }
    void scriptStepAllActive() override { log.rec("f.scripts"); }
    void gameObjectDispatchInteractions() override { log.rec("f.gameObjects"); }
    void renderMainViewFrame() override { log.rec("f.renderWorld"); }
    void weatherUpdateSky() override { log.rec("f.weatherSky"); }
    void dayCycleAndOutdoorMusic() override { log.rec("f.dayCycleMusic"); }
    void hudSelectionAndTargets() override { log.rec("f.hudSelection"); }
    void tooltipDispatch() override { log.rec("f.tooltips"); }
    void hudLabelsAndCaption() override { log.rec("f.hudLabels"); }
    void cameraCombatScroll() override { log.rec("f.combatScroll"); }
    void presentFrame() override { log.rec("f.present"); }
    void optionsChatHotkeyPanels() override { log.rec("f.optionsPanels"); }
    void autosaveAndNetWait() override { log.rec("f.autosave"); }
    void characterCollectByOwner() override { log.rec("f.characterCollect"); }
    void quickJumpContact() override { log.rec("f.quickJump"); }

    // teardown (13)
    void tdGameShutdownSubsystems() override { log.rec("td.GameShutdownSubsystems"); }
    void tdWidgetShutdownSystem() override { log.rec("td.WidgetShutdownSystem"); }
    void tdConfigWriteGfxSettings() override { log.rec("td.ConfigWriteGfxSettings"); }
    void tdGameStateFreeAllResources() override { log.rec("td.GameStateFreeAllResources"); }
    void tdUniverseSwitchActiveSlot0() override { log.rec("td.UniverseSwitchActiveSlot0"); }
    void tdTableResetLightmaps() override { log.rec("td.TableResetLightmaps"); }
    void tdRenderShutdownEngine() override { log.rec("td.RenderShutdownEngine"); }
    void tdInputDirectInputShutdown() override { log.rec("td.InputDirectInputShutdown"); }
    void tdTimeBaseStopTimer() override { log.rec("td.TimeBaseStopTimer"); }
    void tdVfsShutdown() override { log.rec("td.VfsShutdown"); }
    void tdMemPoolShutdownStack() override { log.rec("td.MemPoolShutdownStack"); }
    void tdMemoryShutdownTracker() override { log.rec("td.MemoryShutdownTracker"); }
    void tdErrorLogShutdown() override { log.rec("td.ErrorLogShutdown"); }
};

// Assert that a < b < c < ... in the call log (strictly increasing indices,
// all present).
bool ordered(const CallLog& log, std::initializer_list<const char*> seq) {
    int prev = -1;
    for (const char* s : seq) {
        int i = log.index(s);
        if (i < 0 || i <= prev) return false;
        prev = i;
    }
    return true;
}

} // namespace

// ----------------------------------------------------------------------------
// InitSubsystemsAndMovieDll order (0x527de0)
// ----------------------------------------------------------------------------
TEST(AppSpine, InitSubsystemsOrder) {
    CallLog log;
    MockPlatform plat(log); MockGfx gfx(log); MockAudio audio(log); MockSubsystems sub(log);
    app::GameApp appObj(plat, gfx, audio, sub);

    // showIntro path so the movie DLL is loaded in-sequence.
    appObj.Run("exe", /*mode*/ 3, /*showIntro*/ true, /*net*/ false, /*frames*/ 0);

    CHECK(ordered(log, {"errorLogInit", "memoryInitTracker", "memPoolStartupStack",
                        "loadMovieDll", "fileCreateDirectory", "vfsInit",
                        "timeBaseStartTimer"}));
    // Intro plays after subsystem init, before display init.
    CHECK(ordered(log, {"timeBaseStartTimer", "moviePlayIntroSequence",
                        "configReadGfxAndSound"}));
}

// ----------------------------------------------------------------------------
// InitDisplayAndPaths order (0x527fa4) — fullscreen picks DInput mode 1.
// ----------------------------------------------------------------------------
TEST(AppSpine, InitDisplayOrderFullscreen) {
    CallLog log;
    MockPlatform plat(log); MockGfx gfx(log); MockAudio audio(log); MockSubsystems sub(log);
    app::GameApp appObj(plat, gfx, audio, sub);
    appObj.Run("exe", 3, false, false, 0);

    CHECK(ordered(log, {"configReadGfxAndSound", "renderEnumDisplayModes",
                        "gfx.init", "renderInitEngineDevice",
                        "universeCreateDefaultCameras", "inputDInput.fs",
                        "renderSetAssetPaths", "renderApplyGfxSettings",
                        "guiLoadGfxFile", "widgetInitSystem"}));
}

TEST(AppSpine, InitDisplayWindowedPicksDInputMode6) {
    CallLog log;
    MockPlatform plat(log); MockGfx gfx(log); MockAudio audio(log); MockSubsystems sub(log);
    app::GameApp appObj(plat, gfx, audio, sub);
    appObj.Run("exe", 1, false, false, 0); // windowed
    CHECK(log.index("inputDInput.win") >= 0);
    CHECK(log.index("inputDInput.fs") < 0);
}

// ----------------------------------------------------------------------------
// InitEngineAndScriptCommands order (0x528560)
// ----------------------------------------------------------------------------
TEST(AppSpine, InitEngineOrder) {
    CallLog log;
    MockPlatform plat(log); MockGfx gfx(log); MockAudio audio(log); MockSubsystems sub(log);
    app::GameApp appObj(plat, gfx, audio, sub);
    appObj.Run("exe", 3, false, false, 0);

    CHECK(ordered(log, {"textLoadDefinitionFile", "netConnectToServer",
                        "commandQueueInitAndSync", "worldLoadBuildingAndObjectData",
                        "buildingComputeMarketPrices", "audioStartupMilesDriver",
                        "soundLibInit", "sound3dInitPool", "soundWaveInitSineTables",
                        "soundLoadSampleBank", "soundPreloadIncludeFile",
                        "soundInitMusicThread", "audioApplyVolumeSettings",
                        "scriptRegisterCommands"}));
}

// ----------------------------------------------------------------------------
// Init failure short-circuits to shutdown (window create fails).
// ----------------------------------------------------------------------------
TEST(AppSpine, WindowCreateFailRunsTeardown) {
    CallLog log;
    MockPlatform plat(log); MockGfx gfx(log); MockAudio audio(log); MockSubsystems sub(log);
    plat.windowOk = false;
    app::GameApp appObj(plat, gfx, audio, sub);
    int rc = appObj.Run("exe", 3, false, false, 4);
    CHECK_EQ(rc, 0);
    // Did not get to display init.
    CHECK(log.index("configReadGfxAndSound") < 0);
    // Teardown still ran in order.
    CHECK(ordered(log, {"td.GameShutdownSubsystems", "td.ErrorLogShutdown"}));
}

TEST(AppSpine, GfxFileFailShortCircuits) {
    CallLog log;
    MockPlatform plat(log); MockGfx gfx(log); MockAudio audio(log); MockSubsystems sub(log);
    sub.gfxFileOk = false;
    app::GameApp appObj(plat, gfx, audio, sub);
    appObj.Run("exe", 3, false, false, 4);
    // guiLoadGfxFile ran, but widgetInitSystem (next step) did not.
    CHECK(log.index("guiLoadGfxFile") >= 0);
    CHECK(log.index("widgetInitSystem") < 0);
    // Engine init never reached.
    CHECK(log.index("scriptRegisterCommands") < 0);
}

// ----------------------------------------------------------------------------
// Shutdown teardown: the exact 13-step order.
// ----------------------------------------------------------------------------
TEST(AppSpine, ShutdownThirteenStepOrder) {
    CallLog log;
    MockPlatform plat(log); MockGfx gfx(log); MockAudio audio(log); MockSubsystems sub(log);
    app::GameApp appObj(plat, gfx, audio, sub);
    appObj.Run("exe", 3, false, false, 0);

    CHECK(ordered(log, {
        "td.GameShutdownSubsystems", "td.WidgetShutdownSystem",
        "td.ConfigWriteGfxSettings", "td.GameStateFreeAllResources",
        "td.UniverseSwitchActiveSlot0", "td.TableResetLightmaps",
        "td.RenderShutdownEngine", "td.InputDirectInputShutdown",
        "td.TimeBaseStopTimer", "td.VfsShutdown", "td.MemPoolShutdownStack",
        "td.MemoryShutdownTracker", "td.ErrorLogShutdown"}));
    // destroyWindow comes after the 13 steps.
    CHECK(log.index("plat.destroyWindow") > log.index("td.ErrorLogShutdown"));
    // Each teardown step runs exactly once (single Shutdown).
    CHECK_EQ(log.count("td.GameShutdownSubsystems"), 1);
    CHECK_EQ(log.count("td.ErrorLogShutdown"), 1);
}

TEST(AppSpine, MovieDllExitOnlyWhenIntroAndLoaded) {
    // intro + dll loads -> movieDllExit fires (after the 13 steps, before destroy).
    {
        CallLog log;
        MockPlatform plat(log); MockGfx gfx(log); MockAudio audio(log); MockSubsystems sub(log);
        sub.movieDllLoads = true;
        app::GameApp appObj(plat, gfx, audio, sub);
        appObj.Run("exe", 3, /*intro*/ true, false, 0);
        CHECK(log.index("movieDllExit") > log.index("td.ErrorLogShutdown"));
        CHECK(log.index("plat.destroyWindow") > log.index("movieDllExit"));
    }
    // no intro -> no movieDllExit.
    {
        CallLog log;
        MockPlatform plat(log); MockGfx gfx(log); MockAudio audio(log); MockSubsystems sub(log);
        app::GameApp appObj(plat, gfx, audio, sub);
        appObj.Run("exe", 3, /*intro*/ false, false, 0);
        CHECK(log.index("movieDllExit") < 0);
    }
    // intro but dll fails to load -> no movieDllExit.
    {
        CallLog log;
        MockPlatform plat(log); MockGfx gfx(log); MockAudio audio(log); MockSubsystems sub(log);
        sub.movieDllLoads = false;
        app::GameApp appObj(plat, gfx, audio, sub);
        appObj.Run("exe", 3, /*intro*/ true, false, 0);
        CHECK(log.index("loadMovieDll") >= 0);
        CHECK(log.index("movieDllExit") < 0);
    }
}

// ----------------------------------------------------------------------------
// Frame-loop feature-mask dispatch.
// ----------------------------------------------------------------------------
TEST(AppSpine, FrameLoopAlwaysPumps) {
    CallLog log;
    MockPlatform plat(log); MockGfx gfx(log); MockAudio audio(log); MockSubsystems sub(log);
    app::GameApp appObj(plat, gfx, audio, sub);
    appObj.RunFrameLoop(0); // empty mask
    CHECK_EQ(log.count("f.inputLatchAndPump"), 1);
    // With an all-zero mask, all "& bit" gated steps are skipped...
    CHECK_EQ(log.count("f.widgetMouse"), 0);
    CHECK_EQ(log.count("f.renderWorld"), 0);
    CHECK_EQ(log.count("f.present"), 0);
    CHECK_EQ(log.count("f.optionsPanels"), 0);
    // ...but the two "suppress" defaults run: combat-collect (no kCombatSelect)
    // and autosave (no kAutosaveSuppress).
    CHECK_EQ(log.count("f.characterCollect"), 1);
    CHECK_EQ(log.count("f.autosave"), 1);
    // Mask published.
    CHECK_EQ(appObj.lastFeatureMask(), 0u);
}

TEST(AppSpine, FrameLoopWidgetMouseBit) {
    CallLog log;
    MockPlatform plat(log); MockGfx gfx(log); MockAudio audio(log); MockSubsystems sub(log);
    app::GameApp appObj(plat, gfx, audio, sub);
    appObj.RunFrameLoop(app::mask::kWidgetMouse);
    CHECK_EQ(log.count("f.widgetMouse"), 1);
    CHECK_EQ(log.count("f.hudMouse"), 0);
    CHECK_EQ(log.count("f.scripts"), 0);
}

TEST(AppSpine, FrameLoopHudMouseSuppressedByInputSuppress) {
    // kHudMouse alone -> runs.
    {
        CallLog log;
        MockPlatform plat(log); MockGfx gfx(log); MockAudio audio(log); MockSubsystems sub(log);
        app::GameApp appObj(plat, gfx, audio, sub);
        appObj.RunFrameLoop(app::mask::kHudMouse);
        CHECK_EQ(log.count("f.hudMouse"), 1);
    }
    // kHudMouse | kInputSuppress -> suppressed.
    {
        CallLog log;
        MockPlatform plat(log); MockGfx gfx(log); MockAudio audio(log); MockSubsystems sub(log);
        app::GameApp appObj(plat, gfx, audio, sub);
        appObj.RunFrameLoop(app::mask::kHudMouse | app::mask::kInputSuppress);
        CHECK_EQ(log.count("f.hudMouse"), 0);
    }
}

TEST(AppSpine, FrameLoopRenderWorldRunsDayCycleWhenBitSetAndNotHeadless) {
    // kRenderWorld | kDayCycleMusic -> renderWorld + dayCycleMusic, present skipped.
    {
        CallLog log;
        MockPlatform plat(log); MockGfx gfx(log); MockAudio audio(log); MockSubsystems sub(log);
        app::GameApp appObj(plat, gfx, audio, sub);
        appObj.RunFrameLoop(app::mask::kRenderWorld | app::mask::kDayCycleMusic);
        CHECK_EQ(log.count("f.renderWorld"), 1);
        CHECK_EQ(log.count("f.dayCycleMusic"), 1);
        CHECK_EQ(log.count("f.present"), 0); // present needs kGameObjects
    }
    // Headless suppress kills the renderWorld DAY-CYCLE inner block (gilde.exe
    // 0x4c0c61: (v54 & 0x10000)==0 && (v54 & 0x40000)!=0) — but the MUSIC half of
    // the fused hook still runs: the binary's music gate at 0x4c0d56 is
    // `dword_63C8F8 && (v54 & 0x40000)` with NO headless term, so the fused
    // dayCycleAndOutdoorMusic hook must fire exactly once whenever bit 0x40000 is
    // set. [Pin updated from 0 with that binary evidence.]
    {
        CallLog log;
        MockPlatform plat(log); MockGfx gfx(log); MockAudio audio(log); MockSubsystems sub(log);
        app::GameApp appObj(plat, gfx, audio, sub);
        int rc = appObj.RunFrameLoop(app::mask::kRenderWorld | app::mask::kDayCycleMusic |
                                     app::mask::kHeadlessSuppress);
        CHECK_EQ(rc, 0);
        CHECK_EQ(log.count("f.renderWorld"), 1);   // render still gated by kRenderWorld
        CHECK_EQ(log.count("f.dayCycleMusic"), 1); // music half: 0x40000 alone (0x4c0d56)
    }
}

TEST(AppSpine, FrameLoopDayCycleStandaloneWhenNoRenderWorld) {
    CallLog log;
    MockPlatform plat(log); MockGfx gfx(log); MockAudio audio(log); MockSubsystems sub(log);
    app::GameApp appObj(plat, gfx, audio, sub);
    appObj.RunFrameLoop(app::mask::kDayCycleMusic); // music day-cycle, no world render
    CHECK_EQ(log.count("f.dayCycleMusic"), 1);
    CHECK_EQ(log.count("f.renderWorld"), 0);
}

TEST(AppSpine, FrameLoopPresentBlockGatedByGameObjects) {
    CallLog log;
    MockPlatform plat(log); MockGfx gfx(log); MockAudio audio(log); MockSubsystems sub(log);
    app::GameApp appObj(plat, gfx, audio, sub);
    appObj.RunFrameLoop(app::mask::kGameObjects | app::mask::kHudLabels |
                        app::mask::kCombatScroll);
    CHECK_EQ(log.count("f.gameObjects"), 1);
    CHECK_EQ(log.count("f.hudLabels"), 1);
    CHECK_EQ(log.count("f.combatScroll"), 1);
    CHECK_EQ(log.count("f.present"), 1);
    // present block ordering: gameObjects -> hudLabels -> combatScroll -> present
    CHECK(ordered(log, {"f.gameObjects", "f.hudLabels", "f.combatScroll", "f.present"}));
}

TEST(AppSpine, FrameLoopCombatSelectSuppressesCharacterCollect) {
    CallLog log;
    MockPlatform plat(log); MockGfx gfx(log); MockAudio audio(log); MockSubsystems sub(log);
    app::GameApp appObj(plat, gfx, audio, sub);
    appObj.RunFrameLoop(app::mask::kCombatSelect);
    CHECK_EQ(log.count("f.characterCollect"), 0); // suppressed
}

TEST(AppSpine, FrameLoopAutosaveSuppress) {
    CallLog log;
    MockPlatform plat(log); MockGfx gfx(log); MockAudio audio(log); MockSubsystems sub(log);
    app::GameApp appObj(plat, gfx, audio, sub);
    appObj.RunFrameLoop(app::mask::kAutosaveSuppress);
    CHECK_EQ(log.count("f.autosave"), 0);
}

TEST(AppSpine, FrameLoopQuickJumpAndHudSelectionGatedByInputSuppress) {
    // quick-jump + hud-selection run when not suppressed.
    {
        CallLog log;
        MockPlatform plat(log); MockGfx gfx(log); MockAudio audio(log); MockSubsystems sub(log);
        app::GameApp appObj(plat, gfx, audio, sub);
        appObj.RunFrameLoop(app::mask::kQuickJump | app::mask::kHudSelection);
        CHECK_EQ(log.count("f.quickJump"), 1);
        CHECK_EQ(log.count("f.hudSelection"), 1);
    }
    // input-suppress kills both.
    {
        CallLog log;
        MockPlatform plat(log); MockGfx gfx(log); MockAudio audio(log); MockSubsystems sub(log);
        app::GameApp appObj(plat, gfx, audio, sub);
        appObj.RunFrameLoop(app::mask::kQuickJump | app::mask::kHudSelection |
                            app::mask::kInputSuppress);
        CHECK_EQ(log.count("f.quickJump"), 0);
        CHECK_EQ(log.count("f.hudSelection"), 0);
    }
}

TEST(AppSpine, FrameLoopIndividualBitsIsolated) {
    // Each remaining single-bit mask triggers exactly its own step.
    struct Pair { std::uint32_t bit; const char* call; };
    const Pair pairs[] = {
        {app::mask::kInputCommandPoll, "f.inputCommandPoll"},
        {app::mask::kNetworkCommand,   "f.networkCommand"},
        {app::mask::kScripts,          "f.scripts"},
        {app::mask::kGameObjects,      "f.gameObjects"},
        {app::mask::kRenderWorld,      "f.renderWorld"},
        {app::mask::kWeatherSky,       "f.weatherSky"},
        {app::mask::kTooltips,         "f.tooltips"},
        {app::mask::kOptionsAndPanels, "f.optionsPanels"},
    };
    for (auto& p : pairs) {
        CallLog log;
        MockPlatform plat(log); MockGfx gfx(log); MockAudio audio(log); MockSubsystems sub(log);
        app::GameApp appObj(plat, gfx, audio, sub);
        appObj.RunFrameLoop(p.bit);
        CHECK_EQ(log.count(p.call), 1);
    }
}

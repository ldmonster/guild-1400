// End-to-end test for the app spine (guild::app): a complete mock lifecycle ---
// init -> run N frames with a representative live-game mask -> shutdown --- with
// the full ordered call log verified against the recovered reference sequence.
#include "app/app_init.h"
#include "app/gamelogic.h"
#include "tests/framework/test.h"

#include <string>
#include <vector>

using namespace guild;

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
        int n = 0; for (auto& c : calls) if (c == s) ++n; return n;
    }
    int lastIndex(const std::string& s) const {
        int r = -1;
        for (size_t i = 0; i < calls.size(); ++i)
            if (calls[i] == s) r = static_cast<int>(i);
        return r;
    }
};

class MockPlatform : public shim::IPlatform {
public:
    CallLog& log; explicit MockPlatform(CallLog& l) : log(l) {}
    bool createMainWindow(const char*, int, int, bool fs) override {
        log.rec(fs ? "plat.createWindow.fs" : "plat.createWindow.win"); return true;
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
    CallLog& log; explicit MockGfx(CallLog& l) : log(l) {}
    bool init(int, int, int, bool) override { log.rec("gfx.init"); return true; }
    void shutdown() override {}
    shim::Surface* backbuffer() override { return nullptr; }
    void present() override {}
    void setPalette(const std::uint32_t*) override {}
};
class MockAudio : public shim::IAudioDevice {
public:
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
    CallLog& log; explicit MockSubsystems(CallLog& l) : log(l) {}
    void errorLogInit() override { log.rec("errorLogInit"); }
    void memoryInitTracker(int) override { log.rec("memoryInitTracker"); }
    void memPoolStartupStack(int) override { log.rec("memPoolStartupStack"); }
    bool loadMovieDll() override { log.rec("loadMovieDll"); return true; }
    void fileCreateDirectory(const std::string&) override { log.rec("fileCreateDirectory"); }
    void vfsInit(const std::string&) override { log.rec("vfsInit"); }
    void timeBaseStartTimer(int, int) override { log.rec("timeBaseStartTimer"); }
    void configReadGfxAndSound() override { log.rec("configReadGfxAndSound"); }
    bool renderEnumDisplayModes() override { log.rec("renderEnumDisplayModes"); return true; }
    bool renderInitEngineDevice(int, int, int, bool) override { log.rec("renderInitEngineDevice"); return true; }
    void universeCreateDefaultCameras() override { log.rec("universeCreateDefaultCameras"); }
    void inputDirectInputInit(int mode) override { log.rec(mode == 1 ? "inputDInput.fs" : "inputDInput.win"); }
    void renderSetAssetPaths() override { log.rec("renderSetAssetPaths"); }
    void renderApplyGfxSettings() override { log.rec("renderApplyGfxSettings"); }
    bool guiLoadGfxFile(const std::string&) override { log.rec("guiLoadGfxFile"); return true; }
    void widgetInitSystem() override { log.rec("widgetInitSystem"); }
    bool textLoadDefinitionFile(const std::string&) override { log.rec("textLoadDefinitionFile"); return true; }
    void netConnectToServer(const std::string&, int) override { log.rec("netConnectToServer"); }
    void commandQueueInitAndSync() override { log.rec("commandQueueInitAndSync"); }
    void worldLoadBuildingAndObjectData(const std::string&) override { log.rec("worldLoadBuildingAndObjectData"); }
    void buildingComputeMarketPrices() override { log.rec("buildingComputeMarketPrices"); }
    void audioStartupMilesDriver() override { log.rec("audioStartupMilesDriver"); }
    bool soundLibInit(int, int, int) override { log.rec("soundLibInit"); return true; }
    void sound3dInitPool(int) override { log.rec("sound3dInitPool"); }
    void soundWaveInitSineTables() override { log.rec("soundWaveInitSineTables"); }
    void soundLoadSampleBank(const std::string&) override { log.rec("soundLoadSampleBank"); }
    void soundPreloadIncludeFile(const std::string&) override { log.rec("soundPreloadIncludeFile"); }
    void soundInitMusicThread(int) override { log.rec("soundInitMusicThread"); }
    void audioApplyVolumeSettings() override { log.rec("audioApplyVolumeSettings"); }
    void scriptRegisterCommands() override { log.rec("scriptRegisterCommands"); }
    void moviePlayIntroSequence() override { log.rec("moviePlayIntroSequence"); }
    void movieDllExit() override { log.rec("movieDllExit"); }
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

// Full lifecycle: fullscreen, intro on, single-player, 3 session frames.
TEST(AppSpineE2E, FullLifecycleOrderedCallLog) {
    CallLog log;
    MockPlatform plat(log); MockGfx gfx(log); MockAudio audio; MockSubsystems sub(log);
    app::GameApp appObj(plat, gfx, audio, sub);

    const int N = 3;
    int rc = appObj.Run("C:\\Games\\Gilde", /*mode*/ 3, /*intro*/ true,
                        /*net*/ false, /*frames*/ N);
    CHECK_EQ(rc, 0);

    // ---- 1. Phase ordering: window -> subsystems -> intro -> display -> engine
    //         -> session frames -> teardown -> destroy window.
    CHECK(ordered(log, {
        // window
        "plat.createWindow.fs",
        // InitSubsystemsAndMovieDll (0x527de0)
        "errorLogInit", "memoryInitTracker", "memPoolStartupStack", "loadMovieDll",
        "fileCreateDirectory", "vfsInit", "timeBaseStartTimer",
        // intro
        "moviePlayIntroSequence",
        // InitDisplayAndPaths (0x527fa4)
        "configReadGfxAndSound", "renderEnumDisplayModes", "gfx.init",
        "renderInitEngineDevice", "universeCreateDefaultCameras", "inputDInput.fs",
        "renderSetAssetPaths", "renderApplyGfxSettings", "guiLoadGfxFile",
        "widgetInitSystem",
        // InitEngineAndScriptCommands (0x528560)
        "textLoadDefinitionFile", "netConnectToServer", "commandQueueInitAndSync",
        "worldLoadBuildingAndObjectData", "buildingComputeMarketPrices",
        "audioStartupMilesDriver", "soundLibInit", "sound3dInitPool",
        "soundWaveInitSineTables", "soundLoadSampleBank", "soundPreloadIncludeFile",
        "soundInitMusicThread", "audioApplyVolumeSettings", "scriptRegisterCommands",
        // session frames (at least the first frame's pump)
        "f.inputLatchAndPump",
        // teardown (13) in order
        "td.GameShutdownSubsystems", "td.WidgetShutdownSystem",
        "td.ConfigWriteGfxSettings", "td.GameStateFreeAllResources",
        "td.UniverseSwitchActiveSlot0", "td.TableResetLightmaps",
        "td.RenderShutdownEngine", "td.InputDirectInputShutdown",
        "td.TimeBaseStopTimer", "td.VfsShutdown", "td.MemPoolShutdownStack",
        "td.MemoryShutdownTracker", "td.ErrorLogShutdown",
        // movie dll exit + window destroy tail
        "movieDllExit", "plat.destroyWindow"}));

    // ---- 2. N frames actually ran: the always-pump fired once per frame.
    CHECK_EQ(log.count("f.inputLatchAndPump"), N);

    // ---- 3. The session's live mask enabled the expected steps each frame.
    //         (liveMask in InitOrLoadSession = input+widget+render+gameObjects+
    //          hudMouse+scripts+network+hudLabels+optionsPanels.)
    CHECK_EQ(log.count("f.widgetMouse"), N);
    CHECK_EQ(log.count("f.renderWorld"), N);
    CHECK_EQ(log.count("f.gameObjects"), N);
    CHECK_EQ(log.count("f.scripts"), N);
    CHECK_EQ(log.count("f.networkCommand"), N);
    CHECK_EQ(log.count("f.hudLabels"), N);
    CHECK_EQ(log.count("f.optionsPanels"), N);
    CHECK_EQ(log.count("f.present"), N); // present runs because gameObjects set
    // Disabled-in-live-mask steps did not run during frames.
    CHECK_EQ(log.count("f.weatherSky"), 0);
    CHECK_EQ(log.count("f.tooltips"), 0);
    CHECK_EQ(log.count("f.combatScroll"), 0);
    // characterCollect runs each frame (no kCombatSelect), autosave each frame
    // (no kAutosaveSuppress).
    CHECK_EQ(log.count("f.characterCollect"), N);
    CHECK_EQ(log.count("f.autosave"), N);

    // ---- 4. Teardown ran exactly once and after the last frame.
    CHECK_EQ(log.count("td.ErrorLogShutdown"), 1);
    CHECK(log.index("td.GameShutdownSubsystems") > log.lastIndex("f.inputLatchAndPump"));
    // window destroyed last.
    CHECK_EQ(log.lastIndex("plat.destroyWindow"), static_cast<int>(log.calls.size()) - 1);
}

// Network-client lifecycle: InitOrLoadSession takes the network branch (host
// connect) before the turn frames.
TEST(AppSpineE2E, NetworkClientLifecycle) {
    CallLog log;
    MockPlatform plat(log); MockGfx gfx(log); MockAudio audio; MockSubsystems sub(log);
    app::GameApp appObj(plat, gfx, audio, sub);
    appObj.Run("exe", /*mode*/ 1, /*intro*/ false, /*net*/ true, /*frames*/ 2);

    // Windowed mode -> DInput mode 6, no fullscreen window.
    CHECK(log.index("inputDInput.win") >= 0);
    CHECK(log.index("plat.createWindow.win") >= 0);
    // No intro path.
    CHECK(log.index("moviePlayIntroSequence") < 0);
    CHECK(log.index("movieDllExit") < 0);
    // 2 frames ran, teardown closed out.
    CHECK_EQ(log.count("f.inputLatchAndPump"), 2);
    CHECK_EQ(log.count("td.ErrorLogShutdown"), 1);
}

// Unit tests for the VIBE_App engine/script init orchestration reconstruction.
// gilde.exe 0x528560 / 0x527d48 / 0x527d8c / 0x527db8.
//
// Golden vectors: the exact ordered sequence of subsystem-init / registration
// calls the orchestrator emits, recovered from the Hex-Rays decompile. A
// recording hook captures the order; the test pins it.

#include "tests/framework/test.h"

#include "app/engine_init_app_recon.h"
#include "world/guildstate_recon.h"   // GuardStateGlobal / GuardState (0x4520d0 target)
#include "sim/ai_needs.h"            // kAiNeedsDfnTotalBytes / kAiNeedsRecordCount

#include <memory>
#include <string>
#include <vector>

namespace {

using namespace guild::app;

// Build a hook table that appends a label (and any payload) to `log` on each
// call, so we can assert the exact call ORDER.
EngineInitHooks MakeRecordingHooks(std::vector<std::string>& log,
                                   bool sfxLibInitFails = false,
                                   bool fadeNeedsFrames = false) {
    EngineInitHooks h;
    h.textBuildTextArray = [&](const std::string& p) {
        log.push_back("textBuildTextArray:" + p);
    };
    h.textLoadDefinitionFile = [&](const std::string& p) {
        log.push_back("textLoadDefinitionFile:" + p);
        return 1;
    };
    h.errorLogReportMessage = [&](const std::string& m) {
        log.push_back("errorLogReportMessage:" + m);
    };
    h.historyFreeChronicleFiles = [&] { log.push_back("historyFreeChronicleFiles"); };
    h.gameTickFinalize = [&](const std::string& f) {
        log.push_back("gameTickFinalize:" + f);
        return 42; // form handle
    };
    h.windowPositionCentered = [&](int hnd, int mode) {
        log.push_back("windowPositionCentered:" + std::to_string(hnd) + "," +
                      std::to_string(mode));
    };
    h.widgetLayoutBounds = [&](int, int, int) { log.push_back("widgetLayoutBounds"); };
    h.textRenderRichString = [&](unsigned, int) { log.push_back("textRenderRichString"); };
    h.netConnectToServer = [&](int, int) { log.push_back("netConnectToServer"); };
    h.commandQueueInitAndSync = [&] { log.push_back("commandQueueInitAndSync"); };
    h.buildingDeselectThunk = [&] { log.push_back("buildingDeselectThunk"); };
    h.worldLoadBuildingAndObjectData = [&](const std::string& p) {
        log.push_back("worldLoadBuildingAndObjectData:" + p);
    };
    h.buildingComputeMarketPrice = [&](guild::u16 prot, guild::u8 qty) {
        log.push_back("computeMarketPrice:" + std::to_string(prot) + "," +
                      std::to_string(static_cast<int>(qty)));
        return 0.0f;
    };
    h.audioStartupMilesDriver = [&] { log.push_back("audioStartupMilesDriver"); };
    h.soundLibInit = [&] {
        log.push_back("soundLibInit");
        return sfxLibInitFails ? 1 : 0;
    };
    h.sound3dInitPool = [&](int n) {
        log.push_back("sound3dInitPool:" + std::to_string(n));
    };
    h.soundWaveInitSineTables = [&](guild::u32 n) {
        log.push_back("soundWaveInitSineTables:" + std::to_string(n));
    };
    h.soundSetBankPath = [&](const std::string& p) {
        log.push_back("soundSetBankPath:" + p);
    };
    h.soundLoadSampleBank = [&](const std::string& n) {
        log.push_back("soundLoadSampleBank:" + n);
        return 0;
    };
    h.soundPreloadFromIncludeFile = [&](const std::string& p) {
        log.push_back("soundPreloadFromIncludeFile:" + p);
    };
    h.soundInitThread = [&] { log.push_back("soundInitThread"); };
    h.audioSetTrackNamePrefix = [&](const std::string& p) {
        log.push_back("audioSetTrackNamePrefix:" + p);
    };
    h.audioApplyVolumeSettings = [&] { log.push_back("audioApplyVolumeSettings"); };
    h.cutsceneRegisterTickProc = [&] { log.push_back("cutsceneRegisterTickProc"); };
    h.objectResetSpawnTables = [&] { log.push_back("objectResetSpawnTables"); };
    h.charActionRegisterHandlers = [&] { log.push_back("charActionRegisterHandlers"); };
    h.renderBuildSnowTexture = [&] { log.push_back("renderBuildSnowTexture"); };
    h.gameLogicInitGuardState = [&] { log.push_back("gameLogicInitGuardState"); };
    h.combatInitDefaultParameters = [&] { log.push_back("combatInitDefaultParameters"); };
    h.formDestroy = [&](int hnd) {
        log.push_back("formDestroy:" + std::to_string(hnd));
    };
    h.fadeRegister = [&](guild::i32, guild::i32, guild::i32, guild::i32,
                         const std::string& pal, guild::i32 dur, guild::i32 mode) {
        log.push_back("fadeRegister:" + pal + "," + std::to_string(dur) + "," +
                      std::to_string(mode));
        return 7; // fade id
    };
    if (fadeNeedsFrames) {
        // Report "not done" for the first two queries, then done.
        auto counter = std::make_shared<int>(0);
        h.fadeQueryFlags = [&log, counter](int) -> guild::u8 {
            log.push_back("fadeQueryFlags");
            int n = (*counter)++;
            return n < 2 ? 0 : kFadeDoneBit;
        };
    } else {
        h.fadeQueryFlags = [&](int) -> guild::u8 {
            log.push_back("fadeQueryFlags");
            return kFadeDoneBit;
        };
    }
    h.gameLogicRunFrameLoop = [&](guild::i32 a, guild::i32 b, int form) {
        log.push_back("runFrameLoop:" + std::to_string(a) + "," +
                      std::to_string(b) + "," + std::to_string(form));
    };
    h.fadeUnregister = [&](int id) {
        log.push_back("fadeUnregister:" + std::to_string(id));
    };
    h.scriptConsoleParseLine = [&] { log.push_back("scriptConsoleParseLine"); };
    h.scriptRegisterCommands = [&] { log.push_back("scriptRegisterCommands"); };
    h.scriptRegisterObjectCommands = [&] { log.push_back("scriptRegisterObjectCommands"); };
    h.characterRegisterScriptCommands = [&] { log.push_back("characterRegisterScriptCommands"); };
    h.scriptRegisterSoundCommands = [&] { log.push_back("scriptRegisterSoundCommands"); };
    h.configApplyCameraAndScrollSettings = [&] { log.push_back("configApplyCameraAndScroll"); };
    h.windowPumpMessages = [&] { log.push_back("windowPumpMessages"); };
    return h;
}

} // namespace

// --- 0x528560: full retail path (no sfx, no music) --------------------------
TEST(AppReconEngineInit, RetailOrderNoAudio) {
    std::vector<std::string> log;
    EngineInitHooks h = MakeRecordingHooks(log);
    EngineInitGlobals g; // editor=false, sfx=false, music=false

    int rc = VIBE_App_InitEngineAndScriptCommands(g, h);
    CHECK_EQ(rc, 1);

    const std::vector<std::string> expected = {
        "textLoadDefinitionFile:\\project\\game\\gilde_text.def",
        "historyFreeChronicleFiles",
        "gameTickFinalize:Misc\\Loading_main",
        "windowPositionCentered:42,3",
        "widgetLayoutBounds",
        "windowPositionCentered:42,1",
        "textRenderRichString",
        "netConnectToServer",
        "commandQueueInitAndSync",
        "buildingDeselectThunk",
        "worldLoadBuildingAndObjectData:\\project\\game\\data\\",
        "computeMarketPrice:469,0",
        "computeMarketPrice:471,100",
        "computeMarketPrice:468,100",
        "computeMarketPrice:474,100",
        "computeMarketPrice:473,100",
        "computeMarketPrice:470,100",
        "computeMarketPrice:472,100",
        // no audio (sfx/music gates off)
        "audioApplyVolumeSettings",
        "cutsceneRegisterTickProc",
        "objectResetSpawnTables",
        "charActionRegisterHandlers",
        "renderBuildSnowTexture",
        "gameLogicInitGuardState",
        "combatInitDefaultParameters",
        "formDestroy:42",
        "fadeRegister:BLACK,90,1",
        "fadeQueryFlags", // done immediately -> no frame loop iterations
        "fadeUnregister:7",
        "scriptConsoleParseLine",
        "scriptRegisterCommands",
        "scriptRegisterObjectCommands",
        "characterRegisterScriptCommands",
        "scriptRegisterSoundCommands",
        "configApplyCameraAndScroll",
        "windowPumpMessages",
    };
    CHECK_EQ(log.size(), expected.size());
    for (size_t i = 0; i < expected.size() && i < log.size(); ++i)
        CHECK_EQ(log[i], expected[i]);
}

// --- 0x528560: sfx + music enabled, sfx lib init succeeds -------------------
TEST(AppReconEngineInit, AudioEnabledOrder) {
    std::vector<std::string> log;
    EngineInitHooks h = MakeRecordingHooks(log, /*sfxLibInitFails=*/false);
    EngineInitGlobals g;
    g.sfxEnabled = true;
    g.musicEnabled = true;

    VIBE_App_InitEngineAndScriptCommands(g, h);

    // Find the audio block and verify its internal order verbatim.
    auto idx = [&](const std::string& s) -> int {
        for (size_t i = 0; i < log.size(); ++i)
            if (log[i] == s) return static_cast<int>(i);
        return -1;
    };
    // sfx block
    CHECK(idx("audioStartupMilesDriver") >= 0);
    CHECK(idx("soundLibInit") > idx("audioStartupMilesDriver"));
    CHECK(idx("sound3dInitPool:64") > idx("soundLibInit"));
    CHECK(idx("soundWaveInitSineTables:48") > idx("sound3dInitPool:64"));
    CHECK(idx("soundSetBankPath:\\project\\game\\sfx\\") >
          idx("soundWaveInitSineTables:48"));
    CHECK(idx("soundLoadSampleBank:system.sbf") >
          idx("soundSetBankPath:\\project\\game\\sfx\\"));
    CHECK(idx("soundPreloadFromIncludeFile:\\include_sfx.ini") >
          idx("soundLoadSampleBank:system.sbf"));
    // music block follows sfx, before applyVolume
    CHECK(idx("soundInitThread") > idx("soundPreloadFromIncludeFile:\\include_sfx.ini"));
    CHECK(idx("audioSetTrackNamePrefix:\\project\\game\\msx\\") > idx("soundInitThread"));
    CHECK(idx("audioApplyVolumeSettings") >
          idx("audioSetTrackNamePrefix:\\project\\game\\msx\\"));
}

// --- 0x528560: Sound_LibInit failure clears the MUSIC gate too ---------------
// gilde.exe 0x528779..0x52878a: if ( VIBE_Sound_LibInit(...) ) { dword_63C900 =
// 0; dword_63C8F8 = 0; } — and the music block re-reads dword_63C8F8 at
// 0x528800, so a failed lib init must SKIP soundInitThread/audioSetTrackNamePrefix
// even when music was enabled going in.
TEST(AppReconEngineInit, SfxLibInitFailureSkipsMusicBringup) {
    std::vector<std::string> log;
    EngineInitHooks h = MakeRecordingHooks(log, /*sfxLibInitFails=*/true);
    EngineInitGlobals g;
    g.sfxEnabled = true;
    g.musicEnabled = true;

    VIBE_App_InitEngineAndScriptCommands(g, h);

    auto idx = [&](const std::string& s) -> int {
        for (size_t i = 0; i < log.size(); ++i)
            if (log[i] == s) return static_cast<int>(i);
        return -1;
    };
    // The rest of the sfx block still runs after the failed lib init (the
    // original does not early-out; it only clears the gates).
    CHECK(idx("soundLibInit") >= 0);
    CHECK(idx("sound3dInitPool:64") > idx("soundLibInit"));
    CHECK(idx("soundPreloadFromIncludeFile:\\include_sfx.ini") >= 0);
    // Music bring-up is skipped: dword_63C8F8 was cleared at 0x52878a.
    CHECK(idx("soundInitThread") == -1);
    CHECK(idx("audioSetTrackNamePrefix:\\project\\game\\msx\\") == -1);
    // The unconditional tail still runs.
    CHECK(idx("audioApplyVolumeSettings") >= 0);
}

// --- 0x528560: editor text mode takes BuildTextArray, not LoadDefinitionFile -
TEST(AppReconEngineInit, EditorTextMode) {
    std::vector<std::string> log;
    EngineInitHooks h = MakeRecordingHooks(log);
    EngineInitGlobals g;
    g.editorTextMode = true;

    VIBE_App_InitEngineAndScriptCommands(g, h);

    bool sawBuild = false, sawLoad = false;
    for (const auto& s : log) {
        if (s == "textBuildTextArray:\\project\\game\\gilde_text.def") sawBuild = true;
        if (s.rfind("textLoadDefinitionFile", 0) == 0) sawLoad = true;
    }
    CHECK(sawBuild);
    CHECK(!sawLoad);
}

// --- 0x528560: def-file load failure reports the open-engine error ----------
TEST(AppReconEngineInit, TextLoadFailureReportsError) {
    std::vector<std::string> log;
    EngineInitHooks h = MakeRecordingHooks(log);
    h.textLoadDefinitionFile = [&](const std::string& p) {
        log.push_back("textLoadDefinitionFile:" + p);
        return 0; // fail
    };
    EngineInitGlobals g;

    VIBE_App_InitEngineAndScriptCommands(g, h);

    bool sawErr = false;
    for (const auto& s : log)
        if (s == std::string("errorLogReportMessage:") + kErrOpenEngine) sawErr = true;
    CHECK(sawErr);
}

// --- 0x528560: the BLACK fade wait loop runs frames until the done bit -------
TEST(AppReconEngineInit, FadeWaitLoopRunsFramesUntilDone) {
    std::vector<std::string> log;
    EngineInitHooks h = MakeRecordingHooks(log, /*sfxLibInitFails=*/false,
                                           /*fadeNeedsFrames=*/true);
    EngineInitGlobals g;

    VIBE_App_InitEngineAndScriptCommands(g, h);

    // Two "not done" queries -> exactly two frame-loop iterations with the
    // verbatim args (147591, 147591, formHandle=42), then a third query (done).
    int frames = 0, queries = 0;
    for (const auto& s : log) {
        if (s == "fadeQueryFlags") ++queries;
        if (s == "runFrameLoop:147591,147591,42") ++frames;
    }
    CHECK_EQ(frames, 2);
    CHECK_EQ(queries, 3);
}

// --- 0x528560: the five script-command registrations in exact order ---------
TEST(AppReconEngineInit, ScriptRegistrationOrder) {
    std::vector<std::string> log;
    EngineInitHooks h = MakeRecordingHooks(log);
    EngineInitGlobals g;

    VIBE_App_InitEngineAndScriptCommands(g, h);

    const std::vector<std::string> seq = {
        "scriptConsoleParseLine", "scriptRegisterCommands",
        "scriptRegisterObjectCommands", "characterRegisterScriptCommands",
        "scriptRegisterSoundCommands",
    };
    // Extract just the script-register subsequence and compare.
    std::vector<std::string> got;
    for (const auto& s : log)
        for (const auto& want : seq)
            if (s == want) got.push_back(s);
    CHECK_EQ(got.size(), seq.size());
    for (size_t i = 0; i < seq.size() && i < got.size(); ++i)
        CHECK_EQ(got[i], seq[i]);
}

// --- 0x527d48: single-instance mutex, app not already running ---------------
TEST(AppReconMutex, NotAlreadyRunning) {
    PlatformInitHooks plat;
    plat.createNamedMutex = [](const std::string& name) {
        CHECK_EQ(name, std::string("Die Gilde"));
        return PlatformInitHooks::MutexResult{reinterpret_cast<void*>(0x1234),
                                              /*alreadyExisted=*/false};
    };
    void* handle = nullptr;
    int rc = VIBE_App_CreateSingleInstanceMutex(&handle, plat);
    CHECK_EQ(rc, 0);
    CHECK(handle == reinterpret_cast<void*>(0x1234));
}

// --- 0x527d48: already running -> returns 1 ---------------------------------
TEST(AppReconMutex, AlreadyRunning) {
    PlatformInitHooks plat;
    plat.createNamedMutex = [](const std::string&) {
        return PlatformInitHooks::MutexResult{reinterpret_cast<void*>(0x1),
                                              /*alreadyExisted=*/true};
    };
    void* handle = nullptr;
    int rc = VIBE_App_CreateSingleInstanceMutex(&handle, plat);
    CHECK_EQ(rc, 1);
}

// --- 0x527d48: create failure -> returns -1, handle untouched ---------------
TEST(AppReconMutex, CreateFailureReturnsMinusOne) {
    PlatformInitHooks plat;
    plat.createNamedMutex = [](const std::string&) {
        return PlatformInitHooks::MutexResult{nullptr, false};
    };
    void* handle = reinterpret_cast<void*>(0xDEAD);
    int rc = VIBE_App_CreateSingleInstanceMutex(&handle, plat);
    CHECK_EQ(rc, -1);
    CHECK(handle == reinterpret_cast<void*>(0xDEAD)); // not overwritten
}

// --- 0x527d8c: close handle thunk forwards the handle -----------------------
TEST(AppReconMutex, CloseHandleForwards) {
    PlatformInitHooks plat;
    void* seen = nullptr;
    plat.closeHandle = [&](void* h) {
        seen = h;
        return 1;
    };
    int rc = VIBE_App_CloseHandleThunk(reinterpret_cast<void*>(0xBEEF), plat);
    CHECK_EQ(rc, 1);
    CHECK(seen == reinterpret_cast<void*>(0xBEEF));
}

// --- 0x527db8: screensaver refresh maps a1->word2, a2->word1, SPI=0x10 ------
TEST(AppReconScreensaver, MapsArgsAndAction) {
    PlatformInitHooks plat;
    unsigned action = 0;
    int w1 = -1, w2 = -1;
    plat.systemParametersInfo = [&](unsigned a, int word1, int word2) {
        action = a;
        w1 = word1;
        w2 = word2;
    };
    VIBE_App_RefreshScreensaverSetting(/*a1=*/111, /*a2=*/222, plat);
    CHECK_EQ(static_cast<int>(action), 0x10); // SPI_SETSCREENSAVEACTIVE
    CHECK_EQ(w1, 222); // pvParam[1] = a2
    CHECK_EQ(w2, 111); // pvParam[2] = a1
}

// gilde.exe 0x4520d0 — the fused guard-state init (AI-data load -> InitGuardState),
// the rule-13 default for h.gameLogicInitGuardState. With a full DFN provider the
// AI catalog loads and the guard-state table is initialised (return-1 path); with
// an empty provider the load fails and the table is left untouched (return-0 path).
TEST(AppReconGuardState, FullDfnProviderInitsGuardTable) {
    // Reset the shared guard state.
    guild::world::GuardStateGlobal() = guild::world::GuardState{};
    CHECK(!guild::world::GuardStateGlobal().tableValid);

    EngineInitHooks h;  // defaults; install only the DFN provider.
    h.aiDataDfnProvider = [] {
        // A complete 61x73 DFN (all-zero records: a valid full read -> 1).
        return std::vector<guild::u8>(guild::sim::kAiNeedsDfnTotalBytes, 0);
    };

    RealGameLogicInitGuardState(h);

    // The success path ran InitGuardState's table init verbatim.
    const guild::world::GuardState& g = guild::world::GuardStateGlobal();
    CHECK(g.tableValid);
    CHECK_EQ((int)g.wFAE, 340);
    CHECK_EQ((int)g.wFAC, 342);
    CHECK_EQ((int)g.wFCE, 352);
    CHECK_EQ((int)g.stamp.hour, 6);   // GameTime_Set(&stamp, 6, 0, 0)
}

TEST(AppReconGuardState, NoProviderTakesFailurePath) {
    guild::world::GuardStateGlobal() = guild::world::GuardState{};

    EngineInitHooks h;  // default aiDataDfnProvider returns empty bytes.
    RealGameLogicInitGuardState(h);

    // Load failed -> InitGuardState leaves the table untouched (return 0 path),
    // but still seeds the cleared stamp via GameTime_Set (6,0,0).
    const guild::world::GuardState& g = guild::world::GuardStateGlobal();
    CHECK(!g.tableValid);
    CHECK_EQ((int)g.wFAE, 0);
    CHECK_EQ((int)g.stamp.hour, 6);
}

TEST(AppReconGuardState, ShortDfnTakesFailurePath) {
    guild::world::GuardStateGlobal() = guild::world::GuardState{};

    EngineInitHooks h;
    h.aiDataDfnProvider = [] {
        // One byte short of 61 full records -> AiNeeds_LoadDataFile returns 0.
        return std::vector<guild::u8>(guild::sim::kAiNeedsDfnTotalBytes - 1, 0);
    };
    RealGameLogicInitGuardState(h);

    CHECK(!guild::world::GuardStateGlobal().tableValid);
}

// The default hook binding: when a caller leaves gameLogicInitGuardState unset,
// VIBE_App_InitEngineAndScriptCommands installs RealGameLogicInitGuardState, so
// the orchestrator's InitGuardState step drives the real edge (not a no-op).
TEST(AppReconGuardState, OrchestratorBindsRealDefaultWhenUnset) {
    guild::world::GuardStateGlobal() = guild::world::GuardState{};

    EngineInitHooks h;  // gameLogicInitGuardState left default (empty std::function)
    h.aiDataDfnProvider = [] {
        return std::vector<guild::u8>(guild::sim::kAiNeedsDfnTotalBytes, 0);
    };
    CHECK(!h.gameLogicInitGuardState);   // unset before the orchestrator runs

    EngineInitGlobals g;
    VIBE_App_InitEngineAndScriptCommands(g, h);

    CHECK(h.gameLogicInitGuardState != nullptr);     // bound by the orchestrator
    CHECK(guild::world::GuardStateGlobal().tableValid); // and it ran the real edge
}

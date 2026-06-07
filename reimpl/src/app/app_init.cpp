// gilde.exe — app spine: window creation, subsystem init, session bootstrap,
// the outer Run() flow and the verbatim 13-step shutdown teardown.
// Namespace guild::app. See gamelogic.h / app_init.h for the bit/order tables.
#include "app/app_init.h"
#include "app/gamelogic.h"

namespace guild::app {

GameApp::GameApp(shim::IPlatform& plat, shim::IGraphicsDevice& gfx,
                 shim::IAudioDevice& audio, ISubsystems& sub)
    : plat_(plat), gfx_(gfx), audio_(audio), sub_(sub) {}

// gilde.exe 0x52895c — VIBE_Window_CreateMainWindow(hInstance, mode).
// RegisterClassA("Die Gilde") + CreateWindowExA. mode 3/4 -> fullscreen
// (WS_POPUP|WS_VISIBLE, WS_EX_TOPMOST), else windowed (WS_OVERLAPPED-ish). Here
// the OS-specific window plumbing is routed through shim::IPlatform; the
// fullscreen-vs-windowed decision (the only logic the spine depends on) is
// preserved.
bool GameApp::CreateMainWindow(int mode) {
    const bool fullscreen = (mode == 3 || mode == 4);
    // Title in the original is "Die Gilde - <buildversion>".
    return plat_.createMainWindow("Die Gilde", resW_, resH_, fullscreen);
}

// gilde.exe 0x527de0 — VIBE_App_InitSubsystemsAndMovieDll.
// Order (verbatim): ErrorLog_Init -> Memory_InitTracker(32678) ->
// MemPool_StartupStack(0x80) -> [if show_intro] LoadLibrary("moveahead.dll") +
// resolve mov_* exports -> CreateDirectory("<gfx>gamedata\Screenshots") ->
// Vfs_Init(gfxPath) -> TimeBase_StartTimer(0x0E,0). Returns 1.
bool GameApp::InitSubsystemsAndMovieDll() {
    sub_.errorLogInit();
    sub_.memoryInitTracker(32678);
    sub_.memPoolStartupStack(0x80);
    if (showIntro_) {
        // moveahead.dll: success means mov_* exports were resolved.
        movieDll_ = sub_.loadMovieDll();
    }
    sub_.fileCreateDirectory("\\project\\gfx\\gamedata\\Screenshots");
    sub_.vfsInit("\\project\\gfx\\");
    sub_.timeBaseStartTimer(0x0E, 0);
    return true;
}

// gilde.exe 0x527fa4 — VIBE_Render_InitDisplayAndPaths(mode).
// Order: Config_ReadGfxAndSoundSettings -> Render_EnumDisplayModes (fail -> 0)
// -> pick cur_res -> [window rect via AdjustWindowRectEx/SetWindowPos, here the
// shim graphics init] -> Render_InitEngineDevice (fail -> 0) ->
// Universe_CreateDefaultCameras -> ShowCursor(0) ->
// Input_DirectInputInit(mode==3?1:6) -> Config_ApplyCameraAndScroll ->
// set asset sub-paths (textures/animations/objects/groups/scripts/forms) ->
// Render_ApplyGfxSettings -> Gui_LoadGfxFile("gilde.gfx") (fail -> 0) ->
// Widget_InitSystem. Returns 1 on success.
bool GameApp::InitDisplayAndPaths(int mode) {
    sub_.configReadGfxAndSound();
    if (!sub_.renderEnumDisplayModes())
        return false;

    const bool fullscreen = (mode == 3 || mode == 4);
    // The original fills a 24-dword device descriptor (width,height,bpp=16,...);
    // bpp is fixed at 16 (v27[2] = 16).
    if (!gfx_.init(resW_, resH_, 16, fullscreen))
        return false;
    if (!sub_.renderInitEngineDevice(resW_, resH_, 16, fullscreen))
        return false;

    sub_.universeCreateDefaultCameras();
    // DirectInput mode: 1 = fullscreen, 6 = windowed.
    sub_.inputDirectInputInit(mode == 3 ? 1 : 6);
    sub_.renderSetAssetPaths();
    sub_.renderApplyGfxSettings();
    if (!sub_.guiLoadGfxFile("gilde.gfx"))
        return false;
    sub_.widgetInitSystem();
    return true;
}

// gilde.exe 0x528560 — VIBE_App_InitEngineAndScriptCommands.
// Order: Text_LoadDefinitionFile("<game>gilde_text.def") -> loading screen ->
// Net_ConnectToServer(0,0) [local stub] -> Command_QueueInitAndSync ->
// World_LoadBuildingAndObjectData("<game>data\") -> compute market prices ->
// [audio bring-up, see below] -> register script command tables ->
// initial fade -> ApplyCameraAndScroll. Returns 1.
//
// Audio bring-up: if sfx enabled -> Audio_StartupMilesDriver,
// Sound_LibInit(2097100,48,2,44100), Sound3d_InitPool(64),
// SoundWave_InitSineTables, bank "<game>sfx\", load system.sbf,
// preload "<exeDir>\include_sfx.ini". If music enabled ->
// Sound_InitThread(2,...,44100), track prefix "<game>msx\".
bool GameApp::InitEngineAndScriptCommands() {
    if (!sub_.textLoadDefinitionFile("\\project\\game\\gilde_text.def")) {
        // original: VIBE_ErrorLog_ReportMessage("main_OpenEngine(): could not
        // open txt-engine!") then continues.
    }
    sub_.netConnectToServer("", 0); // VIBE_Net_ConnectToServer(0,0) = local
    sub_.commandQueueInitAndSync();
    sub_.worldLoadBuildingAndObjectData("\\project\\game\\data\\");
    sub_.buildingComputeMarketPrices();

    // sfx-enabled gate (dword_63C900). Music gate is dword_63C8F8.
    sub_.audioStartupMilesDriver();
    if (sub_.soundLibInit(48, 2, 44100)) {
        sub_.sound3dInitPool(64);
        sub_.soundWaveInitSineTables();
        sub_.soundLoadSampleBank("\\project\\game\\sfx\\system.sbf");
        sub_.soundPreloadIncludeFile("include_sfx.ini");
    }
    sub_.soundInitMusicThread(44100);
    sub_.audioApplyVolumeSettings();

    sub_.scriptRegisterCommands();
    return true;
}

// gilde.exe 0x533a54 — VIBE_GameLogic_InitOrLoadSession (bootstrap).
// The original branches on word_63C740 (session::*): network host loads
// Server.dll & opens the broadcast socket; new game loads a .cty city seed;
// load game reads the .SAV; then runs the lockstep turn loop. Here we model the
// bootstrap dispatch and drive the per-round frames through RunFrameLoop with a
// representative live-game mask, so the spine's frame dispatch is exercised.
void GameApp::InitOrLoadSession(std::uint16_t sessionFlags, int framesPerSession) {
    if ((sessionFlags & session::kNetwork) && (sessionFlags & session::kHost)) {
        // LoadLibrary(Server.dll) + Init_ + broadcast socket (modeled as a
        // network connect via the net hook).
        sub_.netConnectToServer("host", 7531);
    }
    sub_.commandQueueInitAndSync();
    sub_.worldLoadBuildingAndObjectData(""); // VIBE_Game_InitWorldAndSounds proxy

    // Turn loop: a live, non-headless game frame mask. This combines the bits a
    // normal interactive single-player frame runs under (world+render+HUD+sim+
    // input+net pump). Tests drive specific masks directly via RunFrameLoop.
    const std::uint32_t liveMask =
        mask::kInputCommandPoll | mask::kWidgetMouse | mask::kRenderWorld |
        mask::kGameObjects | mask::kHudMouse | mask::kScripts |
        mask::kNetworkCommand | mask::kHudLabels | mask::kOptionsAndPanels;
    for (int i = 0; i < framesPerSession; ++i)
        RunFrameLoop(liveMask);
}

// The verbatim 13-step shutdown teardown. This block appears identically at
// EVERY exit of VIBE_GameLogic_MainEntryAndShutdown @0x534bbc; it IS the
// teardown order (recon §1.12). Steps map 1:1 to TeardownStep.
void GameApp::Shutdown() {
    if (shutdownDone_)
        return;
    shutdownDone_ = true;

    // The 13 core steps, in exact order. Routed through dedicated ISubsystems
    // teardown hooks named for the original functions so the order is testable.
    sub_.tdGameShutdownSubsystems();   //  1  VIBE_Game_ShutdownSubsystems  0x5278cc
    sub_.tdWidgetShutdownSystem();     //  2  VIBE_Widget_ShutdownSystem    0x4201f4
    sub_.tdConfigWriteGfxSettings();   //  3  VIBE_Config_WriteGfxSettings  0x56af54
    sub_.tdGameStateFreeAllResources();//  4  VIBE_GameState_FreeAllResources 0x40e308
    sub_.tdUniverseSwitchActiveSlot0();//  5  VIBE_Universe_SwitchActiveSlot(0) 0x5b4a24
    sub_.tdTableResetLightmaps();      //  6  VIBE_Table_ResetLightmaps     0x42e19c
    sub_.tdRenderShutdownEngine();     //  7  VIBE_Render_ShutdownEngine    0x5b0228
    sub_.tdInputDirectInputShutdown(); //  8  VIBE_Input_DirectInputShutdown 0x40cd40
    sub_.tdTimeBaseStopTimer();        //  9  VIBE_TimeBase_StopTimer       0x44e2c4
    sub_.tdVfsShutdown();              // 10  VIBE_Vfs_Shutdown             0x452004
    sub_.tdMemPoolShutdownStack();     // 11  VIBE_MemPool_ShutdownStack    0x44e544
    sub_.tdMemoryShutdownTracker();    // 12  VIBE_Memory_ShutdownTracker   0x439640
    sub_.tdErrorLogShutdown();         // 13  VIBE_ErrorLog_Shutdown        0x438c0c

    // Conditional tails (post the 13 core steps):
    if (showIntro_ && movieDll_)
        sub_.movieDllExit();               // mov_Exit_ + FreeLibrary
    plat_.destroyMainWindow();             // VIBE_Window_DestroyAndUnregisterClass
}

// gilde.exe 0x534bbc — VIBE_GameLogic_MainEntryAndShutdown (the spine).
int GameApp::Run(const std::string& /*exeDir*/, int displayMode, bool showIntro,
                 bool networkClient, int framesPerSession) {
    displayMode_ = displayMode;
    showIntro_ = showIntro;

    // (mutex single-instance guard / screensaver disable happen before this in
    // the original; modeled by the host adapter.)

    if (!CreateMainWindow(displayMode_)) { Shutdown(); return 0; }
    if (!InitSubsystemsAndMovieDll())    { Shutdown(); return 0; }
    if (showIntro_)
        sub_.moviePlayIntroSequence();    // VIBE_Movie_PlayIntroSequence @0x5347d4
    if (!InitDisplayAndPaths(displayMode_)) { Shutdown(); return 0; }
    if (!InitEngineAndScriptCommands())     { Shutdown(); return 0; }

    // Outer loop. The original runs the main menu / auto-start, then iterates
    // over dword_63CC34 players calling InitOrLoadSession. We run a single
    // bounded session (network or single) so the lifecycle is deterministic.
    const std::uint16_t flags = networkClient
        ? (session::kNetwork)
        : (session::kNewGame);
    InitOrLoadSession(flags, framesPerSession);

    Shutdown();
    return 0;
}

} // namespace guild::app

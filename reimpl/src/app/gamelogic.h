#pragma once
// gilde.exe — application spine (guild::app).
//
// This module is the program's top-level structure, translated 1:1 from:
//   VIBE_GameLogic_MainEntryAndShutdown  @0x534bbc  (WinMain)
//   VIBE_App_InitSubsystemsAndMovieDll   @0x527de0
//   VIBE_Render_InitDisplayAndPaths      @0x527fa4
//   VIBE_App_InitEngineAndScriptCommands @0x528560
//   VIBE_Window_CreateMainWindow         @0x52895c
//   VIBE_GameLogic_RunFrameLoop          @0x4c09a0  (feature-mask gated)
//   VIBE_GameLogic_InitOrLoadSession     @0x533a54
//
// The original makes direct Win32 / DDraw / DInput / Miles calls and calls into
// ~14 sibling subsystems (render, gui, sim, world, audio, net, io/vfs, mem,
// crt, ...). To keep the spine 1:1 yet platform-neutral and testable
// (AGENT_GUIDE: "NO direct OS/vendor calls in src/"; "Subsystem entry points
// that exist -> call them; ones that don't -> forward-declare a hook and
// document"), every OS interaction goes through shim::IPlatform /
// IGraphicsDevice / IAudioDevice, and every sibling-subsystem entry point goes
// through the ISubsystems hook interface below. A real host adapter wires those
// hooks to the actual subsystem functions; a test wires them to a recorder.
//
// The control flow, init order, the verbatim 13-step shutdown teardown, the
// session-flag bitmask, and the frame-loop feature-mask bit checks are all
// preserved exactly as in the binary.
#include "shim/IAudioDevice.h"
#include "shim/IGraphicsDevice.h"
#include "shim/IPlatform.h"

#include <cstdint>
#include <string>

namespace guild::app {

// ===========================================================================
// Frame-loop feature mask  (VIBE_GameLogic_RunFrameLoop @0x4c09a0)
// ---------------------------------------------------------------------------
// The frame loop receives a 32-bit feature mask in edx (param a1 -> local v54),
// which it also publishes to the global dword_11BC2D0 @0x11BC2D0. Every major
// per-frame subsystem step is gated by an "& bit" test on this mask. The bit
// meanings below were recovered by reading each `(v54 & 0xNNN)` site in the
// decompilation and noting which subsystem call it enables (or, for the two
// "suppress" bits, disables).
//
// Two bits are *suppressors* (their presence skips work): kHeadlessSuppress
// (0x10000) gates out almost all world/render/HUD work — used by the fade/
// net-wait/menu re-entrant calls that must pump messages without simulating —
// and kInputSuppress (0x100000) / kAutosaveSuppress (0x200000) which block HUD
// mouse handling and autosave/net-wait respectively.
namespace mask {
constexpr std::uint32_t kInputCommandPoll   = 0x00000001; // +bit0  input/cmd-request/cutscene poll block
constexpr std::uint32_t kWidgetMouse        = 0x00000004; // +bit2  VIBE_Widget_DispatchMouseClick
constexpr std::uint32_t kRenderWorld        = 0x00000040; // +bit6  cull+RenderMainViewFrame (day-cycle brightness)
constexpr std::uint32_t kGameObjects        = 0x00000080; // +bit7  GameObject interactions + fade/decompress + HUD draw
constexpr std::uint32_t kHudMouse           = 0x00000100; // +bit8  Hud_HandleMouseClick / EventPanel / Groundplan
constexpr std::uint32_t kScripts            = 0x00000200; // +bit9  VIBE_Script_StepAllActive
constexpr std::uint32_t kTooltips           = 0x00000400; // +bit10 VIBE_Tooltip_DispatchByType
constexpr std::uint32_t kHudLabels          = 0x00001000; // +bit12 Hud labels / name-input caption
constexpr std::uint32_t kOptionsAndPanels   = 0x00002000; // +bit13 options menu / chat / hotkeys / stat panels
constexpr std::uint32_t kWeatherSky         = 0x00004000; // +bit14 Weather_UpdateSky / RenderAndThunder
constexpr std::uint32_t kCombatSelect       = 0x00008000; // +bit15 SUPPRESS: combat/select drag variant (skips Character_CollectByOwner & unit drag)
constexpr std::uint32_t kHeadlessSuppress   = 0x00010000; // +bit16 SUPPRESS: pump-only re-entrancy (skips world/render/HUD/sim)
constexpr std::uint32_t kNetworkCommand     = 0x00020000; // +bit17 Command flush/receive/exec pump
constexpr std::uint32_t kDayCycleMusic      = 0x00040000; // +bit18 DayCycle brightness + outdoor music
constexpr std::uint32_t kCombatScroll       = 0x00080000; // +bit19 Camera_UpdateCombatScroll
constexpr std::uint32_t kInputSuppress      = 0x00100000; // +bit20 SUPPRESS: HUD mouse / quick-jump / speed keys / hotkeys
constexpr std::uint32_t kAutosaveSuppress   = 0x00200000; // +bit21 SUPPRESS: autosave & net wait-loop
constexpr std::uint32_t kQuickJump          = 0x00000010; // +bit4  quick-jump contact (also gates tooltip/drag-cursor off)
constexpr std::uint32_t kHudSelection       = 0x00000020; // +bit5  Hud_UpdateSelectionAndTargets / DrawSelectedUnitInfo
} // namespace mask

// ===========================================================================
// Session flags  (VIBE_GameLogic_InitOrLoadSession @0x533a54, word_63C740)
// ---------------------------------------------------------------------------
namespace session {
constexpr std::uint16_t kNewGame     = 0x0001; // &1  new game
constexpr std::uint16_t kLoadSave    = 0x0002; // &2  load savegame
constexpr std::uint16_t kNetwork     = 0x0004; // &4  network
constexpr std::uint16_t kAiMeister   = 0x0008; // &8  AI / meister
constexpr std::uint16_t kHost        = 0x0010; // &0x10 host (load Server.dll)
constexpr std::uint16_t kLoadNetSave = 0x0040; // &0x40 load network save
constexpr std::uint16_t kTutorial    = 0x0080; // &0x80 tutorial
} // namespace session

// ===========================================================================
// ISubsystems — hooks for every sibling-subsystem entry point the spine calls.
// ---------------------------------------------------------------------------
// Naming keeps the original VIBE_<Module>_<Action> identity (minus prefix).
// "[real]" marks hooks that have a translated implementation elsewhere in src/
// (a host adapter forwards to it); "[hook]" marks entry points not yet built —
// forward-declared here and documented per AGENT_GUIDE.
class ISubsystems {
public:
    virtual ~ISubsystems() = default;

    // True once inputLatchAndPump saw a quit (window-closed / WM_QUIT). The frame
    // loop checks this at the top of the frame and breaks before rendering, like
    // the original message loop. Default false (hooks that don't pump never quit).
    virtual bool quitRequested() const { return false; }

    // ---- init: VIBE_App_InitSubsystemsAndMovieDll @0x527de0 ----------------
    virtual void errorLogInit() = 0;          // [real] VIBE_ErrorLog_Init   (src/config/errorlog)
    virtual void memoryInitTracker(int bytes) = 0;     // [hook] VIBE_Memory_InitTracker
    virtual void memPoolStartupStack(int size) = 0;    // [hook] VIBE_MemPool_StartupStack
    virtual bool loadMovieDll() = 0;          // [hook] LoadLibrary("moveahead.dll") + resolve exports
    virtual void fileCreateDirectory(const std::string& path) = 0; // [hook] VIBE_File_CreateDirectory
    virtual void vfsInit(const std::string& root) = 0; // [real] VIBE_Vfs_Init (src/io)
    virtual void timeBaseStartTimer(int a, int b) = 0; // [hook] VIBE_TimeBase_StartTimer

    // ---- render init: VIBE_Render_InitDisplayAndPaths @0x527fa4 ------------
    virtual void configReadGfxAndSound() = 0;          // [real] VIBE_Config_ReadGfxAndSoundSettings (src/config)
    virtual bool renderEnumDisplayModes() = 0;         // [hook] VIBE_Render_EnumDisplayModes
    virtual bool renderInitEngineDevice(int w, int h, int bpp, bool fullscreen) = 0; // [hook] VIBE_Render_InitEngineDevice
    virtual void universeCreateDefaultCameras() = 0;   // [hook] VIBE_Universe_CreateDefaultCameras
    virtual void inputDirectInputInit(int mode) = 0;   // [hook] VIBE_Input_DirectInputInit (1=fs,6=win)
    virtual void renderSetAssetPaths() = 0;            // [hook] textures/animations/objects/groups/scripts/forms
    virtual void renderApplyGfxSettings() = 0;         // [hook] VIBE_Render_ApplyGfxSettings
    virtual bool guiLoadGfxFile(const std::string& name) = 0; // [real] VIBE_Gui_LoadGfxFile (src/gui)
    virtual void widgetInitSystem() = 0;               // [real] VIBE_Widget_InitSystem (src/gui)

    // ---- engine init: VIBE_App_InitEngineAndScriptCommands @0x528560 ------
    virtual bool textLoadDefinitionFile(const std::string& path) = 0; // [hook] VIBE_Text_LoadDefinitionFile
    virtual void netConnectToServer(const std::string& host, int port) = 0; // [real] VIBE_Net_ConnectToServer (src/net)
    virtual void commandQueueInitAndSync() = 0;        // [real] VIBE_Command_QueueInitAndSync (src/sim)
    virtual void worldLoadBuildingAndObjectData(const std::string& path) = 0; // [real] (src/world)
    virtual void buildingComputeMarketPrices() = 0;    // [real] VIBE_Building_ComputeMarketPrice (src/world)
    virtual void audioStartupMilesDriver() = 0;        // [real] VIBE_Audio_StartupMilesDriver (src/audio)
    virtual bool soundLibInit(int voices, int channels, int rate) = 0; // [real] VIBE_Sound_LibInit (src/audio)
    virtual void sound3dInitPool(int n) = 0;           // [real] VIBE_Sound3d_InitPool (src/audio)
    virtual void soundWaveInitSineTables() = 0;        // [real] VIBE_SoundWave_InitSineTables (src/audio)
    virtual void soundLoadSampleBank(const std::string& path) = 0; // [real] VIBE_Sound_LoadSampleBank (src/audio)
    virtual void soundPreloadIncludeFile(const std::string& path) = 0; // [real] (src/audio)
    virtual void soundInitMusicThread(int rate) = 0;   // [real] VIBE_Sound_InitThread (src/audio)
    virtual void audioApplyVolumeSettings() = 0;       // [real] VIBE_Audio_ApplyVolumeSettings (src/audio)
    virtual void scriptRegisterCommands() = 0;         // [real] VIBE_Script_Register* (src/sim)

    // ---- intro movie -------------------------------------------------------
    virtual void moviePlayIntroSequence() = 0;         // [hook] VIBE_Movie_PlayIntroSequence
    virtual void movieDllExit() = 0;                   // [hook] mov_Exit_ export (called in shutdown)

    // ---- per-frame steps (VIBE_GameLogic_RunFrameLoop @0x4c09a0) ----------
    // Coarse-grained: each represents one feature-mask-gated block of the loop.
    virtual void inputLatchAndPump() = 0;              // [hook/real] PumpMessages + Input_Latch (always)
    virtual void widgetDispatchMouseClick() = 0;       // mask kWidgetMouse
    virtual void hudHandleMouseClick() = 0;            // mask kHudMouse (& !kInputSuppress)
    virtual void inputCommandPoll() = 0;               // mask kInputCommandPoll
    virtual void commandNetworkPump() = 0;             // mask kNetworkCommand
    virtual void scriptStepAllActive() = 0;            // mask kScripts
    virtual void gameObjectDispatchInteractions() = 0; // mask kGameObjects
    virtual void renderMainViewFrame() = 0;            // mask kRenderWorld
    virtual void weatherUpdateSky() = 0;               // mask kWeatherSky
    virtual void dayCycleAndOutdoorMusic() = 0;        // mask kDayCycleMusic
    virtual void hudSelectionAndTargets() = 0;         // mask kHudSelection
    virtual void tooltipDispatch() = 0;                // mask kTooltips
    virtual void hudLabelsAndCaption() = 0;            // mask kHudLabels
    virtual void cameraCombatScroll() = 0;             // mask kCombatScroll
    virtual void presentFrame() = 0;                   // mask kGameObjects (HUD/present block)
    virtual void optionsChatHotkeyPanels() = 0;        // mask kOptionsAndPanels
    virtual void autosaveAndNetWait() = 0;             // !kAutosaveSuppress
    virtual void characterCollectByOwner() = 0;        // !kCombatSelect
    virtual void quickJumpContact() = 0;               // mask kQuickJump (& !kInputSuppress)

    // ---- shutdown teardown: the verbatim 13 steps (recon §1.12) -----------
    // Order MUST match VIBE_GameLogic_MainEntryAndShutdown's teardown block.
    virtual void tdGameShutdownSubsystems() = 0;    //  1  0x5278cc [hook]
    virtual void tdWidgetShutdownSystem() = 0;      //  2  0x4201f4 [real gui]
    virtual void tdConfigWriteGfxSettings() = 0;    //  3  0x56af54 [real config]
    virtual void tdGameStateFreeAllResources() = 0; //  4  0x40e308 [hook]
    virtual void tdUniverseSwitchActiveSlot0() = 0; //  5  0x5b4a24 [hook]
    virtual void tdTableResetLightmaps() = 0;       //  6  0x42e19c [hook]
    virtual void tdRenderShutdownEngine() = 0;      //  7  0x5b0228 [real render]
    virtual void tdInputDirectInputShutdown() = 0;  //  8  0x40cd40 [hook]
    virtual void tdTimeBaseStopTimer() = 0;         //  9  0x44e2c4 [hook]
    virtual void tdVfsShutdown() = 0;               // 10  0x452004 [real io]
    virtual void tdMemPoolShutdownStack() = 0;      // 11  0x44e544 [real mem]
    virtual void tdMemoryShutdownTracker() = 0;     // 12  0x439640 [real mem]
    virtual void tdErrorLogShutdown() = 0;          // 13  0x438c0c [real config]
};

// ===========================================================================
// GameApp — the spine object. Holds the shim devices and the subsystem hooks.
// ===========================================================================
class GameApp {
public:
    GameApp(shim::IPlatform& plat, shim::IGraphicsDevice& gfx,
            shim::IAudioDevice& audio, ISubsystems& sub);

    // gilde.exe 0x534bbc — VIBE_GameLogic_MainEntryAndShutdown.
    // Performs the full lifecycle: window creation, subsystem init, optional
    // intro, display/render init, engine+script init, then (here) a bounded
    // session run, then the verbatim 13-step shutdown. Returns the original's
    // exit code (0 normal-exit, 1 mutex-already-running path).
    // `framesPerSession` lets a test run a deterministic, bounded outer loop in
    // place of the original's blocking menu/session loop.
    int Run(const std::string& exeDir, int displayMode, bool showIntro,
            bool networkClient, int framesPerSession);

    // gilde.exe 0x52895c — VIBE_Window_CreateMainWindow. mode 3 = fullscreen.
    bool CreateMainWindow(int mode);

    // gilde.exe 0x527de0 — VIBE_App_InitSubsystemsAndMovieDll.
    bool InitSubsystemsAndMovieDll();

    // gilde.exe 0x527fa4 — VIBE_Render_InitDisplayAndPaths.
    bool InitDisplayAndPaths(int mode);

    // gilde.exe 0x528560 — VIBE_App_InitEngineAndScriptCommands.
    bool InitEngineAndScriptCommands();

    // gilde.exe 0x4c09a0 — VIBE_GameLogic_RunFrameLoop. Runs one frame under the
    // given feature mask, invoking exactly the enabled subsystem steps. Returns
    // the original's v10 ("ran a logic step") result; here always 1 for a
    // non-headless frame.
    int RunFrameLoop(std::uint32_t featureMask);

    // gilde.exe 0x533a54 — VIBE_GameLogic_InitOrLoadSession (bootstrap only;
    // the original's lockstep turn loop is driven through RunFrameLoop).
    void InitOrLoadSession(std::uint16_t sessionFlags, int framesPerSession);

    // The verbatim 13-step teardown (see ShutdownTeardown in the .cpp).
    void Shutdown();

    // Most-recent feature mask published to dword_11BC2D0 (for inspection/test).
    std::uint32_t lastFeatureMask() const { return lastFeatureMask_; }

    // True once the frame loop's input pump saw a quit (window closed). Drives the
    // interactive run loop (play::PlayableApp) to stop.
    bool quitRequested() const { return sub_.quitRequested(); }

private:
    shim::IPlatform&       plat_;
    shim::IGraphicsDevice& gfx_;
    shim::IAudioDevice&    audio_;
    ISubsystems&           sub_;

    // Effective config mirrored from the INI/cmdline globals the spine reads.
    int  displayMode_ = 3;     // v113: 1=windowed, 3=fullscreen
    bool showIntro_   = false; // dword_63C8F0
    bool movieDll_    = false; // ::hModule != 0 (moveahead.dll loaded)
    int  resW_ = 800, resH_ = 600; // dword_63D728 / dword_63D72C

    std::uint32_t lastFeatureMask_ = 0;
    bool shutdownDone_ = false;
};

} // namespace guild::app

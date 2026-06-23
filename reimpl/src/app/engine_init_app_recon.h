#pragma once
// ============================================================================
// gilde.exe — VIBE_App engine/script init orchestration (1:1 reconstruction).
//
// This unit reconstructs the startup wiring cluster:
//   * gilde.exe 0x528560 — VIBE_App_InitEngineAndScriptCommands  (the engine +
//     script-command init orchestrator: text-engine load, loading screen, audio
//     bring-up, subsystem/registration sequence, the BLACK initial fade loop,
//     the dword_122F4A0 scratch wipe, and the five script-command registrations).
//   * gilde.exe 0x527d48 — VIBE_App_CreateSingleInstanceMutex     (Win32 thunk).
//   * gilde.exe 0x527d8c — VIBE_App_CloseHandleThunk              (Win32 thunk).
//   * gilde.exe 0x527db8 — VIBE_App_RefreshScreensaverSetting     (Win32 thunk).
//
// Provenance is carried per function. The control flow, init order and
// registration sequence are translated VERBATIM from the Hex-Rays decompile
// (the reference of record). Subsystem leaves (text, audio, world, render,
// script, window, fade, ...) — many of which are already reconstructed
// elsewhere in the tree — are NOT reinvented here: every leaf is routed through
// the EngineInitHooks vtable below, whose default implementation is inert. The
// real app spine binds these hooks to the already-reconstructed callees (see the
// wiring map in this unit's report). This keeps the orchestration testable and
// faithful without duplicating leaf logic (no ODR clash, no fake analogues).
//
// Win32 boundary (Rule 4: Win32 -> SDL): the three 0x527d* thunks call Win32
// (CreateMutexA / CloseHandle / SystemParametersInfoA). Their EXACT logic is
// preserved (mutex name "Die Gilde", the GetLastError()==ERROR_ALREADY_EXISTS
// already-running test, SPI_SETSCREENSAVEACTIVE=0x10); the OS call itself is
// routed through PlatformInitHooks so a real backend can map it onto SDL.
//
// Namespace guild::app. Types from guild/common/types.h.
// ============================================================================

#include "guild/common/types.h"

#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace guild::app {

// ---------------------------------------------------------------------------
// Constants recovered from the binary.
// ---------------------------------------------------------------------------

// gilde.exe 0x622968 — ClassName, used as the single-instance mutex name.
inline constexpr const char* kSingleInstanceMutexName = "Die Gilde";

// Win32 ERROR_ALREADY_EXISTS — GetLastError() value the mutex create checks.
inline constexpr u32 kErrorAlreadyExists = 183u;

// Win32 SPI_SETSCREENSAVEACTIVE — uiAction passed to SystemParametersInfoA.
inline constexpr u32 kSpiSetScreenSaveActive = 0x10u;

// gilde.exe 0x622ab0 — "gilde_text"; copied into the local def-name buffer.
inline constexpr const char* kGildeTextBase = "gilde_text";

// gilde.exe 0x63ca0c — "\\project\\game\\"; the def-file path prefix.
inline constexpr const char* kProjectGamePrefix = "\\project\\game\\";

// gilde.exe 0x622af8 — "Misc\\Loading_main"; the loading-screen form name.
inline constexpr const char* kLoadingMainForm = "Misc\\Loading_main";

// gilde.exe 0x622b44 — "BLACK"; the palette name of the initial fade.
inline constexpr const char* kFadeBlackName = "BLACK";

// gilde.exe 0x622ac8 — text-engine open-failure message.
inline constexpr const char* kErrOpenEngine =
    "main_OpenEngine(): could not open txt-engine!";

// VIBE_Building_ComputeMarketPrice is called with these protocol ids, in this
// exact order, at startup (0x5286cc..0x52874c). The first uses register dl (v9,
// uninitialised in the decompile); the remaining six use 0x64 (=100).
struct MarketPriceCall { u16 prot; u8 qty; };
inline constexpr MarketPriceCall kMarketPriceCalls[7] = {
    {469, 0u},   // 0x5286cc  qty = dl (uninitialised in original; modeled as 0)
    {471, 0x64}, // 0x5286e2
    {468, 0x64}, // 0x5286f8
    {474, 0x64}, // 0x52870e
    {473, 0x64}, // 0x528724
    {470, 0x64}, // 0x52873a
    {472, 0x64}, // 0x52874c
};

// VIBE_GameLogic_RunFrameLoop is called inside the initial-fade wait loop with
// these literal args (0x52889e): RunFrameLoop(147591, 147591, formHandle).
inline constexpr i32 kFadeWaitFrameArg = 147591;

// VIBE_Fade_Register initial-fade args (0x52888c): (0,0, hi(dword_69FFB8+2)>>16,
// dword_69FFBC>>16, "BLACK", 90, 1). The fade waits until (flags & 4) != 0.
inline constexpr i32 kInitialFadeDuration = 90;
inline constexpr i32 kInitialFadeMode = 1;
inline constexpr u8  kFadeDoneBit = 0x04; // (*fadeInfo & 4) terminates the wait.

// dword_122F4A0 scratch block: cleared by an inlined memset of 140 bytes
// (v18 = 140 - v17 etc. — total 0x8C = 140) at 0x5288ad..0x5288e1.
inline constexpr u32 kScratchClearBytes = 140u;

// ---------------------------------------------------------------------------
// Inert-default hook tables. Every subsystem leaf the orchestrator calls is a
// std::function here; defaults are no-ops / benign returns so the orchestration
// runs headless. The real spine assigns these to the reconstructed callees.
// ---------------------------------------------------------------------------

// Engine/script subsystem leaves invoked by VIBE_App_InitEngineAndScriptCommands.
struct EngineInitHooks {
    // 0x44bb5c VIBE_Text_BuildTextArray(path, 0)              [editor path]
    std::function<void(const std::string& path)> textBuildTextArray =
        [](const std::string&) {};
    // 0x44b8f4 VIBE_Text_LoadDefinitionFile(path) -> nonzero on success
    std::function<int(const std::string& path)> textLoadDefinitionFile =
        [](const std::string&) { return 1; };
    // 0x438da8 VIBE_ErrorLog_ReportMessage(msg)
    std::function<void(const std::string& msg)> errorLogReportMessage =
        [](const std::string&) {};
    // 0x4fd194 VIBE_History_FreeChronicleFiles()
    std::function<void()> historyFreeChronicleFiles = [] {};
    // 0x41beb8 VIBE_GameTick_Finalize(0,0,"Misc\\Loading_main") -> form handle
    std::function<int(const std::string& form)> gameTickFinalize =
        [](const std::string&) { return 0; };
    // 0x41d764 VIBE_Window_PositionCentered(windowHandle, mode)
    std::function<void(int handle, int mode)> windowPositionCentered =
        [](int, int) {};
    // 0x413220 VIBE_Widget_LayoutBounds(a, b, c)
    std::function<void(int a, int b, int c)> widgetLayoutBounds =
        [](int, int, int) {};
    // 0x59d6e8 VIBE_Text_RenderRichString(0x73, byte_122F218)
    std::function<void(u32 code, int buf)> textRenderRichString =
        [](u32, int) {};
    // 0x43b51c VIBE_Net_ConnectToServer(0, 0) [local]
    std::function<void(int a, int b)> netConnectToServer = [](int, int) {};
    // 0x4931e0 VIBE_Command_QueueInitAndSync()
    std::function<void()> commandQueueInitAndSync = [] {};
    // 0x4f740c VIBE_Building_DeselectThunk()
    std::function<void()> buildingDeselectThunk = [] {};
    // 0x5835f8 VIBE_World_LoadBuildingAndObjectData(path)
    std::function<void(const std::string& path)> worldLoadBuildingAndObjectData =
        [](const std::string&) {};
    // 0x58f3d0 VIBE_Building_ComputeMarketPrice(prot, qty) -> float
    std::function<float(u16 prot, u8 qty)> buildingComputeMarketPrice =
        [](u16, u8) { return 0.0f; };

    // Audio bring-up (sfx gate dword_63C900):
    // 0x449840 VIBE_Audio_StartupMilesDriver()
    std::function<void()> audioStartupMilesDriver = [] {};
    // 0x445d90 VIBE_Sound_LibInit(&unk_989680, 48, 2, 44100) -> nonzero=fail
    std::function<int()> soundLibInit = [] { return 0; };
    // 0x424538 VIBE_Sound3d_InitPool(64)
    std::function<void(int n)> sound3dInitPool = [](int) {};
    // 0x424d40 VIBE_SoundWave_InitSineTables(0x30)
    std::function<void(u32 n)> soundWaveInitSineTables = [](u32) {};
    // 0x4463d8 VIBE_Sound_SetBankPath("<game>sfx\\")
    std::function<void(const std::string& path)> soundSetBankPath =
        [](const std::string&) {};
    // 0x446b2c VIBE_Sound_LoadSampleBank("system.sbf") -> bank handle
    std::function<int(const std::string& name)> soundLoadSampleBank =
        [](const std::string&) { return 0; };
    // 0x52f154 VIBE_Sound_PreloadFromIncludeFile("<exeDir>\\include_sfx.ini")
    std::function<void(const std::string& path)> soundPreloadFromIncludeFile =
        [](const std::string&) {};

    // Music bring-up (music gate dword_63C8F8):
    // 0x439bf8 VIBE_Sound_InitThread(2, ?, 44100)
    std::function<void()> soundInitThread = [] {};
    // 0x43a95c VIBE_Audio_SetTrackNamePrefix("<game>msx\\")
    std::function<void(const std::string& path)> audioSetTrackNamePrefix =
        [](const std::string&) {};

    // 0x56c148 VIBE_Audio_ApplyVolumeSettings()
    std::function<void()> audioApplyVolumeSettings = [] {};
    // 0x4ad4c4 VIBE_Cutscene_RegisterTickProc()
    std::function<void()> cutsceneRegisterTickProc = [] {};
    // 0x4ffee8 VIBE_Object_ResetSpawnTables()
    std::function<void()> objectResetSpawnTables = [] {};
    // 0x40be30 VIBE_CharAction_RegisterHandlers()
    std::function<void()> charActionRegisterHandlers = [] {};
    // 0x42d220 VIBE_Render_BuildSnowTexture()
    std::function<void()> renderBuildSnowTexture = [] {};
    // 0x4520d0 VIBE_GameLogic_InitGuardState() — fused with its internal
    // VIBE_AiMethod_LoadDataFile (0x468a40) call, exactly as the original: it
    // loads the AI-data catalog, then gates the guard-state table init on the
    // load result. The default body (RealGameLogicInitGuardState, defined in the
    // .cpp) performs that real edge over the shared world GuardState + a sim
    // AiNeedsCatalog, pulling the DFN bytes through `aiDataDfnProvider`. (Rule 13
    // — the reconstructed AiNeeds_LoadDataFile + GameLogicInitGuardState are now
    // connected along the original's call edge; previously a no-op.)
    std::function<void()> gameLogicInitGuardState;

    // VFS + gzip boundary for the AI catalog: hand back the DECOMPRESSED bytes of
    // "/gamedata/ai/ai_data.dfn" (the original VIBE_Vfs_OpenFile @0x450bc8 read,
    // 61*73 bytes after gunzip). Default returns empty (no install / no VFS) =>
    // the load fails and InitGuardState takes the original's failure path (table
    // left untouched, returns 0), faithful to a missing data file. A real backend
    // installs a provider that opens the VFS file and runs guild::compress::Gunzip.
    std::function<std::vector<u8>()> aiDataDfnProvider = [] {
        return std::vector<u8>{};
    };
    // 0x48b744 VIBE_Combat_InitDefaultParameters()
    std::function<void()> combatInitDefaultParameters = [] {};
    // 0x41da04 VIBE_Form_Destroy(formHandle)
    std::function<void(int handle)> formDestroy = [](int) {};

    // Initial BLACK fade:
    // 0x41f0e8 VIBE_Fade_Register(...) -> fadeInfo pointer (here an opaque id).
    // The default returns a fade that is ALREADY done (flag bit 4 set) so the
    // wait loop terminates immediately in headless mode.
    std::function<int(i32 x, i32 y, i32 w, i32 h, const std::string& pal,
                      i32 dur, i32 mode)>
        fadeRegister = [](i32, i32, i32, i32, const std::string&, i32, i32) {
            return 1;
        };
    // Returns the current fade flags byte for the given fade id.
    // Default reports the done bit set immediately.
    std::function<u8(int fadeId)> fadeQueryFlags =
        [](int) -> u8 { return kFadeDoneBit; };
    // 0x4c09a0 VIBE_GameLogic_RunFrameLoop(147591, 147591, formHandle)
    std::function<void(i32 a, i32 b, int form)> gameLogicRunFrameLoop =
        [](i32, i32, int) {};
    // 0x41f18c VIBE_Fade_Unregister(fadeInfo, &dword_122F4A0)
    std::function<void(int fadeId)> fadeUnregister = [](int) {};

    // Five script-command registration tables (in this exact order):
    // 0x4453a4 VIBE_Script_ConsoleParseLine()
    std::function<void()> scriptConsoleParseLine = [] {};
    // 0x43c850 VIBE_Script_RegisterCommands()
    std::function<void()> scriptRegisterCommands = [] {};
    // 0x440618 VIBE_Script_RegisterObjectCommands()
    std::function<void()> scriptRegisterObjectCommands = [] {};
    // 0x43dfb0 VIBE_Character_RegisterScriptCommands()
    std::function<void()> characterRegisterScriptCommands = [] {};
    // 0x440df0 VIBE_Script_RegisterSoundCommands()
    std::function<void()> scriptRegisterSoundCommands = [] {};

    // 0x56c0cc VIBE_Config_ApplyCameraAndScrollSettings()
    std::function<void()> configApplyCameraAndScrollSettings = [] {};
    // 0x4bea64 VIBE_Window_PumpMessages()
    std::function<void()> windowPumpMessages = [] {};
};

// Global flags the orchestrator branches on. These are real binary globals; the
// orchestrator only reads them, so they are inputs to the reconstruction.
struct EngineInitGlobals {
    bool editorTextMode = false; // dword_63C7D4 — editor build uses BuildTextArray
    bool sfxEnabled = false;     // dword_63C900 — sfx subsystem gate
    bool musicEnabled = false;   // dword_63C8F8 — music subsystem gate
};

// Platform (Win32 -> SDL) hooks for the three 0x527d* thunks.
struct PlatformInitHooks {
    // OS create-mutex: returns an opaque non-null handle on success, 0 on fail,
    // and reports whether the named mutex already existed (ERROR_ALREADY_EXISTS).
    // (Win32: CreateMutexA(0,1,name) + GetLastError().)
    struct MutexResult { void* handle; bool alreadyExisted; };
    std::function<MutexResult(const std::string& name)> createNamedMutex =
        [](const std::string&) -> MutexResult {
            return MutexResult{reinterpret_cast<void*>(1), false};
        };
    // Win32: CloseHandle(h) -> BOOL.
    std::function<int(void* handle)> closeHandle =
        [](void*) { return 1; };
    // Win32: SystemParametersInfoA(SPI_SETSCREENSAVEACTIVE, 0, pvParam, 0).
    // The pvParam[1]/pvParam[2] words carry (a2, a1); pvParam[0] is forced 0.
    std::function<void(u32 action, i32 word1, i32 word2)> systemParametersInfo =
        [](u32, i32, i32) {};
};

// ---------------------------------------------------------------------------
// Reconstructed functions.
// ---------------------------------------------------------------------------

// gilde.exe 0x527d48 — VIBE_App_CreateSingleInstanceMutex.
//   MutexA = CreateMutexA(0, 1, "Die Gilde");
//   if (!MutexA) return -1;
//   *a1 = MutexA;
//   return GetLastError() == 183;            // 1 == another instance is running
// Returns -1 on create failure, else 1 if the app was already running, 0 if not.
// On success the created handle is written to *outHandle.
int VIBE_App_CreateSingleInstanceMutex(void** outHandle,
                                        const PlatformInitHooks& plat);

// gilde.exe 0x527d8c — VIBE_App_CloseHandleThunk.
//   return CloseHandle(a1);
int VIBE_App_CloseHandleThunk(void* handle, const PlatformInitHooks& plat);

// gilde.exe 0x527db8 — VIBE_App_RefreshScreensaverSetting.
//   pvParam[2]=a1; pvParam[1]=a2; pvParam[0]=0;
//   SystemParametersInfoA(SPI_SETSCREENSAVEACTIVE, 0, pvParam, 0);
//   VIBE_Util_NullSub();
void VIBE_App_RefreshScreensaverSetting(i32 a1, i32 a2,
                                        const PlatformInitHooks& plat);

// gilde.exe 0x528560 — VIBE_App_InitEngineAndScriptCommands (__thiscall).
// Reconstructs the full init orchestration 1:1. `formHandle` (the original v6,
// from VIBE_GameTick_Finalize) flows through the window-positioning, fade-wait
// frame loop and Form_Destroy exactly as in the decompile. Returns 1.
int VIBE_App_InitEngineAndScriptCommands(const EngineInitGlobals& g,
                                         EngineInitHooks& h);

// gilde.exe 0x4520d0 — the real fused guard-state init (AI-data load ->
// InitGuardState). Loads the AI catalog via h.aiDataDfnProvider, then runs
// GameLogicInitGuardState over the shared world GuardState. Used as the default
// h.gameLogicInitGuardState body; exposed so a backend/test can drive the edge
// directly after installing a DFN provider. (Rule 13 integration entry point.)
void RealGameLogicInitGuardState(EngineInitHooks& h);

} // namespace guild::app

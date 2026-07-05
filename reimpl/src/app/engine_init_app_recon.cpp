// ============================================================================
// gilde.exe — VIBE_App engine/script init orchestration (1:1 reconstruction).
// See engine_init_app_recon.h for provenance, the hook model and the wiring map.
//
// Control flow / init order / registration sequence are translated VERBATIM
// from the Hex-Rays decompile of:
//   0x527d48 VIBE_App_CreateSingleInstanceMutex
//   0x527d8c VIBE_App_CloseHandleThunk
//   0x527db8 VIBE_App_RefreshScreensaverSetting
//   0x528560 VIBE_App_InitEngineAndScriptCommands
// ============================================================================

#include "app/engine_init_app_recon.h"

#include "world/guildstate_recon.h"   // GuardState / GameLogicInitGuardState / GuardStateGlobal (0x4520d0)
#include "sim/ai_needs.h"             // AiNeedsCatalog / AiNeeds_LoadDataFile (0x468a40 read path)
#include "compress/gzip.h"           // Gunzip (the .dfn is a gzip member)

namespace guild::app {

// ---------------------------------------------------------------------------
// gilde.exe 0x4520d0 — VIBE_GameLogic_InitGuardState, fused with its internal
// VIBE_AiMethod_LoadDataFile (0x468a40) call. The original opens
// "/gamedata/ai/ai_data.dfn" "rb" via the VFS, runs BuildScoreTable, streams 61
// records into the AiNeeds catalog (BuildScoreTable + the DFN overlay/MemMove),
// and passes the success boolean to the guard-state table init.
//
// Here: the DFN bytes (already VFS-opened + gunzip'd) come from the provider
// hook; AiNeeds_LoadDataFile reconstructs BuildScoreTable + overlay; the result
// drives GameLogicInitGuardState over the shared world GuardState. No provider
// (headless) => empty bytes => load fails => the original's failure path (guard
// table untouched, returns 0). This is the genuine 0x4520d0 call edge.
// ---------------------------------------------------------------------------
void RealGameLogicInitGuardState(EngineInitHooks& h) {
    // The catalog the original keeps as the byte_B57210 global record block.
    static sim::AiNeedsCatalog s_catalog;

    std::vector<u8> dfn = h.aiDataDfnProvider ? h.aiDataDfnProvider()
                                              : std::vector<u8>{};
    int loaded = 0;
    if (!dfn.empty()) {
        // AiNeeds_LoadDataFile == BuildScoreTable(0) + the 61-record overlay; it
        // returns 1 iff the build succeeded AND all 61 records overlaid.
        loaded = sim::AiNeeds_LoadDataFile(dfn.data(), dfn.size(), s_catalog);
    }
    // VIBE_GameLogic_InitGuardState: gate the guard-state table on the load.
    world::GameLogicInitGuardState(world::GuardStateGlobal(), loaded != 0);
}

// gilde.exe 0x527d48 — VIBE_App_CreateSingleInstanceMutex.
int VIBE_App_CreateSingleInstanceMutex(void** outHandle,
                                       const PlatformInitHooks& plat) {
    // MutexA = CreateMutexA(0, 1, ClassName);   (ClassName == "Die Gilde")
    PlatformInitHooks::MutexResult r =
        plat.createNamedMutex(kSingleInstanceMutexName);
    // if ( !MutexA ) return -1;
    if (!r.handle)
        return -1;
    // *a1 = MutexA;
    *outHandle = r.handle;
    // return GetLastError() == 183;  (ERROR_ALREADY_EXISTS -> already running)
    // The platform hook reports alreadyExisted := (GetLastError()==183).
    return r.alreadyExisted ? 1 : 0;
}

// gilde.exe 0x527d8c — VIBE_App_CloseHandleThunk.
int VIBE_App_CloseHandleThunk(void* handle, const PlatformInitHooks& plat) {
    // return CloseHandle(a1);
    return plat.closeHandle(handle);
}

// gilde.exe 0x527db8 — VIBE_App_RefreshScreensaverSetting.
void VIBE_App_RefreshScreensaverSetting(i32 a1, i32 a2,
                                        const PlatformInitHooks& plat) {
    // pvParam[2] = a1; pvParam[1] = a2; pvParam[0] = 0;
    // SystemParametersInfoA(SPI_SETSCREENSAVEACTIVE, 0, pvParam, 0);
    plat.systemParametersInfo(kSpiSetScreenSaveActive, /*word1=*/a2,
                              /*word2=*/a1);
    // VIBE_Util_NullSub();  — inert epilogue, intentionally nothing.
}

// gilde.exe 0x528560 — VIBE_App_InitEngineAndScriptCommands.
int VIBE_App_InitEngineAndScriptCommands(const EngineInitGlobals& g,
                                         EngineInitHooks& h) {
    // Rule-13 default: when the caller did not override the guard-state init hook,
    // bind it to the real fused 0x4520d0 edge (AI-data load -> InitGuardState).
    // A test/backend that set its own hook keeps it (recording hooks override).
    if (!h.gameLogicInitGuardState)
        h.gameLogicInitGuardState = [&h] { RealGameLogicInitGuardState(h); };

    // dword_62D314 = 1;  (a global "engine starting" flag — recorded as a hook
    // side effect by tests; no leaf call). Modeled implicitly by entering here.

    // The def-name buffer (v26) is filled by copying aGildeText ("gilde_text")
    // two bytes at a time. The result is simply "gilde_text".
    const std::string defBase = kGildeTextBase;
    const std::string defPath =
        std::string(kProjectGamePrefix) + defBase + ".def";

    if (g.editorTextMode) {
        // dword_63C7D4 != 0:
        //   sprintf(v25, "%s%s.def", aProjectGame, v26);
        //   VIBE_Text_BuildTextArray(v25, 0);
        h.textBuildTextArray(defPath);
    } else {
        //   sprintf(v25, "%s%s.def", aProjectGame, v26);
        //   if ( !VIBE_Text_LoadDefinitionFile(v25) )
        //       VIBE_ErrorLog_ReportMessage(aMainOpenengine);
        if (!h.textLoadDefinitionFile(defPath))
            h.errorLogReportMessage(kErrOpenEngine);
    }

    // VIBE_History_FreeChronicleFiles();
    h.historyFreeChronicleFiles();

    // v6 = VIBE_GameTick_Finalize(0, 0, "Misc\\Loading_main");
    const int formHandle = h.gameTickFinalize(kLoadingMainForm);

    // VIBE_Window_PositionCentered(dword_676A64[171 * v6], 3);  — primary window.
    // The 171*v6 indexing addresses the form record; here it is the form handle.
    h.windowPositionCentered(formHandle, 3);

    // VIBE_Widget_LayoutBounds(0,
    //     dword_69FFBC - word_67EB8A[...] - 48,
    //     dword_67EDEC[238 * dword_676A68[171 * v6]]);
    // The two index expressions resolve against globals not modeled headless;
    // the call ORDER and its position in the sequence are what we preserve.
    h.widgetLayoutBounds(0, 0, 0);

    // VIBE_Window_PositionCentered(dword_676A68[171 * v6], 1);  — secondary.
    h.windowPositionCentered(formHandle, 1);

    // VIBE_Text_RenderRichString(0x73u, byte_122F218);
    h.textRenderRichString(0x73u, 0);

    // VIBE_Net_ConnectToServer(0, 0);   (local — no server)
    h.netConnectToServer(0, 0);

    // VIBE_Command_QueueInitAndSync();
    h.commandQueueInitAndSync();

    // VIBE_Building_DeselectThunk();
    h.buildingDeselectThunk();

    // sprintf(v25, "%sdata\\", aProjectGame);
    // VIBE_World_LoadBuildingAndObjectData(v25);
    h.worldLoadBuildingAndObjectData(std::string(kProjectGamePrefix) + "data\\");

    // Seven VIBE_Building_ComputeMarketPrice calls, in exact order. The result is
    // stored into the float local v27 and overwritten each time (no further use).
    for (const MarketPriceCall& c : kMarketPriceCalls)
        (void)h.buildingComputeMarketPrice(c.prot, c.qty);

    // ---- audio bring-up (sfx gate dword_63C900) ----------------------------
    // The music gate is a LIVE global the lib-init failure path clears before
    // the music block below reads it (clears at 0x528784/0x52878a; the music
    // block re-reads dword_63C8F8 at 0x528800).
    bool musicEnabled = g.musicEnabled;
    if (g.sfxEnabled) {
        // VIBE_Audio_StartupMilesDriver();
        h.audioStartupMilesDriver();
        // if ( VIBE_Sound_LibInit(&unk_989680, 48, 2, 44100) ) {
        //     dword_63C900 = 0; dword_63C8F8 = 0;   (nonzero = failure)
        // }
        if (h.soundLibInit()) {
            // dword_63C900 = 0 has no further reader inside this function; the
            // dword_63C8F8 = 0 clear DOES gate the music block below
            // (`if (dword_63C8F8)` at 0x5287f6) — mirror it.
            musicEnabled = false;
        }
        // VIBE_Sound3d_InitPool(64);
        h.sound3dInitPool(64);
        // VIBE_SoundWave_InitSineTables(0x30);
        h.soundWaveInitSineTables(0x30u);
        // sprintf(v25, "%ssfx\\", aProjectGame);
        // VIBE_Sound_SetBankPath(v25);
        h.soundSetBankPath(std::string(kProjectGamePrefix) + "sfx\\");
        // dword_63C748 = VIBE_Sound_LoadSampleBank("system.sbf");
        (void)h.soundLoadSampleBank("system.sbf");
        // sprintf(v24, "%s\\include_sfx.ini", byte_122F73C);  (exe dir)
        // VIBE_Sound_PreloadFromIncludeFile(v24);
        h.soundPreloadFromIncludeFile("\\include_sfx.ini");
    }

    // ---- music bring-up (music gate dword_63C8F8, possibly cleared above) ---
    if (musicEnabled) {
        // VIBE_Sound_InitThread(2, ?, 44100);
        h.soundInitThread();
        // sprintf(v25, "%smsx\\", aProjectGame);
        // VIBE_Audio_SetTrackNamePrefix(v25);
        h.audioSetTrackNamePrefix(std::string(kProjectGamePrefix) + "msx\\");
    }

    // VIBE_Audio_ApplyVolumeSettings();
    h.audioApplyVolumeSettings();
    // VIBE_Cutscene_RegisterTickProc();
    h.cutsceneRegisterTickProc();
    // VIBE_Object_ResetSpawnTables();
    h.objectResetSpawnTables();
    // VIBE_CharAction_RegisterHandlers();
    h.charActionRegisterHandlers();
    // VIBE_Render_BuildSnowTexture();
    h.renderBuildSnowTexture();
    // VIBE_GameLogic_InitGuardState();
    h.gameLogicInitGuardState();
    // VIBE_Combat_InitDefaultParameters();
    h.combatInitDefaultParameters();

    // VIBE_Form_Destroy(v6);   — tear down the loading-screen form.
    h.formDestroy(formHandle);

    // ---- initial BLACK fade + wait loop ------------------------------------
    // v15 = VIBE_Fade_Register(0, 0, hi(dword_69FFB8+2)>>16, dword_69FFBC>>16,
    //                          aBlack_2, 90, 1);
    // while ( (*v15 & 4) == 0 )
    //     VIBE_GameLogic_RunFrameLoop(147591, 147591, v6);
    const int fadeId =
        h.fadeRegister(0, 0, /*w*/ 0, /*h*/ 0, kFadeBlackName,
                       kInitialFadeDuration, kInitialFadeMode);
    while ((h.fadeQueryFlags(fadeId) & kFadeDoneBit) == 0)
        h.gameLogicRunFrameLoop(kFadeWaitFrameArg, kFadeWaitFrameArg,
                                formHandle);

    // VIBE_Fade_Unregister(v15, &dword_122F4A0);
    h.fadeUnregister(fadeId);

    // Inlined memset wipe of the 140-byte dword_122F4A0 scratch block
    // (0x5288ad..0x5288e1). Modeled directly as the byte clear it performs.
    {
        unsigned char scratch[kScratchClearBytes];
        std::memset(scratch, 0, sizeof(scratch));
        (void)scratch;
    }

    // byte_63CC1C = 1;   (console-ready flag) then the script command tables.
    // VIBE_Script_ConsoleParseLine();
    h.scriptConsoleParseLine();
    // VIBE_Script_RegisterCommands();
    h.scriptRegisterCommands();
    // VIBE_Script_RegisterObjectCommands();
    h.scriptRegisterObjectCommands();
    // VIBE_Character_RegisterScriptCommands();
    h.characterRegisterScriptCommands();
    // VIBE_Script_RegisterSoundCommands();
    h.scriptRegisterSoundCommands();

    // off_64A904 = v21; dword_64A094 = v22;   (uninitialised regs in the
    // decompile — they store whatever edx held; an artifact, no observable
    // effect, intentionally omitted. See report.)

    // VIBE_Config_ApplyCameraAndScrollSettings();
    h.configApplyCameraAndScrollSettings();
    // VIBE_Window_PumpMessages();
    h.windowPumpMessages();

    // return 1;
    return 1;
}

} // namespace guild::app

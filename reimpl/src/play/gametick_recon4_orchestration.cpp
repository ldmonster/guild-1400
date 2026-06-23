// ===========================================================================
//  gametick_recon4_orchestration.cpp
//  See header for cluster map and provenance.
//  1:1 control-flow / ordering reconstruction of the turn/universe/shutdown
//  orchestration. Engine leaves are invoked through the inert hook table.
// ===========================================================================
#include "gametick_recon4_orchestration.h"

namespace guild {
namespace play {

// --- weight tables (exact bytes from gilde.exe) ---------------------------
// 0x577954: 0,0,1,1,1,1,1,1,2,2,2,2,2,2,2,2
const i32 kEventWeightTableA[16] = {0,0,1,1,1,1,1,1,2,2,2,2,2,2,2,2};
// 0x577994: 0,0,0,0,0,0,0,1,1,2,2,2,2,2,2,2
const i32 kEventWeightTableB[16] = {0,0,0,0,0,0,0,1,1,2,2,2,2,2,2,2};
// 0x5779d4: 0,0,0,0,0,0,0,1,1,1,1,1,1,1,2,2
const i32 kEventWeightTableC[16] = {0,0,0,0,0,0,0,1,1,1,1,1,1,1,2,2};

// ===========================================================================
//  Universe slot suspend / resume / script-pass / destroy
// ===========================================================================

// gilde.exe 0x5b3030 — VIBE_Universe_DisplayLogAndCleanup (__usercall al=al)
u8 Universe_DisplayLogAndCleanup(UniverseState& st, u8 flag,
                                 const GameTickRecon4Hooks& hk) {
    // Original tests bits of (flag<<24): bit25 => 0x02000000, bit24 => 0x01000000.
    // With `flag` being that high byte, that is bit1 / bit0 of `flag`.
    if ((flag & kSlotFlagFloor) != 0 && st.floorObj != 0) {
        if (hk.floorFreeTileBuffers) hk.floorFreeTileBuffers();   // 0x5bccf8
    }
    if ((flag & kSlotFlagGeom) != 0) {
        if (hk.sceneTraverseFreeMesh) hk.sceneTraverseFreeMesh(); // 0x5ac86c+0x5b2ef8
    }
    st.suspendByte = flag;          // byte_649DD0 = HIBYTE(v6)
    return 1;
}

// gilde.exe 0x5b3410 — VIBE_Universe_InitLogAndInflate (__usercall al=al)
u8 Universe_InitLogAndInflate(UniverseState& st, u8 flag,
                              const GameTickRecon4Hooks& hk) {
    if ((flag & kSlotFlagFloor) != 0 && st.floorObj != 0) {
        if (hk.floorAllocInflateBuffers) hk.floorAllocInflateBuffers(); // 0x5bce10
    }
    if ((flag & kSlotFlagGeom) != 0) {
        if (hk.sceneTraverseInflateGeom) hk.sceneTraverseInflateGeom(); // 0x5ac86c+0x5b30d4
    }
    // byte_649DD0 = ~HIBYTE(v5) & 3
    u8 inv = static_cast<u8>(~flag & 3);
    st.suspendByte = inv;
    // if ((~flag&3)!=0 || !heightmap || floorObj) return 1; else build terrain.
    if (inv != 0 || st.heightmapObj == 0 || st.floorObj != 0) {
        return 1;
    }
    if (hk.heightmapBuildTerrainMesh) hk.heightmapBuildTerrainMesh(); // 0x5c5610
    return 1;
}

// gilde.exe 0x5b3628 — VIBE_Universe_RunObjectScriptPass (__usercall al)
u8 Universe_RunObjectScriptPass(const GameTickRecon4Hooks& hk) {
    // v9=1 (first), walk; v9=0 (second), walk; upload textures; refresh lights.
    if (hk.sceneWalkRunScript) hk.sceneWalkRunScript(1);   // 0x5ac738 first pass
    if (hk.sceneWalkRunScript) hk.sceneWalkRunScript(0);   // 0x5ac738 second pass
    if (hk.textureUploadAllRecords) hk.textureUploadAllRecords(); // 0x5db4b8
    if (hk.lightRefreshAllObjects) hk.lightRefreshAllObjects();   // 0x5c886c
    return 1; // VIBE_Light_RefreshAllObjects(1) -> al
}

// gilde.exe 0x5b5050 — VIBE_Universe_DestroySlot (__usercall al, eax=slot)
u8 Universe_DestroySlot(UniverseState& st, u32 slot,
                        const GameTickRecon4Hooks& hk) {
    if (slot >= 0x40)                       // bounds: 64 slots
        return 0;

    if (static_cast<i32>(slot) == st.activeSlot) {
        if (hk.universeResetCurrentSlot) hk.universeResetCurrentSlot(static_cast<i32>(slot)); // 0x5b44c4
        return 1;
    }

    if (!st.slotAllocated[slot])            // dword_13ECF48[246*slot] == 0
        return 1;

    i32 prevActive = st.activeSlot;         // v4 = dword_649D60
    if (static_cast<i32>(slot) != st.activeSlot) {
        if (hk.universeSwitchActiveSlot) hk.universeSwitchActiveSlot(static_cast<i32>(slot)); // 0x5b4a24
        st.activeSlot = static_cast<i32>(slot);
    }

    // drain the live-object list (dword_13FCF10 .. dword_13FCF4C)
    if (hk.objectDispose) hk.objectDispose();        // 0x5b0790 (loop body)
    // drain the render-node list (dword_1408438 .. unk_1408440)
    if (hk.renderFreeObjectNode) hk.renderFreeObjectNode(); // 0x5e0f30 (loop body)

    if (st.floorObj != 0) {
        if (hk.floorFreeBuffers) hk.floorFreeBuffers(); // 0x5ba508
        st.floorObj = 0;                                // dword_64A028 = 0
    }
    if (st.skyObj != 0) {
        if (hk.skyDestroy) hk.skyDestroy();             // 0x5efda0
        st.skyObj = 0;                                  // dword_64A7C8 = 0
    }
    if (st.slotMemory[slot] != 0) {
        if (hk.memoryFreeDebug) hk.memoryFreeDebug();   // 0x43923c
        st.slotMemory[slot] = 0;                        // dword_13ED290[..] = 0
    }
    st.suspendByte = 0;                                 // byte_649DD0 = 0
    // dword_649EFC = 0 (engine-global, not modeled here)

    if (prevActive != st.activeSlot) {
        if (hk.universeSwitchActiveSlot) hk.universeSwitchActiveSlot(prevActive); // 0x5b4a24
        st.activeSlot = prevActive;
    }
    return 1;
}

// ===========================================================================
//  GameTick turn timer
// ===========================================================================

// gilde.exe 0x57957c — VIBE_GameTick_AdvanceTurnTimer
bool GameTick_AdvanceTurnTimer(TurnTimerState& st, const TurnTimerEnv& env,
                               i32* outFired) {
    if (outFired) *outFired = 0;

    if (st.phase != 1)                  // dword_1235238 != 1
        return false;

    // The function works on a local copy of the 36-byte block (v30); we mutate
    // st directly since the trailing RequestBuildOp86 flushes it back out.

    if (st.forced > 0) {                // dword_1235254 > 0  (forced event path)
        if (st.forced == 1) {
            if (env.heFindHandler && env.heFindHandler(89)) return false;
        } else if (st.forced == 2) {
            if (env.heFindHandler && env.heFindHandler(78)) return false;
        } else if (st.forced == 3) {
            if (env.heFindHandler && env.heFindHandler(80)) return false;
        }
        // Not suppressed: clear pending/accum and fall through to flush.
        st.pending = 0;                 // v30[7] = 0
        st.accum   = 0.0f;              // v30[5] = 0
        // LABEL_7: flush
        if (env.requestBuildOp86) env.requestBuildOp86(0);
        return true;
    }

    // Normal path: accumulate.  v7 = rand()*rate + base + accum
    float r = env.randomFloatScaled ? env.randomFloatScaled() : 0.0f;
    float v = r * st.rate + st.base + st.accum;
    st.accum = v;                       // v30[5] = v7

    if (v > st.threshold) {             // v7 > v30[6]
        // SetGrayColorThunk(0,248,&v15) — visual flash; recorded via emitEvent? no.
        st.timestamp = 0;               // v30[2] = qword_13CE852 lo (game time)
        ++st.counter;                   // ++v30[1]

        if (st.category >= 4) {         // v30[8] >= 4
            st.category = 0;            // v30[8] = 0  (reset, no event)
            if (env.requestBuildOp86) env.requestBuildOp86(0);
            return true;
        }

        i32 kind;
        if (st.category == 0) {
            // v30[7] = 3, then LABEL_21 emits kind 80
            st.pending = 3;
            kind = 80;
        } else {
            // pick a weighted entry from the category-specific table, +1
            const i32* tbl;
            if (st.category == 1)      tbl = kEventWeightTableA; // dword_577954
            else if (st.category == 2) tbl = kEventWeightTableB; // dword_577994
            else                       tbl = kEventWeightTableC; // dword_5779d4
            u16 idx = env.randomModulo ? env.randomModulo(16) : 0;
            i32 picked = tbl[idx] + 1;  // v14 = v13 + 1
            st.pending = picked;        // v30[7] = v14
            if (picked == 1)      kind = 89;
            else if (picked == 2) kind = 78;
            else if (picked == 3) kind = 80;
            else {
                // v14 not in {1,2,3}: no event; v30[8] = v30[7]; flush.
                st.category = st.pending;
                if (env.requestBuildOp86) env.requestBuildOp86(0);
                return true;
            }
        }

        // emit the event (GameTime_Advance + QueueRequestSlotReset28 in orig)
        if (env.emitEvent) env.emitEvent(kind);
        if (outFired) *outFired = kind;
        st.category = st.pending;       // v30[8] = v30[7]
    }

    // LABEL_7 — flush state via RequestBuildOp86
    if (env.requestBuildOp86) env.requestBuildOp86(0);
    return true;
}

// gilde.exe 0x579530 — VIBE_GameTick_RequestStartTurn
i32 GameTick_RequestStartTurn(TurnTimerState& st, const TurnTimerEnv& env) {
    // Original snapshots the 36-byte block, zeroes word[0], writes the 'turn'
    // tag (1685283436 == 'lrut' little-endian, i.e. "turl"/turn request) into
    // word[9], then RequestBuildOp86(block, tag).
    const i32 kTurnTag = 1685283436;    // 0x6477726C
    st.phase = 0;                       // v9[0] = 0
    if (env.requestBuildOp86) env.requestBuildOp86(kTurnTag);
    return kTurnTag;
}

// gilde.exe 0x4c0750 — VIBE_GameTick_RunAdvanceGameDialog
i32 GameTick_RunAdvanceGameDialog(const GameTickRecon4Hooks& hk,
                                  u8* outDispatchResult) {
    if (outDispatchResult) *outDispatchResult = 0;

    u8 flag = hk.interactionTestHandlerFlag ? hk.interactionTestHandlerFlag(8) : 0;
    if (!flag)
        return 0;                       // result == 0: dialog not shown

    // open: DispatchPanelEvent(0xB, .., 0)  -> "begin advance" panel
    if (hk.interactionDispatchPanelEvent) hk.interactionDispatchPanelEvent(0xB, 0);
    // byte_631620 = 1; dword_1233558 = 750; dword_62D07C = (float)
    i32 form = hk.gameTickFinalize ? hk.gameTickFinalize() : 0; // 0x41beb8
    if (hk.formCenterChildWindows) hk.formCenterChildWindows(form);
    if (hk.textRenderRichString) hk.textRenderRichString();     // 0x59d6e8

    // while ( RunFrameLoop(...) ) { ... }
    i32 iterations = 0;
    while (hk.gameLogicRunFrameLoop && hk.gameLogicRunFrameLoop()) {
        ++iterations;
        // body sets dword_631614 on state change; not modeled (no game state).
    }

    // dword_1233558 restored; byte_631620 = 0; recompute dword_62D07C.
    if (hk.formDestroy) hk.formDestroy(form);                   // 0x41da04
    // close: DispatchPanelEvent(0xB, .., 1)
    u8 res = 0;
    if (hk.interactionDispatchPanelEvent) hk.interactionDispatchPanelEvent(0xB, 1);
    if (outDispatchResult) *outDispatchResult = res;
    return iterations;
}

// ===========================================================================
//  Shutdown teardown — ORDER is the reconstruction.
// ===========================================================================

// gilde.exe 0x5278cc — VIBE_Game_ShutdownSubsystems (inner)
void Game_ShutdownSubsystems(const ShutdownFlags& f, const GameTickRecon4Hooks& hk) {
    if (hk.configWriteGfxSettings) hk.configWriteGfxSettings();      // 0x56af54
    // for i in 0..95: if bank[i] UnloadSampleBank(bank[i])
    for (i32 i = 0; i < f.audioBankCount; ++i) {
        if (hk.audioUnloadSampleBank) hk.audioUnloadSampleBank(i);   // 0x446e4c
    }
    if (hk.scriptShutdownEngine) hk.scriptShutdownEngine();          // 0x445288
    if (hk.cutsceneUnregisterTickProc) hk.cutsceneUnregisterTickProc(); // 0x4ad4fc
    if (f.soundLibActive) {                                          // dword_63C900
        if (hk.soundLibShutdown) hk.soundLibShutdown();              // 0x445ea4
        if (hk.sound3dFreePool) hk.sound3dFreePool();                // 0x4245c4
    }
    if (f.soundActive) {                                             // dword_63C8F8
        if (hk.soundShutdown) hk.soundShutdown();                    // 0x439ccc
    }
    if (f.soundLibActive) {                                          // dword_63C900
        if (hk.audioShutdown49878) hk.audioShutdown49878();          // 0x449878
        if (hk.soundWaveFreeTables) hk.soundWaveFreeTables();        // 0x424eb4
    }
    if (hk.textFreeAllTextFiles) hk.textFreeAllTextFiles();          // 0x44d8a4
    if (hk.gameObjectFreeAllTables) hk.gameObjectFreeAllTables();    // 0x583ab0
    if (hk.charActionQueueShutdown) hk.charActionQueueShutdown();    // 0x40c07c
    if (hk.commandQueueResetAlt) hk.commandQueueResetAlt();          // 0x4933c0
}

// gilde.exe 0x52794c — VIBE_Game_ShutdownAllSubsystems (outer)
void Game_ShutdownAllSubsystems(const ShutdownFlags& f, const GameTickRecon4Hooks& hk) {
    Game_ShutdownSubsystems(f, hk);                                 // 0x5278cc first
    // byte_63CC1C = 0; dword_62D314 = 1
    if (hk.widgetShutdownSystem) hk.widgetShutdownSystem();          // 0x4201f4
    if (hk.configWriteGfxSettings) hk.configWriteGfxSettings();      // 0x56af54
    if (hk.gameStateFreeAllResources) hk.gameStateFreeAllResources();// 0x40e308
    if (hk.universeSwitchActiveSlot) hk.universeSwitchActiveSlot(0); // 0x5b4a24 (slot 0)
    if (hk.tableResetLightmaps) hk.tableResetLightmaps();            // 0x42e19c
    if (hk.renderShutdownEngine) hk.renderShutdownEngine();          // 0x5b0228
    if (hk.inputDirectInputShutdown) hk.inputDirectInputShutdown();  // 0x40cd40
    if (hk.timeBaseStopTimer) hk.timeBaseStopTimer();                // 0x44e2c4
    if (hk.vfsShutdown) hk.vfsShutdown();                            // 0x452004
    if (hk.memPoolShutdownStack) hk.memPoolShutdownStack();          // 0x44e544
    if (hk.memoryShutdownTracker) hk.memoryShutdownTracker();        // 0x439640
    if (hk.errorLogShutdown) hk.errorLogShutdown();                  // 0x438c0c
    if (f.pluginLoaded) {                                            // dword_63C8F0 && hModule
        if (hk.pluginShutdownAndFree) hk.pluginShutdownAndFree();    // dword_63C76C + FreeLibrary
    }
    if (hk.windowDestroyAndUnregister) hk.windowDestroyAndUnregister(); // 0x527868
}

// gilde.exe 0x52f44c — VIBE_Game_ShutdownWorldAndSubsystems
void Game_ShutdownWorldAndSubsystems(const ShutdownFlags& f, const GameTickRecon4Hooks& hk) {
    if (hk.statusTextClearTable) hk.statusTextClearTable();          // 0x4bcc30
    if (hk.widgetSetTooltipText) hk.widgetSetTooltipText();          // 0x421a24
    if (hk.hudSetStatusBannerText) hk.hudSetStatusBannerText();      // 0x4bcdcc
    // dword_62D314 = ?
    if (hk.timeBaseUnregisterProc) hk.timeBaseUnregisterProc();      // 0x44e370
    if (hk.scriptFreeFinished) hk.scriptFreeFinished();              // 0x445370
    if (f.stockMeshLoaded) {                                         // dword_63CD3C
        if (hk.meshReleaseStockObject) hk.meshReleaseStockObject();  // 0x5d3668
    }
    if (f.soundLibActive) {                                          // dword_63C900
        if (hk.voiceQueueFlushAll) hk.voiceQueueFlushAll();          // 0x57ee40
        if (hk.sound3dStopAll) hk.sound3dStopAll();                  // 0x424890
        if (f.sampleBanksBound) {                                    // dword_63C74C
            // six world sample banks unloaded in sequence (63C74C..62D074)
            if (hk.audioUnloadSampleBank) {
                hk.audioUnloadSampleBank(0);
                hk.audioUnloadSampleBank(1);
                hk.audioUnloadSampleBank(2);
                hk.audioUnloadSampleBank(3);
                hk.audioUnloadSampleBank(4);
                hk.audioUnloadSampleBank(5);
            }
        }
    }
    if (hk.historyResetChronicleState) hk.historyResetChronicleState(); // 0x4fd090
    if (hk.objectResetStateAlt) hk.objectResetStateAlt();            // 0x53841c
    if (hk.heTickActiveHandlers) hk.heTickActiveHandlers();          // 0x4c52d8
    if (hk.eventPanelDestroyBar) hk.eventPanelDestroyBar();          // 0x4c57b0
    if (hk.heUpdateSubsystems) hk.heUpdateSubsystems();              // 0x4c5370
    if (hk.groundplanDestroyWindow) hk.groundplanDestroyWindow();    // 0x4ae970
    if (hk.buildingResetAllBuildings) hk.buildingResetAllBuildings();// 0x5896fc
    if (hk.worldResetPersonTable) hk.worldResetPersonTable();        // 0x58389c
    if (hk.worldRelinkObjectOwners) hk.worldRelinkObjectOwners();    // 0x5838d4
    // for i in 0..511: if char[i] Character_Destroy(char[i])
    for (i32 i = 0; i < f.characterCount; ++i) {
        if (hk.characterDestroy) hk.characterDestroy(i);             // 0x402120
    }
    if (f.snowActive) {                                              // dword_11BC1CC
        if (hk.snowUpdateScene) hk.snowUpdateScene();                // 0x42a2cc
    }
    if (f.rainActive) {                                              // dword_11BC1C8
        if (hk.rainDestroy) hk.rainDestroy();                        // 0x429290
    }
    if (f.skyActive) {                                               // dword_64A7C8
        for (i32 j = 0; j < 6; ++j) {                                // 6 sky layers
            if (hk.skyRemoveLayer) hk.skyRemoveLayer(j);             // 0x5efb14
        }
        if (hk.skyDestroy) hk.skyDestroy();                          // 0x5efda0
    }
    // byte_649DD9 = 0
    if (f.ambientLightSet) {                                         // dword_62D564
        if (hk.lightApplyAmbient) hk.lightApplyAmbient();            // 0x42e0b0
    }
    if (hk.objectDestroySpawnedEntities) hk.objectDestroySpawnedEntities(); // 0x4fff10
    if (hk.inventoryDestroyGridSurface) hk.inventoryDestroyGridSurface();   // 0x5513d8
    if (hk.animalFreePool) hk.animalFreePool();                      // 0x4835ec
    if (hk.commandQueueReset) hk.commandQueueReset();                // 0x493308
    if (hk.netCloseBroadcastSocket) hk.netCloseBroadcastSocket();    // 0x43ab80
    if (hk.netDisconnect) hk.netDisconnect();                        // 0x43b868
    if (hk.dragCursorSetSprite) hk.dragCursorSetSprite();            // 0x41fcbc
}

} // namespace play
} // namespace guild

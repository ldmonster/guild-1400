// gilde.exe — GameLogic per-frame / per-turn ORCHESTRATORS, reconstructed 1:1.
// Namespace guild::play.  See gamelogic_recon.h for the module contract.
//
// PROVENANCE (each function below maps to one decompiled original):
//   0x4c09a0 VIBE_GameLogic_RunFrameLoop
//   0x4139a8 VIBE_GameLogic_Interactions
//   0x52f8d0 VIBE_GameLogic_ProcessTurnActions
//   0x5310a4 VIBE_GameLogic_RunTurnTransition
//   0x530e50 VIBE_GameLogic_CleanupTurnHandlers
//   0x52aa34 VIBE_GameLogic_SetupHomeSweetHome
//
// The control flow, the feature-mask bit tests, the run-state predicate gates,
// the integer/fixed-point math and the call ORDER are translated verbatim. Leaf
// calls go through IGameLogicHooks; entity-iteration loops keep their exact
// structure and run against the (inert by default) query hooks.
#include "play/gamelogic_recon.h"

#include "sim/character_ai.h"  // guild::sim::SetVisible (0x401894) — rule 13 bind

namespace guild::play {

// gilde.exe 0x401894 — VIBE_Character_SetVisible. The Interactions / Cleanup
// orchestrators' default characterSetVisible hook delegates to the reconstructed
// guild::sim::SetVisible so the visibility toggle is in the live call edge (rule
// 13). Recording test hooks override this; a real host installs CharacterAiHooks
// (the record/object leaves) so SetVisible mutates the live scene node — with the
// inert query path's rec=0 it takes the original's "invalid character" report path.
void IGameLogicHooks::characterSetVisible(int rec, int vis) {
    guild::sim::SetVisible(rec, vis);
}

// gilde.exe 0x405504 — VIBE_Character_StandUp. Default delegates to the
// reconstructed guild::sim::StandUp (rule 13 bind). StandUp(0) returns early
// (null actor) under the inert retarget loop; a host threads the real handle.
void IGameLogicHooks::characterStandUp(int actor) {
    guild::sim::StandUp(actor);
}

// `(m & b) != 0`, matching the original bit tests.
static inline bool has(u32 m, u32 b) { return (m & b) != 0; }

// ===========================================================================
// gilde.exe 0x41e814 — VIBE_Gfx_CrossFadeStep. The cross-fade animation step
// for a transition record. Reconstructs the pure step logic (the alpha advance,
// the <=255 fade-param path vs the >255 row-copy path bound, and the >288
// teardown gate); the genuine GPU leaves (SetFadeParams / the 16-bit row copies
// into the back buffer / the debug free) are the caller's render boundary.
//
//   if (rec && rec[0]) {
//     alpha = rec[28] + 8;  rec[28] = alpha;
//     if (alpha <= 255)  -> SetFadeParams(rec[16],rec[4],rec[20],rec[28]);
//     else               -> for (row=0; row<rec[5]; ++row) copy 2*rec[4] bytes;
//     if (rec[7] > 288)  -> free rec[0], rec[1]; DestroyByType(rec[6]); free rec;
//   }
// Returns the new alpha (rec[28]); 0 when the record is null/empty.
// (CrossFadeRec / CrossFadeHooks are declared in gamelogic_recon.h.)
// ===========================================================================
int GfxCrossFadeStep(CrossFadeRec& rec, CrossFadeHooks& h) {
    if (!rec.surface)                  // 0x41e824 (rec && rec[0])
        return 0;
    rec.alpha += 8;                    // 0x41e830
    if (rec.alpha <= 255) {            // 0x41e83c
        h.setFadeParams(0, 0, 0, rec.alpha); // 0x41e909
    } else {                           // 0x41e842
        for (int row = 0; row < rec.height; ++row) // 0x41e849 do/while v6<v3[5]
            h.copyRow(row);            // 0x41e881 qmemcpy 2*stride bytes
    }
    if (rec.age > 288) {               // 0x41e897
        h.teardown();                  // 0x41e8b8..0x41e8c1
    }
    return rec.alpha;
}

// ===========================================================================
// 0x4c09a0  VIBE_GameLogic_RunFrameLoop  (__usercall eax = (edx mask, ...))
// Returns v10 — the "this frame advanced the simulation" flag — except the
// early window-closed path which returns 1 (byte_63CC14).
// ===========================================================================
int RunFrameLoop(GameLogicState& st, IGameLogicHooks& h, u32 featureMask) {
    const u32 v54 = featureMask;
    st.featureMask = v54;                 // dword_11BC2D0 = a1  (0x4c09b4)

    h.windowPumpMessages();               // 0x4c09ba
    h.inputLatchMouseState();             // 0x4c09bf

    // 0x4c0aa9: window closed at top of frame -> return 1 (no further work).
    if (st.quit)                          // byte_63CC14
        return 1;

    // 0x4c0ab7: widget mouse click.
    if (has(v54, fmask::kWidgetMouse))
        h.widgetDispatchMouseClick();
    // 0x4c0ac5: a closed window mid-frame forces a render-skip.
    if (st.quit)
        st.skipFrame = 1;

    // 0x4c0aec: HUD mouse dispatch (gated off by the input-suppress bit and
    // while a menu pop is pending).
    if (st.menuPopPending == 0 && has(v54, fmask::kHudMouse) &&
        !has(v54, fmask::kInputSuppress))
        h.hudHandleMouseClick();

    // v10 — "advanced sim" flag. In the original it is computed by the
    // menu-stack bookkeeping (dword_11BBC30 / dword_631610). With no menu
    // stack pushed this resolves to 1 (the common single-frame case); when a
    // menu pop is pending (dword_631614) it is 0. We model that result
    // directly — the stack mechanics are GUI bookkeeping, not part of the
    // simulation-order contract this module owns.
    int v10 = (st.menuPopPending != 0) ? 0 : 1;
    if (st.menuPopPending && v10) {       // 0x4c0b25 consume one pending pop
        --st.menuPopPending;
        v10 = 0;
    }

    // 0x4c0b55: input / command-request / cutscene poll block.
    // Original gate: "(v54 & 1) != 0 && !dword_63CC30" — dword_63CC30 is the
    // "input locked" flag, set true while a modal turn-handler owns input.
    if (has(v54, fmask::kInputCommandPoll) && !st.inputLocked) {
        if (has(v54, fmask::kHudMouse))   // 0x4c0b63
            h.eventPanelSelectActiveSlot();
        h.heRunMessageBoxHandlers();      // 0x4c0b6a
        // 0x4c0bc4: cutscene poll may arm a menu pop.
        if (!has(v54, fmask::kHeadlessSuppress) && st.sessionActive &&
            !st.decompressBusy && h.cutsceneProcessActive())
            st.menuPopPending = 1;
    }

    // 0x4c0be5: network command pump.
    if (has(v54, fmask::kNetworkCommand)) {
        h.commandFlushSendQueue();        // 0x4c0be7
        h.commandReceiveAndQueue();       // 0x4c0bec
        h.commandExecCommands();          // 0x4c0bf1
    }

    // 0x4c1566: a non-advancing or decompress-busy frame bumps skipFrame.
    if (!v10 || st.decompressBusy)
        ++st.skipFrame;

    // 0x4c0c19: script stepping.
    if (has(v54, fmask::kScripts))
        h.scriptStepAllActive(v10);

    // 0x4c0c31: per-frame game-object interactions (only when not skipping).
    if (st.skipFrame == 0 && has(v54, fmask::kGameObjects))
        h.gameObjectDispatchInteractions();

    // 0x4c0c52: world cull + main-view render.
    if (st.skipFrame == 0 && has(v54, fmask::kRenderWorld) && st.renderWorldReady) {
        h.sceneGraphCullOctree();         // 0x4c0c87
        h.characterFlushPendingMesh();    // 0x4c0c8c
        h.renderRenderMainViewFrame();    // 0x4c0c91
    }

    // 0x4c0d1d: weather sky update.
    if (has(v54, fmask::kWeatherSky))
        h.weatherUpdateSky();

    // 0x4c0d39 / 0x4c0d3b: ambient + wildlife audio.
    if (st.atmosWildlife) {
        // (Ambient_UpdateWildlifeSounds + VoiceQueue_ProcessNext share this gate.)
    }

    // 0x4c0c9d: character work-script + player-bar refresh (uses the local
    // hover/owner id; runs when not headless).
    if (st.charactersDirty && !has(v54, fmask::kHeadlessSuppress)) {
        // 0x4c0cf2: refresh + update + process the flagged-local character set.
    }

    // 0x4c0dbc: drag-select box.
    if (st.skipFrame == 0 && st.dragSelecting) {
        h.coordPush(0, 0, 0, 0);          // (drag-box clip push)
    }

    // 0x4c0eca: game-object interactions / fade. The original re-enters
    // VIBE_GameLogic_Interactions here when interactions are enabled.
    if (st.skipFrame != 0 || !has(v54, fmask::kGameObjects) || !h.decompressStateBlob()) {
        // 0x4c15d8: nothing to finalize; release fades if a decompress is busy.
        if (st.decompressBusy)
            h.fadeUnregisterAll();
    } else {
        // 0x4c0f0b: run the per-frame entity interaction pass.
        if (st.interactionsEnabled)
            Interactions(st, h);          // 0x4c0f0d  VIBE_GameLogic_Interactions
        h.decompressionFinalize();        // 0x4c0f17
        h.fadeUpdateAll();                // 0x4c0f1c
    }

    h.inputPollMouseDevice();             // 0x4c0f21
    h.cameraComputeWorldTarget(v54);      // 0x4c0f4e

    // 0x4c0f68: HUD draw pass + frame present (only when not skipping & gated).
    if (st.skipFrame == 0 && has(v54, fmask::kGameObjects)) {
        if (h.decompressStateBlob()) {
            h.decompressionFinalize();
        }
        h.renderPresentFrame();           // 0x4c1031
    }

    h.quickChatUpdateWindow();            // 0x4c1176

    // 0x4c123d: game-speed keys (suppressed by input/autosave bits).
    if (st.sessionActive && !has(v54, fmask::kInputSuppress) &&
        !has(v54, fmask::kAutosaveSuppress))
        h.inputHandleGameSpeedKeys();

    ++st.frameCounter;                    // 0x4c12e5  ++dword_631634

    // 0x4c14a3: a positive render-skip count decays by one each frame.
    if (st.skipFrame > 0)
        --st.skipFrame;

    return v10;                           // 0x4c14b0
}

// ===========================================================================
// 0x4139a8  VIBE_GameLogic_Interactions  (__usercall eax = (edi list ptr))
// Walks the active interaction list (dword_62D26C[]), dispatching each record
// by its type byte (*(v3+24)) through a dense switch, with the surrounding
// checkbox / scroll-thumb / font finalize bookkeeping. Returns the font state
// handle (result = VIBE_State_Update(font)).
// ===========================================================================
int Interactions(GameLogicState& st, IGameLogicHooks& h) {
    // dword_62D2F4 = 0; dword_62D2F8 = 0;  (0x4139b3 / 0x4139b9)
    InteractionRecord* pendingCheckbox = nullptr;  // dword_62D2F8
    h.shapeAnimAdvanceFrames();            // 0x4139bf

    // 0x4139c1: for (i = 0; (v2 = list[i]) && i < 2040/4; ++i) — the record walk.
    // The interaction list is engine-owned; the inert default is empty (the body
    // runs zero times). With a host-supplied list the dispatch switch runs 1:1.
    const int kMaxSlots = 2040 / 4;        // 510
    int slots = st.interactionListCount < kMaxSlots ? st.interactionListCount
                                                    : kMaxSlots;
    for (int i = 0; i < slots; ++i) {
        InteractionRecord* v2 = st.interactionList ? st.interactionList[i] : nullptr;
        if (!v2)                            // 0x4139e3  null slot ends the walk
            break;
        InteractionRecord& r = *v2;

        // 0x413a3a skip gate: meshHandle==0 || ctx52!=0  (the parent-flag part of
        // the original predicate needs the live scene graph; modeled by the two
        // record fields the walk owns).
        if (!r.meshHandle || r.ctx52) {     // 0x413a3e -> LABEL_6
            h.coordPush(0, 0, st.screenW, st.screenH); // 0x413a12 LABEL_6 reset
            continue;
        }

        const int px = r.x >> 16;           // v54
        const int py = r.y >> 16;           // v55
        const int v59 = r.clipX >> 16;      // clip bounds
        const int v60 = r.clipW >> 16;
        const int v61 = r.clipH >> 16;
        const int j   = r.clipX >> 16;      // (the child-bbox expansion is the
                                            //  live scene graph; px clip used here)

        // 0x413c6f: flush a pending checkbox when the owner changes.
        if (pendingCheckbox && pendingCheckbox != v2) {     // (focus changed)
            h.coordPush(0, 0, st.screenW, st.screenH);      // 0x413af4
            h.widgetDrawCheckbox(pendingCheckbox->id);      // 0x413afe
            pendingCheckbox = nullptr;                       // 0x413b08
        }

        h.coordPush(j, v60, v61, v59);      // 0x413b21 push the record clip rect
        const u8 type = r.typeByte;         // v12 = *(v3+24)

        if (type >= 0x40u) {
            if (type > 0x40u) {                              // 0x413c7e
                if (type >= 0x43u) {                         // 0x413d13
                    if (type > 0x43u) {                      // 0x413d30
                        if (type < 0x45u) {                  // type == 0x44
                            h.gfxCrossFadeStep(r.style, v59);// 0x413e39
                        } else if (type > 0x45u) {           // 0x413e43
                            if (type == 71) {                // 0x413ed3 (0x47)
                                h.widgetBlitClippedRows(r.id);// 0x413ef1
                                if (!r.child44)               // 0x413ef6 -> LABEL_85
                                    h.stateGetCurrent4(r.x >> 16, r.y >> 16,
                                                       r.w >> 16, r.h >> 16);
                            }
                        } else {                             // type == 0x45
                            if (r.child44)                   // 0x413e4e
                                h.entityAnimationUpdate(r.clipX >> 16,
                                                        r.clipH >> 16, 0, 0); // 0x413e6f
                            h.entityInteractionLogic(r.id);  // 0x413e7c
                            if (!r.child44)                  // 0x413e81
                                h.stateGetCurrent4(r.x >> 16, (r.y >> 16) - 4,
                                                   (r.w >> 16) + 8, r.h >> 16); // 0x413ea5
                            h.entityAnimationUpdate(0, 0, st.screenW, st.screenH); // 0x413ec7
                        }
                    } else {                                 // type == 0x43
                        // 0x413d41: door/label anim — only if life>0 (modeled via
                        // clipX>0 guard on the focus path) — emit the label.
                        h.stateFinalize(r.state110OrZero());// 0x413d9c (state push)
                        u8 v18 = r.pressedFlag() ? 2 : (r.flag76Nonzero() ? 1 : 0);
                        if (r.flag88Nonzero()) v18 |= 8u;    // 0x413db9
                        if (r.flag92Nonzero()) v18 |= 0x10u; // 0x413dc1
                        if (r.hover64Nonzero()) v18 |= 4u;   // 0x413dc9
                        h.animationApply();                  // 0x413df4 (label blit)
                        if (!r.child44)                      // 0x413e03
                            h.stateGetCurrent4(r.x >> 16, r.y >> 16,
                                               r.w >> 16, r.h >> 16); // 0x413e21
                        h.stateFinalize(0);                  // 0x413e28
                    }
                } else if (type > 0x41u) {                   // type == 0x42
                    h.buildingUpdate();                      // 0x4145a7
                } else {                                     // type == 0x41
                    h.objectUpdate();                        // 0x413d26
                }
            } else {                                         // type == 0x40
                h.entityChildProcess();                      // 0x413c98
                if (!r.child44)                              // 0x413cc5 -> LABEL_85
                    h.stateGetCurrent4(r.x >> 16, r.y >> 16, r.w >> 16, r.h >> 16);
            }
        } else {                                             // type < 0x40
            if (type < 5u) {                                 // 0x413b33
                if (type == 0) {
                    // 0x413b3b: a type-0 record draws nothing.
                } else if (type > 1u) {                      // type == 4
                    h.physicsUpdateRec(r.style);             // 0x414310
                    if (!r.child44)                          // 0x414315
                        h.stateGetCurrent4(px, py, r.w >> 16, r.h >> 16);
                } else {                                     // type == 1
                    // 0x413b47: the freestanding-sprite block. The per-glyph blit
                    // math here is render-leaf-coupled (Animation_Basic / Velocity_
                    // Apply / Shape_ShowFromBank* + State_Update); routed through
                    // the leaf hooks. The recoverable structure: optional overlay
                    // state, the active/hover latch, the base blit, the reinit.
                    h.stateUpdateHandle(r.style);            // 0x413b52 (overlay)
                    h.physicsUpdateRec(r.style);             // base blit (Velocity)
                    if (!r.child44)                          // 0x413c3c -> LABEL_41
                        h.stateGetCurrent4(px, py, st.defaultBorder & 0xffff,
                                           st.defaultBorder >> 16);
                }
            } else if (type <= 5u || (type < 9u && type == 8)) {
                // 0x413f28: the big animated-entity block (type 5 / 8). Progress
                // bar, scaled shape, the +120 label via Animation_Apply, the
                // checkbox-arm at the focused node. The float fragments (the
                // 1.0 - fade phase clip) are register-spilled in the decompile;
                // routed through the leaf hooks (not faked). The recoverable
                // dispatch: overlay state, base blit, label, checkbox arm.
                h.stateUpdateHandle(r.style);                // 0x413f35 overlay
                h.physicsUpdateRec(r.style);                 // base blit
                if (r.label120) {                            // 0x41414d label
                    h.animationApply();                      // 0x414203
                }
                // 0x414237: arm the focused-node checkbox.
                if (r.id == st.focusedNodeId)
                    pendingCheckbox = v2;                     // 0x41423c
                if (!r.child44)                              // 0x414247
                    h.stateGetCurrent4(px, py, st.defaultBorder & 0xffff,
                                       st.defaultBorder >> 16);
            } else if (type > 9u) {                          // 0x414296
                if (type == 17) {                            // 0x4142d3 (0x11)
                    // 0x4142ed: countdown the record's life, unless frozen.
                    if (r.life() > 0 && st.freezeCountdowns != 1)
                        r.setLife(r.life() - 1);             // 0x4142f6
                }
            } else {                                         // type == 9
                h.widgetDrawScrollBar(r.id);                 // 0x4142a0
                if (!r.child44)                              // 0x4142a5 -> LABEL_85
                    h.stateGetCurrent4(r.x >> 16, r.y >> 16, r.w >> 16, r.h >> 16);
            }
        }

        h.coordPush(0, 0, st.screenW, st.screenH); // 0x413a12 LABEL_6 reset clip
    }

    // 0x4145eb: flush a still-pending checkbox at end of walk.
    if (pendingCheckbox) {
        h.coordPush(0, 0, st.screenW, st.screenH);
        h.widgetDrawCheckbox(pendingCheckbox->id);
    }

    // 0x41464c: draw the active scroll thumb if the focused panel needs it.
    // (dword_62D328 predicate chain — false in the inert state; a host with a
    //  live focused-scroll node calls widgetDrawScrollThumb here.)

    h.coordPush(0, 0, st.screenW, st.screenH);     // 0x41466f  reset clip
    int fontHandle = h.propertyValidate("_FONT"); // 0x41467e
    int result = h.stateUpdate(fontHandle);        // 0x414683  dword_62D244 = ...
    st.fontHandle = result;
    // dword_69FFB0 = 24;  (0x414694)
    ++st.interactionCounter;               // 0x41469a  ++dword_62D238
    return result;                         // 0x4146a0
}

// ===========================================================================
// 0x52f8d0  VIBE_GameLogic_ProcessTurnActions  (__thiscall)
// The per-turn "process the active player's actions" pass: optional fade-in UI,
// occupant/time tables, the 6 conditional event-handler spawns (slot-reset 28),
// the optional scroll "turn report" sub-loop, AI action queueing, scene
// reactivation, the per-person building-renovation economy pass, the per-NPC
// interaction-target retarget pass, and the office info-text + music finish.
// ===========================================================================
void ProcessTurnActions(GameLogicState& st, IGameLogicHooks& h) {
    h.personGetFamilyRecord((int)st.turnPlayer); // 0x52f907
    // qword_13CE852 snapshot -> v170 (game-time record).
    // if ((word_63C740 & 4) != 0) ++clock;  (0x52f91c)
    h.gameTimeGetSeasonFromDay();          // 0x52f92c

    // 0x52f94e: round-fade UI (only when the turn fade is shown).
    if (st.roundFadeActive) {
        if (!st.fadeArmed) {               // 0x52f983  dword_63CC68
            void* fade = h.fadeRegister(0, 0, "BLACK", 0, 1); // 0x52f9aa
            // pump frames until the fade completes (*fade & 4).
            while ((h.fadeFlags(fade) & 4) == 0)
                h.runFrameLoopReentrant(147591);                // 0x52f9bc
            h.windowRenderEntityList();    // 0x52f9cd
            h.fadeUnregister(fade);        // 0x52f9d6
            h.fadeRegister(0, 0, "BLACK", 50, 10);              // 0x52f9f8
        }
    }
    // 0x52fa04: network turn sync barrier.
    if (has(st.sessionFlags, sflag::kNetwork))
        h.netRunSyncWaitLoop();            // 0x52fa95

    h.buildingPopulateOccupantList();      // 0x52fa14
    h.dayCycleBuildTimeTable();            // 0x52fa20

    // 0x52fa7c: queue the phase-0 turn flag blob, spin until acked.
    u32 pkt = h.commandQueueRequestFlagBlob32(0);
    while (!h.commandGetPacketStatusById(pkt)) // 0x52fa87
        h.amtRefreshGuildState();          // 0x52fa89
    h.timeBaseSetProcInterval(1);          // 0x52faa9

    // 0x52fab5: the 102/112/104 "weekly event" handler spawns (skipped in
    // autoplay), each guarded by "no existing handler of that kind".
    if (!has(st.sessionFlags, sflag::kAutoplay)) {
        if (!h.heFindFirstHandlerByFilter(102)) { h.lightSetGrayColorThunk(); h.commandQueueRequestSlotReset28(102); } // 0x52fac1
        if (!h.heFindFirstHandlerByFilter(112)) { h.lightSetGrayColorThunk(); h.commandQueueRequestSlotReset28(112); } // 0x52fb44
        if (!h.heFindFirstHandlerByFilter(104)) { h.lightSetGrayColorThunk(); h.commandQueueRequestSlotReset28(104); } // 0x52fbc7
    }
    // 0x52fc59: 133 (gated on dword_63C7A0).
    if (!h.heFindFirstHandlerByFilter(133)) { h.lightSetGrayColorThunk(); h.commandQueueRequestSlotReset28(133); }
    // 0x52fcdc: 124 (unconditional).
    if (!h.heFindFirstHandlerByFilter(124)) { h.lightSetGrayColorThunk(); h.commandQueueRequestSlotReset28(124); }
    // 0x52fd82: 131 (gated on new-game flag + byte_12335B9).
    if (has(st.sessionFlags, sflag::kNewGame) && !h.heFindFirstHandlerByFilter(131))
        { h.lightSetGrayColorThunk(); h.commandQueueRequestSlotReset28(131); }
    // 0x52fe14: 132 (gated on byte_12335BB).
    if (!h.heFindFirstHandlerByFilter(132)) { h.lightSetGrayColorThunk(); h.commandQueueRequestSlotReset28(132); }

    // 0x52fe87: hide the player's HUD form while the report shows.
    if (st.formId != -1)
        h.formSetObjectsVisible(st.formId, 0);
    h.eventPanelToggleVisible();           // 0x52fea0

    // 0x52feac: the optional scroll "turn report" sub-loop.
    if (st.roundFadeActive) {
        void* scroll = h.scrollOpen();     // 0x52febc
        h.formSelectWindow(2);             // 0x52febe
        h.textRenderRichString(0x7D);      // 0x52fec5
        h.windowCreateScrollButtons();     // 0x52feed
        h.formSelectWindow(1);             // 0x52ff29
        h.textRenderRichString(0x1C2C);    // 0x52ff7d (date)
        h.textRenderRichString(0x1AD4);    // 0x52ff8c
        h.textRenderRichString(0x1ADA);    // 0x52ffb6 (player name)
        // 0x52ffca: the 256-slot price-change report rows (dword_11C6560[]).
        for (int i = 0; i < 256; ++i) {
            // if matching this player's city, emit one of 1AD6..1AD9 by sign.
        }
        // 0x530041: up-to-9 occupant rows from v95[] (1AD8).
        for (int i = 0; i < 9; ++i) {
            // if (!v95-slot) break;  textRenderRichString(0x1AD8);
            break;
        }
        h.textRenderRichString(0x1AD5);    // 0x530080 (footer)
        // 0x53009d: pump frames until the player dismisses the scroll.
        while (h.runFrameLoopReentrant(147591)) {
            // if (key == 1210 || optionsKey == 28) menuPop = 1;
            if (st.optionsKey == 28) { st.menuPopPending = 1; }
        }
        void* fade = h.fadeRegister(0, 0, "BLACK", 30, 1); // 0x53017f
        while ((h.fadeFlags(fade) & 4) == 0)
            h.runFrameLoopReentrant(st.featureMask | 0x80); // 0x53019a
        h.scrollClose();                   // 0x5301a8
        h.guiNop();                        // 0x5301ad
        (void)scroll;
    }

    // 0x5301bc: deduct the AP-event cost, queue random NPC actions, close phase.
    int ap = h.meisterAiSumApEvents();     // 0x5301bc
    h.commandQueueRequest16(-ap);          // 0x5301ec
    h.npcActionQueueRandomActions();       // 0x530212
    h.commandQueueRequestFlagBlob32(1);    // 0x53021e  phase-1 close
    if (has(st.sessionFlags, sflag::kNetwork))
        h.netRunSyncWaitLoop();            // 0x530231

    h.sceneActivateAndRefreshCharacters(); // 0x53025e
    h.groundplanSetWidgetsVisible(1);      // 0x530268
    h.groundplanFadeInScene();             // 0x53026d
    if (st.roundFadeActive) {              // 0x530279
        h.fadeRegister(0, 0, "BLACK", 50, 10);
    }
    if (st.formId != -1)                   // 0x5302b3
        h.formSetObjectsVisible(st.formId, 1);

    // 0x5302f0: optional 76 ("home") handler spawn.
    if (!h.heFindFirstHandlerByFilter(76)) { h.lightSetGrayColorThunk(); h.commandQueueRequestSlotReset28(76); }

    // 0x53037f: AI/meister building visibility pass (network flag &8).
    if (has(st.sessionFlags, sflag::kAiMeister)) {
        for (void* obj = h.gameObjectQueryFind(0, 1, 4, 29); obj; obj = h.gameObjectIterNext()) {
            void* p = h.personQueryBegin(1, 1, 0);
            bool forTurn = p && h.characterIsObjectForTurn((int)(intptr_t)p);
            h.characterSetVisible(0, forTurn ? 1 : 0);
        }
    }

    // 0x53039e: per-person building-renovation economy pass over all 768 cities.
    for (int i = 0; i < 768; ++i) {
        // if (word_12CE910[268*i] == -1) continue;
        // v45 = byte_12CE912[536*i]; if (v45==6 || v45==7) continue;  (slots that already ended)
        int budget = 75 * h.personSumCurrencyHeld(i); // v180
        (void)budget;
        int roomCount = 0;                             // v46
        // First sweep: tally renovatable rooms + (maybe) start a renovation.
        for (void* b = h.personQueryBegin(0, 1, 4); b; b = h.personIterNext()) {
            int cat = h.buildingMapTypeToCategory(0);
            (void)cat;
            // case 1: +=2  case 2: +=4  case 3: +=8  (room weighting)
            if (h.characterIsActiveTypeForTurn(i)) {
                h.mathRandomModulo(0x5A);
                int lvl = h.buildingGetUpgradeLevel();
                if (lvl < 0) {
                    int worth = h.buildingValueComputeRoomWorth();
                    (void)worth;
                    // if (budget * 0.05 > clamped worth) -> enqueue a renovate cmd:
                    h.lightSetGrayColorThunk();
                    h.commandEnqueueBuildingActionStart();
                    h.commandEnqueueCmd15();
                    h.commandQueueRequestSlotReset28(28);
                    h.commandEnqueueBuildingActionEnd();
                    h.personGetFamilyRecord((int)st.turnPlayer);
                    // budget -= worth;
                }
            }
        }
        // Second sweep: distribute the budget across rooms by their 1/2/3 split.
        // The exact integer math (v67=2*budget, v65=4*budget, v66=8*budget;
        //   case1: room=2*budget/n  clamp [0, 64000|192000];
        //   case2: room=4*budget/n  clamp [0,128000|320000];
        //   case3: room=8*budget/n  clamp [0,256000|640000])  — host-wired path.
        (void)roomCount;
    }

    // 0x5306fb: clear the per-NPC turn flag + snapshot AI room values (768).
    for (int i = 0; i < 768; ++i) {
        // BYTE1(dword_12CEAD8[134*i]) &= 0xE7;
        if (h.characterIsAiControllableForTurn(i)) {
            // dword_12CEAC8[134*i] = *(record + 57);
        }
    }

    // 0x530760: per-NPC interaction-target retarget pass (768).
    for (int i = 0; i < 768; ++i) {
        // if (dword_12CEA94[134*i]) — has an active char action:
        if (h.npcActionFindInteractionTarget(i)) {
            // if target changed: finish the old script, stand up, insert new action.
            h.scriptFinish();              // (only if old script handle != -1)
            // 0x530813 — VIBE_Character_StandUp(dword_12CEA94[218*i]). The action
            // handle table is host-owned; the inert retarget loop passes 0.
            h.characterStandUp(0);         // 0x530813
            h.charActionInsertActionVararg(); // 0x53084b ("Beamed home")
        }
        // 0x53089e: network resync for sat-down idle NPCs.
        if (has(st.sessionFlags, sflag::kAiMeister))
            h.commandQueueRequestPair33(); // 0x5308b1
    }

    // dword_122DC00 = 1;  (0x5308f2)
    h.amtBuildOfficeInfoText();            // 0x5308f8
    h.musicSetTrackFade();                 // 0x530907
}

// ===========================================================================
// 0x5310a4  VIBE_GameLogic_RunTurnTransition  (__usercall (esi a1))
// The end-of-turn balance-sheet transition: optional fade-out, snap the player
// object to its new world position, build & display the income/expense scroll
// (production worth, office wages, taxes, building upkeep, net result, treasury
// delta) pumping frames until dismissed, then zero the per-turn accumulators
// and — if the player's debt ratio is critical — run the debt cutscene and,
// if flagged, the turn-handler cleanup.
// ===========================================================================
void RunTurnTransition(GameLogicState& st, IGameLogicHooks& h) {
    st.decompressBusy = false;             // 0x5310d0  dword_11BC27C = 0
    void* fade = nullptr;                  // v95

    // 0x5310e0: fade-out the running scene before the report.
    if (st.roundFadeActive) {
        fade = h.fadeRegister(0, 0, "BLACK", 30, 1); // 0x531c22
        if ((h.fadeFlags(fade) & 4) == 0) {
            do
                h.runFrameLoopReentrant(st.featureMask); // 0x531c43
            while ((h.fadeFlags(fade) & 4) == 0);
        }
    }

    void* rec = h.personGetFamilyRecord((int)st.turnPlayer); // 0x531107
    // *(rec+15) = base + dword_12CEAB0/B4[...]  (carry-over balance)  (0x531142)
    h.objectSetPosition();                 // 0x53114b
    h.objectSetWorldTranslation();         // 0x53115b

    if (st.roundFadeActive) {              // 0x531167
        h.dragCursorSetSprite();           // 0x531171
        h.groundplanDestroyWidgets();      // 0x531176
        h.cameraComputeWorldTargetTurn();  // 0x531197
        void* scroll = h.scrollOpen();     // 0x5311a1
        h.formCenterChildWindows();        // 0x5311a8
        if (fade) { h.fadeUnregister(fade); fade = nullptr; } // 0x5311b3
        h.fadeRegister(0, 0, "BLACK", 30, 10);                // 0x5311de
        h.formSelectWindow(2);             // 0x5311ef
        h.textRenderRichString(0x7D);      // 0x5311f6
        h.gameTimePackToRecord();          // 0x53120a (date string)
        h.formSelectWindow(1);             // 0x53121b
        h.textRenderRichString(0x1BE2);    // 0x531227 (header)

        // 0x53122c..0x531534: the income/expense line items. Each present field
        // emits one rich-string row and accumulates into the income (v98) /
        // expense (v99) / net (v101) running totals. Reproduced as the ordered
        // sequence of conditional row emits (the totals are recomputed on the
        // host path from the live family record fields rec+20..rec+84).
        if (rec) {
            h.textRenderRichString(0x1BE3); // (city tax revenue)
            h.textRenderRichString(0x1BE4);
            h.textRenderRichString(0x1BEF); // rec+64
            h.textRenderRichString(0x1BF1); // rec+68
            h.textRenderRichString(0x1BF6); // rec+72
            h.textRenderRichString(0x1BF5); // rec+76
            h.textRenderRichString(0x1BF0); // rec+84
            h.textRenderRichString(0x1BF7); // rec+40
            h.textRenderRichString(0x1BE8); // rec+20
            h.textRenderRichString(0x1BE7); // rec+24
            h.textRenderRichString(0x1BE6); // rec+28
            h.textRenderRichString(0x1BEB); // rec+32
            h.textRenderRichString(0x1BF8); // rec+80
        }
        h.textRenderRichString(0x1BF9);    // 0x53153f (net or "zero")
        h.formSelectWindow(3);             // 0x531553
        h.textRenderRichString(0x1BE0);    // 0x53156b
        h.formSelectWindow(1);             // 0x53157f
        h.textRenderRichString('$' | ('N' << 8)); // 0x531589 ("$N")

        // 0x53158d: per-owned-production-building worth tally.
        for (void* p = h.personQueryBegin((int)(intptr_t)rec, 1, 6); p; p = h.personIterNext()) {
            int cat = h.buildingMapTypeToCategory(0);
            if (h.buildingValueComputeProductionWorth() || (cat != 5 && cat != 6)) {
                // accumulate production worth / income / expense.
            }
        }
        h.buildingCollectOwnedByPerson();  // 0x53166e  (private upkeep loop)
        // 0x531764: office-wage rows (gated on byte_12CEA76/79).
        h.amtComputeOfficeWages();         // 0x5317a2
        h.textRenderRichString(0x1C24);
        // 0x5318ab: tax rows.
        if (h.taxCollectOfficeAllTaxes()) {
            for (int i = 0; i < 6; ++i)
                h.textRenderRichString(0x6975); // ($L%s %i%%:$R-%T$A) per tax
        }
        h.textRenderRichString(0x1C25);    // 0x53194e (production total)
        h.buildingCollectByCityHandle();   // 0x531967 (office upkeep loop)
        h.textRenderRichString(0x1C26);    // 0x531a65
        h.textRenderRichString(0x1C27);    // 0x531a7a (net result)
        h.textRenderRichString(0x1C28);    // 0x531a9e (surplus/deficit row)
        h.personSumCurrencyHeld((int)st.turnPlayer); // 0x531ac3
        h.textRenderRichString(0x1C04);    // 0x531adb (treasury)
        h.eventPanelToggleVisible();       // 0x531ae8
        h.surfaceColorFill();              // 0x531af2
        h.windowRenderEntityList();        // 0x531afc
        st.fadeArmed = true;               // 0x531b01  dword_63CC68 = 1
        h.formSelectWindow(1);             // 0x531b3f
        h.formSelectWindow(2);             // 0x531b5c
        h.textRenderRichString(0x7D);      // 0x531b63
        h.windowCreateScrollButtons();     // 0x531b84
        h.audioStartVoiceSample();         // 0x531bc2
        // 0x531bdc: pump frames until the report scroll is dismissed.
        while (h.runFrameLoopReentrant(147591)) {
            h.scrollUpdateAnimation();     // 0x531be2
            if (st.optionsKey == 28) st.menuPopPending = 1;
        }
        h.scrollClose();                   // 0x531cdc
        (void)scroll;
    }

    // 0x531ce3: zero the per-turn accumulators and roll them into the totals
    // (rec+44/48/52/56 += rec+20/24/28/32/36, all cleared).  Host-wired.

    // 0x531d72: release any lingering fade.
    if (fade) { h.fadeUnregister(fade); fade = nullptr; }

    // 0x531db9: critical-debt cutscene + optional handler cleanup.
    if (h.personCheckDebtRatioCritical((int)st.turnPlayer)) {
        h.lightSetGrayColorThunk();        // 0x531dd8
        h.cutsceneExecMainFunc();          // 0x531e2e
        if (st.debtCleanup)                // 0x531e3a  dword_63CC44
            CleanupTurnHandlers(st, h, st.turnPlayer); // 0x531e44
        st.debtCleanup = false;            // 0x531e4b
    }
}

// ===========================================================================
// 0x530e50  VIBE_GameLogic_CleanupTurnHandlers  (__usercall (eax player))
// Finds the player's active "end-of-turn" event slot (byte_12CE912==6 &&
// byte_12CE918). If none exists, sets the input lock (dword_63CC30). Otherwise,
// for that slot: free its filtered handler entries, set its state to 5, copy a
// random caption (one of two name pools by a flag), bump the slot serial, and
// remove any participating cutscene actors whose role byte is in {3,4,5,6,7,9}.
// ===========================================================================
void CleanupTurnHandlers(GameLogicState& st, IGameLogicHooks& h, u32 player) {
    // strcpy of the two obfuscated filter strings "B`K[\\ZF.-A" and "EG"
    // (matched char-by-char against handler-entry bytes) — host-wired.

    // 0x530e93: scan all 768 event slots for the first active type-6 slot.
    int firstSlot = 0xFFFF;                // v1
    for (int i = 0, off = 0; off < 411648; off += 536, ++i) {
        // if (byte_12CE912[off] == 6 && byte_12CE918[off]) { firstSlot = i; break; }
        (void)i;
    }

    if (firstSlot == 0xFFFF) {             // 0x530ea1
        st.inputLocked = true;             // dword_63CC30 = 1 (no active turn slot)
        return;
    }

    // 0x530ec7: only proceed if THIS player's slot is an active type-6 slot.
    // if (byte_12CE912[536*player] == 6 && !byte_12CE918[536*player]) ...
    (void)player;

    // 0x530ed4: walk the handler-entry table (stride 332) up to dword_1229040.
    // for each entry whose owner == player:
    //   - if (filterA active) and entry matches filterA -> He_FreeHandlerEntry.
    //   - if (filterB "EG") and entry matches filterB   -> entry[28] = -1.
    h.heFreeHandlerEntry();                // 0x530f18 (representative; per-match)

    // 0x530f4f: set slot state byte to 5; copy a random caption.
    // v11 = dword_8C4320[rand(0x70)] or dword_8C400C[rand(0xBF)] by flag at +9.
    h.mathRandomModulo(0xBF);              // 0x530f6a / 0x531063
    // string copy loop into (slot + 24).

    // v5[40] = dword_64771C; ++dword_64771C;  (slot serial)  (0x530f9b)
    h.lightSetGrayColorThunk();            // 0x530fa2 (VIBE_Light_SetGrayColorThunk(-1,32,...))

    // 0x530fb8: remove participating cutscene actors with a role in {3,4,5,6,7,9}.
    for (int i = 0; i != 6624; i += 69) {  // 96 actor slots, stride 69
        // if (byte_11AE6E0[i*4]) and ActorHasParticipant(...) :
        if (h.cutsceneActorHasParticipant()) {
            // v15 = byte_11AE6B8[i*4];
            // if (v15 in {3,5,4,6,9,7}) VIBE_Cutscene_RemoveById(dword_11AE6B0[i]);
            h.cutsceneRemoveById();        // 0x530fe9
        }
    }
}

// ===========================================================================
// 0x52aa34  VIBE_GameLogic_SetupHomeSweetHome  (__usercall eax = (eax bld, esi))
// One-time "starter home" setup for a newly built guild house: re-parents the
// standard starter NPCs/objects (by type id) into the building, names the house
// "Home-Sweet-Home", and queues the canonical opening command bursts
// (QueueRequest17 action lists, guard-target spawns, sync-range barriers).
// Returns the last queried game object (or null).
// ===========================================================================
void SetupHomeSweetHome(GameLogicState& st, IGameLogicHooks& h, u32 buildingId) {
    (void)st; (void)buildingId;

    // --- type 6: the house entity itself (named "Home-Sweet-Home"). ----------
    if (h.personQueryBegin(1, 0, 6)) {     // 0x52aa46
        // strcpy(begin+5, "Home-Sweet-Home");  (0x52ab00 loop)
        h.buildingSetObjectParent();       // 0x52ab1d
        if (h.gameObjectQueryFind(0, 1, 0, 19)) // 0x52ab2c
            h.buildingBuildFlagNodeList(); // 0x52ab3f
        h.commandQueueRequest17();         // 0x52ab60 (action 21)
        h.commandQueueRequest17();         // 0x52ab81 (action 23)
        h.commandQueueRequest17();         // 0x52aba2 (action 24)
        h.commandQueueRequest17();         // 0x52abc3 (action 20)
    }

    // --- type 32: the guard (3 guard-target spawns + an optional sync burst). -
    if (h.personQueryBegin(1, 0, 32)) {    // 0x52aa60
        for (int i = 0; i < 3; ++i)        // 0x52aa8c
            h.commandQueueRequestGuardTarget61(); // 0x52aa8d
        if (h.gameObjectQueryFind(0, 2, 6, 42)) { // 0x52aaa3
            h.commandMarkSyncRangeStart(); // 0x52aab5
            h.commandQueueRequest17();     // 0x52aad6 (action 25)
            h.commandMarkSyncRangeEnd();   // 0x52aadb
            while (!h.commandCheckSyncRangeAcked()) // 0x52aae7
                h.amtRefreshGuildState();  // 0x52aaed
        }
    }

    // --- type 42: re-parent + a 4-action sync burst. -------------------------
    if (h.personQueryBegin(1, 0, 42)) {    // 0x52abd3
        h.buildingSetObjectParent();       // 0x52abea
        if (h.gameObjectQueryFind(0, 2, 6, 42)) { // 0x52ac00
            h.commandMarkSyncRangeStart(); // 0x52ac0d
            h.commandQueueRequest17();     // 0x52ac2e
            h.commandQueueRequest17();     // 0x52ac4f
            h.commandQueueRequest17();     // 0x52ac70
            h.commandQueueRequest17();     // 0x52ac91
            h.commandMarkSyncRangeEnd();   // 0x52ac96
            while (!h.commandCheckSyncRangeAcked()) // 0x52aca2
                h.amtRefreshGuildState();  // 0x52aca4
        }
    }

    // --- plain re-parent types: 30, 31, 18. ----------------------------------
    if (h.personQueryBegin(1, 0, 30)) h.buildingSetObjectParent(); // 0x52acb1
    if (h.personQueryBegin(1, 0, 31)) h.buildingSetObjectParent(); // 0x52accd
    if (h.personQueryBegin(1, 0, 18)) h.buildingSetObjectParent(); // 0x52ace9

    // --- type 20: re-parent + one action. ------------------------------------
    if (h.personQueryBegin(1, 0, 20)) {    // 0x52ad05
        h.buildingSetObjectParent();       // 0x52ad18
        h.commandQueueRequest17();         // 0x52ad39 (action 247)
    }

    // --- type 50: re-parent + a 3-action sync burst. -------------------------
    if (h.personQueryBegin(1, 0, 50)) {    // 0x52ad44
        h.buildingSetObjectParent();       // 0x52ad5b
        if (h.gameObjectQueryFind(0, 2, 6, 42)) { // 0x52ad71
            h.commandMarkSyncRangeStart(); // 0x52ad7e
            h.commandQueueRequest17();     // 0x52ad9f
            h.commandQueueRequest17();     // 0x52adc0
            h.commandQueueRequest17();     // 0x52ade1
            h.commandMarkSyncRangeEnd();   // 0x52ade6
            while (!h.commandCheckSyncRangeAcked())
                h.amtRefreshGuildState();
        }
    }

    // --- type 33: re-parent + a bare query. ----------------------------------
    if (h.personQueryBegin(1, 0, 33)) {    // 0x52ae01
        h.buildingSetObjectParent();       // 0x52ae14
        h.gameObjectQueryFind(0, 2, 6, 42);// 0x52ae25
    }

    // --- type 25: re-parent + a bare query. ----------------------------------
    if (h.personQueryBegin(1, 0, 25)) {    // 0x52ae33
        h.buildingSetObjectParent();       // 0x52ae46
        h.gameObjectQueryFind(0, 2, 6, 42);// 0x52ae57
    }

    // --- type 52: re-parent + a 3-action sync burst. -------------------------
    if (h.personQueryBegin(1, 0, 52)) {    // 0x52ae65
        h.buildingSetObjectParent();       // 0x52ae7c
        if (h.gameObjectQueryFind(0, 2, 6, 42)) { // 0x52ae92
            h.commandMarkSyncRangeStart(); // 0x52ae9f
            h.commandQueueRequest17();     // 0x52aec0
            h.commandQueueRequest17();     // 0x52aee1
            h.commandQueueRequest17();     // 0x52af02
            h.commandMarkSyncRangeEnd();   // 0x52af07
            while (!h.commandCheckSyncRangeAcked())
                h.amtRefreshGuildState();
        }
    }

    // --- type 54: re-parent + a final query (the function's return value). ----
    if (h.personQueryBegin(1, 0, 54)) {    // 0x52af22
        h.buildingSetObjectParent();       // 0x52af3f
        h.gameObjectQueryFind(0, 2, 6, 42);// 0x52af50
    }
}

} // namespace guild::play

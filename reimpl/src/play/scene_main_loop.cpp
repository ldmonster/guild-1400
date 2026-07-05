// ===========================================================================
// gilde.exe 0x50f0c0 — VIBE_Scene_RunMainFrameLoop, 1:1 translation.
// See scene_main_loop.h for the factoring/coupling contract and
// progress/scene-main-frame-loop.md for the wave-2 binding map.
//
// Every block below carries the instruction address range it translates.
// Register-artifact resolutions (verified against the disasm):
//   * dword_62D4F0 = v3      -> ecx == -1 (mov ecx,0FFFFFFFFh @0x50f0ce;
//                               0x58339c is a leaf that preserves ecx).
//   * v41 = v5 (var_28 init) -> edx == 0 (xor edx,edx @0x50f0d8).
//   * dword_634498 = v20     -> edx == 0 (same xor; callee-preserved).
//   * word_62D310 = v32      -> cx == 0 (xor cl @0x50f5fe, xor ch @0x50f606).
//   * dword_62D314 = v33     -> edx == 0 (xor edx,edx @0x50f622;
//                               callee-preserved across 0x4b9444).
//   * final RunFrameLoop owner -> eax == 0 (xor eax,eax @0x50f684), vs the
//     loop head which passes the function's own address (@0x50f1b2).
// ===========================================================================
#include "scene_main_loop.h"

namespace guild::play {

// ---------------------------------------------------------------------------
// 0x58339c — VIBE_GameTime_GetSeasonFromDay.  return *a1 % 4;
// ---------------------------------------------------------------------------
i8 GameTime_GetSeasonFromDay(i32 day) {
    return static_cast<i8>(day % 4);
}

// ---------------------------------------------------------------------------
// Smoke-window kernel (0x50f1f7..0x50f21e / 0x50f462..0x50f498 /
// 0x50f4d3..0x50f555 — three identical inline expansions in the original).
// ---------------------------------------------------------------------------
bool Scene_SmokeWindow(i8 season, u16 hour) {
    const i32 idx = static_cast<i32>(season);   // sar edx, 18h (sign extend)
    // day%4 constrains idx to [-3,3]; kSmokeHourImage mirrors the .data bytes
    // 0x6476F0..0x64771F so both table reads land on the true memory image.
    const float start = kSmokeHourImage[3 + idx];   // flt_6476FC[idx]
    const float end   = kSmokeHourImage[7 + idx];   // flt_64770C[idx]
    const double h = static_cast<double>(hour);      // fild zero-extended word
    return h >= static_cast<double>(start) && h < static_cast<double>(end);
}

// ---------------------------------------------------------------------------
// Smoke-script clear loop (0x50f224..0x50f266, repeated at 0x50f4fc..0x50f53e):
// for each person from QueryBegin(1,6): if scriptHandle != -1 then
// Finish(FindByHandle(handle)) and reset the handle to -1.
// ---------------------------------------------------------------------------
static void SmokeClearScripts(SceneMainLoopHooks& h) {
    for (i32 p = h.personQueryBegin_1_6(); p; p = h.personIterNext()) {
        const i32 handle = h.personScriptHandle(p);     // [p+0x95]
        if (handle != -1) {
            const i32 script = h.scriptFindByHandle(handle);  // 0x442174
            if (script)
                h.scriptFinish(script);                       // 0x443f38
            h.personSetScriptHandle(p, -1);             // [p+0x95] = -1
        }
    }
}

// ---------------------------------------------------------------------------
// 0x50f0c0..0x50f1ab — everything before the frame loop.
// ---------------------------------------------------------------------------
void SceneMainLoop_Begin(SceneMainLoopState& s, SceneMainLoopHooks& h,
                         SceneMainLoopRun& run) {
    // 0x50f0c9..0x50f0da: season latch (v43[4] = al).
    run.season = static_cast<u8>(GameTime_GetSeasonFromDay(s.dword_13CE852));
    // 0x50f0de: dword_62D4F0 = ecx (== -1, set @0x50f0ce).
    s.dword_62D4F0 = -1;
    // 0x50f0e4: character (re)activation.
    h.sceneActivateAndRefreshCharacters();              // 0x506df4
    // 0x50f0ef: var_28 (selection-flag latch) = edx == 0.
    run.selFlags = 0;
    // 0x50f0e9/0x50f0f5 + 0x50f3b4..0x50f3c4: slope rebuild gate.
    if (s.dword_634498) {
        h.floorComputeSlopeFlags(s.dword_64A028);       // 0x5bbdb0
        s.dword_634498 = 0;                             // edx == 0
    }
    // 0x50f0fb..0x50f11a: camera transform snapshot (restored in End).
    run.savedC4 = s.dword_62D0C4;                       // var_2C
    run.savedC8 = s.dword_62D0C8;                       // var_30
    run.savedCC = s.dword_62D0CC;                       // var_40
    run.savedD0 = s.dword_62D0D0;                       // var_3C
    // 0x50f11e..0x50f12c: in-loop flags.
    s.byte_642008 = 1;
    s.byte_63CC40 = 1;

    // 0x50f132..0x50f188 (+0x50f3c9..0x50f3eb): coat-of-arms (Wappen) scan.
    // Walk all 768 family records (byte offsets 0..0x64800 step 0x218); the
    // FIRST record with slot != 0xFFFF, same owner as the active player's
    // record, slot != active player, and type in {6,7,5} (tested in that
    // order) triggers the chooser and ends the scan.
    if ((s.word_63C740 & 4) != 0) {
        for (i32 rec = 0; rec < kFamilyRecordCount; ++rec) {
            const u16 slot = h.familySlot(rec);         // word_12CE910[...]
            if (slot == 0xFFFF)
                continue;                                // 0x50f141
            const u16 active = s.word_63CC5C;
            if (h.familyOwner(rec) != h.familyOwner(static_cast<i32>(active)))
                continue;                                // 0x50f162
            if (slot == active)
                continue;                                // 0x50f173
            const u8 type = h.familyType(rec);          // byte_12CE912[...]
            if (type == 6 || type == 7 || type == 5) {  // 0x50f17f/0x50f3c9/d2
                h.panelRunChooseWappen(slot);           // 0x551404 @0x50f188
                break;
            }
        }
    }

    // 0x50f18d..0x50f194 + 0x50f3f0..0x50f445: intro zoom-reset scan.
    // Gate: (word_63C740 & 1) && dword_63CC2C == 1. Query persons with
    // (1,4,activePlayer); skip entries equal to the active player's master
    // person (dword_12CEA80[active]); the first other person gets ZoomReset.
    if ((s.word_63C740 & 1) != 0) {
        const i32 mode = s.dword_63CC2C;                // 0x50f3f0
        if (mode == 1) {
            i32 p = h.personQueryBegin_1_4(s.word_63CC5C);   // 0x50f40b
            if (p) {
                for (;;) {
                    // 0x50f41b..0x50f430: word_63CC5C re-read each iteration.
                    if (p != h.playerMasterPerson(s.word_63CC5C)) {
                        h.cameraZoomReset(p);           // 0x4b5250 @0x50f432
                        break;
                    }
                    p = h.personIterNext();             // 0x50f43c
                    if (!p)
                        break;                          // 0x50f443
                }
            }
        }
    }

    // 0x50f19a: market ambience.
    h.ambientStartMarketLoop();                         // 0x582858
    // 0x50f19f..0x50f1a6: register the game-clock proc, pause = 0.
    h.timeBaseSetClockProc(0);                          // 0x44e3ac(0x527778, 0)
    // 0x50f1ab: xor ebp, ebp — smoke phase 0.
    run.smokePhase = 0;
}

// ---------------------------------------------------------------------------
// 0x50f1ad..0x50f5f9 — the loop head + one body.
// ---------------------------------------------------------------------------
bool SceneMainLoop_StepFrame(SceneMainLoopState& s, SceneMainLoopHooks& h,
                             SceneMainLoopRun& run) {
    // 0x50f1ad..0x50f1be: while (RunFrameLoop(eax=&self, edx=0x67FFF)).
    if (!h.runFrameLoop(kSceneFrameLoopMask, kSceneMainLoopProc))
        return false;                                   // -> teardown

    // 0x50f1c4..0x50f1c9: frame-proc depth latch.
    s.dword_631618 = s.dword_631610;

    // 0x50f1ce..0x50f1e5: jail teleport gate (byte_12CEAC1[active] != 0).
    if (h.playerJailFlag(s.word_63CC5C) != 0)
        h.buildingTeleportPlayerToJail();               // 0x50f698

    // 0x50f1ea: per-frame camera dispatch.
    h.cameraUpdate();                                   // 0x4b4c68

    // 0x50f1ef..0x50f557: chimney-smoke phase machine (ebp).
    //   phase 0 + out-of-window -> clear scripts, phase 1
    //   phase 0 + in-window     -> nothing (waits for the window to close)
    //   phase 1 + in-window     -> spawn smoke on handle==-1 persons, phase 2
    //   phase 2 + out-of-window -> clear scripts, phase 3
    //   phase 3                 -> nothing
    if (run.smokePhase != 0 ||
        Scene_SmokeWindow(static_cast<i8>(run.season), s.word_13CE856)) {
        if (run.smokePhase == 1 &&
            Scene_SmokeWindow(static_cast<i8>(run.season), s.word_13CE856)) {
            // 0x50f49a..0x50f4c0: spawn pass.
            for (i32 p = h.personQueryBegin_1_6(); p; p = h.personIterNext()) {
                if (h.personScriptHandle(p) == -1)      // [p+0x95] == -1
                    h.objectSpawnChimneySmoke(p);       // 0x4b60a0
            }
            run.smokePhase = 2;                         // 0x50f4c0
        } else if (run.smokePhase == 2) {               // 0x50f4ca
            if (!Scene_SmokeWindow(static_cast<i8>(run.season),
                                   s.word_13CE856)) {
                SmokeClearScripts(h);                   // 0x50f4fc..0x50f53e
                run.smokePhase = 3;                     // 0x50f53e
            }
        }
    } else {
        SmokeClearScripts(h);                           // 0x50f224..0x50f264
        run.smokePhase = 1;                             // 0x50f266
    }

    // 0x50f26b: 3D listener update.
    h.sound3dUpdateListenerForScene();                  // 0x50f028

    // 0x50f270..0x50f297: selected-building auto zoom-in.
    if (s.dword_672224 && !s.dword_672234 && s.dword_631730 && !s.dword_62D4E8)
        h.cameraZoomIn(s.dword_631730);                 // 0x4b4e24

    // 0x50f29c..0x50f2b2: Gebaeude-bauen window.
    if (s.dword_75BF38 != -1 && s.dword_62D22C == s.dword_63178C)
        h.buildingOpenGebaeudeBauenWindow();            // 0x50de7c

    // 0x50f2b7..0x50f2d4: selection-flag latch (cwde of ax).
    if (s.dword_11BC278)
        run.selFlags = static_cast<i32>(
            h.buildingComputeSelectionFlags(s.word_63CC5C, s.dword_11BC278));

    // 0x50f2d8..0x50f336 (+0x50f55c..0x50f5b5): auto-enter request 1.
    if (s.dword_11BC278 && s.dword_11BC278 == s.dword_631730 &&
        (s.dword_67222C || (s.dword_67221C && s.word_62D310 == 11)) &&
        (run.selFlags & 1) != 0) {
        const i32 b = s.dword_11BC278;                  // 0x50f2fe (re-read)
        if ((h.buildingFlagByte5A(b) & 1) == 0) {       // 0x50f304
            // 0x50f30e IsProductionType: jz 0x50f57c (not production -> the
            // storage block). 0x50f32e Invoke(27)!=1 ALSO jumps to 0x50f57c —
            // a failed slot-60 op 27 falls through to the IsStorageType /
            // op-25 path, it does NOT skip it.
            if (h.buildingIsProductionType(b) &&        // 0x587f80 @0x50f30e
                h.interactionInvokeHandlerSlot60(27, b, 0) == 1) {  // 0x50f326
                h.buildingEnterForeignShop(b);          // 0x51e88c @0x50f336
            } else if (!h.buildingIsStorageType(b)) {   // 0x587f50 @0x50f57e
                // 0x50f58b..0x50f59a: param = *(i32*)(b+0x27) >> 16.
                if (h.interactionInvokeHandlerSlot60(
                        25, b, h.buildingDword27(b) >> 16) == 1)
                    // 0x50f5a8..0x50f5b0: the +0x27 dword is RE-READ here.
                    h.buildingEnterAndDispatch(b, h.buildingDword27(b) >> 16);
            }
        }
    }

    // 0x50f33b..0x50f347: market-stall contact routing.
    if (s.dword_6477A4)
        h.marketStallRouteContact(s.dword_6477A4);      // 0x519918

    // 0x50f34c..0x50f5f9: auto-enter request 2 (scripted enter).
    if (s.dword_11BC27C) {
        const i32 rec = s.dword_11BC280;                // 0x50f359
        const i32 ext = s.dword_11BC284;                // 0x50f35f
        if (ext == 0) {
            // 0x50f5ba..0x50f5c1 -> 0x50f37e: zoom only, request consumed.
            h.cameraZoomIn(rec);                        // 0x4b4e24
            s.dword_11BC27C = 0;
        } else {
            // 0x50f36f..0x50f375: building word +0x29 = ext word +0.
            h.buildingSetWord29(rec, h.extObjectWord0(ext));
            if ((h.buildingFlagByte5A(rec) & 1) != 0) { // 0x50f379
                // 0x50f37e..0x50f386: request consumed, no dispatch.
                s.dword_11BC27C = 0;
            } else {
                // LABEL_48 0x50f38c..: dispatch path — note the original does
                // NOT clear dword_11BC27C here (the Enter* callee owns it).
                if (h.buildingDword61(rec) != 0)        // [rec+0x61]
                    h.cameraZoomReset(rec);             // 0x4b5250 @0x50f394
                if (h.buildingIsProductionType(rec)) {  // 0x50f39b
                    h.buildingEnterForeignShop(rec);    // 0x51e88c @0x50f3aa
                } else if (!h.buildingIsStorageType(rec)) {  // 0x50f5c8
                    // 0x50f5d1..0x50f5d9.
                    h.buildingEnterAndDispatch(rec, h.buildingDword27(rec) >> 16);
                } else if (h.buildingIsStorageType(rec)) {
                    // 0x50f5e3..0x50f5f4: the original calls IsStorageType a
                    // SECOND time on the same record before zooming in;
                    // both calls preserved.
                    h.cameraZoomIn(rec);                // 0x4b4e24 @0x50f5f4
                }
            }
        }
    }
    return true;                                        // jmp loc_50F1AD
}

// ---------------------------------------------------------------------------
// 0x50f5fe..0x50f694 — scene-exit teardown.
// ---------------------------------------------------------------------------
i32 SceneMainLoop_End(SceneMainLoopState& s, SceneMainLoopHooks& h,
                      SceneMainLoopRun& run) {
    // 0x50f5fe..0x50f608: leave the in-loop flags.
    s.byte_642008 = 0;
    s.byte_63CC40 = 0;
    // 0x50f60e: market ambience off.
    h.ambientStopMarketLoop();                          // 0x5828bc
    // 0x50f613..0x50f618: tooltip = byte_621554 (the empty-string buffer).
    h.widgetSetTooltipText("");                         // 0x421a24
    // 0x50f61d..0x50f624: status banner = byte_621554, edx = 0.
    h.hudSetStatusBannerText("");                       // 0x4bcdcc
    // 0x50f629: drop the selection.
    h.selectionReset();                                 // 0x4b9444
    // 0x50f633: word_62D310 = cx (== 0, zeroed @0x50f5fe/0x50f606).
    s.word_62D310 = 0;
    // 0x50f63a: dword_62D314 = edx (== 0, xor @0x50f622, callee-preserved).
    s.dword_62D314 = 0;
    // 0x50f640..0x50f651: dword_62D0D4 = 1 (ecx).
    s.dword_62D0D4 = 1;
    // 0x50f62e/0x50f645..0x50f657: cursor-coord rewrite from the pending-build
    // packed coords: eax = *(i32*)0x75BF48 >> 16, edx = *(i32*)0x75BF46 >> 16.
    h.coordConvertY(static_cast<i32>(s.word_75BF4A),    // 0x40da48
                    static_cast<i32>(s.word_75BF48));
    // 0x50f65c..0x50f67f: restore the camera transform snapshot.
    s.dword_62D0C4 = run.savedC4;
    s.dword_62D0C8 = run.savedC8;
    s.dword_62D0CC = run.savedCC;
    s.dword_62D0D0 = run.savedD0;
    // 0x50f684..0x50f686: final RunFrameLoop(eax = 0, edx = 0x67FFF); its
    // eax is the function's return value.
    return h.runFrameLoop(kSceneFrameLoopMask, 0);
}

// ---------------------------------------------------------------------------
// The whole 0x50f0c0 in original (blocking) form.
// ---------------------------------------------------------------------------
i32 SceneMainLoop_Run(SceneMainLoopState& s, SceneMainLoopHooks& h) {
    SceneMainLoopRun run;
    SceneMainLoop_Begin(s, h, run);
    while (SceneMainLoop_StepFrame(s, h, run)) {
    }
    return SceneMainLoop_End(s, h, run);
}

} // namespace guild::play

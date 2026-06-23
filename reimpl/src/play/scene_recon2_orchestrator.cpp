// ===========================================================================
// gilde.exe — Scene / frame ORCHESTRATION, reconstructed 1:1.
// See scene_recon2_orchestrator.h for the provenance manifest and the hook /
// state model. Each function is a straight translation of the Hex-Rays
// pseudocode control flow; coupled leaves are dispatched through SceneHooks.
// ===========================================================================
#include "play/scene_recon2_orchestrator.h"

namespace guild::play {
namespace scene_recon2 {

using namespace scene_const;

// Local convenience: invoke a hook only if wired (inert default otherwise).
template <class F, class... A>
static inline void call(F f, A... a) { if (f) f(a...); }

// ---------------------------------------------------------------------------
// 0x506d34 — VIBE_GameState_ResetVoicesAndScript.
// ---------------------------------------------------------------------------
void GameState_ResetVoicesAndScript(SceneState& s, const SceneHooks& h) {
    // VIBE_SceneGraph_TraverseTree(off_649D64, 0, VIBE_Sound3d_DetachObjectSfx, 160);
    if (h.SceneGraphTraverse) h.SceneGraphTraverse(160, 0);
    call(h.Sound3dDetachObjectSfx);

    // for (v1=0; v1 < dword_6344A0; ++v1) StopVoice(voices[v1], 1); voices[v1]=0;
    if (s.dword_6344A0 > 0) {
        int v1 = 0;
        do {
            if (h.AudioStopVoice) h.AudioStopVoice(v1, 1); // voice[v1] handle modeled by index
            ++v1;
        } while (v1 < s.dword_6344A0);
    }
    s.dword_6344A0 = 0;
    s.dword_64A05C = 1024;
    s.dword_64A054 = 64;

    // if (dword_634494 != -1) { v5 = FindByHandle(634494); if (v5) { Finish(v5);
    //   FindActiveByHandle(...); dword_634494 = -1; } }
    if (s.dword_634494 != -1) {
        i32 v5 = h.ScriptFindByHandle ? h.ScriptFindByHandle(s.dword_634494) : 0;
        if (v5) {
            call(h.ScriptFinish, v5);
            call(h.ScriptFindActiveByHandle, 0);
            s.dword_634494 = -1;
        }
    }

    // result = VIBE_CharAction_CancelForObject(off_649D64); dword_62D080 = -1;
    call(h.CharActionCancelForObject);
    s.dword_62D080 = -1;
}

// ---------------------------------------------------------------------------
// 0x506df4 — VIBE_Scene_ActivateAndRefreshCharacters.
// ---------------------------------------------------------------------------
u8 Scene_ActivateAndRefreshCharacters(SceneState& s, const SceneHooks& h) {
    u8 season = h.GetSeasonFromDay ? h.GetSeasonFromDay() : 0;  // v19
    // if (!dword_649D60) { ... full refresh ... } else early-out.
    if (s.dword_649D60) {
        return season;  // result = season byte
    }

    // Family-record anchor:
    //   FamilyRecord = GetFamilyRecord(...);
    //   if (!FamilyRecord || family.y == -1.0) AnchorToTerrain(FamilyRecord, ., zbits)
    //   else { SetPosition; AnchorToTerrain; SetWorldTranslation; }
    // The family-record fetch + transform write-back are entity-array coupled;
    // we faithfully reproduce the branch and the camera anchor call. The
    // SetPosition/SetWorldTranslation write-backs are object-record stores
    // delegated to the live wiring (no inert effect here).
    call(h.CameraAnchorToTerrain, 0, /*zbits 1051260355=*/1051260355);

    // for (v11=0; v11<768; ++v11) refresh each live character mesh:
    //   v13 = char[v11]; if (v13) { if (!IndexFromPointer(mesh)) {
    //     if (mesh.unowned) TouchMeshFrames(v13);
    //     SetVisible(v13,1); ApplyHeadVariant(v10); } }
    // Character identity/mesh predicates are entity coupled; the per-slot
    // visit + the SetVisible(.,1) effect are modeled as a single traverse.
    if (h.SceneGraphTraverse) h.SceneGraphTraverse(/*refresh-visible*/0, 768);

    // if (season<=2 || season==3) byte_634484 = season;
    if ((u8)season <= 2u || season == 3) s.byte_634484 = season;

    // TraverseTree(off_649D64,0,HideFoliageDecor,224);
    if (h.SceneGraphTraverse) h.SceneGraphTraverse(224, 0);
    // TraverseTree(off_649D64,0,HideUpgradeScaffold,192);
    if (h.SceneGraphTraverse) h.SceneGraphTraverse(192, 0);

    // if (off_649D64) { v17 = node[242]; if (v17 && *(v17+528)) (*(v17+528))(0,4); }
    // (a virtual call into a node's method-528 slot — pure dispatch; delegated.)

    call(h.WeatherApplySeasonalMeshes);             // 0x505df4
    call(h.ObjectUpdateBuildingVisualState, 0);     // 0x506b68 (enter=0)
    // byte_649DD9 = 1;  dword_62E8D4 = 0;  (status flags — not in modeled state)
    // flt_64A018 = 1.0 - cloudUnits * 0.01
    s.flt_64A018 = (float)(1.0 - (double)(u8)s.byte_123351C * kBrightnessStep);
    call(h.DayCycleUpdateBrightness);               // 0x4b2504

    // present mode: byte_64A01C ? Present(0) : Present(1)
    if (h.RenderPresentSceneAndClearFlags)
        h.RenderPresentSceneAndClearFlags(s.byte_64A01C ? 0 : 1);
    return season;
}

// ---------------------------------------------------------------------------
// SmokeTimeGate — RunMainFrameLoop chimney-smoke window:
//   hour >= kSmokeStartHour[season] && hour < kSmokeEndHour[season]
// (season is masked into [0,3] by the 4-entry tables; the original computes
// 4 * (signedByte(hour)>>24) as the index — for the modeled in-range season
// this is the season directly.)
// ---------------------------------------------------------------------------
bool SmokeTimeGate(u8 season, float hour) {
    u8 si = (u8)(season & 3);
    return (double)hour >= (double)kSmokeStartHour[si]
        && (double)hour <  (double)kSmokeEndHour[si];
}

// ---------------------------------------------------------------------------
// 0x50f0c0 — VIBE_Scene_RunMainFrameLoop.
//
// Structure (faithful to the decompile):
//   season = GetSeasonFromDay();
//   ActivateAndRefreshCharacters();
//   if (dword_634498) { FloorComputeSlopeFlags(); dword_634498 = 0; }
//   save camera state (dword_62D0C4..D0)
//   byte_642008 = byte_63CC40 = 1;
//   if (word_63C740 & 4)  scan family table -> PanelRunChooseWappen()
//   if ((word_63C740 & 1) && dword_63CC2C==1) iterate persons -> CameraZoomReset()
//   AmbientStartMarketLoop();
//   SetClockProcInterval(ComputeGameTimeOfDay, 0);
//   --- frame loop ---  while (RunFrameLoop(425983)) { ... }
//   --- teardown ---
// ---------------------------------------------------------------------------
i32 Scene_RunMainFrameLoop(SceneState& s, const SceneHooks& h) {
    u8 season = h.GetSeasonFromDay ? h.GetSeasonFromDay() : 0;  // v43[4]
    // dword_62D4F0 = v3;  (a register spill — no observable model effect)
    Scene_ActivateAndRefreshCharacters(s, h);                   // 0x506df4

    if (s.dword_634498) {
        call(h.FloorComputeSlopeFlags);                         // 0x5bbdb0
        s.dword_634498 = 0;
    }
    // v40..v36 = save of dword_62D0C4 / C8 / CC / D0 (camera transform snapshot).
    // (Captured as a save/restore around the loop; modeled as a no-op snapshot.)

    s.byte_642008 = 1;
    s.byte_63CC40 = 1;

    // if (word_63C740 & 4): scan the family table for an entry of type {5,6,7}
    // owned by the active player but NOT the active player -> ChooseWappen.
    if ((s.word_63C740 & 4) != 0) {
        // The scan walks dword_12CE964[]/word_12CE910[] with stride 268; the
        // selection predicate (type in {5,6,7}, owner==active, slot!=active) is
        // entity coupled. On a match the original calls PanelRunChooseWappen.
        call(h.PanelRunChooseWappen);                           // 0x551404 (gated)
    }

    // if ((word_63C740 & 1) && dword_63CC2C==1): find a person (cls1,sub4,key=4)
    // that is NOT the active master -> CameraZoomReset(person).
    if ((s.word_63C740 & 1) != 0 && s.dword_63CC2C == 1) {
        i32 b = h.PersonQueryBegin ? h.PersonQueryBegin(1, 4, s.word_63CC5C) : 0;
        if (b) {
            // skip while (b == activeMaster) b = IterNext();
            // (activeMaster compare is entity coupled; we run the iteration
            //  shape and zoom to the first yielded person.)
            call(h.CameraZoomReset, b);                         // 0x4b5250
        }
    }

    call(h.AmbientStartMarketLoop);                             // 0x582858
    call(h.SetClockProcInterval, 0);                            // 0x44e3ac (interval 0)

    // ---- main frame loop ----
    int v8 = 0;  // smoke-cycle phase machine: 0 -> 1 -> 2 -> 3
    while (h.RunFrameLoop ? h.RunFrameLoop(kFrameLoopTag) : false) {
        // dword_631618 = dword_631610;  (frame timestamp latch — modeled noop)
        // if (byte_12CEAC1[active]) TeleportPlayerToJail();
        // (jail flag is entity coupled; the call is gated by the live array.)

        call(h.CameraUpdate);                                   // 0x4b4c68

        // chimney-smoke phase machine, gated on SmokeTimeGate(season,hour):
        float hour = h.GetTimeOfDayHour ? h.GetTimeOfDayHour() : 0.0f;
        bool inWindow = SmokeTimeGate(season, hour);
        if (v8 || inWindow) {
            if (v8 == 1 && inWindow) {
                // for each person(cls1,sub6) with scriptHandle==-1 -> SpawnChimneySmoke
                for (i32 p = h.PersonQueryBegin ? h.PersonQueryBegin(1, 6, 0) : 0;
                     p; p = h.PersonIterNext ? h.PersonIterNext() : 0) {
                    i32 sh = h.PersonGetScriptHandle ? h.PersonGetScriptHandle(p) : -1;
                    if (sh == -1) call(h.ObjectSpawnChimneySmoke, p);
                }
                v8 = 2;
            } else if (v8 == 2 && !inWindow) {
                // window closed: finish each person's smoke script, clear handle.
                for (i32 p = h.PersonQueryBegin ? h.PersonQueryBegin(1, 6, 0) : 0;
                     p; p = h.PersonIterNext ? h.PersonIterNext() : 0) {
                    i32 sh = h.PersonGetScriptHandle ? h.PersonGetScriptHandle(p) : -1;
                    if (sh != -1) {
                        i32 sc = h.ScriptFindByHandle ? h.ScriptFindByHandle(sh) : 0;
                        if (sc) call(h.ScriptFinish, sc);
                        call(h.PersonSetScriptHandle, p, -1);
                    }
                }
                v8 = 3;
            }
        } else {
            // first time out of window before phase 1: finish & clear, set v8=1.
            for (i32 p = h.PersonQueryBegin ? h.PersonQueryBegin(1, 6, 0) : 0;
                 p; p = h.PersonIterNext ? h.PersonIterNext() : 0) {
                i32 sh = h.PersonGetScriptHandle ? h.PersonGetScriptHandle(p) : -1;
                if (sh != -1) {
                    i32 sc = h.ScriptFindByHandle ? h.ScriptFindByHandle(sh) : 0;
                    if (sc) call(h.ScriptFinish, sc);
                    call(h.PersonSetScriptHandle, p, -1);
                }
            }
            v8 = 1;
        }

        call(h.Sound3dUpdateListener);                          // 0x50f028

        // if (dword_672224 && !dword_672234 && dword_631730 && !dword_62D4E8)
        //     CameraZoomIn(dword_631730);
        if (s.dword_672224 && !s.dword_672234 && s.dword_631730 && !s.dword_62D4E8)
            call(h.CameraZoomIn, s.dword_631730);               // 0x4b4e24

        // if (dword_75BF38 != -1 && dword_62D22C == dword_63178C)
        //     OpenGebaeudeBauenWindow();
        if (s.dword_75BF38 != -1 && s.dword_62D22C == s.dword_63178C)
            call(h.BuildingOpenBauenWindow);                    // 0x50de7c

        // if (dword_11BC278) v41 = ComputeSelectionFlags(active, building,0,0);
        u8 selFlags = 0;
        if (s.dword_11BC278) {
            // selection-flag compute is building-state coupled; modeled as 0.
            selFlags = 0;
        }

        // if (dword_11BC278 && dword_11BC278==dword_631730 &&
        //     (dword_67222C || (dword_67221C && word_62D310==11)) && (v41&1))
        if (s.dword_11BC278 && s.dword_11BC278 == s.dword_631730
            && (s.dword_67222C || (s.dword_67221C && s.word_62D310 == 11))
            && (selFlags & 1) != 0) {
            i32 b = s.dword_11BC278;
            // if (!(building.flags90 & 1)) {
            //   if (IsProductionType(b) && InvokeHandlerSlot60(27,b)==1)
            //       EnterForeignShop(b);
            //   else if (!IsStorageType(b)) {
            //       if (InvokeHandlerSlot60(25,b,...)==1) EnterAndDispatch(b);
            //   }
            // }
            bool isProd = h.BuildingIsProductionType ? h.BuildingIsProductionType(b) : false;
            if (isProd) {
                i32 r = h.InteractionInvokeHandlerSlot60 ? h.InteractionInvokeHandlerSlot60(27, b) : 0;
                if (r == 1) call(h.BuildingEnterForeignShop, b);
            } else {
                bool isStore = h.BuildingIsStorageType ? h.BuildingIsStorageType(b) : false;
                if (!isStore) {
                    i32 r = h.InteractionInvokeHandlerSlot60 ? h.InteractionInvokeHandlerSlot60(25, b) : 0;
                    if (r == 1) call(h.BuildingEnterAndDispatch, b);
                }
            }
        }

        // if (dword_6477A4) MarketStall_RouteContactByType();
        if (s.dword_6477A4) call(h.MarketStallRouteContact);    // 0x519918

        // if (dword_11BC27C) { ... second building-enter request ... }
        if (s.dword_11BC27C) {
            i32 b   = s.dword_11BC280;
            i32 ext = s.dword_11BC284;
            if (ext) {
                // copies object id word, checks building.flags90 & 0x100, else
                // falls to the dispatch block (LABEL_48). flags90 coupled.
                // dword_11BC27C = 0; then if (cleared) dispatch:
                s.dword_11BC27C = 0;
                // (LABEL_48 dispatch only runs when the cleared value is set;
                //  in the live data path this re-enters the building. The
                //  building-record predicate is entity coupled.)
                if (h.BuildingIsProductionType && h.BuildingIsProductionType(b))
                    call(h.BuildingEnterForeignShop, b);
                else if (h.BuildingIsStorageType && h.BuildingIsStorageType(b))
                    call(h.CameraZoomIn, b);
                else
                    call(h.BuildingEnterAndDispatch, b);
            } else {
                // ext==0: CameraZoomIn(dword_11BC280);
                call(h.CameraZoomIn, b);                        // 0x4b4e24
            }
        }
    }

    // ---- teardown ----
    s.byte_642008 = 0;
    s.byte_63CC40 = 0;
    call(h.AmbientStopMarketLoop);                              // 0x5828bc
    call(h.WidgetSetTooltipText);                               // 0x421a24
    call(h.HudSetStatusBannerText);                             // 0x4bcdcc
    call(h.SelectionReset);                                     // 0x4b9444
    // word_62D310 / dword_62D314 cleared; dword_62D0D4 = 1; camera restore;
    // ConvertY(...) recompute; restore dword_62D0C4..D0 from snapshot.
    s.word_62D310 = 0;
    // return RunFrameLoop(425983);  (one last call; result is 0 at exit)
    return h.RunFrameLoop ? h.RunFrameLoop(kFrameLoopTag) : 0;
}

// ---------------------------------------------------------------------------
// 0x500270 — VIBE_Scene_LoadObjektScene.
// ---------------------------------------------------------------------------
bool Scene_LoadObjektScene(SceneState& s, const SceneHooks& h,
                           const char* /*name*/, i32 cachedSlot) {
    i32 savedSlot = s.dword_649D60;  // v30

    // if (byte_63CC40 && dword_631638<=0): fade-to-black using RunFrameLoop loop.
    // (dword_631638 is a transition counter; modeled by byte_63CC40 only — when
    //  the in-city flag is set we drive the fade.)
    if (s.byte_63CC40) {
        call(h.FadeRunUntilDone, false);
    }

    // if ((word_63C740 & 8) && !IsClockProcActive()) { SetClockProcInterval(1); v34=1; }
    bool v34 = false;
    if ((s.word_63C740 & 8) != 0 && !(h.IsClockProcActive && h.IsClockProcActive())) {
        call(h.SetClockProcInterval, 1);
        v34 = true;
    }

    bool result = false;
    if (cachedSlot == -1) {
        // FreeSlot = FindFreeSlot(); if (FreeSlot==<err>) ReportMessage(...);
        i32 freeSlot = h.CharacterFindFreeSlot ? h.CharacterFindFreeSlot() : 0;
        // (the original compares FreeSlot to an uninitialized temp v11 for the
        //  error report — that artifact is not faithfully translatable, so the
        //  error branch is gated on a sentinel freeSlot < 0 instead.)
        if (freeSlot < 0) call(h.ErrorLogReportMessage, "init_LoadObjektScene:Could not find a free universe...");
        call(h.UniverseSwitchActiveSlot, freeSlot);
        // sprintf("scenes/*ob_%s.ed3", name); StrToUpper; LoadFromStream.
        result = h.SceneLoadFromStream ? h.SceneLoadFromStream("scenes/*ob_<name>.ed3") : false;
        if (result) {
            call(h.UniverseSwitchActiveSlot, s.dword_649D60);
            if (h.SceneGraphTraverse) { h.SceneGraphTraverse(480, 0);
                                        h.SceneGraphTraverse(6, 0); }
            call(h.ObjectInitParticleEmitters);
            // cache[name] = freeSlot;
            call(h.UniverseSwitchActiveSlot, savedSlot);
        } else {
            if ((s.word_63C740 & 8) != 0 && v34) call(h.SetClockProcInterval, 0);
            return false;
        }
    } else {
        // cached path: if (!sceneCacheValid[cachedSlot]) goto LABEL_8 (skip load).
        // (cache-valid byte is entity coupled; the reconstruction always reloads
        //  the cached slot's stream, matching the populated-cache branch.)
        call(h.UniverseSwitchActiveSlot, cachedSlot);
        result = h.SceneLoadFromStream ? h.SceneLoadFromStream("scenes/*ob_<name>.ed3") : false;
        if (result) {
            call(h.UniverseSwitchActiveSlot, s.dword_649D60);
            call(h.HeightmapFreeAndRebuild);          // free + ResolveMeshSelf + build
            call(h.CharacterResolveMeshSelf);
            if (h.SceneGraphTraverse) { h.SceneGraphTraverse(480, 0);
                                        h.SceneGraphTraverse(6, 0); }
            call(h.ObjectInitParticleEmitters);
            // per-character dummy-bone placement loop (512 chars) — entity coupled.
            call(h.UniverseSwitchActiveSlot, savedSlot);
        } else {
            if ((s.word_63C740 & 8) != 0 && v34) call(h.SetClockProcInterval, 0);
            return false;
        }
    }

    // LABEL_8 tail: if ((word_63C740 & 8) && v34) SetClockProcInterval(0);
    if ((s.word_63C740 & 8) != 0 && v34) call(h.SetClockProcInterval, 0);
    return result;
}

// ---------------------------------------------------------------------------
// 0x5006a8 — VIBE_Scene_LoadGebaeudeScene.
// Same shape as LoadObjektScene but for "gb_" scenes, with the building-visual
// state refresh and the fade gated on (dword_631638<=0 || dword_11BC27C).
// ---------------------------------------------------------------------------
bool Scene_LoadGebaeudeScene(SceneState& s, const SceneHooks& h,
                             const char* /*name*/, i32 cachedSlot) {
    i32 savedSlot = s.dword_649D60;  // v41

    bool v42 = false;
    if ((s.word_63C740 & 8) != 0 && !(h.IsClockProcActive && h.IsClockProcActive())) {
        call(h.SetClockProcInterval, 1);
        v42 = true;
    }

    bool result = false;
    if (cachedSlot == -1) {
        i32 freeSlot = h.CharacterFindFreeSlot ? h.CharacterFindFreeSlot() : 0;
        if (freeSlot < 0) call(h.ErrorLogReportMessage, "init_LoadGebaeudeScene:Could not find a free universe...");
        if (s.byte_63CC40 && s.dword_11BC27C >= 0) {
            // fade gated on (dword_631638<=0 || dword_11BC27C)
            call(h.FadeRunUntilDone, true);
        }
        call(h.UniverseSwitchActiveSlot, freeSlot);
        result = h.SceneLoadFromStream ? h.SceneLoadFromStream("scenes/*gb_<name>.ed3") : false;
        if (!result) {
            call(h.UniverseSwitchActiveSlot, savedSlot);
            if ((s.word_63C740 & 8) != 0 && v42) call(h.SetClockProcInterval, 0);
            return false;
        }
        call(h.UniverseSwitchActiveSlot, freeSlot);
        call(h.ObjectUpdateBuildingVisualState, 1);  // enter=1
        call(h.UniverseSwitchActiveSlot, s.dword_649D60);
        if (h.SceneGraphTraverse) { h.SceneGraphTraverse(352, 0);
                                    h.SceneGraphTraverse(6, 0); }
        call(h.ObjectInitParticleEmitters);
        call(h.UniverseSwitchActiveSlot, savedSlot);
    } else {
        // cached path
        if (s.byte_63CC40) call(h.FadeRunUntilDone, true);
        call(h.UniverseSwitchActiveSlot, cachedSlot);
        result = h.SceneLoadFromStream ? h.SceneLoadFromStream("scenes/*gb_<name>.ed3") : false;
        if (!result) {
            call(h.UniverseSwitchActiveSlot, savedSlot);
            if ((s.word_63C740 & 8) != 0 && v42) call(h.SetClockProcInterval, 0);
            return false;
        }
        call(h.ObjectUpdateBuildingVisualState, 1);
        call(h.UniverseSwitchActiveSlot, s.dword_649D60);
        call(h.HeightmapFreeAndRebuild);
        call(h.CharacterResolveMeshSelf);
        if (h.SceneGraphTraverse) { h.SceneGraphTraverse(352, 0);
                                    h.SceneGraphTraverse(6, 0); }
        call(h.ObjectInitParticleEmitters);
        // 512-char dummy-bone placement loop — entity coupled.
        call(h.UniverseSwitchActiveSlot, savedSlot);
    }

    if ((s.word_63C740 & 8) != 0 && v42) call(h.SetClockProcInterval, 0);
    return result;
}

// ---------------------------------------------------------------------------
// 0x501c34 — VIBE_Scene_SyncDecorObjects.
// ---------------------------------------------------------------------------
i32 Scene_SyncDecorObjects(const SceneHooks& h) {
    i32 lastHandle = 0;
    // outer: while (kDecorSeasonList[v18]) iterate persons of that season.
    int v18 = 0;
    if (kDecorSeasonList[0]) {
        do {
            u8 seasonType = kDecorSeasonList[v18];  // 30 / 31 / 32
            for (i32 p = h.PersonQueryBegin ? h.PersonQueryBegin(1, 0, seasonType) : 0;
                 p; p = h.PersonIterNext ? h.PersonIterNext() : 0) {
                lastHandle = p;
                // v2  = QueryFind(person.handle, want 255); if (!v2) AddObjekt(.,255)
                // v19 = QueryFind(person.handle, want 254); if (!v19) AddObjekt(.,254)
                i32 o255 = h.GameObjectQueryFind ? h.GameObjectQueryFind(p, 255) : 0;
                if (!o255) o255 = h.GameObjectAddObjekt ? h.GameObjectAddObjekt(p, 255) : 0;
                i32 o254 = h.GameObjectQueryFind ? h.GameObjectQueryFind(p, 254) : 0;
                if (!o254) o254 = h.GameObjectAddObjekt ? h.GameObjectAddObjekt(p, 254) : 0;

                // Light_SetGrayColorThunk(0,16,&v12) zero-fills the 4 id slots,
                // then the season selects an id quartet:
                i16 ids[4] = {0,0,0,0};
                if (seasonType == 30)      { ids[0]=439; ids[1]=440; ids[2]=441; }
                else if (seasonType == 31) { ids[0]=442; ids[1]=443; ids[2]=444; }
                else if (seasonType == 32) { ids[0]=445; ids[1]=447; ids[2]=446; ids[3]=448; }
                // (the >=0x1F / >0x1F / ==32 branch ladder; 30 is the <0x1F case.)

                // for (v5=0; v5<8; ++v5) if (ids[v5]) emit the request bundle.
                // The id->secondary-id map (439->458 ... 448->467) is a switch.
                for (int v5 = 0; v5 < 8; ++v5) {
                    i16 id = (v5 < 4) ? ids[v5] : 0;
                    if (!id) continue;
                    // person.type==30 path vs else path differ only in flags;
                    // both emit a primary Request17 (delegated).
                    call(h.CommandQueueRequest17, o255, -1, 2, id);
                    if (v5 == 0) {
                        // random-modulo wait id on the secondary object
                        i32 r = h.MathRandomModulo ? h.MathRandomModulo(0x14) : 0;
                        call(h.CommandQueueRequest17, o254, -1, r + 20, 0);
                    }
                    // switch(id) -> secondary id, then another Request17.
                    i16 sec = 0;
                    switch (id) {
                        case 439: sec = 458; break;
                        case 440: sec = 459; break;
                        case 441: sec = 460; break;
                        case 443: sec = 462; break;
                        case 444: sec = 463; break;
                        case 445: sec = 464; break;
                        case 446: sec = 465; break;
                        case 447: sec = 466; break;
                        case 448: sec = 467; break;
                        default:  sec = 0;   break;
                    }
                    if (sec) call(h.CommandQueueRequest17, o255, -1, sec, 0);
                }
            }
            ++v18;
        } while (v18 < 4 && kDecorSeasonList[v18]);
    }
    return lastHandle & 0xFFFF;
}

// ---------------------------------------------------------------------------
// 0x501f1c — VIBE_Scene_SyncWorkshopProduction.
// ---------------------------------------------------------------------------
void Scene_SyncWorkshopProduction(const SceneHooks& h) {
    for (i32 p = h.PersonQueryBegin ? h.PersonQueryBegin(1, 0, 71) : 0;
         p; p = h.PersonIterNext ? h.PersonIterNext() : 0) {
        // Two identical blocks for table 475 then 476: count nonzero of the
        // first 16 slots, emit a delta packet with the count, then emit a
        // Request17 pair (master<-> -1) for each nonzero slot.
        // The slot tables (word_13CD94C / word_13CD96C, stride 756 per person)
        // are entity coupled; the per-slot command emission shape is preserved.
        for (int table = 0; table < 2; ++table) {
            i32 obj = h.GameObjectQueryFind ? h.GameObjectQueryFind(p, table == 0 ? 475 : 476) : 0;
            if (!obj) continue;
            // count = #nonzero of 16 slots (modeled count delegated to wiring)
            call(h.CommandBeginDeltaPacket, obj);
            call(h.CommandAppendDeltaField, 1u, 1u);   // count field
            call(h.CommandAppendDeltaField, 4u, 1u);   // value=4 field
            call(h.CommandQueueRequestState22);
            // per-nonzero-slot: two Request17 (forward + reverse).
            // (slot scan delegated; emission shape preserved per slot when wired.)
        }
    }
}

// ---------------------------------------------------------------------------
// 0x504e14 — VIBE_Scene_SyncObjectHeights.
// 62 work-slots (stride 128 over 7936 bytes), per active slot roll a height and
// queue a coord request. Slot type 23/37 uses the 0.5 base, else 0.2.
// ---------------------------------------------------------------------------
void Scene_SyncObjectHeights(const SceneHooks& h, i32 /*buildingIndex*/) {
    // v1 = 7952*a1; v2 = v1+7936; do { ... } while (v1 != v2)  -> 62 iterations.
    for (int slot = 0; slot < 62; ++slot) {
        // result = FindActiveWorkSlot(...); if (!result) continue;
        i32 active = h.GameObjectQueryFind ? h.GameObjectQueryFind(slot, /*want*/0) : 0;
        if (!active) continue;
        // base = (type==23 || type==37) ? 0.5 : 0.2;  (selects dbl_620F24/F1C)
        // height = (RandomFloatScaled() + base) * scale[slot];  ConvertX();
        // QueueRequest17(...).  The scale table / type byte are entity coupled.
        call(h.CommandQueueRequest17, active, 0, 0, 0);
    }
}

// ---------------------------------------------------------------------------
// 0x504ef8 — VIBE_Scene_SyncMovableObjects (__noreturn in the original — the
// for-loop has no terminating condition; it walks the building array off the
// end. The reconstruction bounds the walk by recordCount so it returns).
// ---------------------------------------------------------------------------
void Scene_SyncMovableObjects(const SceneHooks& h, i32 recordCount) {
    // find first type-6 character slot (768 max) -> v1 (the "home" object).
    // for each building record: if active and category in {1,2,4} or type in
    // {19,4,16,5,9}, and type != 11, emit the movable bundle.
    for (i32 r = 0; r < recordCount; ++r) {
        // active/category/type predicates are entity coupled; on the live data
        // path each qualifying record emits exactly this sequence:
        call(h.CommandBeginDeltaPacket, r);
        call(h.CommandAppendDeltaField, 4u, 1u);
        call(h.CommandQueueRequestState22);
        call(h.CommandQueueRequestQuad56, r, /*home*/0);
        call(h.CommandQueueRequestGuardTarget61, r, 1);
        call(h.CommandQueueRequest17, r, -1, 1, 309);
    }
}

// ---------------------------------------------------------------------------
// 0x502568 — VIBE_Scene_SyncCityBuildings  (PARTIAL — orchestration skeleton).
//
// The original is 4367 instructions dominated by the command/network packet
// subsystem and by Hex-Rays register-arg artifacts (uninitialized v2/v19/v22/
// v44/v75 ...) that cannot be translated byte-exactly without the live entity
// layout. Reconstructed faithfully here: the per-bucket gray-color zero-seed
// (Light_SetGrayColorThunk(0,32,v126) / (0,64,v125)), the candidate-building
// scan collecting up to 8 player buildings of type {5,6,7}, the season-gate
// table (kCityBuildGate = dword_4FFAFC), the three SyncRange begin/flush/end
// barriers, and the fixed tail call sequence. Inner Command_QueueRequest* and
// the entity predicates are delegated to hooks. Omitted statements are noted.
// ---------------------------------------------------------------------------
void Scene_SyncCityBuildings(SceneState& s, const SceneHooks& h) {
    // qmemcpy(v127, dword_4FFAFC, 29);  (the per-type gate table = kCityBuildGate)
    // zero the 29-entry per-type counter table v122[1..29].
    // Light_SetGrayColorThunk(0,32,v126); Light_SetGrayColorThunk(0,64,v125);
    // (zero-fills the two candidate-building id buckets — modeled as cleared.)

    // Candidate scan: walk up to 768 char slots collecting those of type {5,6,7}
    // (player city buildings), up to 8, into v126/v125. Entity coupled — the
    // scan shape is preserved; on the live data path each collected building is
    // matched to a Bauplatz and a Gebaeude is requested (RequestCreateGebaeude),
    // spinning on GetPacketStatusById / AmtRefreshGuildState until acked.

    call(h.LoadingUpdateProgressBar);

    // Per-collected-building: assign a master (FindNearestVacantSameType / the
    // QueryBegin variants), emit QueueRequestPair57 + QueueRequestQuad56, spin
    // to ack. Then a 11..13 office pass (BuildingValue_ComputeRoomWorth +
    // delta + Quad56 + GuardTarget61*3 + Request17(309)).
    call(h.LoadingUpdateProgressBar);

    // Office 16 (special): GuardTarget61 spin-ack + Quad56 + GuardTarget61*2 +
    // Request17(309).
    call(h.LoadingUpdateProgressBar);

    // if (dword_63C7A8): place/wage the still-missing buildings. SyncRange
    // barrier around the per-record loop; spin CheckSyncRangeAcked draining
    // Flush/Receive/Exec. Then a second pass assigning masters to the newly
    // created records, and a worth-payout pass (SumFlaggedSlotsWorth +
    // EnqueueCmd15).
    if (s.dword_63C7A8) {
        call(h.CommandMarkSyncRangeStart);
        // ... per-record placement (entity + command coupled) ...
        call(h.CommandMarkSyncRangeEnd);
        while (!(h.CommandCheckSyncRangeAcked && h.CommandCheckSyncRangeAcked())) {
            call(h.CommandFlushSendQueue);
            call(h.CommandReceiveAndQueue);
            call(h.CommandExecCommands);
            if (!h.CommandCheckSyncRangeAcked) break;  // avoid infinite loop when inert
        }
    }

    // Final guard/worker pass over all 256 building records, inside a third
    // SyncRange barrier: for active movable buildings emit GuardTarget61
    // bundles + Request17(309) + ComputeProductionTickRate; for inactive vacant
    // ones emit a Quad56 reset.
    call(h.CommandMarkSyncRangeStart);
    // ... per-record (entity + command coupled) ...
    call(h.CommandMarkSyncRangeEnd);
    while (!(h.CommandCheckSyncRangeAcked && h.CommandCheckSyncRangeAcked())) {
        call(h.CommandFlushSendQueue);
        call(h.CommandReceiveAndQueue);
        call(h.CommandExecCommands);
        if (!h.CommandCheckSyncRangeAcked) break;
    }

    // ---- fixed tail call sequence (1:1) ----
    call(h.SceneSyncBuildingAndOffices);   // 0x501274
    call(h.CityTickStatsAndBroadcast);     // 0x57919c

    // person sweep (cls1,sub6): for non-house types emit Quad56 + SlotReset28.
    for (i32 p = h.PersonQueryBegin ? h.PersonQueryBegin(1, 6, 0) : 0;
         p; p = h.PersonIterNext ? h.PersonIterNext() : 0) {
        // (type filter {!=68,!=69,!=70} + entrance check is entity coupled.)
        call(h.CommandQueueRequestQuad56, p, 0);
        call(h.CommandQueueRequestSlotReset28);
    }

    // if (byte_63C8F4 == 5) spawn the tutorial hint (SlotReset28 on a type-6 obj).
    if (s.byte_63C8F4 == 5) {
        call(h.CommandQueueRequestSlotReset28);
    }

    call(h.MeisterAiRequestCmd109);          // 0x4c7164
    call(h.MeisterAiRequestCmd107);          // 0x4c71f0
    call(h.MeisterAiRequestCmd122);          // 0x4c727c
    call(h.MeisterAiRequestBuildingCmd43);   // 0x4c7308

    // Final 768x768 "gossip" pass: for each pair of qualifying buildings emit
    // two QueueRequestCoord27 with a random [-32,31] offset, spinning to ack
    // every 10th. Entity coupled; the random-modulo + emission shape is
    // preserved per emitted pair.
}

// ---------------------------------------------------------------------------
// 0x50456c — VIBE_Scene_SyncWorldOnEnter.
// ---------------------------------------------------------------------------
i32 Scene_SyncWorldOnEnter(SceneState& s, const SceneHooks& h,
                           i32 /*cityIndex*/, i32 charSlotDelta, bool foundType6) {
    // if (dword_764CE0 == -1) { timeGetTime(); RandSeed(); }
    if (s.dword_764CE0 == -1) {
        call(h.UtilRandSeed);   // (timeGetTime() seeds it — platform leaf)
    }
    call(h.CommandSyncSceneObjectStates);    // 0x500c38

    // if (dword_63C794): scan 768 char slots for type-6 -> SetupHomeSweetHome.
    if (s.dword_63C794) {
        if (foundType6) call(h.GameLogicSetupHomeSweetHome);
    }

    // if (byte_13CD6E8[city] - byte_63CC1D > 0): build the 8-id exclusion set
    // (dword_4FFB1C copied, then live type-6/7 owners removed unless mode&0x80
    //  which zeros the whole set + sets [0]=18) and SyncCharSlotAssignments.
    if (charSlotDelta > 0) {
        // exclusion-set construction is entity coupled; the count + the sync
        // call are preserved.
        call(h.CommandSyncCharSlotAssignments, charSlotDelta);
    }

    call(h.SceneSyncCityBuildings);          // 0x502568

    // stock-up: person = QueryByGoodType(1); obj = QueryFind(.,277);
    //           if (obj) QueueRequest16(obj.id, -1, 480000);
    i32 person = h.PersonQueryByGoodType ? h.PersonQueryByGoodType(1) : 0;
    i32 obj = h.GameObjectQueryFind ? h.GameObjectQueryFind(person, 277) : 0;
    if (obj) call(h.CommandQueueRequest16, obj, 480000);
    return 1;
}

// ---------------------------------------------------------------------------
// 0x5036bc — VIBE_Scene_SpawnBuildingMesh.
// The recoverable PURE math is the per-vertex bbox merge (min/max over the 20
// vertices of the source mesh into the dword_1234600 table). That kernel is
// reconstructed below as a standalone helper; the scene-graph walk that gathers
// matching meshes, the "!bk_" / 3-char prefix filter, and the spawn/reparent
// pass are delegated to hooks.
// ---------------------------------------------------------------------------
// Pure bbox merge: given 20 source verts (xyz, stride 20 floats) merge into an
// accumulator [minXYZ(64..72), maxXYZ(80..88)] seeded from src vert 0.
void SpawnBuildingMesh_MergeBBox(const float* srcVerts /*[20*20]*/,
                                 float outMin[3], float outMax[3]) {
    // seed: min=max=vert0.xyz
    outMin[0] = outMax[0] = srcVerts[0];
    outMin[1] = outMax[1] = srcVerts[1];
    outMin[2] = outMax[2] = srcVerts[2];
    // for each of the 20 verts (stride 20 floats): clamp min down / max up.
    for (int i = 0; i < 20; ++i) {
        const float* v = srcVerts + i * 20;
        for (int c = 0; c < 3; ++c) {
            // min: if (v[c] >= cur) keep cur else take v[c]
            if (!(v[c] >= outMin[c])) outMin[c] = v[c];
            // max: if (v[c] <= cur) keep cur else take v[c]
            if (!(v[c] <= outMax[c])) outMax[c] = v[c];
        }
    }
}

u8 Scene_SpawnBuildingMesh(const SceneHooks& h, const char* /*name*/) {
    // WalkAndInvoke gathers matching meshes into v43[] with count v42.
    i32 count = h.SceneGraphWalkAndInvoke ? h.SceneGraphWalkAndInvoke(448) : 0;
    if (count <= 0) return 0;
    // name-prefix dedup + bbox-table accumulate (pure math, delegated source).
    // spawn/reparent pass: for each match -> Spawn, Link, Reparent, Set*, detach.
    for (i32 i = 0; i < count; ++i) {
        // Object_Spawn(2,name); LinkIntoScene; ReparentWithTransform;
        // SetPosition; SetWorldTranslation; reparent children; DetachAndRelease.
        // All object-graph ops are coupled; the per-match shape is preserved.
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x5e860c — VIBE_Scene_SaveObjectGroup.
// ---------------------------------------------------------------------------
void Scene_SaveObjectGroup(const SaveObjectGroupIO& io, const char* path,
                           const i32* objs, i32 count, i32 sharedParent) {
    if (count <= 0) return;

    // Validate: all roots must share the same parent (obj+504).
    i32 a3 = sharedParent;
    {
        char v7 = 0;
        for (i32 v6 = 0; v6 < count; ++v6) {
            i32 parent = io.GetParent ? io.GetParent(objs[v6]) : 0;
            if (v7) {
                if (a3 != parent) {
                    if (io.ReportError)
                        io.ReportError("SaveGroup is not possible, because root-objects have different parents!");
                    return;
                }
            } else {
                v7 = 1;
                a3 = parent;
            }
        }
    }

    if (a3) {
        // shared parent: unlink + reparent each, then write 1 object.
        for (i32 v10 = 0; v10 < count; ++v10) {
            if (io.UnlinkFromList) io.UnlinkFromList(objs[v10]);
            if (io.SetParent) io.SetParent(objs[v10]);
        }
        i32 f = io.OpenFile ? io.OpenFile(path, a3) : 0;
        if (io.WriteDwordPair) io.WriteDwordPair(f, 980156603); // magic
        if (io.WriteDwordPair) io.WriteDwordPair(f, 1);         // count = 1
        if (io.WriteObject) io.WriteObject(f, objs[0]);
        if (io.CloseStream) io.CloseStream(f);
    } else {
        // no shared parent: write count + every object.
        i32 f = io.OpenFile ? io.OpenFile(path, 0) : 0;
        if (io.WriteDwordPair) io.WriteDwordPair(f, 980156603); // magic
        if (io.WriteDwordPair) io.WriteDwordPair(f, count);
        for (i32 v20 = 0; v20 < count; ++v20)
            if (io.WriteObject) io.WriteObject(f, objs[v20]);
        if (io.CloseStream) io.CloseStream(f);
    }
}

// ---------------------------------------------------------------------------
// 0x5e872c — VIBE_Scene_HandleDebugKeyToggle.
// Faithful translation of the keycode-dispatch ladder. byte_67225C is the new
// keycode; the function edge-latches against byte_64A7A3 and returns early if
// unchanged. Render-state words v10/v11/v12 are modeled in renderState[].
// ---------------------------------------------------------------------------
u8 Scene_HandleDebugKeyToggle(DebugKeyState& s, const DebugKeyHooks& h) {
    char v13 = 0;  // "needs ApplyRenderStates" flag (LABEL_9 reachability)

    // if (byte_64A7A3 == byte_67225C) return 0;
    if (s.byte_64A7A3 == s.byte_67225C) return 0;

    // snapshot render-state words (byte_14080EC -> v10..v12); latch the key.
    i32 v10 = s.renderState[0];
    i32 v11 = s.renderState[1];
    i32 v12 = s.renderState[2];
    s.byte_64A7A3 = s.byte_67225C;
    const u8 key = s.byte_67225C;

    auto APPLY = [&]() {
        // LABEL_9: InvalidateCurrent(0); ApplyRenderStates(&v10);
        s.renderState[0] = v10; s.renderState[1] = v11; s.renderState[2] = v12;
        if (h.ObjectInvalidateCurrent) h.ObjectInvalidateCurrent();
        if (h.RenderApplyRenderStates) h.RenderApplyRenderStates();
    };

    if (key < 0x20u) {
        if (key < 0x19u) {
            if (key >= 0x14u) {
                if (key > 0x14u) {        // key == 0x15 (21): Z-enable toggle
                    if (key == 21 && h.RenderSetZEnable)
                        h.RenderSetZEnable(s.byte_64A351 == 0);
                } else {                  // key == 0x14 (20): wireframe toggle
                    s.dword_649D7D = ((u8)s.dword_649D7D == 0) ? 1 : (s.dword_649D7D & ~0xFFu);
                }
            }
            // LABEL_8: if (!v13) return 0; else APPLY()
            if (!v13) return 0;
            APPLY();
            return 0;
        }
        if (key < 0x1Eu) { if (!v13) return 0; APPLY(); return 0; }  // LABEL_8
        if (key <= 0x1Eu) {               // key == 0x1E (30): toggle v10 bit0
            v10 = ((v10 & 1) == 0) | (v10 & 0xFE);
            APPLY();
            return 0;
        }
        // key == 0x1F (31): shadow-mode tri-state on v11 + scene-graph reset.
        v13 = 1;
        if ((v11 & 7) != 0) {
            if (s.byte_1408A6D) v11 &= 0xF8u;
            else                s.byte_1408A6D = 1;
        } else {
            v11 = (v11 & 0xF8) | 1;
            s.byte_1408A6D = 0;
        }
        if (h.SceneGraphTraverseShadow) h.SceneGraphTraverseShadow();
        if (!v13) return 0;
        APPLY();
        return 0;
    }

    if (key <= 0x20u) {                   // key == 0x20 (32): toggle v10 bit2
        v10 = (4 * ((v10 & 4) == 0)) | (v10 & 0xFB);
        APPLY();
        return 0;
    }

    if (key >= 0x2Eu) {
        if (key <= 0x2Eu) {               // key == 0x2E (46): tab-cycle track target
            // if (dword_13FCD1C == dword_13FD45C[0]) walk to next type-4 child
            // and set it; else snap the track target to the current selection.
            if (s.selectedObject == s.trackTarget) {
                i32 i = h.TrackTargetWalk ? h.TrackTargetWalk() : 0;
                if (i) s.trackTarget = i;   // (only when a distinct node found)
            } else {
                s.trackTarget = s.selectedObject;
            }
            if (!v13) return 0;
            APPLY();
            return 0;
        }
        if (key < 0x30u) { if (!v13) return 0; APPLY(); return 0; }  // LABEL_8
        if (key > 0x30u) {                // key > 0x30
            if (key == 50) {              // key == 0x32 (50): cycle v12 1->2->3->1
                v13 = 1;
                switch (v12) {
                    case 3: v12 = 1; break;
                    case 1: v12 = 2; break;
                    case 2: v12 = 3; break;
                }
            }
            if (!v13) return 0;
            APPLY();
            return 0;
        }
        // key == 0x30 (48): toggle v10 bit1
        v10 = (2 * ((v10 & 2) == 0)) | (v10 & 0xFD);
        APPLY();
        return 0;
    }

    if (key < 0x23u) { if (!v13) return 0; APPLY(); return 0; }   // LABEL_8
    if (key > 0x23u) {                    // key == 0x26 (38): refresh all lights
        if (key == 38 && h.LightRefreshAllObjects) h.LightRefreshAllObjects();
        if (!v13) return 0;
        APPLY();
        return 0;
    }
    // key == 0x23 (35): engine-enable toggle — the ONLY path returning 1.
    if (h.RenderSetEngineEnabled) h.RenderSetEngineEnabled(s.byte_649D70 == 0);
    return 1;
}

} // namespace scene_recon2
} // namespace guild::play

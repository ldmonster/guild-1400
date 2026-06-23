#pragma once
// ===========================================================================
// gilde.exe 0x50f0c0 — VIBE_Scene_RunMainFrameLoop
//   (__usercall, eax = (a1@edi, a2@esi); both register args are Hex-Rays
//    scratch artifacts — the function reads no live input through them.)
//
// The in-city per-frame SPINE: season latch, character (re)activation, slope
// rebuild gate, family/coat-of-arms scan, intro-zoom scan, market ambience
// start, game-clock proc registration, then the frame loop
//
//     while (VIBE_GameLogic_RunFrameLoop(eax=&self, edx=0x67FFF)) { body }
//
// and the scene-exit teardown (ambience stop, tooltip/banner clear, selection
// reset, cursor-coord rewrite, camera-transform restore, one final
// RunFrameLoop(eax=0, edx=0x67FFF) whose result is returned).
//
// FACTORING (host contract)
// -------------------------
// The original is a single blocking function. It is factored here EXACTLY
// along its own seams so a host (wave-2 sdl_session) can drive it once per
// frame:
//
//   SceneMainLoop_Begin     0x50f0c0..0x50f1ab   (everything before the loop)
//   SceneMainLoop_StepFrame 0x50f1ad..0x50f5f9   (loop CONDITION + one body;
//                                                 returns false — with no body
//                                                 run — when RunFrameLoop
//                                                 returns 0, exactly the
//                                                 original `while` head)
//   SceneMainLoop_End       0x50f5fe..0x50f694   (teardown; returns the final
//                                                 RunFrameLoop result == eax)
//   SceneMainLoop_Run       = Begin; while (StepFrame); return End;
//                             — byte-equivalent to the whole original.
//
// Loop-carried locals (ebp smoke phase, var_28 selection-flag latch, the
// var_2C/30/40/3C camera snapshot, v43[4] season) live in SceneMainLoopRun.
//
// COUPLING (rule 8 — no fakes)
// ----------------------------
// Every callee and every live-entity field access goes through
// SceneMainLoopHooks (virtual, inert defaults). Each slot carries its original
// address / field offset. Callees that already have reconstructions elsewhere
// in the tree are NOT re-translated — the binding map lives in
// progress/scene-main-frame-loop.md. Callees with NO reconstruction yet are
// named gaps (see the same doc): 0x50f028 Sound3d_UpdateListenerForScene,
// 0x50f698 TeleportPlayerToJail, 0x50de7c OpenGebaeudeBauenWindow,
// 0x51e88c EnterForeignShop, 0x51defc EnterAndDispatch,
// 0x4b60a0 Object_SpawnChimneySmoke, 0x421a24 Widget_SetTooltipText,
// 0x4bcdcc Hud_SetStatusBannerText.
//
// RELATION TO scene_recon2_orchestrator (ODR note)
// ------------------------------------------------
// guild::play::scene_recon2::Scene_RunMainFrameLoop is an earlier COARSE model
// of the same address (entity-coupled scans delegated wholesale, smoke machine
// and request-2 block simplified). This unit is the full 1:1 translation and
// the one wave-2 must wire. Distinct symbols/namespaces — no clash. The
// sibling Begin-callee 0x506df4 stays reconstructed in scene_recon2 and is
// reached via the sceneActivateAndRefreshCharacters() hook.
// ===========================================================================
#include "guild/common/types.h"

namespace guild::play {

// ---------------------------------------------------------------------------
// Constants recovered from the binary.
// ---------------------------------------------------------------------------
// RunFrameLoop feature mask used by this loop (mov edx, 67FFFh @0x50f1ad and
// @0x50f67a): bits 0..14 | bit17 (network command) | bit18 (day-cycle music).
constexpr u32 kSceneFrameLoopMask = 0x67FFF;  // 425983
// The frame-proc owner token: the original passes ITS OWN address in eax at
// the loop head (mov eax, offset VIBE_Scene_RunMainFrameLoop @0x50f1b2) and 0
// at the final teardown call (@0x50f684). RunFrameLoop publishes it into the
// frame-proc stack dword_11BBC30[2*dword_631610] (see 0x50f028's gate).
constexpr u32 kSceneMainLoopProc = 0x0050F0C0;
// Family/player record table stride and count: word_12CE910 scan walks byte
// offsets 0, 0x218, ... while < 0x64800 (cmp @0x50f3e0) -> 768 records.
constexpr i32 kFamilyRecordCount = 768;     // 0x64800 / 0x218
constexpr i32 kFamilyRecordStride = 0x218;  // 536 bytes

// Chimney-smoke hour tables, exact .data image at 0x6476F0..0x64771F
// (12 dwords; get_bytes verified). The gate reads
//   start = *(float*)(0x6476FC + 4*sext(i8 season))   -> kSmokeHourImage[3+idx]
//   end   = *(float*)(0x64770C + 4*sext(i8 season))   -> kSmokeHourImage[7+idx]
// season = day%4 (0x58339c) is in [-3,3]; the image covers that whole range,
// including the out-of-table reads a negative day would perform.
constexpr float kSmokeHourImage[12] = {
    0.0f, 0.0f, 0.0f,            // 0x6476F0..F8 (bytes below the start table)
    8.0f, 7.0f, 8.0f, 9.0f,      // 0x6476FC flt_6476FC[4]  smoke START hour
    20.0f, 21.0f, 20.0f, 19.0f,  // 0x64770C flt_64770C[4]  smoke END hour
    0.0f,                        // 0x64771C (byte above the end table)
};

// ---------------------------------------------------------------------------
// SceneMainLoopHooks — every callee / live-entity access, inert defaults.
// ---------------------------------------------------------------------------
struct SceneMainLoopHooks {
    virtual ~SceneMainLoopHooks() = default;

    // --- Begin-phase callees -----------------------------------------------
    // 0x506df4 VIBE_Scene_ActivateAndRefreshCharacters (call @0x50f0e4).
    //   Reconstructed: scene_recon2::Scene_ActivateAndRefreshCharacters.
    virtual void sceneActivateAndRefreshCharacters() {}
    // 0x5bbdb0 VIBE_Floor_ComputeSlopeFlags (eax = dword_64A028; @0x50f3b9).
    //   Reconstructed: render terrain_mesh (slope/elevation pass).
    virtual void floorComputeSlopeFlags(i32 floorCtx) { (void)floorCtx; }
    // 0x551404 VIBE_Panel_RunChooseWappen (slot/city in ecx at @0x50f188).
    //   Reconstructed: gui::Panel_RunChooseWappen(unsigned short).
    virtual void panelRunChooseWappen(u16 slot) { (void)slot; }
    // 0x582858 VIBE_Ambient_StartMarketLoop (@0x50f19a).
    //   Reconstructed: play/session_audio (market-loop start).
    virtual void ambientStartMarketLoop() {}
    // 0x44e3ac VIBE_TimeBase_SetProcInterval(eax = proc, edx = pause)
    //   (@0x50f1a6, proc = VIBE_Clock_ComputeGameTimeOfDay @0x527778, pause=0).
    virtual void timeBaseSetClockProc(i32 pause) { (void)pause; }

    // --- frame driver --------------------------------------------------------
    // 0x4c09a0 VIBE_GameLogic_RunFrameLoop(eax = ownerProc, edx = featureMask).
    //   Reconstructed: play::RunFrameLoop (gamelogic_recon).
    //   ownerProc == kSceneMainLoopProc inside the loop, 0 at teardown.
    //   Return 0 to leave the loop. Inert default returns 0 (loop never runs).
    virtual i32 runFrameLoop(u32 featureMask, u32 ownerProc) {
        (void)featureMask; (void)ownerProc; return 0;
    }

    // --- per-frame callees ---------------------------------------------------
    // 0x50f698 VIBE_Building_TeleportPlayerToJail (@0x50f1e5). NAMED GAP.
    virtual void buildingTeleportPlayerToJail() {}
    // 0x4b4c68 VIBE_Camera_Update (@0x50f1ea).
    //   Reconstructed: render::Camera_Update (camera_update_recon).
    virtual void cameraUpdate() {}
    // 0x50f028 VIBE_Sound3d_UpdateListenerForScene (@0x50f26b). NAMED GAP.
    virtual void sound3dUpdateListenerForScene() {}
    // 0x4b4e24 VIBE_Camera_ZoomIn (eax = object; @0x50f297/0x50f5bc/0x50f5f4).
    virtual void cameraZoomIn(i32 obj) { (void)obj; }
    // 0x4b5250 VIBE_Camera_ZoomReset (eax = object; @0x50f432/0x50f394).
    //   Reconstructed: render camera_recon2 Camera_ZoomReset.
    virtual void cameraZoomReset(i32 obj) { (void)obj; }
    // 0x50de7c VIBE_Building_OpenGebaeudeBauenWindow (@0x50f2b2). NAMED GAP.
    virtual void buildingOpenGebaeudeBauenWindow() {}
    // 0x588dec VIBE_Building_ComputeSelectionFlags(eax = active player,
    //   edx = building, ecx = 0, ebx = 0; @0x50f2ce). Returns ax (cwde'd by
    //   the caller — see StepFrame).
    virtual i16 buildingComputeSelectionFlags(u16 activePlayer, i32 building) {
        (void)activePlayer; (void)building; return 0;
    }
    // 0x587f80 VIBE_Building_IsProductionType (eax = record).
    //   Reconstructed: sim building_type (kind in {11,12,13,16,28}).
    virtual bool buildingIsProductionType(i32 b) { (void)b; return false; }
    // 0x587f50 VIBE_Building_IsStorageType (eax = record).
    //   Reconstructed: sim building_type (kind == 10).
    virtual bool buildingIsStorageType(i32 b) { (void)b; return false; }
    // 0x595e74 VIBE_Interaction_InvokeHandlerSlot60(eax = op, edx = building,
    //   ecx = param, ebx = 0; ops 27 @0x50f326 / 25 @0x50f59a).
    //   Reconstructed: sim interaction2::InvokeHandlerSlot60.
    virtual i32 interactionInvokeHandlerSlot60(i32 op, i32 building, i32 param) {
        (void)op; (void)building; (void)param; return 0;
    }
    // 0x51e88c VIBE_Building_EnterForeignShop (eax = record). NAMED GAP.
    virtual void buildingEnterForeignShop(i32 b) { (void)b; }
    // 0x51defc VIBE_Building_EnterAndDispatch (eax = record,
    //   edx = *(i32*)(rec+0x27) >> 16). NAMED GAP.
    virtual void buildingEnterAndDispatch(i32 b, i32 param) { (void)b; (void)param; }
    // 0x519918 VIBE_MarketStall_RouteContactByType (eax = contact @0x50f347).
    //   Reconstructed: world/market_stall::MarketStallRouteContact.
    virtual void marketStallRouteContact(i32 contact) { (void)contact; }
    // 0x4b60a0 VIBE_Object_SpawnChimneySmoke (eax = person). NAMED GAP.
    virtual void objectSpawnChimneySmoke(i32 person) { (void)person; }
    // 0x442174 VIBE_Script_FindByHandle (eax = handle) -> script ctx or 0.
    //   Reconstructed: sim script_import4 (FindByHandle).
    virtual i32 scriptFindByHandle(i32 handle) { (void)handle; return 0; }
    // 0x443f38 VIBE_Script_Finish (eax = script ctx).
    virtual void scriptFinish(i32 script) { (void)script; }

    // --- teardown callees ----------------------------------------------------
    // 0x5828bc VIBE_Ambient_StopMarketLoop (@0x50f60e).
    virtual void ambientStopMarketLoop() {}
    // 0x421a24 VIBE_Widget_SetTooltipText (eax = byte_621554, the shared
    //   empty-string buffer; @0x50f618). NAMED GAP (modeled hook elsewhere too).
    virtual void widgetSetTooltipText(const char* text) { (void)text; }
    // 0x4bcdcc VIBE_Hud_SetStatusBannerText (eax = byte_621554, edx = 0;
    //   @0x50f624). NAMED GAP.
    virtual void hudSetStatusBannerText(const char* text) { (void)text; }
    // 0x4b9444 VIBE_Selection_Reset (@0x50f629).
    //   Reconstructed: play input_recon_select Selection_Reset.
    virtual void selectionReset() {}
    // 0x40da48 VIBE_Coord_ConvertY — actually the cursor-coord write
    //   (camera_update_recon::Camera_CursorCoordWrite). Call @0x50f657 with
    //   eax = (i32)*(i32*)0x75BF48 >> 16  (== sext word @0x75BF4A)
    //   edx = (i32)*(i32*)0x75BF46 >> 16  (== sext word @0x75BF48)
    //   (ecx == 1 at the call site — the dword_62D0D4 store value).
    virtual void coordConvertY(i32 a, i32 b) { (void)a; (void)b; }

    // --- live-entity accessors ----------------------------------------------
    // Family table (records of stride 0x218, base addresses below; rec is the
    // record index 0..767):
    //   word  @0x12CE910 + rec*0x218 : slot id (0xFFFF == empty)
    //   byte  @0x12CE912 + rec*0x218 : dynasty/family type
    //   dword @0x12CE964 + rec*0x218 : owner id
    virtual u16 familySlot(i32 rec) { (void)rec; return 0xFFFF; }
    virtual u8  familyType(i32 rec) { (void)rec; return 0; }
    virtual i32 familyOwner(i32 rec) { (void)rec; return 0; }
    // Per-player fields in the same table, indexed by player slot:
    //   dword @0x12CEA80 + player*0x218 : the player's master person record
    //   byte  @0x12CEAC1 + player*0x218 : jail-pending flag
    virtual i32 playerMasterPerson(u16 player) { (void)player; return 0; }
    virtual u8  playerJailFlag(u16 player) { (void)player; return 0; }

    // 0x586c20 VIBE_Person_QueryBegin — vararg iterator open. Two literal call
    // shapes in this function (stack args verbatim):
    //   @0x50f40b  QueryBegin(1, 4, activePlayer)   [intro-zoom scan]
    //   @0x50f228 / 0x50f49d / 0x50f500  QueryBegin(1, 6)  [smoke scans]
    //   Reconstructed: sim entity PersonQueryBegin(filters, count).
    // Returns an opaque person record handle; 0 == no match.
    virtual i32 personQueryBegin_1_4(i32 key) { (void)key; return 0; }
    virtual i32 personQueryBegin_1_6() { return 0; }
    // 0x586a6c VIBE_Person_IterNext.
    virtual i32 personIterNext() { return 0; }
    // Person record field: dword @person+0x95 = chimney-smoke script handle.
    virtual i32  personScriptHandle(i32 person) { (void)person; return -1; }
    virtual void personSetScriptHandle(i32 person, i32 handle) {
        (void)person; (void)handle;
    }

    // Building record fields (offsets from the record pointer):
    //   byte  +0x5A : flags ("occupied/busy" bit0 gates the auto-enter)
    //   word  +0x29 : linked object id (written from the ext record's word 0)
    //   dword +0x27 : packed param; the >>16 high word feeds Enter/slot60
    //   dword +0x61 : zoom-state record (nonzero -> ZoomReset before enter)
    virtual u8   buildingFlagByte5A(i32 b) { (void)b; return 0; }
    virtual void buildingSetWord29(i32 b, u16 v) { (void)b; (void)v; }
    virtual i32  buildingDword27(i32 b) { (void)b; return 0; }
    virtual i32  buildingDword61(i32 b) { (void)b; return 0; }
    // Ext record (dword_11BC284): word @+0 copied into building word +0x29.
    virtual u16  extObjectWord0(i32 ext) { (void)ext; return 0; }
};

// ---------------------------------------------------------------------------
// SceneMainLoopState — the engine globals this function reads/writes, named by
// their gilde.exe addresses. Defaults are the static-image values.
// ---------------------------------------------------------------------------
struct SceneMainLoopState {
    // Game clock record qword_13CE852: dword @+0 = day, word @+4 = hour.
    i32 dword_13CE852 = 0;   // game day (GetSeasonFromDay input)
    u16 word_13CE856  = 0;   // game hour-of-day (the smoke-gate operand)

    i32 dword_62D4F0  = 0;   // set to -1 at entry (ecx == -1 from 0x50f0ce)
    i32 dword_634498  = 0;   // slope-flags dirty (cleared after the rebuild)
    i32 dword_64A028  = 0;   // floor context (eax arg of 0x5bbdb0)

    // Camera transform save/restore around the loop.
    i32 dword_62D0C4 = 0, dword_62D0C8 = 0, dword_62D0CC = 0, dword_62D0D0 = 0;
    i32 dword_62D0D4 = 0;    // set to 1 at teardown

    u8  byte_642008 = 0;     // "in city frame loop" flag
    u8  byte_63CC40 = 0;     // session-active flag (gamelogic_recon reads it)

    u16 word_63C740 = 0;     // session flags (bit2 wappen scan, bit0 intro zoom)
    u16 word_63CC5C = 0;     // active player slot
    i32 dword_63CC2C = 0;    // intro-zoom mode (must be ==1 for the scan)

    i32 dword_631610 = 0;    // frame-proc stack depth
    i32 dword_631618 = 0;    // latched copy (dword_631618 = dword_631610)

    // Camera auto-zoom gate.
    i32 dword_672224 = 0, dword_672234 = 0, dword_62D4E8 = 0;
    i32 dword_631730 = 0;    // currently selected building

    // Gebaeude-bauen window gate.
    i32 dword_75BF38 = -1;   // pending build record id (-1 == none)
    i32 dword_62D22C = 0, dword_63178C = 0;
    // Pending-build packed coord words (the teardown ConvertY operands):
    i16 word_75BF48 = 0;     // word @0x75BF48 (== *(i32*)0x75BF46 >> 16)
    i16 word_75BF4A = 0;     // word @0x75BF4A (== *(i32*)0x75BF48 >> 16)

    // Auto-enter request 1 (selection driven).
    i32 dword_11BC278 = 0;   // pending building-enter record
    i32 dword_67222C = 0, dword_67221C = 0;
    u16 word_62D310 = 0;     // panel mode (==11 alternative gate); zeroed at exit
    i32 dword_62D314 = 0;    // zeroed at exit

    // Auto-enter request 2 (scripted).
    i32 dword_11BC27C = 0;   // request pending flag
    i32 dword_11BC280 = 0;   // building record
    i32 dword_11BC284 = 0;   // ext record (0 -> plain zoom-in)

    i32 dword_6477A4 = 0;    // market-stall contact object
};

// ---------------------------------------------------------------------------
// SceneMainLoopRun — the loop-carried locals of 0x50f0c0.
// ---------------------------------------------------------------------------
struct SceneMainLoopRun {
    u8  season = 0;       // v43[4] / var_1C — GetSeasonFromDay latch
    i32 smokePhase = 0;   // ebp — chimney-smoke phase 0 -> 1 -> 2 -> 3
    i32 selFlags = 0;     // var_28 — ComputeSelectionFlags latch (cwde'd ax);
                          //          NOT reset between frames
    i32 savedC4 = 0, savedC8 = 0, savedCC = 0, savedD0 = 0; // var_2C/30/40/3C
};

// 0x58339c — VIBE_GameTime_GetSeasonFromDay(eax = &qword_13CE852) -> al.
// Body: return *(i32*)day % 4;  (x86 idiv truncation == C++ %).
i8 GameTime_GetSeasonFromDay(i32 day);

// The chimney-smoke window kernel (0x50f1f7..0x50f21e and twins):
//   idx  = sext(i8 season)                  (sar 24 of the season byte)
//   hour = (double)(u16)word_13CE856        (fild of the zero-extended word)
//   in   = hour >= start[idx] && hour < end[idx]
// Reads use the exact .data image (kSmokeHourImage), valid for idx in [-3,3]
// — the full range of day%4. Exposed for golden tests.
bool Scene_SmokeWindow(i8 season, u16 hour);

// The factored 0x50f0c0 (see header comment for the address ranges).
void SceneMainLoop_Begin(SceneMainLoopState& s, SceneMainLoopHooks& h,
                         SceneMainLoopRun& run);
bool SceneMainLoop_StepFrame(SceneMainLoopState& s, SceneMainLoopHooks& h,
                             SceneMainLoopRun& run);
i32  SceneMainLoop_End(SceneMainLoopState& s, SceneMainLoopHooks& h,
                       SceneMainLoopRun& run);
// Whole-function form: Begin; while (StepFrame) {} return End;
i32  SceneMainLoop_Run(SceneMainLoopState& s, SceneMainLoopHooks& h);

} // namespace guild::play

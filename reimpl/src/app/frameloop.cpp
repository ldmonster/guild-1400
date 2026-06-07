// gilde.exe 0x4c09a0 — VIBE_GameLogic_RunFrameLoop (feature-mask gated).
// Namespace guild::app.
//
// The original is a 3684-byte per-frame tick whose major work items are each
// gated by an "& bit" test on the 32-bit feature mask passed in edx (local
// v54, also published to dword_11BC2D0). This translation reproduces the
// mask-gated dispatch: the ORDER of the gated steps and the EXACT bit tests
// (including the two-bit suppress combinations) are preserved 1:1. The many
// global-state predicates the original also checks (e.g. dword_631638 "skip
// rendering", word_63CC5C "hover slot") are engine run-state, not part of the
// feature-mask contract this module owns; here those steps run whenever their
// mask bit is set so the bit semantics can be verified in isolation. A live
// host wires the same hooks to the real subsystems, which apply the run-state
// guards internally.
#include "app/gamelogic.h"

namespace guild::app {

// Helper: bit test matching the original `(v54 & B) != 0`.
static inline bool has(std::uint32_t m, std::uint32_t b) { return (m & b) != 0; }

int GameApp::RunFrameLoop(std::uint32_t featureMask) {
    const std::uint32_t v54 = featureMask;
    lastFeatureMask_ = v54;            // dword_11BC2D0 = a1 (publish the mask)

    const bool headless = has(v54, mask::kHeadlessSuppress); // 0x10000 suppress

    // --- Always-run pump (top of the loop): PumpMessages + Input_Latch. ----
    sub_.inputLatchAndPump();
    // WM_QUIT / window closed at the top of the frame: stop here (don't render a
    // quit frame), exactly like the original's message-loop quit check.
    if (sub_.quitRequested())
        return 0;

    // --- Mouse / widget input -------------------------------------------------
    // if ((v54 & 4) != 0) VIBE_Widget_DispatchMouseClick();
    if (has(v54, mask::kWidgetMouse))
        sub_.widgetDispatchMouseClick();

    // if (!dragPending && (v54 & 0x100) && (v54 & 0x100000)==0) Hud_HandleMouseClick();
    if (has(v54, mask::kHudMouse) && !has(v54, mask::kInputSuppress))
        sub_.hudHandleMouseClick();

    // --- Input/command-request poll block ------------------------------------
    // if ((v54 & 1) != 0 && !dword_63CC30) { ... EventPanel/He/CmdRequest ... }
    if (has(v54, mask::kInputCommandPoll))
        sub_.inputCommandPoll();

    // --- Network command pump ------------------------------------------------
    // if ((v54 & 0x20000) != 0 && ...) { FlushSendQueue; ReceiveAndQueue; ExecCommands; }
    if (has(v54, mask::kNetworkCommand))
        sub_.commandNetworkPump();

    // --- Script stepping -----------------------------------------------------
    // if ((v54 & 0x200) != 0) VIBE_Script_StepAllActive(v10);
    if (has(v54, mask::kScripts))
        sub_.scriptStepAllActive();

    // --- Game-object interactions --------------------------------------------
    // if (!skipFrame && (v54 & 0x80) != 0) VIBE_GameObject_DispatchInteractions();
    if (has(v54, mask::kGameObjects))
        sub_.gameObjectDispatchInteractions();

    // --- World render --------------------------------------------------------
    // if (!skipFrame && (v54 & 0x40) != 0 && dword_631E74) {
    //   if ((v54 & 0x10000)==0 && (v54 & 0x40000)!=0) DayCycle_UpdateBrightness();
    //   ... cull ...; Character_FlushPendingMesh(); Render_RenderMainViewFrame(); }
    if (has(v54, mask::kRenderWorld)) {
        if (!headless && has(v54, mask::kDayCycleMusic))
            sub_.dayCycleAndOutdoorMusic();
        sub_.renderMainViewFrame();
    }

    // --- Character ownership collect (skipped in combat/select mode) ---------
    // if ((v54 & 0x8000) == 0 && byte_63CC40) VIBE_Character_CollectByOwner(...);
    if (!has(v54, mask::kCombatSelect))
        sub_.characterCollectByOwner();

    // --- Weather sky ---------------------------------------------------------
    // if ((v54 & 0x4000) != 0 && ...) VIBE_Weather_UpdateSky();
    if (has(v54, mask::kWeatherSky))
        sub_.weatherUpdateSky();

    // --- Outdoor music (music enabled & day-cycle) ---------------------------
    // if (dword_63C8F8 && (v54 & 0x40000) != 0) Music_UpdateOutdoorTrackPlayback();
    // (folded into dayCycleAndOutdoorMusic; only emit standalone when render
    //  world did not already run the day-cycle block above.)
    if (has(v54, mask::kDayCycleMusic) && !has(v54, mask::kRenderWorld) && !headless)
        sub_.dayCycleAndOutdoorMusic();

    // --- Object update / HUD selection ---------------------------------------
    // if (!dragPending && (v54 & 0x100000)==0) {
    //   ... Object_UpdateGateContact ...
    //   if ((v54 & 0x10) != 0 && (v54 & 0x100000)==0) Object_ResolveQuickJumpContact();
    //   if ((v54 & 0x20) != 0) { Hud_UpdateSelectionAndTargets; Hud_DrawSelectedUnitInfo; }
    // }
    if (!has(v54, mask::kInputSuppress)) {
        if (has(v54, mask::kQuickJump))
            sub_.quickJumpContact();
        if (has(v54, mask::kHudSelection))
            sub_.hudSelectionAndTargets();
    }

    // --- Tooltips ------------------------------------------------------------
    // if ((v54 & 0x400) != 0) VIBE_Tooltip_DispatchByType();
    if (has(v54, mask::kTooltips))
        sub_.tooltipDispatch();

    // --- HUD draw + present block (gated by 0x80) ----------------------------
    // if (!skipFrame && (v54 & 0x80) != 0) {
    //   ... Office timer / damage labels / scroll arrows ...
    //   if ((v54 & 0x80000) != 0) Camera_UpdateCombatScroll();
    //   ... StatusBanner / DragCursor ...
    //   Render_PresentFrame();
    // }
    if (has(v54, mask::kGameObjects)) {
        if (has(v54, mask::kHudLabels))
            sub_.hudLabelsAndCaption();
        if (has(v54, mask::kCombatScroll))
            sub_.cameraCombatScroll();
        sub_.presentFrame();
    }

    // --- Options menu / chat / hotkeys / stat panels (gated by 0x2000) -------
    // Multiple sites test (v54 & 0x2000): options-key, chat console, hotkey
    // handling and the per-panel (inventory/stats/family-tree/...) dispatch.
    if (has(v54, mask::kOptionsAndPanels))
        sub_.optionsChatHotkeyPanels();

    // --- Autosave & network wait loop (suppressed by 0x200000) ---------------
    // if (byte_63CC74 && (dword_11BC2D0 & 0x200000) == 0) { ...autosave... }
    // if (dword_63127C  && (dword_11BC2D0 & 0x200000) == 0) Net_RunWaitLoopWithStatus();
    if (!has(v54, mask::kAutosaveSuppress))
        sub_.autosaveAndNetWait();

    // Original returns v10 (1 = a logic step ran). Headless re-entrant calls
    // still pump/return; here a non-headless frame returns 1, headless 0.
    return headless ? 0 : 1;
}

} // namespace guild::app

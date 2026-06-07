#pragma once
// guild::gui — the FULL function bodies of the three options sub-screen runners.
//
// gilde.exe 0x56c21c — VIBE_Menu_RunOptionsGfx   (form "menu\\options_gfx",  title 6247)
// gilde.exe 0x56c808 — VIBE_Menu_RunOptionsSfx   (form "menu\\options_sfx",  title 6248)
// gilde.exe 0x56cc44 — VIBE_Menu_RunOptionsGame  (form "menu\\options_game", title 6246)
// gilde.exe 0x56af54 — VIBE_Config_WriteGfxSettings (WritePrivateProfileStringA into the
//                       [Gfx]/[Sound]/[Game] sections).
//
// gui/options_screens.{h,cpp} already recovers the LAYOUT TABLES (kGfx/Sfx/GameWidgets[]),
// the gamma inversion, the per-screen OK save-back maps and the commit dispatch (Gfx/Sfx/
// Game_Commit + OptionsScreenSink).  config/* owns the GfxSettings/SoundSettings/
// GameSettings structs and ReadGfxAndSoundSettings.  THIS module reconstructs the missing
// piece: the actual RunOptionsXxx FUNCTION BODY exactly like gui/main_menu_run.cpp does for
// the main menu —
//
//   * the preamble + form build: GameTick_Finalize(form) -> Camera_ComputeWorldTarget ->
//     Form_PositionChildWindows -> Window_PositionAtCoord_Thunk(form,2) -> SelectWindow(0)
//     + Hud_SyncWindowColors + DragCursor_SetSprite -> SelectWindow(2)+Text_RenderRichString
//     (title) -> SelectWindow(1) + the per-child GetChildObjectId / SetValueOrText /
//     SetScrollLimit (+ AppendWideLines for dropdowns) build loop -> SelectWindow(3) +
//     Hud_BuildButtonRow (the OK id == row[0], Cancel id == row[1]).
//
//   * the do/while VIBE_GameLogic_RunFrameLoop loop with the click-edge dispatch on
//     dword_62D22C against the OK / Cancel ids (and, for Sfx, the live volume-preview on a
//     drag of a volume slider), with the EXACT dword_631614 (close) / accept mutations and
//     the dword_672230 || byte_67225C==1 (window-close / ESC) edge.
//
//   * the OK-path save-back + persist + apply (delegated to options_screens' Xxx_Commit,
//     which writes the setting struct then calls WriteGfxSettings + the per-screen apply),
//     then Form_Destroy.
//
// REUSE (NOT redefined here): config::{GfxSettings,SoundSettings,GameSettings},
// gui::{kGfx/Sfx/GameWidgets, Gfx_GammaSeed/Save, Xxx_SaveBack, Xxx_Commit,
// OptionsScreenSink, OptionsResult, kOptions*Form/Title, kScrollLimit*}.
//
// HOST BOUNDARIES — RunFrameLoop tick, the click-edge source (dword_62D22C/dword_672230/
// dword_672220/byte_67225C/dword_75BF38), the form/widget build leaves, the live audio
// preview and the resolution/difficulty caps — go through an installable OptionsRunHooks
// block with INERT DEFAULTS in options_run.cpp, so the functions link in the unified build
// and are fully testable headless.

#include "gui/options_screens.h"
#include "config/ini.h"

#include <cstdint>

namespace guild::gui {

// ===========================================================================
// Reconstructed per-run state — the BSS the runners read for seeds / caps.
// (These mirror the byte_1233xxx / byte_62D59x / byte_63CC40 / byte_63D724 globals the
// originals seed the widgets from; the module keeps its own faithful copy in/out so a run
// is reproducible headless.  Bit/range values match the originals exactly.)
// ===========================================================================
struct OptionsRunState {
    // ---- Gfx (0x56c21c) ----
    // byte_62D59A / byte_62D59B device-caps -> the resolution dropdown's range v21 (1 or 2)
    // and the seeded value (0x56c369: if 62D59A v21=1; if 62D59B v21=2).
    bool gfxResCap1 = false;   // byte_62D59A
    bool gfxResCap2 = false;   // byte_62D59B
    // byte_63CC40: in-game (groundplan active) -> the resolution dropdown is hidden
    // (0x56c667 SetVisibleRecursive(child0,0)) and difficulty change rebuilds the plan.
    bool inGame = false;       // byte_63CC40

    // ---- Game (0x56cc44) difficulty pre-change tracking ----
    // The runner re-reads child 9 (difficulty) and compares to the prior byte_12335B8 to
    // gate the groundplan rebuild.  We carry the prior value in this struct.

    // The live config the screen seeds FROM and (on OK) writes BACK to.  These are the
    // same structs config::ReadGfxAndSoundSettings fills; reused, not redefined.
    config::GfxSettings   gfx;
    config::SoundSettings snd;
    config::GameSettings  game;
};

// ===========================================================================
// Host-boundary hooks (installable; INERT DEFAULTS in options_run.cpp).
// ===========================================================================
struct OptionsRunHooks {
    virtual ~OptionsRunHooks() = default;

    // ---- form / window build leaves ----
    // VIBE_GameTick_Finalize(0,0,name) @0x41beb8 — load the form; returns the form handle.
    virtual int  FormLoad(const char* name) { (void)name; return 1; }
    // VIBE_Camera_ComputeWorldTarget @0x4c0864 / VIBE_Form_PositionChildWindows @0x41d990 /
    // VIBE_Window_PositionAtCoord_Thunk(form,2) @0x41d964 / VIBE_Form_SelectWindow @0x41e4cc.
    virtual void CameraComputeWorldTarget() {}
    virtual void FormPositionChildWindows(int form) { (void)form; }
    virtual void WindowPositionAtCoord(int form, int coord) { (void)form; (void)coord; }
    virtual void FormSelectWindow(int form, int window) { (void)form; (void)window; }
    // VIBE_Hud_SyncWindowColors @0x4bd5dc / VIBE_DragCursor_SetSprite @0x41fcbc.
    virtual void HudSyncWindowColors() {}
    virtual void DragCursorSetSprite() {}
    // VIBE_Text_RenderRichString(title) @0x59d6e8.
    virtual void TextRenderRichString(int title) { (void)title; }
    // VIBE_Form_GetChildObjectId(form, 1, childIndex) @0x41dea8 — returns the widget id.
    // The inert default returns the child index (a stable per-child id).
    virtual int  FormGetChildObjectId(int form, int window, int childIndex) {
        (void)form; (void)window; return childIndex;
    }
    // VIBE_Object_SetValueOrText(widget, min, max, value, _) @0x41dfec — seed a slider/list.
    virtual void ObjectSetValueOrText(int widget, int minV, int maxV, int value) {
        (void)widget; (void)minV; (void)maxV; (void)value;
    }
    // VIBE_Widget_SetScrollLimit(widget, code) @0x41207c.
    virtual void WidgetSetScrollLimit(int widget, int code) { (void)widget; (void)code; }
    // VIBE_Gui_FreeString_Thunk(widget, label) @0x411eac.
    virtual void GuiFreeString(int widget, int labelKey) { (void)widget; (void)labelKey; }
    // VIBE_Text_AppendWideLines(widget, lineCount, textKey) @0x411ee4 (dropdown options).
    virtual void TextAppendWideLines(int widget, int lineCount, int textKey) {
        (void)widget; (void)lineCount; (void)textKey;
    }
    // VIBE_Object_SetVisibleRecursive(widget, visible) @0x41dd98.
    virtual void ObjectSetVisibleRecursive(int widget, int visible) {
        (void)widget; (void)visible;
    }
    // VIBE_Hud_BuildButtonRow(...) @0x4bcdfc — builds the OK/Cancel row; the two button ids
    // land in row[0] (OK) and row[1] (Cancel).  The inert default returns two fixed ids.
    virtual void HudBuildButtonRow(int row[2]) { row[0] = 1000; row[1] = 1001; }
    // VIBE_Form_Destroy(form) @0x41da04.
    virtual int  FormDestroy(int form) { (void)form; return 0; }

    // ---- frame loop + click-edge source ----
    // VIBE_GameLogic_RunFrameLoop @0x4c09a0 — one tick; returns nonzero to keep running.
    virtual int  RunFrameLoop(int frame) { (void)frame; return 0; }
    // dword_75BF38 != -1  (a click EDGE happened this frame); when false no dispatch runs.
    virtual bool ClickEdge(int frame) { (void)frame; return false; }
    // dword_62D22C — the hovered/activated widget id this frame (-1 == none).
    virtual int  HoverId(int frame) { (void)frame; return -1; }
    // dword_672230  (window-close request) || byte_67225C == 1 (ESC) -> arm close.
    virtual bool WindowCloseOrEsc(int frame) { (void)frame; return false; }

    // ---- Sfx live preview (0x56cae0) ----
    // dword_672220 — a left-button DRAG is in progress (live volume preview only fires
    // while dragging one of the volume sliders).
    virtual bool DragActive(int frame) { (void)frame; return false; }
    // VIBE_Audio_SetMasterVolume / ApplyMasterVolume — the live master/sfx preview.
    virtual void AudioPreviewVolume(int master, int sfx) { (void)master; (void)sfx; }
    // VIBE_Audio_StartVoiceSample @ (the sfx-slider drag test sample).
    virtual void AudioStartVoiceSample() {}

    // ---- live widget value read-back (VIBE_Object_GetDataPtr @0x41db9c) ----
    // On OK the runner reads each built widget's live value.  The inert default returns the
    // value last seeded into the widget (so a no-interaction OK round-trips the settings).
    virtual int  ObjectGetDataPtr(int widget) { (void)widget; return 0; }
};

// Install hooks (null restores the inert defaults).  Returns the previous hooks.
OptionsRunHooks* Menu_SetOptionsRunHooks(OptionsRunHooks* hooks);

// ===========================================================================
// Recorded build/dispatch trace (testable).
// ===========================================================================
struct OptionsRunBuiltWidget {
    int childIndex; // VIBE_Form_GetChildObjectId(form,1,childIndex)
    int widgetId;   // the returned id
    int minV;       // SetValueOrText min
    int maxV;       // SetValueOrText max (range)
    int seedValue;  // SetValueOrText value (the seed)
    int scrollLim;  // SetScrollLimit code
    int lines;      // AppendWideLines count (0 = no dropdown text)
    bool hidden;    // SetVisibleRecursive(widget,0) was called
};

struct OptionsRunRecord {
    const char* formName = nullptr;
    int   title = 0;
    int   form = -1;

    OptionsRunBuiltWidget widgets[16];
    int   widgetCount = 0;
    int   okId = -1;       // Hud_BuildButtonRow row[0]
    int   cancelId = -1;   // Hud_BuildButtonRow row[1]
    bool  accepted = false;
    int   frames = 0;

    // Ordered call trace (tags) for the e2e order assertion.
    static constexpr int kMaxTrace = 256;
    const char* trace[kMaxTrace];
    int   traceCount = 0;
};

// ===========================================================================
// The three runners.  `st` carries the seeds in (config + caps) and receives the OK
// write-back out; `rec` (optional) records the build + dispatch for tests; `maxFrames`
// bounds the loop for headless testing (the original spins until RunFrameLoop returns 0).
// Returns the OptionsResult (accepted + resChanged) from the matching Xxx_Commit.
// ===========================================================================
OptionsResult Menu_RunOptionsGfx (OptionsRunState& st, OptionsRunRecord* rec, int maxFrames);
OptionsResult Menu_RunOptionsSfx (OptionsRunState& st, OptionsRunRecord* rec, int maxFrames);
OptionsResult Menu_RunOptionsGame(OptionsRunState& st, OptionsRunRecord* rec, int maxFrames);

} // namespace guild::gui

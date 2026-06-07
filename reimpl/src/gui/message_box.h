#pragma once
// guild::gui — the modal message-box / confirm-box family (the run-loop side of the
// dialog model, complementing dialog.{h,cpp} which owns the decomposed leaves:
// Dialog_FormForFlags / Dialog_BuildButtonGroup / Dialog_ResolveResult).
//
// This module reconstructs the five byte-near-identical modal entry points that the
// game uses for every "press OK / pick a button" pop-up:
//
//   gilde.exe 0x4ad6f0 — VIBE_Dialog_ShowMessageBox          (the primary one; 94 xrefs)
//   gilde.exe 0x4acbd0 — VIBE_Dialog_ShowMessageBoxBig       (2nd reentrancy guard)
//   gilde.exe 0x569a30 — VIBE_Dialog_RunMessageBox           (3rd guard; ignores result)
//   gilde.exe 0x4ad9dc — VIBE_Dialog_ShowMessageBoxGreen     (two-text "MessageBoxGreen")
//   gilde.exe 0x4adc60 — VIBE_Dialog_ShowMessageBoxModeless  (frame-loop ttl = 1)
//
// All five share the same shape (see dialog.cpp for the decomposed helpers):
//   1. bail out if THIS box's reentrancy guard is already set (returns 0);
//   2. clamp the render palette (dword_11BC2D0 & 0xC7), overridden to 464838 by flag 0x08;
//   3. pick a .form by the kind flags and load it via GameTick_Finalize -> form id;
//      (the 0x100 "NONE_PERGA" path additionally selects window 1 before rendering);
//   4. render the message rich-text into the form, center its windows;
//   5. set the reentrancy guard, build a radio group from the form-window's non-'@'
//      children (Dialog_BuildButtonGroup), select button 0;
//   6. flag 0x80 (a1<0): add a progress slider; flag 0x40: keep the box raised each frame;
//   7. run the modal frame loop (GameLogic_RunFrameLoop) until a click/abort/timeout sets
//      dword_631614; per frame: OK (1210 on this window) -> result = group selection + 1,
//      Cancel (1155 on this window) / right-click (dword_672230) / Esc (byte_67225C) -> 0,
//      flag 0x04 auto-close once the ttl tick elapses;
//   8. clear the guard, free the radio group surface, destroy the form; return the result.
//
// The .form loader, the rich-text renderer, the progress slider, and the GameLogic frame
// loop belong to the io/text/sim clusters and are routed through a mockable MessageBoxHost
// so the data-model orchestration is testable headless.  The button-group build reuses the
// already-translated Dialog_BuildButtonGroup (dialog.cpp); the form/window/widget records,
// the current-window globals and the radio-group selection table are REUSED from
// window.h / form.h / radiogroup.h.

#include "gui/types.h"
#include "gui/dialog.h"

namespace guild::gui {

// ---------------------------------------------------------------------------
// Per-box reentrancy guard bytes (each entry point has its own — the originals
// guarantee only one of *that* kind is open at a time, but a Big box may nest
// over a normal one).  Owned here; no other module defines them.
//   byte_631DA8  ShowMessageBox
//   byte_631600  ShowMessageBoxBig
//   byte_63D730  RunMessageBox
//   byte_631DA9  ShowMessageBoxGreen
//   byte_631DAA  ShowMessageBoxModeless
// ---------------------------------------------------------------------------
extern u8 g_msgGuardNormal;   // byte_631DA8
extern u8 g_msgGuardBig;      // byte_631600
extern u8 g_msgGuardRun;      // byte_63D730
extern u8 g_msgGuardGreen;    // byte_631DA9
extern u8 g_msgGuardModeless; // byte_631DAA

// ---------------------------------------------------------------------------
// Palette globals (read by every variant).
//   dword_11BC2D0  the live render palette; the box uses (palette & 0xC7),
//                  overridden to kMsgPaletteOverride when flag 0x08 is set.
// Owned here; reused via extern by no one else (other modules read 11BC2D0 from
// their own owning module — we declare it extern and reuse if present).
// ---------------------------------------------------------------------------
inline constexpr i32 kMsgPaletteMask     = 0xC7;   // palette &= 0xC7
inline constexpr i32 kMsgPaletteOverride = 464838; // v20 = 464838 when flag 0x08 set

// The live render-palette snapshot the box reads (dword_11BC2D0).  Owned by the render
// cluster in the original; the GUI keeps this single mirror (no translated module defines
// dword_11BC2D0 yet — they only extern-read it).  Tests set it to exercise the clamp.
extern i32 g_msgPalette; // dword_11BC2D0 snapshot

// The frame-loop ttl base added to the start tick (dword_62EB38 + 500); the modeless
// variant passes a fixed ttl of 1 instead.
inline constexpr unsigned kMsgTtlSpan = 500;

// Slider geometry (VIBE_Widget_AddSliderToWindow args; ShowMessageBox/Big/Green use
// winH-24, Modeless uses winH-32).  Recovered byte-exact from the decompiles.
inline constexpr int kMsgSliderInsetY        = 24; // normal/big/green
inline constexpr int kMsgSliderInsetYModeless = 32; // modeless
inline constexpr int kMsgSliderX             = 28;
inline constexpr int kMsgSliderWidthInset    = 64; // winW - 64
inline constexpr int kMsgSliderRange         = 500;
inline constexpr int kMsgSliderGfxA          = 114;
inline constexpr int kMsgSliderGfxB          = 66;

// ---------------------------------------------------------------------------
// MessageBoxHost — the OS / cross-cluster boundary the modal loop crosses.
// In the game these are GameTick_Finalize (form load), Text_RenderRichString,
// Form_SelectWindow / Form_CenterChildWindows / Form_RaiseWindows,
// RadioGroup_Create / AddButton / Selection_Update, Widget_AddSliderToWindow,
// Object_SetValueOrText, GameLogic_RunFrameLoop, InitStateReader, Form_Destroy.
// A test supplies a mock that scripts the per-frame click/abort inputs.
// ---------------------------------------------------------------------------
struct MessageBoxHost {
    virtual ~MessageBoxHost() = default;

    // Step 3: load the named .form, return its form id (>=0) or -1.  `selectWindow1`
    // is set for the 0x100 NONE_PERGA path (Form_SelectWindow(form, 1) before render).
    virtual int  LoadForm(const char* /*formName*/, bool /*selectWindow1*/) { return 0; }
    // Step 4: render the primary message text into the current window of `formId`.
    virtual void RenderText(int /*formId*/, int /*textArg*/) {}
    // Green variant only: render the second (body) text into window slot 2.
    virtual void RenderBodyText(int /*formId*/, int /*textArg*/) {}
    // Green variant only: VIBE_Hud_SyncWindowColors(currentWindow) recolor pass.
    virtual void SyncWindowColors(int /*formId*/) {}
    // Step 4: center the form's windows on screen.
    virtual void CenterWindows(int /*formId*/) {}
    // Step 6: keep the box raised this frame (flag 0x40).
    virtual void RaiseWindows(int /*formId*/) {}
    // Step 6: add the progress slider; return its widget index or -1.
    virtual int  AddSlider(int /*formId*/, int /*insetY*/) { return -1; }
    // Step 7 (per frame): update the slider value (elapsed of `range`).
    virtual void SetSliderValue(int /*sliderIdx*/, int /*elapsed*/, int /*range*/) {}
    // Step 8: destroy the form and free the radio-group surface.
    virtual void DestroyForm(int /*formId*/) {}

    // The modal pump.  Called once per loop iteration; returns false to end the loop
    // (the original's `while (GameLogic_RunFrameLoop(palette, ttl, form))`).  The host
    // fills `in` with this frame's click/abort state; the model then decides the result
    // and may request a break by returning false here OR by the model setting
    // closeRequested (mirrors dword_631614).
    struct FrameInput {
        int  clickedId     = 0;  // dword_75BF38 (1210 OK / 1155 Cancel / other)
        int  clickedWindow = 0;  // dword_75BF08 (window slot the click landed on)
        bool rightClick    = false; // dword_672230 != 0
        bool escKey        = false; // byte_67225C == 1
        unsigned tick      = 0;  // dword_62EB38 this frame (for ttl / slider math)
    };
    virtual bool RunFrame(int /*formId*/, i32 /*palette*/, unsigned /*ttl*/,
                          FrameInput& /*in*/) { return false; }
};

// Result codes mirror the integer the originals return.
//   >0  : selected button index + 1  (OK pressed)
//    0  : cancelled / right-click / timeout / guard-blocked
struct MessageBoxResult {
    int  value = 0;        // the returned int (button index + 1, or 0)
    bool guardBlocked = false; // true when the reentrancy guard rejected the call
};

// ---------------------------------------------------------------------------
// The five entry points.  `kindFlags` is the original `a1@<dx>` (the high sign bit,
// i.e. a value < 0 as __int16, requests the progress slider — pass kMsgFlagSlider).
// `buttonGroupArg` is the `a2` radio-group-create argument (passed through).
// `textArg` is the rich-text id rendered into the box.
// ---------------------------------------------------------------------------

// gilde.exe 0x4ad6f0 — VIBE_Dialog_ShowMessageBox  (a1@<dx>=flags, a2@<sil>=group)
int MessageBox_Show(MessageBoxHost& host, i16 kindFlags, char buttonGroupArg, int textArg);

// gilde.exe 0x4acbd0 — VIBE_Dialog_ShowMessageBoxBig  (own guard byte_631600)
int MessageBox_ShowBig(MessageBoxHost& host, i16 kindFlags, char buttonGroupArg, int textArg);

// gilde.exe 0x569a30 — VIBE_Dialog_RunMessageBox  (own guard byte_63D730; always returns 0,
// because the click branches never write the result local — kept faithful: result stays 0).
int MessageBox_Run(MessageBoxHost& host, i16 kindFlags, char buttonGroupArg, int textArg);

// gilde.exe 0x4adc60 — VIBE_Dialog_ShowMessageBoxModeless  (a1@<dl>=flags, ttl=1, returns 0)
int MessageBox_ShowModeless(MessageBoxHost& host, char kindFlags, char buttonGroupArg,
                            int textArg);

// gilde.exe 0x4ad9dc — VIBE_Dialog_ShowMessageBoxGreen
//   (a1@<edx>=bodyText, a2@<bl>=flags, a3@<dil>=group; fixed "misc\MessageBoxGreen" form).
// `headerText` is the optional header rendered into the default window (0 = skip);
// `bodyText` is rendered into window slot 2.
int MessageBox_ShowGreen(MessageBoxHost& host, int bodyText, char kindFlags,
                         char buttonGroupArg, int headerText);

} // namespace guild::gui

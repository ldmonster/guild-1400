#pragma once
// guild::gui — gui_widgetn: a P6 (Wave 29) coverage slice closing genuinely-DEFERRED
// deterministic leaves of the VIBE_Widget_* / VIBE_StatusText_* / VIBE_Scroll_* /
// VIBE_InfoPanel_* GUI families, translated 1:1 from gilde.exe (32-bit x86,
// imagebase 0x400000). Every function here was verified absent from src/ + include/
// (address AND VIBE_ name) before reconstruction — see the per-function provenance.
//
//   0x412480  VIBE_Widget_RefreshText        re-center & re-layout a label widget's
//                                            text bounds (read +88 flag, +44 parent).
//   0x412530  VIBE_Widget_SetTextColor       store a 16-bit text color at widget +20,
//                                            then refresh the text layout.
//   0x4bcc30  VIBE_StatusText_ClearTable     zero the active-marker column of the
//                                            50-dword-stride 32-entry status-text table.
//   0x537318  VIBE_Scroll_RunAnimationLoop   open the parchment scroll, pump the frame
//                                            loop (latching a quit), then close it.
//   0x4b8438  VIBE_InfoPanel_Destroy         tear down the 4 child widgets + the form
//                                            of the floating info panel, reset its 11
//                                            tracking handles to -1.
//
// Window/text-runtime + render leaves the originals call are routed through an
// installable GuiWidgetNHooks struct with inert defaults in gui_widgetn.cpp (the
// house pattern). Already-reconstructed siblings (Widget_RefreshText itself,
// Form_Destroy, Widget_DestroyByType) are reused directly via their real headers.

#include "gui/types.h"

#include <cstdint>

namespace guild::gui {

// ===========================================================================
// Module-owned globals (original BSS bases in the comments).
// ===========================================================================

// Status-text active-marker column — gilde.exe dword_11B5158. The status-text table
// is a 50-dword-stride / 32-entry structure (see hud.h StatusText_Register at
// dword_11B5220, which is +50 dwords from this column). ClearTable zeros only this
// marker column (it does NOT touch the per-object +536 field that ResetEntries does).
inline constexpr int kStatusTextStrideN = 50;  // dwords per entry
inline constexpr int kStatusTextCountN  = 32;  // 1600 / 50
extern std::int32_t g_statusTextMarker[kStatusTextCountN]; // dword_11B5158 column view

// Floating info-panel tracking handles — gilde.exe dword_631768 .. dword_63179C.
//   +0  dword_631768  form id          (-1 == not open)
//   ... dword_63176C, dword_631770, dword_631784..dword_63179C  misc tracking ids
//   dword_631774[4]   the 4 child widget indices
inline constexpr int kInfoPanelChildren = 4;
extern int g_infoPanelForm;              // dword_631768
extern int g_infoPanelChild[kInfoPanelChildren]; // dword_631774
// The ten secondary tracking handles reset alongside the form (all -> -1 on destroy).
extern int g_infoPanelHandle[10];        // 63176C,631770,63179C,631798,631794,
                                         // 63178C,631790,631788,631784  (+1 spare)

// Screen-size dword — gilde.exe dword_69FFBC (hiword = screen height; the
// RefreshText fallback center origin). Settable mirror (real value from display setup).
extern std::uint32_t g_screenSizeDword;  // dword_69FFBC

// dword_631614 — screen-transition / quit latch (modeled locally; observable in tests).
extern int g_scrollQuitLatch;

// ===========================================================================
// Hooks: cross-module / sibling leaves with inert defaults. ABI mirrors the raw
// decompile so each call site translates 1:1.
// ===========================================================================
struct GuiWidgetNHooks {
    // --- Widget_RefreshText leaf (real sibling, hookable) ------------------
    void (*widgetLayoutBounds)(int x, int y, int widgetIdx);    // VIBE_Widget_LayoutBounds @0x413220

    // --- Scroll_RunAnimationLoop leaves -----------------------------------
    void (*scrollOpen)(void* self);                             // VIBE_Scroll_Open @0x4be8d8
    void (*scrollUpdateAnimation)();                            // VIBE_Scroll_UpdateAnimation @0x4be990
    int  (*scrollClose)();                                      // VIBE_Scroll_Close @0x4be960
    int  (*gameLogicRunFrameLoop)(int a, int b, const void* p); // VIBE_GameLogic_RunFrameLoop @0x4c09a0
    int  (*readMouseRelease)();                                 // dword_672230
};

const GuiWidgetNHooks* SetGuiWidgetNHooks(const GuiWidgetNHooks* hooks);
const GuiWidgetNHooks* GuiWidgetNHooks_Default();
const GuiWidgetNHooks& GuiWidgetNHooksActive();

// Reset module-owned tables + restore default hooks (deterministic test start).
void ResetGuiWidgetN();

// ===========================================================================
// Recovered functions (1:1 with the originals).
// ===========================================================================

// gilde.exe 0x412480 — VIBE_Widget_RefreshText (widgetIdx@eax).
// If widget[+88] (text-present flag) is zero -> NOP. Otherwise, if the widget has a
// parent record (+44), centers the text under the parent's mid-x; else centers it
// under the screen mid-x (dword_69FFBC.hiword). The y is taken from widget +16+2
// (the +18 word). Forwards the recomputed origin to Widget_LayoutBounds. Returns the
// low byte of the layout result (0 when text flag clear).
int Widget_RefreshTextN(int widgetIdx);

// gilde.exe 0x412530 — VIBE_Widget_SetTextColor (widgetIdx@eax, color@dx).
// Stores the 16-bit color word at widget[+20], then re-lays the text via
// Widget_RefreshText. Returns the RefreshText result.
// FAITHFUL FIELD OVERLAP: widget +20 is the high word of the dword at +18, which
// RefreshText reads (>>16) as the text "width" for its centering halfW. So the color
// word and the RefreshText width are the SAME storage in the original — setting a
// color changes the next RefreshText centering. This is intentional 1:1 behavior.
int Widget_SetTextColor(int widgetIdx, std::int16_t color);

// gilde.exe 0x4bcc30 — VIBE_StatusText_ClearTable ().
// Zeros the active-marker column (dword_11B5158[i*50]) for all 32 entries. Returns
// 1600*4 == 6400 (the original returns result*4 where result ends at 1600).
int StatusText_ClearTable();

// gilde.exe 0x537318 — VIBE_Scroll_RunAnimationLoop (self@ecx, a2@ebx, a3@edi).
// Opens the parchment scroll, then pumps the game frame loop: each iteration updates
// the scroll animation, and when the mouse-release flag (dword_672230) is set it
// latches the quit (dword_631614 = 1). On loop exit closes the scroll and returns
// its result.
int Scroll_RunAnimationLoop(void* self, int a2, const char* a3);

// gilde.exe 0x4b8438 — VIBE_InfoPanel_Destroy ().
// When the panel form is open (g_infoPanelForm != -1): destroys each of the 4 live
// child widgets (Widget_DestroyByType, reset to -1), destroys the form
// (Form_Destroy), then resets the form id + 10 secondary tracking handles to -1.
void InfoPanel_Destroy();

} // namespace guild::gui

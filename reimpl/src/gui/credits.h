#pragma once
// guild::gui — the credits screens.
//
// gilde.exe 0x56e524 — VIBE_Menu_RunCreditsScroll  (the full-screen scrolling crawl).
// gilde.exe 0x529c30 — VIBE_Menu_RunCreditsWindow  (a fixed framed credits block).
//
// RunCreditsScroll fades to black, renders the 3D scene + entity list, creates a 16px
// "scroll" window (Window_Create(100,0,W,600,16)) at the bottom, fills it with rich-text
// crawl 7135, then runs two frame loops: the main crawl (advancing the window's scroll
// offset every `step` frames) until the text scrolls past its height, then a fade-out
// loop.  The scroll step is rate-adapted from the frame-time metric dword_631630.
// RunCreditsWindow is the simpler boxed variant (Window_Create(32,96,400,620,21), credits
// block 5576, ESC/click to close).
//
// What this module recovers + exposes for isolated testing:
//   * the scroll-window geometry + the rich-text/credits-block string ids;
//   * the scroll-speed ramp keyed on the frame-time metric (>=80 -> 4, >=30 -> 2, else 1);
//   * the per-frame scroll-offset advance (++offset every `step` frames) and the
//     scroll-complete predicate (offset has pushed the text past its rendered height).
//
// ODR: Window_Create / Text_RenderRichString / the fade register live in unowned modules
// (routed through the host sink); only the scroll arithmetic is owned here.

#include "gui/types.h"

namespace guild::gui {

// ---------------------------------------------------------------------------
// String ids / geometry (the literal arguments of the original).
// ---------------------------------------------------------------------------
inline constexpr int kCreditsCrawlText  = 7135; // 0x56e5ba RenderRichString (scroll)
inline constexpr int kCreditsBlockText  = 5576; // 0x529c80 RenderCreditsBlock (window)
inline constexpr int kCreditsFadeColor  = 60;   // 0x56e54e Fade_Register arg ("BLACK", 60)

// RunCreditsWindow box: Window_Create(32, 96, 400, 620, 21).  0x529c6f.
inline constexpr int kCreditsWindowX      = 32;
inline constexpr int kCreditsWindowY      = 96;
inline constexpr int kCreditsWindowW      = 400;
inline constexpr int kCreditsWindowH      = 620;
inline constexpr int kCreditsWindowKind   = 21;

// RunCreditsScroll crawl window: Window_Create(100, 0, screenW, 600, 16).  0x56e5ae.
inline constexpr int kCreditsScrollX      = 100;
inline constexpr int kCreditsScrollY      = 0;
inline constexpr int kCreditsScrollH      = 600;
inline constexpr int kCreditsScrollKind   = 16;

// ---------------------------------------------------------------------------
// Scroll-speed ramp — gilde.exe 0x56e681 / 0x56e768.
//   dword_631630 (a float frame-time metric):
//     >= 80.0 -> step 4 ; >= 30.0 -> step 2 ; else step 1.
// (1117782016 == 80.0f, 1106247680 == 30.0f as IEEE-754 bit patterns.)
// ---------------------------------------------------------------------------
inline constexpr float kCreditsRampHigh = 80.0f; // dword_631630 >= -> 4
inline constexpr float kCreditsRampMid  = 30.0f; // dword_631630 >= -> 2

int Credits_ScrollStep(float frameTimeMetric); // returns 1, 2 or 4

// ---------------------------------------------------------------------------
// Scroll advance — gilde.exe 0x56e635.
//   if (frame % step == 0) ++offset;   (offset starts at -(screenW>>16-ish init))
// We expose the discrete advance so a crawl can be replayed deterministically.
// ---------------------------------------------------------------------------

// Initial scroll offset — 0x56e5d2: *(window+584) = -(screenH).  The crawl starts fully
// below the window so the first visible line scrolls up from the bottom.
inline constexpr int Credits_InitialOffset(int screenHeight) { return -screenHeight; }

// One frame's update: given the current offset and the frame counter, return the new
// offset.  Mirrors `if (!(frame % step)) ++offset;` (0x56e635 / 0x56e74a).
int Credits_AdvanceOffset(int offset, int frame, int step);

// Scroll-complete predicate — gilde.exe 0x56e669.
//   done when  textHeight * dbl_625324 + offset  >  windowTextBottom (window+580).
// dbl_625324 is the line-height scale; we expose the comparison so the loop's exit can be
// checked.  Returns true when the crawl has fully passed (the original sets dword_631614).
bool Credits_ScrollComplete(int textBottom, int textHeight, double lineScale, int offset);

// ---------------------------------------------------------------------------
// Host sink (mockable): the fade/render/window calls live in unowned clusters.
// ---------------------------------------------------------------------------
struct CreditsHost {
    virtual ~CreditsHost() = default;
    // Return true while the frame loop should keep running (VIBE_GameLogic_RunFrameLoop).
    virtual bool RunFrame() { return false; }
    // The escape/close flags (byte_67225C / dword_672230) — true requests close.
    virtual bool CloseRequested() { return false; }
};

// gilde.exe 0x529c30 — run the boxed credits window: keep running frames until the host
// requests close.  Returns the number of frames rendered (for testability).
int Credits_RunWindow(CreditsHost& host);

// gilde.exe 0x56e524 (the main crawl loop only).  Drive the scroll offset across frames
// until either the host closes or the crawl completes.  `screenHeight`/`textHeight` set
// the geometry; `frameTimeMetric` (read per frame) sets the step.  Returns the final
// offset; sets *frames to the frame count.
int Credits_RunScrollLoop(CreditsHost& host, int screenHeight, int textBottom,
                          int textHeight, double lineScale, float frameTimeMetric,
                          int maxFrames, int* frames);

} // namespace guild::gui

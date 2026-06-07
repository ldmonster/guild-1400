#pragma once
// guild::gui — the two credits-screen RunXxx FUNCTION bodies, reconstructed 1:1.
//
// gilde.exe 0x56e524 — VIBE_Menu_RunCreditsScroll  (the full-screen scrolling crawl over
//                      the _CREDITS_BACKGROUND entity list #1792).
// gilde.exe 0x529c30 — VIBE_Menu_RunCreditsWindow  (the boxed credits-window variant).
//
// The SCROLL ARITHMETIC (speed ramp / per-frame offset advance / completion predicate /
// initial offset) is already reconstructed in gui/credits.{h,cpp} as the gui::credits
// model (Credits_ScrollStep / Credits_AdvanceOffset / Credits_ScrollComplete /
// Credits_InitialOffset).  This module does NOT redefine any of that — it REUSES it and
// adds the surrounding function bodies: the fade-in spin, the background render
// (RenderEntityScene + RenderEntityList #1792), the crawl-window build
// (Window_Create(100,0,screenW,600,16); PositionAtCoord; Text_RenderRichString 0x1BDF),
// the two RunFrameLoop loops (crawl + fade-out), and the exit/cleanup.
//
// ============================================================================
// VIBE_Menu_RunCreditsScroll @0x56e524 — body (every step):
//   0x56e549  step v0 = 4
//   0x56e54e  rec = Fade_Register(0,0, screenH>>16, screenW>>16, "BLACK", 60, 1)  (fade IN)
//   0x56e55a  while ((*rec & 4) == 0) RunFrameLoop(147591,147591,4)   // spin until faded
//   0x56e576  if (rec) { Fade_Unregister(rec); Fade_Register(...,60,10); }
//   0x56e57c  Window_RenderEntityScene(rec, sceneArg)
//   0x56e58b  Window_RenderEntityList(1792)                     // _CREDITS_BACKGROUND
//   0x56e5ae  win = Window_Create(100, 0, screenW, 600, 16)     // the crawl window
//   0x56e5b0  Window_PositionAtCoord(win, 1)
//   0x56e5ba  Text_RenderRichString(0x1BDF /*7135*/)            // the crawl text
//   0x56e5d2  *(win+584) = -(screenH>>16)                       // initial offset = -screenH
//   0x56e5df  v5 = 0                                            // frame counter
//   0x56e5f7  dword_62D314 = 1                                  // crawl-active flag
//   --- crawl loop (0x56e612) ---
//   0x56e612  while (RunFrameLoop(147591,0,step)) {
//   0x56e6c3    if (dword_672230 || byte_67225C) dword_631614 = 1;   // ESC / click -> close
//   0x56e635    if (!(v5 % step)) ++*(win+584);                       // advance offset
//   0x56e65d    ++v5;
//   0x56e669    if ((double)*(win+580) < (double)*(i16*)(win+10)*dbl_625324 + *(win+584))
//   0x56e66b      dword_631614 = 1;                                   // crawl complete
//   0x56e681    step = ramp(dword_631630):  >=80 -> 4 ; >=30 -> 2 ; else 1;
//             }
//   --- fade-out loop (0x56e70d) ---
//   0x56e70d  rec2 = Fade_Register(...,60,1)                          // fade IN (out crawl)
//   0x56e719  for (; (*rec2 & 4)==0; ++v5) {
//   0x56e73e    RunFrameLoop(147591, rec2, step);
//   0x56e74a    if (!(v5 % step)) ++*(win+584);                       // keep scrolling
//   0x56e768    step = ramp(dword_631630);
//             }
//   0x56e779  Window_RemoveIfActive(win, ...)
//   0x56e783  Window_RenderEntityList(1773)                           // restore prev list
//   0x56e78a  if (rec2) Fade_Unregister(rec2);
//   0x56e7ab  Fade_Register(...,60,10)                                // final fade OUT
//   0x56e7b2  dword_62D314 = 0
//
// VIBE_Menu_RunCreditsWindow @0x529c30 — body:
//   0x529c4d  dword_69FF88 = 0
//   0x529c53  dword_69FF80 = 1
//   0x529c59  dword_69FF84 = 1
//   0x529c69  dword_69FF8C = 2
//   0x529c6f  win = Window_Create(32, 96, 400, 620, 21)
//   0x529c80  Text_RenderCreditsBlock(5576, win)
//   0x529c98  while (RunFrameLoop(147591, a1, 1))
//   0x529ca1    if (byte_67225C == 1) dword_631614 = 1;               // ESC -> close
//   0x529cb5  Window_RemoveIfActive(win, ...)
// ============================================================================
//
// HOST BOUNDARIES (RunFrameLoop, fade register/poll/unregister, scene/entity-list render,
// window create/position/remove, the rich-text + credits-block fill, the ESC/click edge
// sources dword_672230/byte_67225C) all go through CreditsRunHooks with INERT DEFAULTS in
// credits_run.cpp, so both functions link in the unified build and run headless.
//
// ODR: this module DEFINES no global owned elsewhere; the scene flags (dword_69FF80..8C,
// dword_62D314) and the close/select words (dword_631614, dword_672230, byte_67225C) are
// modelled as one reconstructed CreditsRunState the functions read/write, bit-for-bit.

#include "gui/credits.h"   // gui::credits model — REUSED, not redefined.
#include "gui/types.h"

namespace guild::gui {

// ===========================================================================
// Literal arguments of the two originals (the ids/coords/colors).
// (kCreditsScroll*/kCreditsWindow*/kCreditsCrawlText/kCreditsBlockText/kCreditsFadeColor
//  already live in credits.h and are reused; here we add the ones credits.h omits.)
// ===========================================================================
inline constexpr int kCreditsScrollW          = 600;   // Window_Create(...,600,16) — fixed h arg
inline constexpr int kCreditsBackgroundList   = 1792;  // RenderEntityList(1792) = _CREDITS_BACKGROUND
inline constexpr int kCreditsRestoreList      = 1773;  // RenderEntityList(1773) on exit
inline constexpr int kCreditsScrollPositionAt = 1;     // PositionAtCoord(win, 1)
inline constexpr int kCreditsFadeInDelay      = 1;     // Fade_Register(...,60,1)  (fade in)
inline constexpr int kCreditsFadeOutDelay     = 10;    // Fade_Register(...,60,10) (fade out)
inline constexpr int kCreditsScrollActiveFlag = 1;     // dword_62D314 = 1 during crawl

// RunCreditsWindow scene flags (dword_69FF80..8C) set before the window build.
inline constexpr int kCreditsWinFlag80 = 1; // dword_69FF80 = 1
inline constexpr int kCreditsWinFlag84 = 1; // dword_69FF84 = 1
inline constexpr int kCreditsWinFlag88 = 0; // dword_69FF88 = 0
inline constexpr int kCreditsWinFlag8C = 2; // dword_69FF8C = 2

// ===========================================================================
// Reconstructed state — the BSS words these two functions read/write.
// (Their own faithful copy; bit values match the original exactly.)
// ===========================================================================
struct CreditsRunState {
    // Screen geometry (dword_69FFBC = screenW, dword_69FFB8>>16 = screenH).
    int screenW = 800;        // dword_69FFBC
    int screenH = 600;        // dword_69FFB8 >> 16

    // Crawl text metrics (the crawl window's +580 textBottom, +10 textHeight word).
    int textBottom = 0;       // *(win+580)
    int textHeight = 0;       // *(i16*)(win+10)

    // Frame-time metric driving the speed ramp (dword_631630, an IEEE-754 float).
    float frameTimeMetric = 0.0f;

    // Close / select / scene flags the loops mutate.
    int close = 0;            // dword_631614 (armed -> loop terminates next RunFrameLoop)
    int clickEdge = 0;        // dword_672230 (a click this frame)
    int escDown = 0;          // byte_67225C  (ESC/quit this frame)
    int scrollActive = 0;     // dword_62D314 (1 while the crawl is on screen)

    // RunCreditsWindow scene flags.
    int winFlag80 = 0, winFlag84 = 0, winFlag88 = 0, winFlag8C = 0;

    // Live crawl offset (*(win+584)) — exposed for tests; advanced via the model.
    int offset = 0;
};

// ===========================================================================
// Host-boundary hooks (installable; INERT DEFAULTS in credits_run.cpp).
// ===========================================================================
struct CreditsRunHooks {
    virtual ~CreditsRunHooks() = default;

    // ---- fade (Fade_Register/Unregister @0x41f0e8 / 0x41f18c) ----
    // Register a fade; returns the fade RECORD handle (bit 2 / 0x4 of *handle set == done).
    virtual void* FadeRegister(int color, int delay) { (void)color; (void)delay; return nullptr; }
    // The fade record's "done" bit: (*rec & 4) != 0. Inert default: done immediately.
    virtual bool  FadeDone(void* rec) { (void)rec; return true; }
    virtual void  FadeUnregister(void* rec) { (void)rec; }

    // ---- frame pump (RunFrameLoop @0x4c09a0) ----
    // One tick. Returns nonzero to keep the loop running. `frame` is the loop's counter.
    virtual int   RunFrameLoop(int frame) { (void)frame; return 0; }

    // ---- background render ----
    virtual void  RenderEntityScene(void* fadeRec) { (void)fadeRec; }    // @0x41523c
    virtual void  RenderEntityList(int listId) { (void)listId; }         // @0x4134f0

    // ---- crawl window ----
    virtual int   WindowCreate(int x, int y, int w, int h, int kind) {   // @0x419c38
        (void)x; (void)y; (void)w; (void)h; (void)kind; return -1; }
    virtual void  WindowPositionAtCoord(int win, int coord) { (void)win; (void)coord; }   // @0x41d7e0
    virtual void  WindowRemoveIfActive(int win) { (void)win; }           // @0x41a7a8
    virtual void  TextRenderRichString(int textId) { (void)textId; }     // @0x59d6e8
    virtual void  TextRenderCreditsBlock(int textId, int win) { (void)textId; (void)win; } // @0x5a161c

    // ---- per-frame ESC/click edge sources ----
    // The crawl reads (dword_672230 || byte_67225C); the window reads byte_67225C.
    virtual bool  ClickEdge(int frame) { (void)frame; return false; }    // dword_672230
    virtual bool  EscDown(int frame) { (void)frame; return false; }      // byte_67225C
};

// Install hooks (null restores the inert defaults). Returns the previous hooks.
CreditsRunHooks* Credits_SetRunHooks(CreditsRunHooks* hooks);

// ===========================================================================
// Recorded run (testable): the literal build args + the loop result, recorded via hooks.
// ===========================================================================
struct CreditsRunRecord {
    // RunCreditsScroll build:
    int  windowId     = -1;   // Window_Create return
    int  windowX = 0, windowY = 0, windowW = 0, windowH = 0, windowKind = 0;
    int  positionCoord = 0;   // PositionAtCoord arg
    int  richTextId   = 0;    // Text_RenderRichString arg (0x1BDF)
    int  blockTextId  = 0;    // Text_RenderCreditsBlock arg (5576, window variant)
    int  bgListId     = 0;    // RenderEntityList during build (1792)
    int  restoreListId= 0;    // RenderEntityList on exit  (1773)
    int  initialOffset= 0;    // *(win+584) initial = -screenH

    // Loop outcome:
    int  crawlFrames  = 0;    // crawl-loop iterations
    int  fadeFrames   = 0;    // fade-out-loop iterations
    int  finalOffset  = 0;    // *(win+584) at exit
    bool completedByScroll = false; // crawl finished by the completion predicate
    bool completedByEsc    = false; // crawl finished by ESC/click

    // Window variant:
    int  winVarFrames = 0;

    // Call-order trace (the e2e asserts a deterministic ordered tag sequence).
    static constexpr int kMaxTrace = 256;
    const char* trace[kMaxTrace];
    int  traceCount = 0;
};

// gilde.exe 0x56e524 — run the full scrolling-credits crawl: fade in, render the
// background entity list, build the crawl window, run the crawl loop (advancing the offset
// per the gui::credits ramp) until ESC/click or completion, then the fade-out loop, then
// cleanup. `st` carries geometry/metrics in and the close flags out; `rec` (optional)
// records the build + outcome. `maxFrames` bounds each loop for headless testing.
void Menu_RunCreditsScroll(CreditsRunState& st, CreditsRunRecord* rec, int maxFrames);

// gilde.exe 0x529c30 — run the boxed credits window: set the scene flags, build the window
// (32,96,400,620,21) + credits block 5576, then spin RunFrameLoop until ESC closes it.
void Menu_RunCreditsWindow(CreditsRunState& st, CreditsRunRecord* rec, int maxFrames);

} // namespace guild::gui

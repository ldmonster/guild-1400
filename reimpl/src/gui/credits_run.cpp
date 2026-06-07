#include "gui/credits_run.h"

// guild::gui — the two credits-screen RunXxx function bodies (the form/build + the
// per-frame RunFrameLoop loops + the exit), reconstructed 1:1 from gilde.exe 0x56e524 /
// 0x529c30.  See credits_run.h for the per-address translation map.
//
// The scroll arithmetic is REUSED from gui/credits.{h,cpp} (the gui::credits model):
//   Credits_InitialOffset / Credits_ScrollStep / Credits_AdvanceOffset /
//   Credits_ScrollComplete.  Nothing in that model is redefined here.
//
// Every host boundary (fade, frame pump, scene/list render, window create/position/remove,
// rich-text/credits-block fill, ESC/click edge) goes through CreditsRunHooks; the inert
// defaults below no-op so both functions link in the unified build and run headless.

namespace guild::gui {

// ===========================================================================
// Inert default hooks.
// ===========================================================================
namespace {

struct DefaultCreditsHooks : CreditsRunHooks {};

DefaultCreditsHooks g_defaultCreditsHooks;
CreditsRunHooks*    g_creditsHooks = &g_defaultCreditsHooks;

} // namespace

CreditsRunHooks* Credits_SetRunHooks(CreditsRunHooks* hooks) {
    CreditsRunHooks* prev = g_creditsHooks;
    g_creditsHooks = hooks ? hooks : &g_defaultCreditsHooks;
    return prev;
}

// ---- trace helper -----------------------------------------------------------
static void Trace(CreditsRunRecord* rec, const char* tag) {
    if (rec && rec->traceCount < CreditsRunRecord::kMaxTrace)
        rec->trace[rec->traceCount++] = tag;
}

// dbl_625324 — the line-height scale used in the completion predicate
// (textHeight*dbl_625324 + offset).  gilde.exe 0x625324 == 1.5 (the brief's textHeight*1.5).
static constexpr double kCreditsLineScale = 1.5; // dbl_625324

// ===========================================================================
// gilde.exe 0x56e524 — VIBE_Menu_RunCreditsScroll.
// ===========================================================================
void Menu_RunCreditsScroll(CreditsRunState& st, CreditsRunRecord* rec, int maxFrames) {
    CreditsRunHooks* h = g_creditsHooks;

    Trace(rec, "ScrollBegin");

    // 0x56e549  step starts at 4.
    int step = 4;

    // 0x56e54e  Fade_Register(0,0, screenH, screenW, "BLACK", 60, 1) — the fade IN.
    void* rec1 = h->FadeRegister(kCreditsFadeColor, kCreditsFadeInDelay);
    Trace(rec, "FadeRegisterIn");
    // 0x56e55a  while ((*rec1 & 4) == 0) RunFrameLoop(...);   spin until the fade completes.
    {
        int spin = 0;
        while (!h->FadeDone(rec1)) {
            h->RunFrameLoop(spin);
            ++spin;
            if (maxFrames > 0 && spin >= maxFrames) break; // headless bound
        }
    }
    // 0x56e576  if (rec1) { Fade_Unregister(rec1); Fade_Register(...,60,10); }
    if (rec1) {
        h->FadeUnregister(rec1);
        h->FadeRegister(kCreditsFadeColor, kCreditsFadeOutDelay);
        Trace(rec, "FadeReRegister");
    }

    // 0x56e57c  Window_RenderEntityScene(rec1, ...);  0x56e58b  RenderEntityList(1792).
    h->RenderEntityScene(rec1);
    h->RenderEntityList(kCreditsBackgroundList);     // _CREDITS_BACKGROUND
    Trace(rec, "RenderBackground");

    // 0x56e5ae  win = Window_Create(100, 0, screenW, 600, 16).
    int win = h->WindowCreate(kCreditsScrollX, kCreditsScrollY, st.screenW,
                              kCreditsScrollW, kCreditsScrollKind);
    // 0x56e5b0  Window_PositionAtCoord(win, 1).
    h->WindowPositionAtCoord(win, kCreditsScrollPositionAt);
    // 0x56e5ba  Text_RenderRichString(0x1BDF).
    h->TextRenderRichString(kCreditsCrawlText);
    Trace(rec, "BuildCrawlWindow");

    // 0x56e5d2  *(win+584) = -(screenH).  (REUSE gui::credits initial-offset model.)
    int offset = Credits_InitialOffset(st.screenH);
    st.offset = offset;

    // 0x56e5df  v5 = 0;   0x56e5f7  dword_62D314 = 1 (crawl active).
    int frame = 0;
    st.scrollActive = kCreditsScrollActiveFlag;

    if (rec) {
        rec->windowId      = win;
        rec->windowX = kCreditsScrollX; rec->windowY = kCreditsScrollY;
        rec->windowW = st.screenW;      rec->windowH = kCreditsScrollW;
        rec->windowKind = kCreditsScrollKind;
        rec->positionCoord = kCreditsScrollPositionAt;
        rec->richTextId    = kCreditsCrawlText;
        rec->bgListId      = kCreditsBackgroundList;
        rec->initialOffset = offset;
    }

    // -------------------- crawl loop (0x56e612) --------------------
    // 0x56e612  while (RunFrameLoop(147591,0,step)) { ... }
    Trace(rec, "CrawlLoopBegin");
    bool byEsc = false, byScroll = false;
    while (h->RunFrameLoop(frame)) {
        // 0x56e6c3  if (dword_672230 || byte_67225C) dword_631614 = 1;
        st.clickEdge = h->ClickEdge(frame) ? 1 : 0;   // dword_672230
        st.escDown   = h->EscDown(frame)   ? 1 : 0;   // byte_67225C
        if (st.clickEdge || st.escDown) {
            st.close = 1;                             // dword_631614 = 1
            byEsc = true;
            Trace(rec, "CrawlEsc");
        }

        // 0x56e635  if (!(v5 % step)) ++*(win+584);   (REUSE Credits_AdvanceOffset.)
        offset = Credits_AdvanceOffset(offset, frame, step);
        // 0x56e65d  ++v5;
        ++frame;

        // 0x56e669  completion: textBottom < textHeight*dbl_625324 + offset -> close.
        //           (REUSE Credits_ScrollComplete.)
        if (Credits_ScrollComplete(st.textBottom, st.textHeight, kCreditsLineScale, offset)) {
            st.close = 1;                             // dword_631614 = 1
            byScroll = true;
            Trace(rec, "CrawlComplete");
        }

        // 0x56e681  step = ramp(dword_631630):  >=80 -> 4 ; >=30 -> 2 ; else 1.
        //           (REUSE Credits_ScrollStep.)
        step = Credits_ScrollStep(st.frameTimeMetric);

        st.offset = offset;
        if (st.close) break;                          // dword_631614 -> RunFrameLoop ends loop
        if (maxFrames > 0 && frame >= maxFrames) break; // headless bound
    }
    Trace(rec, "CrawlLoopEnd");

    // -------------------- fade-out loop (0x56e70d) --------------------
    // 0x56e70d  rec2 = Fade_Register(...,60,1).
    void* rec2 = h->FadeRegister(kCreditsFadeColor, kCreditsFadeInDelay);
    Trace(rec, "FadeOutLoopBegin");
    int fadeFrames = 0;
    // 0x56e719  for (; (*rec2 & 4)==0; ++v5) { RunFrameLoop; advance offset; ramp; }
    while (!h->FadeDone(rec2)) {
        h->RunFrameLoop(frame);                       // 0x56e73e
        // 0x56e74a  if (!(v5 % step)) ++*(win+584);
        offset = Credits_AdvanceOffset(offset, frame, step);
        ++frame;
        ++fadeFrames;
        // 0x56e768  step = ramp(dword_631630).
        step = Credits_ScrollStep(st.frameTimeMetric);
        st.offset = offset;
        if (maxFrames > 0 && fadeFrames >= maxFrames) break; // headless bound
    }
    Trace(rec, "FadeOutLoopEnd");

    // 0x56e779  Window_RemoveIfActive(win, ...).
    h->WindowRemoveIfActive(win);
    // 0x56e783  RenderEntityList(1773).
    h->RenderEntityList(kCreditsRestoreList);
    // 0x56e78a  if (rec2) Fade_Unregister(rec2).
    if (rec2)
        h->FadeUnregister(rec2);
    // 0x56e7ab  Fade_Register(...,60,10) — the final fade OUT.
    h->FadeRegister(kCreditsFadeColor, kCreditsFadeOutDelay);
    // 0x56e7b2  dword_62D314 = 0.
    st.scrollActive = 0;
    Trace(rec, "ScrollEnd");

    if (rec) {
        rec->crawlFrames = frame - fadeFrames;
        rec->fadeFrames  = fadeFrames;
        rec->finalOffset = offset;
        rec->restoreListId = kCreditsRestoreList;
        rec->completedByScroll = byScroll;
        rec->completedByEsc    = byEsc;
    }
}

// ===========================================================================
// gilde.exe 0x529c30 — VIBE_Menu_RunCreditsWindow.
// ===========================================================================
void Menu_RunCreditsWindow(CreditsRunState& st, CreditsRunRecord* rec, int maxFrames) {
    CreditsRunHooks* h = g_creditsHooks;

    Trace(rec, "WindowBegin");

    // 0x529c4d..0x529c69  the scene flags.
    st.winFlag88 = kCreditsWinFlag88; // dword_69FF88 = 0
    st.winFlag80 = kCreditsWinFlag80; // dword_69FF80 = 1
    st.winFlag84 = kCreditsWinFlag84; // dword_69FF84 = 1
    st.winFlag8C = kCreditsWinFlag8C; // dword_69FF8C = 2

    // 0x529c6f  win = Window_Create(32, 96, 400, 620, 21).
    int win = h->WindowCreate(kCreditsWindowX, kCreditsWindowY, kCreditsWindowW,
                              kCreditsWindowH, kCreditsWindowKind);
    // 0x529c80  Text_RenderCreditsBlock(5576, win).
    h->TextRenderCreditsBlock(kCreditsBlockText, win);
    Trace(rec, "BuildWindow");

    if (rec) {
        rec->windowId   = win;
        rec->windowX = kCreditsWindowX; rec->windowY = kCreditsWindowY;
        rec->windowW = kCreditsWindowW; rec->windowH = kCreditsWindowH;
        rec->windowKind = kCreditsWindowKind;
        rec->blockTextId = kCreditsBlockText;
    }

    // 0x529c98  while (RunFrameLoop(147591, a1, 1)) { if (byte_67225C==1) dword_631614=1; }
    Trace(rec, "WindowLoopBegin");
    int frame = 0;
    while (h->RunFrameLoop(frame)) {
        st.escDown = h->EscDown(frame) ? 1 : 0;       // byte_67225C
        if (st.escDown == 1) {
            st.close = 1;                             // dword_631614 = 1
            Trace(rec, "WindowEsc");
        }
        ++frame;
        if (st.close) break;                          // dword_631614 -> RunFrameLoop ends loop
        if (maxFrames > 0 && frame >= maxFrames) break; // headless bound
    }
    Trace(rec, "WindowLoopEnd");

    // 0x529cb5  Window_RemoveIfActive(win, ...).
    h->WindowRemoveIfActive(win);
    Trace(rec, "WindowEnd");

    if (rec)
        rec->winVarFrames = frame;
}

} // namespace guild::gui

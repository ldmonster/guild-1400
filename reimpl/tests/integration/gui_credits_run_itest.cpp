// Integration tests for the two credits RunXxx loops (gilde.exe 0x56e524 / 0x529c30).
//
// Drive scripted ESC / click / scroll-completion edges through the run hooks and assert
// the EXACT global-flag mutations (dword_631614 close, dword_672230 click, byte_67225C
// ESC, dword_62D314 scroll-active) and the exit cause the original would take. The loop
// body, the offset advance and the completion predicate run the REAL translated code
// (reusing the gui::credits model); only the host edges are scripted.

#include "test.h"

#include "gui/credits_run.h"
#include "gui/credits.h"

using namespace guild::gui;

namespace {
// Frame-scripted host: emit an ESC or click at a chosen frame; FadeDone true so the
// fade-out loop runs zero iterations.
struct ScriptHooks : CreditsRunHooks {
    int  frameLimit = 64;
    int  escAtFrame = -1;
    int  clickAtFrame = -1;
    int  runFrameCalls = 0;

    void* FadeRegister(int,int) override { return nullptr; }
    bool  FadeDone(void*) override { return true; }
    int   RunFrameLoop(int frame) override { ++runFrameCalls; return frame < frameLimit ? 1 : 0; }
    int   WindowCreate(int,int,int,int,int) override { return 3; }
    bool  ClickEdge(int frame) override { return clickAtFrame >= 0 && frame == clickAtFrame; }
    bool  EscDown(int frame) override { return escAtFrame >= 0 && frame == escAtFrame; }
};
} // namespace

// ---------------------------------------------------------------------------
// Scroll crawl: ESC at frame 3 sets close (dword_631614) and exits by ESC, NOT by scroll.
// ---------------------------------------------------------------------------
TEST(GuiCreditsRunIT, ScrollEscEarlyOut) {
    ScriptHooks h; h.escAtFrame = 3;
    CreditsRunHooks* prev = Credits_SetRunHooks(&h);
    CreditsRunState st; st.screenH = 600; st.screenW = 800;
    st.textBottom = 1 << 30; st.textHeight = 0;     // can never complete by scroll
    st.frameTimeMetric = 0.0f;
    CreditsRunRecord rec;
    Menu_RunCreditsScroll(st, &rec, 64);
    Credits_SetRunHooks(prev);

    CHECK_EQ(st.close, 1);              // dword_631614 = 1
    CHECK_EQ(st.escDown, 1);           // byte_67225C latched
    CHECK(rec.completedByEsc);
    CHECK(!rec.completedByScroll);
    // ESC at frame index 3 -> the loop ran frames 0..3 then broke (4 crawl frames).
    CHECK_EQ(rec.crawlFrames, 4);
    CHECK_EQ(st.scrollActive, 0);      // dword_62D314 reset on exit
}

// ---------------------------------------------------------------------------
// Scroll crawl: a click edge (dword_672230) also arms close and exits by ESC path.
// ---------------------------------------------------------------------------
TEST(GuiCreditsRunIT, ScrollClickEarlyOut) {
    ScriptHooks h; h.clickAtFrame = 2;
    CreditsRunHooks* prev = Credits_SetRunHooks(&h);
    CreditsRunState st; st.screenH = 600; st.screenW = 800;
    st.textBottom = 1 << 30; st.textHeight = 0;
    st.frameTimeMetric = 0.0f;
    CreditsRunRecord rec;
    Menu_RunCreditsScroll(st, &rec, 64);
    Credits_SetRunHooks(prev);

    CHECK_EQ(st.clickEdge, 1);          // dword_672230 latched
    CHECK_EQ(st.close, 1);             // dword_631614 = 1
    CHECK(rec.completedByEsc);         // click takes the same close branch
    CHECK_EQ(rec.crawlFrames, 3);      // frames 0..2 then break
}

// ---------------------------------------------------------------------------
// Scroll crawl: no ESC, the crawl completes by the scroll predicate (0x56e669).
// textBottom small so textHeight*1.5 + offset overtakes it after a few frames.
// ---------------------------------------------------------------------------
TEST(GuiCreditsRunIT, ScrollCompletesByScroll) {
    ScriptHooks h;
    CreditsRunHooks* prev = Credits_SetRunHooks(&h);
    CreditsRunState st; st.screenH = 5; st.screenW = 800;
    // offset starts at -5; step 1 (metric 0). textHeight*1.5 = 0, so complete when
    // textBottom < offset, i.e. offset > textBottom. With textBottom = -2, offset reaches
    // -2 after 3 advances (-5 -> -4 -> -3 -> -2 ... ) and -1 satisfies < .  Pick small.
    st.textBottom = -2; st.textHeight = 0;
    st.frameTimeMetric = 0.0f;
    CreditsRunRecord rec;
    Menu_RunCreditsScroll(st, &rec, 64);
    Credits_SetRunHooks(prev);

    CHECK(rec.completedByScroll);
    CHECK(!rec.completedByEsc);
    CHECK_EQ(st.close, 1);             // dword_631614 = 1
    // Confirm the exit offset really satisfies the REUSED predicate.
    CHECK(Credits_ScrollComplete(st.textBottom, st.textHeight, 1.5, st.offset));
    // ...and the offset one before would NOT have (the loop stopped at the first one).
    CHECK(!Credits_ScrollComplete(st.textBottom, st.textHeight, 1.5, st.offset - 1));
}

// ---------------------------------------------------------------------------
// Window variant: ESC (byte_67225C == 1) arms close; no ESC just runs to the bound.
// ---------------------------------------------------------------------------
TEST(GuiCreditsRunIT, WindowEscClose) {
    ScriptHooks h; h.escAtFrame = 4;
    CreditsRunHooks* prev = Credits_SetRunHooks(&h);
    CreditsRunState st;
    CreditsRunRecord rec;
    Menu_RunCreditsWindow(st, &rec, 64);
    Credits_SetRunHooks(prev);

    CHECK_EQ(st.escDown, 1);
    CHECK_EQ(st.close, 1);             // dword_631614 = 1
    CHECK_EQ(rec.winVarFrames, 5);     // frames 0..4 then break
}

TEST(GuiCreditsRunIT, WindowRunsToBoundWhenNoEsc) {
    ScriptHooks h; // never ESC
    CreditsRunHooks* prev = Credits_SetRunHooks(&h);
    CreditsRunState st;
    CreditsRunRecord rec;
    Menu_RunCreditsWindow(st, &rec, 10);
    Credits_SetRunHooks(prev);

    CHECK_EQ(st.close, 0);             // no close armed
    CHECK_EQ(rec.winVarFrames, 10);   // ran to the headless bound
}

// ---------------------------------------------------------------------------
// Determinism: two identical scripted runs produce identical records.
// ---------------------------------------------------------------------------
TEST(GuiCreditsRunIT, Deterministic) {
    auto run = [](CreditsRunRecord& rec) {
        ScriptHooks h; h.escAtFrame = 7;
        CreditsRunHooks* prev = Credits_SetRunHooks(&h);
        CreditsRunState st; st.screenH = 600; st.screenW = 800;
        st.textBottom = 1 << 30; st.textHeight = 0; st.frameTimeMetric = 45.0f; // step 2
        Menu_RunCreditsScroll(st, &rec, 64);
        Credits_SetRunHooks(prev);
    };
    CreditsRunRecord a, b;
    run(a); run(b);
    CHECK_EQ(a.crawlFrames, b.crawlFrames);
    CHECK_EQ(a.finalOffset, b.finalOffset);
    CHECK_EQ(a.completedByEsc ? 1 : 0, b.completedByEsc ? 1 : 0);
    CHECK_EQ(a.initialOffset, b.initialOffset);
}

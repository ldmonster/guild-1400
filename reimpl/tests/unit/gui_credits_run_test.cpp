// Unit (golden-vector) tests for the two credits-screen RunXxx function bodies
// (gilde.exe 0x56e524 VIBE_Menu_RunCreditsScroll / 0x529c30 VIBE_Menu_RunCreditsWindow),
// reconstructed in gui/credits_run.{h,cpp}.
//
// These assert the EXACT literal build (window geometry, string ids, entity-list ids,
// initial offset, scene flags) recorded via the run hooks, and that the scroll model
// REUSED from gui/credits.* advances deterministically per the real speed ramp.

#include "test.h"

#include "gui/credits_run.h"
#include "gui/credits.h"

using namespace guild::gui;

// A recording host that supplies a bounded frame loop and records each hook call.
namespace {
struct RecHooks : CreditsRunHooks {
    int frameLimit = 4;
    int wcX=0, wcY=0, wcW=0, wcH=0, wcKind=0, wcCalls=0;
    int richId=-1, blockId=-1, blockWin=-1;
    int bgList=-1, restoreList=-1, listCalls=0;
    int posWin=-1, posCoord=-1;
    int removeWin=-1;

    void* FadeRegister(int, int) override { return nullptr; }
    bool  FadeDone(void*) override { return true; }                 // skip fade spins
    int   RunFrameLoop(int frame) override { return frame < frameLimit ? 1 : 0; }
    void  RenderEntityList(int id) override {
        ++listCalls; if (id == 1792) bgList = id; if (id == 1773) restoreList = id;
    }
    int   WindowCreate(int x,int y,int w,int h,int k) override {
        ++wcCalls; wcX=x; wcY=y; wcW=w; wcH=h; wcKind=k; return 7;
    }
    void  WindowPositionAtCoord(int win,int c) override { posWin=win; posCoord=c; }
    void  WindowRemoveIfActive(int win) override { removeWin=win; }
    void  TextRenderRichString(int id) override { richId = id; }
    void  TextRenderCreditsBlock(int id,int win) override { blockId=id; blockWin=win; }
};
} // namespace

// ---------------------------------------------------------------------------
// RunCreditsScroll — the crawl window geometry + string/list ids (0x56e5ae..0x56e5ba).
// ---------------------------------------------------------------------------
TEST(GuiCreditsRun, ScrollBuildConstants) {
    RecHooks h; h.frameLimit = 0; // run no crawl frames, just build
    CreditsRunHooks* prev = Credits_SetRunHooks(&h);
    CreditsRunState st; st.screenW = 800; st.screenH = 600;
    CreditsRunRecord rec;
    Menu_RunCreditsScroll(st, &rec, 8);
    Credits_SetRunHooks(prev);

    // Window_Create(100, 0, screenH, 600, 16).
    // Disasm 0x56e594/0x56e59f: a3@cx = dword_69FFB8>>16 (screenH), NOT screenW.
    // (Hex-Rays mislabels the 3rd arg as dword_69FFBC; the asm loads it from
    //  dword_69FFB8+2.)  a4@bx = 0x258 = 600.
    CHECK_EQ(h.wcX, 100);
    CHECK_EQ(h.wcY, 0);
    CHECK_EQ(h.wcW, 600);          // screenH (a3@cx)
    CHECK_EQ(h.wcH, 600);          // fixed h arg (kCreditsScrollW, a4@bx)
    CHECK_EQ(h.wcKind, 16);
    CHECK_EQ(rec.windowId, 7);
    // PositionAtCoord(win, 1).
    CHECK_EQ(h.posWin, 7);
    CHECK_EQ(h.posCoord, 1);
    // Text_RenderRichString(0x1BDF == 7135).
    CHECK_EQ(h.richId, 7135);
    CHECK_EQ(rec.richTextId, kCreditsCrawlText);
    // RenderEntityList(1792) during build; (1773) on exit.
    CHECK_EQ(h.bgList, 1792);
    CHECK_EQ(rec.bgListId, kCreditsBackgroundList);
    CHECK_EQ(h.restoreList, 1773);
    CHECK_EQ(rec.restoreListId, kCreditsRestoreList);
    // Initial offset = -screenH (REUSED Credits_InitialOffset).
    CHECK_EQ(rec.initialOffset, -600);
    CHECK_EQ(rec.initialOffset, Credits_InitialOffset(600));
    // dword_62D314 cleared back to 0 at exit.
    CHECK_EQ(st.scrollActive, 0);
    // Window removed at exit.
    CHECK_EQ(h.removeWin, 7);
}

// ---------------------------------------------------------------------------
// RunCreditsWindow — boxed window geometry + credits block + scene flags.
// ---------------------------------------------------------------------------
TEST(GuiCreditsRun, WindowBuildConstants) {
    RecHooks h; h.frameLimit = 0;
    CreditsRunHooks* prev = Credits_SetRunHooks(&h);
    CreditsRunState st;
    CreditsRunRecord rec;
    Menu_RunCreditsWindow(st, &rec, 8);
    Credits_SetRunHooks(prev);

    // Window_Create(32, 96, 400, 620, 21).
    CHECK_EQ(h.wcX, 32);
    CHECK_EQ(h.wcY, 96);
    CHECK_EQ(h.wcW, 400);
    CHECK_EQ(h.wcH, 620);
    CHECK_EQ(h.wcKind, 21);
    // Text_RenderCreditsBlock(5576, win).
    CHECK_EQ(h.blockId, 5576);
    CHECK_EQ(rec.blockTextId, kCreditsBlockText);
    CHECK_EQ(h.blockWin, 7);
    // Scene flags dword_69FF80/84/88/8C = 1/1/0/2.
    CHECK_EQ(st.winFlag80, 1);
    CHECK_EQ(st.winFlag84, 1);
    CHECK_EQ(st.winFlag88, 0);
    CHECK_EQ(st.winFlag8C, 2);
    // Window removed at exit.
    CHECK_EQ(h.removeWin, 7);
}

// ---------------------------------------------------------------------------
// The REUSED speed ramp (gui::credits) — >=80 -> 4 ; >=30 -> 2 ; else 1.
// ---------------------------------------------------------------------------
TEST(GuiCreditsRun, ReusedRamp) {
    CHECK_EQ(Credits_ScrollStep(0.0f),   1);
    CHECK_EQ(Credits_ScrollStep(29.99f), 1);
    CHECK_EQ(Credits_ScrollStep(30.0f),  2);
    CHECK_EQ(Credits_ScrollStep(79.99f), 2);
    CHECK_EQ(Credits_ScrollStep(80.0f),  4);
    CHECK_EQ(Credits_ScrollStep(200.0f), 4);
}

// ---------------------------------------------------------------------------
// Per-frame offset advance is deterministic and matches the model directly.
// With metric 0 -> step 1, the offset advances every frame from -screenH.
// ---------------------------------------------------------------------------
TEST(GuiCreditsRun, DeterministicOffsetAdvance) {
    RecHooks h; h.frameLimit = 5;
    CreditsRunHooks* prev = Credits_SetRunHooks(&h);
    CreditsRunState st; st.screenH = 100; st.screenW = 800;
    st.textBottom = 1 << 30; st.textHeight = 0; // never complete by scroll
    st.frameTimeMetric = 0.0f;                  // step 1 every frame
    CreditsRunRecord rec;
    Menu_RunCreditsScroll(st, &rec, 5);
    Credits_SetRunHooks(prev);

    // Started at -100; step 1; 5 crawl frames advance offset by 5 -> -95.
    CHECK_EQ(rec.initialOffset, -100);
    CHECK_EQ(rec.crawlFrames, 5);
    // Each frame f advances when (f % 1)==0 -> always; -100 + 5 = -95.
    CHECK_EQ(st.offset, -95);
    CHECK(!rec.completedByEsc);
    CHECK(!rec.completedByScroll);
}

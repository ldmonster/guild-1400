// Integration tests for gui::Menu_RunLoadGame @0x56a270 — scripted click edges
// (dword_672228 / dword_62D22C) against the slot widget ids -> EXACT flag mutations:
// a picked save sets word_63C740=10, dword_631614=1, byte_122F530 = "Gamedata\\Saves\\..",
// returns 1; cancel/close returns 0; the confirm gate (byte_63CC40) gates the pick.
#include "test.h"

#include "gui/loadgame_run.h"

using namespace guild::gui;

namespace {

// A scripted single-frame harness: optionally a close edge and/or a click edge on a
// chosen hover widget id, then the loop ends after one frame.
struct ClickHooks : LoadGameRunHooks {
    std::vector<LoadGameSlot> grid;
    bool clickEdge = false;
    bool closeEdge = false;
    int  hover = -1;
    bool confirmResult = true;  // RunMessageBox(257) result
    int  framesAllowed = 1;
    int  frameSeen = -1;

    void LoadSlotMetadata(int, const char*, std::vector<LoadGameSlot>& out) override {
        out = grid;
    }
    int  RunFrameLoop() override { return framesAllowed-- > 0 ? 1 : 0; }
    bool CloseRequested(int) override { return closeEdge; }
    bool ClickEdge(int f) override { frameSeen = f; return clickEdge; }
    int  HoverId(int) override { return hover; }
    bool ConfirmLoad() override { return confirmResult; }
};

std::vector<LoadGameSlot> Grid3() {
    std::vector<LoadGameSlot> g(kRunLoadSlotCount);
    for (int i = 0; i < 3; ++i) {
        g[i].present  = true;
        g[i].objId    = 100 + i;
        g[i].widgetId = 200 + i;
        g[i].name     = "SAVE" + std::to_string(i);
    }
    return g;
}

} // namespace

TEST(LoadGameRunDispatch, PickSave_SetsFlagsAndPath) {
    ClickHooks h;
    h.grid = Grid3();
    h.clickEdge = true;
    h.hover = 201;   // matches slot index 1 (widgetId 200+1)
    LoadGameRunHooks* prev = LoadGame_SetRunHooks(&h);

    LoadGameRunState st;   // confirmGate = 0 (no confirm)
    LoadGameRunRecord rec;
    int r = Menu_RunLoadGame(st, &rec, /*maxFrames*/ 4);

    LoadGame_SetRunHooks(prev);

    CHECK_EQ(r, 1);                          // v20 = 1, a save chosen
    CHECK_EQ(rec.matchedSlot, 1);
    CHECK_EQ(st.sessionFlags, 10);           // word_63C740 = 10
    CHECK_EQ(st.close, 1);                   // dword_631614 = 1
    CHECK_EQ(st.loadPath, std::string("Gamedata\\Saves\\SAVE1.SAV"));
    CHECK_EQ(rec.confirmAsked, false);       // gate inactive -> no message box
}

TEST(LoadGameRunDispatch, ClickEmptySlot_NoPick) {
    ClickHooks h;
    h.grid = Grid3();          // only slots 0..2 occupied
    h.clickEdge = true;
    h.hover = 999;             // matches no slot
    LoadGameRunHooks* prev = LoadGame_SetRunHooks(&h);

    LoadGameRunState st;
    LoadGameRunRecord rec;
    int r = Menu_RunLoadGame(st, &rec, 4);

    LoadGame_SetRunHooks(prev);

    CHECK_EQ(r, 0);
    CHECK_EQ(rec.matchedSlot, -1);
    CHECK_EQ(st.sessionFlags, 0);
    CHECK_EQ(st.loadPath, std::string(""));
}

TEST(LoadGameRunDispatch, ClickPlaceholderRow_Skipped) {
    // A row that is present but objId == -1 (placeholder) must be skipped even if its
    // widget id is hovered.
    ClickHooks h;
    h.grid = std::vector<LoadGameSlot>(kRunLoadSlotCount);
    h.grid[0].present = true; h.grid[0].objId = -1; h.grid[0].widgetId = 500;
    h.grid[0].name = "PLACEHOLDER";
    h.clickEdge = true;
    h.hover = 500;
    LoadGameRunHooks* prev = LoadGame_SetRunHooks(&h);

    LoadGameRunState st;
    LoadGameRunRecord rec;
    int r = Menu_RunLoadGame(st, &rec, 4);

    LoadGame_SetRunHooks(prev);

    CHECK_EQ(r, 0);
    CHECK_EQ(rec.matchedSlot, -1);
}

TEST(LoadGameRunDispatch, CloseEdge_ArmsCloseNoPick) {
    ClickHooks h;
    h.grid = Grid3();
    h.closeEdge = true;        // dword_672230 set
    h.clickEdge = false;
    LoadGameRunHooks* prev = LoadGame_SetRunHooks(&h);

    LoadGameRunState st;
    LoadGameRunRecord rec;
    int r = Menu_RunLoadGame(st, &rec, 4);

    LoadGame_SetRunHooks(prev);

    CHECK_EQ(r, 0);                 // no save chosen
    CHECK_EQ(st.close, 1);          // dword_631614 = 1 (close armed)
    CHECK_EQ(st.sessionFlags, 0);   // word_63C740 untouched
    CHECK_EQ(rec.matchedSlot, -1);
}

TEST(LoadGameRunDispatch, ConfirmGate_DeclinedKeepsScreen) {
    ClickHooks h;
    h.grid = Grid3();
    h.clickEdge = true;
    h.hover = 202;
    h.confirmResult = false;       // user clicks "no" in the confirm box
    h.framesAllowed = 3;
    LoadGameRunHooks* prev = LoadGame_SetRunHooks(&h);

    LoadGameRunState st;
    st.confirmGate = 1;            // byte_63CC40 active
    LoadGameRunRecord rec;
    int r = Menu_RunLoadGame(st, &rec, 4);

    LoadGame_SetRunHooks(prev);

    CHECK_EQ(r, 0);                 // declined -> not chosen
    CHECK(rec.confirmAsked);        // confirm box WAS shown
    CHECK_EQ(rec.matchedSlot, 2);   // it matched the slot, just declined the load
    CHECK_EQ(st.sessionFlags, 0);
    CHECK_EQ(st.close, 0);
    CHECK_EQ(st.loadPath, std::string(""));
}

TEST(LoadGameRunDispatch, ConfirmGate_AcceptedPicks) {
    ClickHooks h;
    h.grid = Grid3();
    h.clickEdge = true;
    h.hover = 200;
    h.confirmResult = true;        // user confirms
    LoadGameRunHooks* prev = LoadGame_SetRunHooks(&h);

    LoadGameRunState st;
    st.confirmGate = 1;            // byte_63CC40 active
    LoadGameRunRecord rec;
    int r = Menu_RunLoadGame(st, &rec, 4);

    LoadGame_SetRunHooks(prev);

    CHECK_EQ(r, 1);
    CHECK(rec.confirmAsked);
    CHECK_EQ(rec.matchedSlot, 0);
    CHECK_EQ(st.sessionFlags, 10);
    CHECK_EQ(st.close, 1);
    CHECK_EQ(st.loadPath, std::string("Gamedata\\Saves\\SAVE0.SAV"));
}

TEST(LoadGameRunDispatch, NoClick_LoopRunsToGuard) {
    ClickHooks h;
    h.grid = Grid3();
    h.framesAllowed = 100;        // RunFrameLoop keeps returning 1
    LoadGameRunHooks* prev = LoadGame_SetRunHooks(&h);

    LoadGameRunState st;
    LoadGameRunRecord rec;
    int r = Menu_RunLoadGame(st, &rec, /*maxFrames*/ 5);

    LoadGame_SetRunHooks(prev);

    CHECK_EQ(r, 0);
    CHECK_EQ(rec.frames, 5);      // stopped at the test guard
}

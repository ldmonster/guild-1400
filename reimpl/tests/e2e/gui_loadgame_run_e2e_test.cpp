// e2e tests for gui::Menu_RunLoadGame @0x56a270 — a full scripted screen session
// asserting the build + dispatch + cleanup call ORDER (recorded via the run hooks),
// deterministic across two runs. A GUARDED real-save-dir variant runs only when a real
// save directory is present (it skips cleanly when absent).
#include "test.h"

#include "gui/loadgame_run.h"

#include <cstdlib>
#include <sys/stat.h>

using namespace guild::gui;

namespace {

// A full-session harness: builds a 16-row grid, then on the scripted frame clicks a real
// occupied slot behind the confirm gate, accepts, and the loop ends.
struct SessionHooks : LoadGameRunHooks {
    std::vector<LoadGameSlot> grid;
    int  clickAtFrame = 1;
    int  hover = -1;
    bool confirm = true;
    int  framesAllowed = 8;

    void LoadSlotMetadata(int, const char*, std::vector<LoadGameSlot>& out) override {
        out = grid;
    }
    int  RunFrameLoop() override { return framesAllowed-- > 0 ? 1 : 0; }
    bool ClickEdge(int f) override { return f == clickAtFrame; }
    int  HoverId(int) override { return hover; }
    bool ConfirmLoad() override { return confirm; }
};

std::vector<LoadGameSlot> Grid(int occupied) {
    std::vector<LoadGameSlot> g(kRunLoadSlotCount);
    for (int i = 0; i < occupied; ++i) {
        g[i].present  = true;
        g[i].objId    = 100 + i;
        g[i].widgetId = 200 + i;
        g[i].name     = "GAME" + std::to_string(i);
    }
    return g;
}

LoadGameRunRecord RunSession(int hover, bool confirm) {
    SessionHooks h;
    h.grid = Grid(4);
    h.hover = hover;
    h.confirm = confirm;
    LoadGameRunHooks* prev = LoadGame_SetRunHooks(&h);
    LoadGameRunRecord rec;
    LoadGameRunState st;
    st.confirmGate = 1;
    Menu_RunLoadGame(st, &rec, /*maxFrames*/ 16);
    LoadGame_SetRunHooks(prev);
    return rec;
}

} // namespace

TEST(LoadGameRunE2E, FullSession_BuildDispatchCleanupOrder) {
    SessionHooks h;
    h.grid = Grid(4);
    h.hover = 202;            // pick slot index 2
    h.confirm = true;
    h.clickAtFrame = 1;
    LoadGameRunHooks* prev = LoadGame_SetRunHooks(&h);

    LoadGameRunState st;
    st.confirmGate = 1;       // confirm box shown
    LoadGameRunRecord rec;
    int r = Menu_RunLoadGame(st, &rec, 16);

    LoadGame_SetRunHooks(prev);

    CHECK_EQ(r, 1);
    CHECK_EQ(rec.matchedSlot, 2);
    CHECK_EQ(st.loadPath, std::string("Gamedata\\Saves\\GAME2.SAV"));
    CHECK_EQ(st.sessionFlags, 10);
    CHECK_EQ(st.close, 1);

    // The full ordered trace: build (5 tags) then on the click frame ConfirmLoad + Pick,
    // then cleanup FormDestroy.
    const char* want[] = {"FormLoad", "FormPosition", "RenderTitle", "BuildSliderPanel",
                          "LoadSlotMetadata", "ConfirmLoad", "Pick", "FormDestroy"};
    CHECK_EQ(rec.traceCount, 8);
    for (int i = 0; i < rec.traceCount; ++i)
        CHECK_EQ(std::string(rec.trace[i]), std::string(want[i]));
}

TEST(LoadGameRunE2E, Deterministic_TwoRunsIdentical) {
    LoadGameRunRecord a = RunSession(/*hover*/ 203, /*confirm*/ true);
    LoadGameRunRecord b = RunSession(/*hover*/ 203, /*confirm*/ true);

    CHECK_EQ(a.matchedSlot, b.matchedSlot);
    CHECK_EQ(a.traceCount, b.traceCount);
    CHECK_EQ(a.frames, b.frames);
    CHECK_EQ(a.slotCount, b.slotCount);
    CHECK_EQ(a.occupiedSlots, b.occupiedSlots);
    for (int i = 0; i < a.traceCount; ++i)
        CHECK_EQ(std::string(a.trace[i]), std::string(b.trace[i]));
}

TEST(LoadGameRunE2E, CancelSession_NoPick) {
    SessionHooks h;
    h.grid = Grid(4);
    h.hover = -1;            // nothing hovered -> no click ever resolves
    h.framesAllowed = 6;
    LoadGameRunHooks* prev = LoadGame_SetRunHooks(&h);

    LoadGameRunState st;
    LoadGameRunRecord rec;
    int r = Menu_RunLoadGame(st, &rec, 6);

    LoadGame_SetRunHooks(prev);

    CHECK_EQ(r, 0);
    CHECK_EQ(rec.matchedSlot, -1);
    CHECK_EQ(st.sessionFlags, 0);
    CHECK_EQ(std::string(rec.trace[rec.traceCount - 1]), std::string("FormDestroy"));
}

// GUARDED: only runs if a real save dir is present (e.g. GUILD_SAVE_DIR). Skips cleanly.
TEST(LoadGameRunE2E, RealSaveDir_Guarded) {
    const char* dir = std::getenv("GUILD_SAVE_DIR");
    struct stat sb;
    if (!dir || stat(dir, &sb) != 0 || !S_ISDIR(sb.st_mode)) {
        // No real save dir available -> skip cleanly (the default hooks would just
        // produce 16 placeholders anyway).
        CHECK(true);
        return;
    }
    // With the inert defaults (no VFS wired into this headless test), the screen yields
    // a placeholder grid; assert the screen still builds + tears down cleanly.
    LoadGameRunState st;
    LoadGameRunRecord rec;
    int r = Menu_RunLoadGame(st, &rec, 0);
    CHECK_EQ(r, 0);
    CHECK_EQ(rec.slotCount, 16);
}

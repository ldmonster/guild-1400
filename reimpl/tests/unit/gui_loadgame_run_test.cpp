// Unit tests for gui::Menu_RunLoadGame @0x56a270 — the EXACT preamble build recorded via
// the run hooks: form load ("menu\\loadgame_new"), title render (6244), slider panel
// (532x360, 130 rows), the enumerated save-slot grid (16 rows), and the build call ORDER.
#include "test.h"

#include "gui/loadgame_run.h"

using namespace guild::gui;

namespace {

// Recording hooks: capture the build calls + drive zero frames (RunFrameLoop default
// returns 0 -> loop exits at once). LoadSlotMetadata yields a scripted slot grid.
struct BuildHooks : LoadGameRunHooks {
    int formHandle = 77;
    int winHandle  = 33;
    int titleSeen  = 0;
    int sliderW = 0, sliderH = 0, sliderWin = 0, sliderRows = 0;
    bool destroyed = false;
    int destroyArg = -1;
    std::vector<LoadGameSlot> grid;  // what LoadSlotMetadata returns

    int  FormLoad(const char*) override { return formHandle; }
    void RenderTitle(int id) override { titleSeen = id; }
    int  GetWindowId(int) override { return winHandle; }
    void BuildSliderPanel(int w, int h, int win, int rows) override {
        sliderW = w; sliderH = h; sliderWin = win; sliderRows = rows;
    }
    void LoadSlotMetadata(int, const char*, std::vector<LoadGameSlot>& out) override {
        out = grid;
    }
    void FormDestroy(int form) override { destroyed = true; destroyArg = form; }
};

// A 16-row grid with `occupied` populated rows in front.
std::vector<LoadGameSlot> MakeGrid(int occupied) {
    std::vector<LoadGameSlot> g(kRunLoadSlotCount);
    for (int i = 0; i < kRunLoadSlotCount; ++i) {
        if (i < occupied) {
            g[i].present  = true;
            g[i].objId    = 100 + i;
            g[i].widgetId = 200 + i;
            g[i].name     = "SAVE" + std::to_string(i);
        }
    }
    return g;
}

} // namespace

TEST(LoadGameRun, FormLoadArgsAndConstants) {
    // Literal arguments recovered from the decompile.
    CHECK_EQ(std::string(kRunLoadGameForm), std::string("menu\\loadgame_new"));
    CHECK_EQ(kRunLoadGameTitle, 6244);          // 0x1864
    CHECK_EQ(std::string(kRunLoadSaveDir), std::string("gamedata/saves"));
    CHECK_EQ(kRunLoadPanelW, 532);
    CHECK_EQ(kRunLoadPanelH, 360);
    CHECK_EQ(kRunLoadPanelRows, 130);
    CHECK_EQ(kRunLoadSlotStride, 544);
    CHECK_EQ(kRunLoadSlotTableLen, 8704);
    CHECK_EQ(kRunLoadSlotCount, 16);
    CHECK_EQ(kRunLoadOffWidgetId, 4);
    CHECK_EQ(kRunLoadOffObjId, 8);
    CHECK_EQ(kRunLoadOffPresent, 12);
    CHECK_EQ(kRunLoadOffName, 25);
    CHECK_EQ(kRunSessLoad, 10);
    CHECK_EQ(kRunLoadConfirmBox, 257);
}

TEST(LoadGameRun, ExactPreambleBuild) {
    BuildHooks hooks;
    hooks.grid = MakeGrid(/*occupied*/ 3);
    LoadGameRunHooks* prev = LoadGame_SetRunHooks(&hooks);

    LoadGameRunState st;
    LoadGameRunRecord rec;
    int r = Menu_RunLoadGame(st, &rec, /*maxFrames*/ 0);

    LoadGame_SetRunHooks(prev);

    // No frame ran (RunFrameLoop default 0) -> nothing chosen.
    CHECK_EQ(r, 0);
    CHECK_EQ(rec.frames, 0);

    // Build captured exactly.
    CHECK_EQ(rec.form, 77);
    CHECK_EQ(rec.listWindow, 33);
    CHECK_EQ(rec.titleId, 6244);
    CHECK(rec.sliderBuilt);
    CHECK_EQ(rec.sliderW, 532);
    CHECK_EQ(rec.sliderH, 360);
    CHECK_EQ(rec.sliderRows, 130);
    CHECK_EQ(hooks.sliderWin, 33);     // win passed through to BuildSliderPanel

    // The slot grid: 16 rows, 3 occupied.
    CHECK_EQ(rec.slotCount, 16);
    CHECK_EQ(rec.occupiedSlots, 3);

    // Cleanup ran with the form handle.
    CHECK(hooks.destroyed);
    CHECK_EQ(hooks.destroyArg, 77);
}

TEST(LoadGameRun, BuildCallOrder) {
    BuildHooks hooks;
    hooks.grid = MakeGrid(1);
    LoadGameRunHooks* prev = LoadGame_SetRunHooks(&hooks);

    LoadGameRunState st;
    LoadGameRunRecord rec;
    Menu_RunLoadGame(st, &rec, 0);

    LoadGame_SetRunHooks(prev);

    // The exact preamble + cleanup order (no frames, so no dispatch tags).
    const char* want[] = {"FormLoad", "FormPosition", "RenderTitle",
                          "BuildSliderPanel", "LoadSlotMetadata", "FormDestroy"};
    CHECK_EQ(rec.traceCount, 6);
    for (int i = 0; i < rec.traceCount; ++i)
        CHECK_EQ(std::string(rec.trace[i]), std::string(want[i]));
}

TEST(LoadGameRun, DefaultHooksYield16Placeholders) {
    // Inert defaults reuse the real enumeration/slot-build models over an empty file
    // listing -> 16 placeholder slots, none occupied (the "no .SAV files" outcome).
    LoadGameRunState st;
    LoadGameRunRecord rec;
    int r = Menu_RunLoadGame(st, &rec, 0);

    CHECK_EQ(r, 0);
    CHECK_EQ(rec.slotCount, 16);
    CHECK_EQ(rec.occupiedSlots, 0);
    CHECK_EQ(rec.titleId, 6244);
}

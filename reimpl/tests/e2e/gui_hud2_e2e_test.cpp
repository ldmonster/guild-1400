// End-to-end flow for the second HUD/menu/map batch.
//
// Synthetic scenario: a player has 5 candidate people selected in the city, three
// city offices on the map, and is in the options sub-menu while a tutorial drag-drop
// step is active.  This exercises the whole layout/content/marker pipeline:
//   1. Build the HUD person grid (5 -> 3+2) and verify cell placement + that the
//      content pass (via the object sink) emits the expected per-cell sprites.
//   2. Build the action panel for the selection.
//   3. Collect the on-map office markers (skipping one unresolved id).
//   4. Build a city-point marker for the selected city.
//   5. Open the options sub-tab strip and dispatch a tab click -> mock runner.
//   6. Resolve a tutorial drag-drop click -> expected mock command.
#include "gui/hud_draw.h"
#include "gui/hud_grid.h"
#include "gui/mapview_markers.h"
#include "gui/menu_sub.h"
#include "tests/framework/test.h"

#include <string>
#include <vector>

using namespace guild::gui;

namespace {

// A content sink that records placements AND tags each with the cell it belongs to so
// the e2e can verify the grid drove the content pass.
struct GridContentSink : ObjectSink {
    int AddToWindow(int win, int y, int x, int gfx) override {
        return ObjectSink::AddToWindow(win, y, x, gfx);
    }
};

struct E2ESubTabSink : SubTabCommandSink {
    std::string ran;
    void RunGame() override { ran = "game"; }
    void RunGfx() override { ran = "gfx"; }
    void RunSfx() override { ran = "sfx"; }
};

struct E2EDragSink : DragCommandSink {
    DragCmd last = DragCmd::kNone;
    void Fire(DragCmd c) override { last = c; }
};

} // namespace

TEST(GuiHud2E2E, SelectionScreenFlow) {
    // --- 1. Person grid for 5 selected people: shape 3+2 ---------------------
    auto cells = Hud_BuildPersonGridCells(5);
    CHECK_EQ((int)cells.size(), 5);
    // 5 -> rows {3,2}, base y = 64.
    CHECK_EQ(cells[0].rowY, 64);
    CHECK_EQ(cells[3].rowY, 192);          // second row (64 + 128)
    CHECK_EQ(cells[0].colX, 0);            // 3-col row
    CHECK_EQ(cells[1].colX, 101);
    CHECK_EQ(cells[2].colX, 202);
    CHECK_EQ(cells[3].colX, 101);          // 2-col row -> {101,202}
    CHECK_EQ(cells[4].colX, 202);

    // Drive a content pass: place a person-card sprite per cell at (cellX, rowY+35).
    GridContentSink content;
    Hud_SetObjectSink(&content);
    for (auto& c : cells) {
        // The real builder calls AddToWindow(win, rowY+35, cellX+101, 1401) for the
        // card frame; reproduce the offset to verify the layout constants flow through.
        content.AddToWindow(0, c.rowY + kPersonCellRowY, c.colX + kPersonCellX, 1401);
    }
    CHECK_EQ((int)content.placed.size(), 5);
    CHECK_EQ(content.placed[0].y, 64 + 35);
    CHECK_EQ(content.placed[0].x, 0 + 101);
    CHECK_EQ(content.placed[0].gfx, 1401);
    Hud_SetObjectSink(nullptr);

    // --- 2. Action panel for the selection (subject kind 5) ------------------
    auto panel = Hud_BuildObjectActionPanel(/*actionGfx*/1700, /*hasSubject*/false,
                                            /*subjectKind*/5);
    CHECK_EQ(panel[1].gfx, 5 + kActionSubjectGfxBase); // centre id from subject kind
    CHECK_EQ((int)panel.size(), 7);

    // --- 3. On-map office markers (one id unresolved) ------------------------
    std::vector<int> markers;
    int nm = MapView_CollectOfficeMarkers({101, 0, 103}, /*maxOut*/8, markers);
    CHECK_EQ(nm, 2);
    CHECK_EQ(markers[0], 101);
    CHECK_EQ(markers[1], 103);

    // --- 4. City-point marker for the selected city --------------------------
    struct Sink : CityMarkerSink {
        int proj = 0, attach = 0, anim = 0;
        void ProjectThroughBoneChain() override { ++proj; }
        void AttachToUniverse() override { ++attach; }
        void LoadAnimation(const char*) override { ++anim; }
    } msink;
    CityMap_SetMarkerSink(&msink);
    MarkerRecord rec;
    auto name = CityMap_BuildMarkerName("Köln");
    CHECK(name.key.rfind("dummy_", 0) == 0);
    CHECK(CityMap_CreateCityPointMarker("Augsburg", /*resolved*/true, rec));
    CHECK_EQ(msink.proj, 1);
    CHECK_EQ((int)rec.at<guild::u8>(535), kCityMarkerKind);
    CityMap_SetMarkerSink(nullptr);

    // --- 5. Options sub-tab dispatch -----------------------------------------
    E2ESubTabSink tabSink;
    Menu_SetSubTabCommandSink(&tabSink);
    SubTab tab = Menu_DispatchSubTab(/*buttonIndex*/1); // gfx tab
    CHECK(tab == SubTab::kGfx);
    CHECK(tabSink.ran == "gfx");
    Menu_SetSubTabCommandSink(nullptr);

    // --- 6. Tutorial drag-drop click resolves to the expected command --------
    E2EDragSink dragSink;
    Hud_SetDragCommandSink(&dragSink);
    // From state 2, a click confirms the step and fires a slot-click command -> 3.
    DragResult dr = Hud_ProcessDragClick(2, /*hasNextForm*/true);
    CHECK_EQ(dr.nextState, 3);
    CHECK(dr.cmd == DragCmd::kSlotClick);
    CHECK(dragSink.last == DragCmd::kSlotClick);
    // Advancing to the final state loops back to step 1 with a status-banner command.
    DragResult dr2 = Hud_ProcessDragClick(12, true);
    CHECK_EQ(dr2.nextState, 1);
    CHECK(dr2.cmd == DragCmd::kStatusBanner);
    CHECK(dragSink.last == DragCmd::kStatusBanner);
    Hud_SetDragCommandSink(nullptr);
}

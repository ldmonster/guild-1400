// Unit tests for the second HUD/menu/map module batch:
//   - HUD scroll-arrow placement table (edge offsets + gfx pairs + scroll-state gating)
//   - HUD object-action button panel layout
//   - HUD drag-drop / tutorial click state machine (transitions + mock command)
//   - selected-unit caption text-id selection table
//   - HUD tiled-row / scaled-tiled-bar tiling math (1/21 scale, 0.01 tolerance)
//   - person grid (1..8) cell placement + person column slot tables
//   - building choice/price/training row placement
//   - map office-marker collection + city-point marker name prep + render flags
//   - options sub-tab select dispatch
#include "gui/hud_draw.h"
#include "gui/hud_grid.h"
#include "gui/mapview_markers.h"
#include "gui/menu_sub.h"
#include "tests/framework/test.h"

#include <cmath>
#include <cstring>

using namespace guild::gui;

// ===========================================================================
// Scroll arrows.
// ===========================================================================
TEST(GuiHud2Arrows, BothScrollStatesAllFour) {
    ScreenEdges e{800 /*bottomY*/, 1024 /*rightX*/, 300 /*centerY*/, 1000 /*rightX2*/};
    auto a = Hud_ScrollArrowPlacements(e, 0, 0, false, false, false, false);
    CHECK_EQ((int)a.size(), 4);
    // left arrow at (rightX-16, centerY-32) = (1008, 268), moving gfx 0.
    CHECK_EQ(a[0].x, 1008);
    CHECK_EQ(a[0].y, 268);
    CHECK_EQ(a[0].gfx, kArrowLeftMoveGfx);
    CHECK(a[0].moving);
    // down arrow at (4, 800), gfx 2.
    CHECK_EQ(a[1].x, 4);
    CHECK_EQ(a[1].y, 800);
    CHECK_EQ(a[1].gfx, kArrowDownMoveGfx);
    // right arrow at (rightX2-150, 800) = (850, 800), gfx 4.
    CHECK_EQ(a[2].x, 850);
    CHECK_EQ(a[2].y, 800);
    CHECK_EQ(a[2].gfx, kArrowRightMoveGfx);
    // up arrow at (rightX-16, 56) = (1008, 56), gfx 6.
    CHECK_EQ(a[3].x, 1008);
    CHECK_EQ(a[3].y, kArrowUpY);
    CHECK_EQ(a[3].gfx, kArrowUpMoveGfx);
}

TEST(GuiHud2Arrows, VerticalScrollSelectsUpOnly) {
    ScreenEdges e{800, 1024, 300, 1000};
    // vScroll == -1 -> up only ; hScroll == +1 -> right only.
    auto a = Hud_ScrollArrowPlacements(e, -1, 1, false, false, false, false);
    CHECK_EQ((int)a.size(), 2);
    CHECK(a[0].arrow == Arrow::kRight);
    CHECK(a[1].arrow == Arrow::kUp);
}

TEST(GuiHud2Arrows, SettledShowsIdleSprite) {
    ScreenEdges e{800, 1024, 300, 1000};
    // down settled -> idle gfx 3 (not moving).
    auto a = Hud_ScrollArrowPlacements(e, 1, -1, false, true, false, false);
    // vScroll +1 -> down ; hScroll -1 -> left.
    CHECK_EQ((int)a.size(), 2);
    bool sawDownIdle = false;
    for (auto& p : a)
        if (p.arrow == Arrow::kDown) {
            CHECK_EQ(p.gfx, kArrowDownIdleGfx);
            CHECK(!p.moving);
            sawDownIdle = true;
        }
    CHECK(sawDownIdle);
}

// ===========================================================================
// Object-action panel.
// ===========================================================================
TEST(GuiHud2ActionPanel, StandardBlock) {
    auto b = Hud_BuildObjectActionPanel(/*actionGfx*/2000, /*hasSubject*/false,
                                        /*subjectKind*/0);
    // frame(9,599,1221) ; centre(9,11,1221) ; right(9,599,1039) ; then 4 buttons.
    CHECK_EQ((int)b.size(), 7);
    CHECK_EQ(b[0].y, 9);   CHECK_EQ(b[0].x, 599); CHECK_EQ(b[0].gfx, kActionFrameGfx);
    CHECK_EQ(b[1].x, 11);  CHECK_EQ(b[1].gfx, kActionFrameGfx);
    CHECK_EQ(b[2].gfx, kActionDefaultRightGfx);
    // standard block: (0,x,a2),(400,x,a2+1),(77,x,a2+2),(77,583,a2+3)
    CHECK_EQ(b[3].y, 0);   CHECK_EQ(b[3].gfx, 2000);
    CHECK_EQ(b[4].y, 400); CHECK_EQ(b[4].gfx, 2001);
    CHECK_EQ(b[5].y, 77);  CHECK_EQ(b[5].gfx, 2002);
    CHECK_EQ(b[6].y, 77);  CHECK_EQ(b[6].x, 583); CHECK_EQ(b[6].gfx, 2003);
}

TEST(GuiHud2ActionPanel, InteractionRowInsertsExtraFrame) {
    auto b = Hud_BuildObjectActionPanel(kActionRowInteraction, false, 0);
    // extra frame at x=491 + the 1709-row block.
    bool sawExtra = false, sawRow = false;
    for (auto& e : b) {
        if (e.x == 491 && e.gfx == kActionFrameGfx) sawExtra = true;
        if (e.y == 0 && e.gfx == kActionRowInteraction) sawRow = true;
    }
    CHECK(sawExtra);
    CHECK(sawRow);
}

TEST(GuiHud2ActionPanel, SubjectKindCentre) {
    auto b = Hud_BuildObjectActionPanel(2000, false, 5);
    CHECK_EQ(b[1].gfx, 5 + kActionSubjectGfxBase); // centre = kind+1010
    CHECK_EQ(b[2].gfx, 5 + kActionSubjectGfxBase); // right  = kind+1010
}

// ===========================================================================
// Drag-drop state machine.
// ===========================================================================
namespace {
struct RecordingDragSink : DragCommandSink {
    DragCmd last = DragCmd::kNone;
    int count = 0;
    void Fire(DragCmd c) override { last = c; ++count; }
};
} // namespace

TEST(GuiHud2Drag, TransitionChain) {
    RecordingDragSink sink;
    Hud_SetDragCommandSink(&sink);

    CHECK_EQ(Hud_ProcessDragClick(1, true).nextState, 2);
    CHECK(sink.last == DragCmd::kShowStep);
    CHECK_EQ(Hud_ProcessDragClick(2, true).nextState, 3);
    CHECK(sink.last == DragCmd::kSlotClick);
    CHECK_EQ(Hud_ProcessDragClick(3, true).nextState, 4);
    CHECK_EQ(Hud_ProcessDragClick(5, true).nextState, 6);  // hasNextForm
    CHECK_EQ(Hud_ProcessDragClick(5, false).nextState, 7); // no next form
    CHECK_EQ(Hud_ProcessDragClick(7, true).nextState, 9);
    CHECK_EQ(Hud_ProcessDragClick(7, false).nextState, 10);
    CHECK_EQ(Hud_ProcessDragClick(10, true).nextState, 11);
    CHECK(sink.last == DragCmd::kStatusBanner);
    CHECK_EQ(Hud_ProcessDragClick(12, true).nextState, 1); // loops
    Hud_SetDragCommandSink(nullptr);
}

TEST(GuiHud2Drag, UnknownStateIsNoOp) {
    RecordingDragSink sink;
    Hud_SetDragCommandSink(&sink);
    auto r = Hud_ProcessDragClick(99, true);
    CHECK_EQ(r.nextState, 99);
    CHECK(r.cmd == DragCmd::kNone);
    Hud_SetDragCommandSink(nullptr);
}

// ===========================================================================
// Selected-unit caption table.
// ===========================================================================
TEST(GuiHud2Caption, KnownCategories) {
    CHECK_EQ(Hud_SelectedUnitCaptionBase(20), 3768);
    CHECK_EQ(Hud_SelectedUnitCaptionBase(24), 3780);
    CHECK_EQ(Hud_SelectedUnitCaptionBase(25), 3774);
    CHECK_EQ(Hud_SelectedUnitCaptionBase(26), 3801);
    CHECK_EQ(Hud_SelectedUnitCaptionBase(40), 3759);
    CHECK_EQ(Hud_SelectedUnitCaptionBase(60), 3765);
    CHECK_EQ(Hud_SelectedUnitCaptionBase(63), 3762);
    CHECK_EQ(Hud_SelectedUnitCaptionBase(64), 3783);
    CHECK_EQ(Hud_SelectedUnitCaptionBase(67), 3786);
    CHECK_EQ(Hud_SelectedUnitCaptionBase(68), 3771);
    CHECK_EQ(Hud_SelectedUnitCaptionBase(72), 3792);
    CHECK_EQ(Hud_SelectedUnitCaptionBase(73), 3756);
    CHECK_EQ(Hud_SelectedUnitCaptionBase(97), 3789);
    CHECK_EQ(Hud_SelectedUnitCaptionBase(98), 3777);
    CHECK_EQ(Hud_SelectedUnitCaptionBase(100), 3795);
    CHECK_EQ(Hud_SelectedUnitCaptionBase(101), 3798);
    CHECK_EQ(Hud_SelectedUnitCaptionBase(117), 3762);
}

TEST(GuiHud2Caption, UnknownCategoriesReturnMinusOne) {
    CHECK_EQ(Hud_SelectedUnitCaptionBase(0), -1);
    CHECK_EQ(Hud_SelectedUnitCaptionBase(61), -1);
    CHECK_EQ(Hud_SelectedUnitCaptionBase(62), -1);
    CHECK_EQ(Hud_SelectedUnitCaptionBase(99), -1);
    CHECK_EQ(Hud_SelectedUnitCaptionBase(200), -1);
}

// ===========================================================================
// Tiled row / scaled tiled bar.
// ===========================================================================
TEST(GuiHud2Tiles, TiledRowCountAndScale) {
    ObjectSink sink;
    Hud_SetObjectSink(&sink);
    // value 100 * (1/21) = 4.76 -> (int)4 ; full = 4/2 = 2 ; half = 4%2 = 0.
    int full = Hud_BuildTiledRow(100, 10, 0, 5, 700);
    CHECK_EQ(full, 2);
    CHECK_EQ((int)sink.placed.size(), 2);
    CHECK_EQ(sink.placed[0].x, 0);
    CHECK_EQ(sink.placed[1].x, 10); // step 10
    CHECK_EQ(sink.placed[0].gfx, 700);
    Hud_SetObjectSink(nullptr);
}

TEST(GuiHud2Tiles, TiledRowOddHalf) {
    ObjectSink sink;
    Hud_SetObjectSink(&sink);
    // 63 * (1/21) = 3.0 -> 3 ; full=1, half=1 -> 1 full tile + 1 half (gfx+1).
    int full = Hud_BuildTiledRow(63, 8, 0, 0, 500);
    CHECK_EQ(full, 1);
    CHECK_EQ((int)sink.placed.size(), 2);
    CHECK_EQ(sink.placed[1].gfx, 501); // half tile baseGfx+1
    Hud_SetObjectSink(nullptr);
}

TEST(GuiHud2Tiles, ScaledBarEqualWithinTolerance) {
    ObjectSink sink;
    Hud_SetObjectSink(&sink);
    // cur==base==42 -> |0|/42 = 0 <= 0.01 -> single equal run of `base` tiles.
    int r = Hud_BuildScaledTiledBar(42, 42, 6, 0, 0, 800);
    // 42*(1/21)=2.0 -> 2 ; full=1, half=0 -> 1 tile of gfx 800.
    CHECK_EQ(r, 1);
    CHECK_EQ((int)sink.placed.size(), 1);
    CHECK_EQ(sink.placed[0].gfx, 800);
    Hud_SetObjectSink(nullptr);
}

TEST(GuiHud2Tiles, ScaledBarUpAndDownColors) {
    {
        ObjectSink sink;
        Hud_SetObjectSink(&sink);
        // cur(126) > base(42): up run uses gfx+4, then base run uses gfx.
        Hud_BuildScaledTiledBar(126, 42, 6, 0, 0, 800);
        bool sawUp = false, sawBase = false;
        for (auto& p : sink.placed) {
            if (p.gfx == 804) sawUp = true;
            if (p.gfx == 800) sawBase = true;
        }
        CHECK(sawUp);
        CHECK(sawBase);
        Hud_SetObjectSink(nullptr);
    }
    {
        ObjectSink sink;
        Hud_SetObjectSink(&sink);
        // cur(42) < base(126): down run uses gfx+2, then cur run uses gfx.
        Hud_BuildScaledTiledBar(42, 126, 6, 0, 0, 800);
        bool sawDown = false;
        for (auto& p : sink.placed)
            if (p.gfx == 802) sawDown = true;
        CHECK(sawDown);
        Hud_SetObjectSink(nullptr);
    }
}

// ===========================================================================
// Person grid.
// ===========================================================================
TEST(GuiHud2Grid, GridShapesPerCount) {
    // count 1 -> 1 cell at colX 202, rowY 128.
    auto g1 = Hud_BuildPersonGridCells(1);
    CHECK_EQ((int)g1.size(), 1);
    CHECK_EQ(g1[0].colX, 202);
    CHECK_EQ(g1[0].rowY, 128);

    // count 2 -> cols {101,202} on row at y=128.
    auto g2 = Hud_BuildPersonGridCells(2);
    CHECK_EQ((int)g2.size(), 2);
    CHECK_EQ(g2[0].colX, 101);
    CHECK_EQ(g2[1].colX, 202);
    CHECK_EQ(g2[0].rowY, 128);

    // count 3 -> cols {0,101,202}.
    auto g3 = Hud_BuildPersonGridCells(3);
    CHECK_EQ((int)g3.size(), 3);
    CHECK_EQ(g3[0].colX, 0);
    CHECK_EQ(g3[1].colX, 101);
    CHECK_EQ(g3[2].colX, 202);

    // count 4 -> 2 rows of 2 ; row base y = 64, second row 64+128=192.
    auto g4 = Hud_BuildPersonGridCells(4);
    CHECK_EQ((int)g4.size(), 4);
    CHECK_EQ(g4[0].rowY, 64);
    CHECK_EQ(g4[2].rowY, 192);
    CHECK_EQ(g4[0].colX, 101); // 2-col row -> {101,202}
    CHECK_EQ(g4[1].colX, 202);
}

TEST(GuiHud2Grid, EightPersonThreeRows) {
    auto g = Hud_BuildPersonGridCells(8); // rows 3+2+3, base y = 0
    CHECK_EQ((int)g.size(), 8);
    CHECK_EQ(g[0].rowY, 0);
    CHECK_EQ(g[3].rowY, 128); // start of 2-col row
    CHECK_EQ(g[5].rowY, 256); // start of 3-col row
    // row1 (3 cols): 0,101,202 ; row2 (2 cols): 101,202 ; row3 (3 cols): 0,101,202
    CHECK_EQ(g[3].colX, 101);
    CHECK_EQ(g[5].colX, 0);
}

TEST(GuiHud2Grid, MaxCellsCap) {
    auto g = Hud_BuildPersonGridCells(8, /*maxCells*/3);
    CHECK_EQ((int)g.size(), 3);
}

TEST(GuiHud2Grid, GridOutOfRangeEmpty) {
    CHECK_EQ((int)Hud_BuildPersonGridCells(0).size(), 0);
    CHECK_EQ((int)Hud_BuildPersonGridCells(9).size(), 0);
}

// ===========================================================================
// Person column slots.
// ===========================================================================
TEST(GuiHud2Column, Mode123FourSlots) {
    auto c = Hud_BuildPersonColumnSlots(2);
    CHECK_EQ((int)c.size(), 4);
    CHECK_EQ(c[0].x, 202); CHECK_EQ(c[0].y, 0);
    CHECK_EQ(c[1].x, 202); CHECK_EQ(c[1].y, 128);
    CHECK_EQ(c[2].x, 101); CHECK_EQ(c[2].y, 256);
    CHECK_EQ(c[3].x, 303); CHECK_EQ(c[3].y, 256);
}

TEST(GuiHud2Column, DefaultSixSlots) {
    auto c = Hud_BuildPersonColumnSlots(5);
    CHECK_EQ((int)c.size(), 6);
    CHECK_EQ(c[0].x, 202); CHECK_EQ(c[0].y, 0);
    CHECK_EQ(c[5].x, 404); CHECK_EQ(c[5].y, 256);
}

TEST(GuiHud2Column, Mode7Empty) {
    CHECK_EQ((int)Hud_BuildPersonColumnSlots(7).size(), 0);
}

// ===========================================================================
// Building choice / price / training rows.
// ===========================================================================
TEST(GuiHud2Rows, ChoiceRows) {
    auto r = Hud_BuildBuildingChoiceRows(5, /*capacity*/3, /*windowW*/600, /*childW*/400);
    CHECK_EQ((int)r.size(), 3); // clamped to capacity
    CHECK_EQ(r[0].y, 0);
    CHECK_EQ(r[1].y, 128);
    CHECK_EQ(r[2].y, 256);
    CHECK_EQ(r[0].x, (600 - 400) / 2); // centred = 100
}

TEST(GuiHud2Rows, PriceRows) {
    auto r = Hud_BuildBuildingPriceRows({10, 20, 30});
    CHECK_EQ((int)r.size(), 3);
    CHECK_EQ(r[0].gfxX, 32);
    CHECK_EQ(r[0].gfxY, 0);
    CHECK_EQ(r[1].gfxY, 64);
    CHECK_EQ(r[2].gfxY, 128);
    CHECK_EQ(r[0].worthLabelY, 27);   // (0<<6)+27
    CHECK_EQ(r[1].worthLabelY, 64 + 27);
    CHECK_EQ(r[0].roomGfx, 10 + 1010);
}

TEST(GuiHud2Rows, TrainingRow) {
    auto r = Hud_BuildTrainingRow(2, /*gfx*/1500);
    CHECK_EQ(r.backdropX, 0);
    CHECK_EQ(r.backdropY, 240);  // 120*2
    CHECK_EQ(r.traineeX, 13);
    CHECK_EQ(r.traineeY, 245);   // 240+5
    CHECK_EQ(r.childX, 64);
    CHECK_EQ(r.childY, 240);
}

// ===========================================================================
// Map office-marker collection.
// ===========================================================================
TEST(GuiHud2Markers, CollectSkipsUnresolved) {
    std::vector<int> out;
    // ids resolving to {7, 0, 9, 0, 11} -> stored {7,9,11}, count 3.
    int n = MapView_CollectOfficeMarkers({7, 0, 9, 0, 11}, /*maxOut*/10, out);
    CHECK_EQ(n, 3);
    CHECK_EQ((int)out.size(), 3);
    CHECK_EQ(out[0], 7);
    CHECK_EQ(out[1], 9);
    CHECK_EQ(out[2], 11);
}

TEST(GuiHud2Markers, CollectCapAtMaxOut) {
    std::vector<int> out;
    int n = MapView_CollectOfficeMarkers({1, 2, 3, 4, 5}, /*maxOut*/2, out);
    CHECK_EQ(n, 2);
    CHECK_EQ((int)out.size(), 2);
}

// ===========================================================================
// City-point marker.
// ===========================================================================
TEST(GuiHud2CityMarker, NamePrep) {
    auto nm = CityMap_BuildMarkerName("London");
    CHECK(nm.upper == "LONDON");
    CHECK(nm.key == "dummy_LONDON");
}

TEST(GuiHud2CityMarker, RenderFlags) {
    MarkerRecord rec;
    rec.at<guild::u8>(529) = 0x01;
    rec.at<guild::u8>(530) = 0xFF;
    CityMap_ApplyMarkerFlags(rec);
    CHECK_EQ((int)rec.at<guild::u8>(535), kCityMarkerKind);          // 4
    CHECK_EQ((int)rec.at<guild::i32>(536), kCityMarkerEnabled);      // 1
    CHECK_EQ((int)rec.at<guild::u8>(529), 0x01 | 0x04);              // |=4
    CHECK_EQ((int)rec.at<guild::u8>(530), (0xFF & 0xF3) | 0x04);     // clear+set
}

namespace {
struct CountingMarkerSink : CityMarkerSink {
    int proj = 0, attach = 0, anim = 0;
    void ProjectThroughBoneChain() override { ++proj; }
    void AttachToUniverse() override { ++attach; }
    void LoadAnimation(const char*) override { ++anim; }
};
} // namespace

TEST(GuiHud2CityMarker, BuildWhenResolved) {
    CountingMarkerSink sink;
    CityMap_SetMarkerSink(&sink);
    MarkerRecord rec;
    CHECK(CityMap_CreateCityPointMarker("Paris", true, rec));
    CHECK_EQ(sink.proj, 1);
    CHECK_EQ(sink.attach, 1);
    CHECK_EQ(sink.anim, 1);
    CHECK_EQ((int)rec.at<guild::u8>(535), kCityMarkerKind);
    CityMap_SetMarkerSink(nullptr);
}

TEST(GuiHud2CityMarker, EarlyReturnWhenUnresolved) {
    CountingMarkerSink sink;
    CityMap_SetMarkerSink(&sink);
    MarkerRecord rec;
    CHECK(!CityMap_CreateCityPointMarker("Paris", false, rec));
    CHECK_EQ(sink.proj, 0);
    CHECK_EQ((int)rec.at<guild::u8>(535), 0); // untouched
    CityMap_SetMarkerSink(nullptr);
}

// ===========================================================================
// Options sub-tabs.
// ===========================================================================
TEST(GuiHud2SubTabs, ButtonColumn) {
    CHECK_EQ(kSubTabButtonY[0], 56);
    CHECK_EQ(kSubTabButtonY[1], 102);
    CHECK_EQ(kSubTabButtonY[2], 148);
    CHECK(Menu_SubTabSelect(0) == SubTab::kGame);
    CHECK(Menu_SubTabSelect(1) == SubTab::kGfx);
    CHECK(Menu_SubTabSelect(2) == SubTab::kSfx);
}

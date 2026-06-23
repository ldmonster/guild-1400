// ui_recon4_hud_surface_test.cpp — golden vectors for ui_recon4 pure UI logic.
#include "test.h"
#include "play/ui_recon4_hud_surface.h"

using namespace guild;
using namespace guild::play;

// Helper: pack a sprite-dims cell so (>>16) yields `width` (signed, low word 0).
static i32 PackDims(i16 width, u16 lo = 0) {
    return (static_cast<i32>(width) << 16) | lo;
}

// --- 0x553f30 full person-card centering -----------------------------------
TEST(UiRecon4, FullCardCenterX) {
    UiTheme th{}; th.cardSpriteDims = PackDims(120); th.noRecordSpriteDims = PackDims(40);
    PortraitSprite ps{}; ps.dims = PackDims(48);
    PersonCardRec rec{}; rec.kind = 1;
    auto L = BuildPersonCardLayout(th, ps, &rec, /*baseX*/100, /*baseY*/50, /*withLabels*/true);
    CHECK(L.hasRecord);
    CHECK_EQ(L.cardCenterX, 120 / 2 + 100);          // 160
    CHECK_EQ(L.nameLabelX, 160);
    CHECK_EQ(L.nameLabelY, 202);
    CHECK_EQ((int)L.portraitX, 160 - 48 / 2 - 3);    // 133
    CHECK_EQ((int)L.portraitY, 50 + 28);             // 78
}

TEST(UiRecon4, FullCardNoRecordFallback) {
    UiTheme th{}; th.cardSpriteDims = PackDims(120); th.noRecordSpriteDims = PackDims(40);
    PortraitSprite ps{};
    auto L = BuildPersonCardLayout(th, ps, nullptr, 100, 50, true);
    CHECK(!L.hasRecord);
    CHECK_EQ((int)L.noRecPortraitX, 160 - 40 / 2 - 3); // 137
}

TEST(UiRecon4, FullCardSliderGating) {
    UiTheme th{}; th.cardSpriteDims = PackDims(120);
    PortraitSprite ps{}; ps.dims = PackDims(0);
    PersonCardRec rec{};
    rec.kind = 6; // == 6 -> no slider
    auto L6 = BuildPersonCardLayout(th, ps, &rec, 0, 0, false);
    CHECK(!L6.hasSlider);
    CHECK(L6.hasIconRow); // 6 is in {5,6,7}
    rec.kind = 7;
    auto L7 = BuildPersonCardLayout(th, ps, &rec, 0, 0, false);
    CHECK(L7.hasSlider);
    CHECK(L7.sliderIsFull);  // kind 7 -> full bar
    CHECK(L7.hasIconRow);
    rec.kind = 3;
    auto L3 = BuildPersonCardLayout(th, ps, &rec, 0, 0, false);
    CHECK(L3.hasSlider);
    CHECK(!L3.sliderIsFull);
    CHECK(!L3.hasIconRow);
}

TEST(UiRecon4, FullCardSliderX) {
    UiTheme th{}; th.cardSpriteDims = PackDims(200);
    PortraitSprite ps{}; PersonCardRec rec{}; rec.kind = 1;
    auto L = BuildPersonCardLayout(th, ps, &rec, 10, 0, true);
    CHECK_EQ(L.cardCenterX, 200 / 2 + 10);     // 110
    CHECK_EQ((int)L.sliderX, 110 - 35);        // 75
}

// --- 0x55433c simple person-card -------------------------------------------
TEST(UiRecon4, SimpleCardCentering) {
    UiTheme th{}; th.cardSpriteDims = PackDims(120); th.noRecordSpriteDims = PackDims(40);
    PortraitSprite ps{}; ps.dims = PackDims(48);
    PersonCardRec rec{}; rec.kind = 2;
    auto L = BuildPersonCardSimpleLayout(th, ps, &rec, 100, 50);
    CHECK(L.hasRecord);
    CHECK_EQ(L.cardCenterX, 120 / 2 + 100);            // 160
    CHECK_EQ((int)L.portraitX, 160 - 48 / 2 - 3);      // 133
    CHECK_EQ((int)L.portraitY, 50);                    // simple: slot at baseY
    CHECK(L.hasSlider);
}

TEST(UiRecon4, SimpleCardNoRecord) {
    UiTheme th{}; th.cardSpriteDims = PackDims(80); th.noRecordSpriteDims = PackDims(60);
    PortraitSprite ps{};
    auto L = BuildPersonCardSimpleLayout(th, ps, nullptr, 0, 0);
    CHECK(!L.hasRecord);
    CHECK_EQ((int)L.noRecPortraitX, 80 / 2 - 60 / 2 - 3); // 40-30-3 = 7
}

// --- name-label text id selection ------------------------------------------
TEST(UiRecon4, NameTextIdSelection) {
    PersonCardRec rec{};
    rec.labelCount = 0;
    CHECK_EQ(PersonCardNameTextId(rec, true), -1);   // no labels -> plain path
    rec.labelCount = 5;
    CHECK_EQ(PersonCardNameTextId(rec, false), -1);  // withLabels false -> plain
    rec.genderFlag = 0; rec.labelCount = 5;
    CHECK_EQ(PersonCardNameTextId(rec, true), 272 + 5);
    rec.genderFlag = 1;
    CHECK_EQ(PersonCardNameTextId(rec, true), 279 + 5);
}

// --- 0x4243d8 surface text colour packing ----------------------------------
TEST(UiRecon4, SurfaceTextColorRef) {
    // Binary (0x4243d8): color = a3 | (a4<<8) | (a5<<16); a3=R(bit0), a4=G(bit8),
    // a5=B(bit16). COLORREF == 0x00BBGGRR.
    CHECK_EQ(SurfaceTextColorRef(0xFF, 0x00, 0x00), 0x000000FFu); // pure red  (a3)
    CHECK_EQ(SurfaceTextColorRef(0x00, 0xFF, 0x00), 0x0000FF00u); // pure green(a4)
    CHECK_EQ(SurfaceTextColorRef(0x00, 0x00, 0xFF), 0x00FF0000u); // pure blue (a5)
    CHECK_EQ(SurfaceTextColorRef(0x34, 0x12, 0x56), 0x00561234u); // a3=34,a4=12,a5=56
    CHECK_EQ(SurfaceColorRefRGB(0x12, 0x34, 0x56), 0x00563412u);  // (r,g,b) form
}

// --- 0x41eee8 paintbox clear gating ----------------------------------------
TEST(UiRecon4, PaintboxClearGating) {
    PaintboxWindowRec w{};
    w.windowValid = 0; w.paintboxSurface = 999;
    CHECK(PaintboxClearAlt(w) == PaintboxClearResult::kInvalidateWindow);
    w.windowValid = 1; w.paintboxSurface = 0;
    CHECK(PaintboxClearAlt(w) == PaintboxClearResult::kNoPaintbox);
    w.windowValid = 1; w.paintboxSurface = 777;
    CHECK(PaintboxClearAlt(w) == PaintboxClearResult::kFilled);
}

TEST(UiRecon4, PaintboxRecordIndex) {
    CHECK_EQ(PaintboxWindowRecordIndex(0), 0);
    CHECK_EQ(PaintboxWindowRecordIndex(1), 238);
    CHECK_EQ(PaintboxWindowRecordIndex(3), 714);
}

// --- 0x51adb4 lender dialog pieces -----------------------------------------
TEST(UiRecon4, LenderInitRowSlots) {
    LenderRowSlot slots[16];
    int n = LenderInitRowSlots(slots);
    CHECK_EQ(n, 16);
    for (int k = 0; k < 16; ++k) {
        CHECK_EQ(slots[k].a, -1);
        CHECK_EQ(slots[k].b, -1);
        CHECK_EQ(slots[k].c, 0);
    }
}

TEST(UiRecon4, LenderSliderPanelValues) {
    auto v = LenderSliderPanelValues();
    CHECK_EQ(v.a, 14); CHECK_EQ(v.b, 24); CHECK_EQ(v.c, 14); CHECK_EQ(v.d, 14);
}

TEST(UiRecon4, LenderSumBuildingValues) {
    i32 vals[] = {100, 250, 0, 75};
    CHECK_EQ(LenderSumBuildingValues(vals, 4), (i64)425);
    CHECK_EQ(LenderSumBuildingValues(vals, 0), (i64)0);
    i32 one[] = {42};
    CHECK_EQ(LenderSumBuildingValues(one, 1), (i64)42);
}

// --- 0x51d9a4 asset overview setup -----------------------------------------
TEST(UiRecon4, AssetOverviewSetup) {
    auto p = AssetOverviewSetup();
    CHECK_EQ(p.mode, 6);
    CHECK_EQ(p.capacity, 1024);
    CHECK_EQ(p.textId, 5371);
    CHECK_EQ(p.grayShade, 40);
}

// ---------------------------------------------------------------------------
// HARDENING (wave-12): the lender-row init fills exactly the 16-slot caller array
// (gilde.exe 0x51adb4 inner loop, v3 = 3,6,...,48 -> 16 logical slots). It must
// not write past slots[16] (caught by ASAN if it did).
// ---------------------------------------------------------------------------
TEST(UiRecon4Hud, LenderInitRowSlotsFillsExactlySixteen) {
    LenderRowSlot slots[16];
    int n = LenderInitRowSlots(slots);
    CHECK_EQ(n, 16);
    for (int k = 0; k < 16; ++k) {
        CHECK_EQ(slots[k].a, -1);
        CHECK_EQ(slots[k].b, -1);
        CHECK_EQ(slots[k].c, 0);
    }
}

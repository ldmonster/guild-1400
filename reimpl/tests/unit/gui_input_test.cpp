// Unit tests for the guild::gui input-dispatch + generic-control module:
//   - input hit-test / click routing (ResolveClickedSlot, RouteClick) incl. ids
//   - scrollbar drag value math (ScrollbarDragValue, Scrollbar_SetThumbPosition)
//   - slider step table + value<->thumb roundtrip (Slider_ComputeStep,
//     Scrollbar_ValueFromThumb, Slider_QuantizeRange)
//   - radio group exclusive select + enable/disable (RadioGroup_*, Selection_Update)
//   - z-order insertion order (ZOrder_InsertObject/RemoveObject)
//   - tooltip dispatch by type (Tooltip_ClassifySubject / SelectBuilder)
//   - dialog form-name selection + result resolution
#include "gui/dialog.h"
#include "gui/form.h"
#include "gui/input.h"
#include "gui/object.h"
#include "gui/radiogroup.h"
#include "gui/scrollbar.h"
#include "gui/slider.h"
#include "gui/tooltip.h"
#include "gui/window.h"
#include "gui/zorder.h"
#include "tests/framework/test.h"

#include <cstdint>
#include <cstring>

using namespace guild::gui;

namespace {
void Reset() {
    ResetGuiState();
    ResetRadioGroups();
    ResetInputState();
}

// Make a clickable button widget child of a window with given id and bounds.
int MakeButton(int win, int id, i16 x, i16 y, i16 w, i16 h) {
    int idx = Object_AddToWindow(win, y, x, 0);
    g_widgets[idx].id()       = id;
    g_widgets[idx].w()        = w;
    g_widgets[idx].h()        = h;
    g_widgets[idx].btnFlagA() = 1;
    return idx;
}
} // namespace

// ---------------------------------------------------------------------------
// Slider step table — golden values straight from VIBE_Slider_ComputeStep @0x41df08.
// ---------------------------------------------------------------------------
TEST(GuiInputSlider, ComputeStepTable) {
    CHECK_EQ(Slider_ComputeStep(200000), 10000);
    CHECK_EQ(Slider_ComputeStep(125000), 10000);
    CHECK_EQ(Slider_ComputeStep(124999), 5000);
    CHECK_EQ(Slider_ComputeStep(100000), 5000);
    CHECK_EQ(Slider_ComputeStep(75000), 4000);
    CHECK_EQ(Slider_ComputeStep(50000), 3000);
    CHECK_EQ(Slider_ComputeStep(37500), 2000);
    CHECK_EQ(Slider_ComputeStep(25000), 1500);
    CHECK_EQ(Slider_ComputeStep(12500), 1000);
    CHECK_EQ(Slider_ComputeStep(5000), 500);
    CHECK_EQ(Slider_ComputeStep(2500), 200);
    CHECK_EQ(Slider_ComputeStep(1000), 100);
    CHECK_EQ(Slider_ComputeStep(250), 50);
    CHECK_EQ(Slider_ComputeStep(100), 10);
    CHECK_EQ(Slider_ComputeStep(99), 1);
    CHECK_EQ(Slider_ComputeStep(0), 1);
}

// Value <-> thumb roundtrip via the scrollbar value math.
TEST(GuiInputSlider, ValueFromThumbRoundtrip) {
    // span = max-min = 1000 -> step = 100. delta>>2 then *step.
    // thumbLo=0, thumbHi=40 -> delta=40, 40>>2=10, value = 0 + 100*10 = 1000 -> clamp max.
    CHECK_EQ(Scrollbar_ValueFromThumb(0, 0, 40, 0, 1000), 1000);
    // thumbHi=8 -> delta 8>>2=2 -> 200.
    CHECK_EQ(Scrollbar_ValueFromThumb(0, 0, 8, 0, 1000), 200);
    // negative delta rounds toward zero: -9>>2 == -2 (toward zero).
    CHECK_EQ(Scrollbar_ValueFromThumb(500, 9, 0, 0, 1000), 500 + 100 * (-2));
    // clamp to min.
    CHECK_EQ(Scrollbar_ValueFromThumb(0, 40, 0, 0, 1000), 0);
}

TEST(GuiInputSlider, QuantizeRangeSnapsValueAndBounds) {
    int mn = 0, mx = 1000;
    // step = 100. newMax = 1000 - (1000%100)=1000. newMin = (1000-0)%100 + 0 = 0.
    // value 350 -> 100*(350/100)=300. span 1000, 1000/100=10 (<=25) so max unchanged.
    int v = Slider_QuantizeRange(mn, mx, 350, false);
    CHECK_EQ(v, 300);
    CHECK_EQ(mn, 0);
    CHECK_EQ(mx, 1000);

    // value below min clamps to min.
    int mn2 = 50, mx2 = 1050; // span 1000 -> step 100. newMax=1050-(1000%100)=1050.
                              // newMin=(1050-50)%100+50 = 0+50 = 50.
    int v2 = Slider_QuantizeRange(mn2, mx2, 0, false);
    CHECK_EQ(v2, 50); // 100*(0/100)=0 < min(50) -> clamp to 50
}

// ---------------------------------------------------------------------------
// Scrollbar SetThumbPosition: value clamp + decimal text render.
// ---------------------------------------------------------------------------
TEST(GuiInputScrollbar, SetThumbPositionClampsAndRendersText) {
    ScrollState rec;
    rec.min = 0; rec.max = 1000; rec.flags = 0; rec.value = 0;
    // base 0, lo 0, hi 8 -> step 100, 8>>2=2 -> 200.
    int v = Scrollbar_SetThumbPosition(rec, 0, 0, 8);
    CHECK_EQ(v, 200);
    CHECK_EQ(rec.value, 200);
    CHECK(std::strcmp(rec.text, "200") == 0);

    // Over max clamps.
    int v2 = Scrollbar_SetThumbPosition(rec, 0, 0, 80);
    CHECK_EQ(v2, 1000);
    CHECK(std::strcmp(rec.text, "1000") == 0);

    // Negative value renders with leading '-'.
    // span = 0-(-500) = 500 -> step = Slider_ComputeStep(500) = 50.
    // delta = 0-8 = -8, -8>>2 = -2, value = 50*(-2) + (-100) = -200, in [-500,0].
    rec.min = -500; rec.max = 0;
    int v3 = Scrollbar_SetThumbPosition(rec, -100, 8, 0);
    CHECK_EQ(v3, -200);
    CHECK(std::strcmp(rec.text, "-200") == 0);
}

// ---------------------------------------------------------------------------
// Radio group: create, add, exclusive select, enable/disable.
// ---------------------------------------------------------------------------
TEST(GuiInputRadio, CreateSeedSelectExclusive) {
    Reset();
    int w = Window_Create(0, 0, 100, 100, 0);
    int b0 = MakeButton(w, 100, 0, 0, 10, 10);
    int b1 = MakeButton(w, 101, 0, 0, 10, 10);
    int b2 = MakeButton(w, 102, 0, 0, 10, 10);
    int ids[3] = {b0, b1, b2};

    int g = RadioGroup_Create(3, ids);
    CHECK_EQ(g, 0);
    CHECK_EQ(g_radioGroups[0].count, 3);
    CHECK_EQ(g_radioGroups[0].selected, -1);
    // btnFlagA set + radio bit cleared on each.
    CHECK(g_widgets[b0].btnFlagA() != 0);
    CHECK_EQ((int)(g_widgets[b0].radioFlag() & 2), 0);

    Selection_Update(g, 1);
    CHECK_EQ(g_radioGroups[0].selected, 1);
    CHECK_EQ(g_widgets[b0].value(), 0);
    CHECK_EQ(g_widgets[b1].value(), 1); // only the selected button is "on"
    CHECK_EQ(g_widgets[b2].value(), 0);

    Selection_Update(g, 2);
    CHECK_EQ(g_widgets[b1].value(), 0);
    CHECK_EQ(g_widgets[b2].value(), 1);
}

TEST(GuiInputRadio, AddButtonAutoSelectsFirst) {
    Reset();
    int w = Window_Create(0, 0, 100, 100, 0);
    int b0 = MakeButton(w, 1, 0, 0, 10, 10);
    int b1 = MakeButton(w, 2, 0, 0, 10, 10);

    int g = RadioGroup_Create(0, nullptr); // empty group
    CHECK_EQ(g_radioGroups[0].selected, -1);
    RadioGroup_AddButton(g, b0); // first add: selected was -1 -> auto-select 0
    CHECK_EQ(g_radioGroups[0].count, 1);
    CHECK_EQ(g_radioGroups[0].selected, 0);
    CHECK_EQ(g_widgets[b0].value(), 1);
    RadioGroup_AddButton(g, b1); // already selected -> no auto-select
    CHECK_EQ(g_radioGroups[0].count, 2);
    CHECK_EQ(g_radioGroups[0].selected, 0);
}

TEST(GuiInputRadio, SetEnabledTogglesDisabledFlag) {
    Reset();
    int w = Window_Create(0, 0, 100, 100, 0);
    int b0 = MakeButton(w, 1, 0, 0, 10, 10);
    int b1 = MakeButton(w, 2, 0, 0, 10, 10);
    int ids[2] = {b0, b1};
    int g = RadioGroup_Create(2, ids);

    RadioGroup_SetEnabled(g, 0); // disabled -> +56 = 1
    CHECK_EQ(g_widgets[b0].disabledA(), 1);
    CHECK_EQ(g_widgets[b1].disabledA(), 1);
    RadioGroup_SetEnabled(g, 1); // enabled -> +56 = 0
    CHECK_EQ(g_widgets[b0].disabledA(), 0);
    CHECK_EQ(g_widgets[b1].disabledA(), 0);
}

TEST(GuiInputRadio, CreateRejectsTooManyGroupsAndButtons) {
    Reset();
    // Fill all 8 groups.
    int dummy = 0;
    for (int i = 0; i < kMaxRadioGroups; ++i) {
        int g = RadioGroup_Create(1, &dummy); // 1 button each (widget 0)
        CHECK_EQ(g, i);
    }
    CHECK_EQ(RadioGroup_Create(1, &dummy), -1); // 9th group rejected
    Reset();
    CHECK_EQ(RadioGroup_Create(33, nullptr), -1); // > 32 buttons rejected
}

// ---------------------------------------------------------------------------
// Input hit-test + click routing.
// ---------------------------------------------------------------------------
TEST(GuiInputDispatch, ResolveClickedSlot) {
    Reset();
    int w = Window_Create(0, 0, 200, 200, 0);
    int b0 = MakeButton(w, 100, 0, 0, 10, 10);
    int b1 = MakeButton(w, 101, 0, 0, 10, 10);
    CHECK_EQ(ResolveClickedSlot(w, b0), 0);
    CHECK_EQ(ResolveClickedSlot(w, b1), 1);
    CHECK_EQ(ResolveClickedSlot(w, 999), -1); // not a child
}

TEST(GuiInputDispatch, RouteClickSetsLastClickedId) {
    Reset();
    int w = Window_Create(0, 0, 200, 200, 0);
    int ok     = MakeButton(w, kIdOk, 0, 0, 10, 10);
    int cancel = MakeButton(w, kIdCancel, 0, 0, 10, 10);

    CHECK_EQ(RouteClick(w, ok), kIdOk);
    CHECK_EQ(g_lastClickedId, kIdOk);
    CHECK_EQ(g_lastClickedSlot, 0);

    CHECK_EQ(RouteClick(w, cancel), kIdCancel);
    CHECK_EQ(g_lastClickedId, kIdCancel);
    CHECK_EQ(g_lastClickedSlot, 1);
}

TEST(GuiInputDispatch, RouteClickWindowBackingId) {
    Reset();
    int w = Window_Create(0, 0, 50, 50, 0);
    // The window-backing widget id is slot + 1024.
    Widget& bw = g_widgets[g_windows[w].backWidget()];
    CHECK_EQ(bw.id(), WindowBackingId(w));
    // Make it clickable and route a click to it -> last id is the 1024-offset id.
    bw.btnFlagA() = 1;
    int routed = RouteClick(w, g_windows[w].backWidget());
    CHECK_EQ(routed, WindowBackingId(w));
    CHECK_EQ(g_lastClickedId, w + kWindowIdBase);
}

TEST(GuiInputDispatch, RouteClickTogglesButtonAndRunsRadio) {
    Reset();
    int w = Window_Create(0, 0, 100, 100, 0);
    int b0 = MakeButton(w, 1, 0, 0, 10, 10);
    int b1 = MakeButton(w, 2, 0, 0, 10, 10);
    int ids[2] = {b0, b1};
    int g = RadioGroup_Create(2, ids);
    Selection_Update(g, 0); // b0 on

    // Clicking b1: it is in a radio group -> exclusive select makes b1 on, b0 off,
    // and the group's selected index becomes 1.
    RouteClick(w, b1);
    CHECK_EQ(g_radioGroups[g].selected, 1);
    CHECK_EQ(g_widgets[b0].value(), 0);
    CHECK_EQ(g_widgets[b1].value(), 1);
}

TEST(GuiInputDispatch, RouteClickRejectsNonClickable) {
    Reset();
    int w = Window_Create(0, 0, 100, 100, 0);
    int idx = Object_AddToWindow(w, 0, 0, 0);
    g_widgets[idx].id() = 77; // no btnFlag -> not clickable
    CHECK_EQ(RouteClick(w, idx), -1);
}

TEST(GuiInputDispatch, ScrollbarDragValueBand) {
    // value = thumbMax*(mouseX - winX - 12)/(winW-24) + 4
    // winX=0, winW=100 -> band (10, 90). thumbMax=100.
    CHECK_EQ(ScrollbarDragValue(100, 0, 100, 50, -7), 100 * (50 - 12) / 76 + 4); // = 54
    // Outside band returns current unchanged.
    CHECK_EQ(ScrollbarDragValue(100, 0, 100, 5, -7), -7);
    CHECK_EQ(ScrollbarDragValue(100, 0, 100, 95, -7), -7);
}

// ---------------------------------------------------------------------------
// Z-order insertion / removal order.
// ---------------------------------------------------------------------------
TEST(GuiInputZOrder, InsertKeepsGroupSiblingsContiguous) {
    Reset();
    int w = Window_Create(0, 0, 100, 100, 0);
    int parent = g_windows[w].backWidget();

    // Three children of the same group (groupLink == parent), inserted in turn.
    int c[3];
    for (int i = 0; i < 3; ++i) {
        c[i] = Widget_AllocSlot();
        g_widgets[c[i]].groupLink() = parent;
        g_widgets[c[i]].type() = kTypeLabel;
        ZOrder_InsertObject(c[i], parent);
    }
    // All three must be present in the z-order cache.
    auto present = [&](int idx) {
        for (int s = 0; s <= kMaxWidgets; ++s)
            if (g_widgetCache[s] == &g_widgets[idx]) return true;
        return false;
    };
    CHECK(present(c[0]));
    CHECK(present(c[1]));
    CHECK(present(c[2]));

    // Removing the middle one drops it but keeps the others.
    ZOrder_RemoveObject(c[1]);
    CHECK(present(c[0]));
    CHECK(!present(c[1]));
    CHECK(present(c[2]));
}

// ---------------------------------------------------------------------------
// Tooltip dispatch by type.
// ---------------------------------------------------------------------------
TEST(GuiInputTooltip, ClassifyByRangeAndBuilder) {
    // Build synthetic scene tables.
    static u8 objectTable[65 * 4];
    static u8 buildingTable[8];
    std::memset(objectTable, 0, sizeof(objectTable));
    std::memset(buildingTable, 0, sizeof(buildingTable));
    // object record 2's class byte = 32 -> kObject; record 3 class byte = 5 -> kUpgrade.
    objectTable[65 * 2] = 32;
    objectTable[65 * 3] = 5;
    buildingTable[1] = 4; // building code 4 (< 72)

    TooltipTables t{};
    t.objectBase   = objectTable;
    t.buildingBase = buildingTable;

    // A pointer at objectTable + 65*2 -> object code 2 -> class 32 -> kObject.
    {
        TooltipSubject s = Tooltip_ClassifySubject(t, objectTable + 65 * 2, 0);
        CHECK_EQ(s.objectCode, 2);
        CHECK(s.kind == TooltipKind::kObject);
    }
    // objectTable + 65*3 -> object code 3 -> class 5 -> kUpgrade.
    {
        TooltipSubject s = Tooltip_ClassifySubject(t, objectTable + 65 * 3, 0);
        CHECK_EQ(s.objectCode, 3);
        CHECK(s.kind == TooltipKind::kUpgrade);
    }
    // Building table pointer -> building code, capped at 72.
    {
        TooltipSubject s = Tooltip_ClassifySubject(t, buildingTable + 1, 0);
        CHECK_EQ(s.buildingCode, 4);
        CHECK(s.kind == TooltipKind::kBuilding);
    }
}

TEST(GuiInputTooltip, FallbackByIdWhenNoSceneRef) {
    static u8 objectTable[65 * 8];
    std::memset(objectTable, 0, sizeof(objectTable));
    objectTable[65 * 4] = 23; // id 210 -> object code 210-206 = 4 -> class 23 -> kObject
    TooltipTables t{};
    t.objectBase = objectTable;

    TooltipSubject s = Tooltip_ClassifySubject(t, nullptr, 210);
    CHECK_EQ(s.objectCode, 4);
    CHECK(s.kind == TooltipKind::kObject);

    // id in building range.
    TooltipSubject sb = Tooltip_ClassifySubject(t, nullptr, 1020);
    CHECK_EQ(sb.buildingCode, 1020 + kBldIdBias);
    CHECK(sb.kind == TooltipKind::kBuilding);

    // id out of any range -> none.
    TooltipSubject sn = Tooltip_ClassifySubject(t, nullptr, 50);
    CHECK(sn.kind == TooltipKind::kNone);
}

// ---------------------------------------------------------------------------
// Dialog model.
// ---------------------------------------------------------------------------
TEST(GuiInputDialog, FormForFlags) {
    CHECK(std::strcmp(Dialog_FormForFlags(kMsgFlagAutosize), "misc\\Messagebox_Autosize") == 0);
    CHECK(std::strcmp(Dialog_FormForFlags(kMsgFlagBig), "misc\\Messagebox_BIG") == 0);
    CHECK(std::strcmp(Dialog_FormForFlags(kMsgFlagVeryBig), "misc\\Messagebox_VERY_BIG") == 0);
    CHECK(std::strcmp(Dialog_FormForFlags(kMsgFlagNonePerga), "misc\\Messagebox_NONE_PERGA") == 0);
    CHECK(std::strcmp(Dialog_FormForFlags(0), "misc\\Messagebox") == 0);
    // Priority: Autosize wins over Big.
    CHECK(std::strcmp(Dialog_FormForFlags(kMsgFlagAutosize | kMsgFlagBig),
                      "misc\\Messagebox_Autosize") == 0);
}

TEST(GuiInputDialog, ResolveResult) {
    CHECK_EQ(Dialog_ResolveResult(kIdOk, 0, false), 1);     // OK, button 0 -> 1
    CHECK_EQ(Dialog_ResolveResult(kIdOk, 2, false), 3);     // OK, button 2 -> 3
    CHECK_EQ(Dialog_ResolveResult(kIdCancel, 2, false), 0); // Cancel -> 0
    CHECK_EQ(Dialog_ResolveResult(-1, 2, true), 0);         // right-click -> 0
    CHECK_EQ(Dialog_ResolveResult(-1, 2, false), -1);       // keep running
}

TEST(GuiInputDialog, BuildButtonGroupSkipsWindowBacking) {
    Reset();
    int w = Window_Create(0, 0, 100, 100, 0);
    int b0 = MakeButton(w, kIdOk, 0, 0, 10, 10);
    int b1 = MakeButton(w, kIdCancel, 0, 0, 10, 10);
    (void)b0; (void)b1;
    // The window-backing widget is NOT in the child list (it is the parent), so the
    // group should contain exactly the two buttons.
    int g = Dialog_BuildButtonGroup(w);
    CHECK(g >= 0);
    CHECK_EQ(g_radioGroups[g].count, 2);
    CHECK_EQ(g_radioGroups[g].selected, 0); // Selection_Update(g,0)
}

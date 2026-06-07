// End-to-end: build a messagebox-style form (window-backing + OK/Cancel buttons + a
// radio group + a scrollbar), then drive a sequence of clicks and a scrollbar drag and
// verify (a) which object id each click resolves to (dword_75BF38), (b) the resulting
// radio selection / button values, (c) the scrollbar value after a drag, and (d) the
// dialog result resolution — all against a hand-computed reference.
#include "gui/dialog.h"
#include "gui/form.h"
#include "gui/input.h"
#include "gui/object.h"
#include "gui/radiogroup.h"
#include "gui/scrollbar.h"
#include "gui/slider.h"
#include "gui/window.h"
#include "gui/zorder.h"
#include "tests/framework/test.h"

#include <cstdint>
#include <cstring>

using namespace guild::gui;

namespace {
int MakeButton(int win, int id) {
    int idx = Object_AddToWindow(win, 0, 0, 0);
    g_widgets[idx].id()       = id;
    g_widgets[idx].w()        = 20;
    g_widgets[idx].h()        = 12;
    g_widgets[idx].btnFlagA() = 1;
    return idx;
}
} // namespace

TEST(GuiInputE2E, MessageboxClickAndDragFlow) {
    ResetGuiState();
    ResetRadioGroups();
    ResetInputState();

    // --- Build the form/window with three buttons (OK, Cancel, an extra choice). -----
    int win = Window_Create(40, 30, 200, 120, 0);
    int backing = g_windows[win].backWidget();
    CHECK_EQ(g_widgets[backing].id(), WindowBackingId(win)); // slot + 1024

    int bOk     = MakeButton(win, kIdOk);     // 1210
    int bCancel = MakeButton(win, kIdCancel); // 1155
    int bChoice = MakeButton(win, 300);       // an extra selectable button

    // Wire a form table so the form selector can address this window as group 0.
    Form& f = g_forms[2];
    f.windowCount() = 0;
    f.windowId(0) = win;
    g_currentFormId = 2;
    CHECK_EQ(Form_SelectWindow(2, 0), 1);
    CHECK_EQ(g_currentWindowId, win);

    // --- Build a radio group from the form's buttons (the dialog build pattern). -----
    int g = Dialog_BuildButtonGroup(win);
    CHECK(g >= 0);
    // Window-backing widget is the parent, not a child -> only the 3 buttons join.
    CHECK_EQ(g_radioGroups[g].count, 3);
    CHECK_EQ(g_radioGroups[g].selected, 0);   // Selection_Update(g,0)
    CHECK_EQ(g_widgets[bOk].value(), 1);      // button 0 is selected/on
    CHECK_EQ(g_widgets[bChoice].value(), 0);

    // --- Add a scrollbar to the form (model only). -----------------------------------
    ScrollState bar;
    bar.min = 0; bar.max = 1000; bar.flags = 0; bar.value = 0;

    // --- Click sequence. -------------------------------------------------------------
    // (1) Click the extra choice button. It is in the radio group at index 2, so the
    //     exclusive select moves selection to 2; last-clicked id = 300, slot = 2.
    int routed1 = RouteClick(win, bChoice);
    CHECK_EQ(routed1, 300);
    CHECK_EQ(g_lastClickedId, 300);
    CHECK_EQ(g_lastClickedSlot, 2);              // bChoice is child slot 2
    CHECK_EQ(g_radioGroups[g].selected, 2);
    CHECK_EQ(g_widgets[bOk].value(), 0);         // previously-on button turned off
    CHECK_EQ(g_widgets[bChoice].value(), 1);

    // (2) Drag the scrollbar thumb. Hand reference:
    //     span = 1000 -> step = Slider_ComputeStep(1000) = 100.
    //     lo=0, hi=12 -> delta 12, 12>>2 = 3 -> value = 0 + 100*3 = 300.
    int newVal = Scrollbar_SetThumbPosition(bar, 0, 0, 12);
    CHECK_EQ(newVal, 300);
    CHECK_EQ(bar.value, 300);
    CHECK(std::strcmp(bar.text, "300") == 0);

    // (3) The user's committed choice is the selection standing when OK is pressed.
    //     The modal loop captures dword_676588[group] at the moment it detects the OK
    //     id, so we snapshot it before routing the OK click (which, like every button,
    //     re-runs the group's exclusive select).
    int committedChoice = g_radioGroups[g].selected; // = 2 (bChoice)
    int routed2 = RouteClick(win, bOk);
    CHECK_EQ(routed2, kIdOk);
    CHECK_EQ(g_lastClickedId, kIdOk);
    CHECK_EQ(g_lastClickedSlot, 0);
    int result = Dialog_ResolveResult(g_lastClickedId, committedChoice, false);
    CHECK_EQ(result, 3); // committed choice index 2 + 1

    // (4) A Cancel click resolves the dialog to 0 regardless of selection.
    RouteClick(win, bCancel);
    CHECK_EQ(g_lastClickedId, kIdCancel);
    CHECK_EQ(Dialog_ResolveResult(g_lastClickedId, g_radioGroups[g].selected, false), 0);

    (void)bOk; (void)bCancel; (void)backing;
}

// A second flow: scrollbar value<->thumb roundtrip + slider quantisation across a drag.
TEST(GuiInputE2E, ScrollbarRoundtripAndQuantize) {
    ScrollState bar;
    bar.min = 0; bar.max = 2000; bar.flags = 0; bar.value = 0;

    // span 2000 -> step = Slider_ComputeStep(2000) = 100.
    CHECK_EQ(Slider_ComputeStep(bar.max - bar.min), 100);

    // Drag to ~half: lo=0, hi=40 -> 40>>2=10 -> 100*10 = 1000.
    CHECK_EQ(Scrollbar_SetThumbPosition(bar, 0, 0, 40), 1000);
    CHECK_EQ(bar.value, 1000);

    // Recover the thumb delta that produced this value from base 0:
    //   value = step*(delta>>2) -> delta>>2 = value/step = 10 -> delta in [40,43].
    int step = Slider_ComputeStep(bar.max - bar.min);
    CHECK_EQ(Scrollbar_ValueFromThumb(0, 0, 40, bar.min, bar.max), step * 10);

    // Quantise a fresh range and confirm value snapping.
    int mn = 0, mx = 2000;
    int snapped = Slider_QuantizeRange(mn, mx, 1234, false); // 100*(1234/100)=1200
    CHECK_EQ(snapped, 1200);
    CHECK_EQ(mn, 0);
    CHECK_EQ(mx, 2000);
}

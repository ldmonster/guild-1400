// Integration tests: drive the interactive-widget value model against the REAL
// sibling modules it shares state with — the recovered Widget array + allocator
// (gui/object), the window scroll bookkeeping (gui/window_mgmt + gui/window), the
// cursor-clamp globals (gui/input_state) and the scrollbar value model
// (gui/scrollbar). No mocks: the same g_widgets / g_windows / g_cursorClamp* the
// game uses.
#include "test.h"
#include "gui/widget_interact.h"
#include "gui/object.h"
#include "gui/window.h"
#include "gui/window_mgmt.h"
#include "gui/scrollbar.h"
#include "gui/slider.h"
#include "gui/input_state.h"

using namespace guild::gui;

namespace {
void freshState() {
    ResetGuiState();        // forms + windows + widgets
    ResetWidgetInteract();
    ResetMouseInput();
}
} // namespace

// A scrollbar drag that commits a value into a REAL widget's +40 mirror, then a
// ClearActiveDrag that tears it back down through the real cursor-clamp globals.
TEST(GuiWidgetInteractItest, DragThenClearAcrossWidgetAndInputState) {
    freshState();

    int slot = Widget_AllocSlot();           // real allocator
    CHECK(slot >= 0);

    ScrollDragRecord rec;
    rec.v.min = 0; rec.v.max = 200; rec.v.value = 100; rec.v.flags = (guild::u8)0x82; // drag + vertical
    rec.owner = slot;
    g_activeDrag = &rec;
    g_cursorClampY0 = 0; g_cursorClampY1 = 0;

    // Latch frame: origin captured, vertical clamp opened via real input_state globals.
    Scrollbar_DragThumb(/*curX*/300, /*curY*/150, /*edge20*/1, /*held*/0);
    // step(span200)=10 (>=100). clampY0 = 300 + 4*100/10 = 340 ; clampY1 = 300 - 4*100/10 = 260.
    CHECK_EQ(g_cursorClampY0, 340);
    CHECK_EQ(g_cursorClampY1, 260);
    CHECK_EQ(g_savedClampY0, 0);   // parked the previous clamp
    CHECK_EQ(g_savedClampY1, 0);

    // Drag left to 260 -> delta=300-260=40 -> /4=10 -> *10=100 +base100 = 200 (==max).
    int committed = Scrollbar_DragThumb(260, 150, 1, 0);
    CHECK_EQ(committed, 200);
    CHECK_EQ(rec.v.value, 200);

    // Mark the owning widget mirror as if drawn, then clear the drag.
    g_widgets[slot].valueMirror() = 1;
    g_dragOwnerWidget = slot;                // a clamp was opened on this widget
    int restored = Widget_ClearActiveDrag();
    CHECK_EQ(restored, g_savedClampY1);      // returns the restored clamp y1
    CHECK_EQ((int)g_widgets[slot].valueMirror(), 0);
    CHECK(g_activeDrag == nullptr);
    // Live clamp restored to the parked values.
    CHECK_EQ(g_cursorClampY0, g_savedClampY0);
    CHECK_EQ(g_cursorClampY1, g_savedClampY1);
}

// The scroll-limit code written by Widget_SetScrollLimit is the exact +132 bit
// (0x10 = clamp-to-step) the slider button-step path then reads — verify the two
// real functions agree on the field.
TEST(GuiWidgetInteractItest, SetScrollLimitFeedsStepClamp) {
    freshState();
    int slot = Widget_AllocSlot();
    CHECK(slot >= 0);

    Widget& w = g_widgets[slot];
    w.editVal()   = 3;     // current
    w.editValue() = 0;     // min
    w.editMax()   = 100;   // max
    w.editStep()  = 3;     // step
    w.incButtonHeld() = 1;

    // Without the clamp bit: inc 3 -> 4, stays (4 <= max 100).
    Widget_SetScrollLimit(slot, 0);
    CHECK_EQ(Slider_StepFromButtons(slot, /*clickEdge*/1, 0), 4);

    // With the 0x10 clamp-to-step bit: inc 3 -> 4 > step 3 -> clamped to step 3.
    w.editVal() = 3;
    Widget_SetScrollLimit(slot, 0x10);
    CHECK_EQ((int)(std::uint16_t)w.editFlags(), 0x10);
    CHECK_EQ(Slider_StepFromButtons(slot, 1, 0), 3);
}

// Cross-check the scrollbar drag math against the standalone Scrollbar_ValueFromThumb
// (gui/slider.h) — the drag driver and the pure value helper must agree for an
// in-range move.
TEST(GuiWidgetInteractItest, DragMatchesValueFromThumb) {
    freshState();
    ScrollDragRecord rec;
    rec.v.min = 0; rec.v.max = 1000; rec.v.value = 400; rec.v.flags = 0x02;
    rec.owner = 0;
    g_activeDrag = &rec;

    Scrollbar_DragThumb(500, 0, 1, 0);               // latch: origin 500, start 400
    int driven = Scrollbar_DragThumb(480, 0, 1, 0);  // curX 480

    // Independent oracle: same step bucket, same delta math.
    int oracle = Scrollbar_ValueFromThumb(/*base*/400, /*thumbLo*/480, /*thumbHi*/500,
                                          /*min*/0, /*max*/1000);
    CHECK_EQ(driven, oracle);
}

// Drive a REAL window scroll alongside the drag cluster — they share no state but
// must coexist in one frame; confirm Window_Scroll clamps independently.
TEST(GuiWidgetInteractItest, WindowScrollCoexistsWithDrag) {
    freshState();
    int win = 1;
    Window& w = g_windows[win];
    w.contentHeight() = 500;   // +580
    w.scrollCur()     = 0;     // +584
    w.scrollOffset()  = 0;     // +592
    w.scrollExtraX()  = 0;     // +596
    w.h()             = 100;   // visible height

    // Scroll down by 80 within content -> offset advances by 80.
    Window_Scroll(/*delta*/5, /*amount*/80, win);
    CHECK_EQ(w.scrollOffset(), 80);
    CHECK_EQ(w.scrollExtraX(), 5);

    // Over-scroll: only the remaining (500 - (80+0+100)=320) is allowed for amount 1000.
    Window_Scroll(0, 1000, win);
    CHECK_EQ(w.scrollOffset(), 80 + 320);
}

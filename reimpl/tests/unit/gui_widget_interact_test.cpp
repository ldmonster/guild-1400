// Unit golden-vector tests for guild::gui interactive-widget value math:
//   Widget_SetScrollLimit, Widget_ClearActiveDrag, Scrollbar_DragThumb,
//   Slider_StepFromButtons.  Vectors precomputed with python against the recovered
//   integer arithmetic (round-toward-zero >>2, step-bucket table, button-step clamp).
#include "test.h"
#include "gui/widget_interact.h"
#include "gui/object.h"
#include "gui/slider.h"
#include "gui/input_state.h"

using namespace guild::gui;

namespace {
void clean() {
    ResetWidgets();
    ResetWidgetInteract();
    ResetMouseInput();
}
} // namespace

// ---- Slider_ComputeStep bucket table (dependency, sanity-pinned) -----------
TEST(GuiWidgetInteractUnit, ComputeStepBuckets) {
    CHECK_EQ(Slider_ComputeStep(99), 1);
    CHECK_EQ(Slider_ComputeStep(100), 10);
    CHECK_EQ(Slider_ComputeStep(300), 50);
    CHECK_EQ(Slider_ComputeStep(20000), 1000);
    CHECK_EQ(Slider_ComputeStep(125000), 10000);
}

// ---- Widget_SetScrollLimit -------------------------------------------------
TEST(GuiWidgetInteractUnit, SetScrollLimit) {
    clean();
    int off = Widget_SetScrollLimit(3, 0x10);
    CHECK_EQ(off, 740 * 3);
    CHECK_EQ((int)g_widgets[3].editFlags(), 0x10);
    // High-bit code stored as a signed 16-bit word.
    Widget_SetScrollLimit(3, (guild::i16)0x40);
    CHECK_EQ((int)(std::uint16_t)g_widgets[3].editFlags(), 0x40);
}

// ---- Scrollbar_DragThumb: latch + value commit -----------------------------
TEST(GuiWidgetInteractUnit, DragThumbCommitClampMax) {
    clean();
    ScrollDragRecord rec;
    rec.v.min = 0; rec.v.max = 100; rec.v.value = 50; rec.v.flags = 0x02; // drag-enabled
    rec.owner = 2;
    g_activeDrag = &rec;

    // First frame: edge20=1, originX captured = curMouseX=200, start value=50.
    // delta = originX - curX. Here curX==originX on the latch frame -> delta 0 -> value 50.
    int v0 = Scrollbar_DragThumb(/*curX*/200, /*curY*/120, /*edge20*/1, /*held*/0);
    CHECK_EQ(v0, 50);
    CHECK_EQ(g_dragLatched, 1);
    CHECK_EQ(g_dragStartValue, 50);
    CHECK_EQ(g_dragOriginX, 200);

    // Subsequent frame: mouse moves left to 160 -> delta=200-160=40 -> /4=10 -> *step10 -> +100
    // base50 -> 150 -> clamps to max 100.
    int v1 = Scrollbar_DragThumb(160, 120, 1, 0);
    CHECK_EQ(v1, 100);
    CHECK_EQ(rec.v.value, 100);
}

TEST(GuiWidgetInteractUnit, DragThumbWithinRangeAndLargeSpan) {
    clean();
    ScrollDragRecord rec;
    rec.v.min = 0; rec.v.max = 100; rec.v.value = 50; rec.v.flags = 0x02;
    rec.owner = 1;
    g_activeDrag = &rec;
    Scrollbar_DragThumb(200, 0, 1, 0);             // latch: origin 200, start 50
    // curX=210 -> delta=200-210=-10 -> /4 (toward zero)=-2 -> *10=-20 +50=30
    CHECK_EQ(Scrollbar_DragThumb(210, 0, 1, 0), 30);

    // Large span: span=20000 -> step=1000.
    clean();
    ScrollDragRecord big;
    big.v.min = 0; big.v.max = 20000; big.v.value = 5000; big.v.flags = 0x02; big.owner = 0;
    g_activeDrag = &big;
    Scrollbar_DragThumb(300, 0, 1, 0);             // latch origin 300, start 5000
    // curX=260 delta=300-260=40 /4=10 *1000=10000 +5000=15000
    CHECK_EQ(Scrollbar_DragThumb(260, 0, 1, 0), 15000);
}

TEST(GuiWidgetInteractUnit, DragThumbPageWhileHeld) {
    clean();
    ScrollDragRecord rec;
    rec.v.min = 0; rec.v.max = 100; rec.v.value = 50; rec.v.flags = 0x02; rec.owner = 0;
    g_activeDrag = &rec;
    // held67222C=1 with edge20=0: value paged up to max, then early-returns (no commit).
    int r = Scrollbar_DragThumb(0, 0, /*edge20*/0, /*held*/1);
    CHECK_EQ(rec.v.value, 100);   // paged to max
    CHECK_EQ(r, -1);              // early-return sentinel
}

TEST(GuiWidgetInteractUnit, DragThumbDisabledAndInactive) {
    clean();
    // No active drag -> sentinel.
    CHECK_EQ(Scrollbar_DragThumb(0, 0, 1, 0), -1);
    // Active but not drag-enabled (flags bit 2 clear) -> sentinel, no latch.
    ScrollDragRecord rec;
    rec.v.flags = 0; rec.v.value = 5; g_activeDrag = &rec;
    CHECK_EQ(Scrollbar_DragThumb(10, 10, 1, 0), -1);
    CHECK_EQ(g_dragLatched, 0);
}

TEST(GuiWidgetInteractUnit, DragThumbVerticalClampOpen) {
    clean();
    ScrollDragRecord rec;
    rec.v.min = 0; rec.v.max = 100; rec.v.value = 50;
    rec.v.flags = (guild::u8)0x82; // bit 0x02 drag-enabled + bit 0x80 vertical (sign-bit set)
    rec.owner = 0;
    g_activeDrag = &rec;
    g_cursorClampY0 = 0; g_cursorClampY1 = 0;
    Scrollbar_DragThumb(/*curX*/200, /*curY*/120, /*edge20*/1, /*held*/0);
    // step(span100)=10; clampY0 = originX + 4*(start-min)/step = 200 + 4*50/10 = 220
    //                    clampY1 = originX - 4*(max-start)/step = 200 - 4*50/10 = 180
    CHECK_EQ(g_cursorClampY0, 220);
    CHECK_EQ(g_cursorClampY1, 180);
    CHECK_EQ(rec.track32, 220);   // *(rec+32) = curX + 4*(start-min)/step
}

// ---- Slider_StepFromButtons ------------------------------------------------
TEST(GuiWidgetInteractUnit, SliderStepButtons) {
    clean();
    auto setup = [](int idx, int cur, int mn, int mx, int step, int flags, int dec, int inc) {
        Widget& w = g_widgets[idx];
        w.editVal()    = cur;   // +120
        w.editValue()  = mn;    // +124 (min)
        w.editMax()    = mx;    // +128 (max)
        w.editStep()   = step;  // +140
        w.editFlags()  = (guild::i16)flags; // +132
        w.decButtonHeld() = (guild::u8)dec; // +732
        w.incButtonHeld() = (guild::u8)inc; // +733
        w.dirty()      = 0;     // +96
    };

    // sb1: cur=5 inc, clickEdge -> 6
    setup(0, 5, 0, 10, 3, 0, 0, 1);
    CHECK_EQ(Slider_StepFromButtons(0, /*clickEdge*/1, /*autoRepeat*/0), 6);
    CHECK_EQ((int)g_widgets[0].dirty(), 1);

    // sb2: cur=0 dec -> -1 < min -> clamp to min 0
    setup(0, 0, 0, 10, 3, 0, 1, 0);
    CHECK_EQ(Slider_StepFromButtons(0, 1, 0), 0);

    // sb3: cur=10 inc -> 11 > max -> 10
    setup(0, 10, 0, 10, 3, 0, 0, 1);
    CHECK_EQ(Slider_StepFromButtons(0, 1, 0), 10);

    // sb4: clamp-to-step flag 0x10, cur=2 inc->3, not >step3 -> 3
    setup(0, 2, 0, 10, 3, 0x10, 0, 1);
    CHECK_EQ(Slider_StepFromButtons(0, 1, 0), 3);

    // sb5: clamp-to-step, cur=3 inc->4 >step3 -> v=step=3
    setup(0, 3, 0, 100, 3, 0x10, 0, 1);
    CHECK_EQ(Slider_StepFromButtons(0, 1, 0), 3);

    // sb6: no event (clickEdge=0, autoRepeat=0) -> unchanged 7, not marked dirty
    setup(0, 7, 0, 10, 3, 0, 0, 1);
    CHECK_EQ(Slider_StepFromButtons(0, 0, 0), 7);
    CHECK_EQ((int)g_widgets[0].dirty(), 0);

    // sb7: autoRepeat but max<100 -> no event -> unchanged 7
    setup(0, 7, 0, 50, 3, 0, 0, 1);
    CHECK_EQ(Slider_StepFromButtons(0, 0, 1), 7);

    // sb8: autoRepeat and max>=100 inc -> 8
    setup(0, 7, 0, 100, 3, 0, 0, 1);
    CHECK_EQ(Slider_StepFromButtons(0, 0, 1), 8);
}

// ---- Widget_ClearActiveDrag ------------------------------------------------
TEST(GuiWidgetInteractUnit, ClearActiveDrag) {
    clean();
    // No active drag -> returns 0, no crash.
    CHECK_EQ(Widget_ClearActiveDrag(), 0);

    ScrollDragRecord rec;
    rec.owner = 4;
    g_activeDrag = &rec;
    g_widgets[4].valueMirror() = 7;     // +40 should be cleared
    g_dragOwnerWidget = 9;              // drag had opened a clamp
    g_savedClampY0 = 11; g_savedClampY1 = 22;
    int r = Widget_ClearActiveDrag();
    CHECK_EQ(r, 22);                    // returns saved clamp y1
    CHECK_EQ((int)g_widgets[4].valueMirror(), 0);
    CHECK(g_activeDrag == nullptr);
    CHECK_EQ(g_dragOwnerWidget, -1);
    CHECK_EQ(g_cursorClampY0, 11);
    CHECK_EQ(g_cursorClampY1, 22);

    // Active drag but no clamp owner (-1): returns 740*owner offset, leaves clamp.
    ResetWidgetInteract();
    ResetMouseInput();
    ScrollDragRecord rec2;
    rec2.owner = 2;
    g_activeDrag = &rec2;
    g_dragOwnerWidget = -1;
    CHECK_EQ(Widget_ClearActiveDrag(), 740 * 2);
    CHECK(g_activeDrag == nullptr);
}

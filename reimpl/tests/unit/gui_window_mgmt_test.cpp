// Unit tests for the window lifecycle/management + form lifecycle families.
//   src/gui/window_mgmt.cpp   — Window_PositionCentered / RemoveIfActive /
//                               RemoveChildren / Scroll
//   src/gui/form_lifecycle.cpp — Form_GetWindowId / SetObjectsVisible /
//                               SetChildrenVisible / RaiseWindows / Destroy /
//                               CenterChildWindows / PositionChildWindows
#include "test.h"

#include "gui/window_mgmt.h"
#include "gui/form_lifecycle.h"
#include "gui/form.h"
#include "gui/window.h"
#include "gui/object.h"

#include <cstdint>

using namespace guild::gui;

namespace {

// Test seam: count Widget_LayoutBounds calls and capture last args. The production
// edge is weak in window_render.cpp, so this strong definition wins at link.
int   g_lbCount = 0;
int   g_lbX = 0, g_lbY = 0, g_lbIdx = 0;
int   g_destroyCount = 0;
int   g_lastDestroyed = -1;
int   g_raiseCount = 0;
int   g_lastRaised = -1;

// Helper: make a fresh enabled window with a backing widget and `n` children.
int MakeWindow(i16 x, i16 y, i16 w, i16 h, int n) {
    int slot = Window_Create(x, y, w, h, 0);
    Window& win = g_windows[slot];
    i32* list = WindowChildList(slot);
    for (int i = 0; i < n; ++i) {
        int idx = Widget_AllocSlot();
        list[i] = idx;
        win.objCount() = static_cast<guild::i16>(i + 1);
    }
    return slot;
}

} // namespace

// Strong overrides of the weak render/OS edges, in guild::gui so they match.
namespace guild::gui {
void Widget_LayoutBounds(int x, int y, int idx) {
    ++g_lbCount; g_lbX = x; g_lbY = y; g_lbIdx = idx;
}
void Widget_DestroyByType(int idx, int, int) {
    ++g_destroyCount; g_lastDestroyed = idx;
}
int ZOrder_RaiseWindow(int slot) { ++g_raiseCount; g_lastRaised = slot; return slot; }
} // namespace guild::gui

TEST(GuiWindowMgmt, PositionCenteredModes) {
    ResetGuiState();
    g_screenCenterX = 320; g_screenCenterY = 240;
    int slot = Window_Create(50, 60, 100, 40, 0); // x=50,y=60,w=100,h=40
    int backIdx = g_windows[slot].backWidget();

    g_lbCount = 0;
    Window_PositionCentered(slot, 0);             // neither bit: x=win.x, y=win.y
    CHECK_EQ(g_lbX, 50);
    CHECK_EQ(g_lbY, 60);
    CHECK_EQ(g_lbIdx, backIdx);

    Window_PositionCentered(slot, 1);             // bit0: x = 320 - 100/2 = 270
    CHECK_EQ(g_lbX, 270);
    CHECK_EQ(g_lbY, 60);

    Window_PositionCentered(slot, 2);             // bit1: y = 240 - 40/2 = 220
    CHECK_EQ(g_lbX, 50);
    CHECK_EQ(g_lbY, 220);

    Window_PositionCentered(slot, 3);             // both
    CHECK_EQ(g_lbX, 270);
    CHECK_EQ(g_lbY, 220);
    CHECK_EQ(g_lbCount, 4);
}

TEST(GuiWindowMgmt, RemoveIfActive) {
    ResetGuiState();
    int slot = Window_Create(0, 0, 10, 10, 0);
    int backIdx = g_windows[slot].backWidget();
    g_destroyCount = 0;
    CHECK_EQ(Window_RemoveIfActive(slot, 0, 0), 1);
    CHECK_EQ(g_destroyCount, 1);
    CHECK_EQ(g_lastDestroyed, backIdx);

    // A free slot returns 0 and does not destroy.
    int free = 5;
    g_windows[free].enabled() = 0;
    CHECK_EQ(Window_RemoveIfActive(free, 0, 0), 0);
    CHECK_EQ(g_destroyCount, 1);
}

TEST(GuiWindowMgmt, RemoveChildrenResetsSpecialsAndScroll) {
    ResetGuiState();
    int slot = MakeWindow(0, 0, 20, 20, 3);
    Window& win = g_windows[slot];
    i32* list = WindowChildList(slot);

    // Mark the three special slots to two of the children + a stale id.
    win.reset234() = list[0];
    win.reset235() = list[2];
    win.reset236() = 999; // not a child -> stays
    win.scrollCur() = 7; win.contentHeight() = 99; win.scrollOffset() = 3;
    win.scrollPrev() = 42; win.textBuffer() = 1;

    g_destroyCount = 0;
    CHECK_EQ(Window_RemoveChildren(slot, 1), 1);
    // Two specials that matched a child are reset; the unmatched one is untouched.
    CHECK_EQ(win.reset234(), -1);
    CHECK_EQ(win.reset235(), -1);
    CHECK_EQ(win.reset236(), 999);
    // All 3 children destroyed (keepBacking=1; none are '@'-type here).
    CHECK_EQ(g_destroyCount, 3);
    // Scroll bookkeeping cleared (prev -> -1).
    CHECK_EQ(win.scrollCur(), 0);
    CHECK_EQ(win.contentHeight(), 0);
    CHECK_EQ(win.scrollOffset(), 0);
    CHECK_EQ(win.scrollPrev(), -1);
    CHECK_EQ(win.textBuffer(), 0);
}

TEST(GuiWindowMgmt, RemoveChildrenKeepsBackingWhenFlagClear) {
    ResetGuiState();
    int slot = MakeWindow(0, 0, 20, 20, 2);
    i32* list = WindowChildList(slot);
    // Make child[1] a window-backing ('@') widget.
    g_widgets[list[1]].type() = kTypeWindow;
    g_destroyCount = 0;
    CHECK_EQ(Window_RemoveChildren(slot, 0), 1); // keepBacking=0
    // Only the non-'@' child is destroyed.
    CHECK_EQ(g_destroyCount, 1);
}

TEST(GuiWindowMgmt, RemoveChildrenOutOfRangeOrFree) {
    ResetGuiState();
    CHECK_EQ(Window_RemoveChildren(200, 1), 0); // > 96
    CHECK_EQ(Window_RemoveChildren(3, 1), 0);   // free slot
}

TEST(GuiWindowMgmt, ScrollDownClampsToContent) {
    ResetGuiState();
    int slot = Window_Create(0, 0, 20, 10, 0); // h=10
    Window& win = g_windows[slot];
    win.contentHeight() = 100;
    win.scrollCur() = 0;
    win.scrollOffset() = 0;
    win.scrollExtraX() = 0;

    // amount>0, v8 = 0+0+10 = 10 < 100 -> scrollOffset += min(amount, 100-10=90).
    int ret = Window_Scroll(5, 30, slot);
    CHECK_EQ(win.scrollOffset(), 30);
    CHECK_EQ(win.scrollExtraX(), 5);            // delta accumulated
    CHECK_EQ(ret, 16 * 56 * slot);

    // Now v8 = 30+0+10 = 40 < 100 -> add min(80, 60) = 60 -> 90.
    Window_Scroll(0, 80, slot);
    CHECK_EQ(win.scrollOffset(), 90);
    // v8 = 90+0+10 = 100, not < 100 -> no change.
    Window_Scroll(0, 50, slot);
    CHECK_EQ(win.scrollOffset(), 90);
}

TEST(GuiWindowMgmt, ScrollUpClampsToZero) {
    ResetGuiState();
    int slot = Window_Create(0, 0, 20, 10, 0);
    Window& win = g_windows[slot];
    win.contentHeight() = 100;
    win.scrollCur() = 0;
    win.scrollOffset() = 50;     // scrolled down by 50

    // amount<0, scrollOffset+scrollCur = 50 > 0 -> floor = -scrollCur - offset = -50.
    Window_Scroll(0, -20, slot);
    CHECK_EQ(win.scrollOffset(), -20);
    win.scrollOffset() = 50;
    Window_Scroll(0, -200, slot);                // clamp to -50
    CHECK_EQ(win.scrollOffset(), -50);
}

TEST(GuiFormLifecycle, GetWindowId) {
    ResetGuiState();
    Form& f = g_forms[2];
    f.windowCount() = 3;
    f.windowId(0) = 10; f.windowId(1) = 11; f.windowId(2) = 12; f.windowId(3) = 13;
    CHECK_EQ(Form_GetWindowId(2, 0), 10);
    CHECK_EQ(Form_GetWindowId(2, 2), 12);
    CHECK_EQ(Form_GetWindowId(2, 3), 13);   // slot == count is in range (inclusive bound)
    CHECK_EQ(Form_GetWindowId(2, 4), -1);   // > count
    CHECK_EQ(Form_GetWindowId(2, -1), -1);  // < 0
}

TEST(GuiFormLifecycle, SetObjectsVisible) {
    ResetGuiState();
    Form& f = g_forms[1];
    f.valid() = 1;
    auto key = static_cast<guild::i32>(reinterpret_cast<std::intptr_t>(&f));

    // Two widgets keyed to this form, one keyed elsewhere, one free (type 0).
    g_widgets[3].type() = kTypeLabel; g_widgets[3].parentClip() = key;
    g_widgets[5].type() = kTypeEdit;  g_widgets[5].parentClip() = key;
    g_widgets[7].type() = kTypeLabel; g_widgets[7].parentClip() = key + 4; // other form
    g_widgets[9].type() = 0;          g_widgets[9].parentClip() = key;     // not in use

    Form_SetObjectsVisible(1, 0);   // hide==0 -> renderPtr = 1 (visible)
    CHECK_EQ(g_widgets[3].renderPtr(), 1);
    CHECK_EQ(g_widgets[5].renderPtr(), 1);
    CHECK_EQ(g_widgets[7].renderPtr(), 0); // other form untouched
    CHECK_EQ(g_widgets[9].renderPtr(), 0); // free slot untouched
    CHECK_EQ(f.objectsVisible(), 0);

    Form_SetObjectsVisible(1, 1);   // hide==1 -> renderPtr = 0
    CHECK_EQ(g_widgets[3].renderPtr(), 0);
    CHECK_EQ(f.objectsVisible(), 1);

    // Invalid form: no-op.
    Form& g = g_forms[8];
    g.valid() = 0;
    Form_SetObjectsVisible(8, 0);
    CHECK_EQ(g.objectsVisible(), 0);
}

TEST(GuiFormLifecycle, SetChildrenVisible) {
    ResetGuiState();
    Form& f = g_forms[1];
    f.valid() = 1;
    f.windowCount() = 2;
    int s0 = MakeWindow(0, 0, 10, 10, 2);
    int s1 = MakeWindow(0, 0, 10, 10, 1);
    f.windowId(0) = s0; f.windowId(1) = s1;

    Form_SetChildrenVisible(1, 0);  // hide==0 -> disabledA = 1
    for (int k = 0; k < 2; ++k) CHECK_EQ(g_widgets[WindowChildList(s0)[k]].disabledA(), 1);
    CHECK_EQ(g_widgets[WindowChildList(s1)[0]].disabledA(), 1);
    CHECK_EQ(f.childrenVisible(), 0);

    Form_SetChildrenVisible(1, 9);  // hide!=0 -> disabledA = 0
    CHECK_EQ(g_widgets[WindowChildList(s0)[0]].disabledA(), 0);
    CHECK_EQ(f.childrenVisible(), 9);
}

TEST(GuiFormLifecycle, RaiseWindowsDispatch) {
    ResetGuiState();
    Form& f = g_forms[4];
    f.windowCount() = 3;
    f.windowId(0) = 20; f.windowId(1) = 21; f.windowId(2) = 22;
    g_raiseCount = 0;
    Form_RaiseWindows(4);
    CHECK_EQ(g_raiseCount, 3);
    CHECK_EQ(g_lastRaised, 22);
}

TEST(GuiFormLifecycle, DestroyTearsDownAndFrees) {
    ResetGuiState();
    Form& f = g_forms[2];
    f.valid() = 1;
    f.windowCount() = 2;
    int s0 = Window_Create(0, 0, 10, 10, 0);
    int s1 = Window_Create(0, 0, 10, 10, 0);
    f.windowId(0) = s0; f.windowId(1) = s1;
    f.surfaceA() = 111; f.surfaceB() = 222;

    g_destroyCount = 0;
    Form_Destroy(2);
    CHECK_EQ(g_destroyCount, 2);          // both windows' backing widgets destroyed
    CHECK_EQ(f.valid(), 0);               // form marked free

    // Re-destroy is a no-op.
    g_destroyCount = 0;
    Form_Destroy(2);
    CHECK_EQ(g_destroyCount, 0);
}

TEST(GuiFormLifecycle, CenterAndPositionTopLevelOnly) {
    ResetGuiState();
    g_screenCenterX = 100; g_screenCenterY = 200;
    Form& f = g_forms[3];
    f.windowCount() = 2;
    int s0 = Window_Create(0, 0, 40, 20, 0); // w=40,h=20
    int s1 = Window_Create(0, 0, 60, 30, 0);
    f.windowId(0) = s0; f.windowId(1) = s1;
    // Make s1's backing a non-top-level (groupLink != 0) so it is skipped.
    g_widgets[g_windows[s1].backWidget()].groupLink() = 7;

    g_lbCount = 0;
    Form_CenterChildWindows(3);
    CHECK_EQ(g_lbCount, 1);                // only s0 centered
    CHECK_EQ(g_lbX, 100 - 40 / 2);         // 80
    CHECK_EQ(g_lbY, 200 - 20 / 2);         // 190
    CHECK_EQ(g_lbIdx, g_windows[s0].backWidget());

    // PositionChildWindows routes through Window_PositionCentered (also LayoutBounds).
    g_lbCount = 0;
    Form_PositionChildWindows(3, 0);       // mode 0 -> x=win.x, y=win.y for s0
    CHECK_EQ(g_lbCount, 1);
    CHECK_EQ(g_lbX, 0);
    CHECK_EQ(g_lbY, 0);
}

// End-to-end: build a small form with several windows and widgets, select/lookup,
// traverse the object<->window<->form hierarchy, and verify linkage consistency.
#include "gui/form.h"
#include "gui/object.h"
#include "gui/window.h"
#include "tests/framework/test.h"

#include <cstdint>

using namespace guild::gui;

namespace {
i32 HandleOf(void* p) { return static_cast<i32>(reinterpret_cast<std::intptr_t>(p)); }
} // namespace

TEST(GuiCoreE2E, BuildSelectTraverse) {
    ResetGuiState();

    // --- Build three windows and register them under form id 7 -----------------
    const int kFormId = 7;
    int win[3];
    win[0] = Window_Create(0,   0, 320, 200, 0);
    win[1] = Window_Create(50, 40, 120,  80, kWinFlagTextBuffer);
    win[2] = Window_Create(10, 10, 200, 100, 0);
    CHECK_EQ(win[0], 0);
    CHECK_EQ(win[1], 1);
    CHECK_EQ(win[2], 2);

    Form& form = g_forms[kFormId];
    form.windowCount() = 2;           // 3 slots (0,1,2); check is winSlot > count
    form.windowId(0) = win[0];
    form.windowId(1) = win[1];
    form.windowId(2) = win[2];

    // --- Populate each window with a few children ------------------------------
    // window 0: two plain children + one anim object
    int w0c0 = Object_AddToWindow(win[0], 5,  5, 100);
    int w0c1 = Object_AddToWindow(win[0], 5, 30, 101);
    int w0a  = Object_AddToWindow(win[0], 5, 60, 102);
    g_widgets[w0a].type() = kTypeAnim;
    static Object3D anim0; anim0 = Object3D{};
    anim0.flags() = kAnimFlag328; anim0.ptr328() = 0xAA55;
    SetWidgetData(w0a, &anim0);

    // window 1: an edit field
    int w1e = Object_AddToWindow(win[1], 2, 2, 200);
    g_widgets[w1e].type() = kTypeEdit;
    g_widgets[w1e].editText() = 0xED17;

    // window 2: a button
    int w2b = Object_AddToWindow(win[2], 8, 8, 300);
    g_widgets[w2b].type() = kTypeLabel;
    g_widgets[w2b].btnFlagA() = 1;
    g_widgets[w2b].value() = 1234;

    // --- Counts ---------------------------------------------------------------
    CHECK_EQ(g_windows[win[0]].objCount(), 3);
    CHECK_EQ(g_windows[win[1]].objCount(), 1);
    CHECK_EQ(g_windows[win[2]].objCount(), 1);

    // --- Select windows by id and look children up through the form -----------
    CHECK_EQ(Form_SelectWindow(kFormId, 0), 1);
    CHECK_EQ(g_currentWindowId, win[0]);
    CHECK_EQ(Form_GetObjectPtr(0, 0), w0c0);
    CHECK_EQ(Form_GetObjectPtr(1, 0), w0c1);
    CHECK_EQ(Form_GetObjectPtr(2, 0), w0a);
    // anim object data + anim ptr resolved relative to the current form
    CHECK_EQ(Form_GetObjectDataPtr(2, 0), 0xAA55);
    CHECK_EQ(Form_GetObjectAnimPtr(2, 0), HandleOf(&anim0));

    CHECK_EQ(Form_SelectWindow(kFormId, 1), 1);
    CHECK_EQ(g_currentWindowId, win[1]);
    CHECK_EQ(Form_GetObjectDataPtr(0, 1), 0xED17); // edit text

    CHECK_EQ(Form_SelectWindow(kFormId, 2), 1);
    CHECK_EQ(Form_GetObjectDataPtr(0, 2), 1234);   // button value

    // --- Traverse the full hierarchy and verify back-links --------------------
    for (int g = 0; g <= form.windowCount(); ++g) {
        int winId = form.windowId(g);
        Window& wn = g_windows[winId];
        CHECK(wn.enabled() != 0);
        // backing widget points back at this window and carries id = slot+1024
        Widget& bw = g_widgets[wn.backWidget()];
        CHECK_EQ((int)bw.type(), (int)kTypeWindow);
        CHECK_EQ(bw.id(), winId + 1024);
        CHECK_EQ(bw.ownerWindow(), winId);
        CHECK(WidgetData(wn.backWidget()) == &wn); // +12 -> Window* (side table)
        // every child reports this window as its owner and is reachable via the list
        i32* list = WindowChildList(winId);
        for (int k = 0; k < wn.objCount(); ++k) {
            int idx = list[k];
            CHECK_EQ(g_widgets[idx].ownerWindow(), winId);
            // GetChildObjectId(form, group, child) must agree with the raw list
            CHECK_EQ(Form_GetChildObjectId(kFormId, g, k), idx);
        }
    }

    // --- Tear down and verify slot recycling ----------------------------------
    int before = g_windowCounter;
    CHECK_EQ(Window_Destroy(win[1]), 1);
    CHECK_EQ(g_windows[win[1]].enabled(), 0);
    CHECK_EQ(g_windowCounter, before - 1);
    // recreating reuses the freed slot
    int re = Window_Create(0, 0, 5, 5, 0);
    CHECK_EQ(re, win[1]);
}

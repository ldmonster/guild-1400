// Unit tests for the guild::gui retained-mode core data model.
//   - Widget slot alloc / free / recycle (capacity 511)
//   - Window create / destroy (capacity 96), current-window caching
//   - Object_AddToWindow child list (capacity 384)
//   - Form_SelectWindow id resolution
//   - GetObjectPtr / GetObjectDataPtr / GetObjectAnimPtr incl. the +38 flag branches
#include "gui/form.h"
#include "gui/object.h"
#include "gui/window.h"
#include "tests/framework/test.h"

#include <cstdint>

using namespace guild::gui;

namespace {
void Reset() { ResetGuiState(); }

i32 HandleOf(void* p) {
    return static_cast<i32>(reinterpret_cast<std::intptr_t>(p));
}
} // namespace

TEST(GuiCore, WidgetAllocLinearAndRecycle) {
    Reset();
    int a = Widget_AllocSlot();
    int b = Widget_AllocSlot();
    int c = Widget_AllocSlot();
    CHECK_EQ(a, 0);
    CHECK_EQ(b, 1);
    CHECK_EQ(c, 2);
    CHECK(g_widgets[a].inUse() != 0);
    CHECK_EQ(g_widgetHighWater, 2);

    // Free the middle slot; next alloc must reuse the lowest free index (1).
    Widget_FreeSlot(b);
    CHECK_EQ(g_widgets[b].inUse(), 0);
    int d = Widget_AllocSlot();
    CHECK_EQ(d, 1);
}

TEST(GuiCore, WidgetAllocCapacity511) {
    Reset();
    for (int i = 0; i < kMaxWidgets; ++i)
        CHECK_EQ(Widget_AllocSlot(), i);
    // 512th allocation fails.
    CHECK_EQ(Widget_AllocSlot(), -1);
    CHECK_EQ(g_widgetHighWater, kMaxWidgets - 1);
}

TEST(GuiCore, WindowCreateAndFields) {
    Reset();
    int s = Window_Create(10, 20, 100, 60, 0);
    CHECK_EQ(s, 0);
    Window& win = g_windows[s];
    CHECK_EQ(win.x(), 10);
    CHECK_EQ(win.y(), 20);
    CHECK_EQ(win.w(), 100);
    CHECK_EQ(win.h(), 60);
    CHECK_EQ(win.margin0(), 4);
    CHECK_EQ(win.enabled(), 1);
    CHECK_EQ(win.objCount(), 0);
    CHECK(win.objListPtr() != 0);

    // Backing widget: type '@', id = slot+1024, dataPtr -> this Window.
    Widget& bw = g_widgets[win.backWidget()];
    CHECK_EQ((int)bw.type(), (int)kTypeWindow);
    CHECK_EQ(bw.id(), s + 1024);
    CHECK_EQ(bw.ownerWindow(), s);
    CHECK(WidgetData(win.backWidget()) == &win); // +12 -> Window* (side table)

    // Current-window caching.
    CHECK_EQ(g_currentWindowId, s);
    CHECK(g_currentWindow == &g_windows[s]);
}

TEST(GuiCore, WindowTextBufferFlag) {
    Reset();
    int s0 = Window_Create(0, 0, 10, 10, 0);
    CHECK_EQ(g_windows[s0].textBuffer(), 0); // no buffer without flag
    int s1 = Window_Create(0, 0, 10, 10, kWinFlagTextBuffer);
    CHECK(g_windows[s1].textBuffer() != 0);  // buffer allocated with flag
    Window_Destroy(s1);
    CHECK_EQ(g_windows[s1].textBuffer(), 0); // freed on destroy
}

TEST(GuiCore, WindowCreateCapacity96AndRecycle) {
    Reset();
    int last = -1;
    for (int i = 0; i < kMaxWindows; ++i) {
        last = Window_Create(0, 0, 1, 1, 0);
        CHECK_EQ(last, i);
    }
    CHECK_EQ(Window_Create(0, 0, 1, 1, 0), -1); // full

    // Free one and recreate -> reuses a freed slot.
    CHECK_EQ(Window_Destroy(5), 1);
    CHECK_EQ(g_windows[5].enabled(), 0);
    int again = Window_Create(0, 0, 1, 1, 0);
    CHECK_EQ(again, 5);
    (void)last;
}

TEST(GuiCore, AddChildAndCount) {
    Reset();
    int s = Window_Create(0, 0, 200, 200, 0);
    int c0 = Object_AddToWindow(s, 5, 7, 111);
    int c1 = Object_AddToWindow(s, 9, 3, 222);
    CHECK(c0 >= 0);
    CHECK(c1 >= 0);
    CHECK_EQ(g_windows[s].objCount(), 2);

    // The id list stores the child widget indices in order.
    i32* list = WindowChildList(s);
    CHECK_EQ(list[0], c0);
    CHECK_EQ(list[1], c1);

    // Child geometry = window origin + offset; dataPtr = gfx id.
    CHECK_EQ(g_widgets[c0].x(), 7);
    CHECK_EQ(g_widgets[c0].y(), 5);
    CHECK_EQ(g_widgets[c0].dataPtr(), 111);
    CHECK_EQ(g_widgets[c0].ownerWindow(), s);
}

TEST(GuiCore, AddChildCapacity384) {
    Reset();
    int s = Window_Create(0, 0, 50, 50, 0);
    for (int i = 0; i < kMaxChildren; ++i)
        CHECK(Object_AddToWindow(s, 0, 0, i) >= 0);
    CHECK_EQ(g_windows[s].objCount(), kMaxChildren);
    CHECK_EQ(Object_AddToWindow(s, 0, 0, 999), -1); // 385th rejected
}

TEST(GuiCore, SelectWindowResolvesId) {
    Reset();
    int w0 = Window_Create(0, 0, 10, 10, 0);
    int w1 = Window_Create(0, 0, 10, 10, 0);

    // Wire a form (the .form loader is deferred; we populate the table directly).
    Form& f = g_forms[3];
    f.windowCount() = 1;       // two slots: 0 and 1 (check is winSlot > count)
    f.windowId(0) = w0;
    f.windowId(1) = w1;

    CHECK_EQ(Form_SelectWindow(3, 1), 1);
    CHECK_EQ(g_currentFormId, 3);
    CHECK_EQ(g_currentWindowId, w1);
    CHECK(g_currentWindow == &g_windows[w1]);

    CHECK_EQ(Form_SelectWindow(3, 0), 1);
    CHECK_EQ(g_currentWindowId, w0);

    // Out-of-range slot is rejected.
    CHECK_EQ(Form_SelectWindow(3, 2), 0);
    CHECK_EQ(Form_SelectWindow(3, -1), 0);

    // formId 0 leaves current form unchanged.
    int prev = g_currentFormId;
    Form_SelectWindow(0, 0);
    CHECK_EQ(g_currentFormId, prev);
}

TEST(GuiCore, GetObjectPtrAndBounds) {
    Reset();
    int w = Window_Create(0, 0, 80, 80, 0);
    int c0 = Object_AddToWindow(w, 0, 0, 10);
    int c1 = Object_AddToWindow(w, 0, 0, 20);

    Form& f = g_forms[1];
    f.windowCount() = 0;
    f.windowId(0) = w;
    g_currentFormId = 1;

    CHECK_EQ(Form_GetObjectPtr(0, 0), c0);
    CHECK_EQ(Form_GetObjectPtr(1, 0), c1);
    // objCount()==2; localId<=2 is in-bounds (inclusive), localId==3 is out.
    CHECK_EQ(Form_GetObjectPtr(3, 0), -1);
}

// The +38 flag-bit branches of Object_GetDataPtr (via Form_GetObjectDataPtr).
TEST(GuiCore, DataPtrAnimFlag328) {
    Reset();
    int w = Window_Create(0, 0, 10, 10, 0);
    int c = Object_AddToWindow(w, 0, 0, 0);
    g_widgets[c].type() = kTypeAnim;

    static Object3D rec;
    rec = Object3D{};
    rec.flags() = kAnimFlag328;        // 0x10
    rec.ptr328() = 0x12345678;
    SetWidgetData(c, &rec);

    Form& f = g_forms[0];
    f.windowCount() = 0; f.windowId(0) = w; g_currentFormId = 0;
    CHECK_EQ(Form_GetObjectDataPtr(0, 0), 0x12345678); // returns *(rec+328)
}

TEST(GuiCore, DataPtrAnimFlagInline40) {
    Reset();
    int w = Window_Create(0, 0, 10, 10, 0);
    int c = Object_AddToWindow(w, 0, 0, 0);
    g_widgets[c].type() = kTypeAnim;

    static Object3D rec;
    rec = Object3D{};
    rec.flags() = kAnimFlagInline;     // 0x01
    SetWidgetData(c, &rec);

    Form& f = g_forms[0];
    f.windowCount() = 0; f.windowId(0) = w; g_currentFormId = 0;
    // 0x01 => returns address of rec+40 (a pointer, not a dereferenced value).
    CHECK_EQ(Form_GetObjectDataPtr(0, 0), HandleOf(&rec.inline40()));
}

TEST(GuiCore, DataPtrAnimFlag296) {
    Reset();
    int w = Window_Create(0, 0, 10, 10, 0);
    int c = Object_AddToWindow(w, 0, 0, 0);
    g_widgets[c].type() = kTypeAnim;

    static Object3D rec;
    rec = Object3D{};
    rec.flags() = kAnimFlag296;        // 0x02
    rec.ptr296() = 0x0BADF00D;
    SetWidgetData(c, &rec);

    Form& f = g_forms[0];
    f.windowCount() = 0; f.windowId(0) = w; g_currentFormId = 0;
    CHECK_EQ(Form_GetObjectDataPtr(0, 0), 0x0BADF00D); // *(rec+296)
}

TEST(GuiCore, DataPtrFlagPriority328BeforeInline) {
    Reset();
    int w = Window_Create(0, 0, 10, 10, 0);
    int c = Object_AddToWindow(w, 0, 0, 0);
    g_widgets[c].type() = kTypeAnim;

    static Object3D rec;
    rec = Object3D{};
    rec.flags() = kAnimFlag328 | kAnimFlagInline | kAnimFlag296; // all set
    rec.ptr328() = 0x55;
    SetWidgetData(c, &rec);

    Form& f = g_forms[0];
    f.windowCount() = 0; f.windowId(0) = w; g_currentFormId = 0;
    // 0x10 wins (checked first).
    CHECK_EQ(Form_GetObjectDataPtr(0, 0), 0x55);
}

TEST(GuiCore, DataPtrButtonValueAndEditAndDefault) {
    Reset();
    int w = Window_Create(0, 0, 10, 10, 0);
    int cBtn = Object_AddToWindow(w, 0, 0, 0);
    int cEdit = Object_AddToWindow(w, 0, 0, 0);
    int cNone = Object_AddToWindow(w, 0, 0, 0);

    // Button: btnFlagA(+68) nonzero -> returns value(+36) regardless of (non-A) type.
    g_widgets[cBtn].type() = kTypeLabel;
    g_widgets[cBtn].btnFlagA() = 1;
    g_widgets[cBtn].value() = 4242;

    // Edit field: type 'E', no button flag -> returns editText(+120).
    g_widgets[cEdit].type() = kTypeEdit;
    g_widgets[cEdit].editText() = 0x7777;

    // Plain object: no flags -> -1.
    g_widgets[cNone].type() = kTypeLabel;

    CHECK_EQ(Object_GetDataPtr(cBtn), 4242);
    CHECK_EQ(Object_GetDataPtr(cEdit), 0x7777);
    CHECK_EQ(Object_GetDataPtr(cNone), -1);
}

TEST(GuiCore, GetObjectAnimPtr) {
    Reset();
    int w = Window_Create(0, 0, 10, 10, 0);
    int cAnim = Object_AddToWindow(w, 0, 0, 0);
    int cPlain = Object_AddToWindow(w, 0, 0, 0);

    static Object3D rec;
    rec = Object3D{};
    g_widgets[cAnim].type() = kTypeAnim;
    SetWidgetData(cAnim, &rec);
    g_widgets[cPlain].type() = kTypeLabel;

    Form& f = g_forms[0];
    f.windowCount() = 0; f.windowId(0) = w; g_currentFormId = 0;

    // Type 'A' -> returns the +12 data handle (the 3D/anim record).
    CHECK_EQ(Form_GetObjectAnimPtr(0, 0), HandleOf(&rec));
    // Non-'A' -> 0.
    CHECK_EQ(Form_GetObjectAnimPtr(1, 0), 0);
}

TEST(GuiCore, GetChildObjectIdDirectWindow) {
    Reset();
    int w = Window_Create(0, 0, 10, 10, 0);
    int c0 = Object_AddToWindow(w, 0, 0, 0);
    int c1 = Object_AddToWindow(w, 0, 0, 0);
    // form == -1 -> group is a direct window slot.
    CHECK_EQ(Form_GetChildObjectId(-1, w, 0), c0);
    CHECK_EQ(Form_GetChildObjectId(-1, w, 1), c1);
    CHECK_EQ(Form_GetChildObjectId(-1, w, 5), -1); // out of range
}

// ===== Wave-11 hardening: out-of-range slots + pool exhaustion (ASAN/UBSAN) ==========

TEST(GuiCoreHarden, WindowDestroyOutOfRangeSlot) {
    Reset();
    // kMaxWindows == 96; valid slots are 0..95. The old bound (slot > kMaxWindows) let
    // slot==96 index g_windows[96] (OOB). Negatives must also be rejected.
    CHECK_EQ(Window_Destroy(96), 0);   // == kMaxWindows: was OOB, now rejected
    CHECK_EQ(Window_Destroy(95), 0);   // in range but free -> 0 (no crash)
    CHECK_EQ(Window_Destroy(-1), 0);
    CHECK_EQ(Window_Destroy(1000000), 0);
}

TEST(GuiCoreHarden, WindowCreateWhenWidgetPoolExhausted) {
    Reset();
    // Fill the entire widget pool, then create a window. Window_Create needs a backing
    // widget; AllocSlot returns -1, and the old code dereferenced g_widgets[-1] (OOB
    // write). The guard must make Window_Create fail cleanly instead.
    for (int i = 0; i < kMaxWidgets; ++i)
        CHECK(Widget_AllocSlot() >= 0);
    CHECK_EQ(Widget_AllocSlot(), -1); // pool full
    int s = Window_Create(0, 0, 10, 10, 0);
    CHECK_EQ(s, -1);                   // no backing widget -> safe failure (no OOB)
}

TEST(GuiCoreHarden, AddToWindowWhenWidgetPoolExhausted) {
    Reset();
    int w = Window_Create(0, 0, 100, 100, 0); // consumes one widget (the backing widget)
    CHECK(w >= 0);
    // Exhaust the rest of the widget pool.
    while (Widget_AllocSlot() >= 0) {}
    // Adding a child now fails (AllocSlot -1) without writing g_widgets[-1].
    CHECK_EQ(Object_AddToWindow(w, 0, 0, 0), -1);
}

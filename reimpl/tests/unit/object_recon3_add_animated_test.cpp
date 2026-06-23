// Golden-vector unit tests for VIBE_Object_AddAnimatedToWindow (gilde.exe 0x41af64),
// reconstructed in src/gui/object_add_animated.{h,cpp}.
//
// The tests drive the leaf with deterministic stubs for its two coupled edges (the object
// factory and the anim-scale call) and assert the recovered MODEL byte-for-byte against the
// values the original leaf writes: window-relative screen placement, child id-list linkage,
// child-count bump, clip-extent + render-pointer inheritance, content-height growth, and the
// bounds-check / "too many objects" / factory-failure exits.
#include "gui/object_add_animated.h"
#include "gui/object.h"
#include "gui/window.h"
#include "gui/widget_create.h"   // g_screenClipExt
#include "tests/framework/test.h"

#include <cstdint>

using namespace guild::gui;

// ---- Deterministic overrides of the two coupled edges (replace the weak defaults). -------
namespace {
struct FactoryCall { i16 x = 0, y = 0; int entity = 0; bool called = false; };
FactoryCall g_lastFactory;
int  g_factoryNextId   = -2;   // -2 => allocate a real slot; otherwise return this verbatim
i16  g_factorySeedH    = 0;    // height (word @+22) seeded into the allocated slot
int  g_applyScaleCalls = 0;
int  g_applyScaleLast  = -999;

void ResetEdges() {
    g_lastFactory = FactoryCall{};
    g_factoryNextId = -2;
    g_factorySeedH = 0;
    g_applyScaleCalls = 0;
    g_applyScaleLast = -999;
}
} // namespace

namespace guild::gui {
int AnimObjectFactory(i16 screenX, i16 screenY, int entityIdx) {
    g_lastFactory = FactoryCall{screenX, screenY, entityIdx, true};
    if (g_factoryNextId != -2)
        return g_factoryNextId;                 // forced failure / fixed id
    int idx = Widget_AllocSlot();
    if (idx == -1) return -1;
    Widget& obj = g_widgets[idx];
    obj.x() = screenX; obj.y() = screenY;
    obj.dataPtr() = entityIdx; obj.type() = kTypeAnim; obj.order() = 2;
    obj.h() = g_factorySeedH;                   // +22 (read via dword@+20 >> 16)
    return idx;
}
void ApplyAnimScaleEdge(int objId) { ++g_applyScaleCalls; g_applyScaleLast = objId; }
} // namespace guild::gui

namespace {
void Reset() { ResetGuiState(); ResetWidgetCreate(); ResetEdges(); }

// Open an enabled window and give its backing widget distinct render/clip pointers so the
// inheritance into the animated child is observable.
int OpenWinWithBacking(i16 x, i16 y, i16 w, i16 h, i32 render, i32 clip) {
    int win = Window_Create(x, y, w, h, 0);
    Widget& bw = g_widgets[g_windows[win].backWidget()];
    bw.renderPtr()  = render;   // +52
    bw.parentClip() = clip;     // +60
    return win;
}
} // namespace

// =======================================================================================
TEST(ObjectRecon3, AnimatedPlacementAndLinkage) {
    Reset();
    g_screenClipExt = 0x00C80190;          // dword_69FFBC: low=0x0190(400), high=0x00C8(200)
    g_factorySeedH  = 30;                  // object height

    int win = OpenWinWithBacking(10, 20, 200, 100, /*render*/0x1111, /*clip*/0x2222);
    CHECK_EQ(win, 0);
    g_windows[win].scrollCur() = 5;        // word[292] @+584

    int id = Object_AddAnimatedToWindow(/*x*/7, /*y*/9, win, /*entity*/42, /*animArg*/123);
    CHECK(id >= 0);

    // Factory invoked with window-relative screen coords: x + winX, y + winY - scrollCur.
    CHECK(g_lastFactory.called);
    CHECK_EQ((int)g_lastFactory.x, 7 + 10);            // NO -word[300] bias (animated variant)
    CHECK_EQ((int)g_lastFactory.y, 9 + 20 - 5);
    CHECK_EQ(g_lastFactory.entity, 42);

    Widget& obj = g_widgets[id];
    // Clip/zero fields the leaf writes itself.
    CHECK_EQ((int)obj.clipX0(), 0);                    // +28
    CHECK_EQ((int)obj.clipY0(), 0);                    // +32
    CHECK_EQ(obj.groupLink(), g_windows[win].backWidget()); // +44 parent back-link
    // Screen clip extent split: low word -> +34 (no named accessor), high word -> +30 (clipX1()).
    CHECK_EQ((int)(u16)obj.at<i16>(34), 0x0190);       // +34 = low word
    CHECK_EQ((int)(u16)obj.clipX1(), 0x00C8);          // +30 = high word
    // Inherited from the window backing widget.
    CHECK_EQ(obj.parentClip(), 0x2222);                // +60
    CHECK_EQ(obj.renderPtr(), 0x1111);                 // +52

    // Anim scale applied to the new object.
    CHECK_EQ(g_applyScaleCalls, 1);
    CHECK_EQ(g_applyScaleLast, id);

    // Child id-list + count.
    CHECK_EQ(WindowChildList(win)[0], id);
    CHECK_EQ((int)g_windows[win].objCount(), 1);

    // Content-height growth: obj.h() + y = 30 + 9 = 39 (> 0).
    CHECK_EQ((int)g_windows[win].contentHeight(), 30 + 9);
}

// Content-height only grows when the new bottom exceeds the current extent.
TEST(ObjectRecon3, ContentHeightGrowsMonotonically) {
    Reset();
    int win = OpenWinWithBacking(0, 0, 100, 100, 0, 0);
    g_windows[win].contentHeight() = 500;          // pre-existing tall extent

    g_factorySeedH = 10;
    int id = Object_AddAnimatedToWindow(0, 5, win, 1, 0);
    CHECK(id >= 0);
    // bottom = 10 + 5 = 15, not > 500 -> unchanged.
    CHECK_EQ((int)g_windows[win].contentHeight(), 500);

    // A taller object pushes it up.
    g_factorySeedH = 600;
    int id2 = Object_AddAnimatedToWindow(0, 50, win, 2, 0);
    CHECK(id2 >= 0);
    CHECK_EQ((int)g_windows[win].contentHeight(), 600 + 50);
}

// Disabled window -> -1, no factory call, no child appended.
TEST(ObjectRecon3, DisabledWindowReturnsMinusOne) {
    Reset();
    int win = OpenWinWithBacking(0, 0, 50, 50, 0, 0);
    g_windows[win].enabled() = 0;                  // disable it

    int id = Object_AddAnimatedToWindow(1, 1, win, 1, 0);
    CHECK_EQ(id, -1);
    CHECK(!g_lastFactory.called);
    CHECK_EQ((int)g_windows[win].objCount(), 0);
}

// Full window (>= 384 children) -> -1 before any factory call.
TEST(ObjectRecon3, TooManyObjectsReturnsMinusOne) {
    Reset();
    int win = OpenWinWithBacking(0, 0, 50, 50, 0, 0);
    g_windows[win].objCount() = (i16)kMaxChildren; // 384

    int id = Object_AddAnimatedToWindow(1, 1, win, 1, 0);
    CHECK_EQ(id, -1);
    CHECK(!g_lastFactory.called);
    CHECK_EQ((int)g_windows[win].objCount(), (int)kMaxChildren); // unchanged
}

// Factory failure (id == -1): the -1 is still stored into the child list at the current
// count, then returned; no z-order/scale/count side effects occur.
TEST(ObjectRecon3, FactoryFailureStoresMinusOneAndReturns) {
    Reset();
    int win = OpenWinWithBacking(0, 0, 50, 50, 0, 0);
    g_factoryNextId = -1;                          // force the factory to fail

    int beforeCount = g_windows[win].objCount();
    int id = Object_AddAnimatedToWindow(3, 4, win, 9, 0);
    CHECK_EQ(id, -1);
    CHECK(g_lastFactory.called);                   // factory WAS called
    CHECK_EQ(WindowChildList(win)[beforeCount], -1); // -1 stored at count slot
    CHECK_EQ((int)g_windows[win].objCount(), beforeCount); // count NOT bumped
    CHECK_EQ(g_applyScaleCalls, 0);                // no scale on failure
}

// Two successive additions occupy consecutive child slots and bump the count each time.
TEST(ObjectRecon3, SequentialAdditionsAppend) {
    Reset();
    int win = OpenWinWithBacking(2, 3, 80, 80, 0, 0);
    g_factorySeedH = 4;

    int a = Object_AddAnimatedToWindow(0, 0, win, 100, 0);
    int b = Object_AddAnimatedToWindow(0, 0, win, 200, 0);
    CHECK(a >= 0 && b >= 0 && a != b);
    CHECK_EQ(WindowChildList(win)[0], a);
    CHECK_EQ(WindowChildList(win)[1], b);
    CHECK_EQ((int)g_windows[win].objCount(), 2);
    CHECK_EQ(g_applyScaleCalls, 2);
}

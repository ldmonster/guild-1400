// Golden tests for ui_recon3 — VIBE_Widget_Free_Thunk (gilde.exe 0x410178).
//
// The thunk is `call VIBE_Widget_DestroyByType (0x414f98) ; retn`: a pure
// pass-through. These tests pin that behavior by providing a STRONG override of
// guild::gui::Widget_DestroyByType (the existing definition in
// gui/form_lifecycle.cpp is `__attribute__((weak))`, so this override wins at
// link time and lets us observe the forwarded arguments) and checking that:
//   * the three arguments arrive unchanged and in order, and
//   * the thunk returns 0 (the value the binary's callers discard).
//
// Headless, no game assets.

#include "tests/framework/test.h"
#include "play/ui_recon3_widget.h"

namespace {
int g_lastWidgetIdx = -999;
int g_lastA2        = -999;
int g_lastA3        = -999;
int g_callCount     = 0;
}

// Strong override of the weak placeholder in gui/form_lifecycle.cpp. The thunk
// forwards into this; we record the arguments to verify the pass-through is 1:1.
namespace guild::gui {
void Widget_DestroyByType(int widgetIdx, int a2, int a3) {
    g_lastWidgetIdx = widgetIdx;
    g_lastA2        = a2;
    g_lastA3        = a3;
    ++g_callCount;
}
}

using guild::play::VIBE_Widget_Free_Thunk;

TEST(UiRecon3, FreeThunkForwardsArgsVerbatim) {
    g_callCount = 0;
    int r = VIBE_Widget_Free_Thunk(7, 42, 1000);
    CHECK_EQ(g_callCount, 1);            // exactly one forwarded call
    CHECK_EQ(g_lastWidgetIdx, 7);        // arg0 -> widgetIdx
    CHECK_EQ(g_lastA2, 42);              // arg1 -> a2
    CHECK_EQ(g_lastA3, 1000);            // arg2 -> a3
    CHECK_EQ(r, 0);                      // 0x41017d retn — callers discard
}

TEST(UiRecon3, FreeThunkPreservesNegativeAndZeroArgs) {
    g_callCount = 0;
    int r = VIBE_Widget_Free_Thunk(-1, 0, -2147483647 - 1 /* INT_MIN */);
    CHECK_EQ(g_callCount, 1);
    CHECK_EQ(g_lastWidgetIdx, -1);
    CHECK_EQ(g_lastA2, 0);
    CHECK_EQ(g_lastA3, (-2147483647 - 1));
    CHECK_EQ(r, 0);
}

TEST(UiRecon3, FreeThunkEachCallForwardsAgain) {
    g_callCount = 0;
    VIBE_Widget_Free_Thunk(1, 2, 3);
    VIBE_Widget_Free_Thunk(4, 5, 6);
    CHECK_EQ(g_callCount, 2);           // no caching / dedup — one forward per call
    CHECK_EQ(g_lastWidgetIdx, 4);
    CHECK_EQ(g_lastA2, 5);
    CHECK_EQ(g_lastA3, 6);
}

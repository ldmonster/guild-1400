#include "test.h"
#include "gui/hud_drag.h"
#include "gui/object.h"

using namespace guild::gui;

// ---------------------------------------------------------------------------
// VIBE_Hud_Enable/DisableDragMode @0x595b8c / 0x595b80.
// ---------------------------------------------------------------------------
TEST(GuiHudDrag, DragModeFlag) {
    g_hudDragMode = 0;
    Hud_EnableDragMode();
    CHECK_EQ(g_hudDragMode, 1);
    Hud_DisableDragMode();
    CHECK_EQ(g_hudDragMode, 0);
}

// Helper: light a widget so it belongs to form `formIndex` with a given type tag.
static void makeWidget(int idx, int formIndex, guild::u8 type) {
    Widget& w = g_widgets[idx];
    w = Widget{};
    w.type() = type;
    w.parentClip() = formIndex + 1; // +60 form link model (nonzero pointer)
    w.renderPtr() = 0;              // +52 not yet lit
}

// ---------------------------------------------------------------------------
// VIBE_Hud_ToggleObjectHighlight @0x4bd008 — turning ON lights only widgets that
// belong to an ACTIVE form, are typed, and not already lit; records their slots.
// ---------------------------------------------------------------------------
TEST(GuiHudDrag, HighlightOnSelectsActiveFormWidgets) {
    ResetWidgets();
    ResetHudHighlight();

    // Form 2 is active; form 3 is not.
    g_formActiveA[2] = 1; g_formActiveB[2] = 1;
    g_formActiveA[3] = 1; g_formActiveB[3] = 0; // B view clear -> inactive

    makeWidget(5,  2, kTypeAnim);   // active form, typed, unlit  -> lit
    makeWidget(7,  2, kTypeLabel);  // active form, typed, unlit  -> lit
    makeWidget(9,  2, 0);           // type byte 0                -> skipped
    makeWidget(11, 3, kTypeAnim);   // form 3 inactive            -> skipped
    g_widgets[13] = Widget{}; g_widgets[13].type() = kTypeAnim;
    g_widgets[13].parentClip() = 3; // form 2 link, but pre-lit:
    g_widgets[13].renderPtr() = 1;  // already lit                -> skipped

    Hud_ToggleObjectHighlight(1);

    CHECK_EQ(g_highlightCount, 2);
    // Recorded in scan order (widget index 5 before 7).
    CHECK_EQ(g_highlightList[0], 5);
    CHECK_EQ(g_highlightList[1], 7);
    CHECK_EQ(g_widgets[5].renderPtr(), 1);
    CHECK_EQ(g_widgets[7].renderPtr(), 1);
    CHECK_EQ(g_widgets[9].renderPtr(), 0);
    CHECK_EQ(g_widgets[11].renderPtr(), 0);
}

// ---------------------------------------------------------------------------
// Turning OFF replays the remembered list and clears each widget's lit flag.
// ---------------------------------------------------------------------------
TEST(GuiHudDrag, HighlightOffClearsRememberedWidgets) {
    ResetWidgets();
    ResetHudHighlight();

    g_formActiveA[1] = 1; g_formActiveB[1] = 1;
    makeWidget(2, 1, kTypeAnim);
    makeWidget(4, 1, kTypeAnim);
    Hud_ToggleObjectHighlight(1);
    CHECK_EQ(g_highlightCount, 2);
    CHECK_EQ(g_widgets[2].renderPtr(), 1);
    CHECK_EQ(g_widgets[4].renderPtr(), 1);

    int ret = Hud_ToggleObjectHighlight(0);
    CHECK_EQ(ret, 2);                  // counted up to the remembered count
    // The original does NOT reset dword_631E5C on the OFF pass: g_highlightCount = v1
    // where v1 was the initial count, so it stays equal to the previous value.
    CHECK_EQ(g_highlightCount, 2);
    CHECK_EQ(g_widgets[2].renderPtr(), 0);
    CHECK_EQ(g_widgets[4].renderPtr(), 0);
}

// ---------------------------------------------------------------------------
// OFF with an empty list is a no-op (returns the passed-in `on` == 0).
// ---------------------------------------------------------------------------
TEST(GuiHudDrag, HighlightOffEmptyNoOp) {
    ResetWidgets();
    ResetHudHighlight();
    int ret = Hud_ToggleObjectHighlight(0);
    CHECK_EQ(ret, 0);
    CHECK_EQ(g_highlightCount, 0);
}

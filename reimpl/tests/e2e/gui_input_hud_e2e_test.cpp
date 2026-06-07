#include "test.h"
#include "gui/input_state.h"
#include "gui/hud_drag.h"
#include "gui/object.h"

using namespace guild::gui;

// End-to-end: a HUD interaction frame across the input-state and HUD-drag modules.
//
// Flow:
//   1. The DirectInput latch fills the button block (here we set it directly).
//   2. A left-button press starts drag mode and lights the owned-object highlight.
//   3. SaveMouseButtonSnapshot rotates the double-click bookkeeping.
//   4. The per-frame ClearMouseButtonFlags consumes the click edges.
//   5. Releasing drag mode clears the highlight again.
TEST(GuiInputHudE2E, FrameClickDragHighlightRelease) {
    ResetMouseInput();
    ResetWidgets();
    ResetHudHighlight();
    g_inputRawMode = 1; // raw input mode

    // --- 1. Latch: cursor clamp is open while in-world, buttons start down. ---
    g_cursorClampX0 = 5; g_cursorClampY0 = 6; g_cursorClampX1 = 640; g_cursorClampY1 = 480;
    Input_SwapCursorClampState(0); // park & open wide
    CHECK_EQ(g_cursorClampX1, -32000);
    CHECK_EQ(g_mouseInput.clampX1(), 640);

    // Left button edge + held set by the poll path.
    g_mouseInput.edgeBit01() = 1;
    g_mouseInput.heldBit01() = 1;

    // --- 2. Press starts a drag and lights the highlight on the active form. ---
    Hud_EnableDragMode();
    CHECK_EQ(g_hudDragMode, 1);

    g_formActiveA[0] = 1; g_formActiveB[0] = 1;
    for (int i = 0; i < 3; ++i) {
        g_widgets[i] = Widget{};
        g_widgets[i].type() = kTypeAnim;
        g_widgets[i].parentClip() = 0 + 1; // belongs to form 0
    }
    Hud_ToggleObjectHighlight(1);
    CHECK_EQ(g_highlightCount, 3);
    CHECK_EQ(g_widgets[0].renderPtr(), 1);
    CHECK_EQ(g_widgets[2].renderPtr(), 1);

    // --- 3. Double-click bookkeeping rotates. ---
    g_mouseInput.sc6721D0() = 100;
    g_mouseInput.sc6721E4() = 200;
    Input_SaveMouseButtonSnapshot();
    CHECK_EQ(g_mouseInput.sc6721D0(), 200);
    CHECK_EQ(g_mouseInput.sc6721E4(), 100);

    // --- 4. Per-frame edge consume. ---
    Input_ClearMouseButtonFlags();
    CHECK_EQ(g_mouseInput.edgeBit01(), 0);
    CHECK_EQ(g_mouseInput.heldBit01(), 0);

    // --- 5. Release: drag off, highlight cleared, clamp restored. ---
    Hud_DisableDragMode();
    CHECK_EQ(g_hudDragMode, 0);

    Hud_ToggleObjectHighlight(0);
    CHECK_EQ(g_widgets[0].renderPtr(), 0);
    CHECK_EQ(g_widgets[1].renderPtr(), 0);
    CHECK_EQ(g_widgets[2].renderPtr(), 0);

    Input_SwapCursorClampState(1); // restore the parked clamp
    CHECK_EQ(g_cursorClampX0, 5);
    CHECK_EQ(g_cursorClampX1, 640);
    CHECK_EQ(g_cursorClampY1, 480);
}

// A targeted mask-clear flow: raw vs cooked branches consume different pairs.
TEST(GuiInputHudE2E, MaskClearBranchesDiffer) {
    ResetMouseInput();
    MouseInputState& s = g_mouseInput;

    // RAW: clearing mask 0x08 hits the 0x08 (edge 672228 / held 67218C) pair.
    g_inputRawMode = 1;
    s.edgeBit08() = s.heldBit08() = 1;
    Input_ClearMouseButtonsByMask(0x08);
    CHECK_EQ(s.edgeBit08(), 0);
    CHECK_EQ(s.heldBit08(), 0);

    // COOKED: clearing mask 0x08 instead hits the 0x01 (edge 67223C / held 6721A0) pair.
    g_inputRawMode = 0;
    s.edgeBit01() = s.heldBit01() = 1;
    s.edgeBit08() = s.heldBit08() = 1;
    Input_ClearMouseButtonsByMask(0x08);
    CHECK_EQ(s.edgeBit01(), 0);   // cooked branch cleared the 0x01 pair
    CHECK_EQ(s.heldBit01(), 0);
    CHECK_EQ(s.edgeBit08(), 1);   // ...and left the 0x08 pair alone
    CHECK_EQ(s.heldBit08(), 1);
}

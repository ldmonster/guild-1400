#include "test.h"
#include "gui/input_state.h"

using namespace guild::gui;

// ---------------------------------------------------------------------------
// VIBE_Input_SetWheelBase @0x40c870 — eax = a1 + 256.
// ---------------------------------------------------------------------------
TEST(GuiInputState, SetWheelBaseAddsOffset) {
    ResetMouseInput();
    CHECK_EQ(Input_SetWheelBase(0), 256);
    CHECK_EQ(g_wheelBase, 256);
    CHECK_EQ(Input_SetWheelBase(-256), 0);
    CHECK_EQ(g_wheelBase, 0);
    CHECK_EQ(Input_SetWheelBase(1000), 1256);
}

// ---------------------------------------------------------------------------
// VIBE_Input_ResetMouseButtonState @0x40c87c — clears the full flag set.
// ---------------------------------------------------------------------------
TEST(GuiInputState, ResetClearsEveryFlag) {
    ResetMouseInput();
    MouseInputState& s = g_mouseInput;
    // Set every flag the reset touches to a nonzero sentinel.
    s.edgeBit01() = s.edgeBit02() = s.edgeBit04() = s.edgeBit08() = 7;
    s.edgeBit20() = 7;
    s.heldBit01() = s.heldBit02() = s.heldBit04() = s.heldBit08() = 7;
    s.heldBit20() = 7;
    s.w672188() = s.w672224() = s.w672238() = 7;
    s.sc6721D4() = s.sc6721D8() = s.sc6721DC() = 7;
    s.sc6721E4() = s.sc6721E8() = s.sc6721F0() = s.sc6721EC() = 7;
    s.at<int>(0x67219C) = 7;

    Input_ResetMouseButtonState();

    CHECK_EQ(s.edgeBit01(), 0);
    CHECK_EQ(s.edgeBit02(), 0);
    CHECK_EQ(s.edgeBit04(), 0);
    CHECK_EQ(s.edgeBit08(), 0);
    CHECK_EQ(s.edgeBit20(), 0);
    CHECK_EQ(s.heldBit01(), 0);
    CHECK_EQ(s.heldBit02(), 0);
    CHECK_EQ(s.heldBit04(), 0);
    CHECK_EQ(s.heldBit08(), 0);
    CHECK_EQ(s.heldBit20(), 0);
    CHECK_EQ(s.w672238(), 0);
    CHECK_EQ(s.at<int>(0x67219C), 0);
    CHECK_EQ(s.sc6721EC(), 0);
}

// ---------------------------------------------------------------------------
// VIBE_Input_ClearMouseButtonFlags @0x40c900 — clears the four primary pairs +
// the scene-pick word, but NOT the 0x10/0x20 held/edge pairs.
// ---------------------------------------------------------------------------
TEST(GuiInputState, ClearFlagsLeavesBit10And20) {
    ResetMouseInput();
    MouseInputState& s = g_mouseInput;
    s.edgeBit01() = s.edgeBit02() = s.edgeBit04() = s.edgeBit08() = 1;
    s.heldBit01() = s.heldBit02() = s.heldBit04() = 1;
    s.w672238() = 1;
    // Bits 0x08 / 0x10 / 0x20 are NOT cleared by ClearMouseButtonFlags (it only
    // clears the 0x01/0x02/0x04 pairs + the scene-pick / aux words).
    s.edgeBit10() = s.heldBit10() = 1;
    s.edgeBit20() = s.heldBit20() = 1;

    Input_ClearMouseButtonFlags();

    CHECK_EQ(s.edgeBit01(), 0);
    CHECK_EQ(s.edgeBit02(), 0);
    CHECK_EQ(s.edgeBit04(), 0);
    CHECK_EQ(s.w672238(), 0);
    // Untouched:
    CHECK_EQ(s.edgeBit08(), 1); // dword_672228 is NOT in the clear list
    CHECK_EQ(s.edgeBit10(), 1);
    CHECK_EQ(s.heldBit10(), 1);
    CHECK_EQ(s.edgeBit20(), 1);
    CHECK_EQ(s.heldBit20(), 1);
}

// ---------------------------------------------------------------------------
// VIBE_Input_ClearMouseButtonsByMask @0x40dca8 — raw branch (dword_62D0E4 != 0):
// each bit maps to a "matching" (edge,held) pair.
// ---------------------------------------------------------------------------
TEST(GuiInputState, ClearByMaskRawBranch) {
    ResetMouseInput();
    g_inputRawMode = 1; // dword_62D0E4 != 0
    MouseInputState& s = g_mouseInput;
    // Pre-set all pairs.
    s.edgeBit01() = s.heldBit01() = 1;
    s.edgeBit02() = s.heldBit02() = 1;
    s.edgeBit04() = s.heldBit04() = 1;
    s.edgeBit08() = s.heldBit08() = 1;
    s.edgeBit10() = s.heldBit10() = 1;
    s.edgeBit20() = s.heldBit20() = 1;

    // Clear only bits 0x01 and 0x10.
    CHECK_EQ(Input_ClearMouseButtonsByMask(0x11), 0x11);

    CHECK_EQ(s.edgeBit01(), 0); CHECK_EQ(s.heldBit01(), 0);
    CHECK_EQ(s.edgeBit10(), 0); CHECK_EQ(s.heldBit10(), 0);
    // Others untouched.
    CHECK_EQ(s.edgeBit02(), 1); CHECK_EQ(s.heldBit02(), 1);
    CHECK_EQ(s.edgeBit04(), 1);
    CHECK_EQ(s.edgeBit08(), 1);
    CHECK_EQ(s.edgeBit20(), 1); CHECK_EQ(s.heldBit20(), 1);
}

// ---------------------------------------------------------------------------
// Cooked branch (dword_62D0E4 == 0): the mask bits remap to a SHIFTED set of
// pairs (bit 1 -> 0x08 pair, bit 2 -> 0x10 pair, ...).
// ---------------------------------------------------------------------------
TEST(GuiInputState, ClearByMaskCookedBranch) {
    ResetMouseInput();
    g_inputRawMode = 0; // dword_62D0E4 == 0 -> cooked branch
    MouseInputState& s = g_mouseInput;
    s.edgeBit08() = s.heldBit08() = 1; // bit 1 targets this pair
    s.edgeBit04() = s.heldBit04() = 1; // bit 0x20 targets this pair

    // bit 1 clears the 0x08 pair; bit 0x20 clears the 0x04 pair.
    Input_ClearMouseButtonsByMask(0x21);

    CHECK_EQ(s.edgeBit08(), 0); CHECK_EQ(s.heldBit08(), 0);
    CHECK_EQ(s.edgeBit04(), 0); CHECK_EQ(s.heldBit04(), 0);
}

// ---------------------------------------------------------------------------
// VIBE_Input_SwapCursorClampState @0x40dc2c — park / restore the clamp window.
// ---------------------------------------------------------------------------
TEST(GuiInputState, SwapCursorClampParkAndRestore) {
    ResetMouseInput();
    g_cursorClampX0 = 10;  g_cursorClampY0 = 20;
    g_cursorClampX1 = 600; g_cursorClampY1 = 400;

    // Park (a1 == 0): live clamp opens wide, saved slots hold the old values.
    int ret0 = Input_SwapCursorClampState(0);
    CHECK_EQ(ret0, 400);            // returns old y-max
    CHECK_EQ(g_cursorClampX0, 32000);
    CHECK_EQ(g_cursorClampY0, 32000);
    CHECK_EQ(g_cursorClampX1, -32000);
    CHECK_EQ(g_cursorClampY1, -32000);
    CHECK_EQ(g_mouseInput.clampX0(), 10);
    CHECK_EQ(g_mouseInput.clampY0(), 20);
    CHECK_EQ(g_mouseInput.clampX1(), 600);
    CHECK_EQ(g_mouseInput.clampY1(), 400);

    // Restore (a1 != 0): the saved window comes back.
    int ret1 = Input_SwapCursorClampState(1);
    CHECK_EQ(ret1, 400);
    CHECK_EQ(g_cursorClampX0, 10);
    CHECK_EQ(g_cursorClampY0, 20);
    CHECK_EQ(g_cursorClampX1, 600);
    CHECK_EQ(g_cursorClampY1, 400);
}

// ---------------------------------------------------------------------------
// VIBE_Input_SaveMouseButtonSnapshot @0x40d338 — rotation of the double-click
// bookkeeping words; no-op when raw mode is off.
// ---------------------------------------------------------------------------
TEST(GuiInputState, SnapshotNoOpWhenCooked) {
    ResetMouseInput();
    g_inputRawMode = 0;
    g_mouseInput.sc6721D0() = 11;
    g_mouseInput.sc6721E4() = 22;
    Input_SaveMouseButtonSnapshot();
    CHECK_EQ(g_mouseInput.sc6721D0(), 11); // unchanged
    CHECK_EQ(g_mouseInput.sc6721E4(), 22);
}

TEST(GuiInputState, SnapshotRotatesWords) {
    ResetMouseInput();
    g_inputRawMode = 1;
    MouseInputState& s = g_mouseInput;
    s.sc6721D0() = 1; // old D0
    s.sc6721DC() = 2; // old DC
    s.sc6721E4() = 3; // E4
    s.sc6721F0() = 4; // F0

    Input_SaveMouseButtonSnapshot();

    CHECK_EQ(s.sc6721D0(), 3); // D0 = old E4
    CHECK_EQ(s.sc6721DC(), 4); // DC = old F0
    CHECK_EQ(s.sc6721F0(), 2); // F0 = old DC (snapshot)
    CHECK_EQ(s.sc6721E4(), 1); // E4 = old D0 (snapshot)
}

// ---------------------------------------------------------------------------
// Struct layout sanity — accessor offsets must hit the original addresses.
// ---------------------------------------------------------------------------
TEST(GuiInputState, BlockLayoutOffsets) {
    ResetMouseInput();
    MouseInputState& s = g_mouseInput;
    auto off = [&](void* p) {
        return static_cast<int>(reinterpret_cast<char*>(p) - reinterpret_cast<char*>(s.raw)) + kInputStateBase;
    };
    CHECK_EQ(off(&s.heldBit10()), 0x672180);
    CHECK_EQ(off(&s.heldBit20()), 0x672184);
    CHECK_EQ(off(&s.heldBit08()), 0x67218C);
    CHECK_EQ(off(&s.edgeBit20()), 0x672220);
    CHECK_EQ(off(&s.edgeBit08()), 0x672228);
    CHECK_EQ(off(&s.edgeBit01()), 0x67223C);
    CHECK_EQ(off(&s.clampY1()),   0x672270);
}

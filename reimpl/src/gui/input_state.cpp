#include "gui/input_state.h"

namespace guild::gui {

MouseInputState g_mouseInput; // dword_672174 region

i32 g_inputRawMode  = 0; // dword_62D0E4
i32 g_wheelBase     = 0; // dword_62D0B8

i32 g_cursorClampX0 = 0; // dword_62D0C4
i32 g_cursorClampY0 = 0; // dword_62D0C8
i32 g_cursorClampX1 = 0; // dword_62D0CC
i32 g_cursorClampY1 = 0; // dword_62D0D0

void ResetMouseInput() {
    std::memset(g_mouseInput.raw, 0, sizeof(g_mouseInput.raw));
    g_inputRawMode = 0;
    g_wheelBase = 0;
    g_cursorClampX0 = g_cursorClampY0 = g_cursorClampX1 = g_cursorClampY1 = 0;
}

// gilde.exe 0x40c87c — VIBE_Input_ResetMouseButtonState.
// Zeroes the full edge/held flag set plus the auxiliary state words.  The original
// writes 21 dwords explicitly; reproduced here in the same order.
void Input_ResetMouseButtonState() {
    MouseInputState& s = g_mouseInput;
    s.edgeBit08() = 0; // dword_672228
    s.heldBit08() = 0; // dword_67218C
    s.sc6721DC() = 0;  // dword_6721DC
    s.edgeBit20() = 0; // dword_672220
    s.heldBit20() = 0; // dword_672184
    s.sc6721D4() = 0;  // dword_6721D4
    s.w672224() = 0;   // dword_672224
    s.w672188() = 0;   // dword_672188
    s.sc6721D8() = 0;  // dword_6721D8
    s.edgeBit01() = 0; // dword_67223C
    s.heldBit01() = 0; // dword_6721A0
    s.sc6721F0() = 0;  // dword_6721F0
    s.edgeBit02() = 0; // dword_672230
    s.heldBit02() = 0; // dword_672194
    s.sc6721E4() = 0;  // dword_6721E4
    s.edgeBit04() = 0; // dword_672234
    s.heldBit04() = 0; // dword_672198
    s.sc6721E8() = 0;  // dword_6721E8
    s.w672238() = 0;   // dword_672238
    s.at<i32>(0x67219C) = 0; // dword_67219C
    s.sc6721EC() = 0;  // dword_6721EC
}

// gilde.exe 0x40c900 — VIBE_Input_ClearMouseButtonFlags.
// Clears the four primary (edge,held) pairs + the scene-pick / aux words.
void Input_ClearMouseButtonFlags() {
    MouseInputState& s = g_mouseInput;
    s.edgeBit01() = 0; // dword_67223C
    s.heldBit01() = 0; // dword_6721A0
    s.sc6721F0() = 0;  // dword_6721F0
    s.edgeBit02() = 0; // dword_672230
    s.heldBit02() = 0; // dword_672194
    s.sc6721E4() = 0;  // dword_6721E4
    s.edgeBit04() = 0; // dword_672234
    s.heldBit04() = 0; // dword_672198
    s.sc6721E8() = 0;  // dword_6721E8
    s.w672238() = 0;   // dword_672238
    s.at<i32>(0x67219C) = 0; // dword_67219C
    s.sc6721EC() = 0;  // dword_6721EC
}

// gilde.exe 0x40c870 — VIBE_Input_SetWheelBase.
i32 Input_SetWheelBase(i32 wheel) {
    i32 result = wheel + 256;
    g_wheelBase = result; // dword_62D0B8
    return result;
}

// gilde.exe 0x40dca8 — VIBE_Input_ClearMouseButtonsByMask.
u8 Input_ClearMouseButtonsByMask(u8 mask) {
    MouseInputState& s = g_mouseInput;
    if (g_inputRawMode) {
        if (mask & 1)    { s.edgeBit01() = 0; s.heldBit01() = 0; } // 67223C / 6721A0
        if (mask & 2)    { s.edgeBit02() = 0; s.heldBit02() = 0; } // 672230 / 672194
        if (mask & 4)    { s.edgeBit04() = 0; s.heldBit04() = 0; } // 672234 / 672198
        if (mask & 8)    { s.edgeBit08() = 0; s.heldBit08() = 0; } // 672228 / 67218C
        if (mask & 0x10) { s.edgeBit10() = 0; s.heldBit10() = 0; } // 67221C / 672180
        if (mask & 0x20) { s.edgeBit20() = 0; s.heldBit20() = 0; } // 672220 / 672184
    } else {
        // dword_62D0E4 == 0 here, so the two writes for bit 1 store 0 (the original
        // assigns dword_62D0E4 itself).
        if (mask & 1)    { s.edgeBit08() = g_inputRawMode; s.heldBit08() = g_inputRawMode; } // 672228 / 67218C
        if (mask & 2)    { s.edgeBit10() = 0; s.heldBit10() = 0; } // 67221C / 672180
        if (mask & 4)    { s.edgeBit20() = 0; s.heldBit20() = 0; } // 672220 / 672184
        if (mask & 8)    { s.edgeBit01() = 0; s.heldBit01() = 0; } // 67223C / 6721A0
        if (mask & 0x10) { s.edgeBit02() = 0; s.heldBit02() = 0; } // 672230 / 672194
        if (mask & 0x20) { s.edgeBit04() = 0; s.heldBit04() = 0; } // 672234 / 672198
    }
    return mask;
}

// gilde.exe 0x40dc2c — VIBE_Input_SwapCursorClampState.
i32 Input_SwapCursorClampState(i32 restore) {
    MouseInputState& s = g_mouseInput;
    if (restore) {
        // a1 != 0: pull the saved clamp back into the live window.
        g_cursorClampX0 = s.clampX0(); // dword_62D0C4 = dword_672264
        g_cursorClampY0 = s.clampY0(); // dword_62D0C8 = dword_672268
        g_cursorClampX1 = s.clampX1(); // dword_62D0CC = dword_67226C
        g_cursorClampY1 = s.clampY1(); // dword_62D0D0 = dword_672270
        return s.clampY1();
    }
    // a1 == 0: park the live clamp into the saved slots and open it wide.
    s.clampX0() = g_cursorClampX0; g_cursorClampX0 = 32000;
    s.clampY0() = g_cursorClampY0; g_cursorClampY0 = 32000;
    s.clampX1() = g_cursorClampX1;
    i32 result = g_cursorClampY1;
    g_cursorClampX1 = -32000;
    s.clampY1() = g_cursorClampY1;
    g_cursorClampY1 = -32000;
    return result;
}

// gilde.exe 0x40d338 — VIBE_Input_SaveMouseButtonSnapshot.
// The original snapshots the 19-dword packet window at entry (v0) and then rotates
// the double-click bookkeeping words using that snapshot.
void Input_SaveMouseButtonSnapshot() {
    MouseInputState& s = g_mouseInput;
    if (!g_inputRawMode)
        return;
    // v0 = copy of dword_6721C4..(76 bytes). v0[3] == old dword_6721D0,
    // v0[6] == old dword_6721DC.
    i32 oldD0 = s.sc6721D0(); // v0[3]  (0x6721C4 + 12)
    i32 oldDC = s.sc6721DC(); // v0[6]  (0x6721C4 + 24)
    s.sc6721D0() = s.sc6721E4(); // dword_6721D0 = dword_6721E4
    s.sc6721DC() = s.sc6721F0(); // dword_6721DC = dword_6721F0
    s.sc6721F0() = oldDC;        // dword_6721F0 = v0[6]
    s.sc6721E4() = oldD0;        // dword_6721E4 = v0[3]
}

} // namespace guild::gui

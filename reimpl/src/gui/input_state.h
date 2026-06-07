#pragma once
// guild::gui — raw mouse-button input-state block and its manipulators.
//
// The original keeps the latched pointer state in a flat BSS region based at
// dword_672174 (the "current frame" mouse state) with several parallel one-dword
// flag arrays.  The DirectInput poll path (VIBE_Input_PollMouseDevice /
// VIBE_Input_LatchMouseState) fills it; the GUI run-loops read it and clear edges
// through the small helpers reproduced here.  This module recovers the DATA BLOCK
// and the pure bookkeeping helpers (reset / clear / mask-clear / wheel base /
// cursor-clamp swap / snapshot).  The DirectInput acquisition and the per-event
// ring decode stay on the OS side (shim/IPlatform); these helpers only touch the
// recovered globals and so are fully testable in isolation.
//
// Each mouse button has TWO parallel one-dword flags:
//   * a "held" flag  (button currently down)   — the 0x67218C.. / 0x672180.. cluster
//   * an "edge" flag (button just clicked)      — the 0x67221C.. / 0x67223C cluster
// VIBE_Input_ClearMouseButtonsByMask clears the (edge,held) pair for a set of
// buttons selected by a bit-mask; the mask→pair mapping differs depending on
// dword_62D0E4 (the "raw vs cooked" input mode), reproduced 1:1 below.
//
// Globals (BSS bases in the comments) are defined ONCE here.

#include "guild/common/types.h"
#include <cstring>

namespace guild::gui {

using guild::i16;
using guild::i32;
using guild::u8;

// ---------------------------------------------------------------------------
// The mouse-button input-state block — flat region based at dword_672174.
// Modelled byte-exact (each field at its original offset off 0x672174) so the
// raw-offset addressing in the originals translates directly.  256 bytes covers
// every field these helpers touch (0x672174 .. 0x672274).
// ---------------------------------------------------------------------------
inline constexpr int kInputStateBase  = 0x672174;
inline constexpr int kInputStateBytes = 0x100;

struct MouseInputState {
    u8 raw[kInputStateBytes];

    MouseInputState() { std::memset(raw, 0, sizeof(raw)); }

    template <typename T> T&       at(int absAddr)       { return *reinterpret_cast<T*>(raw + (absAddr - kInputStateBase)); }
    template <typename T> const T& at(int absAddr) const { return *reinterpret_cast<const T*>(raw + (absAddr - kInputStateBase)); }

    // Latched cursor packet copied wholesale by Latch/Process (dword_672174..).
    i32& packetBase()   { return at<i32>(0x672174); } // dword_672174 (copy target for the 0x4C-byte packet)
    i32& packedCoord()  { return at<i32>(0x6721C4); } // dword_6721C4 (16.16 packed mouse y in high word)

    // "Held" flags (button currently down) — one dword per button.
    i32& heldBit10()    { return at<i32>(0x672180); } // dword_672180 (mask 0x10)
    i32& heldBit20()    { return at<i32>(0x672184); } // dword_672184 (mask 0x20)
    i32& heldBit08()    { return at<i32>(0x67218C); } // dword_67218C (mask 0x08)
    i32& heldBit02()    { return at<i32>(0x672194); } // dword_672194 (mask 0x02)
    i32& heldBit04()    { return at<i32>(0x672198); } // dword_672198 (mask 0x04)
    i32& heldBit01()    { return at<i32>(0x6721A0); } // dword_6721A0 (mask 0x01)

    // "Edge" flags (button just clicked this frame) — one dword per button.
    i32& edgeBit10()    { return at<i32>(0x67221C); } // dword_67221C (mask 0x10)
    i32& edgeBit20()    { return at<i32>(0x672220); } // dword_672220 (mask 0x20) [== g_mouseDown selector]
    i32& edgeBit08()    { return at<i32>(0x672228); } // dword_672228 (mask 0x08) [== g_mouseClick selector]
    i32& edgeBit02()    { return at<i32>(0x672230); } // dword_672230 (mask 0x02)
    i32& edgeBit04()    { return at<i32>(0x672234); } // dword_672234 (mask 0x04)
    i32& edgeBit01()    { return at<i32>(0x67223C); } // dword_67223C (mask 0x01)

    // Extra one-dword state words touched by Reset / Snapshot.
    i32& w672188()      { return at<i32>(0x672188); }
    i32& w672224()      { return at<i32>(0x672224); }
    i32& w672238()      { return at<i32>(0x672238); } // 3D scene pick active (dword_672238)
    i32& w67223C()      { return at<i32>(0x67223C); } // alias of edgeBit01
    i32& sc6721D4()     { return at<i32>(0x6721D4); }
    i32& sc6721D8()     { return at<i32>(0x6721D8); }
    i32& sc6721DC()     { return at<i32>(0x6721DC); }
    i32& sc6721D0()     { return at<i32>(0x6721D0); }
    i32& sc6721E4()     { return at<i32>(0x6721E4); }
    i32& sc6721E8()     { return at<i32>(0x6721E8); }
    i32& sc6721EC()     { return at<i32>(0x6721EC); }
    i32& sc6721F0()     { return at<i32>(0x6721F0); }

    // Saved cursor-clamp bounds (swapped in/out by SwapCursorClampState).
    i32& clampX0()      { return at<i32>(0x672264); } // dword_672264
    i32& clampY0()      { return at<i32>(0x672268); } // dword_672268
    i32& clampX1()      { return at<i32>(0x67226C); } // dword_67226C
    i32& clampY1()      { return at<i32>(0x672270); } // dword_672270
};
static_assert(sizeof(MouseInputState) == kInputStateBytes, "MouseInputState must be 256 bytes");

extern MouseInputState g_mouseInput; // BSS region based at dword_672174

// ---------------------------------------------------------------------------
// Live cursor / mode globals (separate 0x62D0xx selectors).
// ---------------------------------------------------------------------------
extern i32 g_inputRawMode;    // dword_62D0E4 (mask→pair selection in ClearByMask; nonzero = "raw")
extern i32 g_wheelBase;       // dword_62D0B8 (set by SetWheelBase)

// Active cursor-clamp window (the live values SwapCursorClampState parks/restores).
extern i32 g_cursorClampX0;   // dword_62D0C4
extern i32 g_cursorClampY0;   // dword_62D0C8
extern i32 g_cursorClampX1;   // dword_62D0CC
extern i32 g_cursorClampY1;   // dword_62D0D0

void ResetMouseInput();       // zero the whole block + the mode/cursor globals (test helper)

// gilde.exe 0x40c87c — VIBE_Input_ResetMouseButtonState  (void(void))
// Zero every per-button edge/held flag (and the auxiliary state words) — the full
// "no buttons pressed" reset used on focus loss / mode change.
void Input_ResetMouseButtonState();

// gilde.exe 0x40c900 — VIBE_Input_ClearMouseButtonFlags  (void(void))
// Clear the four primary (edge,held) pairs (masks 0x01/0x02/0x04/0x08) plus the
// scene-pick word — the per-frame "consume the clicks" clear.
void Input_ClearMouseButtonFlags();

// gilde.exe 0x40c870 — VIBE_Input_SetWheelBase  (eax = a1 + 256)
// Record the wheel accumulator base (dword_62D0B8 = wheel + 256) and return it.
i32 Input_SetWheelBase(i32 wheel);

// gilde.exe 0x40dca8 — VIBE_Input_ClearMouseButtonsByMask  (al = mask@al)
// Clear the (edge,held) flag pair for each button selected in `mask`.  The mask→
// button mapping depends on g_inputRawMode (dword_62D0E4): the "raw" branch
// (nonzero) and the "cooked" branch (zero) wire the bits to different pairs — both
// reproduced 1:1.  Returns `mask` (the original returns al unchanged).
u8 Input_ClearMouseButtonsByMask(u8 mask);

// gilde.exe 0x40dc2c — VIBE_Input_SwapCursorClampState  (eax = a1@eax)
// Toggle the cursor-clamp window.  a1 == 0: park the live clamp (62D0C4..62D0D0)
// into the saved slots (672264..672270) and open the clamp wide (±32000).  a1 != 0:
// restore the live clamp from the saved slots.  Returns the restored/last y-max.
i32 Input_SwapCursorClampState(i32 restore);

// gilde.exe 0x40d338 — VIBE_Input_SaveMouseButtonSnapshot  (void(void))
// When g_inputRawMode is set, advance the double-click bookkeeping: copy the
// 19-dword packet window (6721C4..), promote the current edge words into the
// "previous" slots, and latch the live edge flags.  No-op when g_inputRawMode == 0.
void Input_SaveMouseButtonSnapshot();

} // namespace guild::gui

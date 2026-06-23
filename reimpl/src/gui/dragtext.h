#pragma once
// gui/dragtext.{h,cpp} — begin a TEXT drag from the cursor
// (gilde.exe 0x4ad508 — VIBE_DragSlot_BeginDragText).
//
// Sibling of the drag-cursor cluster in gui/dragselect.{h,cpp} (which owns
// DragCursor_SetSprite @0x41fcbc) and the carried-item table in sim/dragslot.{h,cpp}
// (which owns DragSlot_ResetTable @0x41f9dc). This kicks off "dragging a label":
// it copies the pending drag-text string into the active drag buffer, clears the
// cursor sprite + the carried-item table + the mouse-button state, sets the cursor
// tooltip to the drag text, and arms the drag box anchored at the current cursor.
//
// Decompile (__thiscall, this = the dragging object):
//   v1 = byte_61D9C8;                          // pending drag-text (source)
//   v2 = &byte_11B6B20;                        // active drag-text buffer (dest)
//   VIBE_DragCursor_SetSprite(this, 0);        // clear the cursor sprite (0x41fcbc)
//   VIBE_DragSlot_ResetTable();                // clear carried-item table (0x41f9dc)
//   VIBE_Input_ResetMouseButtonState();        // (0x40c87c)
//   do { *v2 = *v1; if (!*v1) break;           // strcpy(v2, v1) (2-byte stride loop)
//        v2[1]=v1[1]; v1+=2; v2+=2; } while (v1[-1]);
//   dword_631678 = dword_62EB38;               // latch drag-start game tick
//   VIBE_Widget_SetTooltipText(byte_61D9C8);   // tooltip = drag text (0x421a24)
//   dword_62D0D4 = 1;                           // drag active flag
//   dword_62D0C4 = (dword_69FFBC >> 16) - 8;    // drag box x = cursorX - 8
//   dword_62D0CC = 0;                           // drag box (other corner) = 0
//   dword_62D0D0 = 0;
//   dword_62D0C8 = *(i32*)((u8*)&dword_69FFB8 + 2) >> 16;  // drag box y = cursorY
//   return dword_62D0C8;                        // returns the box y
//
// The drag-box globals dword_62D0C4/C8/CC/D0/D4 are the SAME dead-band box modeled in
// gui/dragselect.h (DragBox). dword_69FFBC / dword_69FFB8 are the packed cursor
// position globals (read >>16; the +2 byte offset selects the high half of the second
// dword — the cursor Y). dword_62EB38 is g_gameTick.
//
// We model the touched state explicitly and route the three coupled leaves
// (DragCursor_SetSprite / DragSlot_ResetTable / Input_ResetMouseButtonState /
// Widget_SetTooltipText) through a hook struct so the call ORDER and every write are
// 1:1 and testable. The string copy is the exact strcpy the 2-byte loop performs.

#include "guild/common/types.h"

namespace guild::gui {

using guild::i32;

inline constexpr int kDragTextBufBytes = 256;   // byte_11B6B20 active buffer

// The drag-box + drag-state globals BeginDragText writes (gilde.exe addresses noted).
struct DragTextState {
    char  activeText[kDragTextBufBytes] = {0};  // byte_11B6B20 (dest of the copy)
    i32   startTick = 0;                          // dword_631678 (g_gameTick latch)
    i32   boxX  = 0;                              // dword_62D0C4 (= cursorX - 8)
    i32   boxY  = 0;                              // dword_62D0C8 (= cursorY)
    i32   boxX2 = 0;                              // dword_62D0CC (= 0)
    i32   boxY2 = 0;                              // dword_62D0D0 (= 0)
    i32   active = 0;                             // dword_62D0D4 (= 1)
};

// Coupled-leaf hooks (called in original order before the state writes).
struct DragTextHooks {
    void (*cursorSetSprite)() = nullptr;          // VIBE_DragCursor_SetSprite(this,0) @0x41fcbc
    void (*resetSlotTable)()  = nullptr;          // VIBE_DragSlot_ResetTable @0x41f9dc
    void (*resetMouseButtons)() = nullptr;        // VIBE_Input_ResetMouseButtonState @0x40c87c
    void (*setTooltipText)(const char* s) = nullptr; // VIBE_Widget_SetTooltipText @0x421a24
};

// gilde.exe 0x4ad508 — VIBE_DragSlot_BeginDragText.
// `pendingText` is byte_61D9C8 (the source drag-text). `cursorX`/`cursorY` are the
// >>16 of the packed cursor globals (dword_69FFBC / dword_69FFB8+2). `gameTick` is
// dword_62EB38. Fills `st`, calls the four leaves in order, and returns st.boxY (the
// original's return value, dword_62D0C8).
i32 BeginDragText(DragTextState& st, const char* pendingText,
                  i32 cursorX, i32 cursorY, i32 gameTick,
                  const DragTextHooks& h);

} // namespace guild::gui

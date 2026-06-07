#pragma once
// guild::gui — scrollbar thumb-drag value model.
//
// A scrollbar's mutable model lives in its type-'A' data record:
//   +24  max         (rec24)
//   +28  min         (rec28)
//   +38  flags       (bit 0x40 = "treat -1 as empty", bit 0x02 = drag-enabled)
//   +40  value text  (decimal string of the current value; written by the renderer)
//   +296 value       (current committed value)
// VIBE_Scrollbar_SetThumbPosition @0x420270 maps a thumb delta to a new value, clamps
// it to [min,max] (with the -1/flag special case), stores it at +296 and re-renders the
// decimal string at +40. VIBE_Scrollbar_DragThumb @0x4208ac is the per-frame driver: on
// mouse-down it captures the drag origin, then each frame calls SetThumbPosition with
// the live mouse delta. The capture/per-frame half is bound to the mouse-state globals
// and the renderer coordinate transforms; here we model the value update itself, which
// is the byte-accurate core, on a small ScrollState mirror of the data record.

#include "gui/types.h"

namespace guild::gui {

// Mirror of the scrollbar's type-'A' data record fields the value model touches.
struct ScrollState {
    i32 max   = 0;   // +24
    i32 min   = 0;   // +28
    u8  flags = 0;   // +38  (0x40 = empty-sentinel handling)
    i32 value = 0;   // +296
    char text[16]{}; // +40  decimal string of `value`
};

// gilde.exe 0x420270 — VIBE_Scrollbar_SetThumbPosition  (rec@eax, base@edx, lo@ecx, hi@ebx)
// Computes value = base + step*((hi-lo)>>2), clamps to [min,max] honoring the rec+38
// 0x40 sentinel rule, stores it at value, and writes its decimal string into text.
// Returns the clamped value.
int Scrollbar_SetThumbPosition(ScrollState& rec, int base, int thumbLo, int thumbHi);

} // namespace guild::gui

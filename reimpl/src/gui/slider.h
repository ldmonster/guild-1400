#pragma once
// guild::gui — slider value model: step quantisation and value<->thumb mapping.
//
// A slider/scrollbar widget stores its model in the edit-field region of the 740-byte
// widget record:
//   +124 editMin   (lower bound)
//   +128 editMax   (upper bound)
//   +296 (on the type-'A' data record)  current value being committed
//   +24/+28 (on the data record)        max/min used by the step quantiser
// The original derives a "nice" step size from the value span and snaps values to it.
//
// Recovered originals:
//   VIBE_Slider_ComputeStep      @0x41df08 — span -> step size (12-bucket table).
//   VIBE_Scrollbar_SetThumbPosition @0x420270 — thumb pixel delta -> value (uses step).
// The pixel<->value geometry helpers (VIBE_Slider_ComputeThumbPos @0x41069c,
// VIBE_Slider_UpdateFromMouse @0x420a04) are deferred: they call the renderer's
// VIBE_Coord_Transform / VIBE_Coord_ConvertX to map widget coords to screen pixels and
// use floating-point. Their *integer* core — the step quantiser and the linear
// value<->thumb relation — is what is reproduced here byte-for-byte.

#include "gui/types.h"

namespace guild::gui {

// gilde.exe 0x41df08 — VIBE_Slider_ComputeStep  (dataRec@eax)
// Returns the snap step for a slider whose value span is `span` (= rec[+24] - rec[+28]).
// The 12-bucket lookup is exact: larger spans use coarser steps.
int Slider_ComputeStep(int span);

// gilde.exe 0x420270 (integer core) — VIBE_Scrollbar_SetThumbPosition value math.
// Maps a thumb drag to a new value: `value = base + step * ((delta) >> 2)`, where
// `delta = thumbHi - thumbLo` and `step = Slider_ComputeStep(max - min)`. The original
// computes the arithmetic-shift-right with the exact (__CFSHL__) rounding-toward-zero
// of a signed `>>2`. The result is clamped to [min, max]. Returns the clamped value.
//   base   = a2 (the value at the start of the drag)
//   max    = rec[+24], min = rec[+28]
int Scrollbar_ValueFromThumb(int base, int thumbLo, int thumbHi, int min, int max);

// gilde.exe 0x41dfec (slider-clamp block of VIBE_Object_SetValueOrText) —
// Re-quantises a slider's [min,max] range and current value to the computed step,
// clamping the span to at most 25 steps. Mirrors the in-place updates the original
// performs on rec[+24] (max), rec[+28] (min) and rec[+296] (value). Returns the snapped
// value; min/max are updated in place. `clampFlag` is the rec[+38] & 0x40 bit (when set
// and value==-1 with min/max defaults the value is left untouched).
int Slider_QuantizeRange(int& min, int& max, int value, bool clampFlag);

} // namespace guild::gui

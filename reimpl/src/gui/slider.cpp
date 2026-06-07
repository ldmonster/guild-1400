#include "gui/slider.h"

#include <cstdint>

namespace guild::gui {

// gilde.exe 0x41df08 — VIBE_Slider_ComputeStep
// The original reads span = *(rec+24) - *(rec+28); we take it as a parameter.
int Slider_ComputeStep(int span) {
    int result = 1;
    if (span >= 125000) return 10000;
    if (span >= 100000) return 5000;
    if (span >= 75000)  return 4000;
    if (span >= 50000)  return 3000;
    if (span >= 37500)  return 2000;
    if (span >= 25000)  return 1500;
    if (span >= 12500)  return 1000;
    if (span >= 5000)   return 500;
    if (span >= 2500)   return 200;
    if (span >= 1000)   return 100;
    if (span >= 250)    return 50;
    if (span >= 100)    return 10;
    return result;
}

// gilde.exe 0x420270 — value math of VIBE_Scrollbar_SetThumbPosition.
//   v5 = ComputeStep(span) * ((delta - rounding) >> 2) + base
// where the original computes a signed `(delta) >> 2` rounded toward zero via the
// __CFSHL__/sign-bit fixup. In C++ `delta / 4` on a signed int already rounds toward
// zero, which is exactly what that fixup reproduces, so we use it directly.
int Scrollbar_ValueFromThumb(int base, int thumbLo, int thumbHi, int min, int max) {
    int step  = Slider_ComputeStep(max - min);
    int delta = thumbHi - thumbLo;
    int value = step * (delta / 4) + base; // delta>>2 with round-toward-zero

    // Clamp to [min, max].  (The original also special-cases the -1 sentinel under the
    // rec+38 & 0x40 flag; for the plain scrollbar path min/max/value are real numbers.)
    if (value > max) {
        value = max;
    } else if (value < min) {
        value = min;
    }
    return value;
}

// gilde.exe 0x41dfec — slider-range quantiser block of VIBE_Object_SetValueOrText.
int Slider_QuantizeRange(int& min, int& max, int value, bool clampFlag) {
    int step = Slider_ComputeStep(max - min);            // ComputeStep(span)
    int newMax = max - (max - min) % step;               // v16
    int newMin = (newMax - min) % step + min;            // v17
    int newVal = step * (value / step);                  // v18 (snap value down to step)
    max = newMax;
    min = newMin;
    int span = max - min;                                // v19 = v16 - v17
    value = newVal;                                      // rec296 = v18

    int steps = span / step;                             // v5 = v19 / v15
    if (steps > 25) {
        max = 25 * step;                                 // clamp to 25 steps
    }

    // Final clamp: unless (value == -1 && clampFlag) keep value within [min, ...].
    if (value != -1 || !clampFlag) {
        if (min <= value) {
            // value >= min: keep value as-is.
        } else {
            value = min; // value below min -> clamp up to min
        }
    }
    return value;
}

} // namespace guild::gui

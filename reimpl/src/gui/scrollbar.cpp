#include "gui/scrollbar.h"
#include "gui/slider.h"

#include <cstdint>

namespace guild::gui {

namespace {
// gilde.exe 0x5d92ec — VIBE_AnimationState_Update (radix 10 path) + 0x5d92a0
// IntToStringRadix. For base 10 a leading '-' is emitted for negatives, then the
// magnitude is written as decimal digits. This is the renderer-side string production
// the scrollbar uses for its +40 value text; reproduced locally so the value model is
// self-contained (the original lives in the util cluster).
void AnimationState_Update10(int value, char* buf) {
    char* p = buf;
    unsigned u;
    if (value < 0) {
        *p++ = '-';
        u = static_cast<unsigned>(-static_cast<long long>(value)); // negate (wrap-safe)
    } else {
        u = static_cast<unsigned>(value);
    }
    // Write decimal digits.
    char tmp[12];
    int n = 0;
    do {
        tmp[n++] = static_cast<char>('0' + (u % 10));
        u /= 10;
    } while (u);
    while (n--) *p++ = tmp[n];
    *p = '\0';
}
} // namespace

// gilde.exe 0x420270 — VIBE_Scrollbar_SetThumbPosition
int Scrollbar_SetThumbPosition(ScrollState& rec, int base, int thumbLo, int thumbHi) {
    int step  = Slider_ComputeStep(rec.max - rec.min);
    int delta = thumbHi - thumbLo;
    int v5    = step * (delta / 4) + base; // delta>>2 round-toward-zero

    int v6 = rec.max;
    rec.value = v5;
    if (v5 > v6) {
        rec.value = v6;
    } else {
        int v7 = rec.min;
        // Clamp up to min, except: when the 0x40 sentinel flag is set AND the value is
        // exactly the empty sentinel (-1) AND both bounds are zero, leave it as -1.
        if (v5 < v7 && (((rec.flags & 0x40) == 0) || v5 != -1 || v7 != 0 || v6 != 0))
            rec.value = rec.min;
    }

    AnimationState_Update10(rec.value, rec.text);
    return rec.value;
}

} // namespace guild::gui

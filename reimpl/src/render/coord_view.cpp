#include "render/coord_view.h"

#include "util/coord.h"      // ConvertX (x87 truncate-toward-zero)

namespace guild::render {

// gilde.exe 0x4525b4 — VIBE_Coord_ComputeViewScale.
//   v0 = flt_641DAC * dbl_619130;  ConvertX();  (int)v0 clamped to [5,16].
// The original truncated v0 toward zero on the FPU (ConvertX) before the integer
// compares; we do the same so the clamp boundaries are bit-exact.
i32 ComputeViewScale(float zoom) {
    double v0 = static_cast<double>(zoom) * kViewScaleFactor;
    v0 = util::ConvertX(v0);
    if (static_cast<i32>(v0) < 5)
        return 5;
    i32 result = static_cast<i32>(v0);
    if (static_cast<i32>(v0) > 16)
        return 16;
    return result;
}

} // namespace guild::render

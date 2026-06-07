#include "render/floorwater.h"

#include "util/coord.h"  // ConvertX (x87 truncate-toward-zero)

#include <cmath>

namespace guild::render {

namespace {
// Wave-generator constants (gilde.exe dbl_628Axx, recovered via get_bytes).
constexpr double kC27 = 2.7;   // dbl_628AEC
constexpr double kC24 = 2.4;   // dbl_628AFC
constexpr double kC25 = 2.5;   // dbl_628B04
constexpr double kC26 = 2.6;   // dbl_628B0C
constexpr double kPi  = 3.141592653589793; // dbl_628B14 (0x400921FB54442EEA)
constexpr double kC22 = 2.2;   // dbl_628B1C
constexpr double kC40 = 4.0;   // dbl_628B24
// flt_628760 == 0.5 — gradient round bias.
constexpr float  kHalf = 0.5f;
} // namespace

// gilde.exe 0x5ba750 — VIBE_FloorWater_FloodFillMask
//   mask[y*stride + x] = to; then recurse into the four neighbours == from.
// The original is an iterative tail-recursion on the -y direction with explicit
// recursive calls for +x/-x/+y; we keep the same neighbour order and bounds tests.
void FloodFillMask(u8* mask, int stride, int y, int x, u8 from, u8 to) {
    for (;;) {
        mask[x + y * stride] = to;                 // *v8 = a6
        if (x + 1 < stride) {                      // +x neighbour
            if (mask[x + y * stride + 1] == from)
                FloodFillMask(mask, stride, y, x + 1, from, to);
        }
        if (x - 1 >= 0) {                          // -x neighbour
            if (mask[(x - 1) + y * stride] == from)
                FloodFillMask(mask, stride, y, x - 1, from, to);
        }
        if (y + 1 < stride) {                      // +y neighbour
            if (mask[x + (y + 1) * stride] == from)
                FloodFillMask(mask, stride, y + 1, x, from, to);
        }
        --y;                                       // -y: iterate (tail position)
        if (y < 0) break;
        if (mask[x + y * stride] != from) break;
    }
}

// gilde.exe 0x5ba898 — VIBE_FloorWater_FillHeightGradient
//   step = (hiVal - loVal) / (hi - lo); v = loVal; for i in [lo,hi]:
//     dst[i] = (int)(v + 0.5); v += step;
void FillHeightGradient(u8* dst, int lo, int hi, u8 loVal, u8 hiVal) {
    if (lo > hi) return;
    double v = (double)(float)loVal;                       // v9 = (float)a2
    double step = (double)(hiVal - loVal) / (double)(hi - lo);
    for (int i = lo; i <= hi; ++i) {
        // VIBE_Coord_ConvertX truncates (v + 0.5) toward zero.
        dst[i] = (u8)(int)util::ConvertX(v + (double)kHalf);
        v += step;
    }
}

// gilde.exe 0x5ba824 — VIBE_FloorWater_FindRegionOffset.
//   Walk 20-byte span records (Floor+52); match type + [lo,hi] containment of
//   `query`; stamp the visited marker; return the 80-byte-strided buffer offset.
i32 FindRegionOffset(WaterRegionSpan* spans, i32 spanCount, i32 bufferBase,
                     i32 query, u8 marker, i32 type) {
    if (spans == nullptr)               // a1 == 0 -> 0
        return 0;
    if (spanCount <= 0)                 // *(a1+52) == 0 -> 0 (empty list)
        return 0;
    // Gate: the FIRST span's type < 0 means "no regions" (return 0).
    if (spans[0].type < 0)              // *(int*)(v6+4) >= 0 required to enter
        return 0;
    // Scan: advance while (type mismatch) || (query < lo) || (query > hi). The
    // original peeks the NEXT record's type (rec[+24] == next.type) and bails when
    // it is negative (end-of-list) BEFORE testing the next record.
    int i = 0;
    while (type != spans[i].type || query < spans[i].lo || query > spans[i].hi) {
        // v9 = next.type (spans[i+1].type); rec += 20; if (v9 < 0) return 0.
        i32 nextType = (i + 1 < spanCount) ? spans[i + 1].type : -1;
        ++i;
        if (nextType < 0)
            return 0;                   // terminator: no matching span
    }
    WaterRegionSpan& s = spans[i];
    if (s.marker == 0xFFu)              // unmarked -> stamp the visited marker
        s.marker = marker;
    return 80 * (s.base + query - s.lo) + bufferBase;
}

// gilde.exe 0x5be428 — VIBE_Floor_AnimateWaterVertices (16-vertex wave loop).
void AnimateWaterWaveGrid(float* out, const float amp[4], const float phase[4],
                          double t) {
    for (int c = 0; c < 16; ++c) {
        double dc = (double)c;
        double x = std::sin(std::cos(dc * kC27) * t + dc + (double)phase[0]);
        double y = std::cos(std::sin(kPi - dc * kC24) * t + dc + (double)phase[1]);
        double z = std::sin(dc - std::cos(dc * kC25) * t + kC22 + (double)phase[2]);
        double w = std::cos(dc - std::sin(dc * kC26 + kPi) * t + kC40 + (double)phase[3]);
        float* o = out + 4 * c;
        o[0] = (float)((double)amp[0] * x);
        o[1] = (float)((double)amp[1] * y);
        o[2] = (float)((double)amp[2] * z);
        o[3] = (float)((double)amp[3] * w);
    }
}

} // namespace guild::render

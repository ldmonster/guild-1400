// Snow scene-update arithmetic reconstruction for Die Gilde (gilde.exe).
// Translated 1:1 from the Hex-Rays decompile (reference of record).

#include "render/snow_recon.h"

#include <cmath>

namespace guild {
namespace render {

namespace {
// Exact IEEE-754 constants (recovered with get_bytes).
constexpr float  kCoverScaleAccum = 0.00039999998989515007f; // flt_6118A4 (0x39D1B717)
constexpr float  kCoverMaxF       = 255.0f;                  // float 255 (0x437F0000)
constexpr double kCoverScaleTimer = 0.002;                   // dbl_611894 (0x3F60624DD2F1A9FC)
constexpr double kCoverMaxD       = 255.0;                   // dbl_61189C (0x406FE00000000000)

// VIBE_Coord_ConvertX 0x5c6b08: frndint with rounding forced to chop, then
// fistp -> truncate toward zero.
inline i64 TruncTowardZero(double v) {
    return static_cast<i64>(v >= 0.0 ? std::floor(v) : std::ceil(v));
}
} // namespace

// gilde.exe 0x42b3fc / 0x42b1fc tail — snow coverage threshold.
u32 SnowCoverageThreshold(float coverage, u32* out_trunc) {
    // VIBE_Coord_ConvertX(); v = (int)coverage  (truncate toward zero)
    u32 v = static_cast<u32>(static_cast<i64>(TruncTowardZero(coverage)));
    if (out_trunc) *out_trunc = v;
    // lea eax,[v*4]; xor edx,edx; div 5   -> unsigned 32-bit divide.
    u32 budget = (v * 4u) / 5u;
    // lea edx,[v-16]; cmp edx,budget; jnb -> if (v-16) < budget (unsigned).
    u32 vm16 = v - 16u;
    if (vm16 < budget) budget = vm16;
    return budget;
}

// gilde.exe 0x42b3fc — accumulator step.
float SnowAccumulatorStep(i32 rate, float dt, float acc) {
    // v3 = (double)*(int*)a1 * a2 + *(float*)(a1+84)
    double v3 = static_cast<double>(rate) * static_cast<double>(dt)
              + static_cast<double>(acc);
    return static_cast<float>(v3);
}

// gilde.exe 0x42b3fc — coverage from accumulator.
float SnowCoverageFromAccumulator(float acc) {
    // v18 = acc * flt_6118A4 ; if (flt_6118A8(255) < v18) 255 else v18
    float v18 = acc * kCoverScaleAccum;
    return (kCoverMaxF < v18) ? kCoverMaxF : v18;
}

// gilde.exe 0x42b1fc — coverage from object timer.
float SnowCoverageFromTimer(float timer) {
    // if (timer * 0.002 < 255.0) coverage = timer*0.002 else 255.0  (double math)
    double v = static_cast<double>(timer) * kCoverScaleTimer;
    double r = (v < kCoverMaxD) ? v : kCoverMaxD;
    return static_cast<float>(r);
}

// gilde.exe 0x42b1fc / 0x42b3fc — batched-processing pacing.
i32 SnowBatchLimit(i32 total, i32 per, i32 done_cap) {
    if (total - per >= done_cap)
        return total / per + done_cap;   // signed division (idiv)
    return total;
}

} // namespace render
} // namespace guild

// misc_recon4_entitybar.cpp — see header; only the verifiable prologue is recon'd.
#include "sim/misc_recon4_entitybar.h"

namespace guild::sim {

EntityBarFill EntityBarComputeFill(const EntityBarFields& f) {
    EntityBarFill out;

    // 0x41089e: v5 = (double)total / (double)(max - min)
    // (max-min is v129 - v132 == v144 in the original)
    int denom = f.maxVal - f.minVal;
    f64 v5 = (f64)f.total / (f64)denom;

    // 0x4108c8: fillA = (double)(value  - min) * v5
    // 0x4108d8: fillB = v5 * (double)(value2 - min)
    f32 fillA = (f32)((f64)(f.value  - f.minVal) * v5);
    f32 fillB = (f32)(v5 * (f64)(f.value2 - f.minVal));

    // 0x4108f8 / 0x41091e: clamp tiny-positive up to 1.0 (the (>0 && <1.0)->1.0 gate)
    if (fillA > 0.0f && fillA < 1.0f) fillA = 1.0f;
    if (fillB > 0.0f && fillB < 1.0f) fillB = 1.0f;

    out.fillA = fillA;
    out.fillB = fillB;
    return out;
}

} // namespace guild::sim

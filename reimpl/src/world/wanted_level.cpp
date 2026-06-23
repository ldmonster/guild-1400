#include "world/wanted_level.h"

#include <cstring>

// gilde.exe 0x4c2ba0 — VIBE_Gesetz_ComputeMaxWantedLevel. Faithful
// reconstruction of the 256-slot guard-building scan + the 0.75 short-circuit +
// the (float)(maxSum * 0.01f) tail. The building enumeration is routed through a
// GuardBuildingAccessor so the scan is testable without the live building array.
//
// HANDOFF (rule 13): the caller is VIBE_Gesetz_EvaluateViolation @0x4c2c5c (and
// world/law.cpp's GesetzEvaluateViolation), which passes the perpetrator's
// faction word. The engine binds GuardBuildingAccessor to the live building
// array (dword_13CE298, stride 169; type table dword_13CE294, stride 589;
// VIBE_Building_SumWorkstationByCategory @0x5904fc). law.cpp keeps its scalar
// hook (GesetzSetGuardStationSumFn) for the abstracted path; this module is the
// full scan for callers that have the building array.

namespace guild::world {

// flt_61E588 @0x61E588 = 0A D7 23 3C (little-endian) = 0.01f.
const float kWantedLevelScalar = []() {
    u32 bits = 0x3c23d70au;
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}();

// 0x4c2bdd / 0x4c2be6: the scalar tail. The original returns the double literal
// 0.75 for sum==75, else `(float)((double)v3 * flt_61E588)` — computed in double,
// narrowed to float, then returned in st0 (re-widened to double here).
double WantedLevelFromMaxSum(int maxSum) {
    if (maxSum == 75)
        return 0.75;
    float narrowed = static_cast<float>(static_cast<double>(maxSum)
                                        * static_cast<double>(kWantedLevelScalar));
    return static_cast<double>(narrowed);
}

double ComputeMaxWantedLevel(u16 perpFaction, const GuardBuildingAccessor& acc) {
    int maxSum = 0;          // v3
    int i = 0;               // v4

    // 0x4c2bb5..0x4c2c0a: scan up to 256 slots.
    for (; i < acc.count && i < 256; ++i) {
        // 0x4c2c0a: skip dead slots, foreign owners, non-guard types.
        //   !*v2 || *(u16*)(v2+39) != *a1 || type != 7  -> LABEL_3 (continue)
        if (!acc.alive(i, acc.ctx))
            continue;
        if (acc.owner(i, acc.ctx) != perpFaction)
            continue;
        if (acc.type(i, acc.ctx) != 7)
            continue;

        // 0x4c2c18: v6 = SumWorkstationByCategory(b, 10, 1).
        int v6 = acc.sumCat10(i, acc.ctx);
        // 0x4c2c20: a building summing exactly 75 short-circuits to 0.75.
        if (v6 == 75)
            return 0.75;
        // 0x4c2c24: track the max.
        if (v6 > maxSum)
            maxSum = v6;
    }

    // 0x4c2be6: scan complete -> (float)(maxSum * 0.01f).
    return WantedLevelFromMaxSum(maxSum);
}

} // namespace guild::world

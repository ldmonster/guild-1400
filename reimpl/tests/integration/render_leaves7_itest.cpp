#include "test.h"

#include "render/render_leaves7.h"
#include "render/falloff_lut.h"   // REAL sibling: InitFalloffTable (0x5c88f8)
#include "util/coord.h"           // REAL sibling: ConvertX (0x5c6b08)

#include <cmath>

using namespace guild;
using guild::render::RenderLeaves7Hooks;

namespace {
bool approx(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps * (1.0f + std::fabs(b));
}

// Live wiring: BuildGroundShadow's caster colour key truncates through
// VIBE_Coord_ConvertX. We forward the truncToward hook into the REAL
// guild::util::ConvertX (coord.cpp) exactly as the live call site does (the
// original reads the integer part back off the FP register; ConvertX performs
// the x87 round-toward-zero chop), then take the integer part here.
i32 BridgeToRealConvertX(double v) {
    return static_cast<i32>(util::ConvertX(v));
}
} // namespace

// ---------------------------------------------------------------------------
// Wire the truncToward hook -> REAL util::ConvertX and confirm the cross-module
// chop matches a direct call for a spread of caster-colour-key values.
// ---------------------------------------------------------------------------
TEST(RenderLeaves7_Integration, TruncTowardForwardsToRealConvertX) {
    RenderLeaves7Hooks h{};
    h.truncToward = &BridgeToRealConvertX;
    render::InstallRenderLeaves7Hooks(h);

    const RenderLeaves7Hooks& cur = render::CurrentRenderLeaves7Hooks();
    const double samples[] = {0.0, 2.9, 250.4, 254.99, -3.7, 127.5};
    for (double s : samples) {
        i32 viaHook   = cur.truncToward(s);
        i32 viaDirect = static_cast<i32>(util::ConvertX(s));
        CHECK_EQ(viaHook, viaDirect);
        // ConvertX is round-toward-zero, so it equals C truncation.
        CHECK_EQ(viaHook, static_cast<i32>(s));
    }

    // Reset to inert defaults so other suites in the unified build are clean.
    render::InstallRenderLeaves7Hooks(RenderLeaves7Hooks{});
}

// ---------------------------------------------------------------------------
// Wire the directional-falloff kernel against the REAL light-falloff LUT.
//
// The original indexes flt_1405110[idx]. InitFalloffTable's store loop
// pre-increments (add ecx,4 before fstp @0x5c8929), so its 1024 stores START
// at flt_1405110 — out[i] from the reconstructed sibling IS flt_1405110[i].
// (Harden fix: the previous +1 offset double-counted the pre-increment and
// read one entry past the binary's lookup, going OOB at idx=1023.)
// ---------------------------------------------------------------------------
TEST(RenderLeaves7_Integration, DirectionalFalloffUsesRealLUT) {
    static float table[render::kFalloffEntries];
    render::InitFalloffTable(table);                 // REAL sibling fills 1024.

    // out[i] == flt_1405110[i] (see falloff_lut.h store-base note).
    const float* lut1405110 = table;

    // table[i] = 1 - acos(i/1024)*(2/pi); strictly increasing from table[0]~=0.
    CHECK(approx(table[0], 0.0f));
    CHECK(table[1] > table[0]);

    const float NdotL = -0.5f;                       // -> idx = (int)(0.5*1023) = 511
    int idx = static_cast<int>(NdotL * render::kFalloffIdxScale);
    CHECK_EQ(idx, 511);

    float intensity = 3.0f;
    float got = render::DirectionalFalloffScale(NdotL, intensity, lut1405110);
    // Independent: intensity * table[idx] (flt_1405110[idx]).
    float want = intensity * table[idx];
    CHECK(approx(got, want));

    // Spot-check a second angle near the grazing end of the table.
    float n2 = -0.999f;
    int idx2 = static_cast<int>(n2 * render::kFalloffIdxScale);
    float got2 = render::DirectionalFalloffScale(n2, 1.0f, lut1405110);
    CHECK(approx(got2, table[idx2]));
}

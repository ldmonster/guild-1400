#include "test.h"

// Integration: the time-of-day -> fog/ambient pipeline driven across the REAL
// sibling render modules (no mocks):
//   daycycle.cpp   : BuildTimeTable / UpdateBrightness / BrightnessToBand
//   sky.cpp        : ScaledBlendAlpha / BlendBandLighting / LerpChannelTrunc
//   colorformat.cpp: PackColor (the fog colour packed into a native pixel)
//   fog.cpp        : ApplyAmbientBlend / ConfigureFog / ComputeFogFactor
// This is the actual chain the engine runs each frame: clock -> brightness ->
// sky band + blend fraction -> ambient/fog cross-band lerp -> fog state -> the
// per-distance fog factor the rasterizer samples.
#include "render/fog.h"
#include "render/daycycle.h"
#include "render/sky.h"
#include "render/colorformat.h"

#include <cstdint>

using namespace guild;
using namespace guild::render;

// Clock -> brightness -> band/blend (real daycycle), then feed the blend fraction
// into the real fog cross-band lerp; verify the chain produces a sane, monotone
// fog state and that the per-distance factor decreases with distance.
TEST(RenderFogItest, TimeOfDayDrivesFog) {
    // Season 0 keyframes -> seconds-of-day thresholds (real BuildTimeTable).
    i32 kf[6];
    BuildTimeTable(0, kf);
    CHECK_EQ(kf[0], 3600 * 6 + 60 * 30); // 6:30
    CHECK_EQ(kf[5], 3600 * 21 + 60 * 30); // 21:30

    // Midday (12:00) is well inside the bright plateau -> brightness in (200,400).
    int bMid = UpdateBrightness(kf, 12, 0);
    CHECK(bMid > 200 && bMid < 400);
    // Pre-dawn (3:00) is before kf[0] -> brightness 0.
    CHECK_EQ(UpdateBrightness(kf, 3, 0), 0);

    // Real brightness -> band select.
    SkyBandSelect sel = BrightnessToBand(bMid);
    CHECK(sel.band >= 0 && sel.band < 7);
    CHECK(sel.blend >= 0.0f && sel.blend <= 1.0f);

    // The sky scaled-blend alpha matches the band blend domain [0,1].
    float alpha = ScaledBlendAlpha(bMid / 6); // brightness/6 ~ 0..100 scale
    CHECK(alpha >= 0.0f && alpha <= 1.0f);

    // Drive the fog cross-band lerp with the REAL blend fraction. Two adjacent
    // time-of-day fog bands: dawn (warm, short range) -> noon (cool, long range).
    FogBand bands[6] = {};
    bands[sel.band % 6]       = FogBand{180, 120, 90, 30.0f, 150.0f};
    bands[(sel.band + 1) % 6] = FogBand{120, 160, 220, 50.0f, 260.0f};

    FogState s{};
    bool ok = ApplyAmbientBlend(s, bands, sel.band % 6, (sel.band + 1) % 6,
                                sel.blend, 1.0f, true, true);
    CHECK(ok);
    CHECK(s.enabled);
    // Interpolated near/far lie between the two bands.
    CHECK(s.blendNear >= 30.0f && s.blendNear <= 50.0f);
    CHECK(s.blendFar >= 150.0f && s.blendFar <= 260.0f);

    // Per-distance fog factor: full near the camera, fading with distance.
    int fNear = ComputeFogFactor(s, s.nearPlane * s.nearPlane);     // at near plane
    int fFar  = ComputeFogFactor(s, s.farPlane * s.farPlane);       // at far plane
    int fMid  = ComputeFogFactor(s, (s.nearPlane * s.nearPlane + s.farPlane * s.farPlane) * 0.5f);
    CHECK_EQ(fNear, 255);
    CHECK(fFar <= 0);                 // 0 at the far plane (or wrapped <0 beyond)
    CHECK(fMid < fNear && fMid > fFar);
}

// The fog colour ApplyAmbientBlend assembles round-trips through the REAL
// colorformat pack/unpack (RGB565): pack the interpolated triple, unpack it, and
// confirm the channels survive within 565 precision.
TEST(RenderFogItest, FogColorPacksThroughColorFormat) {
    FogBand bands[6] = {};
    bands[0] = FogBand{200, 100, 50, 20.0f, 120.0f};
    bands[1] = FogBand{100, 200, 250, 40.0f, 200.0f};
    FogState s{};
    CHECK(ApplyAmbientBlend(s, bands, 0, 1, 0.5f, 1.0f, true, true));
    // t=0.5 -> 0x969696 (golden from the unit test).
    CHECK_EQ(s.color, 0x969696);

    u8 r = (u8)(s.color >> 16), g = (u8)(s.color >> 8), b = (u8)s.color;
    ColorFormat fmt = Format565();
    u32 pixel = PackColor(fmt, r, g, b);
    u8 ur, ug, ub;
    UnpackColor(fmt, pixel, ur, ug, ub);
    // 565: R/B keep top 5 bits, G keeps top 6. 0x96 = 150.
    CHECK_EQ(ur & 0xF8, r & 0xF8);
    CHECK_EQ(ug & 0xFC, g & 0xFC);
    CHECK_EQ(ub & 0xF8, b & 0xF8);
}

#include "render/sky.h"
#include "render/particle.h" // TruncToward

namespace guild::render {

// gilde.exe 0x5b85e4 (ambient-light core of VIBE_SkyColor_BlendBandLighting).
// flt_64A074/78/7C = the lerped band RGB (all scaled by `scale`); the luma
// flt_64A070 = r*0.30 + g*0.59 + b*0.11. The original lerps the gradient row
// floats (rowB - rowA)*t + rowA, then multiplies each by `scale`.
SkyAmbient BlendBandLighting(const SkyBandColor bands[kSkyBands], int a, float t, float scale) {
    SkyAmbient out{0.0f, 0.0f, 0.0f, 0.0f};
    if ((unsigned)a >= 7u || t < 0.0f || t > 1.0f)
        return out;

    int b = (a + 1) % 7;
    // r' = (rowB.r - rowA.r)*t + rowA.r ; then * scale  (flt_64A074)
    float r = (bands[b].r - bands[a].r) * t + bands[a].r;
    // g' lerp then * scale (the original applies scale via a3 here)         (78)
    float g = (bands[b].g - bands[a].g) * t + bands[a].g;
    // b' = (rowB.b - rowA.b)*t + rowA.b ; then * scale  (flt_64A07C)
    float bl = (bands[b].b - bands[a].b) * t + bands[a].b;

    r = r * scale;   // flt_64A074 = v13
    g = g * scale;   // flt_64A078
    bl = bl * scale; // flt_64A07C = v15

    out.r = r;
    out.g = g;
    out.b = bl;
    // luma = r*0.30 + g*0.59 + b*0.11  (flt_62872C/28/30); order: g*0.59 + r*0.30,
    // then + b*0.11 — matches the original's v16 = g*0.59 + r*0.30; +v18.
    out.luma = g * kSkyLumaG + r * kSkyLumaR + bl * kSkyLumaB;
    return out;
}

// gilde.exe 0x43f460 — VIBE_SkyColor_ApplyScaledBlend alpha.
float ScaledBlendAlpha(int brightness) {
    float v = (float)((double)brightness * kBrightScale);
    if (v <= 0.0f) {
        // SLODWORD(v6) < 1.0f && v6 <= 0 -> alpha 0
        return 0.0f;
    }
    if (v >= 1.0f)
        return 1.0f;
    return v;
}

// out = trunc( a*(1-t) + b*t ) — the signed-16 channel blend used for the 6
// shade colours (VIBE_Coord_ConvertX truncates toward zero).
int LerpChannelTrunc(int a, int b, float t) {
    double blended = (double)(i16)a * (1.0f - t) + (double)(i16)b * t;
    return TruncToward(blended);
}

} // namespace guild::render

#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — sky colour gradient / band blending (gilde.exe d3_sky.c).
//
//   0x5b85e4  VIBE_SkyColor_BlendBandLighting  (band a -> a+1 colour lerp + luma)
//   0x5b8b04  VIBE_SkyColor_ApplyAmbientBlend  (fog/ambient cross-band lerp)
//   0x43f460  VIBE_SkyColor_ApplyScaledBlend   (brightness*0.01 -> [0,1] alpha)
//
// The sky has 7 colour BANDS (indexed 0..6, wrapping mod 7). Each band keeps a
// 24-float gradient row in the runtime table `flt_13FD1B8` (BSS — populated at
// scene load by SkyColor_StoreBandColors, so it is NOT a static recoverable
// table). The blend takes a band index `a` and a fraction `t` in [0,1] and
// linearly interpolates each gradient channel between row a and row (a+1)%7.
//
// THE RECOVERABLE MATH (verified via get_bytes):
//   * the ambient light triple (flt_64A074/78/7C in the original) is the lerp of
//     the band's RGB scaled by the time-of-day `scale` and a luma weighting:
//       r' = lerp(rowA.r, rowB.r, t) * scale
//       g' = lerp(rowA.g, rowB.g, t) * scale
//       b' = lerp(rowA.b, rowB.b, t) * scale
//       luma = b'*0.59 + g'*0.30 + r'*0.11   (flt_62872C/28/30 = .30/.59/.11)
//   * each of the 6 fog/shade colour triples is a per-channel lerp where the
//     16-bit signed channels are blended with weights (1-t) and t, then
//     truncated toward zero (VIBE_Coord_ConvertX).
// Luma weights: flt_628728 = 0.59, flt_62872C = 0.30, flt_628730 = 0.11.
// =============================================================================
namespace guild::render {

constexpr int kSkyBands = 7;

// Luma weights (same triple the lighting path uses, recovered @0x628728).
constexpr float kSkyLumaG = 0.5899999737739563f;   // flt_628728
constexpr float kSkyLumaR = 0.30000001192092896f;  // flt_62872C
constexpr float kSkyLumaB = 0.10999999940395355f;  // flt_628730
constexpr float kSkyBandMid = 0.5f;                // flt_628724 band pick mid
constexpr float kBrightScale = 0.009999999776482582f; // flt_61752C = 1/100

// One band's colour as the gradient stores it: an RGB triple. (The full row is
// 24 floats; the blend below only needs the colour triple per band, so we model
// the table as an array of 7 RGB triples for testability — the byte-exact lerp
// is identical.)
struct SkyBandColor { float r, g, b; };

// Result of a band blend: the ambient light triple and its luma.
struct SkyAmbient { float r, g, b, luma; };

// gilde.exe 0x5b85e4 (ambient-light core). Blend band `a` -> (a+1)%7 by `t`,
// scale by `scale`, and compute the luma. `bands` is the 7-entry gradient table
// (runtime-populated in the original). Returns 0-filled when args are out of
// range (a>=7 || t<0 || t>1), matching the original's early-out.
SkyAmbient BlendBandLighting(const SkyBandColor bands[kSkyBands], int a, float t, float scale);

// gilde.exe 0x43f460 — brightness (0..~100) -> clamped [0,1] alpha via *0.01.
//   v = brightness * 0.01 ; if v <= 0 -> 0 ; if v >= 1 -> 1 ; else v.
float ScaledBlendAlpha(int brightness);

// Per-channel signed-16 colour lerp with truncate-toward-zero, the inner blend
// VIBE_SkyColor_ApplyAmbientBlend / BlendBandLighting use for the 6 shade
// colours: out = trunc( a*(1-t) + b*t ) per channel. Helpers exposed for tests.
int LerpChannelTrunc(int a, int b, float t);

} // namespace guild::render

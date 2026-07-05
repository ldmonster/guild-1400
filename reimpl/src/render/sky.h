#pragma once
#include "guild/common/types.h"
#include "render/skycolor_recon.h" // SkySceneTable / SkyFogScratch / build

namespace guild::render { struct Surface; }
namespace guild::render { struct SceneHeader; } // scene_load.h (read-only handoff)

// =============================================================================
// guild::render — sky colour gradient / band blending (gilde.exe d3_sky.c).
//
//   0x5b85e4  VIBE_SkyColor_BlendBandLighting  (band a -> a+1 colour lerp + luma)
//   0x5b8b04  VIBE_SkyColor_ApplyAmbientBlend  (fog/ambient cross-band lerp)
//   0x43f460  VIBE_SkyColor_ApplyScaledBlend   (brightness*0.01 -> [0,1] alpha)
//
// THE SKY BACKDROP (what the live 3D city frame shows behind the terrain):
// this DDraw/D3D engine has NO sky dome/gradient GEOMETRY. The visible sky is
// the framebuffer CLEAR COLOUR (`dword_649DD4`), and that colour is the
// time-of-day fog/sky colour produced by VIBE_SkyColor_ApplyAmbientBlend
// @0x5b8b04 -> VIBE_Render_ConfigureFog @0x5ae384 (sets dword_649DD4 = colour).
// BeginUniverseFrame @0x5b3900 clears the viewport to dword_649DD4 (via
// VIBE_Render_ClearViewport @0x5dd464 / VIBE_Render_ClearRect @0x434728) BEFORE
// VIBE_Floor_RenderTerrain @0x5b3a2f. So "render the sky" == fill the surface
// with the time-of-day sky colour, first, before terrain. RenderSky() below is
// that fill; SkyFogColor() is the 1:1 colour math.
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
//       luma = g'*0.59 + r'*0.30 + b'*0.11   (flt_628728=.59 G, flt_62872C=.30 R, flt_628730=.11 B)
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

// gilde.exe flt_64A018 = 1.0f — the global fog-range scale ApplyAmbientBlend
// multiplies the interpolated near/far distances by before ConfigureFog.
constexpr float kFogRangeScale = 1.0f; // flt_64A018 = 0x3F800000

// One per-band sky/fog record as the runtime stores it in the parallel triple
// arrays (dword_13FD170 packed colour / flt_13FD174 near / flt_13FD178 far,
// each stride 3 dwords == one entry).  CopyGradientEntry @0x5b84c4 populates
// these from the engine's current clear colour (dword_649DD4) + the staged
// fog distances per band; ApplyAmbientBlend then cross-fades two of them.
struct SkyFogBand {
    u32   packed;  // dword_13FD170[3*i] — device-packed sky/clear colour (bytes B2,B1,B0)
    float near_;   // flt_13FD174[3*i]   — fog near distance
    float far_;    // flt_13FD178[3*i]   — fog far distance
};

// Result of the cross-band sky/fog blend: the device-packed sky colour that
// becomes the framebuffer clear (dword_649DD4) plus the fog near/far that go
// to VIBE_Render_ConfigureFog.
struct SkyFog {
    u32   color;   // packed sky/clear colour (== what fills the surface)
    float near_;   // ConfigureFog arg1 (v12 = near*scale)
    float far_;    // ConfigureFog arg2 (v17 = far*scale)
    bool  applied; // false on the out-of-range early-out (return 0)
};

// gilde.exe 0x5b8b04 — VIBE_SkyColor_ApplyAmbientBlend (__userpurge, al).
// Cross-fade band `a` -> band `b` by `frac` in [0,1]: per-byte (B2,B1,B0) lerp
// of the packed colour (truncate-toward-zero per channel, VIBE_Coord_ConvertX),
// and a lerp of each band's near/far fog distance scaled by kFogRangeScale.
// Rejects (applied=false, colour 0) when a>=6 || b>=6 || frac<0 || frac>1,
// matching the original's early-out. `bands` is the 6-entry runtime table.
SkyFog BlendAmbientFog(const SkyFogBand bands[6], unsigned a, unsigned b, float frac);

// Convenience wrapper for the live frame: returns just the device-packed sky
// colour produced by BlendAmbientFog (the value the engine puts in dword_649DD4
// and clears the viewport to). On the reject path returns 0.
u32 SkyFogColor(const SkyFogBand bands[6], unsigned a, unsigned b, float frac);

// =============================================================================
// RenderSky — fill the universe surface with the time-of-day sky colour.
//
// This is the software-rasterizer equivalent of the engine's pre-terrain clear
// (BeginUniverseFrame @0x5b3900 -> ClearViewport @0x5dd464 / ClearRect
// @0x434728, both filling with dword_649DD4). It writes `packedColor` across
// the whole surface (respecting the surface's clip rect), so the 3D frame shows
// the real sky colour instead of a flat clear. Must run BEFORE terrain/objects.
//
// The packed colour must already be in the surface's native pixel format — it is
// the device colour dword_649DD4 (see SkyFogColor / BlendAmbientFog above). For
// 16bpp surfaces the low 16 bits are used; for 32bpp the full dword.
// =============================================================================
void RenderSky(Surface* surf, u32 packedColor);

// =============================================================================
// PER-SCENE SKY BAND TABLE — the runtime data the time-of-day clear is built from
// (closes the wave-6 rule-8 gap: the 6-band gradient was per-scene file data that
// had not been surfaced, so the sky used a fallback colour).
//
// CHAIN (all 1:1 from the binary):
//   1. VIBE_Scene_LoadFromStream @0x5e7e38 reads the per-scene day/dusk/night sky
//      colours into flt_13FD1B8.. (band ambient) + dword_13FD1D0/D4/D8 (the 6
//      fog/shade keyframes per band). scene_load.{h,cpp} (owned elsewhere) already
//      parses these bytes into render::SceneHeader::lights[]; LoadSkyBands() below
//      ADAPTS that parsed header into the SkySceneTable the engine globals hold.
//   2. VIBE_SkyColor_BlendBandLighting @0x5b85e4 cross-fades band -> band+1 over
//      its 6 keyframes -> the dword_13FD170/174/178 scratch (SkyColor_BuildFogScratch).
//   3. VIBE_SkyColor_ApplyAmbientBlend @0x5b8b04 cross-fades two of those 6 scratch
//      triples by the time-of-day fraction -> the framebuffer clear colour
//      (BlendAmbientFog, above).
//
// LoadSkyBands is the tiny accessor bridging the scene_load handoff (rule: I do not
// edit scene_load; I read its SceneHeader). It maps the parsed light-rig table onto
// the SkySceneTable field-for-field exactly as 0x5e7e38 fills the globals:
//   light[i].pos        -> ambient[i]           (flt_13FD1B8/1BC/1C0)
//   light[i].color      -> secondary[i]         (flt_13FD1C4/1C8/1CC)
//   light[i].keyframe[k]-> fog[i][k] {id->packed, a->near, b->far}
// =============================================================================

// gilde.exe 0x5e7e38 (light-rig portion) — adapt the scene_load-parsed header into
// the runtime SkySceneTable the sky/fog blend indexes.  Returns the populated
// table; `band_count` is the header's light count (4/6/7).  Bands the file did not
// supply stay zero (matching the BSS zero-init the loader leaves them at).
SkySceneTable LoadSkyBands(const SceneHeader& header);

// Convenience: full time-of-day sky/fog colour from a loaded scene table.
//   build  = SkyColor_BuildFogScratch(table, band, blend)   (step 2)
//   result = BlendAmbientFog(scratch, fogA, fogB, frac)      (step 3)
// `band`/`blend` come from the day-cycle (ComputeSunState: band, blend); `fogA`/
// `fogB`/`frac` select which of the 6 built scratch triples to cross-fade for the
// clear colour (the same band/blend split feeds both in the live frame — fogA=fogB
// with frac in [0,1] picks one of the 6 keyframes' colour ramp).  On any reject
// returns {applied=false}.
SkyFog ComputeSkyFog(const SkySceneTable& table, unsigned band, float blend,
                     unsigned fogA, unsigned fogB, float frac);

} // namespace guild::render

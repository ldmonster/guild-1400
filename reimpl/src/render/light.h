#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — vertex lighting + the shade-ramp table the software
// rasterizer consumes. Faithful 1:1 reconstruction of the gilde.exe lighting
// cluster (d3_engine.c / d3_light) functions:
//
//   0x5c8218  VIBE_Light_BuildObjectCache       (per-vertex RGB->shade reduce)
//   0x5c7f04  VIBE_Light_ApplyVertexShading     (face-flag + per-vertex shade)
//   0x5c6af0  VIBE_Light_SetGrayColorThunk      (broadcast a grey byte ->dword)
//   0x5c88f8  VIBE_Light_InitFalloffTable       (1024-entry cosine falloff LUT)
//   0x42dd4c  VIBE_Light_ComputeRayFalloff      (sun-ray distance falloff)
//
// THE 768*idx SHADE TABLE (reconcile with raster.cpp)
// ---------------------------------------------------------------------------
// The per-vertex *light/shade index* is an 8-bit value in [0,255] (the engine
// clamps to 254 in the depth path, to 255 in the colour path). It is stored in
// the Vertex record at +66 (geometry_types.h Vertex::lightIdx) and at the
// colour bytes +68/+69/+70. The software rasterizer
// (render/raster.cpp FillTexturedSpansShaded) interpolates that byte across the
// triangle and writes it straight to the 8-bit framebuffer — i.e. the shaded
// affine span value IS the light index.
//
// The draw-list SORT KEY is `768 * maxLightIdx` (render/mesh.cpp). 768 = 256*3
// is the *stride of one shade ramp*: a shade table holds, per light index L, a
// 256-entry RGB triple (256*3 = 768 bytes). ShadeRampOffset(L) returns that
// byte offset so callers that key the palette/light table by light index do the
// identical multiply. BuildShadeRamp() reconstructs one such 256*256*3 ramp
// (the black->full-colour fade per palette entry) so a test can verify the
// rasterizer-facing bytes against a Python reference.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Recovered float constants (verified via get_bytes).
//   Grey-reduce weights — gilde.exe BuildObjectCache grayscale path (629... ):
//     flt_628C88 = 0.58999997  (green)   luma-style weights
//     flt_628C8C = 0.30000001  (red)
//     flt_628C90 = 0.10999999  (blue)
//   flt_628C94 = 255.0  (clamp ceiling / normalise numerator)
//   ApplyVertexShading: flt_628C64=0.30 r, flt_628C68=0.59 g, flt_628C6C=0.11 b,
//   flt_628C70 = 0.00390625 (==1/256), flt_628C74 = 255.0 clamp.
// ---------------------------------------------------------------------------
constexpr float kLumaGreen = 0.58999997f;  // flt_628C88 / flt_628C68
constexpr float kLumaRed   = 0.30000001f;  // flt_628C8C / flt_628C64
constexpr float kLumaBlue  = 0.10999999f;  // flt_628C90 / flt_628C6C
constexpr float kShadeMax  = 255.0f;       // flt_628C94 / flt_628C74

// ---------------------------------------------------------------------------
// Light record — the 20-float (80-byte) per-vertex-light cache entry the engine
// accumulates in VIBE_Light_BuildObjectCache (v2[2] = entry count, each entry
// `v4 += 20` floats). The original packed scene-graph state around it; here we
// model the lighting-relevant fields the cache loop reads/writes.
//   floats [12]/[13]/[14] = accumulated B/G/R light (set from flt_64A074/78/7C)
//   byte   +68 = reduced B,  +69 = reduced G,  +70 = reduced R / grey shade idx
// ---------------------------------------------------------------------------
struct LightVertex {
    float r;   // entry[14]  accumulated red   light
    float g;   // entry[13]  accumulated green light
    float b;   // entry[12]  accumulated blue  light
    // outputs (entry bytes +68/+69/+70 in the original 80-byte record)
    u8 outB;   // +68
    u8 outG;   // +69
    u8 outR;   // +70  (grayscale mode: the single shade index)
};

// gilde.exe 0x5c8218 (grayscale branch, byte_649D70 == 0).
// gray = g*0.59 + r*0.30 + b*0.11, truncated toward zero, clamped to 255.
// Returns the 8-bit shade index (also written to lv.outR).
u8 ComputeGrayShade(LightVertex& lv);

// gilde.exe 0x5c8218 (colour branch, byte_649D70 != 0).
// m = max(r,g,b); if m > 254.0 (1132396544 == bit-pattern of 254.99999) the
// three channels are scaled by 255.0/m. Truncated to bytes, stored at
// outB/outG/outR. Returns the shade index (outR).
u8 ComputeColorShade(LightVertex& lv);

// gilde.exe 0x5c7f04 — VIBE_Light_ApplyVertexShading per-vertex inner value.
// shade = clamp((r*0.30 + g*0.59 + b*0.11 + ambientHi) * (1/256) * scale, 255)
// where `scale` is the low byte of the per-object light word and `ambientHi`
// its high byte. Truncated toward zero. (a1[0]+48/52/56 are the vertex normal
// dotted with the light; here we pass the dot result directly as r/g/b.)
u8 ApplyVertexShade(float r, float g, float b, u8 ambientHi, u8 scale);

// gilde.exe 0x5c6af0 — VIBE_Light_SetGrayColorThunk. Broadcasts an 8-bit grey
// value into all four bytes of a dword (the fill value passed to the dword
// memory-fill). Returns 0x00GGGGGG... i.e. grey replicated across 4 bytes.
u32 BroadcastGrayDword(u8 gray);

// NOTE: VIBE_Light_InitFalloffTable (0x5c88f8) is DEFERRED. Its 1024-entry LUT
// recurrence drives off VIBE_Math_AcosGuarded (0x5f0b9c), a two-operand x87
// `acos` whose st0/st1 stack ordering must be modelled to reproduce the table
// bit-exactly; not done here (see final report).

// ShadeRampOffset(L) = 768 * L — the byte offset of light index L's 256-entry
// RGB shade table. Mirrors the `768 * maxLightIdx` sort-key multiply.
inline u32 ShadeRampOffset(int lightIdx) { return 768u * (u32)lightIdx; }

// Build a 256-light x 256-entry RGB shade ramp (256*768 bytes). For light index
// L (0..255) and palette entry P (0..255) the stored triple is the entry colour
// scaled by L/255 (black at L=0, full at L=255). Layout: out[768*L + 3*P + 0/1/2]
// = scaled R/G/B. `pal` is the 256-entry source palette (3 bytes RGB each).
void BuildShadeRamp(u8* out /*256*768*/, const u8 pal[256 * 3]);

// The .ed3's 7-band time-of-day rig is applied via render::BlendBandLighting
// (the ambient blend, gilde.exe 0x5b85e4) — reconstructed in render/sky.h; do
// not redefine it here. play::ComputeSceneAmbient builds its SkyBandColor[7] from
// the parsed SceneLight rig (SceneLight::pos = the band ambient colour).

// gilde.exe 0x5c88f8 — VIBE_Light_InitFalloffTable: the 1024-entry angular
// falloff LUT the per-vertex light accumulation indexes by the (negated) cosine.
//   out[k] = 1.0 - (2/pi) * acos(-k/1023)         for k in [0, 1024)
// (flt_628CB8 = 2/pi == 0.63661975, step flt_628CB4 = -1/1023; the consumer
// indexes it with (int)(NdotL * -1023), NdotL in [-1,0].)  acos via std::acos
// (VIBE_Math_AcosGuarded clamps its argument to [-1,1]).
void BuildFalloffLUT(float out[1024]);

// gilde.exe 0x5c6f90 — VIBE_Light_ApplyToCachedVertices: per-vertex contribution
// of one scene light, accumulated into `accum` (RGB, engine 0..255 light scale).
//
// POINT light (object type != 7): given the vertex world position/normal and the
// light's world position + colour (+92) + range (+144) + intensity (+148) +
// rangeParam (+152):
//   d = vpos - lightPos ; distSq = |d|^2
//   if distSq >= range^2: no contribution (out of range)
//   NdotL = normal . normalize(d)            (d points away from the light)
//   if NdotL >= 0: no contribution (faces away)
//   atten  = intensity * 10 (flt_628C20) * objScale / (rangeParam * distSq)
//   factor = atten * LUT[ clamp((int)(NdotL * -1023), 0, 1023) ]
//   accum += colour * factor
void AccumulatePointLight(const float vpos[3], const float vnormal[3],
                          const float lightPos[3], const float color[3],
                          float range, float intensity, float rangeParam,
                          float objScale, const float lut[1024], float accum[3]);

// DIRECTIONAL ("sun", object type == 7): no distance term; the intensity is
// scaled by 0.001 (flt_628C2C). `dir` is the light direction (normalized here).
//   NdotL = normal . normalize(dir) ; if NdotL >= 0: nothing
//   factor = intensity * 0.001 * objScale * LUT[clamp((int)(NdotL*-1023),0,1023)]
//   accum += colour * factor
void AccumulateDirectionalLight(const float vnormal[3], const float dir[3],
                                const float color[3], float intensity,
                                float objScale, const float lut[1024], float accum[3]);

// gilde.exe 0x42dd4c — VIBE_Light_ComputeRayFalloff(dx, dy, dist).
// Sun-ray falloff: returns a scalar in [0,1] that fades a god-ray sample by its
// screen distance. Faithful to the original branch structure.
float ComputeRayFalloff(float dx, float dy, float dist);

} // namespace guild::render

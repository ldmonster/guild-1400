#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — distance fog + time-of-day fog/ambient cross-band blend.
// Faithful 1:1 reconstruction of the gilde.exe fog cluster (d3_engine.c):
//
//   0x5ae2a0  VIBE_Render_SetFogRange          (near/far -> slope + near^2)
//   0x5ae384  VIBE_Render_ConfigureFog         (gated near<far range update)
//   0x5b8b04  VIBE_SkyColor_ApplyAmbientBlend  (time-of-day cross-band fog lerp)
//   (0x5ac970 inner) ComputeFogFactor          (per-distance fog byte; recovered
//                                               from VIBE_Particle_UpdateBillboards)
//
// FOG STATE (BSS in the original; modelled as a struct here so the math is
// re-entrant/testable). Offsets are the original global addresses:
//   flt_13FC5AC  near plane            flt_13FC568  far plane
//   flt_13FC544  near^2  (= near*near) flt_13FC58C  density slope = 255/(far-near)
// SetFogRange recomputes near^2 and the density slope whenever near/far change.
//
// PER-DISTANCE FOG FACTOR (the per-vertex fog factor, recovered from the
// billboard pass @0x5ac9aa AND the terrain pass VIBE_Floor_TransformTileGeometry
// @0x5be668 — byte-identical math). For a sample at squared camera-distance d2:
//   d2 <= near^2                 -> 255            (inside near plane: no fog)
//   else  e = (sqrt(d2) - near) * slope
//         e' = (e > 255.0) ? 255.0 : e            (clamp ceiling = 255.0)
//         factor = trunc(255.0 - e')              (toward zero -> 8-bit byte)
// CLAMP NOTE (1:1 correction): both passes clamp to 255.0. The billboard branch
// loads the clamp as the inline immediate `mov eax, 406FE000h` -> the HIGH dword
// of a double (low dword 0) = 0x406FE00000000000 = 255.0 (verified: disasm
// 0x5aca75; get_bytes(dbl_628074)=...E0 6F 40 and get_bytes(dbl_628B34)=same).
// Hex-Rays rendered that immediate as `1081073664` (0x40700000) -> 256.0 -> a -1
// wrap at full fog; that is a DECOMPILER ARTIFACT. The true value 0x406FE000 is
// 255.0, so the factor floors at 0 (255-255), NO wrap. Result is in [0,255]:
// 255 = no fog (near), 0 = full fog (>= far).
//
// THE COLOUR BLEND (D3D vertex fog, reconstructed for the software raster).
// The original delegated the blend to Direct3D fixed-function vertex fog:
// VIBE_Render_BeginScene @0x5e010c sets D3DRENDERSTATE_FOGENABLE(28)=byte_649DD8
// and D3DRENDERSTATE_FOGCOLOR(34)=dword_649DD4; FOGTABLEMODE is never set, so
// pixel/table fog is OFF and D3D uses the per-VERTEX fog factor we wrote into the
// FVF specular byte (vertex+79), interpolated linearly across the span. Per the
// D3D fog spec the blend is, per channel, with f = factor/255 in [0,1]:
//   out = f * src + (1 - f) * fogColor
// Rule 3 (D3D -> software raster) requires we reconstruct that blend; see
// BlendFogChannel / BlendFogRgb / BlendFog565 below. f=255 -> src unchanged,
// f=0 -> pure fog colour. This is the clean per-pixel hook the raster layer
// applies after fetching the textured/shaded colour (see fog-render-wave6.md).
// =============================================================================
namespace guild::render {

// flt_628088 / dbl_628074 / dbl_628B34 = 255.0 — fog slope numerator and the fog
// factor clamp ceiling (both billboard + terrain passes clamp to 255.0; see hdr).
constexpr float  kFogSlopeNumer = 255.0f;   // flt_628088 = 0x437F0000
constexpr double kFogFactorMax  = 255.0;    // dbl_628074 / dbl_628B34 = 0x406FE000..
constexpr double kFogFactorClampHi = 255.0; // 1:1: the clamp ceiling is 255.0 (NOT
                                            // 256.0; the Hex-Rays 256.0 was a
                                            // decompiler artifact — verified disasm)

// Mirrors the four BSS fog dwords the renderer reads each frame.
struct FogState {
    float nearPlane;   // flt_13FC5AC
    float farPlane;    // flt_13FC568
    float nearSq;      // flt_13FC544  (nearPlane^2)
    float densitySlope;// flt_13FC58C  (255 / (farPlane - nearPlane))
    // ConfigureFog also latches the active fog colour + enable, modelled here:
    float blendNear;   // flt_13FC5FC  (last ConfigureFog near)
    float blendFar;    // flt_13FC5F8  (last ConfigureFog far)
    i32   color;       // dword_649DD4 (packed fog colour)
    bool  enabled;     // byte_649DD8  (fog on/off = (near < far))
};

// gilde.exe 0x5ae2a0 — VIBE_Render_SetFogRange(near, far) math core.
// Stores near/far, recomputes near^2 and densitySlope = 255/(far-near). Only
// the float-state mutation is reconstructed; the view-transform refresh and
// object invalidation it triggers are renderer side effects (out of scope, see
// header). Returns true if the range actually changed (the original's early-out
// `if (flt_13FC5AC != near || flt_13FC568 != far)`).
bool SetFogRange(FogState& s, float nearPlane, float farPlane);

// gilde.exe 0x5ae384 — VIBE_Render_ConfigureFog(near, far, color).
// `fogEnabledGlobal` = byte_649D70 (renderer has fog), `featureBit` = the
// (byte_140806D & 0x20) feature flag. When both set: if near < far it updates
// the range via SetFogRange; it always latches blendNear/blendFar/color and sets
// enabled = (near < far). Returns true when the configuration was applied.
bool ConfigureFog(FogState& s, float nearPlane, float farPlane, i32 color,
                  bool fogEnabledGlobal, bool featureBit);

// Per-vertex fog factor (recovered from the billboard pass @0x5ac9aa and the
// terrain pass @0x5be668 — identical math). `d2` is the squared camera-space
// distance of the vertex. Returns the factor the original truncates into the
// vertex+79 FVF specular byte; always in [0,255] (255 near, 0 at/beyond far).
int ComputeFogFactor(const FogState& s, float d2);

// ---------------------------------------------------------------------------
// THE FOG-COLOUR BLEND — reconstruction of D3D fixed-function VERTEX fog (Rule 3
// D3D -> software raster). The per-vertex fog factor (ComputeFogFactor, written
// to vertex+79) is interpolated linearly across the span by the rasterizer; this
// per-pixel blend applies the standard D3D fog equation, per channel:
//     out = (factor * src + (255 - factor) * fogColor + 127) / 255
// (factor in [0,255]; +127 rounds to nearest, matching D3D's 8-bit blend). The
// fog colour is the packed 0x00RRGGBB latched in FogState::color / dword_649DD4.
// ---------------------------------------------------------------------------

// Single 8-bit channel blend: lerp src->fog by (255-factor)/255 with /255 round.
u8 BlendFogChannel(u8 src, u8 fogChannel, int factor);

// Blend a full 0x00RRGGBB src colour toward fogColor by the fog factor. Returns
// the packed 0x00RRGGBB result. `fogColor` is FogState::color (dword_649DD4).
u32 BlendFogRgb(u32 srcRgb, u32 fogColor, int factor);

// The raster-facing entry: blend a 16-bit RGB565 surface pixel toward the fog
// colour by `factor`, returning the new RGB565 pixel. The fog colour is unpacked
// from `fogColor` (0x00RRGGBB), the channels are blended in 8-bit, and the result
// re-packed to RGB565. `factor == 255` returns `src565` unchanged (fast path:
// the engine only ran this when fog was enabled and factor < 255). This is the
// span-level hook the textured/shaded raster applies per pixel (see progress doc).
u16 BlendFog565(u16 src565, u32 fogColor, int factor);

// ---------------------------------------------------------------------------
// RASTER-SPAN FOG HOOK (wave-6 W6-INTEGRATE).
//
// The textured/shaded span fill (raster.cpp FillSpanTextured / FillTexturedSpans
// Shaded, raster_textured.cpp FillSpanLoop) reads ONE process-global span-fog
// record and, when enabled, applies BlendFog565 per pixel — the software
// realisation of the D3D fixed-function vertex-fog blend (byte_649DD8 gate). It
// is a process global because the original kept the fog enable + colour in the
// renderer globals (byte_649DD8 / dword_649DD4) the span body read directly.
//
// DEFAULT DISABLED: SpanFog().enabled == false out of the box, so every existing
// rasterized frame is BYTE-IDENTICAL until a caller turns fog on.
//
// THE FACTOR (wave-7 W7-FOGPIX). `SpanFog().enabled` + `SpanFog().color` are the
// fog enable + colour the span body reads. The per-pixel fog FACTOR is now the
// engine's true D3D fixed-function VERTEX fog: the per-vertex factor (vertex+79,
// from ComputeFogFactor @0x5ac9aa/@0x5beb0b) is carried as a THIRD 16.16 span
// channel alongside U/V (RgbzRasterState::vf/fLeft/fGrad + RasterState::fStart/
// fGrad), interpolated linearly across the triangle and blended per pixel. See
// progress/fog-perpixel-wave7.md. `SpanFog().factor` is the wave-6 PER-TRIANGLE
// constant FALLBACK used only by direct FillSpanTextured callers that do NOT seed
// the channel (RasterState::fPerPixel == false) — kept byte-identical for them.
// ---------------------------------------------------------------------------
struct SpanFogState {
    bool enabled = false;   // byte_649DD8 (span-level fog on)
    u32  color   = 0;       // dword_649DD4 (packed 0x00RRGGBB fog colour)
    int  factor  = 255;     // per-TRIANGLE constant fallback factor [0,255] (used
                            //   only when the per-pixel channel is not seeded;
                            //   255 == no fog: BlendFog565 fast-returns src)
};

// The single process-global the raster span body reads. Set by the object/terrain
// draw path before each triangle; default-disabled keeps every frame identical.
SpanFogState& SpanFog();

// One band's time-of-day fog record: a packed colour and near/far distances.
// (In the original these are three parallel arrays flt_13FD170/174/178 with a
// 12-byte stride per band — colour @+0, near @+4, far @+8.)
struct FogBand {
    u8  colorR;  // BYTE2(dword_13FD170[3*i])
    u8  colorG;  // BYTE1
    u8  colorB;  // LOBYTE
    float nearVal; // flt_13FD174[3*i]
    float farVal;  // flt_13FD178[3*i]
};

// gilde.exe 0x5b8b04 — VIBE_SkyColor_ApplyAmbientBlend(bandA, bandB, t).
// Cross-band fog interpolation feeding ConfigureFog: lerps near/far and the 3
// colour bytes between bandA and bandB by `t in [0,1]`, scales near/far by
// `distScale` (flt_64A018), truncates each colour channel toward zero, packs
// 0x00RRGGBB and calls ConfigureFog. Both band indices must be < 6 and 0<=t<=1,
// else it returns false (no change). The runtime band table is passed in `bands`
// (>= 6 entries). `fogEnabledGlobal`/`featureBit` forward to ConfigureFog.
bool ApplyAmbientBlend(FogState& s, const FogBand* bands, int bandA, int bandB,
                       float t, float distScale,
                       bool fogEnabledGlobal, bool featureBit);

} // namespace guild::render

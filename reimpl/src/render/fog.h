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
// PER-DISTANCE FOG FACTOR (the "fog density per distance", recovered from the
// billboard pass @0x5ac9aa). For a sample at squared camera-distance d2:
//   d2 <= near^2                 -> 255            (inside near plane: no fog)
//   else  e = (sqrt(d2) - near) * slope
//         e' = (e > 255.0) ? 256.0 : e            (clamp, dbl_628074 = 255.0,
//                                                  ceiling value 256.0)
//         factor = trunc(255.0 - e')              (toward zero -> 8-bit byte)
// At full fog e>255 gives 255-256 = -1, stored as a byte = 0xFF — the original's
// deliberate wrap; ComputeFogFactor returns the signed value and the caller
// stores the low byte.
// =============================================================================
namespace guild::render {

// flt_628088 / dbl_628074 = 255.0 — fog slope numerator and factor ceiling.
constexpr float  kFogSlopeNumer = 255.0f;   // flt_628088
constexpr double kFogFactorMax  = 255.0;    // dbl_628074
constexpr double kFogFactorClampHi = 256.0; // bit-pattern 0x4070000000000000 (v12)

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

// Per-distance fog factor (recovered from the billboard pass @0x5ac9aa). `d2` is
// the squared camera-space distance of the sample. Returns the signed factor the
// original truncates into a byte (callers keep the low 8 bits). See header for
// the wrap at full fog.
int ComputeFogFactor(const FogState& s, float d2);

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

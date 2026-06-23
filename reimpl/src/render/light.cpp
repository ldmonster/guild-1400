#include "render/light.h"

#include <cmath>

// =============================================================================
// guild::render lighting — implementation. See light.h for the overview and the
// 768*idx shade-table reconciliation. All arithmetic mirrors the Hex-Rays
// pseudocode of the listed gilde.exe functions (same float ops, same truncate-
// toward-zero conversions, same 0/255 clamps).
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// gilde.exe 0x5c8218 — grayscale branch (byte_649D70 == 0).
//   v21 = g*flt_628C88 + r*flt_628C8C + b*flt_628C90
//   if ((unsigned)(int)v21 >= 0xFF) shade = 0xFF else shade = (int)v21
// The compare is on the *signed* truncation reinterpreted unsigned, so any
// negative result (>= 0x80000000) also saturates to 255 — faithful to the
// original `(unsigned int)(int)v21 >= 0xFF`.
// ---------------------------------------------------------------------------
u8 ComputeGrayShade(LightVertex& lv) {
    float v21 = lv.g * kLumaGreen + lv.r * kLumaRed + lv.b * kLumaBlue;
    int   i   = (int)v21;                 // truncate toward zero (FPU->int)
    u8 shade;
    if ((u32)i >= 0xFFu)
        shade = 0xFF;
    else
        shade = (u8)i;
    lv.outR = shade;                      // *((_BYTE*)v19 + 70)
    return shade;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5c8218 — colour branch (byte_649D70 != 0).
//   m = max(r, max(g... )) over (v32=R? actually v32=[12]=b, v28=[13]=g, v33=[14]=r)
// In the original the three accumulators are v10[12]/[13]/[14] and the max is
// taken across all three; if max > 1132396544 (==254.99998f bit pattern) the
// three are scaled by 255.0/max. Truncated bytes -> +68/+69/+70.
//   v31 holds the bit-pattern; `v13 > 1132396544` is a *float* compare done on
// the int bits, equivalent to (max > 254.99998f) for the positive values here.
// We reproduce it as a float compare on the same threshold.
// ---------------------------------------------------------------------------
static constexpr float kColorNormThreshold = 254.999985f; // 0x437EFFFF

u8 ComputeColorShade(LightVertex& lv) {
    float r = lv.r, g = lv.g, b = lv.b;
    // v32=b, v28=g, v33=r in the original; max chained as in the pseudocode:
    //   v31 = (b <= g) ? g : b ;  v30 = (v31 <= r) ? r : v31
    float m = (b <= g) ? g : b;
    m = (m <= r) ? r : m;
    if (m > kColorNormThreshold) {
        float k = kShadeMax / m;          // flt_628C94 / max
        b = b * k;
        g = g * k;
        r = k * r;
    }
    lv.outB = (u8)(int)b;                 // +68
    lv.outG = (u8)(int)g;                 // +69
    lv.outR = (u8)(int)r;                 // +70
    return lv.outR;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5c7f04 — VIBE_Light_ApplyVertexShading inner value.
//   v13 = (float)(unsigned __int8)v18           ; v18 low byte  = scale
//   v17 = (dotR*0.30 + dotG*0.59 + dotB*0.11 + HIBYTE(v18)) * (1/256) * v13
//   v14 = (v17 <= 255.0) ? v17 : 255.0
//   shade = (int)v14                              ; truncate toward zero
// HIBYTE(v18) is the ambient high byte; v13 the low-byte scale.
// ---------------------------------------------------------------------------
u8 ApplyVertexShade(float r, float g, float b, u8 ambientHi, u8 scale) {
    float v13 = (float)scale;
    float v17 = (r * kLumaRed + g * kLumaGreen + b * kLumaBlue
                 + (float)ambientHi)
                * (1.0f / 256.0f)         // flt_628C70
                * v13;
    float v14 = (kShadeMax >= v17) ? v17 : kShadeMax;  // flt_628C74 >= v17
    return (u8)(int)v14;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5c6af0 — VIBE_Light_SetGrayColorThunk.
//   BYTE1(a1)=a1; v3=a1<<8; LOBYTE(v3)=BYTE1(v3); v3<<=8; LOBYTE(v3)=BYTE1(v3);
// This replicates the low byte into bytes 0..2 (and the value re-enters byte 1
// via the shifts) — the net result is the grey byte broadcast into all four
// byte lanes of the dword used for the dword-aligned memory fill.
// ---------------------------------------------------------------------------
u32 BroadcastGrayDword(u8 gray) {
    u32 g = gray;
    return g | (g << 8) | (g << 16) | (g << 24);
}

// ---------------------------------------------------------------------------
// Build a 256-light x 256-entry RGB shade ramp. out[768*L + 3*P + c] = the
// palette colour P channel c scaled by L/255 (rounded toward zero like the
// original ramp builders). 768 = ShadeRampOffset(1).
// ---------------------------------------------------------------------------
void BuildShadeRamp(u8* out, const u8 pal[256 * 3]) {
    for (int L = 0; L < 256; ++L) {
        u8* row = out + ShadeRampOffset(L);
        for (int P = 0; P < 256; ++P) {
            const u8* src = pal + 3 * P;
            // scale by L/255 with integer arithmetic (matches blackened ramps).
            row[3 * P + 0] = (u8)((src[0] * L) / 255);
            row[3 * P + 1] = (u8)((src[1] * L) / 255);
            row[3 * P + 2] = (u8)((src[2] * L) / 255);
        }
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5c88f8 — VIBE_Light_InitFalloffTable.
//   for k in [0,1024): out[k] = 1.0 - acos(k * flt_628CB4) * flt_628CB8
//   flt_628CB4 = 0x3A800000 = 1/1024 (the step); flt_628CB8 = 0x3F22F983 = 2/pi.
//   == 1.0 - (2/pi) * acos(k/1024).  (Disasm: fld 2/pi; fld 1/1024; loop fild k;
//   fmul (1/1024); acos; fmul (2/pi); fld1; fsubrp -> 1 - that.)
// The consumer (VIBE_Light_ApplyToCachedVertices) indexes with
// (int)(NdotL * flt_628C28) where flt_628C28 = -1023 and NdotL in [-1,0], so the
// index is +|NdotL|*1023 and the LUT rises 0 (grazing) -> ~1 (facing) — a softened
// Lambert. (Earlier this used a negated argument, which inverted the falloff.)
// ---------------------------------------------------------------------------
void BuildFalloffLUT(float out[1024]) {
    const float kTwoOverPi = 0.63661975f;       // flt_628CB8
    const float kStep      = 1.0f / 1024.0f;    // flt_628CB4 = 0x3A800000
    for (int k = 0; k < 1024; ++k) {
        float x = static_cast<float>(k) * kStep;    // k/1024 in [0, ~1)
        if (x < -1.0f) x = -1.0f; else if (x > 1.0f) x = 1.0f;  // AcosGuarded clamp
        out[k] = 1.0f - std::acos(x) * kTwoOverPi;
    }
}

namespace {
// flt_628C28 = -1023.0: the LUT index scale. NdotL in [-1,0] -> index in [0,1023].
inline int FalloffIndex(float ndotl) {
    int i = static_cast<int>(ndotl * -1023.0f);
    if (i < 0) i = 0; else if (i > 1023) i = 1023;
    return i;
}
} // namespace

// gilde.exe 0x5c6f90 — point-light per-vertex contribution.
void AccumulatePointLight(const float vpos[3], const float vnormal[3],
                          const float lightPos[3], const float color[3],
                          float range, float intensity, float rangeParam,
                          float objScale, const float lut[1024], float accum[3]) {
    const float dx = vpos[0] - lightPos[0];
    const float dy = vpos[1] - lightPos[1];
    const float dz = vpos[2] - lightPos[2];
    const float distSq = dx * dx + dy * dy + dz * dz;   // before normalize
    if (distSq >= range * range) return;                // out of range (v32 < v105)
    const float dist = std::sqrt(distSq > 1e-12f ? distSq : 1e-12f);
    const float inv = 1.0f / dist;
    const float ndotl = vnormal[0] * dx * inv + vnormal[1] * dy * inv + vnormal[2] * dz * inv;
    if (ndotl >= 0.0f) return;                          // faces away (v33 < 0 required)
    if (rangeParam == 0.0f) return;
    const float atten = (intensity * 10.0f * objScale) / (rangeParam * distSq);
    const float f = atten * lut[FalloffIndex(ndotl)];
    accum[0] += color[0] * f;
    accum[1] += color[1] * f;
    accum[2] += color[2] * f;
}

// gilde.exe 0x5c6f90 — directional (type-7) per-vertex contribution.
void AccumulateDirectionalLight(const float vnormal[3], const float dir[3],
                                const float color[3], float intensity,
                                float objScale, const float lut[1024], float accum[3]) {
    const float l = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (l < 1e-6f) return;
    const float nx = dir[0] / l, ny = dir[1] / l, nz = dir[2] / l;
    const float ndotl = vnormal[0] * nx + vnormal[1] * ny + vnormal[2] * nz;
    if (ndotl >= 0.0f) return;
    const float f = intensity * 0.001f * objScale * lut[FalloffIndex(ndotl)];   // flt_628C2C
    accum[0] += color[0] * f;
    accum[1] += color[1] * f;
    accum[2] += color[2] * f;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x42dd4c — VIBE_Light_ComputeRayFalloff(a1=origin, a2=end, a3=sample).
//   t = (a3-a1)/(a2-a1)
//   if (t > 0 && t >= 1.0) t = 1.0
//   else if (t <= 0.0)    t = 0.0
//   return t*t*(3.0 - t*2.0)            ; smoothstep
// ---------------------------------------------------------------------------
float ComputeRayFalloff(float a1, float a2, float a3) {
    float t = (a3 - a1) / (a2 - a1);
    float v11;
    if (t > 0.0f && t >= 1.0f) {
        v11 = 1.0f;
    } else {
        float t2 = (a3 - a1) / (a2 - a1);
        v11 = (t2 <= 0.0f) ? 0.0f : t2;
    }
    return v11 * v11 * (3.0f - v11 * 2.0f);   // flt_611CE4=3, flt_611CE0=2
}

} // namespace guild::render

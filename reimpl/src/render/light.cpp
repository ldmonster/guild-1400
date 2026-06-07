#include "render/light.h"

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

// NOTE: VIBE_Light_InitFalloffTable (0x5c88f8) is DEFERRED — see light.h. Its
// recurrence relies on the two-operand x87 acos (VIBE_Math_AcosGuarded), whose
// st0/st1 stack semantics must be modelled to reproduce the LUT bit-for-bit.

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

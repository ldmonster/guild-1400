#include "render/fog.h"
#include "render/particle.h" // TruncToward (== VIBE_Coord_ConvertX, x87 chop)

#include <cmath> // sqrtf

namespace guild::render {

// gilde.exe 0x5ae2a0 — VIBE_Render_SetFogRange.
// Original early-out: `if (flt_13FC5AC != a1 || flt_13FC568 != a2)`. Inside it
// stores near, near^2 = a1*a1, far, and the density slope flt_628088/(a2-a1)
// (255 / (far-near)). It then refreshes the view transform if far < the active
// far-clip and invalidates the current object — both are renderer side effects
// reproduced by the caller; here we keep the float-state mutation 1:1.
bool SetFogRange(FogState& s, float nearPlane, float farPlane) {
    if (s.nearPlane == nearPlane && s.farPlane == farPlane) // !(!= || !=)
        return false;
    s.nearPlane = nearPlane;                       // flt_13FC5AC = a1
    s.nearSq = nearPlane * nearPlane;              // flt_13FC544 = a1*a1
    s.farPlane = farPlane;                         // flt_13FC568 = a2
    s.densitySlope = kFogSlopeNumer / (farPlane - nearPlane); // flt_13FC58C
    return true;
}

// gilde.exe 0x5ae384 — VIBE_Render_ConfigureFog.
// Gate: `byte_649D70 && (byte_140806D & 0x20)`. v3 = (a1 < a2). If a1 < a2 it
// updates the range (SetFogRange); the original else-branch only re-runs the
// view transform with the previously-cached fog far (a renderer side effect we
// drop). It always latches flt_13FC5FC=a1, flt_13FC5F8=a2, byte_649DD8=v3,
// dword_649DD4=a3.
bool ConfigureFog(FogState& s, float nearPlane, float farPlane, i32 color,
                  bool fogEnabledGlobal, bool featureBit) {
    if (!(fogEnabledGlobal && featureBit))
        return false;
    bool nearLtFar = nearPlane < farPlane;         // v3 = a1 < (double)a2
    if (nearLtFar)
        SetFogRange(s, nearPlane, farPlane);       // VIBE_Render_SetFogRange(a1,a2)
    // (else: original refreshes the view transform with the cached far; no state
    //  change beyond what the latches below do.)
    s.blendNear = nearPlane;                        // flt_13FC5FC = a1
    s.blendFar = farPlane;                          // flt_13FC5F8 = a2
    s.enabled = nearLtFar;                           // byte_649DD8 = v3
    s.color = color;                                 // dword_649DD4 = a3
    return true;
}

// Per-distance fog factor — recovered from VIBE_Particle_UpdateBillboards
// @0x5ac9aa. d2 = flt_13FC548 (squared distance). The original:
//   if ( d2 <= flt_13FC544 )         v39 = 255.0;
//   else { v38 = (sqrt(d2) - near) * slope;
//          if ( 255.0 >= v38 ) v40 = v38; else v40 = 256.0;
//          v39 = 255.0 - v40; }
//   *(BYTE*)(v4+79) = (int)v39;       // ConvertX -> truncate toward zero
int ComputeFogFactor(const FogState& s, float d2) {
    double v39;
    if (d2 <= s.nearSq) {                            // flt_13FC544
        v39 = kFogFactorMax;                         // 255.0
    } else {
        // sqrt(d2) is the x87 double sqrt of the float d2; near/slope promote to
        // double before the subtract/multiply (no intermediate float truncation).
        double v38 = (std::sqrt((double)d2) - (double)s.nearPlane)
                   * (double)s.densitySlope;          // (sqrt(d2)-near)*slope
        // dbl_628074 (255.0) >= v38 ? keep v38 : clamp to 256.0 (v12 pattern).
        double v40 = (kFogFactorMax >= v38) ? v38 : kFogFactorClampHi;
        v39 = kFogFactorMax - v40;                    // 255.0 - v40
    }
    return TruncToward(v39);                          // (int)v39 toward zero
}

// gilde.exe 0x5b8b04 — VIBE_SkyColor_ApplyAmbientBlend.
// Guard: a1>=6 || a2>=6 || a3<0.0 || a3>1.0 -> return 0. Lerp near/far and the
// three packed colour bytes between band a1 and band a2 by a3, scale near/far by
// flt_64A018, truncate each colour channel toward zero, pack 0x00RRGGBB into
// v20 and call ConfigureFog(near*scale, far*scale, v20). Sets dword_649F08=a1.
bool ApplyAmbientBlend(FogState& s, const FogBand* bands, int bandA, int bandB,
                       float t, float distScale,
                       bool fogEnabledGlobal, bool featureBit) {
    if ((unsigned)bandA >= 6u || (unsigned)bandB >= 6u || t < 0.0f || t > 1.0f)
        return false;

    const FogBand& A = bands[bandA];
    const FogBand& B = bands[bandB];

    // Colour-byte lerps. Each: (B.ch - A.ch)*t + A.ch, then ConvertX truncate.
    // Channels operate on the original BYTE2/BYTE1/LOBYTE = R/G/B order.
    int cr = TruncToward((double)((int)B.colorR - (int)A.colorR) * t
                         + (double)(int)A.colorR);              // v6 -> BYTE2
    int cg = TruncToward((double)((int)B.colorG - (int)A.colorG) * t
                         + (double)(int)A.colorG);              // v11 -> BYTE1
    int cb = TruncToward((double)((int)B.colorB - (int)A.colorB) * t
                         + (double)(int)A.colorB);              // v15 -> LOBYTE
    // Pack 0x00RRGGBB (v20: BYTE2=R, BYTE1=G, LOBYTE=B).
    i32 packed = (i32)(((u32)(u8)cr << 16) | ((u32)(u8)cg << 8) | (u32)(u8)cb);

    // Near/far lerp then * flt_64A018.  v18 = (B.near-A.near)*t + A.near.
    float fogNear = ((B.nearVal - A.nearVal) * t + A.nearVal) * distScale; // v12
    float fogFar  = ((B.farVal  - A.farVal)  * t + A.farVal)  * distScale; // v17

    ConfigureFog(s, fogNear, fogFar, packed, fogEnabledGlobal, featureBit);
    // dword_649F08 = a1 (active fog band) — tracked by the caller in the original.
    return true;
}

} // namespace guild::render

#include "render/rain.h"
#include "render/particle.h"
#include "render/surface.h"     // SurfaceDrawLine
#include "render/colorformat.h" // UnpackColor
#include "util/matrix.h"
#include "crt/rand.h"
#include <cmath>

namespace guild::render {

namespace {
// Recovered constants (get_bytes; bit-exact). See rain.h / report.
constexpr double kRainRandNorm = 3.0518509447574615e-05; // flt_6115E0 (1/32767)
constexpr float  kSeedMul  = 2.0f;     // flt_6115E4
constexpr float  kSeedVelA = 0.25f;    // flt_6115E8 (also pos bias add)
constexpr float  kSeedVelB = 0.05000000074505806f; // flt_6115EC
constexpr float  kSeedBias = -0.5f;    // flt_6115F0

constexpr float  kDtScale  = 0.0024999999441206455f; // flt_611670
constexpr float  kHalf     = 0.5f;     // flt_611674 (z velocity scale)
constexpr float  kTwo      = 2.0f;     // flt_611678
constexpr float  kThree    = 3.0f;     // flt_61167C (projection denom bias)
constexpr float  kThird    = 0.3333333432674408f; // flt_611680
constexpr double kClampLo  = -1000.0;  // dbl_611684
constexpr double kClampHi  = 1000.0;   // dbl_61168C
constexpr double kWrapLo   = -1.0;     // dbl_611694
constexpr double kWrapAdd2 = 2.0;      // dbl_61169C
constexpr double kWrapSub2 = -2.0;     // dbl_6116A4
constexpr float  kGravY    = 0.75f;    // gravity magnitude (0,-0.75,0)
constexpr float  kAnchorZ  = 2.5f;     // anchor billboard (0,0,2.5)

// 0x429c38 render colour constants (get_bytes; bit-exact).
constexpr float  kCountFade = 0.0005000000237f; // flt_611768 (0x3A03126F)
constexpr float  kStreakR   = 96.0f;            // flt_61176C
constexpr float  kStreakG   = 128.0f;           // flt_611770

// VIBE_Coord_ConvertX @0x5c6b08 — it SETS the x87 CW high byte to 0x1F (RC=11 =
// round-toward-ZERO) before frndint, then restores, so it TRUNCATES toward zero
// — the engine does NOT leave the default round-to-nearest here. Verified:
// decompile 0x5c6b08 + the @0x429c38 rain call sites (ConvertX(); v36 = (int)v10).
inline int ConvertXTrunc(double x) {
    return (int)std::trunc(x);
}
} // namespace

void RainSeedDrops(RainSystem& sys) {
    int n = sys.count;
    RainDrop* d = sys.drops;
    if (!d || n <= 0)
        return;
    for (int i = 0; i < n; ++i) {
        RainDrop& s = d[i];
        s.px = (float)((double)crt::RandNext() * kRainRandNorm + kSeedBias) * kSeedMul;
        s.py = (float)((double)crt::RandNext() * kRainRandNorm + kSeedBias) * kSeedMul;
        s.pz = (float)((double)crt::RandNext() * kRainRandNorm + kSeedBias) * kSeedMul;
        s.size = (float)((double)crt::RandNext() * kRainRandNorm * kSeedVelA + kSeedVelA);
        s.d0 = (float)((double)crt::RandNext() * kRainRandNorm * kSeedVelB + kSeedVelA);
        s.d1 = (float)((double)crt::RandNext() * kRainRandNorm * kSeedVelB + kSeedVelA);
    }
}

// gilde.exe 0x4294d4 — VIBE_Rain_UpdateDrop (__userpurge a1@<eax>=system, a2=dt).
// Faithful per-drop rain integrator. The drop system `sys` carries the binary's
// +0x1C/+0x20 wind direction and +0x40/+0x50 prev-anchor/prev-eye snapshots used
// for camera-motion drift. `cam` views the active camera/world block
// (dword_13FCD1C): cam.eye=+76.., cam.anchor=+132.., cam.m=+396.. (3x3, the same
// col-major grouping snow uses). `vp` is the device viewport (dword_13ECE58..64).
void RainUpdateDrop(RainSystem& sys, float dt, const SnowCamera& cam,
                    const SnowViewport& vp) {
    // --- Per-system setup (runs before the count<=0 early-out in the binary;
    //     the snapshot of prev-anchor/prev-eye happens unconditionally) ---------

    // Anchor billboard offset: euler matrix of (prevAnchor - cameraAnchor),
    // then off = (0,0,2.5) * eulerMatrix  =>  off_k = 2.5 * em[8/9/10]. (0x4294fc)
    float euler[3] = {sys.prevAnchor[0] - cam.anchor[0],
                      sys.prevAnchor[1] - cam.anchor[1],
                      sys.prevAnchor[2] - cam.anchor[2]};
    float em[16];
    util::MatrixFromEuler(euler, em);                 // 0x429531
    float off0 = kAnchorZ * em[8];                    // 0x42959f  v47
    float off1 = kAnchorZ * em[9];                    // 0x4295a1  v48
    float off2 = kAnchorZ * em[10];                   // 0x4295a4  v49 (computed; unused)

    // Snapshot camera anchor into the rain prev-anchor block. (0x4295b2)
    sys.prevAnchor[0] = cam.anchor[0];
    sys.prevAnchor[1] = cam.anchor[1];
    sys.prevAnchor[2] = cam.anchor[2];

    // Camera-motion drift: d = (prevEye - cameraEye); drift = d*camMatrix*0.0025.
    // camMatrix multiply (col-major): out_k = d.x*m[k] + d.y*m[3+k] + d.z*m[6+k].
    // (0x4295c7 reads [ecx+50h]-[cam+4Ch].. ; transform 0x429602; *0.0025 0x42964e)
    float d[3] = {sys.prevEye[0] - cam.eye[0], sys.prevEye[1] - cam.eye[1],
                  sys.prevEye[2] - cam.eye[2]};
    float drift0 = (d[0] * cam.m[0] + d[1] * cam.m[3] + d[2] * cam.m[6]) * kDtScale; // v43
    float drift1 = (d[0] * cam.m[1] + d[1] * cam.m[4] + d[2] * cam.m[7]) * kDtScale; // v44
    float drift2 = (d[0] * cam.m[2] + d[1] * cam.m[5] + d[2] * cam.m[8]) * kDtScale; // v45

    // Snapshot camera eye into the rain prev-eye block. (0x429690)
    sys.prevEye[0] = cam.eye[0];
    sys.prevEye[1] = cam.eye[1];
    sys.prevEye[2] = cam.eye[2];

    // Wind basis vector = (windX, 0, windZ) * camMatrix. (0x4296cb)
    float wind0 = sys.windX * cam.m[0] + 0.0f * cam.m[3] + sys.windZ * cam.m[6]; // v34
    float wind1 = sys.windX * cam.m[1] + 0.0f * cam.m[4] + sys.windZ * cam.m[7]; // v35
    float wind2 = sys.windX * cam.m[2] + 0.0f * cam.m[5] + sys.windZ * cam.m[8]; // v36

    // Gravity basis vector = (0, -0.75, 0) * camMatrix. (0x42974b)
    float g0 = 0.0f * cam.m[0] + (-kGravY) * cam.m[3] + 0.0f * cam.m[6]; // v40
    float g1 = 0.0f * cam.m[1] + (-kGravY) * cam.m[4] + 0.0f * cam.m[7]; // v41
    float g2 = 0.0f * cam.m[2] + (-kGravY) * cam.m[5] + 0.0f * cam.m[8]; // v42

    // Viewport half-extents (integer >>1) and centre. (0x429797)
    int halfW = (vp.x1 - vp.x0) >> 1;        // (dword_13ECE60-58)>>1
    int halfH = (vp.y1 - vp.y0) >> 1;        // (dword_13ECE64-5C)>>1
    float fHalfW = (float)halfW;             // v62
    float fHalfH = (float)halfH;             // v61
    float maxHalf = fHalfW > fHalfH ? fHalfW : fHalfH; // v60 (fcomp/ja)
    float cx = (float)vp.x0 + fHalfW;        // v64
    float cy = (float)vp.y0 + fHalfH;        // v65

    RainDrop* drops = sys.drops;
    int n = sys.count;
    if (!drops || n <= 0)                     // 0x429831 test edi/jle
        return;

    float depthBias = maxHalf * kThird;       // v63 = v60 * flt_611680 (0x429840)

    for (int i = 0; i < n; ++i) {
        RainDrop& s = drops[i];

        // X axis ----------------------------------------------------------------
        float vX = s.d1 * g0 + s.d0 * wind0;                  // v66 (0x42984d)
        double px = (double)dt * vX + (double)drift0 + (double)off0 + (double)s.px;
        s.px = (float)px;
        if (px < kClampLo || px > kClampHi)                  // 0x429891/4298a3
            s.px = 0.0f;
        while ((double)s.px < kWrapLo)                       // 0x4298ae
            s.px = (float)((double)s.px + kWrapAdd2);
        while ((double)s.px >= 1.0)                          // 0x429926 (fld1; fcomp)
            s.px = (float)((double)s.px + kWrapSub2);

        // Y axis ----------------------------------------------------------------
        float vY = s.d1 * g1 + s.d0 * wind1;                 // v67 (0x42994e)
        double py = (double)dt * vY + (double)drift1 + (double)off1 + (double)s.py;
        s.py = (float)py;
        if (py < kClampLo || py > kClampHi)
            s.py = 0.0f;
        while ((double)s.py < kWrapLo)
            s.py = (float)((double)s.py + kWrapAdd2);
        while ((double)s.py >= 1.0)
            s.py = (float)((double)s.py + kWrapSub2);

        // Z axis ----------------------------------------------------------------
        // NOTE the binary's asymmetry: Z adds drift2*2.0, NOT off2 (off.z is dead).
        float vZ = s.d1 * g2 + s.d0 * wind2;                 // v68 (0x429a..)
        double pz = (double)dt * vZ + (double)drift2 * kTwo + (double)s.pz;
        s.pz = (float)pz;
        if (pz < kClampLo || pz > kClampHi)
            s.pz = 0.0f;
        while ((double)s.pz < kWrapLo)
            s.pz = (float)((double)s.pz + kWrapAdd2);
        while ((double)s.pz >= 1.0)
            s.pz = (float)((double)s.pz + kWrapSub2);

        // Projection: head point (sx,sy) + velocity-scaled tail point (sx2,sy2) -
        // num = ((double)count*0.5 + depthBias) * 3.0   (0x429b5d: fild [ecx])
        double num = ((double)n * kHalf + depthBias) * kThree;       // v25
        double proj = num / ((double)s.pz * kTwo + kThree);          // v26 (0x429b87)
        s.sx = (float)((double)s.px * proj + (double)cx);            // 0x429b98
        s.sy = (float)((double)cy - (double)s.py * proj);            // 0x429bab
        float wX = vX * s.size;                                      // v66*[edx+0Ch]
        float wY = vY * s.size;                                      // v67*[edx+0Ch]
        float wZ = vZ * s.size;                                      // v68*[edx+0Ch]
        double proj2 = num / ((double)kThree + (double)kTwo * ((double)s.pz + (double)wZ)); // v29
        s.sx2 = (float)((double)cx + ((double)s.px + (double)wX) * proj2);     // 0x429c06
        s.sy2 = (float)((double)cy - proj2 * ((double)s.py + (double)wY));     // 0x429c18
    }
    (void)off2;
}

// gilde.exe 0x429c38 head — streak colour from the active drop count.
u32 RainStreakDiffuse(int count) {
    // fild count -> extended; * flt_611768; fsubr 1.0  =>  f = 1.0 - count*0.0005.
    // The whole chain stays on the x87 stack (modelled as double): `fld st` dups f
    // and `fmul flt_61176C`/`fmul flt_611770` multiply WITHOUT first rounding f to
    // float (0x429d02..0x429d1d). a = trunc(f*96), b = trunc(f*128) — ConvertX does
    // a frndint with RC=truncate-toward-zero before each fistp.
    double f = 1.0 - (double)count * (double)kCountFade;
    int a = ConvertXTrunc(f * (double)kStreakR);
    int b = ConvertXTrunc(f * (double)kStreakG);
    // diffuse = 0x80000000 | (a<<16) | (b<<8) | b
    return 0x80000000u | ((u32)a << 16) | ((u32)b << 8) | (u32)b;
}

// gilde.exe 0x429c38 tail — software rasterise the projected streaks.
int RainRenderToSurface(const RainSystem& sys, const SnowViewport& vp,
                        u32 diffuse, Surface* surf) {
    const RainDrop* d = sys.drops;
    int n = sys.count;
    if (!surf || !d || n <= 0)
        return 0;

    // Unpack the engine's packed diffuse (ARGB 0x80RRGGBB) into 8-bit RGB. The
    // surface stores native pixels; SurfaceDrawLine wants RGB. Take R/G/B bytes
    // straight from the ARGB word (the engine builds it as 0xAARRGGBB).
    u8 r = (u8)((diffuse >> 16) & 0xFF);
    u8 g = (u8)((diffuse >> 8) & 0xFF);
    u8 b = (u8)(diffuse & 0xFF);

    float fx0 = (float)vp.x0, fx1 = (float)vp.x1;
    float fy0 = (float)vp.y0, fy1 = (float)vp.y1;

    int drawn = 0;
    for (int i = 0; i < n; ++i) {
        const RainDrop& s = d[i];
        // Same all-four-corners-inside viewport clip the original applies
        // (0x429d65..0x429e2b): head/tail X in [x0,x1); head/tail Y in [y0,y1).
        if (!((double)s.sx >= fx0 && (double)s.sx < fx1 &&
              (double)s.sx2 >= fx0 && (double)s.sx2 < fx1))
            continue;
        if (!((double)s.sy >= fy0 && (double)s.sy < fy1 &&
              (double)s.sy2 >= fy0 && (double)s.sy2 < fy1))
            continue;
        // The original draws a LINELIST segment head(sx,sy)->tail(sx2,sy2).
        SurfaceDrawLine(surf, (int)s.sx, (int)s.sy, (int)s.sx2, (int)s.sy2,
                        r, g, b);
        ++drawn;
    }
    return drawn;
}

} // namespace guild::render

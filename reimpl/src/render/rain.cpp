#include "render/rain.h"
#include "render/particle.h"
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

void RainUpdateDrop(RainSystem& sys, float dt, const SnowCamera& cam,
                    const SnowViewport& vp) {
    RainDrop* drops = sys.drops;
    int n = sys.count;
    if (!drops || n <= 0)
        return;

    float euler[3] = {cam.anchor[0] - cam.eye[0], cam.anchor[1] - cam.eye[1],
                      cam.anchor[2] - cam.eye[2]};
    float em[16];
    util::MatrixFromEuler(euler, em);
    float off0 = kAnchorZ * em[8];
    float off1 = kAnchorZ * em[9];
    float off2 = kAnchorZ * em[10];

    float d[3] = {cam.anchor[0] - cam.eye[0], cam.anchor[1] - cam.eye[1],
                  cam.anchor[2] - cam.eye[2]};
    float drift0 = (d[0] * cam.m[1] + d[1] * cam.m[4] + d[2] * cam.m[7]) * kDtScale;
    float drift1 = (d[0] * cam.m[2] + d[1] * cam.m[5] + d[2] * cam.m[8]) * kDtScale;
    float drift2 = (d[0] * cam.m[0] + d[1] * cam.m[3] + d[2] * cam.m[6]) * kDtScale;

    float velX0 = cam.m[0];
    float velX1 = cam.m[1];
    float velX2 = cam.m[2];

    float g0 = -kGravY * cam.m[1];
    float g1 = -kGravY * cam.m[4];
    float g2 = -kGravY * cam.m[7];

    int halfW = (vp.x1 - vp.x0) >> 1;
    int halfH = (vp.y1 - vp.y0) >> 1;
    float fHalfW = (float)halfW;
    float fHalfH = (float)halfH;
    float maxHalf = fHalfW > fHalfH ? fHalfW : fHalfH;
    float cx = (float)vp.x0 + fHalfW;
    float cy = (float)vp.y0 + fHalfH;
    float depthBias = maxHalf * kThird; // v63 = maxHalf * flt_611680

    for (int i = 0; i < n; ++i) {
        RainDrop& s = drops[i];

        float vX = s.d1 * g0 + s.d0 * velX0;
        double px = (double)dt * vX + drift0 + off0 + s.px;
        s.px = (float)px;
        if (px < kClampLo || px > kClampHi)
            s.px = 0.0f;
        while ((double)s.px < kWrapLo)
            s.px = (float)((double)s.px + kWrapAdd2);
        while ((double)s.px >= 1.0)
            s.px = (float)((double)s.px + kWrapSub2);

        float vY = s.d1 * g1 + s.d0 * velX1;
        double py = (double)dt * vY + drift1 + off1 + s.py;
        s.py = (float)py;
        if (py < kClampLo || py > kClampHi)
            s.py = 0.0f;
        while ((double)s.py < kWrapLo)
            s.py = (float)((double)s.py + kWrapAdd2);
        while ((double)s.py >= 1.0)
            s.py = (float)((double)s.py + kWrapSub2);

        float vZ = s.d1 * g2 + s.d0 * velX2;
        double pz = (double)dt * vZ + (double)off2 * kHalf + s.pz;
        s.pz = (float)pz;
        if (pz < kClampLo || pz > kClampHi)
            s.pz = 0.0f;
        while ((double)s.pz < kWrapLo)
            s.pz = (float)((double)s.pz + kWrapAdd2);
        while ((double)s.pz >= 1.0)
            s.pz = (float)((double)s.pz + kWrapSub2);

        // Projection: head point and a velocity-scaled tail point (streak).
        double num = ((double)n * kDtScale + depthBias) * kThree; // v25
        double proj = num / ((double)s.pz * kTwo + kThree);        // v26
        s.sx = (float)((double)s.px * proj + cx);
        s.sy = (float)(cy - (double)s.py * proj);
        float wX = vX * s.size;
        float wY = vY * s.size;
        float wZ = vZ * s.size;
        double tailDen = (double)kThree + (double)kTwo * ((double)s.pz + (double)wZ);
        double proj2 = num / tailDen; // v29
        s.sx2 = (float)((double)cx + ((double)s.px + wX) * proj2);
        s.sy2 = (float)((double)cy - proj2 * ((double)s.py + wY));
        (void)drift2;
    }
}

} // namespace guild::render

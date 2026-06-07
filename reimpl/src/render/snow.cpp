#include "render/snow.h"
#include "render/particle.h" // TruncToward
#include "util/matrix.h"     // MatrixFromEuler
#include "crt/rand.h"
#include <cmath>

namespace guild::render {

namespace {
// Recovered float/double constants (get_bytes; all bit-exact). See snow.h.
constexpr double kSnowRandNorm = 3.0518509447574615e-05; // flt_6117AC (1/32767)
constexpr float  kSeedMul  = 2.0f;     // flt_6117B0
constexpr float  kSeedScaleA = 0.5f;   // flt_6117B4
constexpr float  kSeedScaleB = 0.009999999776482582f; // flt_6117B8
constexpr float  kSeedBias = -0.5f;    // flt_6117BC
constexpr float  kSeedVelC0 = 0.75f;   // flt_6117C0
constexpr float  kSeedVelC4 = 0.02500000037252903f;  // flt_6117C4

constexpr float  kDtScale  = 0.0024999999441206455f; // flt_6117F0
constexpr float  kF4       = 2.0f;     // flt_6117F4 (z velocity scale)
constexpr float  kF8       = 13.5f;    // flt_6117F8 (tail length scale)
constexpr float  kFc       = 3.0f;     // flt_6117FC (projection denom bias)
constexpr double kWrapLo   = -1.0;     // dbl_611804 (wrap lower bound)
constexpr double kWrapAdd2 = 2.0;      // dbl_61180C
constexpr double kWrapSub2 = -2.0;     // dbl_611814
constexpr float  kWrapSubF = -2.0f;    // flt_61181C  (z upper-wrap, float)
constexpr float  kGravY    = 1.9f;     // gravity/anchor billboard magnitude

// 1.0f bit pattern for the z upper-wrap comparison (`>= 1065353216` on the
// raw float bits == `>= 1.0f` for positive values, the original's idiom).
inline bool geOneBits(float f) {
    union { float f; std::int32_t i; } u{f};
    return u.i >= 1065353216;
}
} // namespace

void SnowSeedFlakes(SnowSystem& sys) {
    int n = sys.count;
    SnowFlake* f = sys.flakes;
    if (!f || n <= 0)
        return;
    // Original loops `while (i + 1 < count)` writing index i then ++i, so it
    // fills exactly `count` records (the post-increment + pre-condition cover
    // index 0..count-1). Six RandNext() draws per flake, in this exact order.
    for (int i = 0; i < n; ++i) {
        SnowFlake& s = f[i];
        s.px = (float)((double)crt::RandNext() * kSnowRandNorm + kSeedBias) * kSeedMul;
        s.py = (float)((double)crt::RandNext() * kSnowRandNorm + kSeedBias) * kSeedMul;
        s.pz = (float)((double)crt::RandNext() * kSnowRandNorm + kSeedBias) * kSeedMul;
        s.size = (float)((double)crt::RandNext() * kSnowRandNorm * kSeedScaleA + kSeedVelC0);
        s.d0 = (float)((double)crt::RandNext() * kSnowRandNorm * kSeedScaleB + kSeedVelC4);
        s.d1 = (float)((double)crt::RandNext() * kSnowRandNorm * kSeedScaleB + kSeedVelC4);
    }
}

void SnowUpdateFlake(SnowSystem& sys, float dt, const SnowCamera& cam,
                     const SnowViewport& vp) {
    SnowFlake* flakes = sys.flakes;
    int n = sys.count;
    if (!flakes || n <= 0)
        return;

    // (1) anchor billboard offset: (0,0,1.9) rotated by the Euler matrix built
    // from (anchor - eye). The kernel reads only m[8],m[9],m[10] (== 1.9 * ...).
    float euler[3] = {cam.anchor[0] - cam.eye[0], cam.anchor[1] - cam.eye[1],
                      cam.anchor[2] - cam.eye[2]};
    float em[16];
    util::MatrixFromEuler(euler, em);
    float off0 = kGravY * em[8];
    float off1 = kGravY * em[9];
    float off2 = kGravY * em[10];

    // (2) drift base: (anchor - eye) through the view rotation, * dtScale.
    float d[3] = {cam.anchor[0] - cam.eye[0], cam.anchor[1] - cam.eye[1],
                  cam.anchor[2] - cam.eye[2]};
    float drift0 = (d[0] * cam.m[1] + d[1] * cam.m[4] + d[2] * cam.m[7]) * kDtScale; // +400/+416/+432
    float drift1 = (d[0] * cam.m[2] + d[1] * cam.m[5] + d[2] * cam.m[8]) * kDtScale; // +404/+420/+436
    float drift2 = (d[0] * cam.m[0] + d[1] * cam.m[3] + d[2] * cam.m[6]) * kDtScale; // +396/+412/+428

    // (3) per-system velocity basis: (sysVelX, 0, sysVelZ) through view rotation.
    // The original reads these from the system header (a1+68, a1+72); here we
    // fold them into the flake d0/d1 weights, so the basis is just the view
    // rotation columns used by each flake below. Recompute the rotated unit
    // basis as the original does for (1,0,?) and (0,0,1)-style mixes.
    float velX0 = cam.m[0]; // contribution of d0 to x-screen-axis
    float velX1 = cam.m[1];
    float velX2 = cam.m[2];

    // (4) gravity direction (0,-1,0) through the view rotation.
    float g0 = -cam.m[1]; // 0*m0 + -1*m[1] + 0*m[2]  (rows +400/+416/+432)
    float g1 = -cam.m[4];
    float g2 = -cam.m[7];

    int halfW = (vp.x1 - vp.x0) >> 1;
    int halfH = (vp.y1 - vp.y0) >> 1;
    float fHalfW = (float)halfW;
    float fHalfH = (float)halfH;
    float maxHalf = fHalfW > fHalfH ? fHalfW : fHalfH;
    float cx = (float)vp.x0 + fHalfW;
    float cy = (float)vp.y0 + fHalfH;
    float depthScale = maxHalf * kFc; // v49 = maxHalf * flt_6117FC

    for (int i = 0; i < n; ++i) {
        SnowFlake& s = flakes[i];
        // X axis
        float vX = s.d1 * g0 + s.d0 * velX0;
        double px = (double)dt * vX + drift0 + off0 + s.px;
        for (;;) {
            s.px = (float)px;
            px = s.px;
            if (px >= kWrapLo) break;
            px = px + kWrapAdd2;
        }
        while ((double)s.px >= 1.0)
            s.px = (float)((double)s.px + kWrapSub2);

        // Y axis
        float vY = s.d1 * g1 + s.d0 * velX1;
        double py = (double)dt * vY + drift1 + off1 + s.py;
        for (;;) {
            s.py = (float)py;
            py = s.py;
            if (py >= kWrapLo) break;
            py = py + kWrapAdd2;
        }
        while ((double)s.py >= 1.0)
            s.py = (float)((double)s.py + kWrapSub2);

        // Z axis (own scale + float-bit upper wrap)
        float vZ = s.d1 * g2 + s.d0 * velX2;
        double pz = (double)dt * vZ + (double)off2 * kF4 + s.pz;
        for (;;) {
            s.pz = (float)pz;
            if ((double)s.pz >= kWrapLo) break;
            pz = (double)s.pz + (double)kF4; // flt_6117F4 z-wrap increment
        }
        while (geOneBits(s.pz))
            s.pz = s.pz + kWrapSubF;

        // Projection: parallax by depth, billboard to screen.
        double proj = (double)depthScale / ((double)s.pz * kF4 + kFc);
        s.sx = (float)((double)s.px * proj + cx);
        s.sy = (float)(cy - proj * (double)s.py);
        double tail = ((1.0 - (double)s.pz) * kF8 + 1.0) * (double)s.size;
        s.sx2 = (float)((double)s.sx + tail);
        s.sy2 = (float)(tail + (double)s.sy);
    }
    (void)drift2;
}

int SnowMipLevel(unsigned srcDim, unsigned scale) {
    unsigned v = scale ? srcDim / scale : srcDim;
    int i = 0;
    for (; v > 1; ++i)
        v >>= 1;
    return i;
}

void SnowAccumulateMaskedCopy8(u8* dst, const u8* src, const u8* mask,
                               int dim, int threshold) {
    for (int i = 0; i < dim * dim; ++i)
        if (mask[i] < threshold)
            dst[i] = src[i];
}
void SnowAccumulateMaskedCopy16(u16* dst, const u16* src, const u8* mask,
                                int dim, int threshold) {
    for (int i = 0; i < dim * dim; ++i)
        if (mask[i] < threshold)
            dst[i] = src[i];
}
void SnowAccumulateMaskedCopy32(u32* dst, const u32* src, const u8* mask,
                                int dim, int threshold) {
    for (int i = 0; i < dim * dim; ++i)
        if (mask[i] < threshold)
            dst[i] = src[i];
}

} // namespace guild::render

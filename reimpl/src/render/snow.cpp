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

// gilde.exe 0x42a644 — VIBE_Snow_UpdateFlake (__userpurge, a1@eax = system,
// a2 = frame dt). A 1:1 translation of the disasm (the Hex-Rays pseudocode is
// the cross-check; on conflicts the disasm at 0x42a644..0x42ab60 wins).
//
// The function is STATEFUL: it reads frame-to-frame deltas of the camera anchor
// (sys.prevAnchor minus cam.anchor) and eye (sys.prevEye minus cam.eye), then
// overwrites the snapshots with the current camera. `cam.m` is the 4x3 view
// rotation; col[k] = (m[k], m[k+3], m[k+6]).
void SnowUpdateFlake(SnowSystem& sys, float dt, const SnowCamera& cam,
                     const SnowViewport& vp) {
    SnowFlake* flakes = sys.flakes;
    int n = sys.count;

    // (1) anchor billboard offset (v27/v28/v29): Euler matrix from
    //     (prevAnchor - camAnchor); off_k = 1.9 * em[8+k]. 0x42a662..0x42a6ef.
    float euler[3] = {sys.prevAnchor[0] - cam.anchor[0],
                      sys.prevAnchor[1] - cam.anchor[1],
                      sys.prevAnchor[2] - cam.anchor[2]};
    float em[16];
    util::MatrixFromEuler(euler, em);
    float off0 = kGravY * em[8];  // v27 = 1.9 * em[8]
    float off1 = kGravY * em[9];  // v28 = 1.9 * em[9]
    // v29 (1.9*em[10]) is computed but never used by the integrator.

    // store prevAnchor := camAnchor (0x42a6fd..0x42a70f).
    sys.prevAnchor[0] = cam.anchor[0];
    sys.prevAnchor[1] = cam.anchor[1];
    sys.prevAnchor[2] = cam.anchor[2];

    // (2) drift base (v36/v37/v38): de = prevEye - camEye, rotated by each
    //     matrix column, then * flt_6117F0 (0.0025). 0x42a722..0x42a7cb.
    //       v36 (driftX) = de . col0
    //       v37 (driftY) = de . col1
    //       v38 (driftZ) = de . col2
    float de0 = sys.prevEye[0] - cam.eye[0];
    float de1 = sys.prevEye[1] - cam.eye[1];
    float de2 = sys.prevEye[2] - cam.eye[2];
    float v4 = de0 * cam.m[1] + de1 * cam.m[4] + de2 * cam.m[7]; // de.col1 (+400/+416/+432)
    float v5 = de0 * cam.m[2] + de1 * cam.m[5] + de2 * cam.m[8]; // de.col2 (+404/+420/+436)
    float driftX = de0 * cam.m[0] + de1 * cam.m[3] + de2 * cam.m[6]; // v36 de.col0 (+396/+412/+428)
    float driftY = v4; // v37
    float driftZ = v5; // v38
    driftX = driftX * kDtScale;
    driftY = driftY * kDtScale;
    driftZ = kDtScale * driftZ;

    // store prevEye := camEye (0x42a7d8..0x42a7ea).
    sys.prevEye[0] = cam.eye[0];
    sys.prevEye[1] = cam.eye[1];
    sys.prevEye[2] = cam.eye[2];

    // (3) per-system velocity basis (v33/v34/v35): vel = (sysVelX, 0, sysVelZ)
    //     rotated by each matrix column. 0x42a850..0x42a85b.
    float vx = sys.sysVelX; // a1+68
    float vz = sys.sysVelZ; // a1+72
    float v8 = vx * cam.m[1] + 0.0f * cam.m[4] + vz * cam.m[7]; // v34 = vel.col1
    float v9 = vx * cam.m[2] + 0.0f * cam.m[5] + vz * cam.m[8]; // v35 = vel.col2
    float velX0 = vx * cam.m[0] + 0.0f * cam.m[3] + vz * cam.m[6]; // v33 = vel.col0
    float velX1 = v8;
    float velX2 = v9;

    // (4) gravity direction (0,-1,0) rotated: g_k = -m[3+k]. 0x42a8c1..0x42a8cc.
    float g0 = -cam.m[3]; // v24 = -m[3]
    float g1 = -cam.m[4]; // v25 = -m[4]
    float g2 = -cam.m[5]; // v26 = -m[5]

    // (5) viewport center + depth scale. v45 = (double)halfW, v46 = (float)halfH;
    //     v44 = max(v45, (double)v46). 0x42a8e2..0x42a97e.
    int halfW = (vp.x1 - vp.x0) >> 1;
    int halfH = (vp.y1 - vp.y0) >> 1;
    double v45 = (double)halfW;
    float fHalfH = (float)halfH; // v46
    float v44 = (v45 > (double)fHalfH) ? (float)v45 : fHalfH;
    double cx = (double)vp.x0 + v45;             // v48
    double cy = (double)vp.y0 + (double)fHalfH;  // v47
    float depthScale = v44 * kFc;                // v49 = v44 * flt_6117FC

    if (n <= 0 || !flakes)
        return; // original loops only when *(int*)a1 > 0 (v14 deref guarded by it)

    for (int i = 0; i < n; ++i) {
        SnowFlake& s = flakes[i];
        // X axis: v50 = d1*g0 + d0*velX0;  drift v36(driftX) + off v27(off0).
        float v50 = s.d1 * g0 + s.d0 * velX0;
        double px = (double)dt * v50 + driftX + off0 + s.px;
        for (;;) {
            s.px = (float)px;
            px = s.px;
            if (px >= kWrapLo) break;
            px = px + kWrapAdd2;
        }
        while ((double)s.px >= 1.0)
            s.px = (float)((double)s.px + kWrapSub2);

        // Y axis: v51 = d1*g1 + d0*velX1;  drift v37(driftY) + off v28(off1).
        float v51 = s.d1 * g1 + s.d0 * velX1;
        double py = (double)dt * v51 + driftY + off1 + s.py;
        for (;;) {
            s.py = (float)py;
            py = s.py;
            if (py >= kWrapLo) break;
            py = py + kWrapAdd2;
        }
        while ((double)s.py >= 1.0)
            s.py = (float)((double)s.py + kWrapSub2);

        // Z axis: v52 = d1*g2 + d0*velX2;  drift v38(driftZ)*flt_6117F4, NO off.
        // Lower wrap adds flt_6117F4 (2.0); upper wrap uses the raw-bits >= 1.0f.
        float v52 = s.d1 * g2 + s.d0 * velX2;
        double pz = (double)dt * v52 + (double)driftZ * kF4 + s.pz;
        for (;;) {
            s.pz = (float)pz;
            if ((double)s.pz >= kWrapLo) break;
            pz = (double)s.pz + (double)kF4; // flt_6117F4 z-wrap increment
        }
        while (geOneBits(s.pz))
            s.pz = s.pz + kWrapSubF;

        // Projection: v20 = v49 / (pz*flt_6117F4 + flt_6117FC).
        double v20 = (double)depthScale / ((double)s.pz * kF4 + kFc);
        s.sx = (float)((double)s.px * v20 + cx);       // v14[6] = px*v20 + v48
        s.sy = (float)(cy - v20 * (double)s.py);       // v14[7] = v47 - v20*py
        // v21 = ((1.0 - pz)*flt_6117F8 + 1.0) * size.
        double v21 = ((1.0 - (double)s.pz) * kF8 + 1.0) * (double)s.size;
        s.sx2 = (float)((double)s.sx + v21);           // v14[8] = sx + v21
        s.sy2 = (float)(v21 + (double)s.sy);           // v14[9] = v21 + sy
    }
}

// ---------------------------------------------------------------------------
// VIBE_Snow_Render @0x42b5b0 — render-side constants (get_bytes @0x611990).
//   flt_611990 = 0.1                (0x3DCCCCCD)  dt = (now-last) * 0.1
//   flt_611994 = 0.025             (0x3CCCCCCD)  z->v coord scale
//   flt_611998 = 0.5               (0x3F000000)  midpoint blend
// The vertex bit-pattern constants emitted verbatim by the original:
//   1065353216 = 1.0f, 1056964608 = 0.5f, 0 = 0.0f, 1348756580 = snow D3DCOLOR.
namespace {
constexpr float kRenderDt   = 0.10000000149011612f; // flt_611990
constexpr float kZtoV       = 0.02500000037252903f; // flt_611994
constexpr float kMidBlend   = 0.5f;                 // flt_611998
constexpr u32   kSnowColor  = 0x50646464u;          // diffuse dword (1348756580)
constexpr u32   kOneBits    = 0x3F800000u;          // 1.0f
constexpr u32   kHalfBits   = 0x3F000000u;          // 0.5f
inline float bitsToF(u32 b) { union { u32 i; float f; } u{b}; return u.f; }
} // namespace

// gilde.exe 0x42b5c9..0x42b648 — the header interpolation block.
float SnowRenderStepHeader(SnowSystemHdr& h, i32 now) {
    // (A) count ramp: blocks 0x42b5c9 / 0x42b86a. If a count window is active
    // (cBeg != cEnd) interpolate [0] across [cFrom..cTo] by the engine clock,
    // clamping to cTo once `now` passes cEnd. Signed integer arithmetic, exactly
    // as the original (it stores the result back into [0]).
    if (h.cBeg != h.cEnd) {
        if ((u32)h.cEnd > (u32)now) {
            // v4 = cTo*(now-cBeg)/(cEnd-cBeg) + (cEnd-now)*cFrom/(cEnd-cBeg)
            i32 span = (i32)((u32)h.cEnd - (u32)h.cBeg);
            i32 v4 = h.cTo * (now - h.cBeg) / span
                   + (i32)(((u32)h.cEnd - (u32)now) * (u32)h.cFrom) / span;
            h.count = v4;
        } else {
            h.count = h.cTo;
        }
    }
    // (B) direction ramp: blocks 0x42b5e5 / 0x42b88f. If a direction window is
    // active (dBeg != dEnd) interpolate (dirX,dirZ) from (oldX,oldZ) toward
    // (tgtX,tgtZ); once `now` passes dEnd, snap to the target.
    if (h.dBeg != h.dEnd) {
        if ((u32)h.dEnd > (u32)now) {
            i32 v21 = (i32)((u32)h.dEnd - (u32)h.dBeg); // total span
            i32 v22 = h.dEnd - now;                     // remaining
            i32 elapsed = v21 - v22;                    // now - dBeg
            double inv = 1.0 / (double)v21;
            double rem = (double)v22;
            double ela = (double)elapsed;
            h.dirX = (float)(h.tgtX * ela * inv + h.oldX * rem * inv);
            h.dirZ = (float)(ela * h.tgtZ * inv + inv * (rem * h.oldZ));
        } else {
            h.dirX = h.tgtX;
            h.dirZ = h.tgtZ;
        }
    }
    // (C) per-frame dt + advance lastUpdate (0x42b609..0x42b63e).
    u32 delta = (u32)now - (u32)h.lastUpdateMs;
    float dt = (float)((double)delta * (double)kRenderDt);
    h.lastUpdateMs = now;
    return dt;
}

// gilde.exe 0x42b689..0x42b7c8 — build the per-flake quad vertex stream.
int SnowBuildQuads(const SnowSystem& sys, const SnowViewport& vp,
                   SnowVertex* out, int cap) {
    const SnowFlake* f = sys.flakes;
    int n = sys.count;
    if (!f || n <= 0 || !out || cap <= 0)
        return 0;
    // Viewport rect compared as doubles against the projected segment endpoints,
    // exactly as the original: x0<=sx, x1>sx2, y0<=sy, y1>sy2  (note: it tests
    // v8[6]/v8[8] for x and v8[7]/v8[9] for y — sx,sx2 / sy,sy2).
    double rx0 = (double)vp.x0, rx1 = (double)vp.x1;
    double ry0 = (double)vp.y0, ry1 = (double)vp.y1;
    int w = 0;
    for (int i = 0; i < n; ++i) {
        const SnowFlake& s = f[i];
        if (!((double)rx0 <= (double)s.sx && (double)rx1 > (double)s.sx2 &&
              (double)ry0 <= (double)s.sy && (double)ry1 > (double)s.sy2))
            continue;
        if (w + 3 > cap)
            break;
        float vcoord = (1.0f - s.pz) * kZtoV; // var_C = (1 - pz) * flt_611994
        // Exact per-vertex bit patterns from 0x42b708..0x42b7be. rhw=1.0, diffuse=
        // 0x50646464, specular=0; only x,y / tu,tv differ between the 3 vertices.
        // vertex 0 (0x42b708): x=(sx+sx2)*0.5, y=sy, tu=0.5, tv=0
        SnowVertex& a = out[w + 0];
        a.x = (s.sx + s.sx2) * kMidBlend; a.y = s.sy; a.z = vcoord;
        a.rhw = kOneBits; a.color = kSnowColor; a.specular = 0;
        a.u = bitsToF(kHalfBits); a.v = 0.0f;
        // vertex 1 (0x42b747): x=sx2, y=sy2, tu=1.0, tv=1.0
        SnowVertex& b = out[w + 1];
        b.x = s.sx2; b.y = s.sy2; b.z = vcoord;
        b.rhw = kOneBits; b.color = kSnowColor; b.specular = 0;
        b.u = bitsToF(kOneBits); b.v = bitsToF(kOneBits);
        // vertex 2 (0x42b786): x=sx, y=sy2, tu=0.0, tv=1.0
        SnowVertex& c = out[w + 2];
        c.x = s.sx; c.y = s.sy2; c.z = vcoord;
        c.rhw = kOneBits; c.color = kSnowColor; c.specular = 0;
        c.u = 0.0f; c.v = bitsToF(kOneBits);
        w += 3;
    }
    return w;
}

// gilde.exe 0x42a2cc tail — the per-texture transparency decision in UpdateScene.
//   v6 = !dword_140809C && *(u8*)(v4+125) > 8u;
bool SnowResetSceneTexTransparency(int winterFlag, int texLevel) {
    return (winterFlag == 0) && ((unsigned)texLevel > 8u);
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

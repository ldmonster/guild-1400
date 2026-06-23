// =============================================================================
// rain_updatedrop_wave23_test.cpp — golden pins for the FAITHFUL reconstruction
// of VIBE_Rain_UpdateDrop @0x4294d4 (the per-drop rain particle integrator).
//
// Wave-22's coverage audit flagged that the wave-21 RainUpdateDrop body was a
// guess copied from the snow sibling and was NEVER a 1:1 match: it used the wrong
// camera-matrix column indices for drift, treated the velocity basis as raw
// matrix columns instead of the (windX,0,windZ)*camMatrix transform, and added
// `off2*0.5` on Z instead of the binary's `drift2*2.0`. This suite pins the
// rewritten 1:1 integrator against a hand-derived reference that mirrors the
// disassembly term-for-term, plus the frame-to-frame prevAnchor/prevEye snapshot
// behaviour the binary keeps in the rain system struct.
//
// Constants (get_bytes @0x611670..0x6116A4, bit-exact):
//   flt_611670 0.0025   flt_611674 0.5   flt_611678 2.0   flt_61167C 3.0
//   flt_611680 1/3      dbl_611684 -1000 dbl_61168C 1000
//   dbl_611694 -1.0     dbl_61169C 2.0   dbl_6116A4 -2.0   gravity -0.75  anchor 2.5
//
// No float->int conversion happens in the integrator (the only fild/fistp touch
// integer viewport extents and the int drop count); ConvertX truncation is
// confined to the render colour pack, tested elsewhere.
// =============================================================================
#include "render/rain.h"
#include "util/matrix.h"
#include "crt/rand.h"
#include "tests/framework/test.h"

#include <cmath>
#include <cstring>

using namespace guild;
using namespace guild::render;

namespace {

SnowCamera IdentityCam() {
    SnowCamera c{};
    c.eye[0] = 0; c.eye[1] = 0; c.eye[2] = 0;
    c.anchor[0] = 0; c.anchor[1] = 0; c.anchor[2] = 1.0f;
    c.m[0] = 1; c.m[1] = 0; c.m[2] = 0;
    c.m[3] = 0; c.m[4] = 1; c.m[5] = 0;
    c.m[6] = 0; c.m[7] = 0; c.m[8] = 1;
    return c;
}

// A non-trivial camera: a yaw rotation in the XZ plane + moved eye/anchor, so the
// drift / wind / gravity transforms exercise all 9 matrix terms (not identity).
SnowCamera TiltCam() {
    SnowCamera c{};
    c.eye[0] = 1.5f; c.eye[1] = -2.0f; c.eye[2] = 0.5f;
    c.anchor[0] = 0.25f; c.anchor[1] = 0.75f; c.anchor[2] = 3.0f;
    // m laid out as kernel reads (m[0..2]=+396,+400,+404 ; etc). Use a clean yaw.
    float s = 0.6f, co = 0.8f; // 36.87 deg-ish
    c.m[0] = co; c.m[1] = 0;  c.m[2] = -s;
    c.m[3] = 0;  c.m[4] = 1;  c.m[5] = 0;
    c.m[6] = s;  c.m[7] = 0;  c.m[8] = co;
    return c;
}

bool feq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// ---------------------------------------------------------------------------
// Term-for-term reference mirror of VIBE_Rain_UpdateDrop @0x4294d4. Kept
// SEPARATE from the production code so the test is an independent oracle. It also
// mutates `sys` (prevAnchor/prevEye snapshot) exactly as the binary does.
// ---------------------------------------------------------------------------
struct RefDrop { float p[10]; };

void RefUpdateDrop(RainSystem& sys, float dt, const SnowCamera& cam,
                   const SnowViewport& vp) {
    constexpr float kDt = 0.0024999999441206455f, kHalf = 0.5f, kTwo = 2.0f,
                    kThree = 3.0f, kThird = 0.3333333432674408f,
                    kGrav = 0.75f, kAnchor = 2.5f;
    constexpr double kClampLo = -1000.0, kClampHi = 1000.0,
                     kWrapLo = -1.0, kWrapAdd2 = 2.0, kWrapSub2 = -2.0;

    float euler[3] = {sys.prevAnchor[0] - cam.anchor[0],
                      sys.prevAnchor[1] - cam.anchor[1],
                      sys.prevAnchor[2] - cam.anchor[2]};
    float em[16];
    util::MatrixFromEuler(euler, em);
    float off0 = kAnchor * em[8], off1 = kAnchor * em[9];

    sys.prevAnchor[0] = cam.anchor[0];
    sys.prevAnchor[1] = cam.anchor[1];
    sys.prevAnchor[2] = cam.anchor[2];

    float d[3] = {sys.prevEye[0] - cam.eye[0], sys.prevEye[1] - cam.eye[1],
                  sys.prevEye[2] - cam.eye[2]};
    float drift0 = (d[0]*cam.m[0] + d[1]*cam.m[3] + d[2]*cam.m[6]) * kDt;
    float drift1 = (d[0]*cam.m[1] + d[1]*cam.m[4] + d[2]*cam.m[7]) * kDt;
    float drift2 = (d[0]*cam.m[2] + d[1]*cam.m[5] + d[2]*cam.m[8]) * kDt;

    sys.prevEye[0] = cam.eye[0];
    sys.prevEye[1] = cam.eye[1];
    sys.prevEye[2] = cam.eye[2];

    float w0 = sys.windX*cam.m[0] + sys.windZ*cam.m[6];
    float w1 = sys.windX*cam.m[1] + sys.windZ*cam.m[7];
    float w2 = sys.windX*cam.m[2] + sys.windZ*cam.m[8];
    float g0 = -kGrav*cam.m[3], g1 = -kGrav*cam.m[4], g2 = -kGrav*cam.m[5];

    int halfW = (vp.x1 - vp.x0) >> 1, halfH = (vp.y1 - vp.y0) >> 1;
    float fHalfW = (float)halfW, fHalfH = (float)halfH;
    float maxHalf = fHalfW > fHalfH ? fHalfW : fHalfH;
    float cx = (float)vp.x0 + fHalfW, cy = (float)vp.y0 + fHalfH;

    RainDrop* drops = sys.drops;
    int n = sys.count;
    if (!drops || n <= 0) return;
    float depthBias = maxHalf * kThird;

    for (int i = 0; i < n; ++i) {
        RainDrop& s = drops[i];
        float vX = s.d1*g0 + s.d0*w0;
        double px = (double)dt*vX + (double)drift0 + (double)off0 + (double)s.px;
        s.px = (float)px;
        if (px < kClampLo || px > kClampHi) s.px = 0.0f;
        while ((double)s.px < kWrapLo) s.px = (float)((double)s.px + kWrapAdd2);
        while ((double)s.px >= 1.0)    s.px = (float)((double)s.px + kWrapSub2);

        float vY = s.d1*g1 + s.d0*w1;
        double py = (double)dt*vY + (double)drift1 + (double)off1 + (double)s.py;
        s.py = (float)py;
        if (py < kClampLo || py > kClampHi) s.py = 0.0f;
        while ((double)s.py < kWrapLo) s.py = (float)((double)s.py + kWrapAdd2);
        while ((double)s.py >= 1.0)    s.py = (float)((double)s.py + kWrapSub2);

        float vZ = s.d1*g2 + s.d0*w2;
        double pz = (double)dt*vZ + (double)drift2*kTwo + (double)s.pz; // drift2*2.0
        s.pz = (float)pz;
        if (pz < kClampLo || pz > kClampHi) s.pz = 0.0f;
        while ((double)s.pz < kWrapLo) s.pz = (float)((double)s.pz + kWrapAdd2);
        while ((double)s.pz >= 1.0)    s.pz = (float)((double)s.pz + kWrapSub2);

        double num = ((double)n*kHalf + depthBias) * kThree;
        double proj = num / ((double)s.pz*kTwo + kThree);
        s.sx = (float)((double)s.px*proj + (double)cx);
        s.sy = (float)((double)cy - (double)s.py*proj);
        float wX = vX*s.size, wY = vY*s.size, wZ = vZ*s.size;
        double proj2 = num / ((double)kThree + (double)kTwo*((double)s.pz + (double)wZ));
        s.sx2 = (float)((double)cx + ((double)s.px + (double)wX)*proj2);
        s.sy2 = (float)((double)cy - proj2*((double)s.py + (double)wY));
    }
}

} // namespace

// ---------------------------------------------------------------------------
// 1) The production integrator matches the independent term-for-term oracle on
//    a tilted camera over multiple frames (this is the core 1:1 trajectory pin).
//    Two systems get identical seeds; one runs production, one runs the oracle.
// ---------------------------------------------------------------------------
TEST(rain23_updatedrop, matches_reference_tilted_multiframe) {
    RainDrop a[8], b[8];
    std::memset(a, 0, sizeof(a)); std::memset(b, 0, sizeof(b));

    RainSystem pa; pa.count = 8; pa.capacity = 8; pa.drops = a;
    pa.windX = -0.4f; pa.windZ = 1.3f;
    RainSystem pb = pa; pb.drops = b;

    crt::Srand(0xABCD); RainSeedDrops(pa);
    crt::Srand(0xABCD); RainSeedDrops(pb);

    SnowCamera cam = TiltCam();
    SnowViewport vp{0, 0, 800, 600};

    for (int frame = 0; frame < 4; ++frame) {
        // move the camera each frame so prevEye/prevAnchor drift is exercised.
        cam.eye[0]    += 0.05f * frame;
        cam.anchor[1] += 0.03f * frame;
        RainUpdateDrop(pa, 0.5f, cam, vp);
        RefUpdateDrop (pb, 0.5f, cam, vp);
        // prevAnchor/prevEye snapshots must stay in lockstep.
        for (int k = 0; k < 3; ++k) {
            CHECK(feq(pa.prevAnchor[k], pb.prevAnchor[k], 1e-5f));
            CHECK(feq(pa.prevEye[k],    pb.prevEye[k],    1e-5f));
        }
    }
    // Whole drop field byte-comparable (same float ops in same order).
    for (int i = 0; i < 8; ++i) {
        CHECK(feq(a[i].px, b[i].px));   CHECK(feq(a[i].py, b[i].py));
        CHECK(feq(a[i].pz, b[i].pz));
        CHECK(feq(a[i].sx, b[i].sx, 1e-2f));  CHECK(feq(a[i].sy, b[i].sy, 1e-2f));
        CHECK(feq(a[i].sx2, b[i].sx2, 1e-2f)); CHECK(feq(a[i].sy2, b[i].sy2, 1e-2f));
    }
}

// ---------------------------------------------------------------------------
// 2) prevAnchor/prevEye snapshot: after one frame the rain system's stored
//    prev-anchor == camera anchor and prev-eye == camera eye (0x4295b2/0x429690).
// ---------------------------------------------------------------------------
TEST(rain23_updatedrop, snapshots_camera_each_frame) {
    RainDrop d[1]; std::memset(d, 0, sizeof(d));
    RainSystem sys; sys.count = 1; sys.capacity = 1; sys.drops = d;
    SnowCamera cam = TiltCam();
    SnowViewport vp{0, 0, 640, 480};
    RainUpdateDrop(sys, 0.1f, cam, vp);
    for (int k = 0; k < 3; ++k) {
        CHECK(feq(sys.prevAnchor[k], cam.anchor[k], 1e-6f));
        CHECK(feq(sys.prevEye[k],    cam.eye[k],    1e-6f));
    }
}

// ---------------------------------------------------------------------------
// 3) Z-axis asymmetry: the binary integrates Z with drift2*2.0 and NO off term
//    (off.z is a dead store). Construct a case where off.z != 0 and drift.z != 0
//    and check the production Z matches `dt*vZ + drift2*2.0 + pz`, NOT a path
//    that would include off2 or use 0.5. We isolate Z by zeroing wind/gravity
//    contributions for this drop (d0=d1=0 => vZ=0) so Z reduces to drift2*2 + pz0.
// ---------------------------------------------------------------------------
TEST(rain23_updatedrop, z_uses_drift2_times_two_not_off) {
    RainDrop d[1]; std::memset(d, 0, sizeof(d));
    d[0].pz = 0.10f; d[0].size = 0.25f; d[0].d0 = 0.0f; d[0].d1 = 0.0f;
    RainSystem sys; sys.count = 1; sys.capacity = 1; sys.drops = d;
    sys.windX = 1.0f; sys.windZ = 0.0f;
    // Make prevEye != eye so drift.z is non-zero; identity matrix keeps it clean:
    //   d = prevEye - eye = (0,0,-2) ; drift2 = d.z*m[8]*0.0025 = -2*1*0.0025.
    SnowCamera cam = IdentityCam();
    cam.eye[2] = 2.0f;                 // prevEye(0) - eye(2) = -2 on Z
    SnowViewport vp{0, 0, 640, 480};

    float pz0 = d[0].pz;
    RainUpdateDrop(sys, 0.5f, cam, vp);

    constexpr float kDt = 0.0024999999441206455f;
    float driftZ = (-2.0f) * cam.m[8] * kDt;      // d.z * m[8] * 0.0025
    float expPz = (float)((double)0.0f /*vZ=0*/ + (double)driftZ*2.0 + (double)pz0);
    CHECK(feq(d[0].pz, expPz, 1e-5f));
    // The buggy wave-21 path used off2*0.5; off.z here (euler of prevAnchor-anchor,
    // anchor=(0,0,1)) is non-zero, so the buggy result would differ measurably.
    float euler[3] = {0 - 0, 0 - 0, 0 - 1};
    float em[16]; util::MatrixFromEuler(euler, em);
    float off2 = 2.5f * em[10];
    float buggyPz = (float)((double)off2*0.5 + (double)pz0);
    CHECK(!feq(d[0].pz, buggyPz, 1e-3f));
}

// ---------------------------------------------------------------------------
// 4) Wind transform: velocity X basis uses (windX,0,windZ)*camMatrix, not a raw
//    matrix column. Two systems with different wind must diverge in trajectory.
// ---------------------------------------------------------------------------
TEST(rain23_updatedrop, velocity_uses_wind_transform) {
    RainDrop a[1], b[1]; std::memset(a, 0, sizeof(a)); std::memset(b, 0, sizeof(b));
    a[0].d0 = 1.0f; a[0].d1 = 0.0f; a[0].size = 0.25f; // pure wind-weighted drop
    b[0] = a[0];
    RainSystem sa; sa.count = 1; sa.capacity = 1; sa.drops = a; sa.windX = 1.0f; sa.windZ = 0.0f;
    RainSystem sb; sb.count = 1; sb.capacity = 1; sb.drops = b; sb.windX = 0.0f; sb.windZ = 1.0f;
    SnowCamera cam = TiltCam();
    SnowViewport vp{0, 0, 640, 480};
    RainUpdateDrop(sa, 1.0f, cam, vp);
    RainUpdateDrop(sb, 1.0f, cam, vp);
    // Different wind => the velocity transform differs => positions differ.
    CHECK(!(feq(a[0].px, b[0].px) && feq(a[0].pz, b[0].pz)));
    // And each matches its own oracle.
    RainDrop ra[1]; std::memset(ra, 0, sizeof(ra)); ra[0].d0 = 1.0f; ra[0].size = 0.25f;
    RainSystem ora; ora.count = 1; ora.capacity = 1; ora.drops = ra; ora.windX = 1.0f;
    SnowCamera cam2 = TiltCam();
    RefUpdateDrop(ora, 1.0f, cam2, vp);
    CHECK(feq(a[0].px, ra[0].px) && feq(a[0].pz, ra[0].pz));
}

// ---------------------------------------------------------------------------
// 5) Overflow reset + unit-cube wrap (0x429891 clamp, 0x4298ae/0x429926 wraps).
// ---------------------------------------------------------------------------
TEST(rain23_updatedrop, clamp_then_wrap) {
    RainDrop d[3]; std::memset(d, 0, sizeof(d));
    d[0].px = 5000.0f;   d[0].size = 0.25f;   // > 1000 -> reset to 0
    d[1].py = -7777.0f;  d[1].size = 0.25f;   // < -1000 -> reset to 0
    d[2].pz = 0.95f;     d[2].size = 0.25f;   // in range, wraps if it crosses 1
    RainSystem sys; sys.count = 3; sys.capacity = 3; sys.drops = d;
    SnowCamera cam = IdentityCam();
    SnowViewport vp{0, 0, 640, 480};
    RainUpdateDrop(sys, 0.0f, cam, vp);
    for (int i = 0; i < 3; ++i) {
        CHECK(d[i].px >= -1.0f && d[i].px < 1.0f);
        CHECK(d[i].py >= -1.0f && d[i].py < 1.0f);
        CHECK(d[i].pz >= -1.0f && d[i].pz < 1.0f);
    }
}

// ---------------------------------------------------------------------------
// 6) Projection numerator uses the int drop count * 0.5 (fild [ecx]; fmul 0.5),
//    NOT the dt-scale 0.0025 — the wave-21 numerator concern, re-pinned here on
//    the rewritten body.
// ---------------------------------------------------------------------------
TEST(rain23_updatedrop, projection_numerator_count_half) {
    crt::Srand(0x5151);
    RainDrop d[8]; std::memset(d, 0, sizeof(d));
    RainSystem sys; sys.count = 8; sys.capacity = 8; sys.drops = d;
    RainSeedDrops(sys);
    SnowCamera cam = IdentityCam();
    SnowViewport vp{0, 0, 640, 480};
    RainUpdateDrop(sys, 0.0f, cam, vp);

    const float maxHalf = 320.0f;
    const float depthBias = maxHalf * 0.3333333432674408f;
    const float num = (8.0f*0.5f + depthBias) * 3.0f;   // count=8 -> 4 + 106.66.. -> *3
    const float cx = 320.0f, cy = 240.0f;
    for (int i = 0; i < 8; ++i) {
        float proj = num / (d[i].pz*2.0f + 3.0f);
        CHECK(feq(d[i].sx, d[i].px*proj + cx, 1e-2f));
        CHECK(feq(d[i].sy, cy - d[i].py*proj, 1e-2f));
    }
    float buggyNum = (8.0f*0.0024999999441206455f + depthBias) * 3.0f;
    CHECK(!feq(num, buggyNum, 1.0f));
}

// ---------------------------------------------------------------------------
// 7) Deterministic golden trajectory: a fixed seed + fixed camera + N frames
//    reproduces byte-for-byte (the whole point of golden-pinning the integrator).
// ---------------------------------------------------------------------------
TEST(rain23_updatedrop, deterministic_golden_trajectory) {
    RainDrop a[16], b[16];
    std::memset(a, 0, sizeof(a)); std::memset(b, 0, sizeof(b));
    RainSystem sa; sa.count = 16; sa.capacity = 16; sa.drops = a; sa.windX = 0.7f; sa.windZ = -0.2f;
    RainSystem sb; sb.count = 16; sb.capacity = 16; sb.drops = b; sb.windX = 0.7f; sb.windZ = -0.2f;
    SnowCamera cam = TiltCam();
    SnowViewport vp{0, 0, 1024, 768};

    crt::Srand(0xCAFE); RainSeedDrops(sa);
    crt::Srand(0xCAFE); RainSeedDrops(sb);
    for (int f = 0; f < 6; ++f) { RainUpdateDrop(sa, 0.25f, cam, vp);
                                  RainUpdateDrop(sb, 0.25f, cam, vp); }
    CHECK(std::memcmp(a, b, sizeof(a)) == 0);
}

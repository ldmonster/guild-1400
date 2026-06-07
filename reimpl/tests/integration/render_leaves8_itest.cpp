#include "test.h"

// Integration: drive render_leaves8's VIBE_Light_AttachAtFrameMatrix
// (gilde.exe 0x5c8a78) against the REAL reconstructed sibling
// guild::util::MatrixToEuler (gilde.exe 0x5cb2cc, src/util/matrix.cpp) — exactly
// the live wiring (AttachAtFrameMatrix's tail is `VIBE_Math_MatrixToEuler(&m)`).
//
// We install a computeBoneWorldMatrix hook that lays down a known rotation matrix
// (the bone-world-matrix compose the engine would produce), forward the
// matrixToEuler slot to the REAL util::MatrixToEuler, and assert the angles the
// real decomposition extracts in place match a python-computed oracle. This
// exercises the cross-module flow: AttachAtFrameMatrix decides the reference
// frame from [obj+528]'s sign, composes the matrix, and the real Euler extractor
// rewrites m[0..2] — both halves agreeing end to end.
#include "render/render_leaves8.h"
#include "util/matrix.h"          // REAL reconstructed sibling: util::MatrixToEuler

#include <cmath>
#include <cstring>

using namespace guild;
using namespace guild::render;

namespace {

// Small local byte-poke helper (itest-local; no header dependency).
void PutByte(unsigned char* p, int off, signed char v) { p[off] = (unsigned char)v; }

// A bone-world matrix the engine's ComputeBoneWorldMatrix would write. Row-major
// 4x4 (16 floats). MatrixToEuler reads m[0],m[4],m[8],m[9],m[10] for the standard
// branch: e0=atan2(m9,m10), e1=atan2(-m8,hypot(m4,m0)), e2=atan2(m4,m0).
// We use a pure rotation about the matrix's "Z" of ~36.87deg: m0=0.8, m4=0.6.
float g_seed[16];
void  ComposeKnownMatrix(void* /*obj*/, void* ref, int flag, float* m) {
    // The hook also lets us observe the reference-frame decision and the flag.
    CHECK_EQ(flag, 1);
    (void)ref;
    std::memcpy(m, g_seed, sizeof(g_seed));
}

} // namespace

TEST(RenderLeaves8Itest, AttachAtFrameMatrixWiresRealMatrixToEuler) {
    // Identity except a Z-rotation block: m0=0.8, m4=0.6, m1=-0.6, m5=0.8.
    std::memset(g_seed, 0, sizeof(g_seed));
    g_seed[0]  = 0.8f;  g_seed[1] = -0.6f;
    g_seed[4]  = 0.6f;  g_seed[5] =  0.8f;
    g_seed[10] = 1.0f;  g_seed[15] = 1.0f;

    RenderLeaves8Hooks h = DefaultRenderLeaves8Hooks();
    h.computeBoneWorldMatrix = &ComposeKnownMatrix;
    // The live wiring: forward the matrix->euler tail into the REAL sibling.
    h.matrixToEuler = &guild::util::MatrixToEuler;
    InstallRenderLeaves8Hooks(h);

    unsigned char obj[600];
    std::memset(obj, 0, sizeof(obj));
    PutByte(obj, 528, 0);   // >= 0 -> relative reference (still composes the matrix)

    float m[16];
    for (int i = 0; i < 16; ++i) m[i] = 999.0f;   // poison
    AttachAtFrameMatrix(obj, m);

    // Oracle (python): v14 = hypot(m4=0.6, m0=0.8) = 1.0 (> gimbal eps).
    //   e0 = atan2(m9=0, m10=1)        = 0
    //   e1 = atan2(-m8=0, 1.0)         = 0
    //   e2 = atan2(m4=0.6, m0=0.8)     = 0.6435011087932844
    CHECK(std::fabs(m[0] - 0.0f) < 1e-5f);
    CHECK(std::fabs(m[1] - 0.0f) < 1e-5f);
    CHECK(std::fabs(m[2] - 0.6435011087932844f) < 1e-5f);

    InstallRenderLeaves8Hooks(DefaultRenderLeaves8Hooks());
}

// A second cross-module case: a gimbal-lock matrix (hypot(m4,m0) tiny) drives the
// real sibling's lock branch, proving the wiring forwards that path too.
TEST(RenderLeaves8Itest, AttachAtFrameMatrixRealEulerGimbalBranch) {
    std::memset(g_seed, 0, sizeof(g_seed));
    // m0=m4=0 -> v14 ~ 0 -> lock branch: e0=atan2(-m6,m5), e1=atan2(-m8,v14), e2=0.
    g_seed[5]  = 1.0f;   // m5
    g_seed[6]  = -1.0f;  // m6  -> e0 = atan2(1, 1)
    g_seed[8]  = 0.0f;   // m8  -> e1 = atan2(0, 0) = 0
    g_seed[15] = 1.0f;

    RenderLeaves8Hooks h = DefaultRenderLeaves8Hooks();
    h.computeBoneWorldMatrix = &ComposeKnownMatrix;
    h.matrixToEuler = &guild::util::MatrixToEuler;
    InstallRenderLeaves8Hooks(h);

    unsigned char obj[600];
    std::memset(obj, 0, sizeof(obj));
    PutByte(obj, 528, -1);   // < 0 -> absolute reference (null)

    float m[16];
    std::memcpy(m, g_seed, sizeof(m));
    AttachAtFrameMatrix(obj, m);

    // Oracle: e0 = atan2(-m6=1, m5=1) = pi/4; e1 = atan2(-m8=0, ~0) = 0; e2 = 0.
    CHECK(std::fabs(m[0] - 0.7853981633974483f) < 1e-5f);
    CHECK(std::fabs(m[1] - 0.0f) < 1e-5f);
    CHECK(std::fabs(m[2] - 0.0f) < 1e-5f);

    InstallRenderLeaves8Hooks(DefaultRenderLeaves8Hooks());
}

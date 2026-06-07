// Unit tests for guild::util matrix/quaternion/transform math (gilde.exe Math
// matrix block + Transform prefix). Suites/names are prefixed UtilMatrix* to stay
// unique across the test binary.
#include "test.h"
#include "util/matrix.h"

#include <cmath>
#include <cstring>

using namespace guild::util;

namespace {

bool nearf(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

// out = M * [v,1] using the 4x4 with rotation block (0..2/4..6/8..10) and
// translation column (12,13,14), matching MatrixTransformVectors' convention
// (row-of-b times M). We use a simple "rotate dir by 3x3" for direction vectors.
void Rotate3x3(const float* m, const float* v, float* out) {
    out[0] = v[0] * m[0] + v[1] * m[4] + v[2] * m[8];
    out[1] = v[0] * m[1] + v[1] * m[5] + v[2] * m[9];
    out[2] = v[0] * m[2] + v[1] * m[6] + v[2] * m[10];
}

} // namespace

// --- Identity ---------------------------------------------------------------
TEST(UtilMatrix, Identity) {
    float m[16];
    std::memset(m, 0x7f, sizeof(m));
    MatrixIdentity(m);
    for (int i = 0; i < 16; ++i) {
        float expect = (i == 0 || i == 5 || i == 10 || i == 15) ? 1.0f : 0.0f;
        CHECK(nearf(m[i], expect));
    }
    // identity * v == v (via Rotate3x3)
    float v[3] = {1.5f, -2.25f, 3.75f};
    float r[3];
    Rotate3x3(m, v, r);
    CHECK(nearf(r[0], v[0]));
    CHECK(nearf(r[1], v[1]));
    CHECK(nearf(r[2], v[2]));
}

// --- FromEuler golden vector ------------------------------------------------
TEST(UtilMatrix, FromEulerGolden) {
    float a[3] = {0.3f, -0.4f, 0.7f};
    float m[16];
    MatrixFromEuler(a, m);
    // Golden values computed with python3 (see report).
    const float g[16] = {
        0.704466f, -0.703463f, -0.094161f, 0.0f,
        0.593364f,  0.656544f, -0.465692f, 0.0f,
        0.389418f,  0.272192f,  0.879923f, 0.0f,
        0.0f,       0.0f,       0.0f,      1.0f};
    for (int i = 0; i < 16; ++i)
        CHECK(nearf(m[i], g[i], 1e-4f));
}

// --- FromEuler -> ToEuler roundtrip on sampled angles -----------------------
TEST(UtilMatrix, FromEulerToEulerRoundtrip) {
    // ToEuler writes back into the matrix's first 3 floats; sample angles avoiding
    // gimbal lock (pitch near +-pi/2). The reconstructed convention roundtrips
    // angle[1] (pitch) and a consistent (angle0,angle2); we verify the recovered
    // matrix reproduces the same rotation, which is the invariant that matters.
    const float samples[][3] = {
        {0.0f, 0.0f, 0.0f},
        {0.3f, -0.4f, 0.7f},
        {-0.9f, 0.5f, 1.2f},
        {0.1f, 1.0f, -0.6f},
        {-1.3f, -0.2f, 0.4f},
    };
    for (auto& s : samples) {
        float m[16];
        MatrixFromEuler(s, m);
        float euler[16];
        std::memcpy(euler, m, sizeof(m));
        MatrixToEuler(euler);  // euler[0..2] are recovered angles
        float m2[16];
        MatrixFromEuler(euler, m2);
        // The rebuilt rotation block must match the original.
        for (int i = 0; i < 11; ++i)
            CHECK(nearf(m[i], m2[i], 2e-3f));
    }
}

// --- inverse(M) * M ~= I ----------------------------------------------------
TEST(UtilMatrix, InverseRoundtrip) {
    float a[3] = {0.4f, 0.6f, -0.3f};
    float m[16];
    MatrixFromEuler(a, m);
    // Add a translation column to make it a full affine transform.
    m[12] = 5.0f; m[13] = -3.0f; m[14] = 2.0f;
    float inv[16];
    MatrixInverse(m, inv);
    // Compose inv * m via MatrixTransformVectors (out = m (*) inv per convention),
    // then check the result is identity within epsilon.
    float prod[16];
    MatrixTransformVectors(m, inv, prod);
    for (int i = 0; i < 16; ++i) {
        float expect = (i == 0 || i == 5 || i == 10 || i == 15) ? 1.0f : 0.0f;
        CHECK(nearf(prod[i], expect, 2e-3f));
    }
}

TEST(UtilMatrix, InverseSingularFallsBackToTranspose) {
    // A singular matrix (zero rotation block) triggers the MatrixCopy/transpose path.
    float m[16];
    std::memset(m, 0, sizeof(m));
    m[1] = 2.0f;  // off-diagonal set, but det == 0
    float inv[16];
    MatrixInverse(m, inv);
    // transpose: inv[4] should equal m[1].
    CHECK(nearf(inv[4], 2.0f));
    CHECK(nearf(inv[1], 0.0f));
}

// --- MatrixCopy is a transpose ----------------------------------------------
TEST(UtilMatrix, CopyIsTranspose) {
    float src[16], dst[16];
    for (int i = 0; i < 16; ++i) src[i] = (float)(i + 1);
    MatrixCopy(src, dst);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            CHECK(nearf(dst[4 * i + j], src[i + 4 * j]));
}

// --- quat-rotate equals matrix-rotate for the same orientation --------------
TEST(UtilMatrix, QuatRotateEqualsMatrixRotate) {
    // Build a quaternion for a rotation of `angle` about a unit axis, then compare
    // QuatRotateVector against the equivalent rotation matrix applied to a vector.
    float axis[3] = {0.0f, 0.0f, 1.0f};  // Z axis -> matches FromEuler's roll
    float angle = 0.7f;
    float s = std::sin(angle * 0.5f), c = std::cos(angle * 0.5f);
    float q[4] = {axis[0] * s, axis[1] * s, axis[2] * s, c};  // (x,y,z,w)

    float v[3] = {1.0f, 2.0f, 0.5f};
    float qr[3];
    QuatRotateVector(q, v, qr);

    // Reference: rotate v about Z by `angle`.
    float ref[3] = {
        v[0] * std::cos(angle) - v[1] * std::sin(angle),
        v[0] * std::sin(angle) + v[1] * std::cos(angle),
        v[2]};
    CHECK(nearf(qr[0], ref[0], 1e-3f));
    CHECK(nearf(qr[1], ref[1], 1e-3f));
    CHECK(nearf(qr[2], ref[2], 1e-3f));
}

// --- QuatNormalizeAxis ------------------------------------------------------
TEST(UtilMatrix, QuatNormalizeAxisMakesUnit) {
    float q[4] = {3.0f, 0.0f, 4.0f, 0.5f};  // w=0.5 in [-1,1]
    QuatNormalizeAxis(q);
    // axis should be scaled so that x^2+y^2+z^2 == 1 - w^2.
    float axisSq = q[0] * q[0] + q[1] * q[1] + q[2] * q[2];
    float expect = 1.0f - 0.5f * 0.5f;
    CHECK(nearf(axisSq, expect, 1e-4f));
    // w unchanged.
    CHECK(nearf(q[3], 0.5f));
}

TEST(UtilMatrix, QuatNormalizeAxisNoopOutOfRange) {
    float q[4] = {3.0f, 0.0f, 4.0f, 2.0f};  // w > 1 -> no change
    float before[4]; std::memcpy(before, q, sizeof(q));
    QuatNormalizeAxis(q);
    for (int i = 0; i < 4; ++i) CHECK(nearf(q[i], before[i]));
}

// --- MatrixTransformVectors on a batch (compose two rotations) --------------
TEST(UtilMatrix, TransformVectorsComposesRotations) {
    float a0[3] = {0.0f, 0.0f, 0.3f};
    float b0[3] = {0.0f, 0.0f, 0.4f};
    float A[16], B[16];
    MatrixFromEuler(a0, A);
    MatrixFromEuler(b0, B);
    float C[16];
    MatrixTransformVectors(A, B, C);  // C = A (*) B
    // Compose should equal a single rotation by 0.3+0.4 = 0.7 about Z.
    float ab[3] = {0.0f, 0.0f, 0.7f};
    float D[16];
    MatrixFromEuler(ab, D);
    for (int i = 0; i < 11; ++i)
        CHECK(nearf(C[i], D[i], 2e-3f));
}

// --- SnapVectorToAxis: unwraps angles toward the reference ------------------
TEST(UtilMatrix, SnapVectorToAxisUnwraps) {
    // src angle ~ -3.0 (fmod stays -3.0); ref angle ~ +3.1. Closest representative
    // of -3.0 to +3.1 is -3.0 + 2pi ~= 3.283. Snap should pick the +2pi shift.
    float src[3] = {-3.0f, 0.1f, 0.0f};
    float ref[3] = {3.1f, 0.1f, 0.0f};
    float out[3], refmod[3];
    SnapVectorToAxis(src, ref, out, refmod);
    float twoPi = 6.2831853f;
    CHECK(nearf(out[0], -3.0f + twoPi, 1e-3f));
    CHECK(nearf(out[1], 0.1f, 1e-3f));   // already closest
    CHECK(nearf(out[2], 0.0f, 1e-3f));
}

// --- BuildBasisFromAngle: produces an orthonormal basis ---------------------
TEST(UtilMatrix, BuildBasisOrthonormal) {
    float dir[3] = {0.0f, 0.0f, 1.0f};  // forward along Z
    float out[16];
    BuildBasisFromAngle(dir, 0.0f, out);
    // The three basis vectors out[0..2], out[4..6], out[8..10] are unit & orthogonal.
    auto len = [](const float* v) {
        return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    };
    auto dot = [](const float* a, const float* b) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };
    CHECK(nearf(len(out + 8), 1.0f, 1e-3f));   // primary axis == normalized dir
    CHECK(nearf(len(out + 4), 1.0f, 1e-3f));
    CHECK(nearf(len(out + 0), 1.0f, 1e-3f));
    CHECK(nearf(dot(out + 8, out + 4), 0.0f, 1e-3f));
    CHECK(nearf(dot(out + 8, out + 0), 0.0f, 1e-3f));
    CHECK(nearf(dot(out + 4, out + 0), 0.0f, 1e-3f));
    CHECK(nearf(out[15], 1.0f));
}

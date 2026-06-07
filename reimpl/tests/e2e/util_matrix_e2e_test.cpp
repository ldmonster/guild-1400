// End-to-end flow for guild::util matrix + transform: compose a TRS transform,
// apply it to a batch of points, invert it, and recover the originals within
// epsilon. Also exercises the bone-chain Transform_* walkers on a synthetic frame.
#include "test.h"
#include "util/matrix.h"
#include "util/transform.h"

#include <cmath>
#include <cstring>

using namespace guild::util;

namespace {

bool nearf(float a, float b, float eps = 3e-3f) {
    return std::fabs(a - b) <= eps;
}

// Apply an affine 4x4 (rotation block 0..2/4..6/8..10, translation 12,13,14) to a
// point: p' = R*p + T, using the same index layout MatrixTransformVectors uses.
void ApplyAffine(const float* m, const float* p, float* out) {
    out[0] = p[0] * m[0] + p[1] * m[4] + p[2] * m[8] + m[12];
    out[1] = p[0] * m[1] + p[1] * m[5] + p[2] * m[9] + m[13];
    out[2] = p[0] * m[2] + p[1] * m[6] + p[2] * m[10] + m[14];
}

} // namespace

TEST(UtilMatrixE2E, TrsApplyInvertRecover) {
    // Build a rotation from Euler, attach a translation -> a TRS transform.
    float euler[3] = {0.5f, -0.3f, 0.8f};
    float M[16];
    MatrixFromEuler(euler, M);
    M[12] = 10.0f;
    M[13] = -4.0f;
    M[14] = 7.5f;

    // A batch of points.
    const float pts[][3] = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {2.5f, -1.5f, 3.0f},
        {-4.0f, 6.0f, -2.0f},
    };
    const int N = sizeof(pts) / sizeof(pts[0]);

    // Apply M to each point.
    float transformed[N][3];
    for (int i = 0; i < N; ++i)
        ApplyAffine(M, pts[i], transformed[i]);

    // Invert M.
    float Minv[16];
    MatrixInverse(M, Minv);

    // Apply the inverse to recover the originals.
    for (int i = 0; i < N; ++i) {
        float rec[3];
        ApplyAffine(Minv, transformed[i], rec);
        CHECK(nearf(rec[0], pts[i][0]));
        CHECK(nearf(rec[1], pts[i][1]));
        CHECK(nearf(rec[2], pts[i][2]));
    }
}

TEST(UtilMatrixE2E, InverseTimesMatrixIsIdentity) {
    float euler[3] = {-0.7f, 0.9f, 0.2f};
    float M[16];
    MatrixFromEuler(euler, M);
    M[12] = -2.0f; M[13] = 3.3f; M[14] = 0.5f;
    float Minv[16];
    MatrixInverse(M, Minv);
    float prod[16];
    MatrixTransformVectors(M, Minv, prod);
    for (int i = 0; i < 16; ++i) {
        float expect = (i == 0 || i == 5 || i == 10 || i == 15) ? 1.0f : 0.0f;
        CHECK(nearf(prod[i], expect, 3e-3f));
    }
}

// Exercise the bone-chain transform walkers on a synthetic two-frame skeleton.
// We build a flat float buffer per frame large enough for the touched offsets
// (>= float index 127 to hold the parent link at byte 504), set an identity 3x3
// at indices 99..109 and zero translations, so the chain transforms reduce to
// translations we can predict.
TEST(UtilMatrixE2E, BoneChainIdentityFrame) {
    // 160 floats is comfortably > 127; link slot is at byte 504 == float 126.
    static float child[160];
    static float parent[160];
    std::memset(child, 0, sizeof(child));
    std::memset(parent, 0, sizeof(parent));

    auto setIdentity3x3 = [](float* f) {
        // 3x3 read as rows {99,103,107},{100,104,108},{101,105,109}; identity.
        f[99] = 1.0f;  f[103] = 0.0f; f[107] = 0.0f;
        f[100] = 0.0f; f[104] = 1.0f; f[108] = 0.0f;
        f[101] = 0.0f; f[105] = 0.0f; f[109] = 1.0f;
    };
    setIdentity3x3(child);
    setIdentity3x3(parent);

    // Child local translation (frame[30..32]).
    child[30] = 1.0f; child[31] = 2.0f; child[32] = 3.0f;

    // No parent link on the child for the simplest case -> single translate.
    float** childLink = reinterpret_cast<float**>(reinterpret_cast<char*>(child) + 504);
    *childLink = nullptr;

    float point[3] = {10.0f, 20.0f, 30.0f};
    float out[3];
    PointThroughBoneChain(child, point, out);
    // With no parent and identity, result = point + child[30..32].
    CHECK(nearf(out[0], 11.0f));
    CHECK(nearf(out[1], 22.0f));
    CHECK(nearf(out[2], 33.0f));

    // RotateVectorByHierarchy with identity frame and no parent -> vector unchanged.
    float vec[3] = {0.3f, -0.6f, 0.9f};
    float rout[3];
    RotateVectorByHierarchy(child, vec, rout);
    CHECK(nearf(rout[0], vec[0]));
    CHECK(nearf(rout[1], vec[1]));
    CHECK(nearf(rout[2], vec[2]));
}

TEST(UtilMatrixE2E, BoneChainWithParentTranslation) {
    static float child[160];
    static float parent[160];
    std::memset(child, 0, sizeof(child));
    std::memset(parent, 0, sizeof(parent));
    auto setIdentity3x3 = [](float* f) {
        f[99] = 1.0f; f[104] = 1.0f; f[109] = 1.0f;
    };
    setIdentity3x3(child);
    setIdentity3x3(parent);

    // Parent contributes translation terms: pivot i[27..29], i[19..21], i[30..32].
    // With identity rotation the loop body reduces to:
    //   out = (out + i[27]) - i[27] + i[19] + i[30] == out + i[19] + i[30].
    parent[19] = 0.5f; parent[20] = 0.0f; parent[21] = 0.0f;
    parent[30] = 0.0f; parent[31] = 0.25f; parent[32] = 0.0f;
    parent[27] = 100.0f; parent[28] = 100.0f; parent[29] = 100.0f;  // cancels out

    // Link child -> parent; parent has no parent.
    float** childLink = reinterpret_cast<float**>(reinterpret_cast<char*>(child) + 504);
    *childLink = parent;
    float** parentLink = reinterpret_cast<float**>(reinterpret_cast<char*>(parent) + 504);
    *parentLink = nullptr;

    child[30] = 1.0f; child[31] = 1.0f; child[32] = 1.0f;

    float point[3] = {0.0f, 0.0f, 0.0f};
    float out[3];
    PointThroughBoneChain(child, point, out);
    // step1 (child local): 0 + child[30..32] = (1,1,1)
    // step2 (parent identity): (1,1,1) + parent[19]+parent[30] etc.
    //   x: 1 + 0.5 + 0.0 = 1.5; y: 1 + 0.0 + 0.25 = 1.25; z: 1 + 0 + 0 = 1.0
    CHECK(nearf(out[0], 1.5f));
    CHECK(nearf(out[1], 1.25f));
    CHECK(nearf(out[2], 1.0f));
}

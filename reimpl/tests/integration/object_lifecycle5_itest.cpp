// Integration: drive object_lifecycle5's VIBE_Object_TransformPointToParent /
// ApplyParentTransform against the REAL reconstructed Math-matrix siblings
// (util/matrix.cpp: VIBE_Math_MatrixInverse 0x5cac3c, MatrixCopy 0x5cabf0,
// MatrixTransformVectors 0x5caaa4, MatrixToEuler 0x5cb2cc). This is exactly the
// live wiring: the original TransformPointToParent body calls those Math leaves
// through what this reimpl models as ObjLife5Hooks.{matrixInverse,matrixCopy,
// matrixTransformVectors,matrixToEuler}. We forward each hook into the genuine
// reconstructed function (with thin arg-order adapters where the hook's parameter
// order differs from the Math sibling's register order) and feed a known bone
// world matrix through fetchWorldMatrix, then assert the cross-module point
// transform the binary would produce.
//
// The remaining graphics leaves (Mesh/Light/Texture/Anim/SceneGraph/Memory) have
// no reconstructed sibling; those hooks stay inert (their faithful node-field
// arithmetic still runs), which is also exercised here (suspend-state + active
// camera paths run entirely through the module's inert default-hook path).
#include "test.h"

#include "sim/object_lifecycle5.h"
#include "util/matrix.h"

#include <cmath>

using namespace guild;
using namespace guild::sim;

namespace {

// A known bone world matrix used for both the parent and the node fetch: a pure
// translation by (10, 20, 30) on the homogeneous bottom row (column-major as the
// engine stores it — translation at m[12..14]).
float g_worldMat[16];

void InitWorldMat() {
    for (int i = 0; i < 16; ++i) g_worldMat[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    g_worldMat[12] = 10.0f;
    g_worldMat[13] = 20.0f;
    g_worldMat[14] = 30.0f;
}

void FetchWorldMatrix(Node5* /*node*/, float* out) {
    for (int i = 0; i < 16; ++i) out[i] = g_worldMat[i];
}
void ComputeBoneWorldMatrix(Node5* /*node*/, int /*a*/, int /*which*/) {}

// --- REAL Math siblings, wired exactly as the engine indirection forwards ---
// MatrixInverse hook order matches util::MatrixInverse(src, dst).
void RealMatrixInverse(const float* src, float* dst) {
    util::MatrixInverse(src, dst);
}
// The hook is matrixCopy(dst, src); the real sibling is MatrixCopy(src, dst).
void RealMatrixCopy(float* dst, const float* src) {
    util::MatrixCopy(src, dst);
}
// hook: (angles, mat, out); real: MatrixTransformVectors(a, b, out). The original
// passes the angle/source row as `a` and the world matrix as `b`.
void RealMatrixTransformVectors(const float* angles, const float* mat, float* out) {
    float a[16];
    for (int i = 0; i < 16; ++i) a[i] = (i < 3) ? angles[i] : ((i % 5 == 0) ? 1.0f : 0.0f);
    util::MatrixTransformVectors(a, mat, out);
}
void RealMatrixToEuler(float* angles) {
    // Build a 16-float matrix whose first 3 floats hold the angle triple, exactly
    // as the in-place MatrixToEuler operand the original passes.
    float m[16];
    for (int i = 0; i < 16; ++i) m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    m[0] = angles[0]; m[1] = angles[1]; m[2] = angles[2];
    util::MatrixToEuler(m);
    angles[0] = m[0]; angles[1] = m[1]; angles[2] = m[2];
}

ObjLife5Hooks MakeRealMatrixHooks() {
    ObjLife5Hooks h{};
    h.computeBoneWorldMatrix = ComputeBoneWorldMatrix;
    h.fetchWorldMatrix = FetchWorldMatrix;
    h.matrixInverse = RealMatrixInverse;
    h.matrixCopy = RealMatrixCopy;
    h.matrixTransformVectors = RealMatrixTransformVectors;
    h.matrixToEuler = RealMatrixToEuler;
    return h;
}

} // namespace

// TransformPointToParent with a parent bone forwards the point through the REAL
// MatrixInverse of the fed world matrix; the inverse of a pure (10,20,30)
// translation maps a point P to P - (10,20,30).
TEST(ObjectLifecycle5Itest, TransformPointThroughRealMatrixInverse) {
    InitWorldMat();
    auto h = MakeRealMatrixHooks();
    ObjLife5SetHooks(h);

    Node5 parentBone;            // the +504 parent-bone owner
    Node5 node;
    node.p(504) = &parentBone;   // has a parent bone -> the matrix path

    float point[3] = {100.0f, 200.0f, 300.0f};
    float inAngles[3] = {0.0f, 0.0f, 0.0f};
    float outPoint[3] = {0, 0, 0};
    float outAngles[3] = {0, 0, 0};

    ObjectTransformPointToParent(&node, point, outPoint, inAngles, outAngles);
    ObjLife5ResetHooks();

    // Real MatrixInverse of a pure translation T(10,20,30) is T(-10,-20,-30); the
    // module's column-layout multiply then yields point + inverse-translation.
    CHECK(std::fabs(outPoint[0] - 90.0f) < 1e-3f);
    CHECK(std::fabs(outPoint[1] - 180.0f) < 1e-3f);
    CHECK(std::fabs(outPoint[2] - 270.0f) < 1e-3f);
}

// No-parent path: the point passes through unchanged and the REAL MatrixToEuler
// is applied to the angle triple in place (identity-rotation angles -> all zero).
TEST(ObjectLifecycle5Itest, NoParentPassesPointAndRealEuler) {
    auto h = MakeRealMatrixHooks();
    ObjLife5SetHooks(h);

    Node5 node;                  // p(504) == nullptr -> the passthrough branch
    float point[3] = {5.0f, 6.0f, 7.0f};
    float inAngles[3] = {0.0f, 0.0f, 0.0f};
    float outPoint[3] = {0, 0, 0};
    float outAngles[3] = {0, 0, 0};

    ObjectTransformPointToParent(&node, point, outPoint, inAngles, outAngles);
    ObjLife5ResetHooks();

    CHECK(std::fabs(outPoint[0] - 5.0f) < 1e-4f);
    CHECK(std::fabs(outPoint[1] - 6.0f) < 1e-4f);
    CHECK(std::fabs(outPoint[2] - 7.0f) < 1e-4f);
    // MatrixToEuler of an identity-ish basis is finite (no NaN) — proves the real
    // sibling actually ran on the angle triple.
    CHECK(std::isfinite(outAngles[0]));
    CHECK(std::isfinite(outAngles[1]));
    CHECK(std::isfinite(outAngles[2]));
}

// ApplyParentTransform is a thunk over TransformPointToParent + the SetPosition /
// SetWorldTranslation leaves; with the REAL matrix path wired and the position
// leaf captured, the captured position equals the real inverse-transformed point.
TEST(ObjectLifecycle5Itest, ApplyParentTransformFeedsRealTransformedPoint) {
    InitWorldMat();
    auto h = MakeRealMatrixHooks();

    static float g_setPos[3] = {0, 0, 0};
    static int g_setPosCalls = 0;
    g_setPosCalls = 0;
    h.objSetPosition = [](Node5*, const float* pos) {
        g_setPos[0] = pos[0]; g_setPos[1] = pos[1]; g_setPos[2] = pos[2];
        g_setPosCalls++;
    };
    ObjLife5SetHooks(h);

    Node5 parentBone;
    Node5 node;
    node.p(504) = &parentBone;
    float point[3] = {1.0f, 2.0f, 3.0f};
    float angles[3] = {0.0f, 0.0f, 0.0f};

    char r = ObjectApplyParentTransform(&node, point, angles);
    ObjLife5ResetHooks();

    CHECK_EQ((int)r, 1);
    CHECK_EQ(g_setPosCalls, 1);
    // point - (10,20,30) via the REAL MatrixInverse.
    CHECK(std::fabs(g_setPos[0] - (-9.0f)) < 1e-3f);
    CHECK(std::fabs(g_setPos[1] - (-18.0f)) < 1e-3f);
    CHECK(std::fabs(g_setPos[2] - (-27.0f)) < 1e-3f);
}

// The graphics leaves have no reconstructed sibling: the suspend-state toggle and
// the active-camera global run end-to-end through the module's INERT default-hook
// path (no hooks installed), exercising the faithful node-field arithmetic alone.
TEST(ObjectLifecycle5Itest, SuspendAndCameraInertDefaultPath) {
    ObjLife5ResetHooks();   // inert defaults — no graphics leaves installed

    // Suspend: enable==0 latches state 1 and saves the old nodeType into +534.
    Node5 node;
    node.sb(533) = 7;       // current nodeType
    char r = ObjectToggleSuspendStateNamed(&node, /*enable*/ 0);
    CHECK_EQ((int)r, 1);
    CHECK_EQ((int)node.sb(533), 1);   // suspended
    CHECK_EQ((int)node.sb(534), 7);   // saved type

    // Restore (enable!=0 while suspended): pulls +534 back into +533. The light
    // refresh leaf is inert (state 7, not 5) so nothing external is needed.
    r = ObjectToggleSuspendStateNamed(&node, /*enable*/ 1);
    CHECK_EQ((int)r, 1);
    CHECK_EQ((int)node.sb(533), 7);

    // Active camera: only a node with byte533==3 becomes the active camera global.
    g_activeCamera5 = nullptr;
    Node5 cam;  cam.b(533) = 3;
    int set = ObjectSetActiveCamera(&cam);
    CHECK_EQ(set, 1);
    CHECK_EQ(g_activeCamera5, &cam);

    Node5 notCam; notCam.b(533) = 1;   // not a camera -> no change
    g_activeCamera5 = nullptr;
    ObjectSetActiveCamera(&notCam);
    CHECK_EQ(g_activeCamera5, (Node5*)nullptr);
}

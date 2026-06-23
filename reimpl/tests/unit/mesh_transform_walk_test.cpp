#include "render/mesh_transform_walk.h"
#include "render/scene_transform.h"   // MatrixFromEuler / Apply (build a real world matrix)

#include "tests/framework/test.h"

#include <cmath>

// =============================================================================
// MeshTransformWalk — golden tests for the static object-vertex transform walk of
// VIBE_Mesh_InterpolateMorphVertices @0x5c953c (render::TransformMeshVerticesByMatrix):
// identity, translation, the column-major rotation+translation map, and the
// running near/far z bounds (the +529&0x20 path). No assets.
// =============================================================================
using namespace guild;
using guild::render::Vertex;
using guild::render::TransformMeshVerticesByMatrix;
using guild::render::MeshTransformResult;

namespace {
bool feq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
Vertex V(float x, float y, float z) { Vertex v{}; v.x = x; v.y = y; v.z = z; return v; }
} // namespace

// (a) Identity matrix leaves positions unchanged.
TEST(MeshTransformWalk, Identity) {
    Vertex verts[2] = {V(1, 2, 3), V(-4, 5, -6)};
    float m[16] = {0};
    m[0] = m[5] = m[10] = m[15] = 1.0f;
    MeshTransformResult r = TransformMeshVerticesByMatrix(verts, 2, m);
    CHECK_EQ(r.count, 2);
    CHECK(feq(verts[0].x, 1) && feq(verts[0].y, 2) && feq(verts[0].z, 3));
    CHECK(feq(verts[1].x, -4) && feq(verts[1].y, 5) && feq(verts[1].z, -6));
}

// (b) Translation column (m[12..14]) shifts every vertex.
TEST(MeshTransformWalk, Translation) {
    Vertex verts[1] = {V(1, 2, 3)};
    float m[16] = {0};
    m[0] = m[5] = m[10] = m[15] = 1.0f;
    m[12] = 10.0f; m[13] = 20.0f; m[14] = 30.0f;
    TransformMeshVerticesByMatrix(verts, 1, m);
    CHECK(feq(verts[0].x, 11) && feq(verts[0].y, 22) && feq(verts[0].z, 33));
}

// (c) Column-major rotation+translation: out.x = x*m[0]+y*m[4]+z*m[8]+m[12], etc.
// A 90° rotation about Y (cos=0, sin=1): out.x = z, out.z = -x (engine column layout).
TEST(MeshTransformWalk, RotationColumnMajor) {
    Vertex verts[1] = {V(1, 0, 2)};
    // Row-major rotation R about Y by +90deg: Rx=( c, 0, s) Ry=(0,1,0) Rz=(-s,0,c).
    // Build the engine column-major world matrix from R (rotation only).
    float c = 0.0f, s = 1.0f;
    float m[16] = {0};
    // out.x = x*m[0] + y*m[4] + z*m[8] = x*c + z*s  -> m[0]=c, m[4]=0, m[8]=s
    m[0] = c;  m[4] = 0;  m[8]  = s;   m[12] = 0;
    m[1] = 0;  m[5] = 1;  m[9]  = 0;   m[13] = 0;
    // out.z = x*m[2] + z*m[10] = -x*s + z*c -> m[2]=-s, m[10]=c
    m[2] = -s; m[6] = 0;  m[10] = c;   m[14] = 0;
    m[15] = 1.0f;
    TransformMeshVerticesByMatrix(verts, 1, m);
    // (1,0,2) -> x' = 1*0 + 2*1 = 2 ; y'=0 ; z' = -1*1 + 2*0 = -1.
    CHECK(feq(verts[0].x, 2) && feq(verts[0].y, 0) && feq(verts[0].z, -1));
}

// (d) Depth tracking (the +529&0x20 path): running min/max of transformed z.
TEST(MeshTransformWalk, DepthBounds) {
    Vertex verts[3] = {V(0, 0, 5), V(0, 0, -2), V(0, 0, 1)};
    float m[16] = {0};
    m[0] = m[5] = m[10] = m[15] = 1.0f;
    MeshTransformResult r = TransformMeshVerticesByMatrix(verts, 3, m,
                                                          /*trackDepth*/true,
                                                          /*nearSeed*/1e30f, /*farSeed*/-1e30f);
    CHECK(feq(r.nearZ, -2.0f));   // min z
    CHECK(feq(r.farZ, 5.0f));     // max z
    // Without trackDepth the seeds are returned untouched.
    MeshTransformResult r2 = TransformMeshVerticesByMatrix(verts, 3, m, /*trackDepth*/false);
    CHECK(feq(r2.nearZ, 1e30f));
    CHECK(feq(r2.farZ, -1e30f));
}

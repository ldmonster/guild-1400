#include "render/anim_relight.h"
#include "render/skeleton_pose.h"      // MorphMeshBlock, VertexSource, ComputeMeshVertexLightingNonSkinned
#include "render/vertex_lighting.h"    // ComputeEnvMapReflectionUv (golden reference)
#include "render/geometry_types.h"     // Vertex
#include "tests/framework/test.h"

#include <cmath>
#include <vector>

using namespace guild::render;
using guild::u8;
using guild::i32;

namespace {
bool feq(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

// Identity 3x3 in the engine's flat {m0,m1,m2, m4,m5,m6, m8,m9,m10} order.
const float kIdentity3x3[9] = {1, 0, 0,  0, 1, 0,  0, 0, 1};
} // namespace

// =========================================================================
// AnimRelight — VIBE_Anim_CalculateAnimNormals(0x5d0020) -> VIBE_Mesh_ComputeVertex
// Lighting(0x5c9054) glue. RelightPosedFrame must push the frame's per-vertex normals
// into each VertexSource.normal (+12) and then relight, so the env-map UV reflects the
// CURRENT-frame normal rather than the stale rest-pose one.
// =========================================================================

// Closed-form golden: identity rotation, normals orthogonal to the position so the
// reflection dot term vanishes (R == P), giving exact hand-computable UVs.
TEST(AnimRelight, ClosedFormOrthogonalNormals) {
    Vertex verts[2] = {};
    // Vertex 0: P = (1,0,0).  Vertex 1: P = (0,2,0).
    verts[0].x = 1.0f; verts[0].y = 0.0f; verts[0].z = 0.0f;
    verts[1].x = 0.0f; verts[1].y = 2.0f; verts[1].z = 0.0f;

    // Rest-pose source normals deliberately WRONG (would mislight if used) — proves
    // RelightPosedFrame overwrites them with the frame normals.
    VertexSource src[2] = {};
    src[0].normal[0] = 9.0f; src[0].normal[1] = 9.0f; src[0].normal[2] = 9.0f;
    src[1].normal[0] = -9.0f; src[1].normal[1] = -9.0f; src[1].normal[2] = -9.0f;

    u8 lit[2] = {1, 1};

    MorphMeshBlock mesh;
    mesh.vertices = verts;
    mesh.sources = src;
    mesh.litFlags = lit;
    mesh.vertexCount = 2;

    // Frame normals: v0 = +Z (0,0,1), v1 = +X (1,0,0). Both orthogonal to their pos.
    std::vector<float> frameNormals = {
        0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f,
    };

    RelightPosedFrame(mesh, frameNormals, kIdentity3x3);

    // The frame normals must have landed in the source +12 slots.
    CHECK(feq(src[0].normal[0], 0.0f)); CHECK(feq(src[0].normal[1], 0.0f)); CHECK(feq(src[0].normal[2], 1.0f));
    CHECK(feq(src[1].normal[0], 1.0f)); CHECK(feq(src[1].normal[1], 0.0f)); CHECK(feq(src[1].normal[2], 0.0f));

    // Vertex 0: N=(0,0,1), P=(1,0,0). dot=(N.P)*-2=0 -> R=P=(1,0,0) -> normalize=(1,0,0)
    //   u = 0.5*1+0.5 = 1.0 ; v = 0.5*0+0.5 = 0.5
    CHECK(feq(verts[0].u, 1.0f)); CHECK(feq(verts[0].v, 0.5f));
    // Vertex 1: N=(1,0,0), P=(0,2,0). dot=0 -> R=(0,2,0) -> normalize=(0,1,0)
    //   u = 0.5*0+0.5 = 0.5 ; v = 0.5*1+0.5 = 1.0
    CHECK(feq(verts[1].u, 0.5f)); CHECK(feq(verts[1].v, 1.0f));
}

// Non-trivial reflection (dot term active): assert RelightPosedFrame produces exactly
// what the env-map kernel computes for the frame normal — i.e. it is the kernel fed
// the frame normal, not the rest normal.
TEST(AnimRelight, MatchesKernelWithFrameNormal) {
    Vertex v = {};
    v.x = 0.3f; v.y = -0.7f; v.z = 1.2f;

    VertexSource src = {};
    src.normal[0] = 0.0f; src.normal[1] = 1.0f; src.normal[2] = 0.0f;  // stale rest normal

    u8 lit = 1;

    MorphMeshBlock mesh;
    mesh.vertices = &v;
    mesh.sources = &src;
    mesh.litFlags = &lit;
    mesh.vertexCount = 1;

    // A non-axis-aligned frame normal (not orthogonal to pos -> dot term contributes).
    std::vector<float> frameNormals = {0.6f, 0.0f, 0.8f};  // already unit (0.36+0.64=1)

    // Golden reference: run the kernel directly with the FRAME normal + same pos/m3x3.
    float pos[3] = {v.x, v.y, v.z};
    float frameN[3] = {0.6f, 0.0f, 0.8f};
    float expectUv[2];
    ComputeEnvMapReflectionUv(pos, frameN, kIdentity3x3, expectUv);

    RelightPosedFrame(mesh, frameNormals, kIdentity3x3);

    CHECK(feq(v.u, expectUv[0]));
    CHECK(feq(v.v, expectUv[1]));
    // Sanity: had the stale (0,1,0) normal been used, the UV would differ — confirm the
    // frame normal was actually applied by comparing against the stale-normal kernel.
    float staleN[3] = {0.0f, 1.0f, 0.0f};
    float staleUv[2];
    ComputeEnvMapReflectionUv(pos, staleN, kIdentity3x3, staleUv);
    CHECK(!feq(v.u, staleUv[0]) || !feq(v.v, staleUv[1]));
}

// Unlit vertices (+77 flag clear) are skipped by the lighting walk: RelightPosedFrame
// still copies the normal but writes no UV.
TEST(AnimRelight, UnlitVertexSkipped) {
    Vertex v = {};
    v.x = 1.0f; v.y = 0.0f; v.z = 0.0f;
    v.u = 123.0f; v.v = 456.0f;  // sentinel; must remain untouched

    VertexSource src = {};
    u8 lit = 0;  // not lit

    MorphMeshBlock mesh;
    mesh.vertices = &v;
    mesh.sources = &src;
    mesh.litFlags = &lit;
    mesh.vertexCount = 1;

    std::vector<float> frameNormals = {0.0f, 0.0f, 1.0f};
    RelightPosedFrame(mesh, frameNormals, kIdentity3x3);

    // Normal still applied to the source (+12) ...
    CHECK(feq(src.normal[2], 1.0f));
    // ... but the UV sentinel is untouched (lighting walk skipped the vertex).
    CHECK(feq(v.u, 123.0f)); CHECK(feq(v.v, 456.0f));
}

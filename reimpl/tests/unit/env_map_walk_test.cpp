#include "render/env_map_walk.h"
#include "render/vertex_lighting.h"   // ComputeEnvMapReflectionUv (oracle)

#include "tests/framework/test.h"

#include <cmath>
#include <cstddef>
#include <vector>

// =============================================================================
// EnvMapWalk — golden tests for the env-map reflection-UV object walk of
// VIBE_Mesh_ComputeVertexLighting @0x5c9054 (render::ComputeEnvMapVertexUvs):
// the non-skinned + skinned normal-source branches, the per-vertex gate, and the
// early-outs. The per-vertex math is cross-checked against the reconstructed kernel.
// No assets.
// =============================================================================
using namespace guild;
using guild::render::Vertex;
using guild::render::EnvMapWalkInputs;
using guild::render::ComputeEnvMapVertexUvs;

namespace {
bool feq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
Vertex Vp(float x, float y, float z) { Vertex v{}; v.x = x; v.y = y; v.z = z; return v; }
// identity bone 3x3 in the kernel order {m0,m1,m2, m4,m5,m6, m8,m9,m10}.
const float kIdentity3x3[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
} // namespace

// (a) Non-skinned walk: env-map UV per vertex, matching the kernel oracle.
TEST(EnvMapWalk, NonSkinnedMatchesKernel) {
    Vertex verts[2] = {Vp(1, 0, 0), Vp(0, 0, 1)};
    float normals[6] = {0, 0, 1,   0, 0, -1};

    EnvMapWalkInputs in;
    in.m3x3 = kIdentity3x3;
    in.vertexNormals = normals;
    int n = ComputeEnvMapVertexUvs(verts, 2, in);
    CHECK_EQ(n, 2);

    // Oracle: pos (1,0,0) normal (0,0,1): Nt=(0,0,1); dot=(Nt.pos)*-2=0; R=pos=(1,0,0);
    // uv = (0.5*1+0.5, 0.5*0+0.5) = (1.0, 0.5).
    CHECK(feq(verts[0].u, 1.0f)); CHECK(feq(verts[0].v, 0.5f));
    // Cross-check the second vertex directly against the kernel.
    float uv[2];
    const float pos1[3] = {0, 0, 1};
    guild::render::ComputeEnvMapReflectionUv(pos1, &normals[3], kIdentity3x3, uv);
    CHECK(feq(verts[1].u, uv[0])); CHECK(feq(verts[1].v, uv[1]));
}

// (b) Per-vertex reflective gate (+77): only flagged vertices get UVs written.
TEST(EnvMapWalk, ReflectiveGate) {
    Vertex verts[3] = {Vp(1, 0, 0), Vp(0, 1, 0), Vp(0, 0, 1)};
    for (auto& v : verts) { v.u = -9.0f; v.v = -9.0f; }   // sentinel
    float normals[9] = {0, 0, 1,  0, 0, 1,  0, 0, 1};
    u8 flags[3] = {1, 0, 1};

    EnvMapWalkInputs in;
    in.m3x3 = kIdentity3x3;
    in.vertexNormals = normals;
    in.reflectiveFlags = flags;
    int n = ComputeEnvMapVertexUvs(verts, 3, in);
    CHECK_EQ(n, 2);                         // only verts 0 and 2
    CHECK(verts[0].u != -9.0f);             // written
    CHECK(feq(verts[1].u, -9.0f));          // skipped (flag 0)
    CHECK(verts[2].u != -9.0f);             // written
}

// (c) Skinned walk: normal unpacked from keyframe skin bytes (n=(b-128)*2/255).
TEST(EnvMapWalk, SkinnedNormalSource) {
    Vertex verts[1] = {Vp(1, 0, 0)};
    // byte triple (128,128,255) -> normal ~ (0, 0, 0.996).
    u8 skin[3] = {128, 128, 255};

    EnvMapWalkInputs in;
    in.m3x3 = kIdentity3x3;
    in.skinNormalBytes = skin;
    int n = ComputeEnvMapVertexUvs(verts, 1, in);
    CHECK_EQ(n, 1);

    float nUnpacked[3]; guild::render::UnpackSkinNormal(skin, nUnpacked);
    const float pos0[3] = {1, 0, 0};
    float uv[2]; guild::render::ComputeEnvMapReflectionUv(pos0, nUnpacked, kIdentity3x3, uv);
    CHECK(feq(verts[0].u, uv[0])); CHECK(feq(verts[0].v, uv[1]));
}

// (d) Early-outs: null m3x3 or no normal source -> no-op (returns 0).
TEST(EnvMapWalk, EarlyOuts) {
    Vertex verts[1] = {Vp(1, 0, 0)};
    static const float n001[3] = {0, 0, 1};
    EnvMapWalkInputs noMatrix; noMatrix.vertexNormals = n001;
    CHECK_EQ(ComputeEnvMapVertexUvs(verts, 1, noMatrix), 0);
    EnvMapWalkInputs noNormals; noNormals.m3x3 = kIdentity3x3;
    CHECK_EQ(ComputeEnvMapVertexUvs(verts, 1, noNormals), 0);
    // null verts / non-positive count.
    CHECK_EQ(ComputeEnvMapVertexUvs(nullptr, 1, noNormals), 0);
    EnvMapWalkInputs ok; ok.m3x3 = kIdentity3x3; ok.vertexNormals = n001;
    CHECK_EQ(ComputeEnvMapVertexUvs(verts, 0, ok), 0);
}

// (e) GOLDEN reflection vector — non-identity bone 3x3, non-trivial dot.
// Bone 3x3 = {0,1,0, -1,0,0, 0,0,1} (the v20..v28 block, kernel order). For
// pos (1,2,3), normal (1,0,0):  Nt = (m0,m1,m2) = (0,1,0);
//   dot = (Nt.pos)*-2 = (0*1 + 1*2 + 0*3)*-2 = -4;
//   R   = (-4*0+1, -4*1+2, -4*0+3) = (1, -2, 3); normalize -> (0.267261, -0.534522, 0.801784);
//   u = 0.5*Rx+0.5 = 0.633631;  v = 0.5 + 0.5*Ry = 0.232739.
// This pins the env-map reflect math AND the walk's matrix-multiply ordering
// (skinned and non-skinned branches index v20..v28 identically per 0x5c9054).
TEST(EnvMapWalk, GoldenNonIdentityReflection) {
    const float bone[9] = {0, 1, 0,  -1, 0, 0,  0, 0, 1};
    Vertex verts[1] = {Vp(1, 2, 3)};
    float normals[3] = {1, 0, 0};
    EnvMapWalkInputs in; in.m3x3 = bone; in.vertexNormals = normals;
    CHECK_EQ(ComputeEnvMapVertexUvs(verts, 1, in), 1);
    CHECK(feq(verts[0].u, 0.6336306f));
    CHECK(feq(verts[0].v, 0.2327388f));
}

// (f) Exact recovered constants (get_bytes @0x628CBC..C8) surface in the math:
//   flt_628CC0 = 0.5  -> a vertex whose reflected R is (0,0,*) maps to UV (0.5, 0.5)
//     (R.x = R.y = 0 -> u = 0.5*0+0.5, v = 0.5+0.5*0).
//   flt_628CC4 = 2/255, flt_628CC8 = -128  -> skin byte 128 unpacks to exactly 0.
TEST(EnvMapWalk, ConstantsGolden) {
    // pos (0,0,1), normal (0,0,1) (identity bone): Nt=(0,0,1); dot=(0+0+1)*-2=-2;
    // R = (0, 0, -2*1+1) = (0,0,-1); normalize (0,0,-1); u=0.5*0+0.5=0.5; v=0.5+0.5*0=0.5.
    Vertex verts[1] = {Vp(0, 0, 1)};
    float n[3] = {0, 0, 1};
    EnvMapWalkInputs in; in.m3x3 = kIdentity3x3; in.vertexNormals = n;
    CHECK_EQ(ComputeEnvMapVertexUvs(verts, 1, in), 1);
    CHECK(feq(verts[0].u, 0.5f));
    CHECK(feq(verts[0].v, 0.5f));

    // skin byte 128 -> 0 exactly (flt_628CC8 bias), 255 -> (255-128)*2/255 = 0.99608.
    const u8 skinBytes[3] = {128, 128, 255};
    float sn[3]; guild::render::UnpackSkinNormal(skinBytes, sn);
    CHECK(feq(sn[0], 0.0f)); CHECK(feq(sn[1], 0.0f));
    CHECK(feq(sn[2], (255.0f - 128.0f) * (2.0f / 255.0f)));
}

// =============================================================================
// WAVE-10 HARDENING — degenerate / edge coverage (ASAN+UBSAN).
// =============================================================================

// (W10-a) ZERO REFLECTION VECTOR: pos==0 (R = 0 - 0 = 0) -> VectorNormalize collapses
// R to (0,0,0) (zero-length -> 0), so uv = (0.5, 0.5). No NaN, no OOB. Drives the
// walk (and the kernel directly) at the degenerate reflection point.
TEST(EnvMapWalk, ZeroReflectionVector) {
    Vertex verts[1] = {Vp(0, 0, 0)};   // pos == origin -> R == 0
    float normals[3] = {0, 0, 1};
    EnvMapWalkInputs in; in.m3x3 = kIdentity3x3; in.vertexNormals = normals;
    CHECK_EQ(ComputeEnvMapVertexUvs(verts, 1, in), 1);
    CHECK(feq(verts[0].u, 0.5f));
    CHECK(feq(verts[0].v, 0.5f));
    // Cross-check the kernel directly with a zero normal too (Nt == 0 -> R == pos).
    float uv[2]; const float pos[3] = {0, 0, 0}, zn[3] = {0, 0, 0};
    guild::render::ComputeEnvMapReflectionUv(pos, zn, kIdentity3x3, uv);
    CHECK(feq(uv[0], 0.5f)); CHECK(feq(uv[1], 0.5f));
}

// (W10-b) DEGENERATE / ZERO bone 3x3: Nt collapses to (0,0,0) -> R == pos -> normal
// env map of the position. With a zero matrix every vertex still produces a finite UV
// (no divide-by-zero in the kernel; VectorNormalize handles a zero R).
TEST(EnvMapWalk, ZeroMatrixProducesFiniteUv) {
    const float zeroMat[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    Vertex verts[2] = {Vp(1, 2, 3), Vp(0, 0, 0)};
    float normals[6] = {1, 0, 0,   0, 1, 0};
    EnvMapWalkInputs in; in.m3x3 = zeroMat; in.vertexNormals = normals;
    CHECK_EQ(ComputeEnvMapVertexUvs(verts, 2, in), 2);
    // v0: Nt=0 -> R=pos=(1,2,3) normalized -> u=0.5*x+0.5, v=0.5+0.5*y, all finite.
    CHECK(verts[0].u == verts[0].u);    // not NaN
    CHECK(verts[0].v == verts[0].v);
    // v1: pos 0 -> R 0 -> uv (0.5,0.5).
    CHECK(feq(verts[1].u, 0.5f)); CHECK(feq(verts[1].v, 0.5f));
}

// (W10-c) Large/maxed walk under ASAN: every per-vertex normal (skinned + non-skinned)
// is sourced in-bounds; the gate array is read in-bounds; no buffer is over-read.
TEST(EnvMapWalk, LargeWalkInBounds) {
    const int N = 257;   // odd, > a cache line, exercises the i*3 striding fully
    std::vector<Vertex> verts(N);
    std::vector<float> normals((std::size_t)N * 3);
    std::vector<u8> skin((std::size_t)N * 3);
    std::vector<u8> flags(N);
    for (int i = 0; i < N; ++i) {
        verts[i] = Vp((float)i, (float)(i % 7), (float)(i % 3));
        normals[(std::size_t)i*3+0] = 0; normals[(std::size_t)i*3+1] = 0;
        normals[(std::size_t)i*3+2] = 1;
        skin[(std::size_t)i*3+0] = 128; skin[(std::size_t)i*3+1] = 128;
        skin[(std::size_t)i*3+2] = (u8)(i & 0xFF);
        flags[i] = (u8)(i & 1);          // only odd vertices written
    }
    // Non-skinned, full walk (no gate): every vertex written.
    EnvMapWalkInputs ns; ns.m3x3 = kIdentity3x3; ns.vertexNormals = normals.data();
    CHECK_EQ(ComputeEnvMapVertexUvs(verts.data(), N, ns), N);
    // Skinned + gate: only the flagged half written, the skin buffer read to its end.
    EnvMapWalkInputs sk; sk.m3x3 = kIdentity3x3; sk.skinNormalBytes = skin.data();
    sk.reflectiveFlags = flags.data();
    int n = ComputeEnvMapVertexUvs(verts.data(), N, sk);
    CHECK_EQ(n, N / 2);                   // floor(257/2) == 128 odd indices
}

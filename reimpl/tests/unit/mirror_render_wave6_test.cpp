// Golden-vector + render unit tests for the MIRROR / REFLECTION render pipeline
// (wave-6). Vectors derived directly from the gilde.exe decompile (constants,
// control flow, FPU evaluation order). Self-contained.
//
//   0x5F6148  VIBE_Mirror_ClipPolygonToPlanes      (reflect + 8-corner clip-cull)
//   0x5F6084  VIBE_Mirror_ProjectReflectedVertices (reflect + perspective project)
//   0x5F637C  VIBE_Mirror_BuildMirroredGeometry    (reflected-poly draw-list append)
//   0x5F5740  VIBE_Mirror_BuildSilhouettePoints    (silhouette-edge extractor)
//
// Provenance constants verified via get_bytes:
//   flt_62C39C / flt_62C3A0 = 0x40000000 = 2.0f  (reflection scale)
//   dbl_62C2F0 = 0xBF847AE147AE147B = -0.01       (silhouette tolerance)
//   flt_5CA2E0 = {0,0,0}                           (origin ref for TriangleNormal)
//   near gate  = 869711765 = 0x33D6E555
#include "tests/framework/test.h"
#include "render/mirror.h"
#include "render/mirror_project.h"
#include "render/mirror_silhouette.h"
#include "render/geometry_types.h"
#include "render/mesh.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {
bool feq(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

// Reference reflection (the engine math): t = -(P.n - d)*2 ; P' = P + t*n.
void refReflect(const float P[3], const MirrorPlane& m, float out[3]) {
    float t = -(P[0] * m.nx + P[1] * m.ny + P[2] * m.nz - m.d) * 2.0f;
    out[0] = t * m.nx + P[0];
    out[1] = t * m.ny + P[1];
    out[2] = t * m.nz + P[2];
}
} // namespace

// ===========================================================================
// ReflectPointAcrossPlane (0x5F6148 inner reflect) — mirroring across the XY,
// XZ and an oblique plane reproduces the standard reflection.
// ===========================================================================
TEST(MirrorReflect, AcrossGroundPlaneFlipsY) {
    // Plane y = 0 : n = (0,1,0), d = 0. Reflection negates y only.
    MirrorPlane m{0.0f, 1.0f, 0.0f, 0.0f};
    float P[3] = {3.0f, 5.0f, -7.0f};
    float out[3];
    ReflectPointAcrossPlane(P, m, out);
    CHECK(feq(out[0], 3.0f));
    CHECK(feq(out[1], -5.0f));
    CHECK(feq(out[2], -7.0f));
}

TEST(MirrorReflect, AcrossOffsetPlaneUsesD) {
    // Plane y = 2 : n = (0,1,0), d = 2.  P=(0,5,0) -> reflected to y = 2*2-5 = -1.
    MirrorPlane m{0.0f, 1.0f, 0.0f, 2.0f};
    float P[3] = {0.0f, 5.0f, 0.0f};
    float out[3];
    ReflectPointAcrossPlane(P, m, out);
    CHECK(feq(out[1], -1.0f));
}

TEST(MirrorReflect, ObliquePlaneMatchesReference) {
    // Unit normal (1,1,0)/sqrt2, d = 1.0 (plane offset along the diagonal).
    float inv = 1.0f / std::sqrt(2.0f);
    MirrorPlane m{inv, inv, 0.0f, 1.0f};
    float P[3] = {2.0f, -1.0f, 4.0f};
    float out[3], ref[3];
    ReflectPointAcrossPlane(P, m, out);
    refReflect(P, m, ref);
    CHECK(feq(out[0], ref[0]));
    CHECK(feq(out[1], ref[1]));
    CHECK(feq(out[2], ref[2]));
    // A point on the plane is its own reflection (P.n - d == 0).
    float onPlane[3] = {inv, inv, 9.0f};   // (1/sqrt2 + 1/sqrt2)*inv = 1 = d
    float r2[3];
    ReflectPointAcrossPlane(onPlane, m, r2);
    CHECK(feq(r2[0], onPlane[0]));
    CHECK(feq(r2[1], onPlane[1]));
    CHECK(feq(r2[2], onPlane[2]));
}

// ===========================================================================
// ClipReflectedBox (0x5F6148 box clip-cull) — the 8 reflected corners are tested
// against the mirror clip planes; a plane with all 8 corners outside culls.
// Buffer corner stride in the engine is 4 floats (v18[i], i+=4).
// ===========================================================================
TEST(MirrorClip, AllInsideExpandsDepthBounds) {
    // 8 corners of a unit-ish box, stride 4 floats (x,y,z,pad).
    float box[8 * 4];
    int idx = 0;
    for (int x = 0; x < 2; ++x)
        for (int y = 0; y < 2; ++y)
            for (int z = 0; z < 2; ++z) {
                box[idx * 4 + 0] = (float)x;
                box[idx * 4 + 1] = (float)y;
                box[idx * 4 + 2] = (float)z + 10.0f;  // z in [10,11]
                box[idx * 4 + 3] = 0.0f;
                ++idx;
            }
    // One clip plane: inside when z >= 5 (n=(0,0,1), d=5). All corners inside.
    MirrorClipPlane planes[1] = {{0.0f, 0.0f, 1.0f, 5.0f}};
    float nearB = 1.0e10f, farB = 0.0f;
    bool keep = ClipReflectedBox(box, 4, planes, 1, &nearB, &farB);
    CHECK(keep);
    CHECK(feq(nearB, 10.0f));   // min z over corners
    CHECK(feq(farB, 11.0f));    // max z over corners
}

TEST(MirrorClip, AllOutsideOnePlaneCulls) {
    float box[8 * 4];
    for (int i = 0; i < 8; ++i) {
        box[i * 4 + 0] = 0.0f;
        box[i * 4 + 1] = 0.0f;
        box[i * 4 + 2] = 1.0f;   // z = 1, behind the plane z >= 5
        box[i * 4 + 3] = 0.0f;
    }
    MirrorClipPlane planes[1] = {{0.0f, 0.0f, 1.0f, 5.0f}};
    float nearB = 1.0e10f, farB = 0.0f;
    bool keep = ClipReflectedBox(box, 4, planes, 1, &nearB, &farB);
    CHECK(!keep);   // all 8 outside this plane -> fully culled
}

TEST(MirrorClip, MixedCornersKeepWhenAnyInside) {
    // 4 corners inside (z=10), 4 outside (z=1) for a z>=5 plane: NOT culled
    // because at least one corner is inside (engine breaks on first inside).
    float box[8 * 4] = {0};
    for (int i = 0; i < 8; ++i) box[i * 4 + 2] = (i < 4) ? 1.0f : 10.0f;
    MirrorClipPlane planes[1] = {{0.0f, 0.0f, 1.0f, 5.0f}};
    bool keep = ClipReflectedBox(box, 4, planes, 1, nullptr, nullptr);
    CHECK(keep);
}

// ===========================================================================
// ReflectAndProjectVertices (0x5F6084) — reflect in place then perspective
// project, in the engine's exact field order. Stride is 20 floats.
// ===========================================================================
TEST(MirrorProject, ReflectInPlaceAndProject) {
    MirrorPlane plane{0.0f, 1.0f, 0.0f, 0.0f};   // y = 0
    ProjectionParams proj{2.0f, 3.0f, 100.0f, 50.0f};  // projX,projY,offX,offY

    MirrorVertex v{};
    v.x = 4.0f; v.y = 6.0f; v.z = 8.0f;
    MirrorVertex saved = v;

    ReflectAndProjectVertices(&v, 1, plane, proj);

    // Reflection: y negated, x/z unchanged (plane y=0). t = -(-... )
    float t = -(saved.x * plane.nx + saved.y * plane.ny + saved.z * plane.nz
                - plane.d) * 2.0f;   // = -2*6 = -12
    CHECK(feq(v.t, t));
    CHECK(feq(v.t, -12.0f));
    CHECK(feq(v.x, 4.0f));
    CHECK(feq(v.y, -6.0f));
    CHECK(feq(v.z, 8.0f));
    // Projection: screenX = (projX*x')*(1/z') + offX ; screenY = (1/z')*(projY*y') + offY
    float invZ = 1.0f / v.z;
    CHECK(feq(v.screenX, (proj.projX * v.x) * invZ + proj.offX));
    CHECK(feq(v.screenY, invZ * (proj.projY * v.y) + proj.offY));
}

TEST(MirrorProject, MultiVertexStrideAdvances) {
    MirrorPlane plane{0.0f, 1.0f, 0.0f, 0.0f};
    ProjectionParams proj{1.0f, 1.0f, 0.0f, 0.0f};
    MirrorVertex vs[3]{};
    vs[0].x = 1; vs[0].y = 2; vs[0].z = 4;
    vs[1].x = 5; vs[1].y = 6; vs[1].z = 8;
    vs[2].x = 9; vs[2].y = 10; vs[2].z = 16;
    ReflectAndProjectVertices(vs, 3, plane, proj);
    CHECK(feq(vs[0].y, -2.0f));
    CHECK(feq(vs[1].y, -6.0f));
    CHECK(feq(vs[2].y, -10.0f));
    CHECK(feq(vs[1].screenX, (5.0f) * (1.0f / 8.0f)));
}

// ===========================================================================
// AppendMirroredPolys (0x5F637C LABEL_22) — append reflected polys: skip mirror
// surface, require a near vertex, require front-facing-after-reflection (or
// no-cull bit 0x4). Winding flipped: (x0-x2)(y0-y1) > (x0-x1)(y0-y2).
// ===========================================================================
TEST(MirrorAppend, FrontFacingReflectedPolyAppended) {
    // Build a single poly whose reflected screen winding satisfies the flipped
    // area test. Use a CW (in normal terms) triangle so the flipped test passes.
    Vertex a{}, b{}, c{};
    a.screenX = 0; a.screenY = 0;
    b.screenX = 0; b.screenY = 4;   // (x0-x1)=0, (y0-y1)=-4
    c.screenX = 4; c.screenY = 0;   // (x0-x2)=-4, (y0-y2)=0
    // flipped test: (0-4)*(0-4) > (0-0)*(0-0) -> 16 > 0 -> true
    Polygon p{};
    p.v0 = &a; p.v1 = &b; p.v2 = &c; p.flags38 = 0;

    DrawListEntry entries[4]{};
    DrawList dl{entries, 0, 4};
    u32 texSort[1] = {7};       // sort id != mirror
    bool nearV[1] = {true};
    i32 n = AppendMirroredPolys(&p, 1, texSort, /*mirrorViewTexId*/99u, nearV, &dl);
    CHECK_EQ(n, 1);
    CHECK_EQ(dl.count, 1);
    CHECK_EQ(entries[0].sortKey, 7u);
    CHECK(entries[0].poly == &p);
}

TEST(MirrorAppend, SkipsMirrorSurfaceAndFarPolys) {
    Vertex a{}, b{}, c{};
    a.screenX = 0; a.screenY = 0; b.screenX = 0; b.screenY = 4; c.screenX = 4; c.screenY = 0;
    Polygon p{}; p.v0 = &a; p.v1 = &b; p.v2 = &c; p.flags38 = 0;
    DrawListEntry entries[4]{};

    // Case A: tex id matches mirror surface -> skipped.
    { DrawList dl{entries, 0, 4}; u32 t[1] = {99}; bool nv[1] = {true};
      CHECK_EQ(AppendMirroredPolys(&p, 1, t, 99u, nv, &dl), 0); }
    // Case B: no near vertex -> skipped.
    { DrawList dl{entries, 0, 4}; u32 t[1] = {7}; bool nv[1] = {false};
      CHECK_EQ(AppendMirroredPolys(&p, 1, t, 99u, nv, &dl), 0); }
}

TEST(MirrorAppend, NoCullBitBypassesWinding) {
    // Back-facing-after-reflection poly: flipped area test fails, but flags38&4
    // forces the append.
    Vertex a{}, b{}, c{};
    a.screenX = 0; a.screenY = 0; b.screenX = 4; b.screenY = 0; c.screenX = 0; c.screenY = 4;
    // flipped: (0-0)*(0-0)=0 > (0-4)*(0-4)=16 -> false
    Polygon p{}; p.v0 = &a; p.v1 = &b; p.v2 = &c; p.flags38 = 4;  // no-cull
    DrawListEntry entries[2]{}; DrawList dl{entries, 0, 2};
    u32 t[1] = {3}; bool nv[1] = {true};
    CHECK_EQ(AppendMirroredPolys(&p, 1, t, 0u, nv, &dl), 1);
}

TEST(MirrorAppend, HonorsRemainingCapacity) {
    Vertex a{}, b{}, c{};
    a.screenX = 0; a.screenY = 0; b.screenX = 0; b.screenY = 4; c.screenX = 4; c.screenY = 0;
    std::vector<Polygon> polys(5);
    for (auto& p : polys) { p.v0 = &a; p.v1 = &b; p.v2 = &c; p.flags38 = 0; }
    std::vector<u32> tex(5, 7);
    std::vector<bool> nearArr; // vector<bool> not contiguous; use array
    bool nv[5] = {true, true, true, true, true};
    DrawListEntry entries[8]{};
    DrawList dl{entries, 0, 3};  // capacity 3
    i32 n = AppendMirroredPolys(polys.data(), 5, tex.data(), 0u, nv, &dl);
    CHECK_EQ(n, 3);              // clamped to remaining capacity
    CHECK_EQ(dl.count, 3);
}

// ===========================================================================
// BuildSilhouettePoints (0x5F5740) — for a point cloud, an ordered pair (j,i)
// is a silhouette edge when no OTHER point lies on the negative side of the
// origin plane normal = TriangleNormal(0, pt[j], pt[i]); tol = -0.01.
// ===========================================================================
TEST(MirrorSilhouette, EmptySetReturnsZero) {
    i32 outCount = 99;
    bool ok = BuildSilhouettePoints(0, nullptr, &outCount, nullptr, nullptr);
    CHECK(ok);
    CHECK_EQ(outCount, 0);
}

TEST(MirrorSilhouette, TriangleEmitsEdgesAndStampsConnectivity) {
    // Three points in the z=1 plane forming a triangle around the origin's
    // projection. Each adjacent pair is a silhouette edge of the 2D hull.
    float p0[3] = {1.0f, 0.0f, 1.0f};
    float p1[3] = {-0.5f, 0.866f, 1.0f};
    float p2[3] = {-0.5f, -0.866f, 1.0f};
    float* pts[3] = {p0, p1, p2};
    std::vector<guild::u8> conn(3 * 3, 0);
    const float* outPairs[16] = {nullptr};
    i32 outCount = -1;

    bool ok = BuildSilhouettePoints(3, conn.data(), &outCount, pts, outPairs);
    CHECK(ok);
    // Each emitted edge writes 2 pointers; connectivity is symmetric so each
    // unordered edge is taken exactly once. A triangle hull -> 3 edges -> 6 ptrs.
    CHECK_EQ(outCount, 6);
    // Connectivity matrix symmetric and self-consistent.
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i)
            if (i != j)
                CHECK_EQ(conn[j * 3 + i], conn[i * 3 + j]);
    // Every emitted pointer is one of the three input points.
    for (int k = 0; k < outCount; ++k) {
        bool found = (outPairs[k] == p0 || outPairs[k] == p1 || outPairs[k] == p2);
        CHECK(found);
    }
}

// ===========================================================================
// ShouldRenderMirrorPass (0x5b3af0 predicate) — the 4-term AND gate that decides
// whether the reflection pass runs in BeginUniverseFrame.
// ===========================================================================
TEST(MirrorPass, GateRequiresAllConditions) {
    MirrorPassGate g{true, true, true, true, true};
    CHECK(ShouldRenderMirrorPass(g));

    // Dropping any single condition disables the pass.
    { auto x = g; x.featureEnabled = false;     CHECK(!ShouldRenderMirrorPass(x)); }
    { auto x = g; x.reflectionPrepared = false; CHECK(!ShouldRenderMirrorPass(x)); }
    { auto x = g; x.planeParamA = false;        CHECK(!ShouldRenderMirrorPass(x)); }
    { auto x = g; x.planeParamB = false;        CHECK(!ShouldRenderMirrorPass(x)); }
    { auto x = g; x.runtimeActive = false;      CHECK(!ShouldRenderMirrorPass(x)); }
}

// =============================================================================
// WAVE-10 HARDENING — degenerate / edge / capacity-boundary coverage (ASAN+UBSAN).
// =============================================================================

// (W10-a) DEGENERATE PLANE (zero normal) in the reflect: t = -(0 - d)*2 = 2d, and
// out = P + 2d*0 = P (unchanged). No NaN, no OOB — just the identity-ish reflection.
TEST(MirrorReflectHarden, ZeroNormalPlaneLeavesPointUnchanged) {
    MirrorPlane m{0.0f, 0.0f, 0.0f, 3.0f};   // degenerate: |n| == 0
    float P[3] = {2.0f, -5.0f, 7.0f};
    float out[3];
    ReflectPointAcrossPlane(P, m, out);
    CHECK(feq(out[0], 2.0f));
    CHECK(feq(out[1], -5.0f));
    CHECK(feq(out[2], 7.0f));
}

// (W10-b) ClipReflectedBox against a DEGENERATE (zero-normal) plane: n.P == 0 for
// every corner, so "inside" is 0 >= d. With d > 0 all 8 are outside -> culled; with
// d <= 0 all inside -> kept. No OOB indexing the corner buffer either way.
TEST(MirrorClipHarden, ZeroNormalPlane) {
    float box[8 * 4] = {0};
    for (int i = 0; i < 8; ++i) { box[i*4+0]=1; box[i*4+1]=2; box[i*4+2]=3+i; }
    MirrorClipPlane cull[1]  = {{0.0f, 0.0f, 0.0f,  1.0f}};   // 0 >= 1 false -> all out
    CHECK(!ClipReflectedBox(box, 4, cull, 1, nullptr, nullptr));
    MirrorClipPlane keep[1]  = {{0.0f, 0.0f, 0.0f, -1.0f}};   // 0 >= -1 true -> kept
    float nB = 1e10f, fB = -1e10f;
    CHECK(ClipReflectedBox(box, 4, keep, 1, &nB, &fB));
    CHECK(feq(nB, 3.0f)); CHECK(feq(fB, 10.0f));
}

// (W10-c) ClipReflectedBox with planeCount == 0: no plane culls, the depth-bound
// expansion still runs over all 8 corners; returns true. No read past planes[].
TEST(MirrorClipHarden, ZeroPlanesKeepsAndExpands) {
    float box[8 * 4] = {0};
    for (int i = 0; i < 8; ++i) box[i*4+2] = (float)(i + 1);   // z in [1,8]
    float nB = 1e10f, fB = -1e10f;
    CHECK(ClipReflectedBox(box, 4, nullptr, 0, &nB, &fB));
    CHECK(feq(nB, 1.0f)); CHECK(feq(fB, 8.0f));
}

// (W10-d) AppendMirroredPolys with an EXACTLY-FULL draw list (0 remaining capacity):
// nothing is appended, no write past entries[]. And polyCount == 0 is a clean no-op.
TEST(MirrorAppendHarden, ZeroCapacityAndZeroCount) {
    Vertex a{}, b{}, c{};
    a.screenX = 0; a.screenY = 0; b.screenX = 0; b.screenY = 4; c.screenX = 4; c.screenY = 0;
    Polygon p{}; p.v0 = &a; p.v1 = &b; p.v2 = &c; p.flags38 = 0;
    DrawListEntry entries[2]{};
    u32 t[1] = {7}; bool nv[1] = {true};
    // capacity == count == 2: remaining 0 -> append nothing (no entries[2] write).
    DrawList full{entries, 2, 2};
    CHECK_EQ(AppendMirroredPolys(&p, 1, t, 99u, nv, &full), 0);
    CHECK_EQ(full.count, 2);
    // polyCount == 0: loop never runs.
    DrawList dl{entries, 0, 2};
    CHECK_EQ(AppendMirroredPolys(&p, 0, t, 99u, nv, &dl), 0);
    CHECK_EQ(dl.count, 0);
}

// (W10-e) BuildSilhouettePoints with count == 1: the single point yields no edge
// (i==j skipped), outCount stays 0, conn/outPairs never written out of range.
TEST(MirrorSilhouetteHarden, SinglePointNoEdge) {
    float p0[3] = {1, 0, 1};
    float* pts[1] = {p0};
    guild::u8 conn[1] = {0};
    const float* outPairs[2] = {nullptr, nullptr};
    i32 outCount = -1;
    CHECK(BuildSilhouettePoints(1, conn, &outCount, pts, outPairs));
    CHECK_EQ(outCount, 0);
    CHECK_EQ((int)conn[0], 0);
}

// (W10-f) BuildSilhouettePoints — exact silhouette-buffer sizing. For `count` points
// the engine sizes outPairs at 8*count slots (the CreateOutline allocation). A convex
// fan of N points emits N hull edges == 2N pointers <= 8*count; this drives the writer
// to its real fill and confirms it stays within the documented capacity (ASAN-checked
// because outPairs is sized exactly 8*count here).
TEST(MirrorSilhouetteHarden, ConvexFanWithinPairBufferCapacity) {
    const int N = 6;
    // N points evenly around a circle in the z=1 plane -> the silhouette is the hull,
    // N edges, 2N pointers.
    std::vector<std::array<float,3>> pts3(N);
    std::vector<float*> pts(N);
    for (int i = 0; i < N; ++i) {
        float ang = 6.2831853f * (float)i / (float)N;
        pts3[i] = {std::cos(ang), std::sin(ang), 1.0f};
        pts[i] = pts3[i].data();
    }
    std::vector<guild::u8> conn((std::size_t)N * N, 0);
    std::vector<const float*> outPairs((std::size_t)8 * N, nullptr);   // engine capacity
    i32 outCount = -1;
    CHECK(BuildSilhouettePoints(N, conn.data(), &outCount, pts.data(), outPairs.data()));
    // Every emitted pointer index is within the 8*N capacity (no overflow).
    CHECK(outCount >= 0 && outCount <= 8 * N);
    // Connectivity stays symmetric.
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i)
            if (i != j) CHECK_EQ(conn[j*N+i], conn[i*N+j]);
}

TEST(MirrorSilhouette, AlreadyTakenEdgesSkipped) {
    // Pre-stamp conn so the (0,1) edge is considered taken: it must not re-emit.
    float p0[3] = {1.0f, 0.0f, 1.0f};
    float p1[3] = {-0.5f, 0.866f, 1.0f};
    float p2[3] = {-0.5f, -0.866f, 1.0f};
    float* pts[3] = {p0, p1, p2};
    std::vector<guild::u8> conn(3 * 3, 0);
    conn[0 * 3 + 1] = 1;
    conn[1 * 3 + 0] = 1;
    const float* outPairs[16] = {nullptr};
    i32 outCount = -1;
    BuildSilhouettePoints(3, conn.data(), &outCount, pts, outPairs);
    // One unordered edge pre-taken -> 2 remaining edges -> 4 pointers.
    CHECK_EQ(outCount, 4);
}

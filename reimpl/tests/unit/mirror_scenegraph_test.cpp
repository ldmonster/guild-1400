// Golden-vector + structural unit tests for the SCENE-GRAPH halves of the MIRROR
// reflection pass (wave-7) — the binder + clip-plane builder that wave-6 left as
// rule-8 hooks. Reconstructed 1:1 from the gilde.exe decompile:
//
//   0x5F676C  VIBE_Mirror_PrepareReflectionNode  (per-reflective-node binder)
//   0x5F5D08  VIBE_Mirror_CreateClippingPlanes   (mirror-surface -> clip planes)
//   0x5F58FC  VIBE_Mirror_CreateOutline          (silhouette-edge outline tracer)
//   0x5F5740  VIBE_Mirror_BuildSilhouettePoints  (already in wave-6; reused here)
//
// Provenance constants verified via get_bytes:
//   flt_5CA2E0 = {0,0,0}                  (origin reference for TriangleNormal)
//   dbl_62C2F0 = 0xBF847AE147AE147B = -0.01 (silhouette plane tolerance)
//   dword_13DB398 = all-zero at static analysis (RUNTIME frustum table -> hook,
//                   default 0 extra planes; rule-8)
//   VIBE_Memory_AllocDebug (0x438f10) ZERO-FILLS allocations (memset(p,0,n)).
//
// The clip-plane equation (per outline edge a->b, byte-for-byte):
//   n = normalize( TriangleNormal(origin, a, b) )   (== (a-o)x(b-o) normalized)
//   d = -(n . a)
#include "tests/framework/test.h"
#include "render/mirror_project.h"
#include "render/mirror_silhouette.h"
#include "util/math.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {
bool feq(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

// Reference plane for an edge (a,b): the engine's TriangleNormal(origin,a,b) and
// d = -(n . a). TriangleNormal in util computes normalize((a-o)x(b-o)).
void refEdgePlane(const float* a, const float* b, float outN[3], float* outD) {
    float origin[3] = {0, 0, 0};
    util::TriangleNormal(origin, (float*)a, (float*)b, outN);
    *outD = -(outN[0] * a[0] + outN[1] * a[1] + outN[2] * a[2]);
}
} // namespace

// ===========================================================================
// CreateOutline (0x5F58FC) — chains the silhouette edges of a point set into one
// ordered closed boundary loop. A triangle's 3 points -> 3 edges (6 pointers).
// ===========================================================================
TEST(MirrorOutline, TriangleClosesIntoThreeEdges) {
    float P0[3] = { 1, 1, 5};
    float P1[3] = {-1, 1, 5};
    float P2[3] = { 0,-1, 5};
    float* pts[3] = {P0, P1, P2};

    const float** outline = nullptr;
    u32 n = 0;
    bool ok = CreateOutline(pts, 3, &outline, &n);
    CHECK(ok);
    CHECK_EQ(n, 6u);                 // 3 edges * 2 pointers

    // Each emitted pointer must be one of the 3 input points (a closed boundary).
    int seen0 = 0, seen1 = 0, seen2 = 0;
    for (u32 i = 0; i < n; ++i) {
        if (outline[i] == P0) ++seen0;
        else if (outline[i] == P1) ++seen1;
        else if (outline[i] == P2) ++seen2;
        else CHECK(false);
    }
    // Each vertex appears exactly twice (as the end of one edge, start of the next).
    CHECK_EQ(seen0, 2);
    CHECK_EQ(seen1, 2);
    CHECK_EQ(seen2, 2);

    // Edges chain head-to-tail (b of edge k == a of edge k+1, cyclically).
    for (u32 e = 0; e < n / 2; ++e) {
        const float* b = outline[2 * e + 1];
        const float* aNext = outline[(2 * e + 2) % n];
        CHECK(b == aNext);
    }
    MirrorFree(outline);
}

// Fewer than 3 points -> no outline (the v4 < 3 early-out).
TEST(MirrorOutline, RejectsDegenerateCount) {
    float P0[3] = {1, 0, 1};
    float P1[3] = {0, 1, 1};
    float* pts[2] = {P0, P1};
    const float** outline = (const float**)0xdeadbeef;
    u32 n = 99;
    bool ok = CreateOutline(pts, 2, &outline, &n);
    CHECK(!ok);
    // On the reject path the original writes nothing; our locals are untouched.
    CHECK_EQ(n, 99u);
}

// ===========================================================================
// CreateClippingPlanes (0x5F5D08) — full path: collect mirror polys, dedup
// points, trace the outline, derive one clip plane per edge. The triangle mirror
// surface yields exactly 3 planes; each matches TriangleNormal/d byte-for-byte.
// ===========================================================================
TEST(MirrorClipPlanes, TriangleSurfaceYieldsThreeEdgePlanes) {
    float v0[3] = { 1, 1, 5};
    float v1[3] = {-1, 1, 5};
    float v2[3] = { 0,-1, 5};
    MirrorPoly poly{};
    poly.v[0] = v0; poly.v[1] = v1; poly.v[2] = v2;
    poly.surfaceKey = 7;
    poly.signByte = -1;              // back-facing (i8 < 0) -> collected

    MirrorPoly polys[1] = {poly};
    MirrorMeshBlock mesh{};
    mesh.polys = polys;
    mesh.polyCount = 1;

    MirrorClipPlaneList* list = CreateClippingPlanes(&mesh, 7);
    CHECK(list != nullptr);
    CHECK_EQ(list->count(), 3);     // 3 outline edges, 0 frustum planes (default)
    CHECK_EQ((int)list->flag(), 1); // *(buf+4) = 1

    // Each output plane equals the edge-plane of SOME outline edge of the triangle.
    // Verify by matching each produced plane against the 3 candidate edge planes.
    const float* P[3] = {v0, v1, v2};
    bool matched[3] = {false, false, false};
    for (int i = 0; i < list->count(); ++i) {
        MirrorClipPlaneOut got = list->planes()[i];
        bool any = false;
        for (int e = 0; e < 3 && !any; ++e) {
            const float* a = P[e];
            const float* b = P[(e + 1) % 3];
            float n[3], d;
            refEdgePlane(a, b, n, &d);
            if (feq(got.nx, n[0]) && feq(got.ny, n[1]) && feq(got.nz, n[2]) &&
                feq(got.d, d)) {
                matched[e] = true; any = true;
            }
            // also the reverse orientation (b->a)
            refEdgePlane(b, a, n, &d);
            if (!any && feq(got.nx, n[0]) && feq(got.ny, n[1]) &&
                feq(got.nz, n[2]) && feq(got.d, d)) {
                matched[e] = true; any = true;
            }
        }
        CHECK(any);
    }
    CHECK(matched[0] && matched[1] && matched[2]);
    MirrorFree(list);
}

// No mirror surface (key mismatch) -> null (the v8 == 0 early-out).
TEST(MirrorClipPlanes, NoSurfaceReturnsNull) {
    float v0[3] = {1, 1, 5}, v1[3] = {-1, 1, 5}, v2[3] = {0, -1, 5};
    MirrorPoly poly{};
    poly.v[0] = v0; poly.v[1] = v1; poly.v[2] = v2;
    poly.surfaceKey = 7; poly.signByte = -1;
    MirrorPoly polys[1] = {poly};
    MirrorMeshBlock mesh{};
    mesh.polys = polys; mesh.polyCount = 1;

    CHECK(CreateClippingPlanes(&mesh, 999) == nullptr);   // no poly[20]==999
    CHECK(CreateClippingPlanes(&mesh, 0) == nullptr);     // !a2 guard
    CHECK(CreateClippingPlanes(nullptr, 7) == nullptr);   // !a1 guard
}

// Front-facing surface polys (sign byte >= 0) are NOT collected -> degenerate
// (< 3 unique points) -> null. (Pass-2 filter `(i8)poly[36] < 0`.)
TEST(MirrorClipPlanes, FrontFacingPolysAreNotCollected) {
    float v0[3] = {1, 1, 5}, v1[3] = {-1, 1, 5}, v2[3] = {0, -1, 5};
    MirrorPoly poly{};
    poly.v[0] = v0; poly.v[1] = v1; poly.v[2] = v2;
    poly.surfaceKey = 7; poly.signByte = 0;       // front-facing -> skipped in pass 2
    MirrorPoly polys[1] = {poly};
    MirrorMeshBlock mesh{};
    mesh.polys = polys; mesh.polyCount = 1;
    CHECK(CreateClippingPlanes(&mesh, 7) == nullptr);
}

// ===========================================================================
// Frustum-table hook (dword_13DB398, rule-8) — when the runtime table supplies N
// extra planes for the orientation code, they are APPENDED verbatim after the
// outline edge planes and counted into the total.
// ===========================================================================
namespace {
MirrorClipPlaneOut g_frustumPlanes[2] = {
    {0.5f, 0.5f, 0.5f, -1.0f},
    {0.0f, 1.0f, 0.0f, -2.0f},
};
MirrorFrustumPlanes FrustumHook(u32 /*code*/) {
    return MirrorFrustumPlanes{2, g_frustumPlanes};
}
} // namespace

TEST(MirrorClipPlanes, FrustumPlanesAppendedAfterOutline) {
    SetMirrorFrustumTableHook(&FrustumHook);

    float v0[3] = {1, 1, 5}, v1[3] = {-1, 1, 5}, v2[3] = {0, -1, 5};
    MirrorPoly poly{};
    poly.v[0] = v0; poly.v[1] = v1; poly.v[2] = v2;
    poly.surfaceKey = 3; poly.signByte = -1;
    MirrorPoly polys[1] = {poly};
    MirrorMeshBlock mesh{};
    mesh.polys = polys; mesh.polyCount = 1;

    MirrorClipPlaneList* list = CreateClippingPlanes(&mesh, 3);
    CHECK(list != nullptr);
    CHECK_EQ(list->count(), 3 + 2);          // 3 outline + 2 frustum

    // The last two planes are the frustum planes copied verbatim.
    MirrorClipPlaneOut f0 = list->planes()[3];
    MirrorClipPlaneOut f1 = list->planes()[4];
    CHECK(feq(f0.nx, 0.5f) && feq(f0.ny, 0.5f) && feq(f0.nz, 0.5f) && feq(f0.d, -1.0f));
    CHECK(feq(f1.nx, 0.0f) && feq(f1.ny, 1.0f) && feq(f1.nz, 0.0f) && feq(f1.d, -2.0f));
    MirrorFree(list);

    SetMirrorFrustumTableHook(nullptr);      // restore default (0 frustum planes)
}

// ===========================================================================
// Allocator hook — CreateClippingPlanes/CreateOutline route every buffer through
// the hook (the 0x438f10 / 0x43923c seam). Verify alloc/free are balanced and
// the default zero-fills (matching VIBE_Memory_AllocDebug's memset).
// ===========================================================================
namespace {
int g_allocs = 0, g_frees = 0;
void* CountingAlloc(u32 n, const char*) {
    ++g_allocs;
    void* p = std::calloc(n ? n : 1u, 1u);   // zero-fill like AllocDebug
    return p;
}
void CountingFree(void* p) { ++g_frees; std::free(p); }
} // namespace

TEST(MirrorClipPlanes, AllocFreeBalancedViaHook) {
    g_allocs = g_frees = 0;
    SetMirrorAllocHook(&CountingAlloc);
    SetMirrorFreeHook(&CountingFree);

    float v0[3] = {1, 1, 5}, v1[3] = {-1, 1, 5}, v2[3] = {0, -1, 5};
    MirrorPoly poly{};
    poly.v[0] = v0; poly.v[1] = v1; poly.v[2] = v2;
    poly.surfaceKey = 5; poly.signByte = -1;
    MirrorPoly polys[1] = {poly};
    MirrorMeshBlock mesh{};
    mesh.polys = polys; mesh.polyCount = 1;

    MirrorClipPlaneList* list = CreateClippingPlanes(&mesh, 5);
    CHECK(list != nullptr);
    // Every internal scratch buffer is freed; only the returned plane list remains.
    CHECK_EQ(g_frees, g_allocs - 1);
    MirrorFree(list);
    CHECK_EQ(g_frees, g_allocs);

    SetMirrorAllocHook(nullptr);
    SetMirrorFreeHook(nullptr);
}

// ===========================================================================
// PrepareReflectionNode (0x5F676C) — binds the reflective child into the draw
// context, computes the mirror plane, builds the clip planes and publishes the
// node as the prepared reflection node (dword_649D6C).
// ===========================================================================
TEST(MirrorPrepareNode, BindsReflectiveChildAndPublishes) {
    // The reflective surface: a triangle whose poly[20] key == the child id and
    // sign byte < 0. The child is a 128-byte record with byte[104] bit5 (0x20) set.
    float v0[3] = {1, 1, 5}, v1[3] = {-1, 1, 5}, v2[3] = {0, -1, 5};

    // Use the child's address as the surface key (the engine matches poly[20]
    // against the child pointer); store that same value in poly.surfaceKey.
    std::vector<u8> child(128, 0);
    child[104] = 0x20;                         // reflective bit
    void* childPtr = child.data();

    MirrorPoly poly{};
    poly.v[0] = v0; poly.v[1] = v1; poly.v[2] = v2;
    // The engine matches the reflective child against poly+20 (surfaceKey); the
    // u32 view uses the child pointer (truncated) as that key.
    poly.surfaceKey = (u32)(uintptr_t)childPtr;
    poly.signByte = -1;
    MirrorPoly polys[1] = {poly};

    // parent record: child count at +480 (index 120) = 1.
    std::vector<u8> parent(512, 0);
    *(u32*)(parent.data() + 480) = 1;

    void* children[1] = {childPtr};
    MirrorMeshBlock mesh{};
    mesh.polys = polys; mesh.polyCount = 1;
    mesh.parent = parent.data();
    mesh.childArray = children;

    ReflectionNode node{};
    node.mesh = &mesh;
    node.activeFlag = 0x08;                    // bit3 set -> (16*b)>>7 == 1 -> rebuild

    ReflectionBindContext ctx{};
    void* prepared = nullptr;

    void* r = PrepareReflectionNode(&node, &ctx, &prepared, nullptr, nullptr);

    // Published: the global now points at the node and the call returned it.
    CHECK(prepared == &node);
    CHECK(r == &node);
    // The draw context was bound: project callback installed, clip planes built.
    CHECK(ctx.projectCallback != nullptr);
    CHECK(ctx.clipPlanes != nullptr);
    CHECK_EQ(ctx.clipPlanes->count(), 3);      // the triangle's 3 edge planes
    CHECK((ctx.flags & 0x01) != 0);            // valid-surface bit

    MirrorFree(ctx.clipPlanes);
}

// gilde.exe 0x5f688a..0x5f68c8 — the mirror PLANE derivation. The engine rotates
// the bound poly's STORED normal by the camera frame (RotateVectorWithFrame) into
// ctx+24..32, then d = n . firstVertexPos at ctx+36. With no camera node the
// rotation is identity (default hook), so the plane normal == the poly's stored
// normal and d == storedNormal . v0. Pins the FIX (was a fabricated TriangleNormal).
TEST(MirrorPrepareNode, PlaneFromStoredNormalAndFirstVertex) {
    SetMirrorRotateNormalHook(nullptr);   // identity default (dword_13FCD1C == 0)

    float v0[3] = {2, 3, 5}, v1[3] = {-1, 1, 5}, v2[3] = {0, -1, 5};
    std::vector<u8> child(128, 0);
    child[104] = 0x20;
    void* childPtr = child.data();

    MirrorPoly poly{};
    poly.v[0] = v0; poly.v[1] = v1; poly.v[2] = v2;
    poly.surfaceKey = (u32)(uintptr_t)childPtr;
    poly.signByte = -1;
    poly.normal[0] = 0.0f; poly.normal[1] = 0.0f; poly.normal[2] = 1.0f; // stored n
    MirrorPoly polys[1] = {poly};

    std::vector<u8> parent(512, 0);
    *(u32*)(parent.data() + 480) = 1;
    void* children[1] = {childPtr};
    MirrorMeshBlock mesh{};
    mesh.polys = polys; mesh.polyCount = 1;
    mesh.parent = parent.data(); mesh.childArray = children;

    ReflectionNode node{};
    node.mesh = &mesh; node.activeFlag = 0x08;
    ReflectionBindContext ctx{};
    void* prepared = nullptr;

    PrepareReflectionNode(&node, &ctx, &prepared, nullptr, nullptr);

    // n == stored normal (identity rotation).
    CHECK_EQ(ctx.planeNormal[0], 0.0f);
    CHECK_EQ(ctx.planeNormal[1], 0.0f);
    CHECK_EQ(ctx.planeNormal[2], 1.0f);
    // d = n . v0 = 0*2 + 0*3 + 1*5 = 5.
    CHECK_EQ(ctx.planeD, 5.0f);

    if (ctx.clipPlanes) MirrorFree(ctx.clipPlanes);
}

// Early-out: when a reflection node is already published (dword_649D6C != 0), the
// binder returns immediately without touching the context.
TEST(MirrorPrepareNode, EarlyOutWhenAlreadyPrepared) {
    ReflectionNode node{};
    ReflectionBindContext ctx{};
    int sentinel = 0;
    void* prepared = &sentinel;               // already set

    void* r = PrepareReflectionNode(&node, &ctx, &prepared, nullptr, nullptr);
    CHECK(r == &node);
    CHECK(prepared == &sentinel);             // unchanged
    CHECK(ctx.clipPlanes == nullptr);         // context untouched
    CHECK(ctx.projectCallback == nullptr);
}

// =============================================================================
// WAVE-10 HARDENING — degenerate / edge / capacity-boundary coverage (ASAN+UBSAN).
// =============================================================================

// (W10-a) CreateOutline 0-outline boundary: count == 0 / 1 / 2 all reject (< 3) and
// write nothing (the v4 < 3 early-out). No allocation, no OOB.
TEST(MirrorOutlineHarden, ZeroOneTwoPointReject) {
    float P0[3] = {1, 0, 1}, P1[3] = {0, 1, 1};
    float* pts[2] = {P0, P1};
    const float** outline = (const float**)0x1234;
    u32 n = 7;
    CHECK(!CreateOutline(pts, 0, &outline, &n));
    CHECK(!CreateOutline(pts, 1, &outline, &n));
    CHECK(!CreateOutline(pts, 2, &outline, &n));
    CHECK_EQ(n, 7u);                          // untouched on the reject path
}

// (W10-b) CreateOutline COLLINEAR points (degenerate hull): 3 collinear points have
// no enclosing silhouette loop. The tracer must still terminate, allocate within the
// 8*count pair buffer and the pairPtrCount output buffer, and free both (ASAN catches
// any over-write / leak). The result is a valid (possibly empty/short) outline.
TEST(MirrorOutlineHarden, CollinearPointsTerminate) {
    float P0[3] = {0, 0, 1}, P1[3] = {1, 0, 1}, P2[3] = {2, 0, 1};   // collinear
    float* pts[3] = {P0, P1, P2};
    const float** outline = nullptr;
    u32 n = 0;
    bool ok = CreateOutline(pts, 3, &outline, &n);
    CHECK(ok);                                // returns true for count >= 3
    CHECK((n % 2u) == 0u);                    // always an even pointer count
    if (outline) MirrorFree(outline);         // no leak (ASAN/LSAN)
}

// (W10-c) CreateOutline larger (max-ish) convex set: N points on a circle -> N-edge
// boundary loop, every emitted pointer one of the inputs, alloc/free balanced. Drives
// the chain tracer's full fill against the 8*count / pairPtrCount buffers.
TEST(MirrorOutlineHarden, ConvexManyPoints) {
    const int N = 12;
    std::vector<std::array<float,3>> p3(N);
    std::vector<float*> pts(N);
    for (int i = 0; i < N; ++i) {
        float a = 6.2831853f * (float)i / (float)N;
        p3[i] = {std::cos(a), std::sin(a), 1.0f};
        pts[i] = p3[i].data();
    }
    const float** outline = nullptr; u32 n = 0;
    CHECK(CreateOutline(pts.data(), N, &outline, &n));
    CHECK((n % 2u) == 0u);
    CHECK(n <= (u32)(8 * N));                  // within the pair-buffer capacity
    for (u32 i = 0; i < n; ++i) {
        bool found = false;
        for (int k = 0; k < N && !found; ++k) found = (outline[i] == pts[k]);
        CHECK(found);
    }
    MirrorFree(outline);
}

// (W10-d) CreateClippingPlanes capacity boundary — the final buffer is sized
// 16*total + 8 with `total = edgeCount + frustumCount`; a frustum hook that appends
// the MAXIMUM extra planes must land exactly at the buffer end (the memcpy writes
// 16*frustumCount at rec+edgeCount, ending at buf+8+16*total). ASAN verifies the
// write does not run past the allocation.
namespace {
MirrorClipPlaneOut g_maxFrustum[8];
MirrorFrustumPlanes MaxFrustumHook(u32) { return MirrorFrustumPlanes{8, g_maxFrustum}; }
} // namespace
TEST(MirrorClipPlanesHarden, FrustumCapacityBoundary) {
    for (int i = 0; i < 8; ++i)
        g_maxFrustum[i] = MirrorClipPlaneOut{(float)i, (float)-i, 1.0f, (float)(i*2)};
    SetMirrorFrustumTableHook(&MaxFrustumHook);

    float v0[3] = {1, 1, 5}, v1[3] = {-1, 1, 5}, v2[3] = {0, -1, 5};
    MirrorPoly poly{}; poly.v[0] = v0; poly.v[1] = v1; poly.v[2] = v2;
    poly.surfaceKey = 11; poly.signByte = -1;
    MirrorPoly polys[1] = {poly};
    MirrorMeshBlock mesh{}; mesh.polys = polys; mesh.polyCount = 1;

    MirrorClipPlaneList* list = CreateClippingPlanes(&mesh, 11);
    CHECK(list != nullptr);
    CHECK_EQ(list->count(), 3 + 8);           // 3 outline + 8 frustum (the max)
    // The trailing 8 planes are the frustum planes copied verbatim, exactly at the
    // tail of the allocation (the last one fully in-bounds).
    for (int i = 0; i < 8; ++i) {
        MirrorClipPlaneOut g = list->planes()[3 + i];
        CHECK(feq(g.nx, (float)i)); CHECK(feq(g.ny, (float)-i));
        CHECK(feq(g.nz, 1.0f));     CHECK(feq(g.d, (float)(i*2)));
    }
    MirrorFree(list);
    SetMirrorFrustumTableHook(nullptr);
}

// (W10-e) CreateClippingPlanes with a 0-poly / collinear mirror surface: a single
// surface poly of collinear vertices dedups to < 3 unique points (or a degenerate
// outline) and returns null with every scratch buffer freed (no leak, no OOB).
TEST(MirrorClipPlanesHarden, DegenerateSurfaceReturnsNullNoLeak) {
    // Zero-poly mesh.
    MirrorMeshBlock empty{}; empty.polys = nullptr; empty.polyCount = 0;
    CHECK(CreateClippingPlanes(&empty, 7) == nullptr);

    // One back-facing surface poly whose 3 vertices are coincident -> 1 unique point
    // -> uniqueCount < 3 -> null (the scratch alloc is freed on that path).
    float v[3] = {2, 2, 2};
    MirrorPoly poly{}; poly.v[0] = v; poly.v[1] = v; poly.v[2] = v;
    poly.surfaceKey = 9; poly.signByte = -1;
    MirrorPoly polys[1] = {poly};
    MirrorMeshBlock mesh{}; mesh.polys = polys; mesh.polyCount = 1;
    CHECK(CreateClippingPlanes(&mesh, 9) == nullptr);
}

// A node with no reflective child (no byte[104]&0x20) is not published.
TEST(MirrorPrepareNode, NonReflectiveNodeNotPublished) {
    std::vector<u8> child(128, 0);            // bit5 NOT set
    void* childPtr = child.data();
    std::vector<u8> parent(512, 0);
    *(u32*)(parent.data() + 480) = 1;
    void* children[1] = {childPtr};

    MirrorPoly poly{}; poly.surfaceKey = 1; poly.signByte = -1;
    MirrorPoly polys[1] = {poly};
    MirrorMeshBlock mesh{};
    mesh.polys = polys; mesh.polyCount = 1;
    mesh.parent = parent.data(); mesh.childArray = children;

    ReflectionNode node{}; node.mesh = &mesh; node.activeFlag = 0x08;
    ReflectionBindContext ctx{};
    void* prepared = nullptr;

    PrepareReflectionNode(&node, &ctx, &prepared, nullptr, nullptr);
    CHECK(prepared == nullptr);               // not published
    CHECK(ctx.clipPlanes == nullptr);
}

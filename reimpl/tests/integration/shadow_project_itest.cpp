#include "test.h"

#include "render/shadow_project.h"
#include "render/shadow_render.h"   // real ShadowSurface / RasterizeTriangle sibling
#include "util/transform.h"          // real VIBE_Transform_PointThroughBoneChain

#include <cmath>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {
bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
} // namespace

// ===========================================================================
// INTEGRATION: caster origin (real bone-chain transform) -> ComputeCasterHeight
// -> project the caster mesh onto that ground height -> rasterize the projected
// triangle into a real ShadowSurface via the real RasterizeTriangle. This is the
// exact composition VIBE_Shadow_RenderMeshShadow performs, wired across the real
// util/transform and render/shadow_render siblings.
// ===========================================================================
TEST(ShadowProjectIntegration, BoneOriginDrivesGroundHeight) {
    // Build a minimal "frame" buffer: identity-ish with a known local translation
    // at float[30..32] (bytes 120/124/128) and a NULL parent at byte 504.
    std::vector<float> frame(140, 0.0f);
    frame[30] = 1.0f;   // local translation x
    frame[31] = 8.0f;   // local translation y  -> origin.y after no-parent chain
    frame[32] = -2.0f;  // local translation z
    // parent link (byte 504 == float[126]) left zero -> chain ends immediately.

    float origin[3] = {0, 0, 0};
    float point[3]  = {0, 0, 0};
    // out = point + frame[30..32] with no parent -> (1, 8, -2).
    util::PointThroughBoneChain(frame.data(), point, origin);
    CHECK(Near(origin[0], 1.0f) && Near(origin[1], 8.0f) && Near(origin[2], -2.0f));

    // Feed that origin.y into the on-ground caster-height path.
    ShadowCasterSlot table[kCasterTableSlots]; // all empty -> no override
    CasterHeightQuery q;
    q.originY = origin[1];      // 8.0
    q.planeY  = 2.0f;
    q.baseOffset = 0.5f;
    q.onGround = true;
    ShadowSamplePoint samples[8] = {};
    q.samples = samples; q.sampleCount = 8;

    float groundY = ComputeCasterHeight(table, /*enabled=*/true, q);
    // 8.0 - 2.0 + 0.5 = 6.5.
    CHECK(Near(groundY, 6.5f));

    // Now project a caster triangle (above groundY) straight down onto it.
    ShadowVec3 mesh[3] = {
        { 8.0f, 12.0f,  4.0f},
        {16.0f, 12.0f, 14.0f},
        { 2.0f, 12.0f, 14.0f},
    };
    ShadowVec3 dir{0.0f, -1.0f, 0.0f};   // straight-down directional light
    ShadowVec3 proj[3];
    ShadowBounds bounds =
        ProjectMeshToGround(mesh, 3, proj, dir, /*directional=*/true, groundY);

    // Straight-down projection: x,z unchanged, y == groundY.
    for (int i = 0; i < 3; ++i) {
        CHECK(Near(proj[i].x, mesh[i].x));
        CHECK(Near(proj[i].z, mesh[i].z));
        CHECK(Near(proj[i].y, groundY));
    }
    // Bounds fold matches the mesh XZ extent.
    CHECK(Near(bounds.minX, 2.0f) && Near(bounds.maxX, 16.0f));
    CHECK(Near(bounds.minZ, 4.0f) && Near(bounds.maxZ, 14.0f));

    // Rasterize the projected shadow (XZ -> surface XY) with the REAL rasterizer.
    ShadowSurface surf;
    std::vector<u8> px(32 * 32, 0);
    surf.pixels = px.data();
    surf.pitch = 32; surf.width = 32; surf.height = 32; surf.is16bpp = false;

    ShadowTri tri;
    for (int i = 0; i < 3; ++i) { tri.x[i] = proj[i].x; tri.y[i] = proj[i].z; }
    tri.backFlag = true;

    ShadowRasterState s;
    RasterizeTriangle(s, tri, surf);

    int filled = 0;
    for (u8 v : px) if (v == 1) ++filled;
    CHECK(filled > 0);
    // Centroid ((8+16+2)/3, (4+14+14)/3) = (8.67, 10.67) is inside the shadow.
    CHECK(px[10 * 32 + 8] == 1);
    // Corner stays clear.
    CHECK(px[0] == 0);
}

// ===========================================================================
// INTEGRATION: point-light projection produces a divergent (enlarged) shadow
// versus the directional case, and the bounds widen accordingly — verified by
// projecting the same mesh both ways and rasterizing.
// ===========================================================================
TEST(ShadowProjectIntegration, PointVsDirectionalShadowSize) {
    ShadowVec3 mesh[3] = {
        {-2.0f, 6.0f, -2.0f},
        { 2.0f, 6.0f, -2.0f},
        { 0.0f, 6.0f,  2.0f},
    };
    ShadowVec3 dir{0.0f, -1.0f, 0.0f};
    ShadowVec3 dproj[3];
    ShadowBounds db = ProjectMeshToGround(mesh, 3, dproj, dir, true, 0.0f);

    // Point light high above the centroid -> shadow strictly larger than the mesh
    // (rays diverge), so its XZ extent exceeds the straight-down one.
    ShadowVec3 L{0.0f, 18.0f, 0.0f};
    ShadowVec3 pproj[3];
    ShadowBounds pb = ProjectMeshToGround(mesh, 3, pproj, L, /*directional=*/false, 0.0f);

    float dWidth = db.maxX - db.minX;
    float pWidth = pb.maxX - pb.minX;
    CHECK(pWidth > dWidth);                // point shadow is wider
    CHECK(Near(dWidth, 4.0f));             // directional == mesh width

    // Every projected vertex lands on the ground plane for BOTH methods.
    for (int i = 0; i < 3; ++i) {
        CHECK(Near(dproj[i].y, 0.0f));
        CHECK(Near(pproj[i].y, 0.0f));
    }

    // Rasterize the (larger) point shadow into a surface offset to keep it in range.
    ShadowSurface surf;
    std::vector<u8> px(40 * 40, 0);
    surf.pixels = px.data();
    surf.pitch = 40; surf.width = 40; surf.height = 40; surf.is16bpp = false;

    ShadowTri tri;
    for (int i = 0; i < 3; ++i) {
        tri.x[i] = pproj[i].x + 20.0f;     // bias into [0,40)
        tri.y[i] = pproj[i].z + 20.0f;
    }
    tri.backFlag = true;

    ShadowRasterState s;
    RasterizeTriangle(s, tri, surf);
    int filled = 0;
    for (u8 v : px) if (v == 1) ++filled;
    CHECK(filled > 0);
}

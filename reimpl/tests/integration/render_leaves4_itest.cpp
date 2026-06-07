#include "test.h"

// Integration: drive render_leaves4's VIBE_Mesh_AccumulateAabbRecursive against
// the REAL reconstructed sibling VIBE_Object_AssignMeshData (object_lifecycle5.cpp
// 0x5b2c70) — no mock mesh-binder.
//
// AccumulateAabbRecursive folds an object's 8-corner bbox into a running min/max
// box, and BEFORE reading the corners calls two installable hooks:
//   RenderLeaves4Hooks.assignMeshData(obj)      — 0x5b2c70 VIBE_Object_AssignMeshData
//   RenderLeaves4Hooks.propagateDirtyFlag(obj,1) — 0x5af2c0 VIBE_Object_PropagateDirtyFlag
// We forward assignMeshData into the genuine sim::ObjectAssignMeshData, exactly as
// the live engine wires the scene-graph mesh bind ahead of the AABB pass. The real
// sibling selects the LOD mesh (its own meshSelectLodFrame hook), binds it into
// node+460, and stamps the dirty bit node+528 |= 4. We assert those real
// cross-module effects fire while render_leaves4 produces the correct AABB.
//
// (propagateDirtyFlag's only reconstructed candidate — render's
// ObjectPropagateDirtyFlag — is an UNTRANSLATED placeholder, so it is left on the
// render_leaves4 inert default; this test exercises the real AssignMeshData sibling.)
#include "render/render_leaves4.h"
#include "render/colorformat.h"        // real PackColor / UnpackColor (0x434f30 / 0x434f7c)
#include "sim/object_lifecycle5.h"     // real ObjectAssignMeshData (0x5b2c70)

#include <cstdint>
#include <cstring>

using namespace guild;

namespace {

// render_leaves4 reads the object's mesh record at obj+0x1CC (460) as a MeshGeom*
// { corners, triangles, startIndex, triCount } and the child list at obj+0x1FC
// (508, link) / child+0x1F0 (496, link). We build the object as a raw byte block so
// those raw offsets are exact (render reads them via memcpy from the block).
struct RenderObj {
    std::uint8_t raw[0x21C];   // 540 bytes — matches the node block size
    RenderObj() { std::memset(raw, 0, sizeof raw); }
    void SetMeshGeom(const void* g) { std::memcpy(raw + 0x1CC, &g, sizeof(void*)); }
    void SetChild(const void* c)    { std::memcpy(raw + 0x1FC, &c, sizeof(void*)); }
    std::uint8_t& At(int off) { return raw[off]; }
};

// 80-byte-stride corner record: render reads floats at +0/+4/+8 per corner.
struct Corner { float x, y, z; std::uint8_t pad[80 - 12]; };

// --- cross-module wiring state -------------------------------------------------
// The Node5 the real sim::ObjectAssignMeshData operates on. The render hook
// forwards each render obj into this node so we can observe the real sibling's
// scene-graph mutations (mesh bound into +460, dirty bit at +528).
sim::Node5  g_node;
sim::Node5  g_lodMesh;            // the LOD mesh record the real sibling will bind
int         g_assignForwardCalls = 0;

// The real sibling's own LOD-select hook: returns a non-null mesh record so the
// genuine bind path (node->p(460) = mesh; node+528 |= 4) executes.
sim::Node5* RealMeshSelectLodFrame(sim::Node5* /*node*/) { return &g_lodMesh; }

// render_leaves4 hook -> forward into the REAL VIBE_Object_AssignMeshData.
void ForwardAssignMeshData(void* /*renderObj*/) {
    ++g_assignForwardCalls;
    sim::ObjectAssignMeshData(&g_node, /*negativeScale=*/false);
}

} // namespace

// AccumulateAabbRecursive over a single object with 8 corners. The assignMeshData
// hook forwards into the real sibling; assert (1) render produced the exact AABB,
// (2) the real ObjectAssignMeshData bound the mesh + stamped the dirty bit.
TEST(RenderLeaves4Itest, AccumulateAabbForwardsAssignMeshDataToRealSibling) {
    // real sibling wiring: install its LOD-select hook so it does real work.
    sim::ObjLife5Hooks oh{};
    oh.meshSelectLodFrame = &RealMeshSelectLodFrame;
    sim::ObjLife5SetHooks(oh);

    g_node.reset();
    g_lodMesh.reset();
    g_assignForwardCalls = 0;

    // 8 corners forming an axis-aligned box from (-1,-2,-3) to (4,5,6).
    Corner corners[8];
    std::memset(corners, 0, sizeof corners);
    const float xs[2] = {-1.0f, 4.0f};
    const float ys[2] = {-2.0f, 5.0f};
    const float zs[2] = {-3.0f, 6.0f};
    for (int i = 0; i < 8; ++i) {
        corners[i].x = xs[(i >> 0) & 1];
        corners[i].y = ys[(i >> 1) & 1];
        corners[i].z = zs[(i >> 2) & 1];
    }

    render::MeshGeom geom{};
    geom.corners    = &corners[0].x;
    geom.triangles  = nullptr;
    geom.startIndex = 0;
    geom.triCount   = 0;

    RenderObj obj;
    obj.SetMeshGeom(&geom);
    obj.SetChild(nullptr);

    render::RenderLeaves4Hooks rh{};
    rh.assignMeshData     = &ForwardAssignMeshData;
    rh.propagateDirtyFlag = render::CurrentRenderLeaves4Hooks().propagateDirtyFlag; // inert default
    render::InstallRenderLeaves4Hooks(rh);

    float box[7] = {1e30f, 1e30f, 1e30f, 0.0f, -1e30f, -1e30f, -1e30f};
    render::AccumulateAabbRecursive(box, &obj);

    // (1) render_leaves4 produced the correct fold.
    CHECK_EQ(box[0], -1.0f);
    CHECK_EQ(box[1], -2.0f);
    CHECK_EQ(box[2], -3.0f);
    CHECK_EQ(box[4], 4.0f);
    CHECK_EQ(box[5], 5.0f);
    CHECK_EQ(box[6], 6.0f);

    // (2) the real sibling ran via the forwarded hook.
    CHECK_EQ(g_assignForwardCalls, 1);
    // ObjectAssignMeshData bound the LOD mesh into node+460 ...
    CHECK(g_node.p(460) == &g_lodMesh);
    // ... and stamped the dirty bit (node+528 |= 4) — the real cross-module effect.
    CHECK((g_node.b(528) & 4u) != 0u);

    sim::ObjLife5ResetHooks();
}

// Recursion: a parent with one child both forward into the real sibling; assert
// the real AssignMeshData fired once per visited node (2 total) and the AABB spans
// both objects' corners.
TEST(RenderLeaves4Itest, RecursiveAabbForwardsPerNode) {
    sim::ObjLife5Hooks oh{};
    oh.meshSelectLodFrame = &RealMeshSelectLodFrame;
    sim::ObjLife5SetHooks(oh);

    g_node.reset();
    g_lodMesh.reset();
    g_assignForwardCalls = 0;

    Corner pc[8], cc[8];
    std::memset(pc, 0, sizeof pc);
    std::memset(cc, 0, sizeof cc);
    // parent box (0,0,0)-(1,1,1)
    for (int i = 0; i < 8; ++i) {
        pc[i].x = (i & 1) ? 1.0f : 0.0f;
        pc[i].y = (i & 2) ? 1.0f : 0.0f;
        pc[i].z = (i & 4) ? 1.0f : 0.0f;
    }
    // child box (5,5,5)-(7,7,7) — extends the max corner.
    for (int i = 0; i < 8; ++i) {
        cc[i].x = (i & 1) ? 7.0f : 5.0f;
        cc[i].y = (i & 2) ? 7.0f : 5.0f;
        cc[i].z = (i & 4) ? 7.0f : 5.0f;
    }

    render::MeshGeom pg{}, cg{};
    pg.corners = &pc[0].x; pg.startIndex = 0;
    cg.corners = &cc[0].x; cg.startIndex = 0;

    RenderObj child;
    child.SetMeshGeom(&cg);
    child.SetChild(nullptr);

    RenderObj parent;
    parent.SetMeshGeom(&pg);
    parent.SetChild(&child);

    render::RenderLeaves4Hooks rh{};
    rh.assignMeshData     = &ForwardAssignMeshData;
    rh.propagateDirtyFlag = render::CurrentRenderLeaves4Hooks().propagateDirtyFlag;
    render::InstallRenderLeaves4Hooks(rh);

    float box[7] = {1e30f, 1e30f, 1e30f, 0.0f, -1e30f, -1e30f, -1e30f};
    render::AccumulateAabbRecursive(box, &parent);

    CHECK_EQ(box[0], 0.0f);
    CHECK_EQ(box[6], 7.0f);
    // real sibling forwarded once for parent + once for child.
    CHECK_EQ(g_assignForwardCalls, 2);
    CHECK((g_node.b(528) & 4u) != 0u);

    sim::ObjLife5ResetHooks();
}

// Secondary integration: the Surface pixel leaves reuse the REAL (non-hook)
// reconstructed PackColor/UnpackColor (colorformat.cpp). Round-trip a pixel
// through SetPixelRgb (uses real PackColor) then GetPixelRgb (uses real
// UnpackColor) on a 16bpp RGB565 surface and assert the colour survives.
TEST(RenderLeaves4Itest, SurfacePixelRoundTripViaRealColorFormat) {
    render::ColorFormat fmt = render::ComputeChannelShifts(0xF800u, 0x07E0u, 0x001Fu);

    std::uint16_t pixels[16 * 16];
    std::memset(pixels, 0, sizeof pixels);

    // Software-surface record by byte offset: +16 pitch, +20 bpp, +28 pixel base,
    // +36/+40 clip x0/y0, +44/+48 clip x1/y1.
    std::uint8_t surf[64];
    std::memset(surf, 0, sizeof surf);
    std::int32_t pitch = 16, x1 = 16, y1 = 16, zero = 0;
    std::uint8_t bpp = 16;
    void* base = pixels;
    std::memcpy(surf + 16, &pitch, 4);
    std::memcpy(surf + 20, &bpp, 1);
    std::memcpy(surf + 28, &base, sizeof(void*));
    std::memcpy(surf + 36, &zero, 4);
    std::memcpy(surf + 40, &zero, 4);
    std::memcpy(surf + 44, &x1, 4);
    std::memcpy(surf + 48, &y1, 4);

    // Channel values chosen so 565 truncation is lossless on read-back.
    const std::uint8_t R = 0xF8, G = 0xFC, B = 0xF8;
    int wrote = render::SetPixelRgb(fmt, 3, 4, R, G, B, surf);
    CHECK_EQ(wrote, 1);

    std::uint8_t out[3] = {0, 0, 0};
    render::GetPixelRgb(fmt, 3, 4, out, surf);
    // GetPixelRgb 15/16bpp store order is out[0]=r, out[1]=b, out[2]=g.
    CHECK_EQ(out[0], R);
    CHECK_EQ(out[1], B);
    CHECK_EQ(out[2], G);

    // out-of-clip write rejected.
    CHECK_EQ(render::SetPixelRgb(fmt, 99, 99, R, G, B, surf), 0);
}

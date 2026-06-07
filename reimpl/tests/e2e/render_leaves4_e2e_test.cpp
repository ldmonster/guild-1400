#include "test.h"

#include "render/render_leaves4.h"
#include "render/mesh_scene.h"   // ComputeAabbExtents (reused by TestAabbOverlapRecursive)
#include "render/colorformat.h"  // ComputeChannelShifts / PackColor
#include "crt/rand.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;
using guild::render::RenderLeaves4Hooks;

namespace {

template <class T> void Put(void* base, int off, T v) {
    std::memcpy(static_cast<unsigned char*>(base) + off, &v, sizeof(T));
}
template <class T> T Get(const void* base, int off) {
    T v; std::memcpy(&v, static_cast<const unsigned char*>(base) + off, sizeof(T)); return v;
}
bool FEq(float a, float b) {
    std::uint32_t ua, ub; std::memcpy(&ua, &a, 4); std::memcpy(&ub, &b, 4); return ua == ub;
}

// instrumentation for the hooks
int g_assignCalls = 0;
int g_dirtyCalls  = 0;
void CountAssign(void* /*obj*/) { ++g_assignCalls; }
void CountDirty(void* /*obj*/, u32 /*flags*/) { ++g_dirtyCalls; }

} // namespace

// ---------------------------------------------------------------------------
// Flow: light flicker envelope drives a child brightness refresh.
// Reseeds the flicker target deterministically (via crt RNG), reads back the
// blended intensity through the public envelope helper, then runs the child
// brightness propagation pass and checks both lighting modes interoperate.
// ---------------------------------------------------------------------------
namespace guild { namespace render {
float FlickerEnvelopeStep(u32, u32, bool, float, float, float, float*, float*);
} }

TEST(RenderLeaves4_E2E, FlickerThenBrightnessRefresh) {
    // Deterministic reseed: same seed must yield same endpoints every run.
    crt::Srand(2024);
    float p0, n0;
    float a = render::FlickerEnvelopeStep(99, 10, false, 0.0f, 0.0f, 0.5f, &p0, &n0);
    crt::Srand(2024);
    float p1, n1;
    float b = render::FlickerEnvelopeStep(99, 10, false, 0.0f, 0.0f, 0.5f, &p1, &n1);
    CHECK(FEq(a, b));
    CHECK(FEq(p0, p1));
    CHECK(FEq(n0, n1));

    // Build a single-node light list and run brightness propagation in raw mode.
    std::vector<unsigned char> root(0x400, 0), wrap(0x200, 0);
    std::vector<unsigned char> obj(0x400, 0), mesh(0x1000, 0), sub(0x40, 0);
    std::vector<unsigned char> c0(0x80, 0), c1(0x80, 0);
    std::vector<unsigned char> nodeBuf(sizeof(render::LightNode) + 2 * sizeof(render::LightChildPair), 0);
    auto* node = reinterpret_cast<render::LightNode*>(nodeBuf.data());
    Put<void*>(root.data(), 0x1E8, wrap.data());
    Put<void*>(wrap.data(), 0x198, node);
    node->count = 2;                                  // two children
    node->obj = obj.data();
    node->next = nullptr;
    node->pairs[0].vbase = c0.data();
    node->pairs[0].color = 0;
    node->pairs[1].vbase = c1.data();
    node->pairs[1].color = 0;
    Put<void*>(obj.data(), 0x1EC, mesh.data());
    Put<void*>(obj.data(), 0x1CC, sub.data());
    Put<std::int32_t>(sub.data(), 8, 2);
    Put<std::uint32_t>(c0.data(), 0x44, 0xDEADBEEFu);
    Put<std::uint32_t>(c1.data(), 0x44, 0x0BADF00Du);

    render::g_rawLightingFlag = 1;
    render::RefreshChildBrightness(root.data());
    CHECK_EQ(Get<std::uint32_t>(c0.data(), 0x40), 0xDEADBEEFu);
    CHECK_EQ(Get<std::uint32_t>(c1.data(), 0x40), 0x0BADF00Du);
    render::g_rawLightingFlag = 0;
}

// ---------------------------------------------------------------------------
// Flow: AABB accumulate over a parent + child subtree, then an overlap probe.
// Exercises AccumulateAabbRecursive recursion AND TestAabbOverlapRecursive's
// reuse of the already-reconstructed ComputeAabbExtents, plus the installable
// hooks (instrumented to confirm the per-object mesh-prep calls fire).
// ---------------------------------------------------------------------------
TEST(RenderLeaves4_E2E, AabbAccumulateAndOverlapProbe) {
    g_assignCalls = g_dirtyCalls = 0;
    RenderLeaves4Hooks h = render::CurrentRenderLeaves4Hooks();
    h.assignMeshData = &CountAssign;
    h.propagateDirtyFlag = &CountDirty;
    render::InstallRenderLeaves4Hooks(h);

    // Parent obj + one child, each with an 8-corner mesh box.
    std::vector<unsigned char> parent(0x400, 0), child(0x400, 0);
    render::MeshGeom meshP{}, meshC{};
    std::vector<float> cP(8 * 20, 0.0f), cC(8 * 20, 0.0f);

    Put<void*>(parent.data(), 0x1CC, &meshP);
    meshP.corners = cP.data();
    meshP.startIndex = 0;
    Put<void*>(parent.data(), 0x1FC, child.data());   // child list head
    Put<void*>(child.data(), 0x1F0, nullptr);
    Put<void*>(child.data(), 0x1CC, &meshC);
    meshC.corners = cC.data();
    meshC.startIndex = 0;
    Put<void*>(child.data(), 0x1FC, nullptr);

    for (int i = 0; i < 8; ++i) {
        cP[i * 20 + 0] = (i & 1) ? 1.0f : 0.0f;
        cP[i * 20 + 1] = (i & 2) ? 1.0f : 0.0f;
        cP[i * 20 + 2] = (i & 4) ? 1.0f : 0.0f;       // parent box [0,1]^3
        cC[i * 20 + 0] = (i & 1) ? 5.0f : 4.0f;
        cC[i * 20 + 1] = (i & 2) ? 5.0f : 4.0f;
        cC[i * 20 + 2] = (i & 4) ? 5.0f : 4.0f;       // child box [4,5]^3
    }

    float box[7] = {1e30f, 1e30f, 1e30f, 0, -1e30f, -1e30f, -1e30f};
    render::AccumulateAabbRecursive(box, parent.data());
    // merged box spans [0,0,0]..[5,5,5]
    CHECK(FEq(box[0], 0.0f));
    CHECK(FEq(box[4], 5.0f));
    CHECK(FEq(box[6], 5.0f));
    // hooks fired once per object (parent + child)
    CHECK_EQ(g_assignCalls, 2);
    CHECK_EQ(g_dirtyCalls, 2);

    // Overlap probe: class-byte-4 obj with a single triangle; probe interval box
    // chosen to overlap it. Confirms ComputeAabbExtents reuse + Y-span capture.
    std::vector<unsigned char> probe(0x40, 0), pobj(0x400, 0);
    render::MeshGeom pmesh{};
    std::vector<float> pcorners(8 * 20, 0.0f);
    std::vector<float> verts(9, 0.0f);                // 3 verts * 3 floats
    render::MeshTriangle tri{};                        // one triangle

    Put<unsigned char>(pobj.data(), 533, 4);          // class 4
    Put<void*>(pobj.data(), 0x1CC, &pmesh);
    Put<void*>(pobj.data(), 508, nullptr);            // no children
    pmesh.corners = pcorners.data();
    pmesh.startIndex = 0;
    pmesh.triangles = &tri;                            // triangle array
    pmesh.triCount = 1;                                // one triangle

    // mesh corner box [0,1]^3 (so the probe interval can straddle it)
    for (int i = 0; i < 8; ++i) {
        pcorners[i * 20 + 0] = (i & 1) ? 1.0f : 0.0f;
        pcorners[i * 20 + 1] = (i & 2) ? 1.0f : 0.0f;
        pcorners[i * 20 + 2] = (i & 4) ? 1.0f : 0.0f;
    }
    // triangle verts: spanning Y from 0.2 to 0.8 inside the box
    float vv[3][3] = {{0.1f, 0.2f, 0.1f}, {0.9f, 0.8f, 0.1f}, {0.5f, 0.5f, 0.9f}};
    for (int t = 0; t < 3; ++t) {
        verts[t * 3 + 0] = vv[t][0];
        verts[t * 3 + 1] = vv[t][1];
        verts[t * 3 + 2] = vv[t][2];
        tri.v[t] = &verts[t * 3];
    }
    // probe interval box [+88]=minX..[+96]=minZ, [+104]=maxX..[+112]=maxZ.
    Put<float>(probe.data(), 88, -1.0f);   // minX
    Put<float>(probe.data(), 92, -1.0f);   // minY
    Put<float>(probe.data(), 96, -1.0f);   // minZ
    Put<float>(probe.data(), 104, 2.0f);   // maxX
    Put<float>(probe.data(), 108, 2.0f);   // maxY
    Put<float>(probe.data(), 112, 2.0f);   // maxZ
    Put<float>(probe.data(), 16, 1e30f);   // Y-span min accumulator
    Put<float>(probe.data(), 20, -1e30f);  // Y-span max accumulator

    int r = render::TestAabbOverlapRecursive(probe.data(), pobj.data());
    CHECK_EQ(r, 0);  // a class-4 node was processed -> 0
    // Y-span captured from the triangle verts (0.2 .. 0.8)
    CHECK(FEq(Get<float>(probe.data(), 16), 0.2f));
    CHECK(FEq(Get<float>(probe.data(), 20), 0.8f));
}

// ---------------------------------------------------------------------------
// Flow: blit a 16bpp RGB block into a surface, then read individual pixels back
// with GetPixelRgb and confirm each survives the 565 quantization round-trip.
// Exercises BlitRgbToPixels -> GetPixelRgb sharing one ColorFormat/surface.
// ---------------------------------------------------------------------------
TEST(RenderLeaves4_E2E, BlitThenReadbackSurface) {
    render::ColorFormat fmt = render::ComputeChannelShifts(0xF800u, 0x07E0u, 0x001Fu);
    std::vector<unsigned char> surf(0x40, 0);
    std::vector<std::uint16_t> pixels(32 * 32, 0);
    Put<std::int32_t>(surf.data(), 16, 32);          // pitch
    Put<unsigned char>(surf.data(), 20, 16);          // 16bpp
    Put<void*>(surf.data(), 28, pixels.data());
    Put<std::int32_t>(surf.data(), 44, 32);
    Put<std::int32_t>(surf.data(), 48, 32);

    // 2x2 block of 565-quantization-stable colours (low bits already cleared).
    unsigned char block[2 * 2 * 3] = {
        248,124,16,   16,252,248,
        248,0,8,      0,124,248,
    };
    render::BlitRgbToPixels(fmt, 2, 2, block, surf.data());

    // Read back each pixel; the 16bpp Get stores out[0]=r, out[1]=b, out[2]=g.
    struct { int x, y, r, g, b; } expect[4] = {
        {0, 0, 248, 124, 16}, {1, 0, 16, 252, 248},
        {0, 1, 248, 0, 8},    {1, 1, 0, 124, 248},
    };
    for (auto& e : expect) {
        unsigned char out[3] = {0, 0, 0};
        render::GetPixelRgb(fmt, e.x, e.y, out, surf.data());
        CHECK_EQ((int)out[0], e.r);   // r
        CHECK_EQ((int)out[1], e.b);   // b
        CHECK_EQ((int)out[2], e.g);   // g
    }
}

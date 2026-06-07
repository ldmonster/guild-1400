// Integration: world_render places objects at their REAL decoded world transform
// (object_transform.h) instead of the synthetic id-derived grid. We seed live
// g_objects, bind each to a SYNTHETIC engine render node carrying a real world
// position/yaw, install a NodeResolver, build the draw list with real placement,
// and assert each quad lands at the world->screen projection of its true node
// position (distinct, NOT a uniform grid) — then render through the real pipeline.
#include "test.h"

#include <cmath>
#include <cstring>

#include "play/object_transform.h"
#include "play/world_render.h"
#include "sim/entity.h"
#include "shim_impl/memory_graphics.h"

using namespace guild;

namespace {

constexpr int kNodeSize = 600;   // cover +533 type byte + +396..+460 matrix

void WrF(unsigned char* base, int off, float v) {
    std::memcpy(base + off, &v, sizeof v);
}

void MatFromEuler(float ax, float ay, float az, float m[16]) {
    std::memset(m, 0, sizeof(float) * 16);
    float s_ay = std::sin(ay), c_ay = std::cos(ay);
    float c_ax = std::cos(ax), s_ax = std::sin(ax);
    float c_az = std::cos(az), s_az = std::sin(az);
    m[0]=c_ay*c_az; m[4]=c_ay*s_az; m[8]=-s_ay; m[9]=s_ax*c_ay; m[10]=c_ax*c_ay;
    m[15]=1.0f;
    m[1]=s_ax*(s_ay*c_az)-c_ax*s_az; m[2]=(s_ay*c_az)*c_ax+s_ax*s_az;
    m[5]=c_ax*c_az+s_ax*(s_ay*s_az); m[6]=(s_ay*s_az)*c_ax-s_ax*c_az;
}

void MakeNode(unsigned char* n, u8 type, float px, float py, float pz, float yaw) {
    std::memset(n, 0, kNodeSize);
    n[play::kNodeTypeByte] = type;
    WrF(n, play::kNodePosX, px);
    WrF(n, play::kNodePosY, py);
    WrF(n, play::kNodePosZ, pz);
    float m[16]; MatFromEuler(0, yaw, 0, m);
    std::memcpy(n + play::kNodeFrameMatrix, m, sizeof m);
}

bool Near(float a, float b, float eps = 0.5f) { return std::fabs(a - b) <= eps; }

// Module-level synthetic node table the resolver maps object ids to.
struct NodeBinding { i32 id; unsigned char node[kNodeSize]; bool used; };
NodeBinding g_bind[8];
int g_bindCount = 0;

const void* Resolver(const play::EntityRef& e) {
    for (int i = 0; i < g_bindCount; ++i)
        if (g_bind[i].used && g_bind[i].id == e.id)
            return g_bind[i].node;
    return nullptr;
}

void BindObject(int slot, i32 id, float px, float py, float pz, float yaw) {
    g_bind[slot].id = id;
    g_bind[slot].used = true;
    MakeNode(g_bind[slot].node, play::kNodeTypeMeshA, px, py, pz, yaw);
}

} // namespace

TEST(ObjectTransformItest, RealPlacementMatchesProjectedWorldCoords) {
    sim::ResetEntityArrays();
    std::memset(g_bind, 0, sizeof g_bind);
    g_bindCount = 4;

    // 4 alive objects with DISTINCT real world positions (a non-grid scatter).
    struct Seed { i32 id; float x, y, z, yaw; };
    Seed seeds[4] = {
        {101,  20.0f, 0.0f,  30.0f, 0.0f},
        {102,  80.0f, 0.0f,  10.0f, (float)(M_PI / 6.0)},
        {103, -10.0f, 0.0f, -40.0f, (float)(M_PI / 2.0)},
        {104,  55.0f, 0.0f,  70.0f, -(float)(M_PI / 4.0)},
    };
    for (int i = 0; i < 4; ++i) {
        sim::g_objects[i].alive = 1;
        sim::g_objects[i].id = seeds[i].id;
        BindObject(i, seeds[i].id, seeds[i].x, seeds[i].y, seeds[i].z, seeds[i].yaw);
    }

    play::WorldRenderer wr;
    play::WorldRenderer::Options opt;
    opt.fbW = 160; opt.fbH = 120;
    opt.emitTerrain = false;
    opt.scanScene = false;       // only the object array for this test
    opt.scanObjects = true;
    opt.scanPersons = false;
    opt.useRealPlacement = true;
    opt.nodeResolver = &Resolver;
    opt.pixelsPerUnit = 1.0f;
    opt.eyeX = 0.0f; opt.eyeZ = 0.0f;

    play::LoadedWorld w;
    play::WorldDrawList dl = wr.build(opt, w);

    CHECK_EQ(dl.objectQuads, 4);
    CHECK_EQ(w.objectCount, 4);

    // Each quad center must equal the world->screen projection of its node's REAL
    // decoded position: cx = fbW/2 + (x-eyeX)*ppu, cy = fbH/2 + (z-eyeZ)*ppu.
    for (int i = 0; i < 4; ++i) {
        float qcx = (w.objects[i].x0 + w.objects[i].x1) * 0.5f;
        float qcy = (w.objects[i].z0 + w.objects[i].z1) * 0.5f;
        float wantX = opt.fbW * 0.5f + seeds[i].x * opt.pixelsPerUnit;
        float wantY = opt.fbH * 0.5f + seeds[i].z * opt.pixelsPerUnit;
        CHECK(Near(qcx, wantX));
        CHECK(Near(qcy, wantY));
    }

    // The four quads occupy DISTINCT centers (the decoded layout, not a grid that
    // collapses to repeated cells).
    int distinct = 0;
    for (int i = 0; i < 4; ++i) {
        float ix = (w.objects[i].x0 + w.objects[i].x1) * 0.5f;
        float iy = (w.objects[i].z0 + w.objects[i].z1) * 0.5f;
        bool dup = false;
        for (int j = 0; j < i; ++j) {
            float jx = (w.objects[j].x0 + w.objects[j].x1) * 0.5f;
            float jy = (w.objects[j].z0 + w.objects[j].z1) * 0.5f;
            if (Near(ix, jx, 0.01f) && Near(iy, jy, 0.01f)) dup = true;
        }
        if (!dup) ++distinct;
    }
    CHECK_EQ(distinct, 4);

    // Render through the REAL pipeline (proves the real-placement quads project).
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(opt.fbW, opt.fbH, 16, false));
    play::RenderStats st = wr.render(opt, dev);
    std::printf("[ot-itest] appendedPolys=%d rasterTris=%d objectQuads=%d\n",
                st.appendedPolys, st.rasterTris, dl.objectQuads);
    CHECK(st.appendedPolys > 0);
    CHECK(st.presented);

    sim::ResetEntityArrays();
}

// Real placement falls back to the deterministic grid when no node is resolvable
// (a node-less load still renders a frame).
TEST(ObjectTransformItest, FallbackGridWhenNoNode) {
    sim::ResetEntityArrays();
    std::memset(g_bind, 0, sizeof g_bind);
    g_bindCount = 0;   // resolver returns null for everything

    sim::g_objects[0].alive = 1; sim::g_objects[0].id = 5;
    sim::g_objects[1].alive = 1; sim::g_objects[1].id = 6;

    play::WorldRenderer wr;
    play::WorldRenderer::Options opt;
    opt.fbW = 96; opt.fbH = 72;
    opt.emitTerrain = false; opt.scanScene = false; opt.scanObjects = true;
    opt.useRealPlacement = true;
    opt.nodeResolver = &Resolver;

    play::LoadedWorld w;
    play::WorldDrawList dl = wr.build(opt, w);
    // No nodes -> grid fallback still emits both (DefaultPlacementHook is visible).
    CHECK_EQ(dl.objectQuads, 2);
    CHECK_EQ(w.objectCount, 2);
    sim::ResetEntityArrays();
}

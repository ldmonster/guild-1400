// tests/unit/wire_scene_bridge_test.cpp — P6 SCENE-GRAPH WALK / NODE-DISPATCH
// bridge. A small synthetic scene tree proves InstallRealSceneBridge makes the
// generic scene walk (render::WalkAndInvoke) invoke the REAL per-node draw
// dispatch (render::ProcessSceneNodeAppend) instead of the inert no-op: the
// per-node side effect (draw-list append) flips inert -> real.
#include "test.h"

#include "play/wire_scene_bridge.h"
#include "render/geometry_types.h"

#include <cstring>

using namespace guild;
using namespace guild::play;

namespace {

// Build one front-facing textured polygon (the *(p+36) high-bit "visible" gate
// ProcessSceneNodeAppend tests) backed by three vertices, into caller storage.
void MakeVisiblePoly(render::Vertex v[3], render::Polygon& p) {
    for (int i = 0; i < 3; ++i) {
        v[i] = render::Vertex{};
        v[i].x = (float)i; v[i].y = 0.0f; v[i].z = (float)(i + 1);
    }
    p = render::Polygon{};
    p.v0 = &v[0]; p.v1 = &v[1]; p.v2 = &v[2];
    p.flags36 = 0x80;   // bit7 set -> (i8)flags36 < 0 -> front-facing / appended
}

// A draw-list sink over caller storage for the bridge context.
void BindSink(SceneBridgeContext& ctx, render::DrawListEntry* pool, int cap) {
    std::memset(pool, 0, sizeof(render::DrawListEntry) * (size_t)cap);
    ctx.out = render::DrawList{pool, 0, cap};
    ctx.appendCtx = render::NodeAppendContext{};
    ctx.appendCtx.mode = render::NodeAppendMode::Software;
    ctx.appendCtx.baseKey = 1;
}

} // namespace

// A single visible node: inert dispatch visits but appends NOTHING; real dispatch
// appends the node's polygon to the draw list.
TEST(WireSceneBridgeUnit, SingleNodeInertVsReal) {
    render::Vertex v[3];
    render::Polygon poly;
    MakeVisiblePoly(v, poly);
    u32 texSort[1] = {0};

    SceneDrawNode node{};
    node.nodeType = 3;            // type 3 -> mask bit 0x01 (TestNodeFlag)
    node.nextSibling = nullptr;   // null-terminated single-node list
    node.cullByte = 0;            // visible (no 0x40 fully-out)
    node.polys = &poly;
    node.polyCount = 1;
    node.texSortId = texSort;

    render::DrawListEntry pool[8];

    // --- inert: bridge NOT installed -> visit, no append ---
    UninstallRealSceneBridge();
    CHECK(!RealSceneBridgeInstalled());
    SceneBridgeContext inert{};
    BindSink(inert, pool, 8);
    char r1 = WalkSceneTree(&node, /*walkMask=*/0x1FF, inert);
    CHECK_EQ(r1, (char)1);                 // completed walk
    CHECK_EQ(inert.nodesVisited, 1);       // reached the node
    CHECK_EQ(inert.nodesDispatched, 1);    // dispatch callback ran (TestNodeFlag pass)
    CHECK_EQ(inert.polysAppended, 0);      // INERT: nothing appended
    CHECK_EQ(inert.out.count, 0);

    // --- real: bridge installed -> visit AND append ---
    InstallRealSceneBridge();
    CHECK(RealSceneBridgeInstalled());
    SceneBridgeContext real{};
    BindSink(real, pool, 8);
    char r2 = WalkSceneTree(&node, /*walkMask=*/0x1FF, real);
    CHECK_EQ(r2, (char)1);
    CHECK_EQ(real.nodesVisited, 1);
    CHECK_EQ(real.nodesDispatched, 1);
    CHECK_EQ(real.polysAppended, 1);       // REAL: the visible poly was appended
    CHECK_EQ(real.out.count, 1);
    if (real.out.count == 1)
        CHECK_EQ(real.out.entries[0].poly, &poly);  // the real entry points at our poly

    UninstallRealSceneBridge();
}

// TestNodeFlag gating: a node whose type the walk mask does NOT admit is reached
// but its dispatch callback is skipped (so no append even with the real bridge).
TEST(WireSceneBridgeUnit, WalkMaskGatesDispatch) {
    render::Vertex v[3];
    render::Polygon poly;
    MakeVisiblePoly(v, poly);

    SceneDrawNode node{};
    node.nodeType = 3;       // type 3 needs mask bit 0x01
    node.flags528 = 1;
    node.polys = &poly;
    node.polyCount = 1;

    render::DrawListEntry pool[8];
    InstallRealSceneBridge();

    SceneBridgeContext ctx{};
    BindSink(ctx, pool, 8);
    // mask 0x02 admits type 5, NOT type 3 -> node reached but not dispatched.
    char r = WalkSceneTree(&node, /*walkMask=*/0x02, ctx);
    CHECK_EQ(r, (char)1);
    CHECK_EQ(ctx.nodesVisited, 1);      // reached
    CHECK_EQ(ctx.nodesDispatched, 0);   // TestNodeFlag(3, 0x02) == false -> not dispatched
    CHECK_EQ(ctx.polysAppended, 0);

    UninstallRealSceneBridge();
}

// The exposed real dispatch leaf (BridgeDispatchNode) appends the same way the
// installed hook does — proving the hook routes to the real ProcessSceneNodeAppend.
TEST(WireSceneBridgeUnit, ExposedLeafMatchesHook) {
    render::Vertex v[3];
    render::Polygon poly;
    MakeVisiblePoly(v, poly);

    SceneDrawNode node{};
    node.nodeType = 3;
    node.flags528 = 1;
    node.cullByte = 0;
    node.polys = &poly;
    node.polyCount = 1;

    render::DrawListEntry pool[8];
    SceneBridgeContext ctx{};
    BindSink(ctx, pool, 8);

    int appended = BridgeDispatchNode(&node, ctx);
    CHECK_EQ(appended, 1);
    CHECK_EQ(ctx.out.count, 1);

    // A fully-culled node (cullByte 0x40) appends nothing (the engine's 0x40 gate).
    SceneDrawNode culled = node;
    culled.cullByte = 0x40;
    SceneBridgeContext ctx2{};
    BindSink(ctx2, pool, 8);
    CHECK_EQ(BridgeDispatchNode(&culled, ctx2), 0);
    CHECK_EQ(ctx2.out.count, 0);
}

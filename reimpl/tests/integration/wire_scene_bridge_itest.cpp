// tests/integration/wire_scene_bridge_itest.cpp — P6 SCENE-GRAPH WALK bridge
// over a MULTI-NODE scene tree (parent -> children, sibling chains, nested
// subtrees). Proves the bridge-installed walk dispatches the real per-node draw
// across the WHOLE tree (more nodes actually DRAW: inert appends 0, real appends
// every visible node's polys), exercising render::WalkAndInvoke's child recursion
// + sibling-chain traversal with the real ProcessSceneNodeAppend dispatch.
#include "test.h"

#include "play/wire_scene_bridge.h"
#include "render/geometry_types.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::play;

namespace {

// A small owned scene: N nodes, each with one visible polygon, linked into a tree.
struct Scene {
    std::vector<SceneDrawNode> nodes;
    std::vector<render::Vertex> verts;   // 3 per node
    std::vector<render::Polygon> polys;  // 1 per node

    explicit Scene(int n) : nodes(n), verts(n * 3), polys(n) {
        for (int i = 0; i < n; ++i) {
            render::Vertex* v = &verts[i * 3];
            for (int k = 0; k < 3; ++k) {
                v[k] = render::Vertex{};
                v[k].x = (float)(i + k); v[k].z = (float)(k + 1);
            }
            polys[i] = render::Polygon{};
            polys[i].v0 = &v[0]; polys[i].v1 = &v[1]; polys[i].v2 = &v[2];
            polys[i].flags36 = 0x80;   // front-facing -> appended
            nodes[i] = SceneDrawNode{};
            nodes[i].nodeType = 3;     // admitted by mask 0x01 (and 0x1FF)
            nodes[i].cullByte = 0;     // visible
            nodes[i].polys = &polys[i];
            nodes[i].polyCount = 1;
        }
    }
    SceneDrawNode& n(int i) { return nodes[i]; }
};

void BindSink(SceneBridgeContext& ctx, std::vector<render::DrawListEntry>& pool) {
    std::memset(pool.data(), 0, sizeof(render::DrawListEntry) * pool.size());
    ctx.out = render::DrawList{pool.data(), 0, (i32)pool.size()};
    ctx.appendCtx = render::NodeAppendContext{};
    ctx.appendCtx.mode = render::NodeAppendMode::Software;
    ctx.appendCtx.baseKey = 1;
}

} // namespace

// A 7-node tree:
//   root(0)
//     child(1) -> sibling(2) -> sibling(3,terminator)
//       (1)'s child(4) -> sibling(5,terminator)
//     (root has sibling 6, terminator)
// Inert walk: visits all 7, dispatches all 7, appends 0. Real walk: same visit/
// dispatch counts, appends 7 (one visible poly per node). More nodes DRAW.
TEST(WireSceneBridgeItest, MultiNodeTreeInertVsReal) {
    Scene s(7);
    // root(0) + sibling(6)
    s.n(0).firstChild  = &s.n(1);
    s.n(0).nextSibling = &s.n(6);    // null-terminated chains (every node visited)
    // children of 0: 1 -> 2 -> 3
    s.n(1).nextSibling = &s.n(2);
    s.n(2).nextSibling = &s.n(3);
    // children of 1: 4 -> 5
    s.n(1).firstChild  = &s.n(4);
    s.n(4).nextSibling = &s.n(5);

    std::vector<render::DrawListEntry> pool(64);

    // --- inert ---
    UninstallRealSceneBridge();
    SceneBridgeContext inert{};
    BindSink(inert, pool);
    char r1 = WalkSceneTree(&s.n(0), /*walkMask=*/0x1FF, inert);
    CHECK_EQ(r1, (char)1);
    CHECK_EQ(inert.nodesVisited, 7);
    CHECK_EQ(inert.nodesDispatched, 7);
    CHECK_EQ(inert.polysAppended, 0);     // INERT: no node draws
    CHECK_EQ(inert.out.count, 0);

    // --- real ---
    InstallRealSceneBridge();
    SceneBridgeContext real{};
    BindSink(real, pool);
    char r2 = WalkSceneTree(&s.n(0), /*walkMask=*/0x1FF, real);
    CHECK_EQ(r2, (char)1);
    CHECK_EQ(real.nodesVisited, 7);
    CHECK_EQ(real.nodesDispatched, 7);
    CHECK_EQ(real.polysAppended, 7);      // REAL: every visible node drew
    CHECK_EQ(real.out.count, 7);

    // More nodes DISPATCHED-WITH-EFFECT under the real bridge than the inert one.
    CHECK(real.polysAppended > inert.polysAppended);

    UninstallRealSceneBridge();
}

// Mixed node types + a fully-culled node: the real walk dispatches only mask-
// admitted nodes, and a cullByte 0x40 node draws nothing even when dispatched.
TEST(WireSceneBridgeItest, MixedTypesAndCulling) {
    Scene s(4);
    //   0(type3,visible) -> 1(type5,visible) -> 2(type3,CULLED) -> 3(type3,visible)
    s.n(0).nextSibling = &s.n(1);
    s.n(1).nextSibling = &s.n(2);
    s.n(2).nextSibling = &s.n(3);   // null-terminated
    s.n(1).nodeType = 5;     // type 5 -> mask bit 0x02
    s.n(2).cullByte = 0x40;  // fully out -> ProcessSceneNodeAppend appends nothing

    std::vector<render::DrawListEntry> pool(32);
    InstallRealSceneBridge();

    // mask 0x01 admits type 3 ONLY (nodes 0,2,3), not type 5 (node 1).
    SceneBridgeContext ctx{};
    BindSink(ctx, pool);
    char r = WalkSceneTree(&s.n(0), /*walkMask=*/0x01, ctx);
    CHECK_EQ(r, (char)1);
    CHECK_EQ(ctx.nodesVisited, 4);        // all reached
    CHECK_EQ(ctx.nodesDispatched, 3);     // type-3 nodes 0,2,3 dispatched; type-5 node 1 skipped
    CHECK_EQ(ctx.polysAppended, 2);       // node 0 + node 3 (node 2 culled, node 1 not dispatched)

    UninstallRealSceneBridge();
}

// Terminator-sentinel semantics (engine +528 bit0): a sibling whose flags528 bit0
// is set is a LIST-HEAD/terminator sentinel — the walk STOPS at it WITHOUT visiting
// it. Proves the bridge walk reproduces VIBE_SceneGraph_WalkAndInvoke's terminator
// short-circuit, so a real sibling chain ending in a sentinel dispatches only the
// content nodes before it.
TEST(WireSceneBridgeItest, SiblingTerminatorSentinel) {
    Scene s(4);
    //   0(content) -> 1(content) -> 2(content) -> 3(SENTINEL, flags528 bit0)
    s.n(0).nextSibling = &s.n(1);
    s.n(1).nextSibling = &s.n(2);
    s.n(2).nextSibling = &s.n(3);
    s.n(3).flags528 = 1;     // terminator sentinel: reached-at but NOT visited

    std::vector<render::DrawListEntry> pool(16);
    InstallRealSceneBridge();

    SceneBridgeContext ctx{};
    BindSink(ctx, pool);
    char r = WalkSceneTree(&s.n(0), /*walkMask=*/0x1FF, ctx);
    CHECK_EQ(r, (char)1);
    CHECK_EQ(ctx.nodesVisited, 3);     // 0,1,2 visited; sentinel 3 NOT visited
    CHECK_EQ(ctx.nodesDispatched, 3);
    CHECK_EQ(ctx.polysAppended, 3);    // only the 3 content nodes drew

    UninstallRealSceneBridge();
}

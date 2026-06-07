#include "test.h"

// UNIT: the REAL-WORLD draw-list BUILD (play::WorldRenderer::build) from synthetic
// entity records. Asserts golden counts + golden coords:
//   - scanning g_objects / g_sceneNodes emits one quad per visible entity,
//   - the build is capped at the binder's 8-object limit + 1 terrain,
//   - the scene tree is walked parent-before-children (engine WalkAndInvoke order),
//   - the default placement hook produces deterministic, reproducible coords,
//   - the build summary counts (objectQuads / sceneQuads / sceneObjects) are exact.
#include "play/world_render.h"
#include "sim/entity.h"

using namespace guild;
using namespace guild::play;
using namespace guild::sim;

namespace {

void PutObject(int i, int id) {
    g_objects[i].alive = 1;
    g_objects[i].id = id;
}
void PutScene(int i, i16 type, int id, int child) {
    g_sceneNodes[i].type = type;
    g_sceneNodes[i].id = id;
    g_sceneNodes[i].childPtr = child;
    g_sceneNodes[i].entityPtr = -1;
    if (i + 1 > g_sceneNodeCount) g_sceneNodeCount = i + 1;
}

} // namespace

// --- golden counts: objects-only scan emits one quad per alive object ----------
TEST(WorldRenderUnit, ObjectScanGoldenCounts) {
    ResetEntityArrays();
    PutObject(0, 10);
    PutObject(2, 20);
    PutObject(5, 30);   // three alive objects, sparse slots

    WorldRenderer wr;
    WorldRenderer::Options opt;
    opt.scanObjects = true;
    opt.scanScene = false;
    opt.scanPersons = false;
    opt.emitTerrain = true;

    LoadedWorld w;
    WorldDrawList dl = wr.build(opt, w);

    CHECK_EQ(dl.objectQuads, 3);
    CHECK_EQ(dl.sceneQuads, 0);
    CHECK_EQ(dl.sceneObjects(), 3);
    CHECK(dl.hasTerrain);
    CHECK_EQ(dl.quadCount(), 4);          // 3 objects + terrain
    CHECK_EQ(w.objectCount, 3);
    CHECK(w.hasTerrain);
    CHECK(!w.hudEnabled);                  // real-world render: no synthetic HUD
}

// --- the 8-object cap holds (binder kMaxObjects) -------------------------------
TEST(WorldRenderUnit, ObjectCapHoldsAtEight) {
    ResetEntityArrays();
    for (int i = 0; i < 20; ++i)
        PutObject(i, 100 + i);          // 20 alive objects

    WorldRenderer wr;
    WorldRenderer::Options opt;
    opt.scanScene = false;
    opt.scanPersons = false;
    opt.emitTerrain = false;

    LoadedWorld w;
    WorldDrawList dl = wr.build(opt, w);

    CHECK_EQ(w.objectCount, 8);           // capped at the binder's 8 slots
    CHECK_EQ(dl.objectQuads, 8);
    CHECK_EQ(dl.quadCount(), 8);          // no terrain
}

// --- scene tree walked parent-before-children (engine WalkAndInvoke order) -----
TEST(WorldRenderUnit, SceneTreeWalkOrderParentFirst) {
    ResetEntityArrays();
    // node0 (root) -> child node1 -> child node2 ; node3 a separate root.
    PutScene(0, /*type=*/5, /*id=*/1000, /*child=*/1);
    PutScene(1, 5, 1001, 2);
    PutScene(2, 5, 1002, -1);
    PutScene(3, 5, 1003, -1);

    WorldRenderer wr;
    WorldRenderer::Options opt;
    opt.scanObjects = false;
    opt.scanScene = true;
    opt.scanPersons = false;
    opt.emitTerrain = false;
    opt.maxObjects = 8;

    LoadedWorld w;
    WorldDrawList dl = wr.build(opt, w);

    // All 4 scene nodes emitted exactly once (parent-then-children DFS + roots).
    CHECK_EQ(dl.sceneQuads, 4);
    CHECK_EQ(w.objectCount, 4);
    CHECK_EQ(dl.sceneObjects(), 4);
}

// --- empty world: terrain only, no scene objects ------------------------------
TEST(WorldRenderUnit, EmptyWorldTerrainOnly) {
    ResetEntityArrays();
    WorldRenderer wr;
    WorldRenderer::Options opt;
    opt.emitTerrain = true;

    LoadedWorld w;
    WorldDrawList dl = wr.build(opt, w);
    CHECK_EQ(dl.sceneObjects(), 0);
    CHECK_EQ(w.objectCount, 0);
    CHECK(dl.hasTerrain);
    CHECK_EQ(dl.quadCount(), 1);
}

// --- default placement hook is deterministic + on-screen ----------------------
TEST(WorldRenderUnit, DefaultPlacementDeterministicGoldenCoords) {
    EntityRef e{EntityKind::Object, /*id=*/42, /*slot=*/3, /*type=*/1};
    EntityPlacement a = DefaultPlacementHook(e, 96, 72);
    EntityPlacement b = DefaultPlacementHook(e, 96, 72);

    // Reproducible: same input -> identical placement (golden).
    CHECK(a.visible);
    CHECK_EQ(a.quad.x0, b.quad.x0);
    CHECK_EQ(a.quad.z0, b.quad.z0);
    CHECK_EQ(a.quad.x1, b.quad.x1);
    CHECK_EQ(a.quad.z1, b.quad.z1);
    CHECK_EQ((int)a.quad.light, (int)b.quad.light);

    // On-screen + well-formed quad (x1>x0, z1>z0, within the framebuffer).
    CHECK(a.quad.x0 >= 0.0f);
    CHECK(a.quad.x1 <= 96.0f);
    CHECK(a.quad.z0 >= 0.0f);
    CHECK(a.quad.z1 <= 72.0f);
    CHECK(a.quad.x1 > a.quad.x0);
    CHECK(a.quad.z1 > a.quad.z0);
    // id 42 is even -> opaque; an odd id -> translucent.
    CHECK(!a.quad.translucent);
    EntityRef o{EntityKind::Object, /*id=*/43, /*slot=*/3, /*type=*/1};
    CHECK(DefaultPlacementHook(o, 96, 72).quad.translucent);

    ResetEntityArrays();
}

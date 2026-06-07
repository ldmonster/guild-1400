// tests/e2e/wire_scene_bridge_e2e_test.cpp — GUARDED real-asset P6 SCENE-GRAPH
// WALK / NODE-DISPATCH bridge on AUGSBURG.
//
// Loads the real AUGSBURG world (mount real assets + io::LoadWorld populates the
// live sim entity arrays), builds a scene-graph tree from the REAL alive objects +
// scene nodes (parent root -> one node per live entity, in the engine's
// parent-then-sibling order), then walks that tree with render::WalkAndInvoke:
//   - INERT (bridge not installed): every node is visited+dispatched but NOTHING
//     is appended (the engine's "subsystem not present" no-op dispatch).
//   - REAL (InstallRealSceneBridge): each visible node's draw dispatch runs the
//     real ProcessSceneNodeAppend, appending the node's polys to the draw list.
// Asserts the real walk DISPATCHES the real per-node draw across the real nodes
// (more polys appended than inert), and reports the node counts.
//
// Skips cleanly when the shipped AUGSBURG.cty asset is absent (honors GUILD_GAME_DIR).
#include "test.h"

#include "play/wire_scene_bridge.h"
#include "app/real_boot.h"
#include "io/save_world_load.h"
#include "io/vfs.h"
#include "sim/entity.h"
#include "sim/types.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::play;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Gilde.INI") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty");
}

// One front-facing visible polygon (the ProcessSceneNode *(p+36) high-bit gate)
// per scene node, seeded from the entity id so the geometry is stable.
struct NodeGeom {
    render::Vertex v[3];
    render::Polygon poly;
    u32 texSort = 0;
    void Build(i32 idSeed) {
        for (int k = 0; k < 3; ++k) {
            v[k] = render::Vertex{};
            v[k].x = (float)((idSeed + k) & 0x3F);
            v[k].z = (float)(k + 1);
        }
        poly = render::Polygon{};
        poly.v0 = &v[0]; poly.v1 = &v[1]; poly.v2 = &v[2];
        poly.flags36 = 0x80;   // front-facing -> appended by the real dispatch
    }
};

} // namespace

TEST(WireSceneBridgeE2E, RealAugsburgSceneWalk) {
    if (!RealAssetsPresent()) {
        std::printf("[wire_scene_bridge_e2e] AUGSBURG assets absent under %s -> SKIP\n",
                    GameDir().c_str());
        return;  // guarded: no real assets
    }

    shim::DiskFileSystem fs(GameDir());

    // --- mount + load the real world into the live sim arrays ---
    app::RealGameAssets assets =
        app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI", {}, /*caseInsensitive=*/false);
    CHECK(assets.vfsBound);
    if (!assets.vfsBound) { io::VfsShutdown(); return; }

    std::memset(sim::g_objects, 0, sizeof(sim::g_objects));
    std::memset(sim::g_sceneNodes, 0, sizeof(sim::g_sceneNodes));
    sim::ResetEntityArrays();

    const std::string cityPath = app::RealCityPath("Augsburg");
    io::WorldState world{};
    bool loaded = io::LoadWorld(cityPath.c_str(), world);
    CHECK(loaded);
    if (!loaded) { io::VfsShutdown(); return; }

    // --- build a scene tree from the REAL live entities ---
    // root -> [one node per alive g_objects entry] as a sibling chain (the engine's
    // root child-list); a slice of g_sceneNodes are nested as children of the root
    // node to exercise the child recursion with real node ids.
    int aliveObjects = 0;
    for (int i = 0; i < sim::kObjectCapacity; ++i)
        if (sim::g_objects[i].alive) ++aliveObjects;

    int sceneNodes = 0;
    for (int i = 0; i < sim::g_sceneNodeCount && i < sim::kSceneNodeCapacity; ++i)
        if (sim::g_sceneNodes[i].type != 0) ++sceneNodes;

    std::printf("[wire_scene_bridge_e2e] AUGSBURG: alive objects=%d, scene nodes=%d\n",
                aliveObjects, sceneNodes);
    CHECK(aliveObjects > 0);

    // Cap the tree so the draw-list pool stays bounded but representative.
    const int kCap = 64;
    int objNodes = aliveObjects < kCap ? aliveObjects : kCap;
    int scnNodes = sceneNodes < 16 ? sceneNodes : 16;
    int total = objNodes + scnNodes;

    std::vector<SceneDrawNode> nodes(total);
    std::vector<NodeGeom>      geom(total);

    // Object nodes: type 3 (mask 0x01) so the walk admits them.
    int ni = 0;
    for (int i = 0; i < sim::kObjectCapacity && ni < objNodes; ++i) {
        if (!sim::g_objects[i].alive) continue;
        geom[ni].Build(sim::g_objects[i].id);
        nodes[ni] = SceneDrawNode{};
        nodes[ni].nodeType = 3;
        nodes[ni].cullByte = 0;
        nodes[ni].polys = &geom[ni].poly;
        nodes[ni].polyCount = 1;
        nodes[ni].texSortId = &geom[ni].texSort;
        ++ni;
    }
    // Scene nodes: nested as children of the FIRST object node (real ids).
    int firstScene = ni;
    for (int i = 0; i < sim::g_sceneNodeCount && ni < total; ++i) {
        if (sim::g_sceneNodes[i].type == 0) continue;
        geom[ni].Build(sim::g_sceneNodes[i].id);
        nodes[ni] = SceneDrawNode{};
        nodes[ni].nodeType = 3;
        nodes[ni].cullByte = 0;
        nodes[ni].polys = &geom[ni].poly;
        nodes[ni].polyCount = 1;
        nodes[ni].texSortId = &geom[ni].texSort;
        ++ni;
    }
    int builtScene = ni - firstScene;

    // Link the object nodes into a null-terminated sibling chain (root child-list)
    // so every real object node is visited (the engine's +528 bit0 sentinel would
    // exclude the marked node; null-termination visits all content nodes).
    for (int i = 0; i < objNodes; ++i)
        nodes[i].nextSibling = (i + 1 < objNodes) ? &nodes[i + 1] : nullptr;

    // Nest the scene nodes as children of node 0 (null-terminated sibling chain).
    if (builtScene > 0) {
        nodes[0].firstChild = &nodes[firstScene];
        for (int i = firstScene; i < total; ++i)
            nodes[i].nextSibling = (i + 1 < total) ? &nodes[i + 1] : nullptr;
    }

    // --- a shared draw-list pool/context builder ---
    std::vector<render::DrawListEntry> pool(total + 8);
    auto makeCtx = [&](SceneBridgeContext& c) {
        std::memset(pool.data(), 0, sizeof(render::DrawListEntry) * pool.size());
        c = SceneBridgeContext{};
        c.out = render::DrawList{pool.data(), 0, (i32)pool.size()};
        c.appendCtx.mode = render::NodeAppendMode::Software;
        c.appendCtx.baseKey = 1;
    };

    // --- INERT walk (bridge not installed) ---
    UninstallRealSceneBridge();
    SceneBridgeContext inert;
    makeCtx(inert);
    char r1 = WalkSceneTree(&nodes[0], /*walkMask=*/0x1FF, inert);
    CHECK_EQ(r1, (char)1);
    CHECK_EQ(inert.nodesVisited, total);
    CHECK_EQ(inert.nodesDispatched, total);
    CHECK_EQ(inert.polysAppended, 0);    // inert dispatch draws nothing

    // --- REAL walk (bridge installed) ---
    InstallRealSceneBridge();
    CHECK(RealSceneBridgeInstalled());
    SceneBridgeContext real;
    makeCtx(real);
    char r2 = WalkSceneTree(&nodes[0], /*walkMask=*/0x1FF, real);
    CHECK_EQ(r2, (char)1);
    CHECK_EQ(real.nodesVisited, total);
    CHECK_EQ(real.nodesDispatched, total);
    CHECK_EQ(real.polysAppended, total); // every real node drew one visible poly
    CHECK_EQ(real.out.count, total);
    CHECK(real.polysAppended > inert.polysAppended);

    std::printf("[wire_scene_bridge_e2e] walked %d nodes (%d object + %d scene); "
                "REAL dispatched %d / appended %d polys, INERT appended %d\n",
                total, objNodes, builtScene, real.nodesDispatched,
                real.polysAppended, inert.polysAppended);

    UninstallRealSceneBridge();
    io::VfsShutdown();
}

#include "test.h"

// =============================================================================
// GUARDED real-asset e2e: the LIVE scene-graph SPLICE over REAL Objects.BIN members.
//
//   1. mount the real game assets (Gilde.INI + Resources/*.BIN), bind the VFS,
//   2. install the REAL object-attach wiring (InstallRealObjectAttachWiring): the
//      whole VIBE_Object_AttachToUniverseNode @0x5b3e30 chain runs live, INCLUDING the
//      now-real scene_link splice (objSetParent / objLinkIntoScene -> render::SetParent
//      / LinkIntoScene),
//   3. register several REAL parsed .bgf members from Objects.BIN into the stock-object
//      registry (so the attach's resident gate passes — exactly the mesh_stock_object
//      e2e path), then ObjectAttachToUniverseNode each one as a ROOT node (parent=0 ->
//      LinkIntoScene prepends it as the scene-list head),
//   4. WALK the built scene with render::WalkAndInvoke (UniverseRoot.childHead =
//      sentinel->nextSibling, env.listTerminator = the LiveScene sentinel) and assert
//      EVERY attached object's shadow node is visited — proving the splice produced a
//      real traversable tree (not just observer counts).
//
// GUARDED: clean skip (zero checks) when the real game dir / no parseable member is
// present. The shipped models are predominantly the AGF/script chunk variant rather
// than the fast-chunk magic the buffer reader handles; if fewer than two members parse
// with geometry the test reports and returns cleanly (never fakes a mesh — rule 8).
// Honors GUILD_GAME_DIR.
// =============================================================================
#include "app/real_boot.h"
#include "io/vfs.h"
#include "render/mesh_asset.h"
#include "render/mesh_load.h"
#include "render/mesh_stock_object.h"
#include "render/scene_link.h"
#include "render/scene_walk.h"
#include "sim/object_attach_wiring.h"
#include "sim/object_lifecycle10.h"
#include "shim_impl/disk_filesystem.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/guild-1400/reimpl/europe_guild_1400_original";
}

bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Gilde.INI");
}

// A real parsed mesh + its registry key, found by scanning Objects.BIN.
struct RealMember {
    std::string        key;
    render::Mesh       mesh;
};

// The mesh the registry's loadMesh hook serves for the currently-attaching key.
render::Mesh g_serveMesh;
bool         g_haveServe = false;
bool ServeReal(const char* /*path*/, const std::string& name, render::Mesh& out) {
    if (!g_haveServe) return false;
    out = g_serveMesh;
    if (out.name.empty()) out.name = name;
    return true;
}

// Scan Objects.BIN for up to `want` .bgf members that parse with geometry.
std::vector<RealMember> FindRealBgfMembers(app::RealGameAssets& assets, int want) {
    std::vector<RealMember> found;
    io::ArchiveMount* objs = nullptr;
    for (auto& a : assets.archives)
        if (a.mounted && a.mount &&
            a.name.find("Objects") != std::string::npos) { objs = a.mount.get(); break; }
    if (!objs) return found;

    int tried = 0;
    for (const auto& m : objs->members()) {
        std::string n = m.name;
        for (auto& ch : n) ch = (char)std::tolower((unsigned char)ch);
        if (n.size() < 4 || n.compare(n.size() - 4, 4, ".bgf") != 0)
            continue;
        if (++tried > 512) break;
        std::vector<u8> bytes;
        if (!objs->OpenMember(m.name.c_str(), bytes) || bytes.empty())
            continue;
        std::string key = m.name;
        auto slash = key.find_last_of("/\\");
        if (slash != std::string::npos) key = key.substr(slash + 1);
        auto dot = key.find_last_of('.');
        if (dot != std::string::npos) key = key.substr(0, dot);

        render::Mesh mesh;
        if (!render::LoadBgfFile(bytes.data(), bytes.size(), key, mesh))
            continue;
        if (mesh.polyCount <= 0 || mesh.vertexCount <= 0)
            continue;
        found.push_back({key, std::move(mesh)});
        if (static_cast<int>(found.size()) >= want) break;
    }
    return found;
}

}  // namespace

TEST(SceneLinkChain, E2eRealMembersSpliceAndWalk) {
    if (!RealAssetsPresent()) {
        std::printf("    [skip] real game dir absent (set GUILD_GAME_DIR)\n");
        return;
    }
    shim::DiskFileSystem fs(GameDir());
    app::RealGameAssets assets =
        app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI");
    if (!assets.vfsBound) {
        std::printf("    [skip] VFS not bound\n");
        return;
    }

    std::vector<RealMember> members = FindRealBgfMembers(assets, 3);
    if (members.size() < 2) {
        std::printf("    [skip] fewer than 2 fast-chunk .bgf members parsed (shipped "
                    "models are the AGF/script chunk variant)\n");
        return;
    }

    // Install the live attach wiring (real splice) + a real-mesh loadMesh hook so each
    // attach registers its stock object and the resident gate passes.
    sim::InstallRealObjectAttachWiring();
    sim::ObjectAttachWiringResetLiveScene();
    render::StockObjectHooksMut().loadMesh = &ServeReal;

    // Attach each real member as a ROOT node (parent=0 -> LinkIntoScene splice).
    std::vector<render::SceneNode*> attachedShadows;
    int attachedCount = 0;
    void* firstRoot = nullptr;               // first attached SceneNode10 (for the parent path)
    float pos[3]   = {100.0f, 0.0f, 200.0f};
    float xlate[3] = {0.0f, 0.0f, 0.0f};
    for (auto& mem : members) {
        g_serveMesh = mem.mesh;
        g_haveServe = true;
        void* node = sim::ObjectAttachToUniverseNode(
            /*parent=*/nullptr, pos, mem.key.c_str(), xlate, /*ctx=*/nullptr);
        if (!node) {
            std::printf("    [info] '%s' did not attach (no resident submeshes)\n",
                        mem.key.c_str());
            continue;
        }
        ++attachedCount;
        if (!firstRoot) firstRoot = node;
        render::SceneNode* shadow =
            sim::ObjectAttachWiringShadowFor(reinterpret_cast<sim::SceneNode10*>(node));
        CHECK(shadow != nullptr);
        if (shadow) attachedShadows.push_back(shadow);
    }

    if (attachedCount < 2) {
        std::printf("    [skip] fewer than 2 members reached LinkIntoScene "
                    "(attach resident gate)\n");
        render::StockObjectHooksMut().loadMesh = &render::Mesh_LoadByName;
        sim::ObjectAttachWiringResetLiveScene();
        return;
    }

    // Each root attach fired the REAL LinkIntoScene splice.
    CHECK_EQ(sim::ObjectAttachWiringLinkIntoSceneCalls(), attachedCount);
    CHECK_EQ(sim::ObjectAttachWiringSetParentCalls(), 0);   // all root -> no SetParent

    // Attach one more member UNDER the first attached root (parent != 0 -> the REAL
    // SetParent splice: parent->firstChild = child, child->parent = parent), proving the
    // parent path builds a real child link the walk descends. `firstRoot` is the first
    // attached SceneNode10 captured in the loop above.
    render::SceneNode* childShadow = nullptr;
    if (firstRoot) {
        g_serveMesh = members[0].mesh;       // re-serve a known-good mesh for the child
        g_haveServe = true;
        void* childNode = sim::ObjectAttachToUniverseNode(
            /*parent=*/firstRoot, pos, members[0].key.c_str(), xlate, /*ctx=*/nullptr);
        if (childNode) {
            CHECK_EQ(sim::ObjectAttachWiringSetParentCalls(), 1);  // parent path fired
            childShadow =
                sim::ObjectAttachWiringShadowFor(reinterpret_cast<sim::SceneNode10*>(childNode));
            render::SceneNode* parentShadow = attachedShadows[0];
            CHECK(parentShadow->firstChild == childShadow);        // +508 firstChild
            CHECK(childShadow->parent == parentShadow);            // +504 parent
        }
    }

    // WALK the built scene. The next-chain runs sentinel -> oldest .. newest -> sentinel;
    // the walk descends from sentinel->nextSibling (the universe child head) and stops at
    // the sentinel (flags528 bit0 terminator).
    render::LiveScene& scene = sim::ObjectAttachWiringLiveScene();
    render::SceneNode* childHead = scene.sentinel ? scene.sentinel->nextSibling : nullptr;
    CHECK(childHead != nullptr);

    render::UniverseRoot uroot;
    uroot.childHead = childHead;
    render::SceneWalkEnv env;
    env.root = &uroot;
    env.listTerminator = scene.sentinel;

    static std::vector<render::SceneNode*> s_visited;
    s_visited.clear();
    auto cb = [](render::SceneNode* n, std::intptr_t) -> char {
        s_visited.push_back(n);
        return 1;
    };
    char rc = render::WalkAndInvoke(&uroot, /*node=*/nullptr, cb, /*walkMask=*/0x1FF,
                                    /*userArg=*/0, env);
    CHECK_EQ((int)rc, 1);

    // Every attached object's shadow node was visited exactly once; the sentinel is not.
    auto countVisits = [&](render::SceneNode* n) {
        int c = 0;
        for (auto* v : s_visited) if (v == n) ++c;
        return c;
    };
    for (auto* s : attachedShadows)
        CHECK_EQ(countVisits(s), 1);
    if (childShadow)
        CHECK_EQ(countVisits(childShadow), 1);   // the child under the first root is reached
    CHECK_EQ(countVisits(scene.sentinel), 0);
    CHECK(static_cast<int>(s_visited.size()) >= attachedCount);

    std::printf("    [info] spliced + walked %d real Objects.BIN members; "
                "%d scene nodes visited (poly counts: ", attachedCount,
                static_cast<int>(s_visited.size()));
    for (size_t i = 0; i < members.size() && i < attachedShadows.size(); ++i)
        std::printf("%s=%d%s", members[i].key.c_str(), members[i].mesh.polyCount,
                    i + 1 < members.size() ? " " : "");
    std::printf(")\n");

    // restore inert defaults for other suites.
    render::StockObjectHooksMut().loadMesh = &render::Mesh_LoadByName;
    g_haveServe = false;
    sim::ObjectAttachWiringResetLiveScene();
}

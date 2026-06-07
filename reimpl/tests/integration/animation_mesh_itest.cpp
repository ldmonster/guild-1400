#include "test.h"

// Integration: drive animation_mesh's VIBE_Mesh_SetActiveTexturePath /
// VIBE_Mesh_BuildTexturePath against the REAL reconstructed VFS sibling
// (io/vfs_tree.cpp: VIBE_Vfs_NormalizeDirPath @0x44f88c and
// VIBE_Vfs_ResolveAndBuildPath @0x4500a0). The AnimMeshHooks normalizeDirPath /
// resolvePath slots ARE those two VFS functions in the live binary (the hook
// comments name them); we forward them into a REAL VfsNode directory tree built
// with the real GetOrCreateSubDir / AddFileSorted / BuildFinishedFileList leaves,
// exactly as the engine wires the texture-path resolver, and assert the path math
// lands on the right real tree nodes / resolved file.
//
// (inflateGeometry / precacheTexture / frameProcess have no exported reconstructed
// sibling, so the recursion / accumulate / TXS leaves below run on the module's
// inert default-hook path — noted at each test.)
#include "render/animation_mesh.h"
#include "io/vfs_tree.h"          // REAL reconstructed sibling: VFS tree
#include "shim/IFileSystem.h"

#include <cstring>

using namespace guild;

namespace {

// The real VFS tree the hooks resolve against (built per test). The
// NormalizeDirPath sibling uppercases path components when NOT case-insensitive
// (the default), so directory nodes are created with UPPERCASE names.
io::VfsNode* g_texRoot = nullptr;
io::VfsNode* g_lastResolvedDir = nullptr;

// animation_mesh normalizeDirPath hook -> REAL VIBE_Vfs_NormalizeDirPath.
// MeshSetActiveTexturePath stores the returned context handle and returns it; we
// hand back the matched VfsNode* as the opaque int context, exactly as the engine
// stores dword_1406110.
int NormalizeHook(const char* dir) {
    io::VfsNode* n = io::NormalizeDirPath(dir, g_texRoot);
    g_lastResolvedDir = n;
    return static_cast<int>(reinterpret_cast<intptr_t>(n) & 0x7fffffff);
}

// animation_mesh resolvePath hook -> REAL VIBE_Vfs_ResolveAndBuildPath.
char g_resolveOut[272];
const char* ResolveHook(const char* name) {
    // animation_mesh prefixes the texture-VFS marker '*'; the real path resolver
    // expects a plain dir/file path, so peel the marker before forwarding (the
    // engine's VFS layer strips the texture-set context marker the same way).
    if (name && name[0] == '*') ++name;
    char* r = io::ResolveAndBuildPath(name, g_texRoot, g_resolveOut, false);
    return r;  // nullptr when the asset is unknown (faithful miss path)
}

} // namespace

// MeshSetActiveTexturePath("textures/stone/") forwards into the real
// NormalizeDirPath, which walks the real dir tree TEXTURES -> STONE and returns
// the deepest node. Assert the returned context is that real node, and a missing
// component yields the real "not found" (null) result.
TEST(AnimationMeshItest, SetActiveTexturePathWalksRealVfsTree) {
    // Build a real directory tree with the real GetOrCreateSubDir sibling.
    io::VfsNode* root    = io::GetOrCreateSubDir("ROOT", nullptr);
    io::VfsNode* tex     = io::GetOrCreateSubDir("TEXTURES", root);
    io::VfsNode* stone   = io::GetOrCreateSubDir("STONE", tex);
    CHECK(root  != nullptr);
    CHECK(tex   != nullptr);
    CHECK(stone != nullptr);
    g_texRoot = root;
    g_lastResolvedDir = nullptr;

    render::AnimMeshHooks h{};
    h.normalizeDirPath = &NormalizeHook;   // -> real VIBE_Vfs_NormalizeDirPath
    render::AnimMeshSetHooks(h);

    int ctx = render::MeshSetActiveTexturePath("textures/stone/");
    // The real sibling matched the deepest dir node.
    CHECK(g_lastResolvedDir == stone);
    if (stone) {
        int want = static_cast<int>(reinterpret_cast<intptr_t>(stone) & 0x7fffffff);
        CHECK_EQ(ctx, want);
    }

    // A path whose component is absent resolves to the real null (no node).
    int miss = render::MeshSetActiveTexturePath("textures/wood/");
    CHECK_EQ(miss, 0);
    CHECK(g_lastResolvedDir == nullptr);

    io::FreeNodeTree(root);
    render::AnimMeshResetHooks();
    g_texRoot = nullptr;
}

// MeshBuildTexturePath assembles "*"+a1+a2 and forwards it into the real
// ResolveAndBuildPath. Build a real tree with one finalized file under TEX/, then
// assert the assembled "*..." string and that the real resolver finds (and renders
// the full path of) the existing file but misses a non-existent one.
TEST(AnimationMeshItest, BuildTexturePathResolvesViaRealVfs) {
    io::VfsNode* root = io::GetOrCreateSubDir("ROOT", nullptr);
    io::VfsNode* tex  = io::GetOrCreateSubDir("TEX", root);
    CHECK(root != nullptr);
    CHECK(tex  != nullptr);
    // Attach a real file leaf, then flatten to the finalized sorted file array.
    io::AddFileSorted("WALL.BGF", tex, 0, nullptr, 0, 0, 0);
    io::BuildFinishedFileList(root, /*recurse=*/true);
    g_texRoot = root;

    render::AnimMeshHooks h{};
    h.resolvePath = &ResolveHook;   // -> real VIBE_Vfs_ResolveAndBuildPath
    render::AnimMeshSetHooks(h);

    // First verify the pure string assembly ("*"+a1+a2) the module builds.
    char out[272];
    const char* resolved = render::MeshBuildTexturePath(out, "TEX/", "WALL.BGF");
    CHECK_EQ(std::strncmp(out, "*TEX/WALL.BGF", 13), 0);
    // The real ResolveAndBuildPath found the finalized file and rendered a path.
    CHECK(resolved != nullptr);
    if (resolved) CHECK(std::strstr(resolved, "WALL.BGF") != nullptr);

    // A name with no matching asset resolves to the real miss (nullptr).
    char out2[272];
    const char* missing = render::MeshBuildTexturePath(out2, "TEX/", "NOPE.BGF");
    CHECK(missing == nullptr);

    io::FreeNodeTree(root);
    render::AnimMeshResetHooks();
    g_texRoot = nullptr;
}

// Pure-leaf path (no reconstructed sibling involved — exercises the module's own
// math + the .TXS serializer/deserializer round-trip). MeshAccumulateAabbRecursive
// folds a 2-node tree; SaveTextureSet/LoadTextureSet round-trips a grid. These run
// entirely on the module's inert default hooks (none are touched here).
TEST(AnimationMeshItest, AabbAndTxsRoundTripOnDefaultPath) {
    render::AnimMeshResetHooks();  // inert defaults (untouched by these leaves)

    render::AabbNode child{};
    child.hasMesh = true;
    for (int i = 0; i < 8; ++i) {
        child.corners[i].x = (i & 1) ? 9.0f : 3.0f;
        child.corners[i].y = (i & 2) ? 8.0f : 2.0f;
        child.corners[i].z = (i & 4) ? 7.0f : 1.0f;
    }
    render::AabbNode root{};
    root.hasMesh = true;
    for (int i = 0; i < 8; ++i) {
        root.corners[i].x = (i & 1) ? 1.0f : -1.0f;
        root.corners[i].y = (i & 2) ? 1.0f : -1.0f;
        root.corners[i].z = (i & 4) ? 1.0f : -1.0f;
    }
    root.firstChild = &child;

    render::Aabb box;
    box.mn[0] = box.mn[1] = box.mn[2] = render::kAabbSeedMin;
    box.mx[0] = box.mx[1] = box.mx[2] = render::kAabbSeedMax;
    render::MeshAccumulateAabbRecursive(box, &root);
    CHECK_EQ(box.mn[0], -1.0f);
    CHECK_EQ(box.mx[0], 9.0f);
    CHECK_EQ(box.mn[2], -1.0f);
    CHECK_EQ(box.mx[2], 7.0f);

    // .TXS grid round-trip through the real serializer leaves.
    render::MeshTextureSet ts{};
    ts.rows = 2; ts.cols = 2;
    std::strcpy(ts.at(0, 0), "a.bmp");
    std::strcpy(ts.at(0, 1), "b.bmp");
    std::strcpy(ts.at(1, 0), "c.bmp");
    std::strcpy(ts.at(1, 1), "d.bmp");
    u8 buf[4096];
    std::size_t n = render::SaveTextureSet(ts, buf, sizeof buf);
    CHECK(n > 0);

    render::MeshTextureSet back{};
    bool ok = render::LoadTextureSet(buf, n, back);
    CHECK(ok);
    CHECK_EQ(back.rows, 2);
    CHECK_EQ(back.cols, 2);
    if (back.rows == 2 && back.cols == 2) {
        CHECK_EQ(std::strcmp(back.at(0, 0), "a.bmp"), 0);
        CHECK_EQ(std::strcmp(back.at(1, 1), "d.bmp"), 0);
    }
}

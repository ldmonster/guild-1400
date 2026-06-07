// End-to-end flow across the animation/mesh loader + AABB leaves:
//   1. set the active texture-set dir context (SetActiveTexturePath)
//   2. resolve a LOD .bgf filename via the mode-2 probe (BuildLodFileName ->
//      BuildTexturePath -> resolvePath hook)
//   3. build a small scene-graph, inflate it (ApplyTransformRecursive), and
//      accumulate / test its world AABB across two mesh leaves.
//   4. save+reload the resolved texture set (.TXS round-trip).
#include "test.h"
#include "render/animation_mesh.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {
// A tiny virtual file system: a name "resolves" iff it is in the set.
std::vector<std::string> g_files;
int g_dirCtx = 0;

int NormalizeDir(const char* dir) {
    // pretend the dir context is its length; nonzero proves it ran.
    g_dirCtx = (int)std::strlen(dir) + 1;
    return g_dirCtx;
}
const char* Resolve(const char* p) {
    for (auto& f : g_files)
        if (f == p) return p;
    return nullptr;
}

// inflate hook: tag each visited node so we can verify DFS coverage.
int g_visitOrder = 0;
char Inflate(AabbNode* n) {
    // stash visit index in firstChild-less spare? use a side table instead.
    (void)n;
    ++g_visitOrder;
    return 1;
}
} // namespace

TEST(AnimMeshE2E, LoadResolveAndBound) {
    AnimMeshResetHooks();
    g_files.clear();
    g_visitOrder = 0;
    g_dirCtx = 0;

    AnimMeshHooks h;
    h.normalizeDirPath = &NormalizeDir;
    h.resolvePath      = &Resolve;
    h.inflateGeometry  = &Inflate;
    AnimMeshSetHooks(h);

    // 1. set the active texture path context.
    int ctx = MeshSetActiveTexturePath("models/lod/");
    CHECK(ctx != 0);

    // 2. LOD mode-2 probe. Only "*chair_1.bgf" exists, so the first probe
    //    (index v7-1 == 1 -> "chair_1") resolves immediately.
    g_files.push_back("*chair_1.bgf");
    char name[272];
    char r = MeshBuildLodFileName("chair", nullptr, name, 0, nullptr, (signed char)2);
    CHECK_EQ((int)r, 1);
    CHECK(std::strcmp(name, "chair_1") == 0);

    // 3. build scene graph: root -> {meshA -> {meshA_child}, meshB}
    const float ca[8][3] = {
        {0,0,0},{3,0,0},{0,3,0},{0,0,3},{3,3,0},{3,0,3},{0,3,3},{3,3,3}};
    const float cb[8][3] = {
        {-2,-2,-2},{-1,-2,-2},{-2,-1,-2},{-2,-2,-1},
        {-1,-1,-2},{-1,-2,-1},{-2,-1,-1},{-1,-1,-1}};

    // The accumulate/test recursion descends only through MESH nodes (the
    // original gates the child loop inside the `if (mesh)` block). So make the
    // root a mesh whose child is the second mesh: root(meshA) -> meshB -> child.
    AabbNode child, meshA, meshB;
    meshA.hasMesh = true;
    meshB.hasMesh = true;
    child.hasMesh = false;
    for (int i = 0; i < 8; ++i) {
        meshA.corners[i] = {ca[i][0], ca[i][1], ca[i][2]};
        meshB.corners[i] = {cb[i][0], cb[i][1], cb[i][2]};
    }
    meshA.firstChild = &meshB;
    meshB.firstChild = &child;

    // inflate the whole tree (ApplyTransformRecursive descends unconditionally).
    MeshApplyTransformRecursive(&meshA);
    CHECK_EQ(g_visitOrder, 3);             // meshA, meshB, child

    // accumulate the world AABB across both mesh leaves.
    Aabb box;
    box.mn[0]=box.mn[1]=box.mn[2]=kAabbSeedMin;
    box.mx[0]=box.mx[1]=box.mx[2]=kAabbSeedMax;
    MeshAccumulateAabbRecursive(box, &meshA);
    // union of meshA (0..3) and meshB (-2..-1) on each axis -> [-2,3].
    CHECK_EQ(box.mn[0], -2.f); CHECK_EQ(box.mx[0], 3.f);
    CHECK_EQ(box.mn[1], -2.f); CHECK_EQ(box.mx[1], 3.f);
    CHECK_EQ(box.mn[2], -2.f); CHECK_EQ(box.mx[2], 3.f);

    // overlap test against a query that straddles only meshA's box.
    Aabb q;
    q.mn[0]=q.mn[1]=q.mn[2]= 1.f;
    q.mx[0]=q.mx[1]=q.mx[2]= 5.f;
    Aabb grown;
    grown.mn[0]=grown.mn[1]=grown.mn[2]=kAabbSeedMin;
    grown.mx[0]=grown.mx[1]=grown.mx[2]=kAabbSeedMax;
    int hit = MeshTestAabbOverlapRecursive(q, &meshA, &grown);
    // meshA is a mesh -> 0, AND children -> 0.
    CHECK_EQ(hit, 0);
    // meshA overlaps q (grows toward [0,3]).
    CHECK_EQ(grown.mn[0], 0.f); CHECK_EQ(grown.mx[0], 3.f);

    // 4. .TXS round trip of a 1x2 set naming the resolved LOD.
    MeshTextureSet ts;
    ts.rows = 1; ts.cols = 2;
    std::memset(ts.at(0,0), 0, 64);
    std::memset(ts.at(0,1), 0, 64);
    std::strcpy(ts.at(0,0), name);          // "chair_1"
    std::strcpy(ts.at(0,1), "chair_diffuse");

    u8 buf[2048];
    std::size_t n = SaveTextureSet(ts, buf, sizeof(buf));
    CHECK_EQ((int)n, 12 + 1 * 2 * 64);
    MeshTextureSet ld;
    bool ok = LoadTextureSet(buf, n, ld);
    CHECK(ok);
    if (ok) {
        CHECK_EQ(ld.rows, 1);
        CHECK_EQ(ld.cols, 2);
        CHECK(std::strcmp(ld.at(0,0), "chair_1") == 0);
        CHECK(std::strcmp(ld.at(0,1), "chair_diffuse") == 0);
    }

    AnimMeshResetHooks();
}

#include "render/object_transform_ops.h"
#include "util/transform.h"
#include "util/matrix.h"

#include <cstring>

namespace guild::render {

// ---------------------------------------------------------------------------
// 0x5f0b18 — VIBE_SceneGraph_RemoveMeshFromTree
//
// The original:
//   if (a2 && a1)
//       a1 = WalkAndInvoke(off_649D64, a1, RemoveMeshRecursive,
//                          *(u16*)(a2+68) | 0x280, a2);
//   return (char)a1;
//
// RemoveMeshRecursive is the WalkAndInvoke callback; its __usercall ABI is
//   (obj@eax, cell@edx) where `cell` is the walk's userArg (a5 == a2). So the
// callback removes the *currently visited* node's mesh from the region cell a2.
// We adapt that to render::WalkAndInvoke's portable callback signature
// (char(SceneNode*, intptr_t)) by threading the region cell + the mem pools
// through a small call context (the original carried `a5`=cell as the userArg and
// the pools as the file-scope dword_64A7D0/64A7CC globals).
// ---------------------------------------------------------------------------
namespace {
struct RemoveCtx {
    SceneCell* regionCell = nullptr;
    const SceneCellPools* pools = nullptr;
};
// One in-flight removal at a time (matches the original's single-threaded
// scene-graph walk; mirrors the global-pool dependency of RemoveMeshRecursive).
thread_local RemoveCtx g_removeCtx;

char RemoveMeshCallback(SceneNode* visited, std::intptr_t userArg) {
    // userArg round-trips the region cell pointer (the original's a5 == a2).
    auto* cell = reinterpret_cast<SceneCell*>(userArg);
    if (cell && g_removeCtx.pools)
        return RemoveMeshRecursive(visited, cell, *g_removeCtx.pools);
    return 1;
}
}  // namespace

char RemoveMeshFromTree(SceneNode* node, SceneCell* regionCell,
                        const SceneWalkEnv& env, const SceneCellPools& pools,
                        u16 regionMask) {
    if (!regionCell || !node)                       // 0x5f0b1e
        return static_cast<char>(reinterpret_cast<std::intptr_t>(node));

    g_removeCtx.regionCell = regionCell;
    g_removeCtx.pools = &pools;

    // walkMask = *(u16*)(regionCell+68) | 0x280            // 0x5f0b41
    i16 walkMask = static_cast<i16>(regionMask | 0x280);

    char r = WalkAndInvoke(env.root, node, &RemoveMeshCallback, walkMask,
                           reinterpret_cast<std::intptr_t>(regionCell), env);

    g_removeCtx = RemoveCtx{};
    return r;                                        // 0x5f0b20 (al == walk result)
}

// ---------------------------------------------------------------------------
// Transform math re-exports (thin; reuse util/*).
// ---------------------------------------------------------------------------
float* PointThroughBoneChain(float* frame, const float* point, float* out) {
    return guild::util::PointThroughBoneChain(frame, point, out);
}

void TransformPointPassThrough(const float point[3], float inAngles[3],
                               float outPoint[3], float outAngles[3]) {
    // 0x5b7d38 — no-parent branch of TransformPointToParent.
    outPoint[0] = point[0];
    outPoint[1] = point[1];
    outPoint[2] = point[2];
    guild::util::MatrixToEuler(inAngles);   // 0x5b7d46 (in place over inAngles)
    outAngles[0] = inAngles[0];
    outAngles[1] = inAngles[1];
    outAngles[2] = inAngles[2];
}

void TransformPointByInverseWorld(const float point[3], const float inv[16],
                                  float outPoint[3]) {
    // 0x5b7dce — point * inverse(parentWorld), the affine row layout the
    // original emits (note the y/z are computed before x is stored).
    float py = point[0] * inv[1] + point[1] * inv[5] +
               point[2] * inv[9] + inv[13];
    float pz = point[0] * inv[2] + point[1] * inv[6] +
               point[2] * inv[10] + inv[14];
    outPoint[0] = point[0] * inv[0] + point[1] * inv[4] +
                  point[2] * inv[8] + inv[12];
    outPoint[2] = pz;
    outPoint[1] = py;
}

}  // namespace guild::render

#pragma once
#include "guild/common/types.h"
#include "render/scene_walk.h"   // SceneNode, MeshCellNode, SceneCellPools (reused)
#include "mem/mempool.h"

// =============================================================================
// guild::render — VIBE_SceneGraph octree-build cluster (gilde.exe, d3_engine.c).
//
// Faithful 1:1 reconstruction of the spatial octree that the scene/light refresh
// path builds over a region's mesh nodes. The cluster:
//
//   0x5effa8  VIBE_SceneGraph_ComputeMeshNodeBounds  (world AABB of one mesh node)
//   0x5eff10  VIBE_SceneGraph_CollectMeshesInBounds  (gather mesh nodes into a cell)
//   0x5f0160  VIBE_SceneGraph_SubdivideOctree        (recursive octree subdivision)
//   0x5f05b0  VIBE_SceneGraph_BuildOctreeForRegion   (top-level build driver)
//
// The two leaf helpers it calls — VIBE_SceneGraph_AddMeshToCell @0x5efea0 and
// VIBE_SceneGraph_FreeNodeRecursive @0x5efe50 — are ALREADY reconstructed in
// render/scene_walk.{h,cpp} (AddMeshToCell / FreeNodeRecursive, on the leaner
// `SceneCell`). This cluster's octree cell carries strictly MORE fields (an AABB,
// a mesh-count gate, a region back-link), so it is modelled here as `OctreeCell`,
// and the (trivial) mesh-list push / subtree-free are replayed as file-local
// statics (`octreePushMesh` / `octreeFreeRecursive`) over `OctreeCell` to avoid an
// ODR clash with the already-present `SceneCell` twins. They mirror those byte for
// byte (same +0x30 refcount, +0x3C/0x40 list head/tail, same pool ops).
//
// OCTREE CELL block (dword indices read by the cluster; +0x1EC drawCell payload):
//   dword[0..3]  octant[4]    child cell pointers (the do/while over a1..a1+4)
//   dword[4..6]  boxMin[3]    +0x10  accumulated AABB min   (a1[4],a1[5],a1[6])
//   dword[8..10] boxMax[3]    +0x20  accumulated AABB max   (a1[8],a1[9],a1[10])
//   dword[12]    meshCount    +0x30  live-mesh count / subdivide gate (== SceneCell
//                                    refCount; AddMeshToCell increments it)
//   dword[13]    region       +0x34  build-region back pointer (v5[13] = a4)
//   dword[14]    extra        +0x38  (v5[14] = v6, the alloc's scratch ecx)
//   dword[15]    listHead     +0x3C  mesh-cell list head
//   dword[16]    listTail     +0x40  mesh-cell list tail
//   word [34]    regionMask   +0x44  region walk-mask short (v5 word[34] = a2)
//
// SCENE-NODE side (the +0x1EC drawData and per-mesh AABB the cluster reads/writes):
//   drawData+144/148/152  mesh world-AABB min   (ComputeMeshNodeBounds writes)
//   drawData+160/164/168  mesh world-AABB max
//   drawData+176          octree marker dword (BuildOctreeForRegion stamps -1)
//   drawData+260          mesh-list-present sentinel (AddMeshToCell gate)
//   node+530 bit7         "no-pivot" sign flag (gates TransformBoundingVolume)
//   node+531 bit2         mesh-in-cell present flag (AddMeshToCell gate)
//
// flt_62C178 = 0.5f  (the half-extent split factor used to bisect each axis).
// dword_13FCD1C       a 3-float origin vector (currently {0,0,0}) handed to the
//                     mesh bbox transform when the node still needs transforming.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// OctreeCell — the octree cell block (the SceneNode::drawCell payload), modelled
// with the FULL set of named fields the build cluster touches (see header note).
// octant[4]/meshCount/listHead/listTail mirror the already-present SceneCell
// (refCount == meshCount, dword[15/16] == listHead/listTail); the bounds + region
// fields are the octree-only additions.
// ---------------------------------------------------------------------------
struct OctreeCell {
    OctreeCell* octant[4] = {nullptr, nullptr, nullptr, nullptr}; // dword[0..3]
    float boxMin[3] = {0.0f, 0.0f, 0.0f};   // dword[4..6]  = +0x10
    float boxMax[3] = {0.0f, 0.0f, 0.0f};   // dword[8..10] = +0x20
    i32   meshCount = 0;                     // dword[12]    = +0x30 (== refCount)
    void* region = nullptr;                  // dword[13]    = +0x34 (a4 region)
    void* extra = nullptr;                   // dword[14]    = +0x38
    MeshCellNode* listHead = nullptr;        // dword[15]    = +0x3C
    MeshCellNode* listTail = nullptr;        // dword[16]    = +0x40
    u16   regionMask = 0;                    // word[34]     = +0x44 (a2)
};

// ---------------------------------------------------------------------------
// MeshNodeBounds hook — VIBE_Mesh_TransformBoundingVolume @0x5ad438 and
// VIBE_Mesh_ComputeBoundingBox @0x5c9e64 transform a mesh's eight box corners
// into world space; that depends on mesh asset geometry + the node transform
// (coupled GPU/asset data not modelled here). It is injected as a hook so the
// pure octree traversal stays 1:1 and testable.
//
//   transformIfNeeded == (node->flagNoPivot bit7 clear) in the original: when the
//     node's +530 sign bit is clear, the engine first re-transforms the bounding
//     volume. The hook receives that flag.
//   On success: fill boxMin[3]/boxMax[3] with the node's world AABB and return
//     non-zero. On failure (no mesh data): return 0 (ComputeMeshNodeBounds then
//     returns 0 and the node is skipped). Default hook: returns 0 (inert).
// ---------------------------------------------------------------------------
struct OctreeBuildHooks {
    // mirrors VIBE_Mesh_ComputeBoundingBox feeding the per-node AABB reduce.
    char (*computeNodeBounds)(SceneNode* node, bool transformIfNeeded,
                              float boxMin[3], float boxMax[3]) = nullptr;

    // Accessor for the node's cached world AABB stored in drawData (+144 min.x,
    // +148 min.y, +152 min.z, +160 max.x, +164 max.y, +168 max.z). Returns a
    // pointer to that 6-float block laid out as [minX,minY,minZ, maxX,maxY,maxZ]
    // (the original indexes it as v7[36],v7[38],v7[40],v7[42] for x/z min/max and
    // SubdivideOctree's depth>1 path reads +144..+168). ComputeMeshNodeBounds
    // WRITES it; CollectMeshesInBounds + the SubdivideOctree depth>1 reduce READ
    // it. Default null -> those two consumers degrade to the inert path (no slab
    // narrowing / no cached reduce), preserving control flow + side effects.
    float* (*nodeMeshAABB)(SceneNode* node) = nullptr;
};

// gilde.exe 0x5effa8 — VIBE_SceneGraph_ComputeMeshNodeBounds
//   (__usercall al=fn(node@eax, boxMin@edx, boxMax@ebx))
//   Computes the world-space AABB of one mesh node. If node+530 bit7 is clear it
//   first transforms the bounding volume (origin dword_13FCD1C). Then it reduces
//   the eight transformed box corners into [boxMin, boxMax], also caching them back
//   into the node's drawData (+144..+168). Returns 1 on success, 0 if the node has
//   no mesh box. Modelled over the injected hook (the corner reduce is byte-exact
//   in the original; here the hook yields the already-reduced min/max).
char ComputeMeshNodeBounds(SceneNode* node, float boxMin[3], float boxMax[3],
                           const OctreeBuildHooks& hooks);

// gilde.exe 0x5eff10 — VIBE_SceneGraph_CollectMeshesInBounds
//   (__usercall eax=fn(boxMin@edx, boxMax@ebx))
//   Allocates a fresh octree cell, then walks the PARENT cell's mesh list (the
//   caller's cell, passed implicitly via ecx in the original — here `parent`),
//   adding to the new cell every mesh node whose drawData AABB (+144..+168)
//   overlaps the [boxMin,boxMax] slab. If the new cell collected ≥1 mesh
//   (meshCount != 0) it is returned, else it is freed and null returned.
OctreeCell* CollectMeshesInBounds(OctreeCell* parent, const float boxMin[3],
                                  const float boxMax[3],
                                  const SceneCellPools& pools,
                                  const OctreeBuildHooks& hooks);

// gilde.exe 0x5f0160 — VIBE_SceneGraph_SubdivideOctree
//   (__usercall eax=fn(cell@eax, minDepth@edx, maxDepth@ecx, depth@ebx))
//   Recursively subdivides `cell`. First it (re)accumulates the cell's AABB from
//   its mesh list — using per-node cached drawData bounds when depth>1, else the
//   freshly computed ComputeMeshNodeBounds. Then, while depth<maxDepth and
//   minDepth<meshCount, it bisects the box at its 0.5 midpoints into four quadrant
//   child cells (CollectMeshesInBounds each). If all four children hold the SAME
//   single mesh (markers at drawData+48 equal) the split is rejected and the
//   children are freed; otherwise each non-null child is recursively subdivided.
//   Returns the last touched cell pointer (the original's `result`).
OctreeCell* SubdivideOctree(OctreeCell* cell, u32 minDepth, u32 maxDepth, u32 depth,
                            const SceneCellPools& pools, const OctreeBuildHooks& hooks);

// gilde.exe 0x5f05b0 — VIBE_SceneGraph_BuildOctreeForRegion
//   (__usercall eax=fn(treeRoot@eax, regionMask@dx, maxDepth@ecx, minDepth@ebx))
//   Top-level build: allocates the root cell, stamps its region mask (+0x44) and
//   minDepth (dword[13]); walks the universe (off_649D64) collecting every mesh of
//   the region into the root cell (WalkAndInvoke + AddMeshToCell); subdivides it;
//   then, if any mesh landed, resets every collected node's drawData+176 marker to
//   -1 and returns the root cell. If empty, frees it and returns the residual ecx
//   (0 in practice). `walkRoot` / `walkEnv` thread the existing WalkAndInvoke; the
//   per-node mark stamp is done over the collected cell's mesh list.
OctreeCell* BuildOctreeForRegion(UniverseRoot* treeRoot, u16 regionMask, u32 maxDepth,
                                 u32 minDepth, const SceneCellPools& pools,
                                 const OctreeBuildHooks& hooks, const SceneWalkEnv& walkEnv,
                                 void (*markNodeBuilt)(SceneNode* node));

} // namespace guild::render

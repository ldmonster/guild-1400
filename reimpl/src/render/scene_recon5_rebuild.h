#pragma once
#include "guild/common/types.h"
#include "render/scene_recon_octree.h"  // OctreeCell, SubdivideOctree, hooks, pools
#include "render/scene_walk.h"          // SceneNode, SceneWalkEnv, FreeNodeRecursive

// =============================================================================
// guild::render — recon5 octree region-rebuild (gilde.exe d3_engine.c).
//
//   0x5f0b4c  VIBE_SceneGraph_RebuildRegionOctree
//
// Thin orchestrator that REBUILDS the octree for one already-allocated region
// cell: free its existing octant subtree, OR bit9 (high-byte bit1) into the
// region walk-mask, re-walk the universe collecting that region's meshes back
// into the cell (WalkAndInvoke + AddMeshToCell), then re-subdivide from depth 1.
//
// It composes already-reconstructed cluster pieces:
//   * the octant free reuses the cluster's recursive subtree free
//     (render/scene_walk FreeNodeRecursive twin, exposed via the free hook),
//   * the re-collect reuses WalkAndInvoke + AddMeshToCell (SceneWalkEnv),
//   * the re-subdivide calls SubdivideOctree (render/scene_recon_octree).
//
// Original control flow (0x5f0b4c):
//   if (!cell) return cell;
//   for each octant child:  FreeNodeRecursive(child);   // do/while over a1..a1+4
//   regionMask = cell.word[34]; HIBYTE(regionMask) |= 2;
//   WalkAndInvoke(off_649D64, walkRoot, AddMeshToCell, regionMask, cell);
//   return SubdivideOctree(cell, cell.dword[13], cell.dword[14], 1);
// =============================================================================
namespace guild::render {

// Injected re-collect + subtree-free for the rebuild. `freeOctantSubtree` mirrors
// VIBE_SceneGraph_FreeNodeRecursive @0x5efe50 applied to each octant child;
// `recollectRegion` mirrors the WalkAndInvoke(... AddMeshToCell ...) pass that
// re-gathers the region's meshes into `cell` after the mask bit is set.
struct OctreeRebuildHooks {
    void (*freeOctantSubtree)(OctreeCell* child, const SceneCellPools& pools) = nullptr;
    void (*recollectRegion)(OctreeCell* cell, u16 regionMask,
                            const SceneWalkEnv& walkEnv) = nullptr;
};

// gilde.exe 0x5f0b4c — VIBE_SceneGraph_RebuildRegionOctree(cell, walk...).
// `minDepth`/`maxDepth` are cell.dword[13]/[14] in the original (read off the
// cell); modelled as the cell's `region`-adjacent fields are opaque here, the
// caller supplies the two depths the original reads from +52/+56. Returns the
// SubdivideOctree result (the rebuilt cell), or `cell` unchanged if cell==null.
OctreeCell* RebuildRegionOctree(OctreeCell* cell, u32 minDepth, u32 maxDepth,
                                const SceneCellPools& pools,
                                const OctreeBuildHooks& buildHooks,
                                const OctreeRebuildHooks& rebuildHooks,
                                const SceneWalkEnv& walkEnv);

} // namespace guild::render

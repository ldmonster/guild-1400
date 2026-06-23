#include "render/scene_recon5_rebuild.h"

namespace guild::render {

// gilde.exe 0x5f0b4c — VIBE_SceneGraph_RebuildRegionOctree.
OctreeCell* RebuildRegionOctree(OctreeCell* cell, u32 minDepth, u32 maxDepth,
                                const SceneCellPools& pools,
                                const OctreeBuildHooks& buildHooks,
                                const OctreeRebuildHooks& rebuildHooks,
                                const SceneWalkEnv& walkEnv) {
    if (!cell)
        return cell;                       // if (!a2) return result

    // for each octant child: FreeNodeRecursive(child)  (do/while over a1..a1+4)
    for (int i = 0; i < 4; ++i) {
        if (cell->octant[i] && rebuildHooks.freeOctantSubtree) {
            rebuildHooks.freeOctantSubtree(cell->octant[i], pools);
        }
        cell->octant[i] = nullptr;
    }

    // regionMask = cell.word[34]; HIBYTE(regionMask) |= 2;   (set bit 9)
    u16 regionMask = cell->regionMask;
    regionMask = (u16)(regionMask | 0x0200);   // HIBYTE |= 2  ->  bit 9

    // WalkAndInvoke(off_649D64, walkRoot, AddMeshToCell, regionMask, cell)
    if (rebuildHooks.recollectRegion) {
        rebuildHooks.recollectRegion(cell, regionMask, walkEnv);
    }
    cell->regionMask = regionMask;

    // return SubdivideOctree(cell, cell.dword[13], cell.dword[14], 1)
    return SubdivideOctree(cell, minDepth, maxDepth, 1u, pools, buildHooks);
}

} // namespace guild::render

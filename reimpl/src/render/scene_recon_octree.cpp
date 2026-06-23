#include "render/scene_recon_octree.h"
#include <new>   // placement new over the zeroed pool slot

namespace guild::render {

// ---------------------------------------------------------------------------
// File-local constants recovered from the binary.
//   flt_62C178 = 0x3F000000 = 0.5f  (octree axis split factor)
// The +1e10 / -1e10 sentinels are the exact float bit patterns 0x501502F9 /
// 0xD01502F9 the original stores; written here as the literals they denote.
// ---------------------------------------------------------------------------
static constexpr float kSplit = 0.5f;          // flt_62C178
static constexpr float kBig   = 1.0e10f;        // 0x501502F9
static constexpr float kNeg   = -1.0e10f;       // 0xD01502F9

// ---------------------------------------------------------------------------
// octreePushMesh — mirror of VIBE_SceneGraph_AddMeshToCell @0x5efea0, replayed
// over OctreeCell (the already-present render/scene_walk AddMeshToCell does the
// identical thing over the leaner SceneCell). gate: obj has a drawData with a
// mesh list (drawCell && drawCell->meshPresent) and obj+531 bit2 set; then
// ++cell->meshCount, alloc an 8-byte list node ([0]=obj), append to head/tail.
// ---------------------------------------------------------------------------
static char octreePushMesh(SceneNode* obj, OctreeCell* cell, const SceneCellPools& pools) {
    SceneCell* drawCell = obj->drawCell;                         // *(obj+492)
    if (!drawCell || !drawCell->meshPresent || (obj->flags531 & 4) == 0)
        return 1;                                                // gate failed -> noop

    ++cell->meshCount;                                           // ++*(cell+48)
    MeshCellNode* node = static_cast<MeshCellNode*>(mem::MemPoolAlloc(
        pools.cellListPool, *pools.tracker, sizeof(MeshCellNode), 128));
    node->obj = obj;                                            // [0] = obj
    node->next = nullptr;
    MeshCellNode* tail = cell->listTail;                       // *(cell+64)
    if (tail)
        tail->next = node;                                     // *(tail+4) = node
    else
        cell->listHead = node;                                 // *(cell+60) = node
    cell->listTail = node;                                     // *(cell+64) = node
    return 1;
}

// ---------------------------------------------------------------------------
// octreeFreeRecursive — mirror of VIBE_SceneGraph_FreeNodeRecursive @0x5efe50,
// replayed over OctreeCell: drain the mesh list (free each node from cellListPool,
// caching next before the free), recurse into the four octant children, then free
// the cell block from cellBlockPool. Returns the cell (unchanged).
// ---------------------------------------------------------------------------
static OctreeCell* octreeFreeRecursive(OctreeCell* cell, const SceneCellPools& pools) {
    if (!cell)
        return cell;

    MeshCellNode* listNode = cell->listHead;                   // *(cell+60)
    while (listNode) {
        MeshCellNode* next = listNode->next;                   // cache before free
        mem::MemPoolFree(pools.cellListPool, *pools.tracker, listNode);
        listNode = next;
    }
    for (int i = 0; i < 4; ++i) {
        if (cell->octant[i])
            octreeFreeRecursive(cell->octant[i], pools);
    }
    mem::MemPoolFree(pools.cellBlockPool, *pools.tracker, cell);
    return cell;
}

// ---------------------------------------------------------------------------
// allocCell — VIBE_MemPool_Alloc(&dword_64A7CC, 72, 64): a zeroed 72-byte octree
// cell block. The pool zeroes the slot, so placement-construct over it to get the
// host OctreeCell defaults (all-null/zero) exactly matching the original's zero
// init.
// ---------------------------------------------------------------------------
static OctreeCell* allocCell(const SceneCellPools& pools) {
    void* mem = mem::MemPoolAlloc(pools.cellBlockPool, *pools.tracker,
                                  sizeof(OctreeCell), 64);
    return new (mem) OctreeCell();
}

// gilde.exe 0x5effa8 — VIBE_SceneGraph_ComputeMeshNodeBounds
//
//   if (node+530 bit7 clear) TransformBoundingVolume(node, dword_13FCD1C, 0);
//   corners = ComputeBoundingBox(drawData+244, drawData+8); if (!corners) return 0;
//   reduce the 8 corners into [boxMin, boxMax]; cache back to drawData+144..168.
// The transform + 8-corner reduce are mesh-asset coupled -> the hook yields the
// already-reduced min/max (returning 0 == "no mesh box"); we then cache it back
// into drawData exactly as the original does.
char ComputeMeshNodeBounds(SceneNode* node, float boxMin[3], float boxMax[3],
                           const OctreeBuildHooks& hooks) {
    // node+530 bit7 clear -> needs (re)transform before bounding.
    const bool transformIfNeeded = (node->flagNoPivot & 0x80) == 0;

    if (!hooks.computeNodeBounds)
        return 0;   // inert default: no mesh data available -> skip (orig returns 0)

    // The original seeds min=+1e10, max=-1e10 and reduces 8 corners; the hook
    // returns the reduced result. Seed identically so a hook that only narrows is
    // still faithful.
    boxMin[0] = kBig; boxMin[1] = kBig; boxMin[2] = kBig;
    boxMax[0] = kNeg; boxMax[1] = kNeg; boxMax[2] = kNeg;

    char ok = hooks.computeNodeBounds(node, transformIfNeeded, boxMin, boxMax);
    if (!ok)
        return 0;

    // Cache the world AABB back into the node's drawData (+144..+168): the original
    // writes *(drawData+144..152)=min, *(drawData+160..168)=max. We surface those
    // through the nodeMeshAABB accessor so CollectMeshesInBounds + the depth>1
    // reduce read the same cache the original does.
    if (hooks.nodeMeshAABB) {
        float* aabb = hooks.nodeMeshAABB(node);   // [minX,minY,minZ,maxX,maxY,maxZ]
        if (aabb) {
            aabb[0] = boxMin[0]; aabb[1] = boxMin[1]; aabb[2] = boxMin[2];
            aabb[3] = boxMax[0]; aabb[4] = boxMax[1]; aabb[5] = boxMax[2];
        }
    }
    return 1;
}

// gilde.exe 0x5eff10 — VIBE_SceneGraph_CollectMeshesInBounds
//
//   cell = alloc(72); for (n in parent->meshList) if (n.drawData AABB overlaps the
//   [boxMin,boxMax] slab) AddMeshToCell(n, cell);
//   return cell->meshCount ? cell : (free(cell), 0).
// The overlap test reads the per-node cached AABB at drawData+144(min.x)/+148(min.y
// is unused here)/+152(min.z) and +160(max.x)/+168(max.z): the original tests
//   v7[36] <= boxMax.x && v7[40] >= boxMin.x && v7[38] <= boxMax.z && v7[42] >= boxMin.z
// i.e. a 2-D (x,z) slab overlap using the node's cached AABB. We thread that via a
// MeshAABB accessor on the node's drawData (asset side); modelled by reading the
// cached boxMin/boxMax the build seeded (here surfaced through the same hook path).
OctreeCell* CollectMeshesInBounds(OctreeCell* parent, const float boxMin[3],
                                  const float boxMax[3],
                                  const SceneCellPools& pools,
                                  const OctreeBuildHooks& hooks) {
    OctreeCell* cell = allocCell(pools);

    // Walk the parent cell's mesh list (the original's *(ecx+60) iterator).
    for (MeshCellNode* it = parent ? parent->listHead : nullptr; it; it = it->next) {
        SceneNode* n = it->obj;
        SceneCell* draw = n ? n->drawCell : nullptr;     // *(*v5 + 492)
        if (!draw)
            continue;

        // Slab overlap on (x,z) using the node's cached drawData AABB:
        //   nodeMin.x <= hi.x && nodeMax.x >= lo.x && nodeMin.z <= hi.z && nodeMax.z >= lo.z
        // boxMin == lo (edx/a2), boxMax == hi (ebx/a1); each is laid out {x, z, _}.
        // The cache lives at drawData+144..168 (nodeMeshAABB accessor). When the
        // accessor is absent (inert build) we keep the original's control flow but
        // cannot narrow, so every listed node is collected (documented in report).
        bool overlap = true;
        if (hooks.nodeMeshAABB) {
            const float* aabb = hooks.nodeMeshAABB(n);   // [minX,minY,minZ,maxX,maxY,maxZ]
            if (aabb) {
                overlap = aabb[0] <= boxMax[0]   // nodeMin.x <= hi.x   (v7[36] <= *a2)
                       && aabb[3] >= boxMin[0]   // nodeMax.x >= lo.x   (v7[40] >= *a1)
                       && aabb[2] <= boxMax[1]   // nodeMin.z <= hi.z   (v7[38] <= a2[2])
                       && aabb[5] >= boxMin[1];  // nodeMax.z >= lo.z   (v7[42] >= a1[2])
            }
        }
        if (overlap)
            octreePushMesh(n, cell, pools);              // VIBE_SceneGraph_AddMeshToCell
    }

    if (cell->meshCount)              // *(cell+48) != 0
        return cell;
    octreeFreeRecursive(cell, pools); // original frees the bare cell block
    return nullptr;
}

// gilde.exe 0x5f0160 — VIBE_SceneGraph_SubdivideOctree
//
// Phase 1 (meshCount>0): accumulate the cell AABB from its mesh list. When
//   depth>1 use each node's cached drawData AABB (+144..+168); else compute it
//   fresh via ComputeMeshNodeBounds. Seeds min=+1e10/max=-1e10.
// Phase 2 (depth<maxDepth && minDepth<meshCount): bisect at the box midpoints
//   (mid = lo + 0.5*(hi-lo)) on x and z into four quadrant child cells; if all
//   four hold the same single mesh (drawData+48 markers equal) reject the split
//   and free the children, else recurse into each non-null child.
OctreeCell* SubdivideOctree(OctreeCell* cell, u32 minDepth, u32 maxDepth, u32 depth,
                            const SceneCellPools& pools, const OctreeBuildHooks& hooks) {
    // ---- Phase 1: accumulate the cell's AABB over its mesh list. ----
    if (cell->meshCount > 0) {
        cell->boxMin[0] = kBig; cell->boxMin[1] = kBig; cell->boxMin[2] = kBig;
        cell->boxMax[0] = kNeg; cell->boxMax[1] = kNeg; cell->boxMax[2] = kNeg;

        if (depth > 1) {
            // Use each node's CACHED drawData AABB (+144..+168) — the original reads
            // *(drawData+144)=min.x .. *(drawData+168)=max.z and reduces into the
            // cell box. nodeMeshAABB surfaces that cache as [minX,minY,minZ,maxX,
            // maxY,maxZ]. (a1[4]=min.x via drawData+144, etc.)
            for (MeshCellNode* it = cell->listHead; it; it = it->next) {
                if (!it->obj)
                    continue;
                const float* aabb = hooks.nodeMeshAABB ? hooks.nodeMeshAABB(it->obj)
                                                       : nullptr;
                if (!aabb)
                    continue;
                if (cell->boxMin[0] >= aabb[0]) cell->boxMin[0] = aabb[0]; // +144
                if (cell->boxMin[1] >= aabb[1]) cell->boxMin[1] = aabb[1]; // +148
                if (cell->boxMin[2] >= aabb[2]) cell->boxMin[2] = aabb[2]; // +152
                if (cell->boxMax[0] <= aabb[3]) cell->boxMax[0] = aabb[3]; // +160
                if (cell->boxMax[1] <= aabb[4]) cell->boxMax[1] = aabb[4]; // +164
                if (cell->boxMax[2] <= aabb[5]) cell->boxMax[2] = aabb[5]; // +168
            }
        } else {
            // depth<=1: compute each node's AABB fresh, reducing into the cell box.
            float nMin[3], nMax[3];
            for (MeshCellNode* it = cell->listHead; it; it = it->next) {
                if (ComputeMeshNodeBounds(it->obj, nMin, nMax, hooks)) {
                    if (cell->boxMin[0] >= nMin[0]) cell->boxMin[0] = nMin[0];
                    if (cell->boxMin[1] >= nMin[1]) cell->boxMin[1] = nMin[1];
                    if (cell->boxMin[2] >= nMin[2]) cell->boxMin[2] = nMin[2];
                    if (cell->boxMax[0] <= nMax[0]) cell->boxMax[0] = nMax[0];
                    if (cell->boxMax[1] <= nMax[1]) cell->boxMax[1] = nMax[1];
                    if (cell->boxMax[2] <= nMax[2]) cell->boxMax[2] = nMax[2];
                }
            }
        }
    }

    // ---- Phase 2: subdivide while under depth + mesh-count thresholds. ----
    OctreeCell* result = cell;
    if (depth < maxDepth && minDepth < static_cast<u32>(cell->meshCount)) {
        // Half extents along x and z (the original splits only x/z; y is full span).
        const float halfX = (cell->boxMax[0] - cell->boxMin[0]) * kSplit;
        const float halfZ = (cell->boxMax[2] - cell->boxMin[2]) * kSplit;
        // (boxMax.y - boxMin.y) is computed in the original (v57) but the y span is
        // passed full to every child; the four splits are the x/z quadrants.

        // Quadrant 0: [minX .. minX+halfX] x [minZ .. minZ+halfZ]
        float q0min[3] = {cell->boxMin[0], cell->boxMin[2], 0.0f};
        float q0max[3] = {cell->boxMin[0] + halfX, cell->boxMin[2] + halfZ, 0.0f};
        cell->octant[0] = CollectMeshesInBounds(cell, q0min, q0max, pools, hooks);

        // Quadrant 1: [minX+halfX .. maxX] x [minZ .. minZ+halfZ]
        float q1min[3] = {cell->boxMin[0] + halfX, cell->boxMin[2], 0.0f};
        float q1max[3] = {cell->boxMax[0], cell->boxMin[2] + halfZ, 0.0f};
        cell->octant[1] = CollectMeshesInBounds(cell, q1min, q1max, pools, hooks);

        // Quadrant 2: [minX+halfX .. maxX] x [minZ+halfZ .. maxZ]
        float q2min[3] = {cell->boxMin[0] + halfX, cell->boxMin[2] + halfZ, 0.0f};
        float q2max[3] = {cell->boxMax[0], cell->boxMax[2], 0.0f};
        cell->octant[2] = CollectMeshesInBounds(cell, q2min, q2max, pools, hooks);

        // Quadrant 3: [minX .. minX+halfX] x [minZ+halfZ .. maxZ]
        float q3min[3] = {cell->boxMin[0], cell->boxMin[2] + halfX, 0.0f};
        float q3max[3] = {cell->boxMin[0] + halfX, cell->boxMax[2], 0.0f};
        cell->octant[3] = CollectMeshesInBounds(cell, q3min, q3max, pools, hooks);

        // Reject the split if every non-null child holds the SAME single mesh —
        // the original compares each child's *(child+48) (meshCount) marker; if all
        // equal (v70 stays 1) the children are freed instead of recursed. (The
        // decompile reads (*(LODWORD(v39)+48)) — child->meshCount.)
        bool collapse = true;     // v70
        int marker = -1;          // v37
        for (int i = 0; i < 4; ++i) {
            OctreeCell* ch = cell->octant[i];
            if (ch) {
                if (marker < 0)
                    marker = ch->meshCount;          // first child seeds the marker
                else if (marker != ch->meshCount)
                    collapse = false;                // mismatch -> keep the split
            }
        }

        for (int i = 0; i < 4; ++i) {
            OctreeCell* ch = cell->octant[i];
            if (!ch)
                continue;
            if (collapse) {
                octreeFreeRecursive(ch, pools);
                cell->octant[i] = nullptr;
            } else {
                result = SubdivideOctree(ch, minDepth, maxDepth, depth + 1, pools, hooks);
            }
        }
    }
    return result;
}

// gilde.exe 0x5f05b0 — VIBE_SceneGraph_BuildOctreeForRegion
//
//   cell = alloc(72); cell.dword[14]=ecx; cell.word[34]=regionMask; cell.dword[13]
//     = minDepth (a4);
//   WalkAndInvoke(off_649D64, treeRoot, AddMeshToCell, regionMask, cell);
//   SubdivideOctree(cell, minDepth, maxDepth, 1);
//   if (cell->meshCount) { for (n in cell->meshList) *(n.drawData+176) = -1; return cell; }
//   else { free(cell); return ecx; }
OctreeCell* BuildOctreeForRegion(UniverseRoot* treeRoot, u16 regionMask, u32 maxDepth,
                                 u32 minDepth, const SceneCellPools& pools,
                                 const OctreeBuildHooks& hooks, const SceneWalkEnv& walkEnv,
                                 void (*markNodeBuilt)(SceneNode* node)) {
    OctreeCell* cell = allocCell(pools);
    cell->regionMask = regionMask;     // word[34] = a2
    cell->region = reinterpret_cast<void*>(static_cast<std::intptr_t>(minDepth)); // dword[13] = a4

    // Walk the universe collecting every region mesh into the root cell. The
    // original calls WalkAndInvoke(off_649D64, treeRoot, AddMeshToCell, regionMask,
    // cell): AddMeshToCell @0x5efea0 (== our octreePushMesh) receives each visited
    // node as eax and the cell as the edx context (the walk's userArg). The
    // already-present WalkAndInvoke takes a non-capturing `char(*)(SceneNode*,
    // intptr_t)`, so the OctreeCell* rides in the userArg and the pools are threaded
    // through a thread-local bridge (the original reaches them via file-scope pool
    // heads dword_64A7CC/D0; we keep them re-entrant instead of global).
    struct Bridge {
        static const SceneCellPools*& poolSlot() {
            static thread_local const SceneCellPools* p = nullptr; return p; }
        static char cb(SceneNode* obj, std::intptr_t ctx) {
            return octreePushMesh(obj, reinterpret_cast<OctreeCell*>(ctx), *poolSlot());
        }
    };
    Bridge::poolSlot() = &pools;
    WalkAndInvoke(treeRoot, nullptr, &Bridge::cb,
                  static_cast<i16>(regionMask),
                  reinterpret_cast<std::intptr_t>(cell), walkEnv);

    SubdivideOctree(cell, minDepth, maxDepth, 1u, pools, hooks);

    if (cell->meshCount) {                 // *(cell+48) != 0
        // Stamp every collected node's drawData+176 marker to -1.
        for (MeshCellNode* it = cell->listHead; it; it = it->next) {
            if (markNodeBuilt && it->obj)
                markNodeBuilt(it->obj);
        }
        return cell;
    }
    octreeFreeRecursive(cell, pools);
    return nullptr;                        // original returns the residual ecx (0)
}

} // namespace guild::render

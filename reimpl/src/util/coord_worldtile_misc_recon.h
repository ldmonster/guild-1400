#pragma once
// =============================================================================
// coord_worldtile_misc_recon — VIBE_Coord_WorldToTile (gilde.exe 0x577690).
//
//   int __userpurge VIBE_Coord_WorldToTile@<eax>(
//       float *outU@<eax>, float *outV@<edx>,
//       float *worldPt@<ebx>, float *pivotChain@<ecx>, int mapCtx);
//
// Projects a world point onto the active map's 2D tile space.  The original:
//   1. transforms `worldPt` through the bone-chain pivot (`pivotChain`) into a
//      scratch vec3 via VIBE_Transform_PointThroughBoneChainPivot (0x5c8d0c);
//   2. divides the X and Z components, biased by the map origin, by the map
//      tile scale:
//        *outU = (p.x - map[+0x90]) / map[+0xA0];
//        *outV = (p.z - map[+0x98]) / map[+0xB8];
//   where `map = *(void**)mapCtx` (mapCtx's first field is the map record).
//
// The map-record field offsets are byte offsets into the ORIGINAL record.
// VIBE_Transform_PointThroughBoneChainPivot already exists in the reimpl
// (src/util/transform.cpp); here it is reached through a hook so this leftover
// file stays self-contained and link-order independent.
// =============================================================================
#include "guild/common/types.h"

namespace guild::util {

// Map record fields consulted by 0x577690 (offsets in the original record).
struct CoordMapRecord {
    float originX; // +0x90
    float originZ; // +0x98
    float scaleU;  // +0xA0
    float scaleV;  // +0xB8
};

// Transform hook: VIBE_Transform_PointThroughBoneChainPivot(pivotChain, worldPt, out[3]).
using PivotTransformFn = void (*)(const float* pivotChain, const float* worldPt, float* out3);

// gilde.exe 0x577690 — VIBE_Coord_WorldToTile.
void Coord_WorldToTile(float* outU, float* outV, const float* worldPt,
                       const float* pivotChain, const CoordMapRecord& map,
                       PivotTransformFn transform);

} // namespace guild::util

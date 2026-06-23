// =============================================================================
// coord_worldtile_misc_recon.cpp — VIBE_Coord_WorldToTile (0x577690). 1:1.
// =============================================================================
#include "util/coord_worldtile_misc_recon.h"

namespace guild::util {

// gilde.exe 0x577690 — VIBE_Coord_WorldToTile
//   call VIBE_Transform_PointThroughBoneChainPivot(pivotChain, worldPt, scratch)
//   eax = *(void**)mapCtx
//   *outU = (scratch[0] - map[+0x90]) / map[+0xA0]
//   *outV = (scratch[2] - map[+0x98]) / map[+0xB8]
void Coord_WorldToTile(float* outU, float* outV, const float* worldPt,
                       const float* pivotChain, const CoordMapRecord& map,
                       PivotTransformFn transform) {
    float scratch[4];                                        /*[esp+18h] var_18 (vec3+)*/
    transform(pivotChain, worldPt, scratch);                 /*0x5776a3*/
    *outU = (scratch[0] - map.originX) / map.scaleU;         /*0x5776a8..0x5776b9*/
    *outV = (scratch[2] - map.originZ) / map.scaleV;         /*0x5776bb..0x5776cd*/
}

} // namespace guild::util

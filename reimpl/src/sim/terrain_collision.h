#pragma once
// terrain_collision — the fixed-point terrain height-range scan + mesh-vs-terrain
// collision resolver of gilde.exe (32-bit x86, imagebase 0x400000). Faithful 1:1
// ports reached from the frame loop (0x4c09a0):
//
//   0x426e2c  VIBE_Terrain_ScanLineHeightRange      (triangle height-range raster)
//   0x427370  VIBE_Terrain_ScanSegmentHeightRange   (two-triangle quad wrapper)
//   0x427b60  VIBE_Collision_ResolveMeshAgainstTerrain
//
// The shared scanline primitive (0x426da0 VIBE_Terrain_ScanRowHeightRange) is
// already reconstructed in render/terrain_scan.{h,cpp}; we REUSE it (extern) so
// there is exactly one definition of the row fold in the unified build.
//
// The geometry is fixed-point: every world->grid coordinate is truncated toward
// zero by VIBE_Coord_ConvertX (0x5c6b08) before being stored as an int. We route
// every float->int site through guild::util::ConvertX (truncate) to preserve the
// original's exact rounding (NOT round-to-nearest). See the .cpp for the per-site
// provenance addresses.
//
// ResolveMeshAgainstTerrain touches the scene-graph / object-transform leaves
// (SetPosition, ReparentWithTransform, Mesh_ComputeHeightRange, …) which live in
// other modules; those are routed through TerrainCollisionHooks with inert
// defaults. Tests install their own. The deterministic geometry core (the scan +
// the 4-corner min/max picks) is exercised directly.

#include "guild/common/types.h"

namespace guild::sim {

// ===========================================================================
// Terrain grid descriptor as the scan functions index it. Field offsets are the
// original byte offsets off the terrain-root pointer `a1`:
//   +0x00   width    (square grid edge; height == width)
//   +0x10   heights  (width*width elevation bytes, row-major)  [used by row scan]
//   +0x90   originX  (144)  world X of grid origin
//   +0x94   heightBase (148)  world Y added to an elevation byte
//   +0x98   originZ  (152)  world Z of grid origin
//   +0xA0   scaleX   (160)  world units per grid column (X)
//   +0xB8   scaleZ   (184)  world units per grid row    (Z)
//   +0xC4   heightScale (196) world Y per elevation byte
// ===========================================================================
struct TerrainGrid {
    i32       width;        // +0x00
    i32       _pad04;       // +0x04
    i32       _pad08;       // +0x08
    i32       _pad0c;       // +0x0C
    const u8* heights;      // +0x10
    // The scan reads scalar floats off the same base; we keep them as explicit
    // fields so a synthetic grid is trivial to build. The runtime view aliases
    // the original record via the byte offsets above.
    float originX;          // +0x90
    float heightBase;       // +0x94
    float originZ;          // +0x98
    float scaleX;           // +0xA0
    float scaleZ;           // +0xB8
    float heightScale;      // +0xC4
};

// gilde.exe 0x426e2c — VIBE_Terrain_ScanLineHeightRange.
// Projects the three world-space triangle vertices `v0`,`v1`,`v2` (each a float*
// laid out {x, _, z}) into grid space (truncating coords toward zero), sorts them
// by row ascending, clips against the grid, then walks the triangle edge-by-edge
// calling ScanRowHeightRange for every covered scanline to fold the cell
// elevations into a [min,max] range. On a non-clipped hit it writes the world-Y
// of the min elevation to *lowOut and the world-Y of the max to *highOut, and
// returns 0; on a fully-clipped triangle it returns 1 and leaves the outputs
// untouched.
int ScanLineHeightRange(const TerrainGrid* grid, float* lowOut, const float* v0,
                        float* highOut, const float* v1, const float* v2);

// gilde.exe 0x427370 — VIBE_Terrain_ScanSegmentHeightRange.
// Splits a quad (corners a3,a6 with the shared diagonal to a5,a7) into two
// triangles, scans each via ScanLineHeightRange, and merges the results:
//   - both triangles clipped         -> return 1 (no surface under the quad)
//   - exactly one clipped            -> copy the other's range, return 0
//   - both hit                       -> *lowOut=min, *highOut=max, return 0
int ScanSegmentHeightRange(const TerrainGrid* grid, float* lowOut, const float* a3,
                           float* highOut, const float* a5, const float* a6,
                           const float* a7);

// ===========================================================================
// Cross-module object/scene leaves used only by ResolveMeshAgainstTerrain. Records
// are raw pointers (the original dereferences *(_TYPE*)(base+off)). nullptr/0 model
// an inert world; the host installs the real scene-graph wiring.
// ===========================================================================
struct TerrainCollisionHooks {
    // VIBE_Object_ReparentWithTransform(node, newParent, ref)  (0x5b7e54)
    void (*reparentWithTransform)(int node, int newParent, int ref);
    // VIBE_Object_SetWorldTranslationXYZ(node, x, y, z)        (0x5af5cc)
    void (*setWorldTranslationXYZ)(int node, int x, int y, int z);
    // VIBE_Object_SetPositionXYZ(node, x, y, z)                (0x5af3ec)
    void (*setPositionXYZ)(int node, int x, int y, int z);
    // VIBE_Object_SetPosition(node, vec3*)                     (0x5af38c)
    void (*setPosition)(int node, const int* vec3);
    // VIBE_Object_SetWorldTranslation(node, vec3*)             (0x5af50c)
    void (*setWorldTranslation)(int node, const int* vec3);
    // VIBE_Mesh_ComputeHeightRange(node, _, outRange2f*)       (0x42698c) -> nonzero=fail
    int  (*meshComputeHeightRange)(int node, float* outLowHigh);
    // VIBE_Mesh_DrawBoundingBox(node, h, c0,c1,c2,c3)          (0x426a30) -> nonzero=fail.
    // Each corner is a float[6] {x,y,z,? } — the scan reads x at [0], z at [2].
    int  (*meshDrawBoundingBox)(int node, float h, float* c0, float* c1, float* c2,
                                float* c3);
    // VIBE_Mesh_TestAabbOverlapRecursive(ctx, node)           (0x427820) -> 1 if clear
    int  (*meshTestAabbOverlapRecursive)(const int* ctx, int node);
    // The active scene "scratch" node dword_13FCD1C: the original parks a probe
    // mesh here. We model it as an opaque handle the hooks understand.
    int  scratchNode;       // dword_13FCD1C
    // The active terrain root dword_64A028 (the TerrainGrid* the scan runs on), or
    // 0 if no terrain is loaded (forces the open-Y fallback range).
    const TerrainGrid* terrain;   // dword_64A028
    // dword_13FCD1C's transform snapshot fields are read directly off scratchNode
    // through these accessors (+76,+80,+84 position, +132,+136,+140 world-translate).
    int  (*readScratchField)(int node, int byteOff);
    // The original's apply step: *(float*)(node+0x50) = newY; *(byte*)(node+0x210) |= 4.
    // Modeled as one side-effecting call so the hook owns the field writes.
    //   (0x427dbc / 0x428382 store; 0x427dbf / 0x428385 dirty-bit OR.)
    void (*applyNodeY)(int node, float newY);
};

TerrainCollisionHooks TerrainCollisionSetHooks(const TerrainCollisionHooks* hooks);
const TerrainCollisionHooks& TerrainCollisionGetHooks();

// gilde.exe 0x427b60 — VIBE_Collision_ResolveMeshAgainstTerrain.
//   a1 = scene node, a2 = "apply" flag, a3 = "use max edge" flag.
// Snaps a mesh down/up onto the terrain surface: computes the mesh height range,
// derives a probe height (lerp by flt_6115B8), scans the terrain under the mesh
// AABB to find the surface min/max, then (if a2) shifts the node's +0x80 Y by the
// surface delta and sets the +0x208 dirty bit. Returns 1 when nothing was applied
// (no mesh / clipped / blocked) and 0 when the node was adjusted.
int ResolveMeshAgainstTerrain(int node, int applyFlag, int useMaxEdge);

// flt_6115B8 — the probe-height lerp factor between the mesh's low and high Y.
extern const float kProbeHeightLerp;

} // namespace guild::sim

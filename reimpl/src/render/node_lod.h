#pragma once
#include "guild/common/types.h"
#include "render/geometry_types.h"   // Frustum

// =============================================================================
// guild::render — the per-node LOD select + frustum classify/cull leaves the
// ProcessSceneNode walk invokes before projecting an object. Faithful 1:1
// reconstruction of the gilde.exe (d3_engine.c) node-gate cluster:
//
//   0x5adb6c  VIBE_Mesh_SelectLodFrame            (distance/forced LOD frame pick)
//   0x5ad1f4  VIBE_Render_ClassifyBoundingBoxPlanes (8-corner frustum outcode AND/OR)
//   0x5ad588  VIBE_Render_CullNodeAgainstFrustum   (transform bbox + classify)
//
// These are the self-contained math gates. The bounding-volume transform
// (VIBE_Mesh_TransformBoundingVolume) and the node's projected-AABB accessor
// (the vtbl+500 call) are engine-coupled and supplied by the caller; the cull
// here operates on a caller-provided 8-corner world AABB.
//
// RECOVERED LAYOUTS / CONSTANTS
// ---------------------------------------------------------------------------
// SelectLodFrame:
//   object +492 (idx123) -> draw-data block; +2316 byte = LOD frame count
//   object +531 render-flags byte: bits 0x30 set OR no world -> use the FORCED
//     LOD index ((4*flags >> 6) - 1), else compute by camera distance.
//   object +76/+80/+84 = object world position; world(dword_13FCD1C)+76/80/84 = cam
//   distance LOD = sqrt(d^2) * lodCount * flt_13FC774 (1/fov screen scale), via
//     VIBE_Coord_ConvertX (x87 truncate toward zero).
//   flt_62807C = -1.0  (the "max LOD" fallback bias term, lodCount + (-1)).
//   each LOD frame is 384 bytes; result = drawData + 244 + 384*index.
//   The +8/+12 (poly count/cap) of the chosen frame must be nonzero, else 0.
//   On a successful new frame the object +528 |= 0x40 (cull/dirty) is set.
// ClassifyBoundingBoxPlanes: 8 corners (stride 20 floats == 80 bytes), 6-bit
//   outcode per corner (4 side planes + near/far z); returns AND-of-all in bit6
//   (0x40 == fully outside one plane) | OR-of-all low bits; tracks min/max z.
// =============================================================================
namespace guild::render {

// View/LOD parameters the SelectLodFrame distance path reads from file-scope
// globals, gathered for re-entrancy. `worldPresent` mirrors dword_13FCD1C != 0.
struct LodView {
    bool  worldPresent = true;  // dword_13FCD1C != 0
    float camPos[3] = {0,0,0};  // *(dword_13FCD1C + 76/80/84)
    float fovScale = 0.0f;      // flt_13FC774 (1/fov screen scale)
    bool  forceRebuild = false; // byte_64A068 (force the +528 |= 0x40 set)
};

// A single object's LOD frame (the 384-byte stride records at drawData+244). Only
// the poly count/cap the selector validates are modelled.
struct LodFrame {
    i32 polyCount = 0;  // frame+8
    i32 polyCap = 0;    // frame+12
};

// The object's LOD-relevant fields (the +492/+531/+76.. accesses). `currentFrame`
// is the object's +460 active mesh ptr index (here the index into `frames`).
struct LodObject {
    float pos[3] = {0,0,0};    // object +76/+80/+84
    u8    renderFlags = 0;     // object +531 (bits 0x30 = forced LOD)
    u8    lodCount = 0;        // drawData +2316
    bool  drawDataReady = false; // *(drawData) != 0 && *(drawData+2316) (= lodCount>0)
    const LodFrame* frames = nullptr; // drawData + 244, 384-byte stride
    i32   currentFrameIndex = -1;     // object +460 (matched against the pick)
};

// gilde.exe 0x5adb6c — VIBE_Mesh_SelectLodFrame.
//   Returns the selected LOD frame INDEX (0-based), or -1 when the object has no
//   drawable LOD or the chosen frame has zero polys (the original's `return 0` ->
//   null frame). `outSetCullBit` (optional) receives whether the object +528 |=
//   0x40 set fired (frame changed / forced). Faithful to the forced vs distance
//   branch, the lodCount-1 clamp, and the truncate-toward-zero distance LOD.
i32 SelectLodFrame(const LodObject& obj, const LodView& view, bool* outSetCullBit);

// gilde.exe 0x5ad1f4 — VIBE_Render_ClassifyBoundingBoxPlanes.
//   `corners` is the 8-corner world AABB, 8 * 3 floats (the original strided by 20
//   floats per corner reading [0],[1],[2]); pass a tightly packed 24-float array.
//   Computes the per-corner 6-bit outcode against the 4 side planes + near/far z
//   in `fr`, returns (AND-bit6 << 6) | OR-low-bits exactly as the original. When
//   not fully culled, writes the min/max corner z to *outMinZ / *outMaxZ (if
//   non-null) — the running near/far bounds the engine clamps globally.
u8 ClassifyBoundingBoxPlanes(const float* corners, const Frustum& fr,
                             float* outMinZ, float* outMaxZ);

// gilde.exe 0x5ad588 — VIBE_Render_CullNodeAgainstFrustum.
//   `obj529` is the object's +529 flags byte (bit 0x20 == "force visible": return
//   (signByte & 0x80) | 0x3F). `corners`/`fr` feed ClassifyBoundingBoxPlanes when
//   the node has a projected AABB (`hasAabb`); otherwise returns signByte | 0x40
//   (fully culled). `signByte` is the pre-existing classification byte the original
//   carried in from TransformBoundingVolume. Returns the node cull byte.
u8 CullNodeAgainstFrustum(u8 obj529, u8 signByte, bool hasAabb,
                          const float* corners, const Frustum& fr,
                          float* outMinZ, float* outMaxZ);

} // namespace guild::render

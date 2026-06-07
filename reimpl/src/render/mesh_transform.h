#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — mesh vertex transform / skin, bounding-volume reduction, and
// per-vertex colour helpers. Faithful 1:1 reconstruction of the gilde.exe
// (d3_engine.c) mesh-geometry cluster:
//
//   0x5c9d04  VIBE_Mesh_TransformPackedVertices  (skin: packed bytes -> world xyz)
//   0x5c9c58  VIBE_Mesh_TransformVertexNormals   (skin: per-vertex normal rotate)
//   0x5c9e64  VIBE_Mesh_ComputeBoundingBox       (8-corner AABB + transform)
//   0x42698c  VIBE_Mesh_ComputeHeightRange       (vegetation y-extent from AABB)
//   0x426bec  VIBE_Mesh_ComputeBoundingRadius    (centroid + farthest-corner radius)
//   0x5c5084  VIBE_Mesh_AccumulateVertexBounds   (grow an AABB by a sub-mesh's verts)
//   0x5b29d8  VIBE_Mesh_AccumulateVertexAabb     (same, scene-walk callback form)
//   0x428898  VIBE_Mesh_ResetVertexColors        (set all vtx colours to neutral 0x80)
//   0x428928  VIBE_Mesh_SetVertexColors          (set all vtx colours to b,g,r)
//
// The originals address the object/mesh/vertex records as raw byte offsets into
// opaque blocks (the IDB had no named UDTs for these). To keep the reconstruction
// 1:1 AND self-contained/testable, these functions take `void*`/`u8*` bases and
// read/write the SAME byte offsets the original machine code used. Field offsets
// are documented inline. External scene-graph/transform leaves the originals call
// (VIBE_Mesh_TransformBoundingVolume @0x5ad438, VIBE_Light_BuildObjectCache
// @0x5c8218, VIBE_Transform_ComputeBoneWorldMatrix @0x5c8fac) are declared here
// and resolved by their owning modules at link time.
//
// RECOVERED RECORD LAYOUTS (byte offsets, from the machine code)
// ---------------------------------------------------------------------------
// Object record (the `obj` base):
//   +460 ptr  meshSkin block (the skinned-vertex source; a2 of the transforms)
//   +492 ptr  drawData block (+8 -> the 8-corner transformed AABB buffer)
//   +528 byte flags (bit7 sign -> "no pivot": use world pivot dword_13FCD1C)
//   +530 byte flags (bit1 = per-vertex-colour override active)
//   +533 byte object-type (== 4 -> vegetation height path)
// Mesh/skin block (a2 in the transforms; the +460 target):
//   +0   ptr  vertex array base (80-byte Vertex stride)
//   +4   ptr  sub-block whose +492 is a free/clear vtable thunk
//   +8   i32  vertex count
//   +12  i32  sub-mesh count (AccumulateVertexBounds / colour walks)
//   +184 ptr  (a3 of TransformPackedVertices) packed-position byte source
// Per-vertex record (80-byte stride):
//   +0/+4/+8   float  world xyz (transform output)
//   +32/+36/+40 float skin-target xyz slot (the v7 / +32 cursor)
//   +44/+48/+52 float skin-target normal slot
//   +68/+69/+70 byte   B / R / G colour (the *(v+68/69/70) writes)
//   +72  ptr  per-vertex source block (+0 model pos, +12 model normal)
// =============================================================================
namespace guild::render {

// Packed-position dequant constants (the byte -> signed-axis remap):
//   flt_628CDC = -128.0   flt_628CD8 = 1/127.5 (0x3C008081)
//   -> axis = (byte + (-128)) * (1/127.5)
constexpr float kPackedBias  = -128.0f;
constexpr float kPackedScale = 0.007843137718737125f; // 0x3C008081

// Bounding-radius centroid scale: flt_6115B0 = 0.125 (== 1/8, the 8-corner mean).
constexpr float kCornerMeanScale = 0.125f;

// ---------------------------------------------------------------------------
// External leaves owned by other render modules — forward-declared, linked.
// ---------------------------------------------------------------------------
// gilde.exe 0x5c8fac — VIBE_Transform_ComputeBoneWorldMatrix (in skeleton.cpp).
float* ComputeBoneWorldMatrix(float* frame, const float* pivot, u8 isRoot, float* out);
// gilde.exe 0x5ad438 — VIBE_Mesh_TransformBoundingVolume (node_lod family).
//   Recomputes the object's 8-corner transformed AABB into +492's buffer.
int TransformBoundingVolume(void* obj, float* pivot, u8 flag);
// gilde.exe 0x5c8218 — VIBE_Light_BuildObjectCache (light module).
void BuildObjectCache(void* obj);

// ---------------------------------------------------------------------------
// Mesh skin / vertex transform.
// ---------------------------------------------------------------------------

// gilde.exe 0x5c9d04 — VIBE_Mesh_TransformPackedVertices
//   (__usercall eax=obj, edx=mesh, ebx=packedSrc)
//   Skins a mesh: builds the object's bone world matrix, then for each vertex
//   dequantises 3 packed bytes from packedSrc (the +184 byte stream) and rotates
//   them by the matrix's upper-left 3x3 into the vertex's +32 slot. If packedSrc
//   is null, invokes the mesh's +4->+492 clear thunk instead. Returns `mesh`.
//   `worldPivot` is dword_13FCD1C in the original: used as the matrix pivot when
//   the object's +528 sign byte is clear, else null is used (the bone is unpivoted).
void* TransformPackedVertices(void* obj, void* mesh, const u8* packedSrc,
                              const float* worldPivot);

// gilde.exe 0x5c9c58 — VIBE_Mesh_TransformVertexNormals (__usercall eax=obj, edx=mesh)
//   Builds the object's bone world matrix (rotation-only path, isRoot=0) and rotates
//   each vertex's source normal (*(vtx+72) +12) into the vertex's +32 slot.
//   Returns a pointer into the last vertex's source block (matching the original).
//   `worldPivot` is dword_13FCD1C (see TransformPackedVertices).
float* TransformVertexNormals(void* obj, void* mesh, const float* worldPivot);

// ---------------------------------------------------------------------------
// Bounding-volume reduction.
// ---------------------------------------------------------------------------

// gilde.exe 0x5c9e64 — VIBE_Mesh_ComputeBoundingBox (__usercall edx=mesh, ebx=matrix)
//   Two modes keyed on mesh+380:
//     branch A (mesh+380 == 0): transform the 8 corner vertices in place by the
//       4x4 matrix (vertex world xyz = M * (*(vtx+72))).
//     branch B (mesh+380 != 0): scan all sub-mesh keyframe AABBs (192-byte records
//       at +132->+348, indexed by +28), reduce to a min/max box, expand to the 8
//       corners and transform them by the matrix.
//   Returns the vertex-array base offset by the vertex count (the engine's cursor),
//   or 0 if the mesh has no vertices (mesh+8 == 0).
void* ComputeBoundingBox(void* mesh, const float* matrix);

// gilde.exe 0x42698c — VIBE_Mesh_ComputeHeightRange
//   (__usercall eax=obj, edx=outMinY, ebx=outMaxY)
//   For vegetation objects (obj+533 == 4): transform the bounding volume, compute
//   the 8-corner box, then min/max its corners' y to outMinY/outMaxY. Returns 1
//   (no-op) for non-vegetation or empty meshes, 0 when a range was produced.
//   `worldPivot` is dword_13FCD1C (passed through to TransformBoundingVolume).
int ComputeHeightRange(void* obj, float* outMinY, float* outMaxY, float* worldPivot);

// gilde.exe 0x426bec — VIBE_Mesh_ComputeBoundingRadius
//   (__usercall eax=obj, edx=pivot, ecx=outRadius, ebx=outCenter)
//   Transforms the bounding volume, fetches the object's 8-corner box, averages the
//   corners (*0.125) to a centroid, then takes the max corner distance as the radius.
//   Writes the radius to outRadius (default 100.0 on failure) and centroid to
//   outCenter (xyz). Returns 1 on success, 0 if obj/outputs are null.
//   `vtableCall` is the +492 corner-buffer accessor thunk the original invokes
//   ( *(*(obj+492 +244 +16) +500) ); pass the 8-corner float buffer it returns.
u8 ComputeBoundingRadius(void* obj, float* pivot, float* outRadius, float* outCenter,
                         const float* (*cornerBuffer)(void* obj));

// gilde.exe 0x5c5084 — VIBE_Mesh_AccumulateVertexBounds (__usercall eax=obj, edx=box)
//   Grows the AABB `box` (min xyz at box[0..2], max xyz at box[4..6]) by every
//   vertex of the object's +460 mesh — but only when the object's +530 cull bits
//   allow it ((flags & 0xC)==0 || (flags & 0x10)). Returns 1.
u8 AccumulateVertexBounds(void* obj, float* box);

// gilde.exe 0x5b29d8 — VIBE_Mesh_AccumulateVertexAabb (__usercall eax=obj, edx=box)
//   Same min/max-accumulate as AccumulateVertexBounds but with NO cull-flag gate
//   (the scene-walk callback form). Returns 1.
u8 AccumulateVertexAabb(void* obj, float* box);

// ---------------------------------------------------------------------------
// Per-vertex colour.
// ---------------------------------------------------------------------------

// gilde.exe 0x428898 — VIBE_Mesh_ResetVertexColors (__usercall eax=obj, edx=enable)
//   enable != 0: stamp neutral 0x80 into every vertex's +68/+69/+70 colour bytes
//   and set obj+530 bit1 + obj+528 bit2. enable == 0: clear obj+530 bit1, rebuild
//   the object light cache, set obj+528 bit2. Returns obj.
void* ResetVertexColors(void* obj, int enable);

// gilde.exe 0x428928 — VIBE_Mesh_SetVertexColors (__usercall eax=obj, dl=b, cl=g, bl=r)
//   If any of b/g/r is non-zero: stamp them into every vertex's +70/+68/+69 bytes
//   and set obj+530 bit1 + obj+528 bit2. If all zero: clear obj+530 bit1, rebuild
//   the light cache, set obj+528 bit2. Returns obj.
void* SetVertexColors(void* obj, u8 b, u8 g, u8 r);

} // namespace guild::render

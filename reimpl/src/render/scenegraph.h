#pragma once
#include "render/geometry_types.h"

// Scene-graph frustum culling for the guild::render pipeline (d3_engine.c).
// Classifies an object's transformed bounding box against the six frustum planes,
// transforms a node's 8 AABB corners into view space, and recursively culls an
// octree against the frustum.
//
//   VIBE_Render_ClassifyBoundingBoxPlanes  @0x5ad1f4
//   VIBE_SceneGraph_TransformNodeBoxCorners@0x5f0664
//   VIBE_SceneGraph_CullOctreeAgainstFrustum @0x5f09f0
//   VIBE_Render_CullNodeAgainstFrustum     @0x5ad588 (struct walk; see cpp note)
//
// The original kept the frustum planes / near-far in file-scope floats; here they
// are passed in a Frustum record (see geometry_types.h).
namespace guild::render {

// Result bits returned by ClassifyBoundingBoxPlanes (matches the original byte):
//   bits 0..5 : OR of the per-plane "outside" outcodes across all 8 corners
//               (bit set => *every-or-some* corner was outside that plane)
//   bit 6 (0x40): the box is FULLY outside at least one plane (all 8 corners
//               share an outside outcode) => the node is culled.
// A return with (result & 0x40) == 0 means at least partially visible.
enum : u8 {
    kCullBitFar     = 0x20,  // some corner z > farZ
    kCullBitNear    = 0x10,  // some corner z < nearZ
    kCullBitPlane3  = 0x08,
    kCullBitPlane2  = 0x04,
    kCullBitPlane1  = 0x02,
    kCullBitPlane0  = 0x01,
    kCullFullyOut   = 0x40,
};

// gilde.exe 0x5ad1f4 — VIBE_Render_ClassifyBoundingBoxPlanes
//   (__usercall fn(corners@eax, minZOut@edx, maxZOut@ebx)). `corners` is 8 box
// corners at stride 20 floats (the layout TransformNodeBoxCorners writes). For each
// corner it builds a 6-bit outcode (4 side planes + near + far); ANDs the codes
// across all corners (v23) and ORs them (v24). If the AND over bits0..5 is nonzero
// the box is fully outside (bit6 set). When not fully culled it also computes the
// min/max corner z and, if the out pointers are non-null, writes them and expands
// two running depth bounds (flt_13FD168 min, flt_13FCF3C max in the original).
// Returns the classification byte.
u8 ClassifyBoundingBoxPlanes(const float* corners, const Frustum& f,
                             float* minZOut, float* maxZOut,
                             float* runningNear, float* runningFar);

// gilde.exe 0x5f0664 — VIBE_SceneGraph_TransformNodeBoxCorners
//   (__usercall fn(box@eax, origin@edx, matrix@ebx)). The node's AABB is given by
// box[4..6] (max corner) and box[8..10] (min corner); both are offset by `origin`,
// then each of the 8 combinations is transformed by the 4x4 `matrix` (only the
// first corner adds the translation row matrix[12..14]; the engine quirk is
// preserved). Writes 8 corners at stride 20 floats into `out` and returns `out`.
float* TransformNodeBoxCorners(const float* box, const float* origin,
                               const float* matrix, float* out);

// A scene/octree node as walked by the cull. Mirrors the offsets the original
// dereferenced: child list head at +60 (dword index 15), sibling link at +4,
// AABB box pointer is the node itself (passed to TransformNodeBoxCorners), and a
// per-octant child array of 4 pointers (node[0..3]) for the recursive descent.
struct OctreeNode {
    OctreeNode* octant[4];   // node[0..3]: octant child pointers (the do/while scan)
    OctreeNode* sibling;     // +4 reused as list link in the leaf path
    // (the recursion only needs octant[]/sibling/meshList for the cull walk)
    void*       meshListHead;// *(node+15) = +60: linked mesh list (leaf payload)
};

// gilde.exe 0x5f09f0 — VIBE_SceneGraph_CullOctreeAgainstFrustum
//   (__usercall fn(node@eax, ctx@edx, visibleTag@ebx)). Transforms the node box,
// classifies it; if fully outside (0x40) it stamps every mesh in the leaf list with
// `visibleTag` (the "fully visible, no further test" fast path in the original);
// else if partially inside it recurses into the four octant children. We model the
// per-leaf mesh stamping and the recursion via the callbacks below so the geometry
// module stays decoupled from the full engine node struct.
struct CullCallbacks {
    // Transform+classify this node's box; return the classification byte.
    u8 (*classify)(void* node, void* ctx, const Frustum& f);
    // Stamp all meshes under a fully-visible leaf with the visible tag.
    void (*stampLeaf)(void* node, int visibleTag);
    // Visit children: returns octant[i] (or null). i in [0,4).
    void* (*child)(void* node, int i);
    const Frustum* frustum;
};
char CullOctreeAgainstFrustum(void* node, void* ctx, int visibleTag,
                              const CullCallbacks& cb);

} // namespace guild::render

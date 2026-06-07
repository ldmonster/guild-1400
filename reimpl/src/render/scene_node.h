#pragma once
#include "guild/common/types.h"
#include "render/geometry_types.h"
#include "render/mesh.h"   // DrawList

// =============================================================================
// guild::render — the SCENE-NODE project/cull/append callback the frame walk
// (VIBE_SceneGraph_WalkAndInvoke) invokes per object. Faithful 1:1 reconstruction
// of the draw-list APPEND core of:
//
//   0x5ADD1C  VIBE_Render_ProcessSceneNode
//
// SCOPE / DEFERRED INNER DETAIL
// ---------------------------------------------------------------------------
// The full ProcessSceneNode (374 instructions, 75 basic blocks) chains: LOD frame
// selection (VIBE_Mesh_SelectLodFrame), frustum cull
// (VIBE_Render_CullNodeAgainstFrustum), per-object light cache rebuild
// (VIBE_Light_BuildObjectCache), shadow casting (VIBE_Shadow_*), morph-vertex
// interpolation (VIBE_Mesh_InterpolateMorphVertices), clip-flag compute
// (VIBE_Render_ComputeVertexClipFlags), billboard update + mirror reflection-node
// prep — all deeply engine-state coupled. Those leaf subsystems are LISTED as
// deferred (see the module report). What is reconstructed here is the
// project/cull GATE plus the two DRAW-LIST APPEND loops that are the function's
// payload: given a culled node and its projected mesh, append every front-facing
// polygon to the global PolyList with the proper sort key.
//
// THE APPEND CORE (byte-for-byte)
// ---------------------------------------------------------------------------
// After the cull byte (v29, from CullNodeAgainstFrustum) is computed, when the node
// is NOT fully culled ((v29 & 0x40) == 0), the engine walks the mesh's poly array
// (stride 40 bytes, up to min(capacity-count, polyCount) polys) in one of two modes
// selected by byte_649D70 (1 == software raster path):
//
//  Software path (byte_649D70 != 0): for each poly with flags36 high bit set
//    (front-facing, *(p+36) < 0):
//       entry.poly = poly
//       if poly has a texture (*(p+20) != 0):
//           entry.sortKey = base + ((tex - dword_1406A84) >> 7)
//           *(tex + 84) = dword_649D58   (frame stamp)
//       else entry.sortKey = 0
//       *(p+12) = &dword_13DB398[26 * (flags36 & 0x3F)]   (light table ptr)
//       ++count
//    where `base` (v27) is 1 normally, 0x4000 for the root / special node.
//
//  Hardware path (byte_649D70 == 0): per visible poly the sort key is a depth key
//       maxZ = max(p->v0->z, p->v1->z, p->v2->z)
//       entry.sortKey = (i32)(flt_13FC774 * flt_628080 * flt_628084 * maxZ)
//    (a 1/fov * screen-scale * depth product), else identical append.
//
// To stay decoupled from the full engine node struct (as render/scenegraph.h does)
// the per-poly texture sort id and the cull byte are supplied by the caller.
// =============================================================================
namespace guild::render {

// The append-mode selector, mirroring byte_649D70.
enum class NodeAppendMode {
    Software,   // byte_649D70 != 0: texture-id sort key
    Hardware,   // byte_649D70 == 0: depth sort key
};

// Per-frame append context, gathering the file-scope globals the append reads.
struct NodeAppendContext {
    NodeAppendMode mode = NodeAppendMode::Software;
    u32   baseKey = 1;        // v27: 1 normally, 0x4000 for the root/special node
    u32   frameStamp = 0;     // dword_649D58: stamped into each used texture (+84)
    // Hardware depth-key scalars (flt_13FC774 * flt_628080 * flt_628084):
    float depthScale = 0.0f;  // product of the three scalars
};

// gilde.exe 0x5add1c — VIBE_Render_ProcessSceneNode (append core).
// Append the node's visible polygons to `out`. `cullByte` is the node's
// classification byte (VIBE_Render_CullNodeAgainstFrustum result): when
// (cullByte & 0x40) != 0 the node is fully culled and nothing is appended.
// `polys`/`polyCount` is the projected mesh poly array; `texSortId[i]` is poly i's
// software texture sort delta ((tex - dword_1406A84) >> 7) or 0 when untextured;
// `texFrameStamp[i]` (out) receives the frame stamp for textured polys (the engine
// wrote dword_649D58 into *(tex+84)). The hardware path ignores texSortId and uses
// the per-poly max vertex z. Returns the number of polygons appended.
i32 ProcessSceneNodeAppend(u8 cullByte, const Polygon* polys, i32 polyCount,
                           const u32* texSortId, const NodeAppendContext& ctx,
                           DrawList* out, u32* texFrameStamp);

} // namespace guild::render

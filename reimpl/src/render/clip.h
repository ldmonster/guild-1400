#pragma once
#include "guild/common/types.h"
#include "render/geometry_types.h"

// =============================================================================
// guild::render — polygon CLIP against a set of planes (Sutherland-Hodgman).
// Faithful 1:1 reconstruction of the gilde.exe (d3_engine.c) clipper that the
// software draw-list flush (VIBE_Render_RasterizeMeshList) runs per polygon
// before the triangle-fan rasterize:
//
//   0x5AD7D8  VIBE_Render_ClipPolygonToPlane  (clip the working polygon against
//                                              every plane of a clip context)
//
// THE GLOBAL CLIP LAYOUT (recovered byte-for-byte)
// ---------------------------------------------------------------------------
// The original kept the clip scratch in a block of file-scope globals:
//   dword_13D8798  = the PING-PONG vertex-pointer lists. Two lists of 128 u32
//                    each (512 bytes apart): list[(i&1)<<9]. List A is the input
//                    polygon's vertex pointers; clipping against plane i writes
//                    the result into the *other* list, then they swap. The first
//                    three pointers (dword_13D8798/879C/87A0) are seeded by the
//                    flush with the polygon's 3 vertex pointers.
//   unk_13D8B98    = the NEW-VERTEX POOL: each generated (interpolated) vertex is
//                    an 80-byte Vertex (20 floats), written sequentially (v3+=20).
//   dword_649D78   = INPUT vertex count for the current plane pass.
//   dword_649D74   = OUTPUT vertex count produced by the current plane pass
//                    (the final clipped vertex count on return).
//
// Per plane (a,b,c,d), a vertex P is INSIDE when  a*Px + b*Py + c*Pz >= d.
// Walking the polygon edge (prev -> cur):
//   - if PREV was inside, emit PREV;
//   - if the inside-state changes across the edge, emit a new vertex
//     interpolated at the plane: t = (d - dotPrev) / (dotCur - dotPrev), then
//     lerp xyz(+w at +12), the +44 float, and the four colour bytes +64..+67 and
//     +79 (truncated toward zero into bytes).
// The original's edge order seeds PREV from the *last* vertex (the list is closed
// by copying list[0] to list[count]); we reproduce that closing copy verbatim.
//
// The plane set comes from a ClipContext: a1[0] = plane count, planes start at
// a1+8 as {a,b,c,d} float quads (stride 4 floats). a1 == VIBE poly field [+12].
// =============================================================================
namespace guild::render {

// Re-entrant clip scratch — gathers the file-scope clip globals so the clip is
// self-contained and testable. The two pointer lists and the new-vertex pool
// mirror the original's fixed-size global arrays.
struct ClipScratch {
    // dword_13D8798: two vertex-pointer lists, 128 entries each (512 bytes apart).
    // listPtrs[side][k] holds a Vertex*; `side` toggles each plane pass.
    Vertex* listPtrs[2][128];   // dword_13D8798 (+0) and (+512)
    // unk_13D8B98: pool of generated vertices (each new clip vertex is one Vertex).
    Vertex  newVerts[256];      // unk_13D8B98 (80 bytes / 20 floats each)
    i32     inCount;            // dword_649D78  input vertex count for the pass
    i32     outCount;           // dword_649D74  output vertex count (final result)
};

// A clip context: plane count + plane array. Mirrors the poly's [+12] block
// (a1[0] = count, planes at a1+8 as {a,b,c,d}).
struct ClipPlane { float a, b, c, d; };  // inside when a*x+b*y+c*z >= d
struct ClipContext {
    i32              planeCount;  // *(a1)
    const ClipPlane* planes;      // a1+8 (stride 4 floats)
};

// gilde.exe 0x5AD7D8 — VIBE_Render_ClipPolygonToPlane (__usercall, a1@eax).
// Clips the working polygon (whose first `ctx.firstCount`/seeded vertex pointers
// live in scratch.listPtrs[0][..] with scratch.inCount entries) against every
// plane in `ctx`, Sutherland-Hodgman, generating interpolated vertices in
// scratch.newVerts. On return scratch.outCount is the clipped vertex count and
// the returned array holds the clipped polygon's vertex pointers (a fan).
// `seedCount` is the polygon's starting vertex count (3 for a triangle); it is
// written into scratch.inCount before the first pass (mirrors dword_649D78 set
// by the flush to 3). Returns nullptr when the polygon is degenerate/empty.
Vertex** ClipPolygonToPlane(ClipScratch& scratch, const ClipContext& ctx,
                            i32 seedCount);

} // namespace guild::render

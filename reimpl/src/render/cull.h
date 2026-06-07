#pragma once
#include "guild/common/types.h"
#include "render/geometry_types.h"

// =============================================================================
// guild::render — per-vertex frustum outcode + per-polygon backface/clip flag
// computation. This is the cull/clip-classify pass the scene-node renderer
// (VIBE_Render_ProcessSceneNode @0x5add1c) runs over a mesh's projected/
// transformed vertex+polygon arrays before the draw-list append and rasterize.
//
//   0x5ad614  VIBE_Render_ComputeVertexClipFlags
//
// FRUSTUM PLANE LAYOUT (the globals the original reads; gathered into Frustum,
// see geometry_types.h):
//   plane0 (bit0): x*plane[0].a + z*plane[0].c < plane[0].d   (flt_13DCDA0/A8, dword_13DCDAC)
//   plane1 (bit1): x*plane[1].a + z*plane[1].c < plane[1].d   (flt_13DCDB0/B8, dword_13DCDBC)
//   plane2 (bit2): y*plane[2].b + z*plane[2].c < plane[2].d   (flt_13DCDC4/C8, dword_13DCDCC)
//   plane3 (bit3): y*plane[3].b + z*plane[3].c < plane[3].d   (flt_13DCDD4/D8, dword_13DCDDC)
//   near   (bit4): z < nearZ                                  (flt_13FC76C)
//   far    (bit5): z > farZ                                   (flt_13FCAFC)
// NOTE the original side planes only ever multiply two of the (a,b,c) terms:
// plane0/1 use the x and z components (a,c); plane2/3 use the y and z (b,c). The
// reconstruction preserves exactly which two components each plane uses so the
// arithmetic is byte-identical to the decompiled code (the unused component is
// simply never read).
//
// The `flags` byte (the original's a1@<eax>, threaded from the node classify
// result v29) is a 6-bit MASK selecting which planes to test this pass: bit i set
// => test plane i. Per vertex, only the selected planes contribute to its outcode;
// each selected plane first CLEARS its bit in the vertex's clipFlags (+76) then
// ORs back the freshly computed in/out bit, so re-running with a different mask
// updates only the masked bits. Bit7 (0x80) of clipFlags is cleared at the top of
// every vertex (it is the polygon-pass "visible" marker, set below).
//
// POLYGON PASS (second loop): for each polygon (stride 40 in the original; here a
// Polygon record), clears flags36, then if the polygon has a vertex-0 ptr and is
// not skip-flagged (flags38 bit1 == 0):
//   - if the AND of the three vertices' outcodes (bits0..5) is zero (i.e. the tri
//     is not wholly outside any single plane), the polygon is KEPT: flags36 gets
//     (OR of the three outcodes & 0x3F) | 0x80, and each vertex's bit7 is set
//     (marks it "used"/visible for the later projection+draw-list pass).
//   - otherwise (all three share an outside plane) the polygon is culled: flags36
//     stays 0 and no vertex is marked.
// =============================================================================
namespace guild::render {

// Plane-select / outcode bit positions (match scenegraph.h's kCullBit* and the
// +76 / +36 byte semantics). bit7 of a vertex's clipFlags is the "kept" marker.
enum : u8 {
    kClipPlane0 = 0x01,
    kClipPlane1 = 0x02,
    kClipPlane2 = 0x04,
    kClipPlane3 = 0x08,
    kClipNear   = 0x10,
    kClipFar    = 0x20,
    kClipOutMask = 0x3F,   // the six frustum-plane bits
    kClipKept   = 0x80,    // bit7: vertex used by a kept polygon / poly flags36 kept
};

// gilde.exe 0x5ad614 — VIBE_Render_ComputeVertexClipFlags
//   (__userpurge eax=fn(flags@eax, verts@edx, polys@ecx, vertCount@ebx, polyCount)).
// Classifies every vertex against the masked frustum planes (writing its outcode
// to Vertex.clipFlags) then sets per-polygon keep/cull flags (Polygon.flags36) and
// marks the surviving polygons' vertices (bit7). `planeMask` selects which of the
// six planes are tested (bits 0..5). `verts`/`polys` are the mesh's vertex and
// polygon arrays; `vertCount`/`polyCount` their lengths.
void ComputeVertexClipFlags(u8 planeMask, Vertex* verts, i32 vertCount,
                            Polygon* polys, i32 polyCount, const Frustum& f);

} // namespace guild::render

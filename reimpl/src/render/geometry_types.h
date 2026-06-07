#pragma once
#include "guild/common/types.h"

// Geometry / transform / cull / animation record layouts for the guild::render
// 3D pipeline (d3_engine.c). All byte offsets are from the ORIGINAL 32-bit x86
// records, recovered from raw *(type*)(base+off) access in gilde.exe (the IDB had
// no named UDTs). This header owns the *geometry* structs; the bottom-of-pipeline
// software-surface records live in render/types.h.
//
// SCOPE: this module is geometry/transform/cull/anim only. It produces the
// projected + sorted vertex/polygon draw lists; the triangle RASTERIZER that
// consumes those lists is a separate module.
//
// Fidelity: the original engine kept the view/frustum parameters in a block of
// file-scope globals (flt_13FCAFC, flt_13DCDA0.., dword_13FC570, ...). To keep
// this reconstruction re-entrant and testable we gather those globals into the
// explicit ViewState / Frustum / DrawList records below; each field carries the
// original global symbol it mirrors. The arithmetic is otherwise verbatim.
namespace guild::render {

// ---------------------------------------------------------------------------
// Vertex record — 80-byte stride, iterated `j += 80` in
// VIBE_Mesh_ProjectVerticesToScreen (gilde.exe 0x5c5120).
// Model-space xyz live at +0/+4/+8; the perspective reciprocal 1/z is stashed at
// +16 *before* projection, then +16/+20 are overwritten with the projected screen
// x,y. +66 holds the per-vertex light/shade table index (used as 768*idx).
// ---------------------------------------------------------------------------
struct Vertex {
    float x;          // +0x00  model-space x
    float y;          // +0x04  model-space y
    float z;          // +0x08  model-space z / depth
    float _pad0c;     // +0x0C
    float screenX;    // +0x10  pre-project: 1/z reciprocal; post-project: screen x
    float screenY;    // +0x14  projected screen y
    float _pad18;     // +0x18
    float _pad1c;     // +0x1C
    float u;          // +0x20  texture U
    float v;          // +0x24  texture V
    u8    _pad28[24]; // +0x28 .. +0x3F  (24 bytes -> reaches +0x40)
    u8    color0;     // +0x40  (+64) colour/light component
    u8    _pad41;     // +0x41  per-vertex render-flag byte (the +65 *(j-15) write)
    u8    lightIdx;   // +0x42  (+66) light/shade table index (768 * idx offset)
    u8    _pad43[9];  // +0x43 .. +0x4B
    u8    clipFlags;  // +0x4C  (+76) per-vertex frustum outcode (bits0..5) | bit7
    u8    _pad4d[3];  // +0x4D .. +0x4F  (pads record to 80 bytes)
};
// In the original 32-bit record the stride is exactly 80 bytes; this native-pointer
// reconstruction keeps the documented field offsets (it never serialises Vertex).
static_assert(sizeof(Vertex) == 80, "Vertex must be 80 bytes (engine stride)");

// ---------------------------------------------------------------------------
// Polygon record — 40-byte stride, iterated `v19 += 40` in ProjectVerticesToScreen.
// Three vertex pointers, a float UV/offset triple at +24/+28/+32, and two flag
// bytes: +36 (bit7 = backface, 0x40/0xC0 = culled) and +38 (bit1/bit2/bit4).
// ---------------------------------------------------------------------------
// Original 32-bit record is 40 bytes (vertex ptrs at +0/+4/+8). With native 64-bit
// pointers the struct is larger; we keep the same field *order* and document the
// original byte offsets. Polygon is never serialised, so the larger native size is
// fine. Offsets in comments are the ORIGINAL 32-bit byte offsets.
struct Polygon {
    Vertex* v0;       // +0x00  vertex ptr 0
    Vertex* v1;       // +0x04  vertex ptr 1
    Vertex* v2;       // +0x08  vertex ptr 2
    float   uvX;      // +0x18  (+24) UV/offset x
    float   uvY;      // +0x1C  (+28) UV/offset y
    float   uvZ;      // +0x20  (+32) UV/offset z
    u8      flags36;  // +0x24  (+36) bit7 backface, 0x40/0xC0 = culled
    u8      flags38;  // +0x26  (+38) bit1 = no-cull, bit2 = ?, bit4 = ?
};

// ---------------------------------------------------------------------------
// Draw-list entry — 8 bytes each in the global PolyLists (dword_13FC570 cursor;
// dword_13FC584/13FC51C bases). [+0] sort key (768 * maxLightIdx), [+4] poly ptr.
// ---------------------------------------------------------------------------
struct DrawListEntry {
    u32      sortKey; // +0x00  768 * max(vertex light index)
    Polygon* poly;    // +0x04  -> polygon record
};

// Mesh geometry block — pointed to from object +460 in the original. The poly
// array base/count/capacity are at [+4]/[+8]/[+12]; [+0] points at the first
// vertex record. We model it as a plain record (the engine read it via offsets).
//   *(geom+0)  -> Vertex array base
//   *(geom+4)  -> Polygon array base
//   *(geom+8)  =  polygon count
//   *(geom+12) =  polygon capacity
struct MeshGeometry {
    Vertex*  vertices;   // +0x00  vertex array
    Polygon* polygons;   // +0x04  polygon array
    i32      polyCount;  // +0x08  number of polygons
    i32      polyCap;    // +0x0C  polygon capacity
    i32      vertexCount;// (reconstruction helper; the original derived it elsewhere)
};

// ---------------------------------------------------------------------------
// View / camera state. In the original these were scattered globals read by the
// projection (VIBE_Coord_ProjectPoint) and screen-scale code. eye[] is the camera
// position (a1[0..2] in ProjectPoint), invDepth is 1/a1[4] (a1[4] = view depth),
// and screenScaleX/Y map normalised device offsets to pixels. The +0.5 rounding
// bias is dbl_610784 (== 0.5).
// ---------------------------------------------------------------------------
struct ViewState {
    float eye[3];        // a1[0..2] in ProjectPoint: camera/world origin
    float _pad;          // a1[3]
    float viewDepth;     // a1[4]: perspective depth; ProjectPoint uses 1/viewDepth
    // Screen-scale terms from VIBE_Mesh_ProjectVerticesToScreen:
    float scaleX;        // flt_628B94 path uses per-axis scales held in v30[4]/v31/v32
    float scaleY;        // (see project_screen for the exact mapping)
    float screenW;       // *(v35+32): screen width clamp for on-screen test
};

// ---------------------------------------------------------------------------
// Frustum — six clip planes plus the near/far depth bounds, mirroring the globals
// read by VIBE_Render_ClassifyBoundingBoxPlanes (gilde.exe 0x5ad1f4):
//   plane[0] = flt_13DCDA0..AC   plane[1] = flt_13DCDB0..BC
//   plane[2] = flt_13DCDC0..CC   plane[3] = flt_13DCDD0..DC
//   farZ  = flt_13FCAFC (bit5: z >  farZ)   nearZ = flt_13FC76C (bit4: z < nearZ)
// Each plane is (a,b,c, d): the point is OUTSIDE that plane when a*x+b*y+c*z < d.
// ---------------------------------------------------------------------------
struct Frustum {
    float plane[4][4];   // 4 side planes: {a,b,c,d}; outside if a*x+b*y+c*z < d
    float farZ;          // flt_13FCAFC: outcode bit5 set when corner z >  farZ
    float nearZ;         // flt_13FC76C: outcode bit4 set when corner z <  nearZ
};

} // namespace guild::render

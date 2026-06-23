#pragma once
// mesh_recon3_geometry.h — VIBE_Mesh_* geometry leaves (recon3 cluster)
//
// Faithful 1:1 reconstructions of two self-contained mesh geometry routines from
// gilde.exe. Both are pure floating-point geometry over flat vertex/face arrays;
// they touch no Win32/GPU/audio/file subsystems, so they reconstruct cleanly.
//
//   gilde.exe 0x5d1a6c — VIBE_Mesh_ComputeVertexNormals
//   gilde.exe 0x5d1b54 — VIBE_Mesh_ComputeBoundingExtents
//
// Provenance for the struct field offsets is documented inline against the
// original record layouts (byte offsets from the Hex-Rays decompile).

#include "guild/common/types.h"

namespace guild::render::mesh_recon3 {

using guild::u8;
using guild::i32;
using guild::u32;
using f32 = float;
using f64 = double;

// ---------------------------------------------------------------------------
// Vertex record (stride 24 bytes = 6 floats), as indexed by the original via
// `*((_DWORD*)mesh + 16)` (= mesh+64, the vertex array base):
//   [0..2] position xyz
//   [3..5] accumulated/normalized normal xyz
// ---------------------------------------------------------------------------
struct MeshVertex {       // 24 bytes
    f32 pos[3];           // +0x00
    f32 normal[3];        // +0x0C
};

// ---------------------------------------------------------------------------
// Face record (stride 56 bytes = 14 dwords), as indexed by the original via
// `*((_DWORD*)mesh + 18)` (= mesh+72, the face array base). Only the fields
// touched by ComputeVertexNormals are modelled here; the rest is opaque pad so
// the 56-byte stride is preserved exactly.
//   +0x18 (dword 6) vertex index 0
//   +0x1C (dword 7) vertex index 1
//   +0x20 (dword 8) vertex index 2
//   +0x2C (dword 11..13) computed face normal xyz
// ---------------------------------------------------------------------------
struct MeshFace {         // 56 bytes (0x38)
    u8  pad0[0x18];       // +0x00 .. +0x17 (opaque: flags / material / etc.)
    i32 idx[3];           // +0x18 .. +0x23 vertex indices
    u8  pad1[0x08];       // +0x24 .. +0x2B (opaque -> normal at +0x2C)
    f32 face_normal[3];   // +0x2C .. +0x37
};

// ---------------------------------------------------------------------------
// Minimal mesh view matching the dword fields the originals index by raw offset.
// These mirror the *((_DWORD*)mesh + N) accesses in the decompile:
//   +0x40 (dword 16) verts   (MeshVertex* base)
//   +0x44 (dword 17) vert_count
//   +0x48 (dword 18) faces   (MeshFace* base)
//   +0x4C (dword 19) face_count
// ---------------------------------------------------------------------------
struct MeshView {
    MeshVertex* verts;     // +0x40 / dword 16
    i32         vert_count;// +0x44 / dword 17
    MeshFace*   faces;     // +0x48 / dword 18
    i32         face_count;// +0x4C / dword 19
};

// gilde.exe 0x5d1a6c — VIBE_Mesh_ComputeVertexNormals (__usercall, eax = mesh)
// For each face: face_normal = TriangleNormal(v[idx0], v[idx1], v[idx2]).
// Then for each vertex: sum the face_normals of every face referencing it and
// normalize in place into vertex.normal. Matches the original op order exactly.
void ComputeVertexNormals(MeshView& mesh);

// ---------------------------------------------------------------------------
// Bounding-extents object view. ComputeBoundingExtents indexes an "object"
// struct by raw byte offset:
//   +0x40 (dword 16) verts   (MeshVertex* base, 24-byte stride)
//   +0x44 (dword 17) vert_count
//   +0x68 (dword 26) center_x
//   +0x6C (dword 27) center_y
//   +0x70 (dword 28) center_z
//   +0x1D4 (dword 117) radius2 (written first to dword 118 then overwritten)
//   +0x1D8 (dword 118) radius  (the value at +0x1D4 copied to +0x1D0)
//   +0x1D0 (dword 116) radius_alias  (set = radius)
//
// NOTE on layout: the original is a1+64=verts, a1+68=count, a1+104/108/112 =
// center xyz, a1+468 = bounding-radius alias, a1+472 = bounding radius. It also
// REQUIRES 8 extra MeshVertex slots after the real vertices (it writes the 8
// AABB corner points at indices [count .. count+7]). The caller must allocate
// vert_count + 8 vertices. We model that contract explicitly.
// ---------------------------------------------------------------------------
struct BoundsObject {
    MeshVertex* verts;      // +0x40 (must hold vert_count + 8 entries)
    i32         vert_count; // +0x44
    f32         center[3];  // +0x68 / +0x6C / +0x70
    f32         radius_alias;// +0x1D0 (a1+468)
    f32         radius;     // +0x1D4 (a1+472)
};

// gilde.exe 0x5d1b54 — VIBE_Mesh_ComputeBoundingExtents (__usercall, eax = obj)
// 1. Find the max vertex distance from origin (sqrt of squared length) -> radius.
// 2. Find the AABB (min/max xyz). Write the 8 corner vertices at indices
//    [count..count+7] in the same corner order as the original.
// 3. radius = length of the AABB diagonal (max-min).
// 4. center = (sum of the 8 corner coords) * 0.125  (flt_628FC0 = 0.125).
// Reproduces the exact branch structure (the seed +/-1e10 and the min/max
// compare-and-select order) verbatim.
void ComputeBoundingExtents(BoundsObject& obj);

} // namespace guild::render::mesh_recon3

#pragma once
#include "render/bgf_loader.h"   // reuse BgfModel / BgfVertex / BgfPolygon / BgfMaterial / BgfDummy
#include "guild/common/types.h"
#include <cstddef>

// =============================================================================
// guild::render — .BGF "AGF" token-script mesh loader (the SHIPPED format).
//
// The .bgf entries in Resources/Objects.BIN are NOT the fast-chunk binary the
// fast-chunk loader (bgf_loader.cpp) handles. After PKZIP/DEFLATE decompression
// each entry begins with the ASCII magic "BGF\0" (42 47 46 00) followed by a
// TOKEN / SCRIPT stream. The real engine loader tries the fast chunk first and
// ONLY on failure falls back to this AGF reader:
//
//   0x5D2348  VIBE_Mesh_LoadBgfFile      (fast chunk -> AGF fallback)
//   0x5E44B4  VIBE_ModelIo_ReadChunkTag  (read "BGF\0", '.', version dword,
//                                          then VIBE_Script_ParseBlock(TOP))
//   0x5E3BD0  VIBE_Script_ReadToken      (1 byte; EOF->'+'(0x2B); >0x3A->'\''(0x27))
//   0x5E3C44  VIBE_Script_ParseBlock     (recursive, 64-entry dispatch tables)
//   0x5E3C0C  VIBE_Script_FindTokenHandler (linear scan; stops at the '(' entry)
//
// THE GRAMMAR (recovered byte-for-byte from the dispatch tables byte_64A4F8 /
// 0x64A420 / 0x64A360 / 0x64A444 / 0x64A480 / 0x64A4C8 and every leaf handler)
// -----------------------------------------------------------------------------
// Each dispatch-table entry is 12 bytes {u32 token, u32 ptr, u32 flag}.
// ParseBlock reads single-byte tokens (via ReadToken):
//   '\'' (0x27)        -> end of this block.
//   '/'  (0x2F)        -> end of block.
//   '+'  (0x2B) / EOF  -> end of block (the whole stream ends with '+').
//   '('  (0x28)        -> fire the '(' entry's open handler, then end the block.
//   else               -> FindTokenHandler scans the table (terminating at the
//                         first '(' entry); if found:
//                           * leaf  (ptr!=0, flag==0): call ptr() to read payload,
//                           * sub   (flag!=0): RECURSE into the sub-table (ptr),
//                           * recur (ptr==0): RECURSE into the same table.
//
// The handlers fill the SAME in-memory model the fast-chunk loader builds (the
// engine's v145 parse struct, ~408B): +12 materialCount, +16 material ptr (224B
// stride), +28 polyTotal, +32 poly ptr (56B stride), +44 vertexTotal, +48 vertex
// ptr (24B stride: pos[3]+normal[3]), +52 dummyCount, +56 dummy buffer (88B).
// Per-block counts (+20 poly, +36 vertex) accumulate into the totals (+28/+44)
// each time a '(' open handler (VIBE_ModelIo_ResetBufferCounters @0x5E42A0) runs,
// so multi-block meshes concatenate into one vertex/poly array. The vertex index
// written into each poly is the on-disk index + the running vertex base (+44).
//
// Material fields (sub-table 0x64A360): name @+0, mesh @+64, texture @+128 (each a
// VIBE_Bio_ReadString with a trailing ".ext" stripped), plus byte/dword flag
// fields. A '(' (VIBE_Script_IncrementCounter @0x5E3D28) advances the material
// counter, so one ParseBlock(0x64A360) reads exactly one material; the outer block
// re-enters for the next.
//
// We parse straight from a flat decompressed buffer and fill a render::BgfModel
// (the exact struct bgf_loader.h already defines — NOT modified here). The
// existing render::BuildGeometry then produces engine-stride geometry. The
// optional post-parse stages (bounding-extents @0x5D1B54, vertex-normal recompute
// @0x5D1A6C) are reconstructed below; the vertex dedup / morph-rotation bake /
// material dedup remain documented TODO (see report).
// =============================================================================
namespace guild::render {

// Script token bytes (VIBE_Script_ReadToken / ParseBlock).
constexpr u8 kAgfTokenEnd        = 0x2B;  // '+'  EOF / end of stream
constexpr u8 kAgfTokenBlockEnd   = 0x27;  // '\'' end of block
constexpr u8 kAgfTokenBlockEnd2  = 0x2F;  // '/'  end of block (script -> binary)
constexpr u8 kAgfTokenOpen       = 0x28;  // '('  open/begin handler
constexpr u8 kAgfTokenVersion    = 0x2E;  // '.'  version marker after "BGF\0"
constexpr u8 kAgfTokenNameThresh = 0x3A;  // bytes > this collapse to '\''

// gilde.exe 0x5E3BD0 — VIBE_Script_ReadToken (cursor form). Reads one byte from
// [*p, end); returns '+' at EOF, '\'' for any byte > 0x3A, else the raw byte.
// Advances *p only when a byte was consumed.
u8 AgfReadToken(const u8** p, const u8* end);

// gilde.exe 0x5E44B4 / 0x5E3C44 — parse an AGF "BGF\0" token-script buffer into
// `out` (a render::BgfModel). Verifies the "BGF\0" magic + '.' version marker,
// runs the recursive token parser over the dispatch grammar, and leaves `out`
// holding the concatenated vertex / poly / material / dummy arrays with
// out.vertexCount / polyCount / materialCount / dummyCount set. Returns false on a
// bad magic or a truncated record. (No vertex +8 slack here: the AGF reader sizes
// the array to the exact total.)
bool LoadAgfModel(const u8* data, size_t size, BgfModel& out);

// gilde.exe 0x5D1B54 — VIBE_Mesh_ComputeBoundingExtents. Fills the model's
// bounding info from its vertex positions. The binary computes max|vertex| into
// mesh+472 and copies it to +468 (radius2), then — when the vertex count is
// positive — the per-axis AABB (accumulators seeded ±1e10), writes the 8 AABB
// corner vertices into the slack slots past the live vertices, OVERWRITES +472
// with the AABB diagonal length, and averages the 8 corners (× flt_628FC0 =
// 0.125) into the centroid at +104/+108/+112. (The full slack-slot form over the
// engine Mesh record lives in render/mesh_postprocess.cpp; this helper surfaces
// the same scalar outputs for the BgfModel view.) Pure over the parsed positions.
struct BgfBounds {
    float radius = 0.0f;          // +472 final: AABB diagonal (max|v| if count==0)
    float radius2 = 0.0f;         // +468: max sqrt(x^2+y^2+z^2) over all vertices
    float centroid[3] = {0, 0, 0}; // +104/+108/+112: mean of the 8 AABB corners
    float min[3] = {0, 0, 0};
    float max[3] = {0, 0, 0};
};
BgfBounds ComputeBoundingExtents(const BgfModel& m);

// gilde.exe 0x5D1A6C — VIBE_Mesh_ComputeVertexNormals. Recomputes each vertex's
// normal as the normalized sum of the face normals of the triangles that use it
// (the engine first wrote each triangle's normal onto its poly, then averaged per
// vertex). Overwrites out.vertices[*].normal in place.
void ComputeVertexNormals(BgfModel& m);

} // namespace guild::render

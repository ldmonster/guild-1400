#pragma once
#include "render/bgf_loader.h"   // render::BgfModel / BgfVertex / BgfPolygon / BgfMaterial
#include "guild/common/types.h"

// =============================================================================
// guild::render — AGF mesh POST-PROCESS stages (the documented Wave 27 TODO).
//
// VIBE_Mesh_LoadBgfFile @0x5d2348 runs several post-parse stages AFTER the raw
// AGF token-script parse (render::LoadAgfModel, agf_loader.cpp, which deliberately
// skipped them). This module reconstructs those stages 1:1 from the binary and
// applies them to an already-parsed render::BgfModel. agf_loader.cpp /
// real_mesh_source.cpp are owned/stable and NOT edited.
//
// The engine's in-memory parse struct fields (offsets from the IDB) map onto
// render::BgfModel as:
//   parse +44 vertexCount  / +48 vertex ptr (24B stride pos[3]+normal[3])  -> vertexCount / vertices
//   parse +28 polyCount    / +32 poly ptr   (56B stride)                   -> polyCount   / polygons
//   parse +12 materialCount/ +16 material ptr(224B stride)                 -> materialCount/ materials
//   poly +24/+28/+32 = vtx indices ; poly +36 = texId ; poly +40 = matIndex
//
// THE STAGES (recovered byte-for-byte from @0x5d2348, in the engine's order)
// -----------------------------------------------------------------------------
//  (1) VERTEX DEDUP  (0x5d2425..0x5d254d)
//        For every ordered pair (i,j), i!=j, if the two positions are within
//        tolerance 0.001 on all three axes (VIBE_Math_VectorWithinTolerance
//        @0x5caa4c: fabs(dx)<=t && fabs(dy)<=t && fabs(dz)<=t), vertex j is a
//        duplicate of i: every poly vtx index == j is remapped to i, every index
//        > j is decremented, and vertex j is removed (the array shrinks by one and
//        j is re-examined). Runs only when the parse-struct "skip-dedup" flag byte
//        (struct +1) is clear; LoadAgfModel never sets it, so it always runs here.
//
//  (2) MORPH-ROTATION BAKE  (0x5d257e..0x5d26e9)
//        Each (deduped) vertex position is transformed by a fixed rotation built
//        from sin/cos of dbl_6290CC = -PI/2 and dbl_6290D4 = PI, with a final
//        Z-flip via dbl_6290DC = -1.0. The verbatim FP algebra (with these
//        constants) reduces to (x,y,z) -> (-x, z, -y). Implemented from the raw
//        algebra so the constants are load-bearing, not the simplified result.
//
//  (3) MATERIAL DEDUP + COMPACT/REORDER  (0x5d26f2..0x5d2b60)
//        3a RemoveDoubleMaterials: byte-identical 224-byte material records are
//           collapsed — every poly matIndex pointing at a later identical material
//           is remapped to the first occurrence (poly +40).
//        3b Compact-unused: materials not referenced by any poly are removed; poly
//           matIndex values above each removed slot are decremented.
//        3c Reorder-by-first-use: materials are reordered into the order their
//           first referencing poly appears, and poly matIndex values are renumbered
//           to the new sequential order (the engine's TempMat[ReOrder] pass).
//
//  NOT reconstructed here (true gap — SAY SO): the per-material UV transform pass
//  at 0x5d2b67..0x5d2c94 ("second pass near +204..+220") rotates/scales each poly's
//  UV pairs using material fields at +204/+208 (u/v scale), +212/+216 (u/v offset)
//  and +220 (angle), with flt_6290E4 = 0.5 and flt_6290E8 = PI. render::BgfMaterial
//  does NOT carry those five float fields (LoadAgfModel never reads them), so the
//  pass cannot be applied to a BgfModel without changing the owned loader/struct.
//  See report. The texture-resolve + dummy-copy stages are also out of scope.
// =============================================================================
namespace guild::render {

// Position tolerance for the dedup compare (gilde.exe 0x5d248c literal 0.001).
constexpr float kAgfVertexMergeTolerance = 0.001f;

// Morph-bake constants (gilde.exe data; see header banner for the decoded values).
constexpr double kAgfMorphAngleA = -1.5707963267948966;  // dbl_6290CC = -PI/2
constexpr double kAgfMorphAngleB =  3.14159265358979312;  // dbl_6290D4 =  PI
constexpr double kAgfMorphZScale = -1.0;                  // dbl_6290DC = -1.0

// gilde.exe 0x5caa4c — VIBE_Math_VectorWithinTolerance. True iff the per-axis
// absolute differences of two positions are all <= tol.
bool VectorWithinTolerance(const float* a, const float* b, float tol);

// gilde.exe 0x5d2425 — STAGE 1. Collapse near-duplicate vertices (tolerance
// kAgfVertexMergeTolerance), remapping every poly vtx index and shrinking
// m.vertexCount / m.vertices. Returns the number of vertices removed.
u32 DeduplicateVertices(BgfModel& m);

// gilde.exe 0x5d257e — STAGE 2. Bake the fixed morph rotation into every vertex
// position (m.vertices[i].pos). Normals are left untouched (the engine recomputes
// them afterward; ComputeVertexNormals lives in agf_loader). Operates on the first
// m.vertexCount vertices.
void BakeMorphRotation(BgfModel& m);

// gilde.exe 0x5d26f2 — STAGE 3. Material dedup (3a), compact-unused (3b), and
// reorder-by-first-use (3c), keeping poly matIndex (poly +40) consistent the whole
// way. Shrinks m.materialCount / m.materials. Returns the number of materials
// removed (dedup + unused).
u32 DeduplicateMaterials(BgfModel& m);

// Run the post-process stages in the engine's order: vertex dedup -> morph bake
// -> material dedup/reorder. (The UV-transform + texture stages are out of scope;
// see the header banner.)
void PostProcessModel(BgfModel& m);

} // namespace guild::render

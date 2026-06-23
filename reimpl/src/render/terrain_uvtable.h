#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — the terrain tile-corner UV TABLE writer. Faithful 1:1
// reconstruction of the deterministic first half of:
//
//   0x5B94CC  VIBE_Render_ComputeFilterWeights  (writes the 24-float UV table at
//                                                flt_13FE540)
//
// PROVENANCE / DATA FLOW (all captured):
//   * Writers/callers: VIBE_TextureCache_Setup @0x5ba37c and
//     VIBE_Render_SetMipFilterLevel @0x5b9e74 both end with
//     ComputeFilterWeights(flt_13FE540); SetMipFilterLevel derives the mip tile
//     size dword_64A038 (a power of two in [4,64]) the table is built from.
//   * Reader: VIBE_Floor_RenderTerrain @0x5bf22c reads flt_13FE540 at 0x5c1ff5
//     and 0x5c221c (xrefs_to verified) — the per-tile texture-quad UV corners.
//
// THE TABLE (decompiled @0x5b94cc, the a1[0..23] writes):
//   e = 1.0 / (double)mipTileSize          (v63, stored single)
//   f = 1.0 - e - e                        (v50, the inset span)
// The 24 floats form 12 (u,v) pairs = 4 triangles x 3 corners, the texture-space
// corners of the two quad-split modes the tile walk emits (corners inset by e):
//   T0: (e,e+f) (e,e)     (e+f,e)      T1: (e+f,e) (e+f,e+f) (e,e+f)
//   T2: (e,e)   (e+f,e+f) (e,e+f)      T3: (e,e)   (e+f,e)   (e+f,e+f)
// (T0/T1 = the TL-BR-diagonal split; T2/T3 = the alternate diagonal — the same
//  split selector Pass A of the terrain walk stores in poly flags38 bit0.)
//
// NAMED GAP (rule 8): the second half of @0x5b94cc (the do/while v27 != 6144
// loop) fills a 64-entry random jitter table via VIBE_Util_RandNext +
// VIBE_Math_MatrixFromEuler through four output cursors (v33/v34/v35/v36) that
// Hex-Rays could NOT resolve in the captured decompile — the destination of
// that table is unrecoverable from the available evidence, so it is left out.
// =============================================================================
namespace guild::render {

// Number of floats in the flt_13FE540 UV table (24 = 4 triangles x 3 verts x uv).
constexpr int kTerrainUvTableSize = 24;
// One quad-split triangle's UV record is 6 floats (3 verts x (u,v)) == 24 bytes.
constexpr int kTriUvFloats = 6;
// Per-sub-texture stride in flt_13FE540 is 0x60 bytes == 24 floats (subTexId*0x60).
constexpr int kSubTexUvStride = kTerrainUvTableSize;
// The midpoint seam-blend weight flt_628B48 = 0.5 (0x3f000000), confirmed via
// get_bytes 0x628B48 == 00 00 00 3f.
constexpr float kSeamUvBlend = 0.5f;

// gilde.exe 0x5b94cc — VIBE_Render_ComputeFilterWeights (deterministic half).
// Build the 24-float tile-corner UV table for the given mip tile size
// (dword_64A038; SetMipFilterLevel clamps it to a power of two in [4,64], 64
// being the shipped default). `out` receives a1[0..23] exactly as the original
// stored them (x87 doubles, stored single).
void BuildTerrainUvTable(float out[kTerrainUvTableSize], u32 mipTileSize);

// =============================================================================
// PER-QUAD UV EMISSION (the @0x5bf22c poly+16/+56 UV-pointer assignment)
// -----------------------------------------------------------------------------
// VIBE_Floor_RenderTerrain @0x5bf22c, the per-quad poly write (disasm
// @0x5c1f95..0x5c203f, decoded 1:1):
//   subTexId = (cellFlag & 0x40) ? (byte_13DCE58[(quad & 0xFF)+base] & 0x3F) : 0
//   uvBase   = subTexId * 0x60                       (imul edx, var_208, 0x60)
//   poly+0x14 = texRec                               (bound texture id, always)
//   if (cellFlag & 0x80):                            (the slope/visible bit)
//       poly+0x3c = texRec                           (tri1 texid)
//       poly+0x10 = &flt_13FE540[uvBase]             (T0 UV: floats[0..5])
//       poly+0x38 = &flt_13FE540[uvBase] + 0x18      (T1 UV: floats[6..11])
// i.e. tri0 takes the FIRST 6 floats of the sub-texture's 24-float record, tri1
// the NEXT 6. (subTexId == 0 in the shipped image -> the single record at
// flt_13FE540[0..23]; byte_13DCE58 is all-zero, get_bytes verified.)
//
// This repo's Polygon carries a UV triple, not the engine's flt_13FE540 pointer.
// The per-poly UV emission is therefore modelled as an explicit 6-float record
// copied off the table into per-poly UV storage (terrain_walk), exactly the bytes
// poly+16/+56 pointed at.
// =============================================================================

// Byte offset 0x60 == kSubTexUvStride*4; the per-quad sub-texture id.
inline int TerrainUvBaseIndex(u32 subTexId) {
    return (int)subTexId * kSubTexUvStride;          // subTexId * 0x60 / 4 floats
}

// gilde.exe 0x5c1f88 — the per-cell sub-texture id selector.
//   subTexId = (cellFlag & 0x40) ? (byte_13DCE58[(quadIdx & 0xFF)+base] & 0x3F) : 0
// `subTexSrc` mirrors byte_13DCE58+base (the runtime per-cell table; all-zero in
// the static image -> always 0). Null `subTexSrc` -> 0 (the shipped default).
u32 TerrainSubTexId(u8 cellFlag, const u8* subTexSrc, i32 quadIdx);

// Copy tri0's 6-float UV record (poly+16 target == &flt_13FE540[subTexId*0x60]).
void TerrainQuadUvT0(float out[kTriUvFloats], const float* uvTable, u32 subTexId);
// Copy tri1's 6-float UV record (poly+56 target == that + 0x18 == +6 floats).
void TerrainQuadUvT1(float out[kTriUvFloats], const float* uvTable, u32 subTexId);

// gilde.exe 0x5bf22c — the seam-UV midpoint blend (disasm @0x5bfd27..0x5bfd79 and
// the three sibling arms; flt_628B48 = 0.5). The engine copies the boundary tri's
// 6-float UV record into a per-tile 24-byte UV scratch (`rec`), then averages the
// two seam-edge endpoint UVs into the destination vertex slot, so the midpoint
// vertex carries the mean of the two corner UVs. Two diagonal cases, decoded 1:1:
//   TL-BR (*(poly+38)&1 set, disasm @0x5bfd27): average record verts 1 & 2 into
//     vert 2's slot:  rec[4]=(rec[2]+rec[4])*0.5;  rec[5]=(rec[3]+rec[5])*0.5;
//   BL-TR (clear, disasm @0x5c2348): average record verts 0 & 1 into vert 0's
//     slot:           rec[0]=(rec[0]+rec[2])*0.5;  rec[1]=(rec[1]+rec[3])*0.5;
// `rec` is the 6-float per-tile UV scratch (modified in place).
void TerrainSeamBlendUv(float rec[kTriUvFloats], bool diagTLBR);

} // namespace guild::render

#pragma once
// =============================================================================
// guild::render — 24-bit TEXTURE PALETTIZATION (the VIBE_Quant_* family).
//
// Faithful 1:1 reconstruction of the colour quantizer the software texture
// path runs over a 24-bit BMP source (gilde.exe, all __usercall):
//
//   0x6029f0  VIBE_Quant_BuildPalette      (the orchestrator; maxColors=256,
//                                           dither=1 at the only texture call
//                                           site, VIBE_Bmp_LoadBuffer 0x5f14c4)
//   0x602aa4  VIBE_Quant_InitLookupTables  (3 channel bit-interleave tables +
//                                           the squared-difference table)
//   0x602be0  VIBE_Quant_AllocColorNodes   (6 tree levels: 1/8/64/512/4096/
//                                           32768 nodes of 24 bytes, zeroed)
//   0x602d2c  VIBE_Quant_BuildHistogram    (15-bit interleaved histogram +
//                                           leaf heap + ancestor masks)
//   0x602f3c  VIBE_Quant_HeapSiftDown      (min-heap by node subtree count)
//   0x603068  VIBE_Quant_HeapReduceColors  (merge least-populated leaves up
//                                           the octree until <= maxColors)
//   0x603180  VIBE_Quant_TreeCollectPalette(depth-first, children bit 7..0
//                                           first; palette = rounded average)
//   0x603a30  VIBE_Quant_FindClosestColor  (squared distance over the leaves;
//                                           query bucketed to (c&0xF8)+4)
//   0x6033b4  VIBE_Quant_MapImageToPalette (serpentine Floyd–Steinberg dither
//                                           with a 32768-entry RGB555 lazy
//                                           closest-colour cache and the +-20
//                                           error clamp; or the undithered
//                                           histogram-table arm)
//   0x602a70  VIBE_Quant_CopyPaletteEntries(planar 256-R/256-G/256-B copy-out)
//   0x602c8c  VIBE_Quant_FreeColorNodes
//
// HOW THE ORIGINAL REACHES IT (the live call tree):
//   VIBE_Object_SelectTextureSet 0x5b3f54 / VIBE_Mesh_LoadBgfFile 0x5d2348 /
//   VIBE_Model_LoadFastChunk 0x5f87b8
//     -> VIBE_Texture_LoadByName 0x5da714      (record by name; width==height
//                                               gate; +125 = source bpp)
//     -> VIBE_Texture_UploadToSurface 0x5db234 (software arm, byte_649D70==0)
//     -> VIBE_Texture_LoadSoftPalettize 0x5da34c
//          -> VIBE_Bmp_LoadBuffer 0x5f0ce4 (flags=7): a 24-bit source goes
//             through VIBE_Quant_BuildPalette(width, rgb, dst, height,
//             256, /*dither=*/1, palette) — THE functions reconstructed here —
//             yielding 8-bit indices + a 256-colour palette;
//          -> used-colour compaction + VIBE_HiColTab_FindOrBuild 0x5da04c
//             (already reconstructed: render/texlight_recon.h HiColTabBank +
//             render/hicoltab.cpp HiColTabAddEntry) re-bases those indices
//             into the shared 16-bit shading banks. The texel COLOURS are
//             decided entirely by the quantizer; the HiColTab stage only
//             renumbers indices into the shared bank.
//
// In the reimpl the live consumer is the material resolve layer
// (play::RealTextureSource): a DecodedBmp whose source is 24-bit gains the
// quantized `indices` + `palette` via PalettizeDecodedBmp() below, after which
// the EXISTING palettized bind path (city_view3d bindFor / the universe
// driver's buildTextureBind -> render::Texture + 565 LUT) draws it exactly
// like an 8-bit source — the original software renderer's behaviour. This
// REPLACES the former default-off RGB stand-in (render/texture.h
// SetRgb24MaterialStandIn, kept for API compatibility but now unreachable on
// the default path because palettized indices are always present).
//
// STATE NOTE (1:1): the original keeps the collected palette in persistent
// globals (byte_1409708/1409808/1409908) that are only overwritten up to the
// new palette's colour count — CopyPaletteEntries copies all 256 entries, so
// entries beyond the count replicate the PREVIOUS load. This reconstruction
// keeps the same persistent process state; indices beyond the colour count
// never occur in the quantized image.
// =============================================================================
#include "guild/common/types.h"
#include "render/texture_bin.h"

namespace guild::render {

// gilde.exe 0x6029f0 — VIBE_Quant_BuildPalette.
//   rgb        : width*height RGB triples (R,G,B — the order LoadBuffer feeds
//                after its BGR->RGB swap), top-down rows.
//   outIndices : width*height bytes (one palette index per pixel).
//   outPalette : 768 bytes, PLANAR exactly as VIBE_Quant_CopyPaletteEntries
//                writes it: [0..255]=R, [256..511]=G, [512..767]=B.
//   maxColors  : leaf budget (the texture path passes 256).
//   dither     : nonzero = the serpentine Floyd–Steinberg arm (the texture
//                path passes 1); zero = the undithered table arm.
// Returns false only on the original's allocation-failure paths (never here).
bool QuantBuildPalette(const u8* rgb, i32 width, i32 height, i32 maxColors,
                       i32 dither, u8* outIndices, u8 outPalette[768]);

// The resolve-layer wiring point: quantize a decoded 24-bit BMP in place,
// filling DecodedBmp::indices (w*h) and DecodedBmp::palette (768 bytes as
// INTERLEAVED R,G,B triples — the layout VIBE_Texture_LoadSoftPalettize builds
// from the planar copy-out, and the layout DecodedBmp uses for 8-bit sources).
// No-op (returns false) when the bmp is not a palettizable 24-bit source or
// already has indices. rgba alpha is ignored exactly as the original ignores
// the padding byte.
bool PalettizeDecodedBmp(DecodedBmp& bmp);

} // namespace guild::render

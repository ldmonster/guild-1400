#pragma once
#include "guild/common/types.h"

// Colour quantizer support from gilde.exe (d3:Quant.*). The full pipeline is an
// octree/min-heap median-cut reducer (VIBE_Quant_BuildPalette @0x6029f0). This
// module reconstructs the self-contained, golden-testable pieces:
//   * the bit-spread + squared-distance lookup tables (InitLookupTables), and
//   * the nearest-palette-colour search (FindClosestColor) + palette copy-out,
// which together perform the final image->palette index mapping. The octree
// histogram/heap internals are documented as deferred in the module report.
namespace guild::render {

// Per-process quantizer state (the original used file-scope globals).
struct QuantState {
    // Bit-spread tables: each maps one 8-bit channel value into its position in
    // the 15-bit (R5:G5:B5) octree bucket index. (word_1409508 / _14090EE / _14092EE)
    u16 spreadHi[256];   // word_1409508  (high channel)
    u16 spreadMid[256];  // word_14090EE  (mid channel,  stored at index+1)
    u16 spreadLo[256];   // word_14092EE  (low channel,  stored at index+1)
    // Squared distance LUT: sq[d] = d*d for d in [-255,255]; addressed via a base
    // pointer set to &sq[255] so sq[d] == base[d]. (dword_1409E0C / dword_140A210)
    i32 sq[511];
    i32* sqBase;         // &sq[255]

    // Active palette (R/G/B parallel arrays) and its size. (byte_1409708/808/908,
    // dword_1409A08)
    u8  palR[256];
    u8  palG[256];
    u8  palB[256];
    int palCount = 0;

    QuantState();
};

// gilde.exe 0x602aa4 — VIBE_Quant_InitLookupTables. Build spread* and sq tables.
void QuantInitLookupTables(QuantState& q);

// gilde.exe 0x603a30 — VIBE_Quant_FindClosestColor (r@al, g@dl, b@bl).
// Nearest palette index by squared distance. Each channel is masked to its top
// 5 bits and biased by +4 (matching the original's (c & 0xF8) + 4 rounding).
unsigned QuantFindClosestColor(const QuantState& q, u8 r, u8 g, u8 b);

// gilde.exe 0x602a70 — VIBE_Quant_CopyPaletteEntries. Emit the palette as planar
// R[256], G[256], B[256] into `out` (768 bytes).
void QuantCopyPaletteEntries(const QuantState& q, u8* out);

} // namespace guild::render

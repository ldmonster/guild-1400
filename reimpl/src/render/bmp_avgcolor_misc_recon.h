#pragma once
// =============================================================================
// bmp_avgcolor_misc_recon — VIBE_Bmp_GetAverageColor (gilde.exe 0x5f1af4).
//
//   char __usercall VIBE_Bmp_GetAverageColor@<al>(
//       char *bmpBytes@<eax>, int len@<edx>, char *outRgb@<edi>);
//
// Computes the palette-weighted average colour of an 8-bit paletted BMP:
//   1. read header (w,h) and decode the index buffer + 256*3 palette
//      (VIBE_Bmp_ReadHeaderInfo 0x5f0c10 / VIBE_Bmp_LoadBuffer 0x5f0ce4);
//   2. build a 256-bin histogram of palette indices over all w*h pixels;
//   3. accumulate, per channel, sum_e( palette[e][c] * count[e] / total );
//   4. truncate each accumulator toward zero (VIBE_Coord_ConvertX == trunc)
//      and store: out[2] = round(R-acc), out[1] = round(G-acc), out[0] = B-acc.
//
// The decode of (w,h,indices,palette) is the codec already implemented in
// render/bmp.cpp.  Reconstructed here is the histogram-average math kernel
// (steps 2-4), which is the substantive, 1:1 part of 0x5f1af4; it takes the
// decoded indices + palette so it stays self-contained and is golden-testable.
// =============================================================================
#include "guild/common/types.h"

namespace guild::render {

// gilde.exe 0x5f1af4 (histogram-average kernel) — VIBE_Bmp_GetAverageColor.
//   indices : w*h palette indices (`pixelCount` entries).
//   palette : 256*3 bytes, channel layout [e*3 + c]; the inner channel loop walks
//             c = 0,1,2 and writes accumulators acc[0],acc[1],acc[2] in that order.
//   outRgb  : 3 bytes; written outRgb[2]=acc0, outRgb[1]=acc1, outRgb[0]=acc2.
//   Returns 1 on success, 0 if there are no pixels.
u8 Bmp_GetAverageColor(const u8* indices, int pixelCount, const u8* palette,
                       u8* outRgb);

} // namespace guild::render

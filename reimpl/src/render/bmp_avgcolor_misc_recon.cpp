// =============================================================================
// bmp_avgcolor_misc_recon.cpp — VIBE_Bmp_GetAverageColor histogram-average
// kernel (gilde.exe 0x5f1af4). Strict 1:1 of the accumulation math.
// =============================================================================
#include "render/bmp_avgcolor_misc_recon.h"
#include <cmath>

namespace guild::render {

// gilde.exe 0x5f1af4 (kernel) — VIBE_Bmp_GetAverageColor.
//   memset(hist, 0, 1024);                       // 256 u32 bins
//   for ( pixel : indices ) ++hist[pixel];
//   total = (float)(w*h);
//   acc0 = acc1 = acc2 = 0.0;
//   for ( e = 0; e < 256; ++e ) {
//     count = (float)hist[e];
//     for ( c = 0; c < 3; ++c )
//       acc[c] += (double)(__int16)palette[3*e + c] * count / total;
//   }
//   out[2] = (int)trunc(acc0);
//   out[1] = (int)trunc(acc1);
//   out[0] = (int)trunc(acc2);
//   return 1;
u8 Bmp_GetAverageColor(const u8* indices, int pixelCount, const u8* palette,
                       u8* outRgb) {
    if (!indices || pixelCount <= 0 || !palette || !outRgb)
        return 0;                                                 /*0x5f1b18*/

    unsigned hist[256];                                           /*v26[256]*/
    for (int e = 0; e < 256; ++e) hist[e] = 0;                    /*memset(v26,0,1024)*/

    for (int p = 0; p < pixelCount; ++p)                          /*0x5f1bba loop*/
        ++hist[indices[p]];                                       /*++v26[*v11++]*/

    const float total = static_cast<float>(pixelCount);           /*v36 = (float)(w*h)*/
    // The accumulators are FLOATs in the binary (v28/v29/v30): each term is
    // computed at 80-bit ((i16)pal * count / total) and added to the float
    // accumulator with a single float store per iteration
    // (`*(float*)&... = (double)(__int16)v39 * v18 / v17 + *(float*)&...`).
    float acc0 = 0.0f, acc1 = 0.0f, acc2 = 0.0f;                  /*v28,v29,v30*/

    for (int e = 0; e < 256; ++e) {                               /*0x5f1c0d .. 0x5f1c76*/
        const float count = static_cast<float>(hist[e]);          /*v37 = (float)v31*/
        // inner: c = 0,1,2 -> acc0,acc1,acc2 (one float store each)
        acc0 = static_cast<float>(
            static_cast<double>(static_cast<i16>(palette[3 * e + 0]))
                * count / total + acc0);
        acc1 = static_cast<float>(
            static_cast<double>(static_cast<i16>(palette[3 * e + 1]))
                * count / total + acc1);
        acc2 = static_cast<float>(
            static_cast<double>(static_cast<i16>(palette[3 * e + 2]))
                * count / total + acc2);
    }

    // VIBE_Coord_ConvertX == trunc toward zero; stored as a byte.
    outRgb[2] = static_cast<u8>(static_cast<int>(acc0));             /*0x5f1c99*/
    outRgb[1] = static_cast<u8>(static_cast<int>(acc1));             /*0x5f1cb6*/
    outRgb[0] = static_cast<u8>(static_cast<int>(acc2));             /*0x5f1cd3*/
    return 1;                                                        /*0x5f1cd7*/
}

} // namespace guild::render

#include "render/terrain_render.h"

namespace guild::render {

// File-scope LOD constants recovered byte-for-byte from gilde.exe .rdata:
//   flt_628BAC = 50.0   (0x628bac: 00 00 48 42)   LOD numerator
//   dbl_628BB4 = 0.5    (0x628bb4: ... E0 3F)      LOD rounding bias
//   flt_628BA4 = -1.75  (0x628ba4: 00 00 E0 BF)    grid-X denominator bias
static constexpr float kLodNumer  = 50.0f;   // flt_628BAC
static constexpr double kLodBias  = 0.5;     // dbl_628BB4
static constexpr float kGridDenomBias = -1.75f; // flt_628BA4

// gilde.exe 0x5c5610 — VIBE_Heightmap_BuildTerrainMesh LOD clamp.
i32 SelectTileMeshLod(float scaleX) {
    // v51 = 50.0 / scaleX ; v53 = v51 + 0.5 ; t = (int)v53 (trunc toward 0,
    // VIBE_Coord_ConvertX); v54 = t / 4 (the MSVC signed div-by-4 idiom in the
    // decompile: ((t - (4*(t>>31) + cf)) >> 2) == truncating division by 4).
    double v53 = (double)(kLodNumer / scaleX) + kLodBias;
    i32 t = (i32)v53;          // ConvertX truncates toward zero
    i32 v54 = t / 4;           // truncating signed division by 4
    i32 lod = v54;
    if (lod < 1) lod = 1;      // if (v54 < 1) v54 = 1
    if (lod > 4) lod = 4;      // if (v54 > 4) v181 = 4
    return lod;
}

i32 SelectTileMeshLodFromBounds(float minX, float maxX, i32 size) {
    // scaleX = (maxX - minX) / (size + flt_628BA4)  (flt_628BA4 = -1.75)
    float scaleX = (maxX - minX) / ((float)size + kGridDenomBias);
    return SelectTileMeshLod(scaleX);
}

// gilde.exe 0x5bf22c — VIBE_Floor_RenderTerrain per-tile subdivision count.
i32 TileSubdivCount(i32 tileSpan, u8 lod, i32 colIndex, i32 rowIndex, bool isCol) {
    if (lod == 0) return 0;            // walk guards `if (v372)` before use
    // base = tileSpan / lod + 1  (v179 = v180 = v376 / v372 + 1)
    i32 base = tileSpan / (i32)lod + 1;
    i32 edge = 4 / (i32)lod;           // the `- 4 / v372` seam-stitch term
    i32 idx = isCol ? colIndex : rowIndex;
    // if (col/row index == 7) subtract the seam term (v180 = v179 - 4/v372, and
    // symmetrically v179 -= 4/v372 for the last column).
    if (idx == 7) return base - edge;
    return base;
}

} // namespace guild::render

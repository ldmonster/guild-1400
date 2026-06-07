#include "render/tile_visibility.h"

#include <cmath>

namespace guild::render {

// File-scope constants (gilde.exe .rdata, recovered via get_bytes):
//   flt_628758 = 40.0  (0x628758: 00 00 20 42)   crowd-scale numerator
//   flt_62875C = 0.4   (0x62875c: CD CC CC 3E)   crowd-scale factor
static constexpr double kCrowdNumer  = 40.0;   // flt_628758
static constexpr double kCrowdFactor = 0.4;    // flt_62875C

// gilde.exe 0x5bef08 (bound pass): centre = (sum of 8 corners) * 0.125;
// radius = sqrt(cx^2 + cy^2 + cz^2). Corners stride 20 floats (the 80-byte Vertex).
float ComputeTileCenterRadius(const float* corners, float outCentre[3]) {
    float sx = corners[0], sy = corners[1], sz = corners[2];   // v18/v20/v22 seed
    for (const float* p = corners + 20; p != corners + 160; p += 20) {
        sx += p[0];           // v18 = *v10 + v18
        sy += p[1];           // v20 = v10[1] + v20
        sz += p[2];           // v22 = v10[2] + v22
    }
    float cx = sx * kTileAvgWeight;   // v19 = v18 * flt_628B40
    float cy = sy * kTileAvgWeight;
    float cz = sz * kTileAvgWeight;
    outCentre[0] = cx;
    outCentre[1] = cy;
    outCentre[2] = cz;
    return (float)std::sqrt(cx * cx + cy * cy + cz * cz);   // *(v3+76)
}

// gilde.exe 0x5ba438 — VIBE_Floor_ComputeLodLevel (distance LOD + 8-frame debounce).
u8 ComputeLodLevel(u32 floorFlags, bool hasEntries, float thrFar, float thrNear,
                   float tileScale, float tileBias, float radius,
                   int visibleCount, u8 prevLod, u8* pendingLod, int* counter) {
    // Forced-LOD override: if (flags & 0x1C) return (8 * flags) >> 5 (byte).
    if ((floorFlags & 0x1Cu) != 0)
        return (u8)((unsigned)(u8)(8u * (u8)floorFlags) >> 5);
    if (!hasEntries)
        return 0;                          // Floor+36 == 0 -> not built

    double crowd = (kCrowdNumer - (double)visibleCount) * kCrowdFactor;  // v8
    float metric = radius / tileScale;                                   // v7
    u8 result;
    if ((double)thrFar + (double)tileBias + crowd >= (double)metric) {
        if ((double)thrNear + (double)tileBias + crowd >= (double)metric)
            result = 1;
        else
            result = 2;
    } else {
        result = 4;
    }

    // 8-frame debounce against prevLod (tile+95).
    if (prevLod == 0 || prevLod == 0xFF || result == prevLod) {
        *counter = 0;                      // LABEL_10: tile[88] = 0
        return result;
    }
    int v6 = ++(*counter);                 // v6 = ++tile[88]
    *pendingLod = result;                  // tile[97] = result
    if (v6 > 8) {
        result = *pendingLod;              // result = tile[97]
        *counter = 0;                      // tile[88] = 0
        return result;
    }
    return prevLod;                        // hold previous frame's LOD
}

// gilde.exe 0x5bef08 (stitch pass): 2:1 seam fix vs left/up neighbour, then clamp
// to the floor minimum LOD.
u8 StitchTileLod(u8 myLod, u8 leftLod, u8 upLod, u8 floorMinNibble) {
    if (myLod == 0)
        return 0;                          // unset LOD untouched
    // Left neighbour seam (leftLod == 0 means no left neighbour / col 0).
    if (leftLod != 0) {
        if ((myLod == 4 && leftLod == 1) || (myLod == 1 && leftLod == 4))
            myLod = 2;
    }
    // Up neighbour seam.
    if (upLod != 0) {
        if ((myLod == 4 && upLod == 1) || (myLod == 1 && upLod == 4))
            myLod = 2;
    }
    // Clamp to minimum LOD = 1 << (Floor+7281 & 0xF); v16 = max(minLod, myLod).
    u8 minLod = (u8)(1u << (floorMinNibble & 0x0Fu));
    if (minLod <= myLod)
        minLod = myLod;
    return minLod;
}

} // namespace guild::render

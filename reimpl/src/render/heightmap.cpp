#include "render/heightmap.h"

#include "util/coord.h"  // ConvertX (x87 truncate-toward-zero)

namespace guild::render {

namespace {
// flt_628BFC == 0.5 — added to each height byte before scaling (cell-centre bias)
// in WorldToTileWithHeight / AverageAreaHeight.
constexpr double kHalf = 0.5;
// flt_6115B4 == 1/64 (0x3C800000) — the 8x8 area-average weight.
constexpr float  kAreaAvgWeight = 1.0f / 64.0f;
// VIBE_Heightmap_BuildTerrainMesh scale constant: flt_628BA4 (size bias).
constexpr float  kSizeBias = -1.75f;
} // namespace

// gilde.exe 0x5c65d4 — VIBE_Heightmap_TileToWorld
bool TileToWorld(const Heightmap* hm, int tileX, int tileY, float out[3]) {
    if (!hm) return false;            // if (!a1) return 0
    if (tileX < 0) return false;      // if (a2 < 0) return 0
    int n = hm->size;                 // v5 = *(a1+32)
    if (tileX >= n || tileY < 0 || tileY >= n) return false;
    out[0] = (float)((double)tileX * hm->scaleX + hm->originX);
    out[1] = (float)((double)hm->heights[tileY * n + tileX] * hm->scaleY + hm->originY);
    out[2] = (float)((double)tileY * hm->scaleZ + hm->originZ);
    return true;
}

// gilde.exe 0x5c6644 — VIBE_Heightmap_WorldToTileWithHeight
//   v5 = (world.x - originX) / scaleX ; v6 = (world.z - originZ) / scaleZ
//   (truncated toward zero -> tile indices). Then bilinear interpolation of the
//   four corner heights at the fractional position inside the cell.
bool WorldToTileWithHeight(const Heightmap* hm, const float world[3],
                           int* tileXOut, int* tileYOut, float* heightOut) {
    if (!hm) return false;
    double fx = ((double)world[0] - hm->originX) / hm->scaleX;
    double fz = ((double)world[2] - hm->originZ) / hm->scaleZ;
    int tx = (int)util::ConvertX(fx);   // (int)v5 after VIBE_Coord_ConvertX
    int tz = (int)util::ConvertX(fz);   // (int)v6
    int n = hm->size;
    if (tx < 0 || tx >= n || tz < 0 || tz >= n || !tileXOut || !tileYOut)
        return false;
    *tileXOut = tx;
    *tileYOut = tz;
    if (heightOut) {
        // Fractional offset of the world point inside the cell, in cell units.
        double fracX = ((double)world[0] - ((double)tx * hm->scaleX + hm->originX))
                       / hm->scaleX;
        double fracZ = ((double)world[2] - ((double)tz * hm->scaleZ + hm->originZ))
                       / hm->scaleZ;
        const u8* h = hm->heights;
        // Corner heights (world Y). Neighbours wrap with (n-1)&(i+1) — exact for
        // power-of-two grids (the original's mask), and clamps to the last column
        // / row at the high edge otherwise.
        auto hgt = [&](int x, int z) -> double {
            return ((double)h[x + n * z] + kHalf) * hm->scaleY + hm->originY;
        };
        int nx = (n - 1) & (tx + 1);
        int nz = (n - 1) & (tz + 1);
        double h00 = hgt(tx, tz);   // base corner (v14)
        double hX  = hgt(nx, tz);   // +x neighbour
        double hZ  = hgt(tx, nz);   // +z neighbour
        *heightOut = (float)((hZ - h00) * fracZ + fracX * (hX - h00) + h00);
    }
    return true;
}

// gilde.exe 0x5c5530 — VIBE_Heightmap_FloodFillTileType
int FloodFillTileType(Heightmap* hm, u8 fromType, u8 toType) {
    int n = hm->size;
    u8* base = hm->entries;            // *(a1+36)
    int result = n - 1;
    for (int row = 1; row < n - 1; ++row) {
        if (n - 1 <= 1) break;         // matches the inner guard
        // v5 = base + 24*(n*row + 1): the (1,row) cell.
        u8* cell = base + 24 * (n * row + 1);
        for (int col = 1; col < n - 1; ++col, cell += 24) {
            if (*cell != fromType) continue;
            u8 up    = cell[-24 * n];            // cell at (col, row-1)
            if (!(up == fromType || up == toType)) continue;
            u8 down  = cell[24 * n];             // (col, row+1)
            if (!(down == fromType || down == toType)) continue;
            u8 left  = *(cell - 24);             // (col-1, row)
            if (!(left == fromType || left == toType)) continue;
            // right neighbour: *(int*)(cell+21) >> 24 == byte at cell+24.
            u8 right = cell[24];                 // (col+1, row)
            if (!(right == fromType || right == toType)) continue;
            *cell = toType;
        }
    }
    return result;
}

// gilde.exe 0x427468 — VIBE_Terrain_AverageAreaHeight
double AverageAreaHeight(const Heightmap* hm, const float world[3]) {
    double sum = 0.0;
    if (!hm) return 0.0;
    int n = hm->size;
    if (n == 0 || !hm->heights) return 0.0;
    int tx = 0, tz = 0;
    float bilinear = 0.0f;
    float w[3] = {world[0], world[1], world[2]};
    if (!WorldToTileWithHeight(hm, w, &tx, &tz, &bilinear))
        return 0.0;
    int edge = n - 1;                  // v5 = *(a1+32) - 1
    // Sum over the 8x8 block of cells [tz-4, tz+4) x [tx-4, tx+4).
    for (int z = tz - 4; z != tz + 4; ++z) {
        for (int x = tx - 4; x != tx + 4; ++x) {
            if (z >= edge || x >= edge || z <= 0 || x <= 0) {
                sum += bilinear;       // edge cells -> fall back to bilinear sample
            } else {
                // Note: the original sign-extends the height byte ((__int16)v16),
                // but the byte is 0..255 so this is just the raw value.
                u8 hb = hm->heights[z * n + x];
                sum += (double)(i16)(u8)hb * hm->scaleY + hm->originY;
            }
        }
    }
    return sum * (double)kAreaAvgWeight;
}

// gilde.exe 0x5c6438 — VIBE_Heightmap_Free
void Free(Heightmap* hm) {
    if (!hm) return;
    hm->entries = nullptr;   // *(a1+36) freed then nulled
    hm->heights = nullptr;   // *(a1+40) freed then nulled
}

// Grid-scale core of VIBE_Heightmap_BuildTerrainMesh @0x5c5610.
void DeriveGridScaleXZ(Heightmap* hm, float minX, float maxX,
                       float originZ, float minZ) {
    int n = hm->size;
    hm->originX = minX;                                       // *(v3) = v123
    // scaleX = (maxX - minX) / (size + kSizeBias)
    double denomX = (double)n + (double)kSizeBias;            // flt_628BA4 = -1.75
    hm->scaleX = (float)(((double)maxX - (double)minX) / denomX);
    hm->originZ = originZ;                                    // *(v3+8) = v128
    // scaleZ = (minZ - originZ) / (kSizeBias + size)
    double denomZ = (double)kSizeBias + (double)n;
    hm->scaleZ = (float)(((double)minZ - (double)originZ) / denomZ);
}

} // namespace guild::render

#include "sim/map.h"

#include <cmath>
#include <cstdlib>

#include "render/heightmap.h"
#include "util/transform.h"

// Faithful 1:1 port of the VIBE_Map_* collision-grid machinery from gilde.exe.
//
// The originals address the active scene map through a chain of globals
// (off_649D64 -> +44 = map object; map+32 = grid size; map+36 = tile entries).
// In the reimpl the caller resolves and passes the MapGrid view explicitly, so
// the global chain collapses to direct field reads on `g`. The cell arithmetic
// (24*(x + size*y), the type byte at +0, the walkable test value!=0 && value!=13)
// is preserved verbatim.

namespace guild::sim {

// gilde.exe dword_62D09C / dword_62D0A0 / dword_62D0A4 / dword_62D0A8.
int g_collisionDirtyMinX = 0;
int g_collisionDirtyMaxX = 0;
int g_collisionDirtyMinY = 0;
int g_collisionDirtyMaxY = 0;

void MapResetDirtyRect() {
    g_collisionDirtyMinX = 0;
    g_collisionDirtyMaxX = 0;
    g_collisionDirtyMinY = 0;
    g_collisionDirtyMaxY = 0;
}

MapGrid MapGridFromHeightmap(const guild::render::Heightmap* hm) {
    MapGrid g{};
    if (hm) {
        g.size = hm->size;        // heightmap +0x20
        g.entries = hm->entries;  // heightmap +0x24
    }
    return g;
}

u8 MapCellAt(const MapGrid& g, int x, int y) {
    return g.entries[kTileEntryStride * (x + g.size * y)];
}

void MapSetCellAt(const MapGrid& g, int x, int y, u8 value) {
    g.entries[kTileEntryStride * (x + g.size * y)] = value;
}

// gilde.exe 0x486068 — VIBE_Map_IsTileWalkable.
//   v5 = size; if (x>size||x<0||y<0||y>size) return 0;
//   if (x<size && y<size) a3 = cell(x,y);   // else a3 keeps its seed
//   return a3 && a3 != 13;
bool MapIsTileWalkable(const MapGrid& g, int a1, int a2, int a3) {
    const int v5 = g.size;
    if (a1 > v5 || a1 < 0 || a2 < 0 || a2 > v5)
        return false;
    if (a1 < v5 && a2 < v5)
        a3 = static_cast<i8>(MapCellAt(g, a1, a2));  // original loads a signed char
    return a3 != 0 && a3 != kCellBlocked;
}

// gilde.exe 0x404d10 — VIBE_Map_StampCollisionArea.
// Diamond (Manhattan-disk) fill of `value`, radius `a5`, centred at (a1,a2),
// clipped to the interior band [1, size-2] in both axes. The original walks an
// outer row counter v21 (0..min(size,radius)) bounded by v20=min(size-2, v19),
// and for each row an inner span derived from the remaining radius budget. The
// control flow below is a behavior-identical de-gotoed transcription.
int MapStampCollisionArea(const MapGrid& g, int a1, int a2, u8 a3, int a5) {
    if (!g.entries || g.size <= 0)
        return 0;

    const int size = g.size;
    int v21 = 0;     // ring index from centre row
    int v19 = a2;    // upper row tracker (grows)
    int i = a2;      // lower row tracker (shrinks): i = a2 - v21

    for (;; --i) {
        if (v21 >= size || v21 >= a5)
            break;

        int v20 = size - 2;             // clamp upper row span
        if (v20 >= v19)
            v20 = v19;

        int v8 = i;                     // clamp lower row to >= 1
        if (v8 < 1)
            v8 = 1;
        int v9 = v8;

        if (v9 <= v20) {
            int v23 = a2 - v8;          // signed distance of this row from centre
            do {
                // Remaining Manhattan budget at this row -> half-width in X.
                unsigned int v10 = static_cast<unsigned int>(v21) -
                                   static_cast<unsigned int>(std::abs(v23));
                int v11 = static_cast<int>(v10) + a1;        // right edge
                if (size - 2 < v11)
                    v11 = size - 2;
                int v12 = v11;
                int v13 = a1 - static_cast<int>(v10);        // left edge
                if (v13 < 1)
                    v13 = 1;
                for (int j = v13; j <= v12; ++j) {
                    if (v9 >= 1) {
                        int edge = size - 1;
                        if (v9 < edge && j >= 1 && j < edge)
                            g.entries[kTileEntryStride * (j + v9 * size)] = a3;
                    }
                }
                ++v9;
                --v23;
            } while (v9 <= v20);
        }
        ++v19;
        ++v21;
    }

    // Grow the dirty rectangle to cover the stamped diamond's bounding box.
    if (a1 - a5 < g_collisionDirtyMinX)
        g_collisionDirtyMinX = a1 - a5;
    if (a5 + a1 > g_collisionDirtyMaxX)
        g_collisionDirtyMaxX = a5 + a1 + 1;
    if (a2 - a5 < g_collisionDirtyMinY)
        g_collisionDirtyMinY = a2 - a5;
    if (a5 + a2 > g_collisionDirtyMaxY)
        g_collisionDirtyMaxY = a5 + a2 + 1;
    return 1;
}

// gilde.exe 0x404b90 — VIBE_Map_BuildCollisionGrid.
// out[x + size*y] = cell(x,y). The original indexes the shadow buffer as
// v2[i + col*size] while reading entries[24*(i + col*size)]; i is the column,
// the outer loop variable is the row -- same flat index either way.
void MapBuildCollisionGrid(const MapGrid& g, u8* out) {
    const int size = g.size;
    for (int i = 0; i < size; ++i) {
        for (int col = 0; col < size; ++col) {
            const int flat = i + col * size;
            out[flat] = g.entries[kTileEntryStride * flat];
        }
    }
}

// gilde.exe 0x404bf4 — VIBE_Map_ClearCollisionRegion.
// Restores cells inside the clamped dirty rect from the shadow buffer, then
// resets the dirty rect to the "empty" sentinel (min=size, max=0).
void MapClearCollisionRegion(const MapGrid& g, const u8* shadow) {
    const int size = g.size;

    if (g_collisionDirtyMinX < 0) g_collisionDirtyMinX = 0;
    if (size < g_collisionDirtyMaxX) g_collisionDirtyMaxX = size;
    if (g_collisionDirtyMinY < 0) g_collisionDirtyMinY = 0;
    if (size < g_collisionDirtyMaxY) g_collisionDirtyMaxY = size;

    for (int x = g_collisionDirtyMinX; x < g_collisionDirtyMaxX; ++x) {
        for (int y = g_collisionDirtyMinY; y < g_collisionDirtyMaxY; ++y) {
            const int flat = x + y * size;
            g.entries[kTileEntryStride * flat] = shadow[flat];
        }
    }

    g_collisionDirtyMinX = size;
    g_collisionDirtyMaxX = 0;
    g_collisionDirtyMinY = size;
    g_collisionDirtyMaxY = 0;
}

// gilde.exe 0x4860c8 — VIBE_Path_FindNearestFreeTile.
// Expanding Manhattan rings (v22 = ring radius, up to 64) around (a1,a2); the
// first walkable cell found writes its column into *outX (a3) and row into
// *outY (a4). Returns 1 on hit, 0 after 64 empty rings.
int PathFindNearestFreeTile(const MapGrid& g, int a1, int a2, int* outX, int* outY) {
    const int size = g.size;
    int v22 = 0;   // ring radius
    int v18 = a2;  // upper-row tracker

    for (int i = a2;; --i) {
        int v5 = size - 1;
        if (v5 >= v18)
            v5 = v18;
        int v21 = v5;

        int v6 = i < 0 ? 0 : i;
        int v7 = v6;                 // current row
        if (v6 <= v5) {
            int v8 = a2 - v6;        // signed row distance
            for (;;) {
                int v9 = v22 - std::abs(v8);
                int v10 = v9 + a1;   // right edge
                if (size - 1 < v9 + a1)
                    v10 = size - 1;
                int v11 = a1 - v9;   // left edge
                if (v11 < 0)
                    v11 = 0;
                int v12 = v11;       // current column
                if (v11 <= v10) {
                    while (!MapIsTileWalkable(g, v12, v7, v7)) {
                        v12 = v12 + 1;
                        if (v12 > v10)
                            goto next_row;
                    }
                    *outY = v7;
                    *outX = v12;
                    return 1;
                }
            next_row:
                ++v7;
                --v8;
                if (v7 > v21)
                    break;
            }
        }
        ++v18;
        if (++v22 >= 64)
            return 0;
    }
}

// gilde.exe 0x4861d8 — VIBE_Path_FindNearestTileToPoint.
// Minimum walkable-tile distance gate (dbl_61B12C == 30.0): a tile must be more
// than this far from the object's world point to qualify (so the object's own
// cell, distance 0, never wins).
static const double kFindNearestTileMinDist = 30.0;  // dbl_61B12C

int PathFindNearestTileToPoint(const guild::render::Heightmap* hm,
                               const float* frame, int mode,
                               int* outX, int* outY) {
    float bestMinDist = 100000000.0f;   // v35  (nearest tracker; init 1e8)
    float bestMaxDist = 0.0f;           // v36  (farthest tracker; init 0)
    int   nearX = 0, nearY = 0;         // v30, v33
    int   farX = 0,  farY = 0;          // v31, v32
    int   found = 0;                    // v5

    // World point of the object's origin (frame[19..21]) through its bone chain.
    float world[4] = {0, 0, 0, 0};      // v19[4]
    guild::util::PointThroughBoneChain(const_cast<float*>(frame),
                                       frame + 19, world);

    // Map the world point back to its containing tile; bail if off-map.
    int tileX = 0, tileY = 0;           // v21, v22
    if (!guild::render::WorldToTileWithHeight(hm, world, &tileX, &tileY,
                                              nullptr))
        return 0;

    const int size = hm->size;          // *(map+32)
    // The original samples the SAME scene map for walkability (off_649D64+44 ==
    // this heightmap); build the cell view once.
    const MapGrid grid = MapGridFromHeightmap(hm);

    for (int ring = 0; ring < 64; ++ring) {   // v28
        int rowHi = size - 1;                 // v27 = min(size-1, tileY+ring)
        if (rowHi >= ring + tileY)
            rowHi = ring + tileY;
        int rowLo = tileY - ring;             // max(0, tileY-ring)
        if (rowLo < 0)
            rowLo = 0;

        for (int i = rowLo; i <= rowHi; ++i) {
            int budget = ring - std::abs(tileY - i);   // v9 (column half-width)
            int colHi = size - 1;                       // v34 = min(size-1, tileX+budget)
            if (colHi >= budget + tileX)
                colHi = budget + tileX;
            int colLo = tileX - budget;                 // max(0, tileX-budget)
            if (colLo < 0)
                colLo = 0;

            for (int j = colLo; j <= colHi; ++j) {
                // The original seeds IsTileWalkable with (budget+tileX); it only
                // matters on the size-edge (unreachable for interior j,i<size).
                if (!MapIsTileWalkable(grid, j, i, budget + tileX))
                    continue;

                float w[4] = {0, 0, 0, 0};  // v16/v17/v18 (+ scratch)
                guild::render::TileToWorld(hm, j, i, w);
                float dx = world[0] - w[0];
                float dy = world[1] - w[1];
                float dz = world[2] - w[2];
                double dist = std::sqrt((double)dx * dx + (double)dy * dy +
                                        (double)dz * dz);

                if (dist < (double)bestMinDist &&
                    dist > kFindNearestTileMinDist) {
                    found = 1;
                    nearX = j;
                    nearY = i;
                    bestMinDist = (float)dist;
                }
                if (dist > (double)bestMaxDist &&
                    dist > kFindNearestTileMinDist) {
                    found = 1;
                    farX = j;
                    farY = i;
                    bestMaxDist = (float)dist;
                }
            }
        }
    }

    if (!found)
        return 0;
    if (mode) {            // v23: farthest
        *outX = farX;
        *outY = farY;
    } else {               // nearest
        *outX = nearX;
        *outY = nearY;
    }
    return 1;
}

// gilde.exe 0x406f10 — VIBE_Map_TraceLineOfSight.
int MapTraceLineOfSight(const guild::render::Heightmap* hm, const float* query,
                        int targetCol, const float* ref, int targetRow,
                        int* outCol, int* outRow, int maxRings, int flags) {
    if (!hm || !hm->entries || !outCol || !outRow)
        return 0;

    const int size = hm->size;          // *(a1+32)
    const u8* entries = hm->entries;     // *(a1+36)
    auto cell = [&](int col, int row) -> u8 {
        return entries[kTileEntryStride * (col + row * size)];
    };

    float targetWorld[4] = {0, 0, 0, 0};  // v37/v38/v39 (target world point)

    if (flags & 0x10) {
        // bit4: short-circuit if the target tile is itself walkable.
        u8 tc = cell(targetCol, targetRow);
        if (tc != 0 && tc != 13) {
            *outCol = targetCol;
            *outRow = targetRow;
            return 1;
        }
        targetWorld[0] = ref[0];        // v37 = *a4
        targetWorld[1] = ref[1];        // v38 = a4[1]
        targetWorld[2] = ref[2];        // v39 = a4[2]
    } else {
        guild::render::TileToWorld(hm, targetCol, targetRow, targetWorld);
    }

    const float qx = query[0];          // v54
    const float qy = query[1];          // v55
    const float qz = query[2];          // v45
    *outCol = -1;
    *outRow = -1;

    if ((flags & 1) != 0 || (flags & 0x10) != 0) {
        // ---- "best approach" min-distance scan -----------------------------
        float bestSum = 9.9999998e10f;          // v59
        int ring = 0;                           // v47
        int rowHi0 = targetRow + ring;          // v48
        int rowLo0 = targetRow - ring;          // v51
        (void)rowHi0; (void)rowLo0;
        for (; ; ++ring) {
            if (ring >= size || ring >= maxRings)
                break;
            int rowHi = size - 1;               // v43 = min(size-1, targetRow+ring)
            if (rowHi >= targetRow + ring)
                rowHi = targetRow + ring;
            int rowLo = targetRow - ring;       // max(1, targetRow-ring)
            if (rowLo < 1)
                rowLo = 1;
            if (rowLo <= rowHi) {
                int signedRow = targetRow - rowLo;   // v44
                for (int row = rowLo; row <= rowHi; ++row, --signedRow) {
                    int budget = ring - std::abs(signedRow);     // v27
                    int colHi = size - 1;                         // v56
                    if (colHi >= budget + targetCol)
                        colHi = budget + targetCol;
                    int colLo = targetCol - budget;               // max(0,...)
                    if (colLo < 0)
                        colLo = 0;
                    for (int col = colLo; col <= colHi; ++col) {
                        if (row < 0 || row >= size || col < 0 || col >= size)
                            continue;
                        u8 c = cell(col, row);
                        if (c == 0 || c == 13)
                            continue;
                        float w[4] = {0, 0, 0, 0};   // v34/v35/v36
                        if (!guild::render::TileToWorld(hm, col, row, w))
                            continue;
                        float ax = qx - w[0], ay = qy - w[1], az = qz - w[2];
                        double d1 = std::sqrt((double)ax * ax + (double)ay * ay +
                                              (double)az * az);   // v57
                        float bx = targetWorld[0] - w[0];
                        float by = targetWorld[1] - w[1];
                        float bz = targetWorld[2] - w[2];
                        double d2 = std::sqrt((double)bx * bx + (double)by * by +
                                              (double)bz * bz);   // v58
                        double sum = d1 + d2;                      // v33
                        if (sum < (double)bestSum) {
                            bestSum = (float)sum;
                            *outCol = col;
                            *outRow = row;
                        }
                    }
                }
            }
        }
    } else {
        // ---- "first walkable" spiral (returns immediately) ------------------
        int ring = 0;                           // v47
        for (; ; ++ring) {
            if (ring >= size || ring >= maxRings)
                break;
            int rowHi = size - 1;               // v52 = min(size-1, targetRow+ring)
            if (rowHi >= targetRow + ring)
                rowHi = targetRow + ring;
            int rowLo = targetRow - ring;       // max(1, targetRow-ring)
            if (rowLo < 1)
                rowLo = 1;
            if (rowLo <= rowHi) {
                int signedRow = targetRow - rowLo;   // v46
                for (int row = rowLo; row <= rowHi; ++row, --signedRow) {
                    int budget = ring - std::abs(signedRow);     // v16
                    int colHi = budget + targetCol;               // v17
                    if (size - 1 < colHi)
                        colHi = size - 1;
                    int colLo = targetCol - budget;               // v18
                    if (colLo < 0)
                        colLo = 0;
                    if (colLo > colHi)
                        continue;
                    for (int col = colLo; col <= colHi; ++col) {
                        if (row < 0 || row >= size || col < 0 || col >= size)
                            continue;
                        u8 c = cell(col, row);
                        if (c != 0 && c != 13) {
                            *outCol = col;
                            *outRow = row;
                            return 1;
                        }
                    }
                }
            }
        }
    }

    if (*outCol != -1 || *outRow != -1)
        return 1;
    *outCol = targetCol;
    *outRow = targetRow;
    return 1;
}

// gilde.exe 0x404ef8 — VIBE_Map_StampEntityCollision.
int MapStampEntityCollision(const guild::render::Heightmap* hm, const float* world,
                            u8 value, int profile, int radius) {
    // The original resolves the scene map: v7 = dword_13ECF78[246*profile]; if it
    // is null OR the world point is off the map -> -1.
    if (!hm)
        return -1;
    int tileX = 0, tileY = 0;          // v9[0], v9[1]
    if (!guild::render::WorldToTileWithHeight(hm, world, &tileX, &tileY, nullptr))
        return -1;
    (void)profile;  // profile selected the map (already resolved into `hm`).
    const MapGrid g = MapGridFromHeightmap(hm);
    return MapStampCollisionArea(g, tileX, tileY, value, radius);
}

// gilde.exe 0x408740 — VIBE_Map_CheckPathWalkable.
int MapCheckPathWalkable(const MapGrid& g, const u8* waypoints, int cap, int curIdx,
                         int seed) {
    // v5 = cap; if (cap < 2) return seed.
    if (cap < 2)
        return seed;
    int hi = cap - 2;                  // v6 = cap-2 (upper bound, exclusive in loop)
    if (curIdx + 2 < cap - 2)          // if (curIdx+2 < cap-2) hi = curIdx+2
        hi = curIdx + 2;
    int i = (curIdx >= 1) ? curIdx : 1;   // v8 = max(curIdx, 1)
    if (i >= hi)                        // empty range -> pass through seed
        return seed;
    const u8* p = waypoints + 2 * i;    // v9 = buffer + 2*v7
    do {
        int col = p[0];                 // *v9
        int row = p[1];                 // v9[1]
        u8 c = g.entries[kTileEntryStride * (g.size * row + col)];
        if (c == 0 || c == kCellBlocked)
            return 0;
        ++i;
        p += 2;
    } while (i < hi);
    return seed;
}

// gilde.exe 0x577320 — VIBE_Map_FindNearestDoorCell (scan portion).
int MapFindNearestDoorCell(const MapGrid& g, int startCol, int startRow,
                           int* doorCol, int* doorRow) {
    // Only scan when no door has been resolved yet (both >= 0 means "keep it").
    if (*doorCol >= 0 && *doorRow >= 0)
        return 1;

    const int size = g.size;
    int bestCol = -1;                         // v18
    int bestRow = -1;                         // a5
    // Initial best = 2*size*size (larger than any in-grid squared offset).
    float best = (float)(2 * size * size);    // v19
    int rowOff = -startRow;                    // v17 = row - startRow (starts -startRow)

    for (int row = 0; row < size; ++row, ++rowOff) {
        int colOff = -startCol;               // v9 = col - startCol
        for (int col = 0; col < size; ++col, ++colOff) {
            u8 c = g.entries[kTileEntryStride * (col + row * size)];
            if (c == 6 || c == 11) {
                double d = (double)(rowOff * rowOff + colOff * colOff);  // v13
                if (d < (double)best) {
                    bestCol = col;
                    bestRow = row;
                    best = (float)d;
                }
            }
        }
    }

    if (bestCol < 0 || bestRow < 0)
        return 0;
    *doorCol = bestCol;
    *doorRow = bestRow;
    return 1;
}

} // namespace guild::sim

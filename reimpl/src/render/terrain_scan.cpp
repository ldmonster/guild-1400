#include "render/terrain_scan.h"

namespace guild::render {

// gilde.exe 0x426da0 — VIBE_Terrain_ScanRowHeightRange
int ScanRowHeightRange(const TerrainHeightGrid* grid, int* lowOut, int xStart,
                       int* highOut, int xEnd, int row) {
    const int width = grid->width;            // *a1

    // row must fall inside the square grid.
    if (row < 0 || row >= width)              // if (a6 < 0 || a6 >= *a1)
        return 1;

    int lo = xStart;                          // a3 (ecx)
    int hi = xEnd;                            // v6 = a5
    if (xEnd < xStart) {                      // if (a5 < a3) swap
        lo = xEnd;
        hi = xStart;
    }

    // Whole span clipped off the row?
    if (lo >= width || hi < 0)                // if (a3 >= *a1 || v6 < 0)
        return 1;

    if (lo < 0)                               // if (a3 < 0) a3 = 0
        lo = 0;
    if (hi >= width - 1)                      // if (v6 >= *a1 - 1) v6 = *a1 - 1
        hi = width - 1;

    // Fold each cell elevation into the running [min,max] range. The original
    // walks a byte pointer i = heights + lo + width*row, incrementing per column.
    const u8* cell = grid->heights + lo + width * row;
    for (int x = lo; x <= hi; ++x, ++cell) {
        int v = *cell;                        // v10 = *i
        if (v > *lowOut)                      // if (v10 > *a2) v10 = *a2
            v = *lowOut;
        *lowOut = v;                          // *a2 = min(*a2, cell)

        int w = *cell;                        // v11 = *i
        if (w < *highOut)                     // if (v11 < *a4) v11 = *a4
            w = *highOut;
        *highOut = w;                         // *a4 = max(*a4, cell)
    }
    return 0;
}

} // namespace guild::render

#include "render/terrain.h"

namespace guild::render {

// gilde.exe 0x5bbbf4 — VIBE_Floor_TileIsUniform
//   v6 = -1 (no reference yet);  xEnd = x0+span;  yEnd = y0+span;
//   if (y0 > yEnd) return 1;
//   for (y = y0; y <= yEnd; ++y)
//     for (x = x0; x <= xEnd; ++x) {
//        idx = (mask & y)*size + (x & mask);
//        if (v6 == -1) v6 = types[idx];          // seed reference
//        else if (v6 != types[idx]) return 0;    // mismatch
//     }
//   return 1;
bool TileIsUniform(const TileGrid* grid, int x0, int span, int y0) {
    int xEnd = x0 + span;
    int yEnd = y0 + span;
    if (y0 > yEnd) return true;          // if (a4 > a4 + a3) return 1
    int ref = -1;                        // v6 = -1
    for (int y = y0; y <= yEnd; ++y) {
        for (int x = x0; x <= xEnd; ++x) {
            i32 idx = (grid->mask & y) * grid->size + (x & grid->mask);
            u8 t = grid->types[idx];
            if (ref == -1) {
                ref = t;                 // seed on first cell
            } else if ((u8)ref != t) {
                return false;            // mismatch
            }
        }
    }
    return true;
}

} // namespace guild::render

#include "render/terrain_tile_query.h"

#include <cstdlib>   // abs

namespace guild::render {

// gilde.exe 0x5c3418 — VIBE_Heightmap_LookupTileAttribute
//   Original: for (i = a1[200*(y/bs) + 69 + 25*(x/bs)]; i; i += 20) { ... }
//   The list head and the +20-byte chaining are pointer math in the 32-bit
//   binary; we model the head as a TileAttrEntry* and chain with ++entry.
int LookupTileAttribute(const TileAttributeGrid* grid, int x, int y) {
    const int size = grid->size;                  // *a1
    if (x > 0 && y > 0 && x < size && y < size) {
        const int bs = grid->bucketSize;          // a1[1]
        // Original int-array index of the bucket head:
        const int slot = 200 * (y / bs) + 69 + 25 * (x / bs);
        // buckets[] is the int array shifted by the two leading size/bucketSize
        // ints (a1[0], a1[1]), so the head lives at slot - 2.
        const TileAttrEntry* entry = grid->buckets[slot - 2];
        for (; entry; ++entry) {                  // i += 20 (sizeof entry)
            int row = entry->row;                 // *(i+4)
            if (row < 0)                           // negative -> end of list
                break;
            if (y == row && x >= entry->xMin && x <= entry->xMax)
                return entry->attribute;          // *(unsigned __int8 *)(i+16)
        }
    }
    return -1;
}

// gilde.exe 0x5c6478 — VIBE_Heightmap_FindNearestWalkableTile
bool FindNearestWalkableTile(const TileWalkGrid* grid, int centerY, int* foundX,
                             int centerX, int* foundY) {
    int ring = 0;                                  // v22 (current diamond radius)
    if (grid && grid->cells && foundX && foundY) {
        int colBase = centerX;                     // v20 (shrinks each ring)
        for (int i = centerX; ; ++i) {             // i grows each ring
            const int size = grid->size;           // v7 = *(a1+32)
            if (ring >= size)                      // searched every ring -> miss
                break;

            int rowHi = size - 1;                  // v8
            if (size - 1 >= i)                     // v8 = min(size-1, i)
                rowHi = i;
            const int v21 = rowHi;

            int rowLo;                             // v9 = max(0, colBase)
            if (colBase < 0)
                rowLo = 0;
            else
                rowLo = colBase;

            int row = rowLo;                       // v10 (current row, iterates up)
            if (rowLo <= v21) {
                int diag = centerX - rowLo;        // v24
                for (;;) {                          // per-row loop in this ring
                    // Column span for this row within the diamond.
                    int span = ring - std::abs(diag);          // v11
                    int colHi = span + centerY;                // v12
                    if (size - 1 < colHi)                      // clamp high
                        colHi = size - 1;
                    int colLo = centerY - span;                // v13
                    if (colLo < 0)
                        colLo = 0;

                    int col = colLo;               // v14 (current column)
                    if (colLo <= colHi) {
                        bool brk = false;          // inner column scan
                        for (;;) {
                            if (col >= 0) {
                                int s = size;      // v15
                                if (col < s && row >= 0 && row < s) {
                                    // @0x5c6550: imul edx,ecx(row); add edx,eax(col) ->
                                    // index = col + row*size  (NOT row + col*size).
                                    u8 t = grid->cells[24 * (col + row * s)];
                                    if (t && t != 13) { brk = true; break; }
                                }
                            }
                            if (++col > colHi)
                                break;             // exhausted row's columns
                        }
                        if (brk) {                 // walkable found
                            *foundX = col;         // *a3 = v14 (column)
                            *foundY = row;         // *a5 = v10 (row)
                            return true;
                        }
                    }
                    // advance to next row in this ring
                    ++row;                         // v10
                    --diag;                        // v24
                    if (row > v21)
                        break;
                }
            }
            --colBase;                             // v20
            ++ring;                                // v22
        }
    }
    return false;
}

} // namespace guild::render

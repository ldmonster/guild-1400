#include "render/terrain_mesh.h"
#include "render/terrain.h"   // TileGrid + VIBE_Floor_TileIsUniform (reused)

namespace guild::render {

// Static subdivision-span table — gilde.exe byte_5B8CE3 = {1, 2, 4} (recovered via
// get_bytes @0x5b8ce3). MarkUniformTiles walks the grid at each of these spans.
static constexpr u8 kUniformSpans[3] = {1, 2, 4};

// gilde.exe 0x5bdec4 — VIBE_Floor_ComputeTileVertices.
// Build the 8 corner vertices (4 XZ corners * 2 height samples) of one tile.
void ComputeTileVertices(const TileBuildParams& p, i32 tileSpan, u8 cornerHeight,
                         u8 gridHeight, i32 colIndex, i32 rowIndex, float out[24]) {
    // v4 = Floor.tileSpan. Sample coordinates of the tile's four XZ corners.
    const float colA = (float)(colIndex * tileSpan);        // v42 = a4*v4
    const float colB = (float)(tileSpan * (colIndex + 1));  // v34 = v4*(a4+1)
    const float rowA = (float)(rowIndex * tileSpan);        // v43 = a3*v4
    const float rowB = (float)(tileSpan * (rowIndex + 1));  // v39 = v4*(a3+1)

    // Column base = origin + col*axisU   (the flt_1404250.. + colN*flt_13FFD40.. adds).
    const float colBaseA0 = p.origin[0] + colA * p.axisU[0];  // v27
    const float colBaseA1 = p.origin[1] + colA * p.axisU[1];  // v25
    const float colBaseA2 = p.origin[2] + colA * p.axisU[2];  // v26
    const float colBaseB0 = p.origin[0] + colB * p.axisU[0];  // v51
    const float colBaseB1 = p.origin[1] + colB * p.axisU[1];  // v52
    const float colBaseB2 = p.origin[2] + colB * p.axisU[2];  // v24

    // Row offsets = row*axisRow  (the rowN*flt_13FD500.. terms v44/v38/v40, v36/v35/v37).
    const float rowOffA0 = rowA * p.axisRow[0];  // v44
    const float rowOffA1 = rowA * p.axisRow[1];  // v38
    const float rowOffA2 = rowA * p.axisRow[2];  // v40
    const float rowOffB0 = rowB * p.axisRow[0];  // v36
    const float rowOffB1 = rowB * p.axisRow[1];  // v35
    const float rowOffB2 = rowB * p.axisRow[2];  // v37

    // The four XZ corners (no height yet): A=(col,row) B=(col+1,row) C=(col+1,row+1)
    // D=(col,row+1). (v32/v33/v31, v61/v53/v57, v62/v54/v58, v29/v28/v30.)
    const float A0 = colBaseA0 + rowOffA0, A1 = colBaseA1 + rowOffA1, A2 = colBaseA2 + rowOffA2;
    const float B0 = colBaseB0 + rowOffA0, B1 = colBaseB1 + rowOffA1, B2 = colBaseB2 + rowOffA2;
    const float C0 = colBaseB0 + rowOffB0, C1 = colBaseB1 + rowOffB1, C2 = colBaseB2 + rowOffB2;
    const float D0 = colBaseA0 + rowOffB0, D1 = colBaseA1 + rowOffB1, D2 = colBaseA2 + rowOffB2;

    // Height-axis lift for sample 1 (cornerHeight == tileRec[+92]): v50/v49/v45.
    const float h1 = (float)cornerHeight;
    const float lift1_0 = h1 * p.axisHeight[0];
    const float lift1_1 = h1 * p.axisHeight[1];
    const float lift1_2 = h1 * p.axisHeight[2];

    // Group 1: the four corners lifted by h1 (flt_1404E74 / EC4 / F14 / F64).
    out[0]  = A0 + lift1_0; out[1]  = A1 + lift1_1; out[2]  = A2 + lift1_2;  // E74 (A)
    out[3]  = B0 + lift1_0; out[4]  = B1 + lift1_1; out[5]  = B2 + lift1_2;  // EC4 (B)
    out[6]  = C0 + lift1_0; out[7]  = C1 + lift1_1; out[8]  = C2 + lift1_2;  // F14 (C)
    out[9]  = D0 + lift1_0; out[10] = D1 + lift1_1; out[11] = D2 + lift1_2;  // F64 (D)

    // Height-axis lift for sample 2 (gridHeight == floor[100*col+800*row+317]):
    // v48/v46/v47 = v13 * flt_13FD520/24/28.
    const float h2 = (float)gridHeight;
    const float lift2_0 = h2 * p.axisHeight[0];
    const float lift2_1 = h2 * p.axisHeight[1];
    const float lift2_2 = h2 * p.axisHeight[2];

    // Group 2: the same four corners lifted by h2 (flt_1404FB4 / 5004 / 5054 / 50A4).
    out[12] = A0 + lift2_0; out[13] = A1 + lift2_1; out[14] = A2 + lift2_2;  // FB4 (A)
    out[15] = B0 + lift2_0; out[16] = B1 + lift2_1; out[17] = B2 + lift2_2;  // 5004 (B)
    out[18] = C0 + lift2_0; out[19] = C1 + lift2_1; out[20] = C2 + lift2_2;  // 5054 (C)
    out[21] = D0 + lift2_0; out[22] = D1 + lift2_1; out[23] = D2 + lift2_2;  // 50A4 (D)
}

// gilde.exe 0x5ba704 — VIBE_Floor_InvalidateTiles.
u32 InvalidateTiles(u32 floorFlags, u8* tileLodStates, u32* outFlags) {
    // *(a1+7280) |= 1  (set the geometry-dirty bit).
    floorFlags |= 1u;
    // Walk the 8x8 tile grid; the original writes 0xFF to tile record +219 (the
    // per-tile LOD-state byte) of every tile. The two nested do/while loops cover
    // all 64 tiles (8 rows of 8, 800/row, 100/tile).
    for (int i = 0; i < 64; ++i)
        tileLodStates[i] = 0xFF;          // *(result+219) = -1
    if (outFlags) *outFlags = floorFlags;
    return floorFlags;
}

// gilde.exe 0x5bbc74 — VIBE_Floor_MarkUniformTiles.
u32 MarkUniformTiles(const u8* types, i32 size, i32 mask, u8* flagPlanes[3],
                     u32 floorFlags, u32* outFlags) {
    TileGrid grid{size, mask, types};   // the descriptor TileIsUniform indexes
    // v12 selects the span (1/2/4); v11 indexes the matching flag plane (the
    // original's *(a1+36+4*k)). For each span, walk the grid in span*span blocks.
    for (int k = 0; k < 3; ++k) {
        const int span = (int)kUniformSpans[k];     // v10 = byte_5B8CE3[k]
        const int blocks = size / span;             // v9 = size / span
        u8* plane = flagPlanes[k];                  // *(a1 + 36 + 4*k)
        if (blocks <= 0) continue;
        int destBase = 0;                           // v7 (block index accumulator)
        int y0 = 0;                                 // v13 = block-row * span
        for (int by = 0; by < blocks; ++by) {       // v8 < v9
            int dest = destBase;                    // v2 = v7
            int x0 = 0;                             // v3 = block-col * span
            for (int bx = 0; bx < blocks; ++bx) {   // v1 < v9
                bool uniform = TileIsUniform(&grid, x0, span, y0);
                if (uniform)
                    plane[dest] |= 0x40u;           // |= 0x40
                else
                    plane[dest] &= (u8)~0x40u;      // &= ~0x40
                ++dest;                             // ++v2
                x0 += span;                         // v3 += v10
            }
            destBase += blocks;                     // v7 += v9
            y0 += span;                             // v13 += v10
        }
    }
    floorFlags |= 1u;                               // *(a1+7280) |= 1
    if (outFlags) *outFlags = floorFlags;
    return floorFlags;
}

// gilde.exe 0x5bbdb0 — VIBE_Floor_ComputeSlopeFlags (per-tile min/max elevation, the
// self-contained third pass). For each of the 64 tiles scan its (tileSpan+1)^2
// sample block over the primary and (when present) overlay grids.
void SummarizeTileElevations(const u8* heights, const u8* overlay, i32 size,
                             i32 mask, i32 tileSpan, TileElevationSummary* out) {
    // for (j = 0; j < 8; ++j)  outer tile row; tile col index 0..7 inner.
    for (int j = 0; j < 8; ++j) {                       // tile row
        for (int tileCol = 0; tileCol < 8; ++tileCol) { // tile column (v39)
            u8 hi = 0;                                  // v16 = 0
            u8 lo = 0xFF;                               // v17 = -1
            int rowSample = tileSpan * j;               // v45 = v15*j
            // v46 = 0; while (v46 <= tileSpan): (tileSpan+1) rows inclusive.
            for (int r = 0; r <= tileSpan; ++r, ++rowSample) {
                const int rowIdx = (mask & rowSample) * size;        // v49
                int colSample = tileSpan * tileCol;                  // v19 = v15*v39
                // v18 = 0; while (v18 <= tileSpan): (tileSpan+1) cols inclusive.
                for (int c = 0; c <= tileSpan; ++c, ++colSample) {
                    const u8 hv = heights[rowIdx + (mask & colSample)]; // primary cell
                    if (hi <= hv) hi = hv;              // v16 = max
                    if (lo >= hv) lo = hv;              // v17 = min
                    if (overlay) {                      // if (*(a1+24))
                        const u8 ov = overlay[(mask & colSample) + size * (mask & rowSample)];
                        if (ov) {                       // if (v21) — 0 skips overlay
                            if (hi <= ov) hi = ov;
                            if (lo >= ov) lo = ov;
                        }
                    }
                }
            }
            const int t = j * 8 + tileCol;
            out->minHeight[t] = lo;                     // v40[316] = v17
            out->maxHeight[t] = hi;                     // v40[317] = v16
        }
    }
}

} // namespace guild::render

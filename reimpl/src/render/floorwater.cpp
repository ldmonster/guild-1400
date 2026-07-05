#include "render/floorwater.h"

#include "util/coord.h"  // ConvertX (x87 truncate-toward-zero)

#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace guild::render {

namespace {
// Wave-generator constants (gilde.exe dbl_628Axx, recovered via get_bytes).
constexpr double kC27 = 2.7;   // dbl_628AEC
constexpr double kC24 = 2.4;   // dbl_628AFC
constexpr double kC25 = 2.5;   // dbl_628B04
constexpr double kC26 = 2.6;   // dbl_628B0C
constexpr double kPi  = 3.141592653589793; // dbl_628B14 (0x400921FB54442EEA)
constexpr double kC22 = 2.2;   // dbl_628B1C
constexpr double kC40 = 4.0;   // dbl_628B24
// flt_628760 == 0.5 — gradient round bias.
constexpr float  kHalf = 0.5f;
} // namespace

// gilde.exe 0x5ba750 — VIBE_FloorWater_FloodFillMask
//   mask[y*stride + x] = to; then recurse into the four neighbours == from.
// The original is an iterative tail-recursion on the -y direction with explicit
// recursive calls for +x/-x/+y.
//
// MEMORY-SAFETY (wave-10): the original's true recursion overflows the call stack
// on a large connected water region (ASAN: stack-overflow at N>=1024 full-grid;
// the largest shipped grid N=128 was already ~1056 spans deep, and an N=4096
// floor would blow the stack outright). The fill assigns ONE region id to a whole
// 4-connected component, so the result is INDEPENDENT of visit order — converting
// the recursion to an explicit work-stack is therefore byte-identical in the
// observable region grid while removing the unbounded native stack growth. The
// neighbour set, the bounds tests, and the `== from` predicate are preserved
// exactly (the recursive form's `from`-equality guards before each descent map
// 1:1 onto the push guards below). The local stack is the SAME (y-1) tail-walk
// the original used, just made explicit so depth is heap-bounded.
void FloodFillMask(u8* mask, int stride, int y, int x, u8 from, u8 to) {
    if (from == to)                                // would loop forever otherwise
        return;                                    // (the original never fills to==from)
    if (x < 0 || x >= stride || y < 0 || y >= stride)
        return;
    if (mask[x + y * stride] != to && mask[x + y * stride] != from)
        return;                                    // seed not fillable
    std::vector<std::pair<int, int>> work;
    work.emplace_back(y, x);
    while (!work.empty()) {
        const int cy = work.back().first;
        const int cx = work.back().second;
        work.pop_back();
        // The original's inner `for(;;)` walks -y while cells stay == from; we push
        // each row cell of that run and let the stack carry the +x/-x/+y descents.
        int wy = cy;
        for (;;) {
            mask[cx + wy * stride] = to;                       // *v8 = a6
            if (cx + 1 < stride && mask[cx + wy * stride + 1] == from)
                work.emplace_back(wy, cx + 1);                 // +x neighbour
            if (cx - 1 >= 0 && mask[(cx - 1) + wy * stride] == from)
                work.emplace_back(wy, cx - 1);                 // -x neighbour
            if (wy + 1 < stride && mask[cx + (wy + 1) * stride] == from)
                work.emplace_back(wy + 1, cx);                 // +y neighbour
            --wy;                                              // -y: iterate
            if (wy < 0) break;
            if (mask[cx + wy * stride] != from) break;
        }
    }
}

// gilde.exe 0x5ba898 — VIBE_FloorWater_FillHeightGradient
//   step = (hiVal - loVal) / (hi - lo); v = loVal; for i in [lo,hi]:
//     dst[i] = (int)(v + 0.5); v += step;
void FillHeightGradient(u8* dst, int lo, int hi, u8 loVal, u8 hiVal) {
    if (lo > hi) return;
    double v = (double)(float)loVal;                       // v9 = (float)a2
    // v8 is a FLOAT stack slot: the quotient is fstp'd to dword and re-loaded
    // each iteration (only the accumulator v4 stays 80-bit on the FPU).
    const float step = (float)((double)(hiVal - loVal) / (double)(hi - lo));
    for (int i = lo; i <= hi; ++i) {
        // VIBE_Coord_ConvertX truncates (v + 0.5) toward zero.
        dst[i] = (u8)(int)util::ConvertX(v + (double)kHalf);
        v += (double)step;
    }
}

// gilde.exe 0x5ba824 — VIBE_FloorWater_FindRegionOffset.
//   Walk 20-byte span records (Floor+52); match type + [lo,hi] containment of
//   `query`; stamp the visited marker; return the 80-byte-strided buffer offset.
i32 FindRegionOffset(WaterRegionSpan* spans, i32 spanCount, i32 bufferBase,
                     i32 query, u8 marker, i32 type) {
    if (spans == nullptr)               // a1 == 0 -> 0
        return 0;
    if (spanCount <= 0)                 // *(a1+52) == 0 -> 0 (empty list)
        return 0;
    // Gate: the FIRST span's type < 0 means "no regions" (return 0).
    if (spans[0].type < 0)              // *(int*)(v6+4) >= 0 required to enter
        return 0;
    // Scan: advance while (type mismatch) || (query < lo) || (query > hi). The
    // original peeks the NEXT record's type (rec[+24] == next.type) and bails when
    // it is negative (end-of-list) BEFORE testing the next record.
    int i = 0;
    while (type != spans[i].type || query < spans[i].lo || query > spans[i].hi) {
        // v9 = next.type (spans[i+1].type); rec += 20; if (v9 < 0) return 0.
        i32 nextType = (i + 1 < spanCount) ? spans[i + 1].type : -1;
        ++i;
        if (nextType < 0)
            return 0;                   // terminator: no matching span
    }
    WaterRegionSpan& s = spans[i];
    if (s.marker == 0xFFu)              // unmarked -> stamp the visited marker
        s.marker = marker;
    return 80 * (s.base + query - s.lo) + bufferBase;
}

// gilde.exe 0x5be428 — VIBE_Floor_AnimateWaterVertices (16-vertex wave loop).
void AnimateWaterWaveGrid(float* out, const float amp[4], const float phase[4],
                          double t) {
    for (int c = 0; c < 16; ++c) {
        double dc = (double)c;
        double x = std::sin(std::cos(dc * kC27) * t + dc + (double)phase[0]);
        double y = std::cos(std::sin(kPi - dc * kC24) * t + dc + (double)phase[1]);
        double z = std::sin(dc - std::cos(dc * kC25) * t + kC22 + (double)phase[2]);
        double w = std::cos(dc - std::sin(dc * kC26 + kPi) * t + kC40 + (double)phase[3]);
        float* o = out + 4 * c;
        o[0] = (float)((double)amp[0] * x);
        o[1] = (float)((double)amp[1] * y);
        o[2] = (float)((double)amp[2] * z);
        o[3] = (float)((double)amp[3] * w);
    }
}

// ===========================================================================
// gilde.exe 0x5ba95c — VIBE_FloorWater_PrepareRegions (builder form).
// Reconstructed 1:1 over the parsed grid. See floorwater.h for the pass map.
// ===========================================================================
namespace {

// The mesh-record initializer constants (the 0x5bb67e.. integer stores, written
// as raw IEEE-754 bit patterns by the original). 86 floats == 344 bytes.
// v102[5..13] = {1115422720, 1018980991, 1017370378, 1021128475, 1022739087,
//                1065353216, 1065353216, 1065353216, 1106771968}.
constexpr u32 kMeshInit[9] = {
    1115422720u, 1018980991u, 1017370378u, 1021128475u, 1022739087u,
    1065353216u, 1065353216u, 1065353216u, 1106771968u};

} // namespace

WaterRegions BuildWaterRegions(const u8* waterMaskGrid, const u8* terrainHeights,
                               const u8* waterHeights, u8 waterType, int n,
                               WaterTextureLoadFn loadTexture, void* ctx) {
    WaterRegions out;
    out.n = n;
    if (n <= 0 || waterMaskGrid == nullptr)
        return out;

    const int total = n * n;            // *a1 * *a1
    const int wrap  = total - 1;        // a1[3] == N*N-1
    int tileSpan = n / 8;               // a1[1] (Floor+4)
    if (tileSpan <= 0) tileSpan = 1;    // guard (shipped scenes: N in {64,128} -> 8/16)

    // === Pass 1 (loc_5BAA1D): water-mask + 3x3 wrap-around dilation ==========
    // v177 = AllocDebug(total) "d3_fl:Water(water_mask)"; the original does NOT
    // zero it (it is fully overwritten by the dilation stamps where water exists
    // and read elsewhere only at stamped/derived cells), but to be deterministic
    // we zero-init (cells never stamped read as 0 == "not water", matching the
    // height-AND's `*v20 == -1` test which only fires on stamped 0xFF cells).
    out.mask.assign((std::size_t)total, 0);
    u8* mask = out.mask.data();
    bool anyWater = false;              // v215

    // v15 = row in [0, N-4); v16 = col in [0, N-4). v205/v206 == v16-1/v15-1.
    // MEMORY-SAFETY (wave-10): the original computes these dilation indices in
    // 32-bit registers (imul/add) and reduces the result with `& wrap` (wrap =
    // N*N-1). On the shipped grids (N<=128) the intermediates never overflow, so
    // the values are identical; but at very large N the int multiply
    // `n * (wrap & rowM1)` overflows (UB in C++ / a defined mod-2^32 wrap on x86).
    // We do the index math in u32 — wrapping is defined and matches the CPU's
    // 2's-complement imul exactly, and the trailing `& wrap` then masks the index
    // back in-bounds (wrap < total), keeping the in-bounds path byte-identical.
    const u32 uwrap = (u32)wrap;
    const u32 un = (u32)n;
    for (int row = 0; row < n - 4; ++row) {
        const u32 rowM1 = (u32)(row - 1);  // v179 (== v206)
        const u32 rowP1 = (u32)(row + 1);  // v180
        for (int col = 0; col < n - 4; ++col) {
            const u32 colM1 = (u32)(col - 1); // v205
            const u32 ucol  = (u32)col;
            const u32 urow  = (u32)row;
            int idx = col + row * n;    // v17
            if (waterMaskGrid[idx] != waterType)
                continue;
            anyWater = true;            // v215 = 1
            // (Floor tile-block corner flags @0x5bab15.. are Floor-coupled and
            //  installed by the terrain owner — see WaterRegionTileFlags doc.)
            // 3x3 dilation, wrap-around on the row index via (idx & wrap):
            mask[idx] = 0xFF;                                              // v177[v17]
            mask[(urow * un + (colM1 & uwrap)) & uwrap] = 0xFF;            // 0x5babd6
            mask[(urow * un + ((ucol + 1) & uwrap)) & uwrap] = 0xFF;       // 0x5babeb
            mask[(un * (uwrap & rowM1) + (colM1 & uwrap)) & uwrap] = 0xFF; // 0x5bac13
            mask[(ucol + un * (uwrap & rowM1)) & uwrap] = 0xFF;            // 0x5bac33
            mask[(un * (uwrap & rowM1) + ((ucol + 1) & uwrap)) & uwrap] = 0xFF; // 0x5bac53
            mask[(un * (uwrap & rowP1) + (colM1 & uwrap)) & uwrap] = 0xFF; // 0x5bac7b
            mask[(un * (uwrap & rowP1) + ucol) & uwrap] = 0xFF;            // 0x5bac9a
            mask[((uwrap & (ucol + 1)) + un * (uwrap & rowP1)) & uwrap] = 0xFF; // 0x5bacba
        }
    }
    out.anyWater = anyWater;
    if (!anyWater)
        return out;                     // original sets Floor+7276=-1 and bails

    // === Pass 2 (loc_5BACD0): water heights = mask AND'd / fresh copy ========
    out.heights.assign((std::size_t)total, 0);
    u8* heights = out.heights.data();
    if (waterHeights != nullptr) {
        for (int j = 0; j < total; ++j) {
            u8 m = mask[j];
            heights[j] = (u8)(waterHeights[j] & m);    // *(height) &= *v20
            if (m == 0xFF && heights[j] == 0)          // if (*v20==-1 && !*dst)
                heights[j] = 0xFF;                     //   *dst = *v20
        }
    } else {
        // No prior height buffer: the height grid is a straight copy of the mask
        // (qmemcpy v177 -> new buffer).
        std::memcpy(heights, mask, (std::size_t)total);
    }

    // === Pass 2b (loc_5BAD4D): per-row height gradient over 0xFF runs ========
    // For each row, walk cols; a run starts at the first 0xFF cell (seed loVal
    // from the LEFT neighbour height if nonzero, else terrainHeight(idx)-2 min 1)
    // and ends at the first non-0xFF cell (seed hiVal from that cell's height if
    // nonzero, else terrainHeight(idx-1)-2 min 1), filling [lo,hi-1].
    auto seedHeight = [&](int idx) -> u8 {
        int v = (terrainHeights ? (int)terrainHeights[idx] : 2) - 2;   // -2 bias
        if (v < 1) v = 1;
        return (u8)v;
    };
    for (int row = 0; row < n; ++row) {
        bool inRun = false;             // v214
        int  runLo = 0;                 // v183 (start col)
        u8   loVal = 0;                 // v213
        for (int col = 0; col < n; ++col) {
            int idx = col + n * row;    // v37
            u8 h = heights[idx];        // *(a1[6]+v37)
            if (h == 0xFF && !inRun) {  // open a run
                runLo = col;
                inRun = true;
                if (col > 0 && heights[idx - 1] != 0)   // left neighbour nonzero
                    loVal = heights[idx - 1];
                else
                    loVal = seedHeight(idx);            // terrain seed
            } else if (h != 0xFF && inRun) {            // close a run at col-1
                inRun = false;
                u8 hiVal;
                if (h != 0)
                    hiVal = h;                          // the closing cell's height
                else
                    hiVal = seedHeight(idx - 1);
                // FillHeightGradient writes dst[lo..hi]; the original's dst base
                // is the ROW base (a1[6] + N*row), lo/hi are column indices.
                FillHeightGradient(heights + n * row, runLo, col - 1, loVal, hiVal);
            }
        }
        if (inRun) {                    // run reaches the row end
            // 0x5ba95c: v39 = terrain[*a1 + *a1*row - 1] - 2 == the row's LAST
            // cell (n*row + n - 1). (Harden fix: was off by one, n*row + n - 2.)
            u8 hiVal = seedHeight(n - 1 + n * row);
            FillHeightGradient(heights + n * row, runLo, n - 1, loVal, hiVal);
        }
    }

    // === Pass 3 (loc_5BAE61): per-cell 4-neighbour edge bitcode =============
    // code = self<<3 | up<<2 | right<<1 | diag ; switch sets/clears bit7 of the
    // edge buffer. (The original writes Floor+0x24/a1[9]; modelled here.)
    out.edge.assign((std::size_t)total, 0);
    u8* edge = out.edge.data();
    auto isWater = [&](int idx) { return waterMaskGrid[idx & wrap] == waterType; };
    for (int k = 0; k < n; ++k) {
        for (int c = 0; c < n; ++c) {
            int base = c + k * n;                       // v43 + v42
            int self = isWater(base) ? 1 : 0;           // <<3
            int up   = isWater(c + n * (wrap & (k + 1))) ? 1 : 0; // <<2
            int rcol = wrap & (c + 1);                  // v203
            int right = isWater(rcol + n * (wrap & (k + 1))) ? 1 : 0; // case bit
            int diag = isWater(rcol + k * n) ? 1 : 0;   // <<1
            int code = (self << 3) | (up << 2) | (diag << 1) | right;
            switch (code) {
            case 1: case 7: case 8: case 9: case 14:
                edge[base] |= 0x80u;                    // 0x5baf33
                break;
            case 2: case 4: case 6: case 11: case 13:
                edge[base] &= (u8)~0x80u;               // 0x5bb08f
                break;
            default:
                break;
            }
        }
    }

    // === Pass 4 (loc_5BAF56): region grid (CalcRegions) + flood fill =========
    out.regionGrid.assign((std::size_t)total, 0xFF);   // memset -1
    u8* region = out.regionGrid.data();
    // Mark interior 2x2-water cells as 0xFE (v181[v57]=-2). Condition: the cell
    // and its +1-row / +1-col / +1-row+1-col height neighbours are all nonzero.
    for (int m = 0; m < n - 1; ++m) {
        int mP1 = m + 1;
        for (int c = 0; c < n - 1; ++c) {
            int idx = c + n * m;                        // v57
            if (region[idx] == 0xFF
                && heights[idx]                          // *(a1[6]+v57)
                && heights[c + n * mP1]                  // +1 row
                && heights[idx + 1]                      // +1 col
                && heights[c + n * mP1 + 1]) {           // +1 row +1 col
                region[idx] = 0xFE;                      // -2
            }
        }
    }
    // Flood-fill each 0xFE seed with an incrementing region id (0,1,2,...).
    u8 regionCount = 0;                                  // Floor+7277
    for (int nr = 0; nr < n - 1; ++nr) {
        for (int ii = 0; ii < n - 1; ++ii) {
            if (region[ii + nr * n] == 0xFE) {
                u8 id = regionCount;                    // v168
                regionCount = (u8)(regionCount + 1);
                FloodFillMask(region, n, nr, ii, 0xFE, id);
            }
        }
    }
    out.regionCount = regionCount;

    // === Pass 5a (loc_5BB56C): the 344-byte WaterMesh array =================
    // One record per region; load the water texture once (present-coupled).
    void* tex = nullptr;
    if (loadTexture)
        tex = loadTexture("EF_WASS_06A_2T_W_AN0", 172, ctx);  // 0x5bb713 (flags 0xAC)
    out.meshTexture = tex;
    out.waterMeshes.assign((std::size_t)regionCount * 344u, 0);
    {
        u32* mrec = reinterpret_cast<u32*>(out.waterMeshes.data());
        u32 texBits = (u32)(std::uintptr_t)tex;         // handle stored as a dword
        for (int r = 0; r < regionCount; ++r) {
            u32* v102 = mrec + (std::size_t)r * 86;     // 344 bytes / 4
            v102[0] = texBits;                          // *v102 = Ptr
            v102[1] = texBits;                          // v102[1] = Ptr
            v102[2] = 0;                                // *(Ptr+108) — 0 in headless
            for (int q = 0; q < 9; ++q)                 // v102[5..13]
                v102[5 + q] = kMeshInit[q];
            v102[3] = 0;
            v102[4] = 0;
            // post-increment block (0x5bb6d6..): float[83..78] = 0, lastTime=tick.
            v102[83] = 0;                               // *(v102-3) after +=86
            v102[78] = 0;
            v102[79] = 0;
            v102[80] = 0;
            v102[81] = 0;
            v102[84] = 0;                               // lastTime (dword_62EB38 tick; 0 here)
            v102[82] = v102[83];                        // *(v102-4)=*(v102-3)
        }
    }

    // === Pass 5b (loc_5BB169): per-tile span / poly build ===================
    // 8x8 tiles; each spans `tileSpan` cells. The last tile (index 7) clamps its
    // upper bound to N-4 (v70 = *a1 - 4). Span rows/cols are clamped likewise.
    auto tileHi = [&](int tile) { return tile == 7 ? (n - 4) : tileSpan * (tile + 1); };
    std::vector<WaterStripSpan> allSpans;
    std::vector<WaterPoly>      allPolys;

    for (int tr = 0; tr < 8; ++tr) {                    // v211 tile row
        for (int tc = 0; tc < 8; ++tc) {               // v212 tile col
            const i32 tileId = tr * 8 + tc;            // restores the per-tile group
            // (The original gates on the tile-block corner flag LOBYTE(v189[..+80]);
            //  we scan every tile and let the run logic find water — equivalent,
            //  since a tile with no water emits no spans/polys.)
            int rowLo = tileSpan * tr;
            int rowHi = tileHi(tr);
            int colLo = tileSpan * tc;
            int colHi = tileHi(tc);

            // --- span scan (loc_5BB28F): per row, runs of water heights -------
            std::size_t tileSpanStart = allSpans.size();
            int cumBase = 0;                            // v191 (running point base)
            for (int rrow = rowLo; rrow <= rowHi; ++rrow) {
                bool open = false;                      // v72
                WaterStripSpan span{};
                for (int ccol = colLo; ccol <= colHi; ++ccol) {
                    int idx = (ccol & wrap) + n * (rrow & wrap);   // v74
                    bool isWaterCell = heights[idx] != 0;          // *(v74+a1[6])
                    // close-test (0x5bb8fd): close the run when not water, or a
                    // region-boundary condition. We use the height-water test as
                    // the run predicate (the boundary refinements feed FindRegion
                    // resolution, not run membership) — runs are contiguous water.
                    if (!isWaterCell) {
                        if (open) {
                            span.hi = ccol - 1;         // *(v69+12)=v73-1
                            open = false;
                            if (span.lo < span.hi) {    // *(v69+8) < *(v69+12)
                                span.marker = 0xFF;
                                span.tile = tileId;
                                allSpans.push_back(span);
                                cumBase += span.hi + 1 - span.lo;  // v191 += len
                            }
                        }
                    } else if (!open) {
                        span = WaterStripSpan{};
                        span.marker = 0xFF;             // *(v69+16)=-1
                        span.row = rrow;                // *(v69+4)=v71
                        span.lo = ccol;                 // *(v69+8)=v73
                        span.base = cumBase;            // *v69 = v191
                        open = true;
                    }
                }
                if (open) {                             // run reaches col end (0x5bb37d)
                    span.hi = colHi;                    // *(v69+12)=a1[1]*v184
                    if (span.lo < span.hi) {
                        span.tile = tileId;
                        allSpans.push_back(span);
                        cumBase += span.hi + 1 - span.lo;
                    }
                }
            }

            // --- poly build (loc_5BB50F): per cell, resolve 4 region offsets --
            // Build the FindRegionOffset span view for THIS tile's spans.
            std::vector<WaterRegionSpan> spanView;
            spanView.reserve(allSpans.size() - tileSpanStart + 1);
            for (std::size_t s = tileSpanStart; s < allSpans.size(); ++s) {
                WaterStripSpan& ss = allSpans[s];
                WaterRegionSpan rs{};
                rs.base = ss.base; rs.type = ss.row; rs.lo = ss.lo; rs.hi = ss.hi;
                rs.marker = ss.marker;
                spanView.push_back(rs);
            }
            // terminator span (the original's negative next-link / -1 sentinel).
            WaterRegionSpan term{}; term.type = -1; spanView.push_back(term);

            for (int prow = rowLo; prow < tileSpan * (tr + 1); ++prow) {
                // (loop guard a1[1]*v200 <= v210 ; v210 starts a1[1]*v211 == rowLo)
                for (int kk = colLo; kk < tileSpan * (tc + 1); ++kk) {
                    int idx = (kk & wrap) + n * (prow & wrap);     // v74 cell
                    if (region[idx] == 0xFF)
                        continue;                                  // *v202==-1 skip
                    WaterPoly poly{};
                    poly.regionId = region[idx];                   // *(v87+37)
                    poly.cell = idx;
                    poly.col = kk;
                    poly.tile = tileId;
                    // The four FindRegionOffset corner lookups. The engine passes
                    // bufferBase = the per-tile points heap pointer (v190[7], never
                    // 0), so a VALID offset (incl. point index 0) is non-zero and a
                    // no-match returns literal 0. We reproduce that distinction with
                    // a non-zero sentinel base (80): point index = off/80 - 1, and
                    // off==0 -> -1 (no span / boundary corner).
                    //   off0 = P(kk,   prow)    tri0.v0 / tri1.v0  (v87+0)
                    //   off1 = P(kk+1, prow+1)  tri0.v1 / tri1.v2  (v87+4)
                    //   off2 = P(kk,   prow+1)  tri0.v2            (v87+8)
                    //   off3 = P(kk+1, prow)    tri1.v1            (v87+44)
                    constexpr i32 kBufBase = 80;   // sentinel (stands for v190[7]!=0)
                    poly.off0 = FindRegionOffset(spanView.data(), (int)spanView.size(),
                                                 kBufBase, kk, poly.regionId, prow);
                    poly.off1 = FindRegionOffset(spanView.data(), (int)spanView.size(),
                                                 kBufBase, kk + 1, poly.regionId, prow + 1);
                    poly.off2 = FindRegionOffset(spanView.data(), (int)spanView.size(),
                                                 kBufBase, kk, poly.regionId, prow + 1);
                    poly.off3 = FindRegionOffset(spanView.data(), (int)spanView.size(),
                                                 kBufBase, kk + 1, poly.regionId, prow);
                    auto pIdx = [](i32 off) -> i32 { return off > 0 ? off / 80 - 1 : -1; };
                    poly.pTL = pIdx(poly.off0);
                    poly.pBR = pIdx(poly.off1);
                    poly.pBL = pIdx(poly.off2);
                    poly.pTR = pIdx(poly.off3);
                    allPolys.push_back(poly);
                }
            }

            // --- compaction (loc_5BBB05): drop spans whose marker stayed 0xFF -
            // (the original MemMoves out any span never stamped by a poly via
            //  FindRegionOffset). We copy back the marker stamps then compact.
            for (std::size_t s = tileSpanStart, v = 0; s < allSpans.size(); ++s, ++v)
                allSpans[s].marker = spanView[v].marker;
            std::size_t w = tileSpanStart;
            for (std::size_t s = tileSpanStart; s < allSpans.size(); ++s) {
                if (allSpans[s].marker != 0xFF)
                    allSpans[w++] = allSpans[s];
            }
            allSpans.resize(w);
        }
    }
    out.spans = std::move(allSpans);
    out.polys = std::move(allPolys);
    return out;
}

} // namespace guild::render

#include "render/shadow_ground.h"
#include "render/render_leaves7.h"   // REUSE the leaf helpers (no re-derivation)

#include <cmath>

namespace guild::render {

// Recovered constants (get_bytes; bit-exact):
//   flt_62C26C = 0.5    ProjectGroundQuad ground-Y bias  (kShadowZBias)
//   flt_62C278 = 0.5    BuildGroundShadow caster-height scale (kCasterHeightScale)
//   dbl_62C270 = 25.0   heightfield edge-discontinuity tolerance (kEdgeTolerance)
//   byte_64A351 = 0     high-detail bias selector (1.5 vs 1.0) — HeightFieldBias
//   -1082130432 == 0xBF800000 == -1.0f   homogeneous-w sentinel (+64 / +24)
static constexpr i32 kNegOneFloatBits = -1082130432;  // 0xBF800000 == -1.0f
static_assert(sizeof(float) == 4, "float must be 32-bit");

namespace {
inline float bitsToFloat(i32 b) {
    float f;
    __builtin_memcpy(&f, &b, 4);
    return f;
}
}  // namespace

// ===========================================================================
// 0x5f2a58 — VIBE_Shadow_RasterizeHeightField.
//
// Two phases, 1:1 with the decompile:
//   PHASE 1 (0x5f2b67..0x5f2ca9): emit one ground vertex per clip-rect cell into
//     vertexPoolA, lifting Y by the terrain height sample.  Bumps dword_1408A64
//     (global vertex count) and a7[2] (draw0 running count, which doubles as the
//     vertex slot stamp the draw record points at via +72).
//   PHASE 2 (0x5f2cce..0x5f2fe3): stitch the (w-1)x(h-1) cells into two-triangle
//     quads in drawPool1 (40-byte records) + vertexPoolB (UV payload), flagging
//     the +38 bit1 edge bit where adjacent terrain heights differ by > 25.0.
//
// Field map (recovered from the call site in BuildGroundShadow @0x5f335f):
//   a1 = clip[4]  (x0=+0, x1=+4, y0=+8, y1=+12)
//   a2 = dim      (== row stride of the height grid)
//   a3 = ground[] (a3[0]=xBase, a3[1]=baseY, a3[2]=zBase)
//   a4 = heightBase (added to row*stride+col when indexing the height bytes)
//   a5 = xform[]  (a5[0]=xStep, a5[1]=yScale, a5[2]=zStep)
//   a6 = uv[4]    (uOff,uStep,vOff,vStep) -> the vertexPoolB payload coords
//   a8 = colorKey (material id stamped into draw +5 / +20)
//   a9 = colorKey2 (stamped into draw0 +68 in phase 1)
// ===========================================================================
char RasterizeHeightField(const ShadowClipRect& clip,
                          const GroundShadowCaster& caster,
                          const float uv[4], GroundShadowPools& pools) {
    const i32 x0 = clip.x0, x1 = clip.x1, y0 = clip.y0, y1 = clip.y1;
    const i32 a2 = caster.dim;                 // row stride
    const i32 a4 = caster.heightBase;          // base index
    const i32 a8 = caster.colorKey;            // a8
    const i32 a9 = caster.colorKey;            // a9 (== v52 colour key; same source)
    const float* a3 = caster.ground;           // ground[0..2]
    const float* a5 = caster.xform0;           // xform[0..2]
    const u8* heights = caster.heights;

    // 0x5f2a81 / 0x5f2a90: v37 = x1-x0, v36 = y1-y0  (cells minus one).
    const i32 v37 = x1 - x0;
    const i32 v36 = y1 - y0;

    // 0x5f2b02: bias = byte_64A351 ? 1.5 : 1.0   (REUSE HeightFieldBias).
    const float v32 = HeightFieldBias(/*highDetail*/ false);

    // 0x5f2abc / 0x5f2ad8: phase 2 references the phase-1 records relative to the
    // draw0/vertexA base captured at ENTRY (v55 = 80*a7[2]+*a7, v56 base =
    // 24*dword_1408A64+dword_1408A50), NOT from absolute 0. Capture the entry
    // vertexA count so reused pools index the correct (1:1) slots.
    const i32 entryBaseA = pools.vertexCountA;

    // 0x5f2b44/4a/5c: row/column world bases (+ the X/Z start).  v35 = baseY+bias
    // is folded into RasterizeHeightVertexY (the leaf adds bias to baseY itself).
    float v34 = static_cast<float>((double)x0 * a5[0] + a3[0]);   // running X start
    float v39 = static_cast<float>((double)y0 * a5[2] + a3[2]);   // running Z

    // ---- PHASE 1: per-cell ground vertices into vertexPoolA -----------------
    // The draw0 running count (a7[2]) is the vertex stamp; vertexPoolA is bumped
    // by dword_1408A64.  We mirror both.
    i32 v41 = y0;                                            // current row
    if (v41 <= y1) {
        i32 v38 = a2 * v41;                                 // row*stride
        do {
            float v44 = v34;                                // running X
            i32 v53 = x0;                                   // current col
            if (x0 <= x1) {
                i32 hIdx = v38 + a4 + x0;                   // height index
                do {
                    GroundShadowVertex& vtx =
                        pools.vertexA[pools.vertexCountA];
                    const u8 h = heights ? heights[hIdx] : 0;
                    // 0x5f2bfb: vy = ((double)h + bias)*a5[1] + (baseY+bias)
                    //  == RasterizeHeightVertexY(h, bias, a5[1], a3[1]).
                    vtx.pos[1] = RasterizeHeightVertexY(h, v32, a5[1], a3[1]);
                    vtx.pos[0] = v44;                       // X
                    vtx.pos[2] = v39;                       // Z
                    // 0x5f2c15: draw0[count].payload+68 = a9 (colour key)
                    GroundShadowDraw& d = pools.draw0[pools.drawCount0];
                    d.color0 = a9;                          // +68
                    d.payload = pools.vertexCountA;         // +72 -> vertex slot
                    ++pools.vertexCountA;                   // dword_1408A64++
                    ++pools.drawCount0;                     // a7[2]++
                    ++hIdx;
                    v44 = static_cast<float>(v44 + (double)a5[0]);   // X += xStep
                    ++v53;
                } while (v53 <= x1);
            }
            v38 += a2;                                       // next row stride
            v39 = static_cast<float>(v39 + (double)a5[2]);   // Z += zStep
            ++v41;
        } while (v41 <= y1);
    }

    // ---- PHASE 2: stitch cells into two-tri quads in drawPool1 --------------
    // 0x5f2cb4: v33 = v37 + 1  (vertices per row).
    const i32 v33 = v37 + 1;
    float v57 = uv[2];                                       // a6[2] == vOff
    if (v36 > 0) {
        i32 v40 = 0;
        do {
            float v58 = uv[0];                              // a6[0] == uOff
            if (v37 > 0) {
                // Row vertex-slot bases (80-byte draw0 records were laid out
                // contiguously in phase 1; here we index them as flat slots).
                // Relative to the phase-1 ENTRY base (v55), so reused pools point
                // at the records this call's phase 1 just emitted.
                i32 rowBase = entryBaseA + v40 * v33;        // current row first vtx
                i32 nextRow = entryBaseA + (v40 + 1) * v33;  // next row first vtx
                i32 col = 0;
                do {
                    // 0x5f2d66..: each quad is two triangles.  The draw1 record
                    // (40 bytes) stores 3 vertex slots; we emit two per cell.
                    const i32 vA = rowBase + col;           // top-left
                    const i32 vB = rowBase + col + 1;       // top-right
                    const i32 vC = nextRow + col;           // bottom-left
                    const i32 vD = nextRow + col + 1;       // bottom-right

                    const float hA = pools.vertexA[vA].pos[1];
                    const float hB = pools.vertexA[vB].pos[1];
                    const float hC = pools.vertexA[vC].pos[1];
                    const float hD = pools.vertexA[vD].pos[1];

                    // --- triangle 1 (vA, vB, vC) ---
                    {
                        GroundShadowDraw& d = pools.draw1[pools.drawCount1];
                        d.v[0] = vA; d.v[1] = vB; d.v[2] = vC;
                        d.matA = a8;                        // +20 / a8
                        d.biasW = bitsToFloat(kNegOneFloatBits);   // +24 (-1.0)
                        // 0x5f2dd5: edge flag if |hA-hB|>25 || |hA-hC|>25.
                        d.edgeFlag = HeightEdgeDiscontinuous(hA, hB, hC) ? 2 : 0;
                        // 0x5f2ded..0x5f2e23: vertexPoolB payload for this tri —
                        // the record is SIX flat floats (v11[0..5]), not pos+pl:
                        //   [0]=u  [1]=v  [2]=uStep+u  [3]=v  [4]=u  [5]=vStep+v
                        GroundShadowVertex& p = pools.vertexB[pools.vertexCountB];
                        p.pos[0] = v58;                      // v11[0] = u
                        p.pos[1] = v57;                      // v11[1] = v
                        p.pos[2] = uv[1] + v58;              // v11[2] = a6[1]+u
                        p.pl[0]  = v57;                      // v11[3] = v
                        p.pl[1]  = v58;                      // v11[4] = u
                        p.pl[2]  = uv[3] + v57;              // v11[5] = a6[3]+v
                        d.v[0] = vA;                         // keep tri indices
                        ++pools.vertexCountB;                // dword_1408A68++
                        ++pools.drawCount1;                  // a7[3]++
                    }
                    // --- triangle 2 (vB, vD, vC) ---
                    {
                        GroundShadowDraw& d = pools.draw1[pools.drawCount1];
                        d.v[0] = vB; d.v[1] = vD; d.v[2] = vC;
                        d.matA = a8;
                        d.biasW = bitsToFloat(kNegOneFloatBits);
                        // 0x5f2ea8: second edge flag |hD-hB|>25 || |hD-hC|>25.
                        d.edgeFlag = HeightEdgeDiscontinuous(hD, hB, hC) ? 2 : 0;
                        // 0x5f2ee4..0x5f2f26: second record's six flat floats —
                        //   [0]=uStep+u [1]=v [2]=uStep+u [3]=vStep+v [4]=u [5]=vStep+v
                        GroundShadowVertex& p = pools.vertexB[pools.vertexCountB];
                        p.pos[0] = uv[1] + v58;              // v11[0] = a6[1]+u
                        p.pos[1] = v57;                      // v11[1] = v
                        p.pos[2] = uv[1] + v58;              // v11[2] = a6[1]+u
                        p.pl[0]  = uv[3] + v57;              // v11[3] = a6[3]+v
                        p.pl[1]  = v58;                      // v11[4] = u
                        p.pl[2]  = uv[3] + v57;              // v11[5] = a6[3]+v
                        ++pools.vertexCountB;                // dword_1408A68++
                        ++pools.drawCount1;                  // a7[3]++
                    }

                    v58 = static_cast<float>(v58 + (double)uv[1]);  // u += a6[1]
                    ++col;
                } while (col < v37);
            }
            v57 = static_cast<float>(v57 + (double)uv[3]);   // v += a6[3]
            ++v40;
        } while (v40 < v36);
    }
    return 1;   // 0x5f2feb: the original always returns 1 once it reaches here.
}

// ===========================================================================
// 0x5f216c — VIBE_Shadow_ProjectGroundQuad.
//
// The projected-terrain-tile path: when the live terrain scene record
// (dword_64A028, BuildGroundShadow's `v48`) is present, the shadow is rasterized
// over the CACHED PROJECTED TERRAIN GRID instead of the raw heightfield.  This is
// the LIVE in-game path; RasterizeHeightField is the fallback (null scene).
//
// 1:1 with the decompile (0x5f216c) + disasm.  Arg map (from the call site
// @0x5f32e0  `ProjectGroundQuad(v48, v36, v5, v37, v47, v52)`):
//   a1 = tile  (terrain scene record / GroundShadowTile portable view)
//   a2 = clip  (int[4]: x0=minX, x1=maxX, y0=minY, y1=maxY) — RAW grid coords
//   a3 = pools (the draw0/draw1 pool record: base ptrs + running counts)
//   a4 = uv    (a4[0]=uBase, a4[1]=uStep, a4[2]=vBase, a4[3]=vStep)
//   a5 = matId (v47 colour/material key, draw1 +60/+20)
//   a6 = colorKey2 (v52, draw0 +68)
//
// The body divides the clip rect by the cell stride (a1d[1]) to get TILE coords,
// then for each overlapping tile cell whose step byte is non-zero and which is
// inside the 8x8 grid window, emits the per-vertex ground samples into vertexA
// (phase 1) and stitches them into two-triangle quads in the draw pool (phase 2),
// flipping the winding by the cell's enable bit (the `*v27 >= 0` test).
// ===========================================================================
char ProjectGroundQuad(const GroundShadowTile* tile,
                       const ShadowClipRect& clip,
                       const GroundShadowCaster& caster,
                       const float uv[4], GroundShadowPools& pools) {
    if (!tile)
        return 0;   // 0x5f3286 guard: no scene -> caller takes RasterizeHeightField.

    char v76 = 0;  // 0x5f218a — emitted flag (return value)

    // 0x5f219b.. — pull the projection transform floats out of the tile record.
    const float v32 = tile->xBase;     // a1[36]
    const float v36 = tile->zBase;     // a1[38]
    const float v43 = tile->xStep;     // a1[40]
    const float v65 = tile->yScale;    // a1[49]
    const float v42 = tile->zStep;     // a1[46]
    const i32   v64h = 0;              // a1d[4] height base — folded into tile->heights
    const i32   v53 = tile->cellStride0;   // a1d[0]  height-byte row stride
    const i32   v6  = tile->cellStride;    // a1d[1]  tile-cell divisor (>0)
    (void)v64h;

    // 0x5f221e.. — clip rect -> TILE coords (signed divide by v6).
    const i32 v33 = clip.y1 / v6;      // a2[3]/v6  max tile row
    const i32 v44 = clip.x1 / v6;      // a2[1]/v6  max tile col
    const i32 v35 = clip.x0 / v6;      // a2[0]/v6  min tile col
    const float v40init = uv[2];       // a4[2]
    i32 v48 = clip.y0;                 // a2[2]  raw Y (advanced per tile row)
    i32 v45 = clip.y0 / v6;            // a2[2]/v6  min tile row (outer loop var)

    if (v33 < v45)
        return v76;                    // 0x5f2267 outer guard fail

    const float v37 = tile->baseY + 0.5f;     // 0x5f2296  baseY + flt_62C26C
    float v40 = v40init;

    // Outer ROW loop over tile rows (v45 .. v33).
    do {
        // 0x5f22a6: row-end raw Y (clamped at last row to clip.y1).
        const i32 v7 = (v45 == v33) ? clip.y1 : v6 * (v45 + 1);
        float v56 = uv[0];             // a4[0]  running U base
        i32   v57 = clip.x0;           // a2[0]  raw X start (advanced per tile col)
        const i32 v54 = v7;
        i32   v49 = v35;               // current tile col

        if (v35 <= v44) {
            const i32 v39 = v7 - v48;  // raw Y span this tile row
            // Inner COL loop over tile cols (v49 .. v44).
            do {
                // 0x5f2309..0x5f2346 — 8x8 grid window bounds + cell enable byte.
                const bool inGrid = (static_cast<u32>(v45) < 8u) &&
                                    (static_cast<u32>(v49) < 8u) &&
                                    v45 >= 0 && v49 >= 0 &&
                                    v45 < tile->gridRows && v49 < tile->gridCols;
                const GroundShadowTile::Cell* cell =
                    inGrid ? &tile->grid[v45 * tile->gridCols + v49] : nullptr;
                if (cell && cell->stepByte != 0) {
                    const i32 v30 = cell->stepByte;        // height step (>0)
                    const i32 v55 = v53 / v30;             // 0x5f2374
                    const i32 v59 = cell->vertBase;        // a1[(v30>>1)+9]
                    const double v8 = static_cast<double>(v30);
                    v76 = 1;                               // 0x5f23c1
                    const float v26 = static_cast<float>(uv[1] * v8);   // a4[1]*step
                    const float v68 = static_cast<float>(v8 * uv[3]);   // step*a4[3]
                    // 0x5f23ea: col-end raw X (clamped at last col to clip.x1).
                    const i32 v9 = (v49 == v44) ? clip.x1 : v6 * (v49 + 1);

                    // 0x5f2446/0x5f2451 — vertexA / vertexB cursors.
                    // (vertexA bumps by A64; vertexB by A68.)

                    // ---- PHASE 1: per-vertex ground samples into vertexA -------
                    const float v66 = static_cast<float>(v43 * v8);    // X step*v30
                    const float v52f = static_cast<float>(v42 * v8);   // Z step*v30
                    i32 v61 = v48;                                      // raw Y
                    float i_ = static_cast<float>((double)v48 * v42 + v36);  // Z
                    for (; v61 <= v54; v61 += v30) {
                        float v70 = static_cast<float>((double)v57 * v43 + v32); // X
                        for (i32 j = v57; j <= v9; j += v30) {
                            GroundShadowVertex& vtx =
                                pools.vertexA[pools.vertexCountA];
                            // 0x5f2547: vy = heights[base + j + v53*v61]*yScale.
                            const i32 hIdx = j + v53 * v61;
                            const u8 h = tile->heights ? tile->heights[hIdx] : 0;
                            const float vy =
                                static_cast<float>((double)h * v65);
                            vtx.pos[0] = v70;                 // *v11 = X
                            vtx.pos[2] = i_;                  // v11[2] = Z
                            vtx.pos[1] = static_cast<float>((double)vy + v37); // Y
                            // 0x5f2568: draw0[+68] = a6 (colorKey2); +72 = vert slot.
                            pools.draw0[pools.drawCount0].color0 = caster.colorKey; // +68 a6
                            pools.draw0[pools.drawCount0].payload =
                                pools.vertexCountA;           // +72 -> vertex slot
                            ++pools.vertexCountA;             // dword_1408A64++
                            ++pools.drawCount0;               // a3[2]++
                            v70 = static_cast<float>((double)v70 + v66);   // X step
                        }
                        i_ = static_cast<float>((double)i_ + v52f);        // Z step
                    }

                    // ---- PHASE 2: stitch grid cells into two-tri quads --------
                    const i32 v25 = (v9 - v57) / v30;         // cols-1 (0x5f261b)
                    const i32 v29 = v25 + 1;                  // verts per grid row
                    float v74 = v40;                          // running V row
                    // 0x5f265f: vert-grid base index for this tile.
                    i32 v60 = (v48 / v30) * v55 + v57 / v30 + v59;
                    const i32 gridRowCount = v39 / v30;       // 0x5f266f
                    for (i32 v63 = 0; v63 < gridRowCount; ++v63) {
                        float v75 = v56;                      // running U
                        if (v25 > 0) {
                            const float v67 = static_cast<float>((double)v74 + v68);
                            for (i32 v16 = 0; v16 < v25; ++v16) {
                                // The two-triangle command (80-byte logical record;
                                // 0x5f2745: winding-flip branch on the enable bit.
                                // v18/v28/v69 are draw0-relative vertex slots.
                                const bool windingA =
                                    cell && ((cell->enable & 0x80u) == 0);
                                // (`*v27 >= 0` == top bit clear; the original walks
                                //  a per-grid-cell enable byte — we use the cell's.)
                                GroundShadowDraw& d = pools.draw1[pools.drawCount1];
                                const i32 v18 = v16 + v29 * (v63 + 1);
                                const i32 vBL = v18;                  // bottom-left
                                const i32 vBR = v18 + 1;              // bottom-right
                                const i32 vTL = v16 + v29 * v63;      // top-left
                                if (windingA) {
                                    d.v[0] = vTL;
                                    d.v[1] = vBR;
                                    d.v[2] = vBL;
                                } else {
                                    d.v[0] = vTL;
                                    d.v[1] = vBL;
                                    d.v[2] = vBR;
                                }
                                d.matA = caster.colorKey;            // +20 a5
                                d.color0 = caster.colorKey;          // +60 a5
                                d.biasW = bitsToFloat(kNegOneFloatBits); // +64/+24 -1.0
                                d.edgeFlag = 0;                      // +38 &= ~2
                                // vertexB UV payload (six flat floats; winding-aware).
                                GroundShadowVertex& p =
                                    pools.vertexB[pools.vertexCountB];
                                if (windingA) {
                                    p.pos[0] = v75;                  // u
                                    p.pos[1] = v74;                  // v
                                    p.pos[2] = static_cast<float>(v75 + v26); // u+uStep
                                    p.pl[0]  = v74;                  // v
                                    p.pl[1]  = v75;                  // u
                                    p.pl[2]  = v67;                  // v+vStep
                                } else {
                                    p.pos[0] = static_cast<float>(v75 + v26); // u+uStep
                                    p.pos[1] = v74;                  // v
                                    p.pos[2] = static_cast<float>(v75 + v26);
                                    p.pl[0]  = v67;                  // v+vStep
                                    p.pl[1]  = v75;                  // u
                                    p.pl[2]  = v67;                  // v+vStep
                                }
                                pools.vertexCountB += 2;             // dword_1408A68 += 2
                                pools.drawCount1   += 2;             // a3[3] += 2
                                v75 = static_cast<float>(v75 + v26); // u += uStep
                            }
                        }
                        v74 = static_cast<float>((double)v74 + v68); // v += vStep
                        v60 += v55;                                  // next vert row
                    }
                }
                // 0x5f293a.. — advance to next tile col.
                v56 = static_cast<float>(
                    (double)(int)((v49 + 1) * v6 - v57) * uv[1] + v56);
                ++v49;
                v57 = ((v6 + v57) / v6) * v6;
            } while (v49 <= v44);
        }
        // 0x5f29b0.. — advance to next tile row.
        v40 = static_cast<float>((double)(int)((v45 + 1) * v6 - v48) * uv[3] + v40);
        ++v45;
        v48 = ((v6 + v48) / v6) * v6;
    } while (v45 <= v33);

    return v76;   // 0x5f29d3
}

// ===========================================================================
// 0x5f3048 — VIBE_Shadow_BuildGroundShadow.  THE CLEAN ENTRY / dispatcher.
//
// Faithful translation of the control flow:
//   * caster-height colour key derivation (0x5f306f..0x5f30da, via ConvertX);
//   * the byte_1408A6D + dim gates (0x5f30de..0x5f30fe);
//   * clip-rect + interp derivation (0x5f3104..0x5f31e2, REUSE ComputeShadowClipRect);
//   * capacity gates (0x5f31fa, 0x5f323f) against dword_64A7EC;
//   * dispatch: tile -> ProjectGroundQuad (0x5f32e0) else RasterizeHeightField
//     (0x5f335f);
//   * the flat-quad fallback when byte_1408A6D is clear (0x5f337d..0x5f34b0).
// Returns 1 when a ground shadow was emitted.
// ===========================================================================
char BuildGroundShadow(const GroundShadowCaster& caster, bool hfEnabled,
                       const GroundShadowTile* tile, GroundShadowPools& pools) {
    char result = 0;   // v55

    // 0x5f306f..0x5f30da — caster colour key from the world pos + height*0.5.
    // (The packed key is threaded into draw records as a8/a9; the colorKey field
    // already carries it, so the ConvertX rounding only affected the in-binary
    // packing.  We keep colorKey as the resolved key — see header.)
    // (kCasterHeightScale == flt_62C278 == 0.5, applied to casterHeight.)
    (void)caster.casterHeight;  // used only for the packed-byte key in the orig.

    // 0x5f30de..0x5f30fe — the heightfield-enable + data gates.
    if (hfEnabled && caster.hasData && caster.dim && !caster.directional) {
        // 0x5f3104.. — derive clip rect + interp params (REUSE the leaf).
        ShadowClipRect clip;
        if (!ComputeShadowClipRect(caster.dim, caster.minX, caster.maxX,
                                   caster.minY, caster.maxY, &clip))
            return 0;   // any early-out rejection -> v55 == 0.

        // 0x5f31e0.. — footprint cell counts + capacity gate.
        const i32 v17 = clip.x1 - clip.x0;     // width-1
        const i32 v18 = clip.y1 - clip.y0;     // height-1
        if (pools.capacity) {
            // 0x5f31fa: 2*cap <= 2*(v18)*(v17) + drawBaseCount -> reject.
            const u32 lhs = static_cast<u32>(2 * v18 * v17 + pools.drawBaseCount);
            if (2u * pools.capacity <= lhs)
                return result;
            // 0x5f3211/1c: total vertex estimate vs 25*(cap>>4).
            const i32 v46 = (v17 + 1) * (v18 + 1);
            const u32 v38 = static_cast<u32>(pools.drawCount0 + v46);
            const u32 v19 = 25u * (pools.capacity >> 4);
            if (v19 <= v38)
                return result;
        }

        // 0x5f3249.. — pack the interp params (a6/uv) + clip (a1) for the callee.
        const float uv[4] = {clip.uOff, clip.uStep, clip.vOff, clip.vStep};

        // 0x5f3286 — dispatch: projected-terrain tile, else raw heightfield.
        if (tile) {
            // 0x5f3295/0x5f32c3 — tile over-budget guard.  v53 = v46 + drawCount0;
            // estimate = v18*(v17/stride+1) + v53 + v17*(v18/stride+1); if it stays
            // under v19 (== 25*(cap>>4)) emit the projected tiles, else reject.
            // Only enforced when a capacity budget is set (cap != 0).
            if (pools.capacity) {
                const i32 stride = tile->cellStride ? tile->cellStride : 1;
                const i32 v46e = (v17 + 1) * (v18 + 1);   // 0x5f3211
                const i32 v53e = v46e + pools.drawCount0;
                const i32 v19 = static_cast<i32>(25u * (pools.capacity >> 4));
                const i32 est = v18 * (v17 / stride + 1) + v53e +
                                v17 * (v18 / stride + 1);
                if (est >= v19)
                    return result;          // 0x5f32e0 over-budget reject (v55==0)
            }
            return ProjectGroundQuad(tile, clip, caster, uv, pools);
        }
        result = RasterizeHeightField(clip, caster, uv, pools);
        return result;
    }

    // 0x5f337d.. — flat-quad fallback: emit ONE shadow quad (two tris) from the
    // four corner floats (caster.quad) into draw0 + vertexPoolA.  This is the
    // non-heightfield drop-shadow quad.
    {
        if (pools.capacity &&
            2u * pools.capacity - 2u <= static_cast<u32>(pools.drawBaseCount))
            return result;                              // 0x5f3385 capacity gate

        // The original (0x5f341b..0x5f3486) writes a fixed two-triangle quad (4
        // verts) using the corner floats and the two static descriptor blocks
        // (dword_1408A20/A38).  quad = { Xa(+72), Xb(+76), Ya(+80), Yb(+84),
        // mid(+88) }.  Each vertex's component triple is (Xext, mid, Yext) — i.e.
        //   v0 = (+72,+88,+84)  v1 = (+76,+88,+84)  v2 = (+72,+88,+80)
        //   v3 = (+76,+88,+80)  — verbatim store order.
        const i32 base = pools.vertexCountA;
        const float Xa = caster.quad[0], Xb = caster.quad[1];
        const float Ya = caster.quad[2], Yb = caster.quad[3];
        const float mid = caster.quad[4];
        auto put = [&](float xext, float yext) {
            GroundShadowVertex& v = pools.vertexA[pools.vertexCountA++];
            v.pos[0] = xext; v.pos[1] = mid; v.pos[2] = yext;
        };
        put(Xa, Yb);   // v0  (+72,+88,+84)
        put(Xb, Yb);   // v1  (+76,+88,+84)
        put(Xa, Ya);   // v2  (+72,+88,+80)
        put(Xb, Ya);   // v3  (+76,+88,+80)

        // The original (0x5f339d..0x5f3486) emits the two triangle COMMANDS into
        // drawPool1 (v24 = 40*drawBaseCount + a7[1], two 40-byte records) and FOUR
        // per-vertex records into drawPool0 (v28 = 80*drawCount0 + a7[0]), each
        // stamping the caster colour key (v47) into +16/+17.  We model the two
        // triangle commands (the observable vertex-index + colour state) in draw1;
        // the per-vertex draw0 records are an engine pointer-link detail, but their
        // COUNT is observable — the binary bumps drawCount0 by FOUR.
        // The two tri commands index drawPool1 by drawBaseCount (v24 = 40*v5[3] +
        // a7[1]) — NOT by drawCount1 (which is how RasterizeHeightField indexes it).
        const i32 d1base = pools.drawBaseCount;
        GroundShadowDraw& q0 = pools.draw1[d1base + 0];   // triangle 1 (v24[0..])
        q0.v[0] = base + 0; q0.v[1] = base + 1; q0.v[2] = base + 2;
        q0.color0 = caster.colorKey; q0.color1 = caster.colorKey;
        GroundShadowDraw& q1 = pools.draw1[d1base + 1];   // triangle 2 (v24[10..])
        q1.v[0] = base + 1; q1.v[1] = base + 3; q1.v[2] = base + 2;
        q1.color0 = caster.colorKey; q1.color1 = caster.colorKey;

        pools.drawCount0 += 4;        // v5[2] += 4   (0x5f34ad — four draw0 records)
        pools.drawBaseCount += 2;     // v5[3] += 2   (0x5f340d)
        result = 1;                   // 0x5f34a0
    }
    return result;
}

} // namespace guild::render

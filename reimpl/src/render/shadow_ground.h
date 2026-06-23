#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — GROUND shadow (the soft shadow quad that conforms to the
// terrain heightfield under a caster).  Faithful 1:1 reconstruction of the three
// vertex-pool-emitting bodies the gilde.exe shadow cluster uses *after* the mesh
// silhouette has been splatted into the caster's shadow surface:
//
//   0x5f3048  VIBE_Shadow_BuildGroundShadow    (clip-rect + dispatch + flat-quad)
//   0x5f216c  VIBE_Shadow_ProjectGroundQuad    (per-cell terrain-tile projection)
//   0x5f2a58  VIBE_Shadow_RasterizeHeightField (rasterize shadow over the heightfield)
//
// WHY A SEPARATE MODULE: render_leaves7.{h,cpp} reconstructed only the leaf
// ARITHMETIC fragments of these three (ComputeShadowClipRect, ProjectGroundVertex,
// RasterizeHeightVertexY, HeightFieldBias, HeightEdgeDiscontinuous) and DEFERRED
// the full pool-emitting bodies ("the heavier projection / rasterisation ... that
// emits into the global vertex pool").  This module supplies those bodies, the
// global vertex/draw pools they fill, and the clean caster-shadow entry.  The leaf
// helpers are REUSED (extern via render_leaves7.h), never re-derived.
//
// ---------------------------------------------------------------------------
// THE GLOBAL POOLS (gilde.exe runtime-initialised; here owned by the caller)
// ---------------------------------------------------------------------------
// The engine streams two interleaved vertex pools and two draw-command pools and
// bumps four running counters as it emits:
//
//   dword_1408A50  vertexPoolA  base, stride 24 bytes == 6 floats   [count A64]
//   dword_1408A58  vertexPoolB  base, stride 24 bytes == 6 floats   [count A68]
//   a7[0] (*a7)    drawPool0    base, stride 80 bytes               [count a7[2]]
//   a7[1]          drawPool1    base, stride 40 bytes               [count a7[3]]
//   a7[2]          running index into drawPool0 (80-byte records)
//   a7[3]          running index into drawPool1 (40-byte records)
//
// `a7` is the per-object "shadow batch" record at  obj+492 + 244  (the original's
// v5 / a3 / a7).  dword_1408A64 / dword_1408A68 are the global vertex counters.
// A draw-command record (80 bytes) carries 3 vertex-pointer slots + 2 colour-key
// dwords + an edge/flags byte; the vertex records are 6 floats (pos.xyz at +0/+4/+8
// then a payload triple at +12/+16/+20 the rasterizer reads back).
//
// We mirror the byte layout exactly so the arithmetic, strides and counter bumps
// are bit-identical to the original; only the storage is caller-owned (rule 3/4:
// no DDraw surface, the splat target is a plain buffer).
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// One emitted ground-shadow vertex — 6 floats / 24 bytes (vertexPoolA/B element).
//   pos[0..2]  world X / Y / Z of the projected ground sample
//   pl[0..2]   the payload triple (UV / colour-key) the draw record references
// ---------------------------------------------------------------------------
struct GroundShadowVertex {
    float pos[3] = {0, 0, 0};   // +0  +4  +8
    float pl[3]  = {0, 0, 0};   // +12 +16 +20
};

// One emitted draw command — 80-byte record (drawPool0 element).  We keep the
// fields the three bodies actually write; the remaining padding mirrors stride.
//   v[0..2]    pointers (indices) to the 3 ground-shadow vertices of the tri
//   color0/1   the caster colour key (a6 / a8 / a9 — the packed RGBA dword)
//   payload    +72 slot: a pointer to the source vertex (heightfield edge read)
//   edgeFlag   +38 bit1 (2): set when the tri spans a height discontinuity
//   biasW      +64/+24 word: the -1082130432 (== -1.0f) homogeneous-w sentinel
struct GroundShadowDraw {
    i32   v[3]    = {0, 0, 0};
    i32   color0  = 0;
    i32   color1  = 0;
    i32   payload = 0;       // +72
    u8    edgeFlag = 0;      // +38 bit1
    float biasW   = 0.0f;    // +64 / +24 (-1.0f sentinel)
    i32   matA    = 0;       // +60 / +20  (a5 / a8 material id)
};

// The two vertex pools + two draw pools + the four running counters, packed into
// the engine's "shadow batch" record (obj+492+244 / a7 / v5 / a3).
struct GroundShadowPools {
    GroundShadowVertex* vertexA = nullptr;  // dword_1408A50
    GroundShadowVertex* vertexB = nullptr;  // dword_1408A58
    GroundShadowDraw*   draw0   = nullptr;  // a7[0]
    GroundShadowDraw*   draw1   = nullptr;  // a7[1]
    i32 vertexCountA = 0;                    // dword_1408A64
    i32 vertexCountB = 0;                    // dword_1408A68
    i32 drawCount0   = 0;                    // a7[2]
    i32 drawCount1   = 0;                    // a7[3]

    // Capacity guard mirror of dword_64A7EC (2*cap-2 vertex slots, etc).  When 0
    // the caps are treated as unbounded (tests pass big buffers).
    u32 capacity = 0;                        // dword_64A7EC
    i32 drawBaseCount = 0;                    // a7-record running base (v5[3])
};

// ---------------------------------------------------------------------------
// The shadow-map / caster record passed to BuildGroundShadow (the original's a1).
// Field offsets named per the decompile (see .cpp for the addresses).
// ---------------------------------------------------------------------------
struct GroundShadowCaster {
    i32   colorKey = 0;     // *a1 (+0)   v47 — packed caster colour, threaded down
    // light/transform floats (a1[1] -> +0x5C..0x64 world pos, +0x94 caster height)
    float worldPos[3] = {0, 0, 0};   // a1[1]+0x5C/0x60/0x64  (X, ?, ?)
    float casterHeight = 0.0f;       // a1[1]+0x94 (* 0.5 == kCasterHeightScale)

    i32   dim = 0;          // +0x10 (16)  shadow-map dimension (exclusive bound)
    i32   hasData = 1;      // +0x0C (12)  non-zero gate
    u8    directional = 0;  // +0x7C (124) directional-light flag (skips heightfield)

    i32   minX = 0, maxX = 0, minY = 0, maxY = 0;  // +0x5C/+60/+64/+68 (92/96/100/104)

    // Heightfield rasterize inputs (the RasterizeHeightField path):
    const u8* heights = nullptr;     // a4 base + offset  (per-cell u8 height byte)
    i32   heightStride = 0;          // a2  row stride
    i32   heightBase = 0;            // a4  base index
    float xform0[3] = {0, 0, 0};     // a5  (a3[+0x28]/+40): xStep, yStep, zStep
    float ground[3] = {0, 0, 0};     // a3  (a1+0x38/+56): xBase, baseY, zBase

    // The flat-quad fallback (no clip rect): four corner floats + colour.
    float quad[5] = {0, 0, 0, 0, 0}; // +0x48/+4C/+50/+54/+58 (72/76/80/84/88)
};

// ===========================================================================
// 0x5f2a58 — VIBE_Shadow_RasterizeHeightField.  Rasterize the caster's ground
// shadow over the terrain heightfield: for every cell of the clamped clip rect
// it emits one ground vertex (X/Z from the interpolation params, Y lifted by the
// terrain height sample), then stitches the cells into two-triangle quads, marking
// the +38 bit1 edge flag where adjacent heights differ by > 25.0 (dbl_62C270).
// Returns 1 (the original always returns 1 once it reaches here).
//   clip  : the clamped rect + interp params (from ComputeShadowClipRect)
//   uv    : the 4 interp params (a6: uOff/uStep then vOff/vStep) — drives pl[].
// ===========================================================================
char RasterizeHeightField(const struct ShadowClipRect& clip,
                          const GroundShadowCaster& caster,
                          const float uv[4], GroundShadowPools& pools);

// ===========================================================================
// 0x5f216c — VIBE_Shadow_ProjectGroundQuad.  The terrain-tile (cached projected
// terrain) path: walks the tile grid that overlaps the clip rect, sampling the
// already-projected terrain vertices and re-emitting them tinted by the shadow.
//
// The original's `a1` is the LIVE TERRAIN SCENE RECORD (dword_64A028) — the same
// global the terrain/collision/snow/weather subsystems use.  It is passed by
// BuildGroundShadow as `v48 = dword_64A028` (see the caller @0x5f428d:
// `BuildGroundShadow((int*)v5, (float*)dword_64A028)`).  This is the LIVE shadow
// path in-game; RasterizeHeightField is the fallback when the scene ptr is null.
//
// GroundShadowTile is the PORTABLE VIEW of exactly the fields ProjectGroundQuad
// reads from that record — recovered byte-for-byte from the decompile/disasm:
//   *(int*)(a1+0)   cellStride0  — height-byte row stride (a1d[0], v53)
//   *(int*)(a1+4)   cellStride   — tile-cell divisor    (a1d[1], v6)
//   *(int*)(a1+16)  heights      — base ptr of the per-cell u8 height bytes (v64)
//   a1[36]=xBase a1[37]=baseY a1[38]=zBase a1[40]=xStep a1[46]=zStep a1[49]=yScale
//   tile grid @ a1+0xE0: per outer-row 800 bytes (×v45), per outer-col 100 bytes
//     (×v49); each cell's height-step byte at +0x5E (== a1+318+...), and the
//     enable/winding byte read at the same address (BYTE2 at +0x13E from a1).
//   per-cell projected-vertex base index: a1[(stepByte>>1)+9]  (v59).
// The caller owns the storage (rule 3/4: no DDraw surface) and supplies these as
// flat arrays so the arithmetic, strides and counter bumps stay bit-identical.
// Returns 1 if anything was emitted, else 0.
// ===========================================================================
struct GroundShadowTile {
    // Scalar header fields (a1d[0], a1d[1]).
    i32 cellStride0 = 0;    // *(int*)(a1+0)  = v53  height-byte row stride
    i32 cellStride  = 1;    // *(int*)(a1+4)  = v6   tile-cell divisor (>0)
    const u8* heights = nullptr;  // *(int*)(a1+16) = v64  per-cell height byte base

    // Projection transform floats (a1[36..49]).
    float xBase = 0;        // a1[36]
    float baseY = 0;        // a1[37]  (lifted by +0.5 == flt_62C26C)
    float zBase = 0;        // a1[38]
    float xStep = 0;        // a1[40]
    float zStep = 0;        // a1[46]
    float yScale = 0;       // a1[49]

    // The per-cell tile grid (a1+0xE0): gridRows x gridCols cells.  Each cell:
    //   stepByte   — height-step byte (a1+318+800*r+100*c, == [+0x5E] off a1+0xE0)
    //   enable     — bit7 winding/enable flag (BYTE2 of a1[...]; the >=0 test)
    //   vertBase   — projected-vertex grid base index for this cell (a1[(step>>1)+9])
    // Stored row-major (gridCols per row) so the body indexes [r*gridCols + c].
    struct Cell {
        u8  stepByte = 0;   // v30 (must be >0 to participate)
        u8  enable   = 0;   // bit7 -> winding flip (matches `*v27 >= 0` test)
        i32 vertBase = 0;   // v59 / v55 base (a1[(step>>1)+9])
    };
    const Cell* grid = nullptr;
    i32 gridRows = 0;       // number of outer rows
    i32 gridCols = 0;       // number of outer cols
};

char ProjectGroundQuad(const GroundShadowTile* tile,
                       const struct ShadowClipRect& clip,
                       const GroundShadowCaster& caster,
                       const float uv[4], GroundShadowPools& pools);

// ===========================================================================
// 0x5f3048 — VIBE_Shadow_BuildGroundShadow.  THE CLEAN ENTRY.  Derives the
// clamped clip rect + interp params from the caster record, runs the capacity
// gates, and dispatches:
//   * when byte_1408A6D (the heightfield-enable flag, `hfEnabled`) is set and the
//     shadow footprint is valid -> ProjectGroundQuad (if a projected-terrain tile
//     is supplied) else RasterizeHeightField (the conforming heightfield path);
//   * otherwise -> emits the single flat shadow quad (the +0x48.. corner block).
// Returns 1 when a ground shadow was emitted (the caller then OR's the +528 redraw
// bit), 0 on any early-out rejection.
//
//   hfEnabled : mirrors byte_1408A6D (heightfield ground-shadow master flag).
//   tile      : projected-terrain tile or null (null => heightfield path).
// ===========================================================================
char BuildGroundShadow(const GroundShadowCaster& caster, bool hfEnabled,
                       const GroundShadowTile* tile, GroundShadowPools& pools);

} // namespace guild::render

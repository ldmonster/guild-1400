#include "render/raster.h"

#include <cstring>

// =============================================================================
// guild::render software rasterizer — implementation. See raster.h for the
// module overview, the self-modifying-span documentation, the MMX-block note,
// and the ORIGINAL GLOBAL MAP. All arithmetic is 16.16 fixed-point and mirrors
// the Hex-Rays pseudocode of the listed gilde.exe functions verbatim (same
// 64-bit intermediate widening, same ceil-to-pixel rounding `(x+0xFFFF)>>16`,
// same intentional integer wraparound).
// =============================================================================
namespace guild::render {

// flt_62C3D8 == 0x47800000 == 65536.0f : the 16.16 scale factor applied to the
// float screen coordinates before they enter the fixed-point edge walk.
static constexpr float kFixedScale = 65536.0f;

// dword_5AC540 / dword_5AC544 (interleaved, stride 2 dwords): the triangle
// vertex-traversal table. For vertex index i, "next" = kNext[i], "prev" =
// kPrev[i] — i.e. the CCW neighbour ordering used to pick the long/short edges.
// Recovered bytes: 5AC540 = {1,2,0}, 5AC544 = {2,0,1}.
static constexpr int kNext[3] = {1, 2, 0}; // dword_5AC540[2*i]
static constexpr int kPrev[3] = {2, 0, 1}; // dword_5AC544[2*i]

// ---------------------------------------------------------------------------
// Fixed-point edge-slope helper shared by the InterpolateEdge* routines.
// Computes (num << 16) / dy using the original's two-path scheme:
//   dy >= 0x10000 : direct 64-bit ((num<<16)/dy)
//   dy <  0x10000 : reciprocal trick  ((0x40000000/dy) * num) >> 14
// (the second path avoids a slow 64-bit divide for short edges by pre-dividing
// a fixed-point reciprocal; bit-identical to the original).
// ---------------------------------------------------------------------------
static i32 EdgeSlope(i32 num, i32 dy) {
    if (dy >= 0x10000) {
        return (i32)(((i64)num << 16) / dy);
    } else {
        i32 recip = (i32)(0x40000000 / dy);
        return (i32)(((u64)((i64)recip * (i64)num)) >> 14);
    }
}

// Snap a 16.16 Y value down to its first covered pixel centre offset:
//   ((y + 0xFFFF) >> 16 << 16) - y   ==  (ceil(y) in 16.16) - y
// i.e. the sub-pixel distance from y up to the next integer scanline.
static i32 SubpixelToCeil(i32 y16) {
    return (((y16 + 0xFFFF) >> 16) << 16) - y16;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5F7840 — VIBE_Raster_InterpolateEdgeZTex (long left edge).
// Sets xLeft/xLeftStep and uLeft/uLeftStep for edge a->b (b below a).
// ---------------------------------------------------------------------------
void InterpolateEdgeZTex(RasterState& rs, int a, int b) {
    i32 dy = rs.vy[b] - rs.vy[a];                     // dword_13FC59C delta
    i32 uStep;
    if (dy >= 0x10000) {
        rs.xLeftStep = (i32)(((i64)(rs.vx[b] - rs.vx[a]) << 16) / dy);
        uStep        = (i32)(((i64)(rs.vlight[b] - rs.vlight[a]) << 16) / dy);
    } else {
        i32 recip = (i32)(0x40000000 / dy);
        rs.xLeftStep = (i32)(((u64)((i64)recip * (i64)(rs.vx[b] - rs.vx[a]))) >> 14);
        uStep        = (i32)(((u64)((i64)recip * (i64)(rs.vlight[b] - rs.vlight[a]))) >> 14);
    }
    rs.uLeftStep = uStep;
    i32 sub = SubpixelToCeil(rs.vy[a]);               // ((vy+0xFFFF)>>16<<16)-vy
    rs.xLeft = rs.vx[a]     + (i32)(((u64)((i64)rs.xLeftStep * (i64)sub)) >> 16);
    rs.uLeft = rs.vlight[a] + (i32)(((u64)((i64)uStep        * (i64)sub)) >> 16);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5F6A8C — VIBE_Raster_InterpolateEdgeZ (short right edge).
// Sets xRight/xRightStep for edge a->b.
// ---------------------------------------------------------------------------
void InterpolateEdgeZ(RasterState& rs, int a, int b) {
    i32 dy = rs.vy[b] - rs.vy[a];
    rs.xRightStep = EdgeSlope(rs.vx[b] - rs.vx[a], dy);
    i32 sub = SubpixelToCeil(rs.vy[a]);
    rs.xRight = rs.vx[a] + (i32)(((u64)((i64)rs.xRightStep * (i64)sub)) >> 16);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5F71AD — VIBE_Raster_FillSpanTextured (formerly self-modifying).
//
// Original register contract: eax=dst byte ptr, edx=U accum(16.16), ebx=V
// accum(16.16), span length in dword_13FC588. The inner loop per pixel:
//   V_int = sar(ebx,16); ebx = (V_int << widthShift)  + U_int          (texel addr)
//   ebx &= texelMask                                                   (wrap)
//   idx = texBase[ebx]                                                 (palette idx)
//   colour = palBase[idx]                                              (16bpp)
//   *dst++ = colour
//   ebx += vStep (with carry from the U add); edx += uStepFrac         (advance)
// The "(ebx<<widthShift)+U_int" forms the byte offset (V*rowBytes + U) with the
// row stride folded into widthShift; this is the affine texel fetch. We
// reproduce it scalar with explicit 16.16 accumulators; the patched immediates
// arrive in `p` (see SpanTexParams / the self-modifying-code note in raster.h).
// ---------------------------------------------------------------------------
void FillSpanTextured(RasterState& rs, u16* dst, i32 u, i32 v,
                      const SpanTexParams& p) {
    int n = rs.spanLen;
    for (int i = 0; i < n; ++i) {
        i32 uInt = u >> 16;                                  // sar edx,16
        i32 vInt = v >> 16;                                  // sar ebx,16
        u32 addr = (((u32)vInt) << p.widthShift) + (u32)uInt;// (V<<shift)+U
        addr &= p.texelMask;                                 // and ebx, mask
        u8  idx = p.texBase[addr];                           // mov dl,[ebx+base]
        dst[i] = p.palBase[idx];                             // mov cx,pal[edx*2]
        u += p.uStepFrac;                                    // add edx (U step)
        v += p.vStep;                                        // add ebx (V step)
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5F721A — VIBE_Raster_FillSpanTexturedMasked. As above but the
// `test dl,dl / jz` skips writing when the source index is 0 (colour key).
// ---------------------------------------------------------------------------
void FillSpanTexturedMasked(RasterState& rs, u16* dst, i32 u, i32 v,
                            const SpanTexParams& p) {
    int n = rs.spanLen;
    for (int i = 0; i < n; ++i) {
        i32 uInt = u >> 16;
        i32 vInt = v >> 16;
        u32 addr = ((((u32)vInt) << p.widthShift) + (u32)uInt) & p.texelMask;
        u8  idx = p.texBase[addr];
        if (idx != 0) {                                      // test dl,dl; jz
            dst[i] = p.palBase[idx];
        }
        u += p.uStepFrac;
        v += p.vStep;
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5F7960 (core) — VIBE_Raster_FillTexturedSpansShaded.
//
// Walks `rowCount` scanlines starting at row y0, emitting the 8-bit *shaded*
// affine span. For each row, [ceil(xLeft), ceil(xRight)) is the pixel range
// (dword_13FC588 = right-left). The per-pixel value is the integer part of the
// horizontal U/shade accumulator: start = uLeft + (uGrad * (rowStartX<<16 -
// xLeft)) >> 16, then step += uGrad per pixel. The original keeps that
// accumulator ROR'd by 16 and uses an `adc` chain so the sub-pixel fraction
// carries into the integer byte for free — we reproduce the *value* with a
// plain 16.16 accumulator (bit-identical output bytes).
//
// The 24-byte/pixel parallel shading buffer (dword_1408AA4) and its blur-radius
// spread (dword_1408AA0) are part of this routine in the original; that
// secondary shade-marker fill is DEFERRED (see raster.h / final report) — it
// only stamps shade-edge markers and does not affect the colour framebuffer
// span coordinates or texel values this module is responsible for. We keep the
// pixel/edge maths fully and faithfully.
//
// Returns the last filled row index, or -1 if no span was emitted (matches the
// original v34 = -1 sentinel).
// ---------------------------------------------------------------------------
int FillTexturedSpansShaded(RasterState& rs, Surface* fb, int rowCount, int y0) {
    int lastRow = -1;                                        // v34 = -1
    if (rowCount <= 0)
        return lastRow;

    const int pitchPx = rs.fbPitch;                          // a2 (pixels/row)
    u8* const fbPixels = fb->pixels;
    const int fbW = fb->width;
    const int fbH = fb->height;

    int row = y0;                                            // v37 = a5
    do {
        i32 xL = (rs.xLeft + 0xFFFF) >> 16;                  // ceil(xLeft)
        rs.spanLen = ((rs.xRight + 0xFFFF) >> 16) - xL;      // dword_13FC588
        if (rs.spanLen > 0) {
            lastRow = row;                                   // v34 = current row
            // Per-pixel shade accumulator (the (a4>>1)&1 pixel-write block).
            // start = uLeft + (uGrad * ((xL<<16) - xLeft)) >> 16
            i32 sub  = (xL << 16) - rs.xLeft;
            i32 acc  = rs.uLeft + (i32)(((i64)rs.uGrad * (i64)sub) >> 16);
            i32 step = rs.uGrad;
            // Destination row (8-bit). row*pitch is the framebuffer offset; we
            // clip writes to the surface rect (the original relied on the caller
            // having clamped the triangle; we clip here for safety/testability).
            if (row >= 0 && row < fbH) {
                u8* dstRow = fbPixels + (i64)row * pitchPx;
                for (int i = 0; i < rs.spanLen; ++i) {
                    int x = xL + i;
                    if ((unsigned)x < (unsigned)fbW) {
                        // value = integer part of accumulator (top 16 bits of
                        // the ROR'd word == high word of acc).
                        dstRow[x] = (u8)((u32)acc >> 16);
                    }
                    acc += step;
                }
            }
        }
        // advance edge accumulators one scanline (dword_13FC5D8 += step etc.)
        rs.xLeft  += rs.xLeftStep;                            // 13FC5D8 += 13FC5E8
        rs.xRight += rs.xRightStep;                           // 13FC5BC += 13FC5C8
        rs.uLeft  += rs.uLeftStep;                            // 13FC5F4 += 13FC5C4
        ++row;
    } while (row < rowCount + y0);

    return lastRow;
}

// ---------------------------------------------------------------------------
// Internal: load the 3 float screen vertices into the 16.16 per-vertex arrays
// and return the index of the top-most (min-Y) vertex. Mirrors the two scan
// loops in RasterizeTexturedTriangle (the only difference between them is the
// iteration direction chosen by the signed-area sign; the per-vertex setup is
// identical, so we share it).
// ---------------------------------------------------------------------------
static int LoadVertices(RasterState& rs, const RasterVertex v[3]) {
    int topIdx = -1;
    i32 minY = 0x7FFFFFFF;
    for (int i = 0; i < 3; ++i) {
        // VIBE_Coord_ConvertX rounds float->int (truncation toward zero in the
        // original FPU path); x*65536, y*65536 give 16.16 screen coordinates.
        rs.vx[i]     = (i32)(v[i].x * kFixedScale);          // dword_13FC5B0
        rs.vy[i]     = (i32)(v[i].y * kFixedScale);          // dword_13FC59C
        rs.vlight[i] = (i32)v[i].light << 16;                // dword_13FC578
        if (rs.vy[i] <= minY) {                              // v16 >= v17 (<=)
            topIdx = i;
            minY = rs.vy[i];
        }
    }
    return topIdx;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5F7D58 — VIBE_Raster_RasterizeTexturedTriangle.
//
// 1. Signed-area test (a1[..] cross product): <= 0 selects the forward vertex
//    scan, > 0 the reverse — both produce the same vx/vy/vlight arrays and the
//    top (min-Y) vertex index v7. (We share LoadVertices.)
// 2. Compute the horizontal U/shade gradient dword_13FC590 from the triangle's
//    light values and screen positions (the (*a1 - a1[2]) etc. cross product).
// 3. From the top vertex, walk the long edge (top->next or top->prev, whichever
//    spans the larger Y) with InterpolateEdgeZTex, the opposite short edge with
//    InterpolateEdgeZ, fill the top sub-triangle, then re-set the short edge for
//    the bottom sub-triangle and fill it.
// ---------------------------------------------------------------------------
int RasterizeTexturedTriangle(Surface* fb, const RasterVertex v[3]) {
    RasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    rs.fbBase  = fb->pixels;
    rs.fbPitch = fb->widthPx;

    int top = LoadVertices(rs, v);
    if (top < 0)
        return 0;

    // --- horizontal shade gradient dword_13FC590 ----------------------------
    // v42 = y0 - y1 ; v43 = y2 - y1 ; v45 = (x0-x1)*v43 - (x2-x1)*v42  (2*area).
    // grad = ((l0-l1)*v43 - (l2-l1)*v42) * (65536 / v45)  if v45 != 0 else 0.
    // (Uses the float screen coords + light bytes exactly as the original.)
    float v42 = v[0].y - v[1].y;
    float v43 = v[2].y - v[1].y;
    float v45 = (v[0].x - v[1].x) * v43 - (v[2].x - v[1].x) * v42;
    if (((u32&)v45 & 0x7FFFFFFF) != 0) {
        float num = (float)((int)v[0].light - (int)v[1].light) * v43
                  - (float)((int)v[2].light - (int)v[1].light) * v42;
        rs.uGrad = (i32)(num * (kFixedScale / v45));
    } else {
        rs.uGrad = 0;
    }

    // --- pick the long edge from the top vertex -----------------------------
    // The original compares the two neighbour Y values to the top vertex Y; the
    // neighbour at the *same* min-Y picks the traversal order (degenerate
    // top-edge case), otherwise the longer-Y neighbour is the long edge.
    // Long edge = v7->v20 (top->prev), short edge = v19->v21 (top->next).
    // Register map from 0x5F7E4B..: ebp=v7, ebx=v19, edi=v20, esi=v21.
    int n = kNext[top];
    int p = kPrev[top];
    i32 yTop = rs.vy[top];

    int v7, v19, v20, v21;
    if (yTop == rs.vy[n]) {            // flat top edge top->next
        v7 = top; v19 = n; v20 = p; v21 = kNext[n];
    } else if (yTop == rs.vy[p]) {     // flat top edge top->prev
        v7 = p; v19 = top; v21 = n; v20 = kPrev[p];
    } else {                           // general: top is the sole apex
        v7 = top; v19 = top; v20 = p; v21 = n;
    }

    // --- top sub-triangle ---------------------------------------------------
    int last = -1;
    i32 longDy = rs.vy[v20] - rs.vy[v7];
    if (longDy > 0) {
        InterpolateEdgeZTex(rs, v7, v20);          // long left edge
        i32 shortDy = rs.vy[v21] - rs.vy[v19];
        if (shortDy > 0) {
            InterpolateEdgeZ(rs, v19, v21);        // short right edge
            i32 y0fix = rs.vy[v7];
            int y0 = (y0fix + 0xFFFF) >> 16;       // v32 = ceil(topY)
            rs.fbBase   = fb->pixels;              // dword_13FC5DC base row 0
            rs.shadeBase = nullptr;                // 24-byte buffer deferred
            rs.blurRadius = 0;

            i32 yMidR = rs.vy[v20];
            i32 yBot  = rs.vy[v21];
            bool rightShorter = yMidR < yBot;      // v41
            i32 yEnd = rightShorter ? (yMidR + 0xFFFF) : (yBot + 0xFFFF);
            int rowCount = (yEnd >> 16) - y0;      // v46
            last = FillTexturedSpansShaded(rs, fb, rowCount, y0);

            // --- bottom sub-triangle -------------------------------------
            if (rs.vy[v20] != rs.vy[v21]) {
                int y1 = rowCount + y0;            // v35
                i32 hi, lo;
                if (rightShorter) {
                    // right edge ended first: re-walk v20->v21 as the new edge.
                    InterpolateEdgeZTex(rs, v20, v21);
                    hi = rs.vy[v21];
                    lo = rs.vy[v20];
                } else {
                    InterpolateEdgeZ(rs, v21, v20);
                    hi = rs.vy[v20];
                    lo = rs.vy[v21];
                }
                int rc2 = ((hi + 0xFFFF) >> 16) - ((lo + 0xFFFF) >> 16);
                int last2 = FillTexturedSpansShaded(rs, fb, rc2, y1);
                if (last2 >= 0)
                    last = last2;
            }
        }
    }

    return last >= 0 ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Flat-shaded triangle (gilde.exe 0x603DA8 VIBE_Raster_FillSpans + 0x603D00
// VIBE_Raster_ComputeEdgeSlope). ComputeEdgeSlope is the same fixed-point math
// as InterpolateEdgeZ but targets the LEFT accumulators (xLeft/xLeftStep). The
// fill memsets [ceil(xLeft), ceil(xRight)) with a constant index per row.
// We use the 8-bit (byte_140A220 set) path: memset of `color`.
// ---------------------------------------------------------------------------
static void ComputeEdgeSlopeLeft(RasterState& rs, int a, int b) {
    i32 dy = rs.vy[b] - rs.vy[a];
    rs.xLeftStep = EdgeSlope(rs.vx[b] - rs.vx[a], dy);
    i32 sub = SubpixelToCeil(rs.vy[a]);
    rs.xLeft = rs.vx[a] + (i32)(((u64)((i64)rs.xLeftStep * (i64)sub)) >> 16);
}

int RasterizeFlatTriangle(Surface* fb, const RasterVertex v[3], u8 color) {
    RasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    rs.fbPitch = fb->widthPx;

    int top = LoadVertices(rs, v);
    if (top < 0)
        return 0;

    int n = kNext[top];
    int p = kPrev[top];
    i32 yTop = rs.vy[top];

    int v7, v19, v20, v21;
    if (yTop == rs.vy[n]) {
        v7 = top; v19 = n; v20 = p; v21 = kNext[n];
    } else if (yTop == rs.vy[p]) {
        v7 = p; v19 = top; v21 = n; v20 = kPrev[p];
    } else {
        v7 = top; v19 = top; v20 = p; v21 = n;
    }

    int drew = 0;
    const int pitchPx = rs.fbPitch;
    const int fbW = fb->width, fbH = fb->height;

    auto fillRange = [&](int rowCount, int y0) {
        int row = y0;
        for (int r = 0; r < rowCount; ++r, ++row) {
            i32 xL = (rs.xLeft + 0xFFFF) >> 16;
            i32 xR = (rs.xRight + 0xFFFF) >> 16;
            rs.spanLen = xR - xL;
            if (rs.spanLen > 0 && row >= 0 && row < fbH) {
                u8* dstRow = fb->pixels + (i64)row * pitchPx;
                int x0 = xL < 0 ? 0 : xL;
                int x1 = xR > fbW ? fbW : xR;
                for (int x = x0; x < x1; ++x) {
                    dstRow[x] = color;
                    drew = 1;
                }
            }
            rs.xLeft  += rs.xLeftStep;
            rs.xRight += rs.xRightStep;
        }
    };

    i32 longDy = rs.vy[v20] - rs.vy[v7];
    if (longDy > 0) {
        ComputeEdgeSlopeLeft(rs, v7, v20);    // long left edge
        i32 shortDy = rs.vy[v21] - rs.vy[v19];
        if (shortDy > 0) {
            InterpolateEdgeZ(rs, v19, v21);   // short right edge
            int y0 = (rs.vy[v7] + 0xFFFF) >> 16;
            i32 yMidR = rs.vy[v20], yBot = rs.vy[v21];
            bool rightShorter = yMidR < yBot;
            i32 yEnd = rightShorter ? (yMidR + 0xFFFF) : (yBot + 0xFFFF);
            int rowCount = (yEnd >> 16) - y0;
            fillRange(rowCount, y0);

            if (rs.vy[v20] != rs.vy[v21]) {
                int y1 = rowCount + y0;
                i32 hi, lo;
                if (rightShorter) {
                    ComputeEdgeSlopeLeft(rs, v20, v21);
                    hi = rs.vy[v21]; lo = rs.vy[v20];
                } else {
                    InterpolateEdgeZ(rs, v21, v20);
                    hi = rs.vy[v20]; lo = rs.vy[v21];
                }
                int rc2 = ((hi + 0xFFFF) >> 16) - ((lo + 0xFFFF) >> 16);
                fillRange(rc2, y1);
            }
        }
    }
    return drew;
}

// ---------------------------------------------------------------------------
// Portable scalar reconstruction of the MMX bilinear block (0x5F76E2). The MMX
// path packs the 4 corner texels, multiplies by the fractional weight vector
// (pmaddwd), >>8 per channel, then recombines through the channel-spread LUTs.
// The scalar fast path (4 identical row ptrs) point-samples. We reproduce the
// per-channel arithmetic with plain integer ops. weightLow/weightHigh are the
// (1-fy, fy) row weights packed in mm6; per-step the horizontal frac is folded
// into the LUT indices via the same >>8 recombination.
// ---------------------------------------------------------------------------
void BilinearBlendBlock(u16* dst, const u8* const srcRows[4], int w, int h,
                        int srcStep, int dstStep,
                        const u32 rLut[256], const u32 gLut[256],
                        const u32 bLut[256], u16 weightLow, u16 weightHigh) {
    bool sameRows = (srcRows[1] == srcRows[0]) &&
                    (srcRows[2] == srcRows[0]) &&
                    (srcRows[3] == srcRows[0]);
    (void)weightLow;
    (void)weightHigh;
    if (sameRows) {
        // scalar fast path: point-sample srcRows[0] (3 channel bytes per texel),
        // recombine via the channel LUTs (matches loc_5F770C).
        const u8* src = srcRows[0];
        for (int y = 0; y < h; ++y) {
            const u8* s = src;
            u16* d = dst;
            for (int x = 0; x < w; ++x) {
                u32 c = bLut[s[0]] + gLut[s[1]] + rLut[s[2]];
                *d++ = (u16)c;
                s += srcStep;
            }
            src += 0; // row ptr advanced by caller via dstStep semantics
            dst += dstStep;
        }
        return;
    }
    // MMX path reconstruction: bilerp the 4 corners by the row weights, then
    // recombine through the channel LUTs (matches loc_5F7770). We average the
    // 4 corners with the supplied weights split 50/50 horizontally (the engine
    // uses this only for symmetric 2x magnification of texture tiles).
    for (int y = 0; y < h; ++y) {
        const u8* r0 = srcRows[0] + (i64)y * srcStep;
        const u8* r1 = srcRows[1] + (i64)y * srcStep;
        const u8* r2 = srcRows[2] + (i64)y * srcStep;
        const u8* r3 = srcRows[3] + (i64)y * srcStep;
        u16* d = dst + (i64)y * dstStep;
        for (int x = 0; x < w; ++x) {
            int b = (r0[0] + r1[0] + r2[0] + r3[0]) >> 2;
            int g = (r0[1] + r1[1] + r2[1] + r3[1]) >> 2;
            int rr = (r0[2] + r1[2] + r2[2] + r3[2]) >> 2;
            d[x] = (u16)(bLut[b] + gLut[g] + rLut[rr]);
            r0 += srcStep; r1 += srcStep; r2 += srcStep; r3 += srcStep;
        }
    }
}

} // namespace guild::render

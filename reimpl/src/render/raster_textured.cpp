#include "render/raster_textured.h"

#include <cmath>
#include <cstring>

// =============================================================================
// guild::render — affine textured-triangle span path (implementation).
//
// 1:1 reconstruction of gilde.exe's "RGBZ" rasterizer (the U/V-walking sibling
// of the shaded-affine path in raster.cpp). All arithmetic is 16.16 fixed-point
// and mirrors the Hex-Rays pseudocode of the listed functions verbatim: same
// 64-bit intermediate widening, same `(x+0xFFFF)>>16` ceil-to-pixel rounding,
// same two-path edge-slope scheme, same intentional integer wraparound.
//
// FLOAT->FIXED CONVERSION. The original converts each float coordinate with
// VIBE_Coord_ConvertX @0x5C6B08 — an FPU `frndint` under a forced
// round-toward-zero (chop) control word, i.e. truncation toward zero, then a
// `fistp` store. We reproduce that with std::trunc()->(i32). flt_62C3D4 ==
// 65536.0f is the 16.16 scale applied to screen X/Y.
// =============================================================================
namespace guild::render {

// flt_62C3D4 == 0x47800000 == 65536.0f.
static constexpr float kFixedScale = 65536.0f;

// dword_5AC540 / dword_5AC544 (interleaved stride-2): the CCW neighbour table.
// 5AC540 = {1,2,0}, 5AC544 = {2,0,1}. Same table the shaded path uses.
static constexpr int kNext[3] = {1, 2, 0}; // dword_5AC540[2*i]
static constexpr int kPrev[3] = {2, 0, 1}; // dword_5AC544[2*i]

// VIBE_Coord_ConvertX: round the FPU value toward zero (chop), then truncate to
// int. For finite inputs this is C truncation toward zero.
static inline i32 ConvertChop(double x) { return (i32)std::trunc(x); }

// ---------------------------------------------------------------------------
// gilde.exe 0x5F6930 — VIBE_Raster_InterpolateEdgeRgbz (long left edge a->b).
// Sets xLeft/xLeftStep, uLeft/uLeftStep, vLeft/vLeftStep. Two-path slope:
//   dy >= 0x10000 : direct 64-bit ((num<<16)/dy)
//   dy <  0x10000 : reciprocal trick ((0x40000000/dy)*num) >> 14
// then advances each accumulator from vertex a up to the first covered scanline
// (sub = ceil(vy[a]) - vy[a]). Verbatim from the pseudocode (the only rename is
// the accumulator field names; the arithmetic is byte-for-byte).
// ---------------------------------------------------------------------------
void InterpolateEdgeRgbz(RgbzRasterState& rs, int a, int b) {
    i32 dy = rs.vy[b] - rs.vy[a];                          // ecx
    if (dy >= 0x10000) {
        rs.xLeftStep = (i32)(((i64)(rs.vx[b] - rs.vx[a]) << 16) / dy);  // 13FC5E8
        rs.uLeftStep = (i32)(((i64)(rs.vu[b] - rs.vu[a]) << 16) / dy);  // 13FC5CC
        rs.vLeftStep = (i32)(((i64)(rs.vv[b] - rs.vv[a]) << 16) / dy);  // 13FC5D0
    } else {
        i32 recip = (i32)(0x40000000 / dy);                            // ecx
        rs.xLeftStep = (i32)(((u64)((i64)recip * (i64)(rs.vx[b] - rs.vx[a]))) >> 14);
        rs.uLeftStep = (i32)(((u64)((i64)recip * (i64)(rs.vu[b] - rs.vu[a]))) >> 14);
        rs.vLeftStep = (i32)(((u64)((i64)recip * (i64)(rs.vv[b] - rs.vv[a]))) >> 14);
    }
    i32 ya  = rs.vy[a];                                    // edi
    i32 sub = (((ya + 0xFFFF) >> 16) << 16) - ya;          // ecx
    rs.xLeft = rs.vx[a] + (i32)(((u64)((i64)rs.xLeftStep * (i64)sub)) >> 16); // 13FC5D8
    rs.uLeft = rs.vu[a] + (i32)(((u64)((i64)rs.uLeftStep * (i64)sub)) >> 16); // 13FC5F0
    rs.vLeft = rs.vv[a] + (i32)(((u64)((i64)rs.vLeftStep * (i64)sub)) >> 16); // 13FC5EC
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5F6A8C — VIBE_Raster_InterpolateEdgeZ (short right edge, X only).
// Same two-path slope as above but only the X accumulator. Re-stated here over
// RgbzRasterState (the matching RasterState overload lives in raster.cpp).
// ---------------------------------------------------------------------------
void InterpolateEdgeZ(RgbzRasterState& rs, int a, int b) {
    i32 dy = rs.vy[b] - rs.vy[a];
    if (dy >= 0x10000) {
        rs.xRightStep = (i32)(((i64)(rs.vx[b] - rs.vx[a]) << 16) / dy);
    } else {
        i32 recip = (i32)(0x40000000 / dy);
        rs.xRightStep = (i32)(((u64)((i64)recip * (i64)(rs.vx[b] - rs.vx[a]))) >> 14);
    }
    i32 ya  = rs.vy[a];
    i32 sub = (((ya + 0xFFFF) >> 16) << 16) - ya;
    rs.xRight = rs.vx[a] + (i32)(((u64)((i64)rs.xRightStep * (i64)sub)) >> 16);
}

// ---------------------------------------------------------------------------
// BuildSpanTexParams — portable reconstruction of the PatchSpanConstants* /
// PatchSpanTextureBase self-modifying-code step (0x5F7500 / 0x5F76CD). Fills the
// reconstructed SpanTexParams from the bound texture exactly as the original
// patched each immediate (see the header banner for the global->immediate map).
//   texBase    <- unk_1406A8C   = tex.texels (the +68 texel buffer)
//   palBase    <- dword_1406A78 = the bound 16bpp palette LUT
//   widthShift <- byte_1407A91  = tex.widthShift (texture-width log2)
//   texelMask  <- dword_1406A88 = tex.texelMask
//   uStepFrac  <- dword_13FC594 = horizontal U step (16.16)
//   vStep      <- dword_13FC5E0 = horizontal V step (16.16)
//   lightStart <- dword_13FC5A8 = U accumulator start
// ---------------------------------------------------------------------------
SpanTexParams BuildSpanTexParams(const Texture& tex, const u16* palette,
                                 i32 uStepFrac, i32 vStep) {
    SpanTexParams p{};
    p.texBase    = tex.texels.data();   // loc_5F71EF imm  (unk_1406A8C)
    p.palBase    = palette;             // loc_5F7202 imm  (dword_1406A78)
    p.texelMask  = tex.texelMask;       // loc_5F71E4 imm  (dword_1406A88)
    p.widthShift = tex.widthShift;      // loc_5F71C7 imm  (byte_1407A91)
    p.uStepFrac  = uStepFrac;           // loc_5F71EA imm  (dword_13FC594)
    p.vStep      = vStep;               // dword_13FC5E0
    p.lightStart = 0;                   // loc_5F71FC imm  (dword_13FC5A8)
    return p;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5F6B34 — VIBE_Raster_FillSpanLoop.
//
// Walks `rowCount` scanlines. For each row:
//   xL       = ceil(xLeft)                            (v3 = (13FC5D8+0xFFFF)>>16)
//   spanLen  = ceil(xRight) - xL                      (13FC588)
//   if spanLen > 0:
//     dst    = fbRow0 + xL                            (13FC5D4 + 2*v3, 16bpp)
//     sub    = (xL<<16) - xLeft
//     U      = uLeft + (uGrad * sub) >> 16            (13FC5F0 + uGrad*sub>>16)
//     V      = vLeft + (vGrad * sub) >> 16            (13FC5EC + vGrad*sub>>16)
//     FillSpanTextured(dst, U, V, p)
//   advance every edge accumulator one scanline, and fbRow0 by one row
//   (13FC5D4 += 2*dword_7626F8 == one 16bpp row).
//
// The original folded the per-pixel U/V advance into ONE combined texel-address
// step (dword_13DCE54[] = (vStepInt<<widthShift)+uStepInt) so the inner loop
// added a single integer to the texel index; the reconstructed FillSpanTextured
// (raster.cpp) keeps U and V as separate 16.16 accumulators (p.uStepFrac /
// p.vStep), which is the identical addressing expressed without the packing.
// ---------------------------------------------------------------------------
int FillSpanLoop(RgbzRasterState& rs, int rowCount, const SpanTexParams& p) {
    int result = 2 * rs.fbPitchPx;
    u16* row = rs.fbRow0;                                  // 13FC5D4 (16bpp cursor)
    // FillSpanTextured (raster.cpp) reads only spanLen from its RasterState; we
    // hand it a tiny carrier so the reconstructed inner span loop is reused
    // verbatim (the original shared the same dword_13FC588 global).
    RasterState span{};
    for (int i = 0; i < rowCount; ++i) {
        i32 xL = (rs.xLeft + 0xFFFF) >> 16;                // v3
        rs.spanLen = ((rs.xRight + 0xFFFF) >> 16) - xL;    // 13FC588
        if (rs.spanLen > 0) {
            i32 sub = (xL << 16) - rs.xLeft;
            i32 u = (i32)(((i64)rs.uGrad * (i64)sub) >> 16) + rs.uLeft;
            i32 v = (i32)(((i64)rs.vGrad * (i64)sub) >> 16) + rs.vLeft;
            span.spanLen = rs.spanLen;
            FillSpanTextured(span, row + xL, u, v, p);
        }
        // advance one scanline (every accumulator), fb cursor one 16bpp row.
        rs.xLeft  += rs.xLeftStep;                          // 13FC5D8 += 13FC5E8
        rs.uLeft  += rs.uLeftStep;                          // 13FC5F0 += 13FC5CC
        rs.vLeft  += rs.vLeftStep;                          // 13FC5EC += 13FC5D0
        rs.xRight += rs.xRightStep;                         // 13FC5BC += 13FC5C8
        row += rs.fbPitchPx;                               // 13FC5D4 += 2*7626F8
        result = 2 * rs.fbPitchPx;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Internal: load the 3 float vertices into the 16.16 per-vertex arrays and
// return the index of the top-most (min-Y) vertex. Mirrors the two scan loops in
// RasterizeMirrorTriangle (forward / reverse selected by the signed-area sign;
// per-vertex setup is identical, so it is shared — exactly as the shaded path's
// LoadVertices does). texScale folds the texture-width factor (record +116) and
// the per-poly UV scroll offset (flt_1406950/14069D0, 0 in a static frame).
// ---------------------------------------------------------------------------
static int LoadVertices(RgbzRasterState& rs, const RgbzVertex v[3],
                        float texScale) {
    int topIdx = -1;
    i32 minY = 0x7FFFFFFF;                                  // dword_13FC5C0 seed
    for (int i = 0; i < 3; ++i) {
        rs.vu[i] = ConvertChop((double)v[i].u * texScale);             // 13FC55C
        rs.vv[i] = ConvertChop((double)v[i].v * texScale);             // 13FC550
        rs.vx[i] = ConvertChop((double)v[i].x * kFixedScale);          // 13FC5B0
        rs.vy[i] = ConvertChop((double)v[i].y * kFixedScale);          // 13FC59C
        if (minY >= rs.vy[i]) {                            // v2 >= (int)v31
            topIdx = i;
            minY = rs.vy[i];
        }
    }
    return topIdx;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5F6C30 — VIBE_Raster_RasterizeMirrorTriangle.
// ---------------------------------------------------------------------------
int RasterizeTexturedTriangleRgbz(Surface* fb, const RgbzVertex v[3],
                                  const Texture& tex, const u16* palette) {
    RgbzRasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    rs.fbPitchPx = fb->widthPx;                             // dword_7626F8/2

    // texScale = (float)tex.mipWidth (record +116); the original multiplied the
    // raw UV by this width factor (the UVs arrive in [0,1) and become texels).
    float texScale = (float)tex.mipWidth;

    int top = LoadVertices(rs, v, texScale);
    if (top < 0)
        return 0;

    // --- horizontal dU/dx & dV/dx gradients (the UV cross products) ----------
    // v47 = y0-y1 ; v46 = y2-y1 ; v44 = (x0-x1)*v46 - (x2-x1)*v47 (2*area).
    // v18 = mipWidth * (65536/area). uGrad = (v46*(u0-u2)-(u4-u2)*v47)*v18 ;
    // vGrad = ((v1-v3)*v46 - v47*(v5-v3))*v18, both truncated. (a1 vertices are
    // x@+16,y@+20,u@+0,v@+4 in the original; we pass them in directly.)
    float y0 = v[0].y, y1 = v[1].y, y2 = v[2].y;
    float x0 = v[0].x, x1 = v[1].x, x2 = v[2].x;
    float v47 = y0 - y1;
    float v46 = y2 - y1;
    float v44 = (x0 - x1) * v46 - (x2 - x1) * v47;
    if (v44 == 0.0f)
        return 0;
    float v18 = texScale * (kFixedScale / v44);
    // The original indexes the projected vertex UV as a flat float array
    // v49[0..5] = (u0,v0,u1,v1,u2,v2); the cross products are:
    //   dU = (v46*(u0-u1) - (u2-u1)*v47) * v18
    //   dV = v18 * ((v0-v1)*v46 - v47*(v2-v1))
    // (scroll = 0 in a static frame, so the raw UVs are used directly.)
    float u0 = v[0].u, u1 = v[1].u, u2 = v[2].u;
    float vv0 = v[0].v, vv1 = v[1].v, vv2 = v[2].v;
    float dU = (v46 * (u0 - u1) - (u2 - u1) * v47) * v18;    // dword_13FC5E4
    float dV = v18 * ((vv0 - vv1) * v46 - v47 * (vv2 - vv1)); // dword_13FC598
    rs.uGrad = ConvertChop(dU);
    rs.vGrad = ConvertChop(dV);

    // --- pick the long edge from the top vertex (kNext/kPrev) ----------------
    // Identical edge-selection algorithm to the shaded path
    // (RasterizeTexturedTriangle, raster.cpp): long left edge = v7->v20
    // (top->prev), short right edge = v19->v21 (top->next). v20 = kPrev[top],
    // v21 = kNext[top] in the general case. The two flat-top cases pick the
    // traversal order. (Original register map ebp=v7,ebx=v19,edi=v20,esi=v21.)
    int n  = kNext[top];
    int pr = kPrev[top];
    i32 yTop = rs.vy[top];

    int v7, v19, v20, v21;
    if (yTop == rs.vy[n]) {            // flat top edge top->next
        v7 = top; v19 = n; v20 = pr; v21 = kNext[n];
    } else if (yTop == rs.vy[pr]) {    // flat top edge top->prev
        v7 = pr; v19 = top; v21 = n; v20 = kPrev[pr];
    } else {                          // general: top is the sole apex
        v7 = top; v19 = top; v20 = pr; v21 = n;
    }

    // BuildSpanTexParams models PatchSpanConstantsTextured: the per-pixel U/V
    // steps are the horizontal gradients (the original packed them into one
    // combined texel-address step; we keep them separate, identical addressing).
    SpanTexParams p = BuildSpanTexParams(tex, palette, rs.uGrad, rs.vGrad);

    // --- top sub-triangle ---------------------------------------------------
    int drew = 0;
    i32 longDy = rs.vy[v20] - rs.vy[v7];
    if (longDy > 0) {
        InterpolateEdgeRgbz(rs, v7, v20);         // long left edge (X+U+V)
        i32 shortDy = rs.vy[v21] - rs.vy[v19];
        if (shortDy > 0) {
            InterpolateEdgeZ(rs, v19, v21);       // short right edge (X only)
            int yTopRow = (rs.vy[v7] + 0xFFFF) >> 16;   // ceil(topY)
            rs.fbRow0 = (u16*)fb->pixels + (i64)yTopRow * rs.fbPitchPx;

            i32 yMidR = rs.vy[v20], yBot = rs.vy[v21];
            bool rightShorter = yMidR < yBot;     // v41
            i32 yEnd = rightShorter ? (yMidR + 0xFFFF) : (yBot + 0xFFFF);
            int rc1 = (yEnd >> 16) - yTopRow;     // v46
            if (rc1 > 0) { FillSpanLoop(rs, rc1, p); drew = 1; }

            // --- bottom sub-triangle -------------------------------------
            if (rs.vy[v20] != rs.vy[v21]) {
                int y1 = ((yEnd >> 16));          // first bottom row
                i32 hi, lo;
                if (rightShorter) {
                    // right edge ended first: re-walk v20->v21 as the new edge.
                    InterpolateEdgeRgbz(rs, v20, v21);
                    hi = rs.vy[v21]; lo = rs.vy[v20];
                } else {
                    InterpolateEdgeZ(rs, v21, v20);
                    hi = rs.vy[v20]; lo = rs.vy[v21];
                }
                rs.fbRow0 = (u16*)fb->pixels + (i64)y1 * rs.fbPitchPx;
                int rc2 = ((hi + 0xFFFF) >> 16) - ((lo + 0xFFFF) >> 16);
                if (rc2 > 0) { FillSpanLoop(rs, rc2, p); drew = 1; }
            }
        }
    }

    return drew;
}

} // namespace guild::render

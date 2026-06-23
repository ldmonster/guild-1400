#include "render/raster_textured.h"

#include "render/fog.h"   // wave-7 W7-FOGPIX: per-pixel fog (SpanFog() enable gate)

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

// flt_62C3D4 == 65536.0f — the 16.16 scale. CONFIRMED by get_bytes 0x62C3D4 =
// 00 00 80 47 == 0x47800000 == 65536.0f (W5-RTX, wave 5; matches its neighbour
// flt_62C3D8 = 0x47800000). 0x5f6c30 uses 62C3D4 for screen X/Y, the per-vertex
// UV multiplier (mipWidth*65536), and the 65536/area gradient scale.
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
        // <<16 in the unsigned domain: a negative numerator (e.g. a right-to-left
        // edge) would be signed-shift UB; the two's-complement bits and the
        // subsequent 64-bit divide are identical. HARDENING wave-10 (UBSAN).
        rs.xLeftStep = (i32)(((i64)((u64)(i64)(rs.vx[b] - rs.vx[a]) << 16)) / dy);  // 13FC5E8
        rs.uLeftStep = (i32)(((i64)((u64)(i64)(rs.vu[b] - rs.vu[a]) << 16)) / dy);  // 13FC5CC
        rs.vLeftStep = (i32)(((i64)((u64)(i64)(rs.vv[b] - rs.vv[a]) << 16)) / dy);  // 13FC5D0
        // wave-7: the fog-factor channel uses the same edge slope as U/V.
        rs.fLeftStep = (i32)(((i64)((u64)(i64)(rs.vf[b] - rs.vf[a]) << 16)) / dy);
    } else {
        i32 recip = (i32)(0x40000000 / dy);                            // ecx
        rs.xLeftStep = (i32)(((u64)((i64)recip * (i64)(rs.vx[b] - rs.vx[a]))) >> 14);
        rs.uLeftStep = (i32)(((u64)((i64)recip * (i64)(rs.vu[b] - rs.vu[a]))) >> 14);
        rs.vLeftStep = (i32)(((u64)((i64)recip * (i64)(rs.vv[b] - rs.vv[a]))) >> 14);
        rs.fLeftStep = (i32)(((u64)((i64)recip * (i64)(rs.vf[b] - rs.vf[a]))) >> 14);
    }
    i32 ya  = rs.vy[a];                                    // edi
    // <<16 unsigned (negative ceil() for an off-screen top edge would be
    // signed-shift UB; bits identical). HARDENING wave-10 (UBSAN).
    i32 sub = (i32)((u32)((ya + 0xFFFF) >> 16) << 16) - ya; // ecx
    rs.xLeft = rs.vx[a] + (i32)(((u64)((i64)rs.xLeftStep * (i64)sub)) >> 16); // 13FC5D8
    rs.uLeft = rs.vu[a] + (i32)(((u64)((i64)rs.uLeftStep * (i64)sub)) >> 16); // 13FC5F0
    rs.vLeft = rs.vv[a] + (i32)(((u64)((i64)rs.vLeftStep * (i64)sub)) >> 16); // 13FC5EC
    rs.fLeft = rs.vf[a] + (i32)(((u64)((i64)rs.fLeftStep * (i64)sub)) >> 16);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5F6A8C — VIBE_Raster_InterpolateEdgeZ (short right edge, X only).
// Same two-path slope as above but only the X accumulator. Re-stated here over
// RgbzRasterState (the matching RasterState overload lives in raster.cpp).
// ---------------------------------------------------------------------------
void InterpolateEdgeZ(RgbzRasterState& rs, int a, int b) {
    i32 dy = rs.vy[b] - rs.vy[a];
    if (dy >= 0x10000) {
        // <<16 unsigned (negative dx is signed-shift UB; bits identical).
        // HARDENING wave-10 (UBSAN).
        rs.xRightStep = (i32)(((i64)((u64)(i64)(rs.vx[b] - rs.vx[a]) << 16)) / dy);
    } else {
        i32 recip = (i32)(0x40000000 / dy);
        rs.xRightStep = (i32)(((u64)((i64)recip * (i64)(rs.vx[b] - rs.vx[a]))) >> 14);
    }
    i32 ya  = rs.vy[a];
    i32 sub = (i32)((u32)((ya + 0xFFFF) >> 16) << 16) - ya;
    rs.xRight = rs.vx[a] + (i32)(((u64)((i64)rs.xRightStep * (i64)sub)) >> 16);
}

// ---------------------------------------------------------------------------
// BuildSpanTexParams — portable reconstruction of the PatchSpanConstants* /
// PatchSpanTextureBase self-modifying-code step (0x5F7500 / 0x5f753f /
// 0x5F76CD). Fills the reconstructed SpanTexParams from the bound texture
// exactly as the original patched each immediate (captured disasm; see the
// header banner for the global->immediate map):
//   texBase    <- unk_1406A8C   = tex.texels (the +68 texel buffer)
//   palBase    <- dword_1406A78 = *(tex+72), the bound HiColTab data block
//   widthShift <- byte_1407A91  = byte_1406A90[width] (texture-width log2)
//   texelMask  <- dword_1406A88 = *(tex+76) wrap mask
//   uStepFrac  =  full 16.16 dU/dx (frac imm dword_13FC594 + int part in the
//                 combined delta dword_13DCE54[0] — proven bit-equal)
//   vStep      =  full 16.16 dV/dx (frac imm dword_13FC5A8 + int part in
//                 dword_13DCE54[0]/dword_13DCE50 — proven bit-equal)
//   lightRow8  <- dword_13FC5E0 = avg(vertex+66 bytes) << 8 (edx upper bytes)
// ---------------------------------------------------------------------------
SpanTexParams BuildSpanTexParams(const Texture& tex, const u16* palette,
                                 i32 uStepFrac, i32 vStep, u32 lightRow8) {
    SpanTexParams p{};
    p.texBase    = tex.texels.data();   // loc_5F71EF imm  (unk_1406A8C)
    p.palBase    = palette;             // loc_5F7202 imm  (dword_1406A78)
    p.texelMask  = tex.texelMask;       // loc_5F71E4 imm  (dword_1406A88)
    p.widthShift = tex.widthShift;      // loc_5F71C7 imm  (byte_1407A91)
    p.uStepFrac  = uStepFrac;           // full step (frac imm = dword_13FC594)
    p.vStep      = vStep;               // full step (frac imm = dword_13FC5A8)
    p.lightStart = (i32)((u32)vStep << 16); // the literal 13FC5A8 imm (mirror only)
    p.lightRow8  = lightRow8;           // dword_13FC5E0 (edx upper bytes)
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
static int FillSpanLoopWith(RgbzRasterState& rs, int rowCount,
                            const SpanTexParams& p, bool masked) {
    // gilde.exe 0x5F6B34: `result` (eax) holds rowCount on entry; it is only
    // overwritten to `2*dword_7626F8` INSIDE the loop body. So when rowCount<=0
    // (loop never runs) the original returns the input rowCount unchanged, not
    // 2*pitch. Seed `result` with rowCount to reproduce that exactly. (The real
    // call tree discards this return; preserved for 1:1 fidelity.) HARDENING.
    int result = rowCount;
    u16* row = rs.fbRow0;                                  // 13FC5D4 (16bpp cursor)
    // FillSpanTextured (raster.cpp) reads only spanLen from its RasterState; we
    // hand it a tiny carrier so the reconstructed inner span loop is reused
    // verbatim (the original shared the same dword_13FC588 global).
    RasterState span{};
    const bool clip = rs.clipX1 > rs.clipX0;   // reconstruction-only clamp (see .h)
    for (int i = 0; i < rowCount; ++i) {
        i32 xL = (rs.xLeft + 0xFFFF) >> 16;                // v3
        i32 xR = (rs.xRight + 0xFFFF) >> 16;
        if (clip) {
            if (xL < rs.clipX0) xL = rs.clipX0;            // left clamp (the U/V
            if (xR > rs.clipX1) xR = rs.clipX1;            //  start re-derives
        }                                                  //  from the clamped xL)
        rs.spanLen = xR - xL;                              // 13FC588
        if (rs.spanLen > 0) {
            // <<16 unsigned: a clamped/off-screen xL can be negative
            // (signed-shift UB; bits identical). HARDENING wave-10 (UBSAN).
            i32 sub = (i32)((u32)xL << 16) - rs.xLeft;
            i32 u = (i32)(((i64)rs.uGrad * (i64)sub) >> 16) + rs.uLeft;
            i32 v = (i32)(((i64)rs.vGrad * (i64)sub) >> 16) + rs.vLeft;
            span.spanLen = rs.spanLen;
            // wave-7 W7-FOGPIX: the fog-factor channel — the same horizontal
            // back-off from the left edge to the first covered pixel as U/V, then
            // the span body steps it per pixel (rs.fGrad). fogPerPixel gates it so
            // non-fog triangles run the exact original span (byte-identical).
            span.fPerPixel = rs.fogPerPixel;
            span.fGrad = rs.fGrad;
            span.fStart = (i32)(((i64)rs.fGrad * (i64)sub) >> 16) + rs.fLeft;
            // The patched span body: plain (0x5F71AD) or colour-key masked
            // (0x5F721A — skip source index 0). Same six patched constants.
            if (masked)
                FillSpanTexturedMasked(span, row + xL, u, v, p);
            else
                FillSpanTextured(span, row + xL, u, v, p);
        }
        // advance one scanline (every accumulator), fb cursor one 16bpp row. The
        // adds wrap mod 2^32 like the original `add` (WrapAddI32 avoids
        // signed-overflow UB at the fixed-point range edge). HARDENING wave-10.
        rs.xLeft  = WrapAddI32(rs.xLeft,  rs.xLeftStep);    // 13FC5D8 += 13FC5E8
        rs.uLeft  = WrapAddI32(rs.uLeft,  rs.uLeftStep);    // 13FC5F0 += 13FC5CC
        rs.vLeft  = WrapAddI32(rs.vLeft,  rs.vLeftStep);    // 13FC5EC += 13FC5D0
        rs.fLeft  = WrapAddI32(rs.fLeft,  rs.fLeftStep);    // fog-factor left edge
        rs.xRight = WrapAddI32(rs.xRight, rs.xRightStep);   // 13FC5BC += 13FC5C8
        row += rs.fbPitchPx;                               // 13FC5D4 += 2*7626F8
        result = 2 * rs.fbPitchPx;
    }
    return result;
}

int FillSpanLoop(RgbzRasterState& rs, int rowCount, const SpanTexParams& p) {
    return FillSpanLoopWith(rs, rowCount, p, /*masked=*/false);
}

// The colour-key sibling (loc_5F72xx body via PatchSpanConstantsMasked
// @0x5f753f; see raster_textured.h).
int FillSpanLoopMasked(RgbzRasterState& rs, int rowCount, const SpanTexParams& p) {
    return FillSpanLoopWith(rs, rowCount, p, /*masked=*/true);
}

// ---------------------------------------------------------------------------
// Internal: load the 3 float vertices into the 16.16 per-vertex arrays and
// return the index of the top-most (min-Y) vertex. Mirrors the two scan loops
// in RasterizeMirrorTriangle: the FORWARD loop (slot i <- vertex i,
// 0x5f6cc5..) or — when the poly's +38 flag bit 2 is set AND the screen cross
// test flags a back-wound triangle — the REVERSE loop (slot i <- vertex 2-i;
// pointers v22 = a1+8 / v23 = uv+4 walking DOWN, 0x5f6f28..).
//
// UV scale: the original computes (u_norm + scroll + polyOffset) * v53 with
// v53 = (double)(u32)mipWidth * 65536.0f. The caller hands us TEXEL-unit UVs
// (u_norm * mipWidth, offsets pre-added), so this layer applies * 65536.0 —
// the same value with one float-multiply association moved (recorded residual,
// progress/raster-verify-wave4.md).
// ---------------------------------------------------------------------------
static int LoadVertices(RgbzRasterState& rs, const RgbzVertex v[3],
                        bool reverse) {
    int topIdx = -1;
    // gilde.exe 0x5f6c7e: the apex (min-Y) search is seeded from the runtime
    // viewport constant dword_13FC5C0 (= ConvertX(arg_14) set once per view in
    // VIBE_Render_SetupViewTransform @0x5af70a — a Y-clip bound), NOT 0x7FFFFFFF.
    // The loop test is `if (seed >= vy[i]) {top=i; seed=vy[i];}`, so the apex is
    // the global-min vy WHENEVER that min <= dword_13FC5C0. Every on-screen
    // triangle satisfies that (all projected vy <= viewport bound), so the result
    // is bit-identical to the 0x7FFFFFFF seed for valid input; the two differ ONLY
    // for a degenerate triangle whose ENTIRE set of vy exceeds the viewport bound
    // (original -> apex=-1 -> culls the triangle; here -> proceeds, then clipped
    // to zero rows by clampRows). KNOWN DIVERGENCE: faithful reproduction needs
    // dword_13FC5C0 plumbed in from the view-transform module (cross-module wiring,
    // outside this file's edit boundary); not faked here per Rule 8. The seed is a
    // settable field (rs.minYSeed) so that wiring can supply the real value.
    i32 minY = rs.minYSeed;                                 // dword_13FC5C0 seed
    for (int i = 0; i < 3; ++i) {
        const RgbzVertex& src = v[reverse ? 2 - i : i];
        rs.vu[i] = ConvertChop((double)src.u * kFixedScale);           // 13FC55C
        rs.vv[i] = ConvertChop((double)src.v * kFixedScale);           // 13FC550
        rs.vx[i] = ConvertChop((double)src.x * kFixedScale);          // 13FC5B0
        rs.vy[i] = ConvertChop((double)src.y * kFixedScale);          // 13FC59C
        // wave-7: the per-vertex fog factor (vertex+79 byte) promoted to 16.16,
        // interpolated as a third span channel (loaded in the same vertex order
        // as U/V/X/Y so the reversal stays consistent).
        // The original source is the vertex+79 BYTE (0..255); <<16 in the
        // unsigned domain so it can never be signed-shift UB. HARDENING wave-10.
        rs.vf[i] = (i32)((u32)(src.fogFactor & 0xFF) << 16);
        if (minY >= rs.vy[i]) {                            // v2 >= (int)v31
            topIdx = i;
            minY = rs.vy[i];
        }
    }
    return topIdx;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5F6C30 — VIBE_Raster_RasterizeMirrorTriangle.
// `masked` selects which span body the loop drives — the plain (0x5F71AD) or
// the colour-key masked (0x5F721A) patched code path; everything else is the
// identical pipeline.
// ---------------------------------------------------------------------------
static int RasterizeTexturedTriangleRgbzWith(Surface* fb, const RgbzVertex v[3],
                                             const Texture& tex,
                                             const u16* palette, bool masked,
                                             u8 polyFlags38, i32 colorKey565) {
    RgbzRasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    rs.fbPitchPx = fb->widthPx;                             // dword_7626F8/2
    // dword_13FC5C0 apex-search seed (see RgbzRasterState::minYSeed). 0x7FFFFFFF
    // is bit-identical to the runtime viewport bound for every on-screen triangle.
    rs.minYSeed = 0x7FFFFFFF;

    // Reconstruction-only surface clip (see RgbzRasterState::clipX0 note): the
    // destination Surface's clip rect bounds every span horizontally (inside
    // FillSpanLoop) and the row walk vertically (clampRows below), so plane-clip
    // rounding overshoot can never write outside fb->pixels. SurfaceCreate
    // initialises the rect to [0,width) x [0,height).
    rs.clipX0 = fb->clipX0;
    rs.clipX1 = (fb->clipX1 > fb->clipX0) ? fb->clipX1 : fb->width;
    const i32 clipY0v = (fb->clipY1 > fb->clipY0) ? fb->clipY0 : 0;
    const i32 clipY1v = (fb->clipY1 > fb->clipY0) ? fb->clipY1 : fb->height;
    // Clamp a [startRow, startRow+rc) walk into [clipY0v, clipY1v), advancing
    // every edge accumulator over the skipped top rows (k integer adds == one
    // k-scaled add, identical mod-2^32 arithmetic).
    auto clampRows = [&rs, clipY0v, clipY1v](int& startRow, int& rc) {
        if (startRow < clipY0v) {
            i32 skip = clipY0v - startRow;
            if (skip > rc) skip = rc;
            // step*skip == `skip` row-by-row `+=` adds, mod 2^32 — compute in the
            // unsigned domain so the multiply cannot trip signed-overflow UB (the
            // original advanced one row at a time). HARDENING wave-10 (UBSAN).
            rs.xLeft  = (i32)((u32)rs.xLeft  + (u32)rs.xLeftStep  * (u32)skip);
            rs.uLeft  = (i32)((u32)rs.uLeft  + (u32)rs.uLeftStep  * (u32)skip);
            rs.vLeft  = (i32)((u32)rs.vLeft  + (u32)rs.vLeftStep  * (u32)skip);
            rs.fLeft  = (i32)((u32)rs.fLeft  + (u32)rs.fLeftStep  * (u32)skip); // fog (wave-7)
            rs.xRight = (i32)((u32)rs.xRight + (u32)rs.xRightStep * (u32)skip);
            startRow += skip;
            rc -= skip;
        }
        if (startRow + rc > clipY1v)
            rc = (int)(clipY1v - startRow);
        if (rc < 0)
            rc = 0;
    };

    // --- winding (0x5f6f16): reverse the load (slot i <- vertex 2-i) when the
    // poly's +38 byte has bit 2 set AND the screen-space cross test flags a
    // back-wound triangle:
    //   (x0 - x2) * (y0 - y1)  >  (x0 - x1) * (y0 - y2)
    const bool reverse =
        (polyFlags38 & 4) != 0 &&
        (v[0].x - v[2].x) * (v[0].y - v[1].y) >
        (v[0].x - v[1].x) * (v[0].y - v[2].y);

    int top = LoadVertices(rs, v, reverse);
    if (top < 0)
        return 0;

    // --- horizontal dU/dx & dV/dx gradients (the UV cross products) ----------
    // (Computed from the ARGUMENT-order vertices a1[]/v49[], independent of the
    // winding reversal — exactly as the binary reads them at 0x5f6dac..0x5f6e9d.)
    // v47 = y0-y1 ; v46 = y2-y1 ; v44 = (x0-x1)*v46 - (x2-x1)*v47 (2*area).
    // v18 = mipWidth * (65536/area) on normalised UVs; our UVs are already in
    // texels (caller multiplied by mipWidth), so v18 = 65536/area.
    float y0 = v[0].y, y1 = v[1].y, y2 = v[2].y;
    float x0 = v[0].x, x1 = v[1].x, x2 = v[2].x;
    float v47 = y0 - y1;
    float v46 = y2 - y1;
    float v44 = (x0 - x1) * v46 - (x2 - x1) * v47;
    if (v44 == 0.0f)                       // `if (0.0 == v17) goto LABEL_11`
        return 0;
    float v18 = kFixedScale / v44;
    // The original indexes the projected vertex UV as a flat float array
    // v49[0..5] = (u0,v0,u1,v1,u2,v2); the cross products are:
    //   dU = (v46*(u0-u1) - (u2-u1)*v47) * v18          -> dword_13FC5E4
    //   dV = v18 * ((v0-v1)*v46 - v47*(v2-v1))          -> dword_13FC598
    float u0 = v[0].u, u1 = v[1].u, u2 = v[2].u;
    float vv0 = v[0].v, vv1 = v[1].v, vv2 = v[2].v;
    float dU = (v46 * (u0 - u1) - (u2 - u1) * v47) * v18;    // dword_13FC5E4
    float dV = v18 * ((vv0 - vv1) * v46 - v47 * (vv2 - vv1)); // dword_13FC598
    rs.uGrad = ConvertChop(dU);
    rs.vGrad = ConvertChop(dV);

    // wave-7 W7-FOGPIX: the horizontal dF/dx for the per-vertex FOG FACTOR channel
    // — the SAME screen-space cross product as dU/dx, on the per-vertex factor
    // bytes (argument order, matching how the binary reads the projected vertex
    // attributes). D3D fixed-function vertex fog (FOGTABLEMODE never set, see
    // BeginScene @0x5e010c) interpolates the factor LINEARLY across the triangle
    // exactly like a colour/UV attribute, so the gradient is computed identically.
    // The factor is held in 16.16 (vf[i] = factor<<16); the gradient is scaled by
    // the same v18 (65536/area) so dF/dx is already 16.16 per pixel.
    float f0 = (float)v[0].fogFactor, f1 = (float)v[1].fogFactor,
          f2 = (float)v[2].fogFactor;
    float dF = (v46 * (f0 - f1) - (f2 - f1) * v47) * v18;
    rs.fGrad = ConvertChop(dF);
    // Gate the per-pixel channel on the live fog enable (byte_649DD8). When fog is
    // off the textured spans run with no fog accumulation (byte-identical to the
    // pre-fog raster).
    rs.fogPerPixel = SpanFog().enabled;

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

    // BuildSpanTexParams models BindActive + PatchSpanConstantsTextured: the
    // per-pixel U/V steps are the horizontal gradients (the original packed
    // them into one combined texel-address step; we keep them separate —
    // identical addressing, see the equivalence note in raster.cpp), and the
    // palette light row is dword_13FC5E0 (0x5f70bd):
    //   lightRow8 = ((l0 + l1 + l2) / 3) << 8  of the three vertex +66 bytes
    // (argument order; the unsigned-byte sum and the /3 are integer ops).
    const u32 lightRow8 =
        (u32)(((u32)v[0].light + (u32)v[1].light + (u32)v[2].light) / 3) << 8;
    SpanTexParams p = BuildSpanTexParams(tex, palette, rs.uGrad, rs.vGrad,
                                         lightRow8);
    // wave-5 W5-CKEY: when a resolved colour key is supplied (>=0) the masked
    // span keys on the resolved 16bpp value (the DDraw KEYSRC on pal[0]==black),
    // not on source index 0. colorKey565 < 0 keeps the original index-0 rule.
    if (colorKey565 >= 0) {
        p.useColorKey = true;
        p.colorKey565 = (u16)colorKey565;
    }

    // --- top sub-triangle ---------------------------------------------------
    int drew = 0;
    i32 longDy = rs.vy[v20] - rs.vy[v7];
    if (longDy > 0) {
        InterpolateEdgeRgbz(rs, v7, v20);         // long left edge (X+U+V)
        i32 shortDy = rs.vy[v21] - rs.vy[v19];
        if (shortDy > 0) {
            InterpolateEdgeZ(rs, v19, v21);       // short right edge (X only)
            int yTopRow = (rs.vy[v7] + 0xFFFF) >> 16;   // ceil(topY)

            i32 yMidR = rs.vy[v20], yBot = rs.vy[v21];
            bool rightShorter = yMidR < yBot;     // v41
            i32 yEnd = rightShorter ? (yMidR + 0xFFFF) : (yBot + 0xFFFF);
            int rc1 = (yEnd >> 16) - yTopRow;     // v46
            clampRows(yTopRow, rc1);              // surface clip (recon-only)
            rs.fbRow0 = (u16*)fb->pixels + (i64)yTopRow * rs.fbPitchPx;
            if (rc1 > 0) { FillSpanLoopWith(rs, rc1, p, masked); drew = 1; }

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
                int rc2 = ((hi + 0xFFFF) >> 16) - ((lo + 0xFFFF) >> 16);
                clampRows(y1, rc2);               // surface clip (recon-only)
                rs.fbRow0 = (u16*)fb->pixels + (i64)y1 * rs.fbPitchPx;
                if (rc2 > 0) { FillSpanLoopWith(rs, rc2, p, masked); drew = 1; }
            }
        }
    }

    return drew;
}

// gilde.exe 0x5F6C30 — the plain-span pipeline (PatchSpanConstantsTextured
// @0x5F7500 state).
int RasterizeTexturedTriangleRgbz(Surface* fb, const RgbzVertex v[3],
                                  const Texture& tex, const u16* palette,
                                  u8 polyFlags38) {
    return RasterizeTexturedTriangleRgbzWith(fb, v, tex, palette,
                                             /*masked=*/false, polyFlags38,
                                             /*colorKey565=*/-1);
}

// Colour-key variant — the same pipeline with the masked span body patched in
// (PatchSpanConstantsMasked @0x5f753f -> FillSpanTexturedMasked @0x5F721A;
// runtime selector UNRECOVERED — see the header note).
int RasterizeTexturedTriangleRgbzMasked(Surface* fb, const RgbzVertex v[3],
                                        const Texture& tex, const u16* palette,
                                        u8 polyFlags38, i32 colorKey565) {
    return RasterizeTexturedTriangleRgbzWith(fb, v, tex, palette,
                                             /*masked=*/true, polyFlags38,
                                             colorKey565);
}

} // namespace guild::render

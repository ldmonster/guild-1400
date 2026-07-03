// gilde.exe 0x5e1278 — VIBE_Particle_RenderSystem (inner projection math)
//
// 1:1 reconstruction of the genuine billboard transform / fade / projection /
// cull arithmetic of the particle render routine. See fx_recon3_particle_render.h
// for the scope / rule-3 / rule-8 notes (GPU draw-list append and the scene-graph
// + math-helper leaves are surfaced as inert hooks, not faked).
#include "render/fx_recon3_particle_render.h"

#include "render/types.h"            // Surface, ColorFormat
#include "render/texture.h"          // Texture
#include "render/raster.h"           // RasterState, SpanTexParams
#include "render/raster_textured.h"  // RgbzRasterState, Interpolate*, RgbzVertex
#include "render/raster_blend.h"     // SpanBlendParams, FillSpanTextured{Blend,Or}

#include <cmath>
#include <cstring>

namespace guild::render::fxrecon3 {

// ---------------------------------------------------------------------------
// Inert hook defaults.
// ---------------------------------------------------------------------------
namespace {

i32 inert_free_object_node(void*, void*) { return 0; }

void inert_compute_bone_world(const void*, const void*, int, Mat4* out) {
    // Identity (the original writes a real bone->world; inert keeps coords as-is).
    for (int i = 0; i < 16; ++i) out->m[i] = 0.0f;
    out->m[0] = out->m[5] = out->m[10] = out->m[15] = 1.0f;
}

void inert_coord_convert_x() { /* FPU rounding side effect — no-op headless */ }

void* inert_find_group_member(void* group, int /*sub*/) { return group; }

} // namespace

void install_default_render_hooks(RenderHooks& h) {
    if (!h.freeObjectNode)   h.freeObjectNode   = inert_free_object_node;
    if (!h.computeBoneWorld) h.computeBoneWorld = inert_compute_bone_world;
    if (!h.coordConvertX)    h.coordConvertX    = inert_coord_convert_x;
    if (!h.findGroupMember)  h.findGroupMember  = inert_find_group_member;
}

// ---------------------------------------------------------------------------
// project_slot — verbatim translation of the inner loop body
// (gilde.exe 0x5e13ac .. 0x5e1ad8). Float op-order preserved exactly.
// ---------------------------------------------------------------------------
SlotProjection project_slot(const ParticleSlot& s, const Mat4& m,
                            const ProjState& ps, bool colorReplaced) {
    SlotProjection r{};
    r.visible = false;

    // Slot must be active: (flags & 1) && alpha != 0   (decompile 0x5e1395)
    if (!((s.flags() & 1) != 0 && s.alpha() != 0)) {
        return r;
    }

    const f32 cx = s.cx(), cy = s.cy(), cz = s.cz();

    // v121 = size * 0.5
    const f32 v121 = s.size() * kHalf;

    // Eye-space transform. The original computes all three MAC chains in x87
    // 80-bit and stores each to a 32-bit float (fstp dword at 0x5e1423/25/28):
    //   X (v85=*[edx])   = cx*m0 + cy*m4 + cz*m8  + m12   -> v95
    //   Z (v87=*[edx+8]) = cx*m2 + cy*m6 + cz*m10 + m14   -> v97
    //   Y (v86=*[edx+4]) = cx*m1 + cy*m5 + cz*m9  + m13   -> v96
    // All three are stored as float and reloaded for distSq, so each is computed
    // and rounded identically (no double-vs-float asymmetry).
    const f32 v95 = cx * m.m[0] + cy * m.m[4] + cz * m.m[8]  + m.m[12]; // X
    const f32 v96 = cx * m.m[1] + cy * m.m[5] + cz * m.m[9]  + m.m[13]; // Y
    const f32 v97 = cx * m.m[2] + cy * m.m[6] + cz * m.m[10] + m.m[14]; // Z

    // v10 = v86*v86 + v85*v85 + v87*v87 (distSq), computed by reloading the three
    // stored floats (0x5e1431..0x5e1445), flt_13FC548 = v10.
    const f64 v10 = (f64)v96 * v96 + (f64)v95 * v95 + (f64)v97 * v97;
    r.distSq = (f32)v10;

    // v118 = 1.0 / v97
    const f32 v118 = 1.0f / v97;

    // Distance fade -> v93 in [0..255].
    f64 v93;
    if (v10 <= (f64)ps.minDistSq) {
        v93 = kDbl255;                                  // 255.0
    } else {
        const f64 v91 = (std::sqrt((f64)r.distSq) - ps.fadeBias) * ps.fadeScale;
        const f64 v92 = (kDbl255 >= v91) ? v91 : kDbl255; // dbl_62BA8C==255
        v93 = kDbl255 - v92;                             // dbl_62BA8C - v92
    }
    // VIBE_Coord_ConvertX(); // FPU rounding side effect (hooked at call site)
    const i32 v127 = (i32)v93;   // 0x5e14ca
    const u8  v129 = (u8)((i32)v93); // 0x5e14de (alpha byte)
    r.fadeAlpha = v127;

    // (min/max Z trackers flt_13FD168 / flt_13FCF3C are global running bounds;
    //  not part of the per-slot result — omitted here, see header note.)

    // Screen half extents and projected center.
    const f32 v115 = ps.sx * v121 * v118;            // halfW  (flt_13FCD0C)
    const f32 v117 = v118 * (v121 * -ps.sy);         // halfH  (-flt_13FCAF8)
    const f32 v116 = ps.sx * v95 * v118 + ps.ox;     // cxScr  (flt_13FCD0C * .. + flt_13FCD18)
    const f32 v114 = ps.sy * v96 * v118 + ps.oy;     // cyScr  (flt_13FCAF8 * .. + flt_13FCD10)

    r.ex = v95; r.ey = v96; r.ez = v97; r.invZ = v118;
    r.halfW = v115; r.halfH = v117; r.cxScr = v116; r.cyScr = v114;

    // Cull (0x5e1c56). Note original compares int scissor promoted to double.
    bool vis = true;
    if ((f64)ps.scTop  <= (f64)(v116 - v115)
     || (f64)ps.scLeft >  (f64)(v116 + v115)
     || (f64)ps.scBottom <= (f64)(v114 - v117)
     || (f64)ps.scRight  >  (f64)(v114 + v117)
     || (f64)v97 >= (f64)ps.zFar
     || (f64)v97 <  (f64)ps.zNear) {
        vis = false;
    }
    r.visible = vis;
    if (!vis) return r;

    // ---- color resolution (0x5e1696 branch) ----
    u8 cr, cg, cb;
    if (colorReplaced) {
        // alpha-modulated: chan = (alpha * chan) >> 8  with alpha = slot.alpha (+79)
        const u32 a = s.alpha();
        cb = (u8)(((u16)(a * s.colB())) >> 8);   // BYTE2 of v113
        cg = (u8)(((u16)(a * s.colG())) >> 8);   // BYTE1
        cr = (u8)(((u16)(a * s.colR())) >> 8);   // LOBYTE
    } else {
        cr = s.colR(); cg = s.colG(); cb = s.colB();
    }
    r.colR = cr; r.colG = cg; r.colB = cb;
    r.colA = v129;   // the fade alpha byte written to the rasterizer color (+319 etc.)

    // ---- quad corners (eye-space, before viewport) ----
    // The decompile writes two halves; corner positions in screen-aligned space:
    //   corner0 (a2+0)   = (v95 - v121, v96 + v121)   "TL" pre-project x/y
    //   corner1 (v19+0)  = (v95 + v121, v96 - v121)   "BR"
    //   corner2 (a2+160) = (v95 - v121, v96 - v121)
    //   corner3 (a2+240) = (v95 + v121, v96 + v121)
    r.qx[0] = v95 - v121; r.qy[0] = v96 + v121;
    r.qx[1] = v95 + v121; r.qy[1] = v96 - v121;
    r.qx[2] = v95 - v121; r.qy[2] = v96 - v121;
    r.qx[3] = v95 + v121; r.qy[3] = v96 + v121;

    // ---- SCREEN-space billboard corners (the geometry actually rasterized) ----
    // Exactly the four poly verts the original writes (0x5e179b..0x5e1ad8):
    //   left  X = v103 - v102 = cxScr - halfW      right X = v103 + v102
    //   top   Y = v101 - v104 = cyScr - halfH      bottom Y = v101 + v104
    // Vert order matches the engine's two 80-byte halves:
    //   sv0 (a2+16/20)   = (left ,top )
    //   sv1 (v14+16/20)  = (right,bottom)
    //   sv2 (a2+176/180) = (left ,bottom)
    //   sv3 (a2+256/260) = (right,top )
    const f32 left   = v116 - v115;   // v99
    const f32 right  = v116 + v115;   // v97
    const f32 top    = v114 - v117;   // v98
    const f32 bottom = v114 + v117;   // v94
    r.sxc[0] = left;  r.syc[0] = top;
    r.sxc[1] = right; r.syc[1] = bottom;
    r.sxc[2] = left;  r.syc[2] = bottom;
    r.sxc[3] = right; r.syc[3] = top;

    return r;
}

// ===========================================================================
// render_system_to_surface — the visible-output completion of
// VIBE_Particle_RenderSystem (0x5e1278). RenderSystem builds the screen-space
// billboard quad for each visible slot and appends two textured triangles to the
// engine display list (the &dword_1408100 / &dword_1408118 poly draw vtbl); that
// list is rasterized by the SAME translucent textured-triangle leaf the rest of
// the scene uses. Here we close the loop: project each slot (project_slot, 1:1),
// then drive that translucent textured-triangle rasterization into the 16bpp
// software Surface with the blend mode the particle poly node carries.
//
// The poly draw leaf is the affine textured triangle (raster_textured.cpp's
// edge interpolators + BuildSpanTexParams), with the OPAQUE inner span body
// swapped for the translucent one (raster_blend.cpp):
//   kBlendAlpha -> FillSpanTexturedBlend  (0x5F728A, 50/50)
//   kBlendAdd   -> FillSpanTexturedOr     (0x5F739C, additive OR)
// We reuse the public edge/gradient machinery (InterpolateEdgeRgbz/EdgeZ,
// kFixedScale via the same chop) and feed the blend span — i.e. the identical
// pipeline as RasterizeTexturedTriangleRgbz, only the per-pixel combine differs,
// which is exactly the difference between the two patched span bodies in the
// binary.
// ===========================================================================
namespace {

using guild::render::Surface;
using guild::render::Texture;
using guild::render::RasterState;
using guild::render::RgbzRasterState;
using guild::render::RgbzVertex;
using guild::render::SpanBlendParams;
using guild::render::ColorFormat;

// flt_62C3D4 == 65536.0f, the 16.16 scale (matches raster_textured.cpp).
constexpr float kFixedScale = 65536.0f;
inline i32 ConvertChop(double x) { return (i32)std::trunc(x); }

// The CCW neighbour table (dword_5AC540/5AC544), as in raster_textured.cpp.
constexpr int kNext[3] = {1, 2, 0};
constexpr int kPrev[3] = {2, 0, 1};

// Per-field LSB-clear blend mask (the patched word_1234567 immediate). Derived
// from the surface ColorFormat: clear bit0 of each of the R/G/B fields so the
// >>1 average cannot carry between channels. RGB565 -> 0xF7DE, RGB555 -> 0x7BDE.
u16 BlendMaskFromFormat(const ColorFormat& f) {
    // The engine's blend mask (word_1406944, built by VIBE_Shape_InitColorMasks
    // @0x5d4ad4): ((1 << (7 - prec)) - 1) << pos per channel — each field KEEPS
    // its lower bits and drops its TOP bit, because the span applies the mask
    // AFTER the >>1 (the foreign bit from the field above lands on the top).
    // 565 -> 0x7BEF (matches the live word_1406944 read). The previous ~LSB
    // form (0xF7DE) was the PRE-shift mask — wrong for this span order (channel
    // bleed on blended foliage).
    const u32 keep = (((1u << (7 - f.rPrec)) - 1u) << f.rPos) |
                     (((1u << (7 - f.gPrec)) - 1u) << f.gPos) |
                     (((1u << (7 - f.bPrec)) - 1u) << f.bPos);
    return (u16)(keep & 0xFFFFu);
}

} // namespace

// One particle billboard triangle, blended. Mirrors the body of
// RasterizeTexturedTriangleRgbzWith but drives the blend/OR span. polyFlags38=0
// for particles (billboards are always front-wound), so no winding reversal.
// PUBLIC (declared in fx_recon3_particle_render.h): the city foliage alpha
// route reuses it with masked=true.
int RasterizeBlendTriangle(Surface* fb, const RgbzVertex v[3], const Texture& tex,
                           const u16* palette, ParticleBlend blend, bool masked) {
    RgbzRasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    rs.fbPitchPx = fb->widthPx;
    rs.clipX0 = fb->clipX0;
    rs.clipX1 = (fb->clipX1 > fb->clipX0) ? fb->clipX1 : fb->width;
    const i32 clipY0v = (fb->clipY1 > fb->clipY0) ? fb->clipY0 : 0;
    const i32 clipY1v = (fb->clipY1 > fb->clipY0) ? fb->clipY1 : fb->height;

    // Load vertices to 16.16, find top (min-Y) — forward winding (no reversal).
    int top = -1;
    i32 minY = 0x7FFFFFFF;
    for (int i = 0; i < 3; ++i) {
        rs.vu[i] = ConvertChop((double)v[i].u * kFixedScale);
        rs.vv[i] = ConvertChop((double)v[i].v * kFixedScale);
        rs.vx[i] = ConvertChop((double)v[i].x * kFixedScale);
        rs.vy[i] = ConvertChop((double)v[i].y * kFixedScale);
        if (minY >= rs.vy[i]) { top = i; minY = rs.vy[i]; }
    }
    if (top < 0) return 0;

    // Horizontal dU/dx & dV/dx gradients (UV cross products), same as the
    // textured path.
    float y0 = v[0].y, y1 = v[1].y, y2 = v[2].y;
    float x0 = v[0].x, x1 = v[1].x, x2 = v[2].x;
    float gy01 = y0 - y1;
    float gy21 = y2 - y1;
    float area = (x0 - x1) * gy21 - (x2 - x1) * gy01;
    if (area == 0.0f) return 0;
    float inv = kFixedScale / area;
    float u0 = v[0].u, u1 = v[1].u, u2 = v[2].u;
    float w0 = v[0].v, w1 = v[1].v, w2 = v[2].v;
    rs.uGrad = ConvertChop((gy21 * (u0 - u1) - (u2 - u1) * gy01) * inv);
    rs.vGrad = ConvertChop(inv * ((w0 - w1) * gy21 - gy01 * (w2 - w1)));

    int n = kNext[top], pr = kPrev[top];
    i32 yTop = rs.vy[top];
    int v7, v19, v20, v21;
    if (yTop == rs.vy[n]) { v7 = top; v19 = n; v20 = pr; v21 = kNext[n]; }
    else if (yTop == rs.vy[pr]) { v7 = pr; v19 = top; v21 = n; v20 = kPrev[pr]; }
    else { v7 = top; v19 = top; v20 = pr; v21 = n; }

    // Blend span params — the texel-fetch fields are identical to SpanTexParams;
    // particles don't use the palette light row (it is folded into the per-vertex
    // colour, not a span constant), so the LUT is the plain bound palette.
    SpanBlendParams bp{};
    bp.texBase    = tex.texels.data();
    bp.palBase    = palette;
    bp.texelMask  = tex.texelMask;
    bp.widthShift = tex.widthShift;
    bp.uStepFrac  = rs.uGrad;
    bp.vStep      = rs.vGrad;
    bp.blendMask  = BlendMaskFromFormat(fb->fmt);

    auto clampRows = [&](int& startRow, int& rc) {
        if (startRow < clipY0v) {
            i32 skip = clipY0v - startRow;
            if (skip > rc) skip = rc;
            rs.xLeft += rs.xLeftStep * skip; rs.uLeft += rs.uLeftStep * skip;
            rs.vLeft += rs.vLeftStep * skip; rs.xRight += rs.xRightStep * skip;
            startRow += skip; rc -= skip;
        }
        if (startRow + rc > clipY1v) rc = (int)(clipY1v - startRow);
        if (rc < 0) rc = 0;
    };

    // The per-scanline span driver (FillSpanLoop body) with the blend inner span.
    auto fillLoop = [&](int rowCount) {
        u16* row = rs.fbRow0;
        const bool clip = rs.clipX1 > rs.clipX0;
        RasterState span{};
        for (int i = 0; i < rowCount; ++i) {
            i32 xL = (rs.xLeft + 0xFFFF) >> 16;
            i32 xR = (rs.xRight + 0xFFFF) >> 16;
            if (clip) { if (xL < rs.clipX0) xL = rs.clipX0; if (xR > rs.clipX1) xR = rs.clipX1; }
            rs.spanLen = xR - xL;
            if (rs.spanLen > 0) {
                i32 sub = (xL << 16) - rs.xLeft;
                i32 u = (i32)(((i64)rs.uGrad * (i64)sub) >> 16) + rs.uLeft;
                i32 vv = (i32)(((i64)rs.vGrad * (i64)sub) >> 16) + rs.vLeft;
                span.spanLen = rs.spanLen;
                if (blend == kBlendAdd) {
                    if (masked)
                        guild::render::FillSpanTexturedOrMasked(span, row + xL, u, vv, bp);
                    else
                        guild::render::FillSpanTexturedOr(span, row + xL, u, vv, bp);
                } else {
                    if (masked)
                        guild::render::FillSpanTexturedBlendMasked(span, row + xL, u, vv, bp);
                    else
                        guild::render::FillSpanTexturedBlend(span, row + xL, u, vv, bp);
                }
            }
            rs.xLeft += rs.xLeftStep; rs.uLeft += rs.uLeftStep;
            rs.vLeft += rs.vLeftStep; rs.xRight += rs.xRightStep;
            row += rs.fbPitchPx;
        }
    };

    int drew = 0;
    i32 longDy = rs.vy[v20] - rs.vy[v7];
    if (longDy > 0) {
        InterpolateEdgeRgbz(rs, v7, v20);
        i32 shortDy = rs.vy[v21] - rs.vy[v19];
        if (shortDy > 0) {
            InterpolateEdgeZ(rs, v19, v21);
            int yTopRow = (rs.vy[v7] + 0xFFFF) >> 16;
            i32 yMidR = rs.vy[v20], yBot = rs.vy[v21];
            bool rightShorter = yMidR < yBot;
            i32 yEnd = rightShorter ? (yMidR + 0xFFFF) : (yBot + 0xFFFF);
            int rc1 = (yEnd >> 16) - yTopRow;
            clampRows(yTopRow, rc1);
            rs.fbRow0 = (u16*)fb->pixels + (i64)yTopRow * rs.fbPitchPx;
            if (rc1 > 0) { fillLoop(rc1); drew = 1; }
            if (rs.vy[v20] != rs.vy[v21]) {
                int y1r = (yEnd >> 16);
                i32 hi, lo;
                if (rightShorter) { InterpolateEdgeRgbz(rs, v20, v21); hi = rs.vy[v21]; lo = rs.vy[v20]; }
                else { InterpolateEdgeZ(rs, v21, v20); hi = rs.vy[v20]; lo = rs.vy[v21]; }
                int rc2 = ((hi + 0xFFFF) >> 16) - ((lo + 0xFFFF) >> 16);
                clampRows(y1r, rc2);
                rs.fbRow0 = (u16*)fb->pixels + (i64)y1r * rs.fbPitchPx;
                if (rc2 > 0) { fillLoop(rc2); drew = 1; }
            }
        }
    }
    return drew;
}

int render_system_to_surface(Surface* fb, const ParticleSystemView& sys,
                             const Mat4& world, const ProjState& ps,
                             ParticleBlend blend, bool colorReplaced) {
    if (!fb || !fb->pixels || !sys.slots || sys.slotCount <= 0 ||
        !sys.defTex || !sys.palette) {
        return 0;
    }
    const Texture& tex = *sys.defTex;
    const u16* pal = sys.palette;

    int drawn = 0;
    // Walk every slot (a1+208 count), 84-byte stride (the slots array view).
    for (int i = 0; i < sys.slotCount; ++i) {
        const ParticleSlot& slot = sys.slots[i];
        SlotProjection pj = project_slot(slot, world, ps, colorReplaced);
        if (!pj.visible) continue;

        // Build the two billboard triangles covering the axis-aligned screen
        // quad. The projected corners give a left/right X and two Y values
        // (cyScr +/- halfH, halfH can be negative); take the geometric extents so
        // the two triangles are always front-wound the way the engine's textured-
        // triangle leaf expects (top vertex apex, long edge on the left — the same
        // winding the live RasterizeTexturedTriangleRgbz golden uses). The whole
        // sprite texture maps onto the quad: U follows screen X (left=0,right=1),
        // V follows screen Y (top=0,bottom=1). Texel-space UVs (the rasterizer
        // applies the *65536 step), so 0..mipWidth.
        const float W = (float)(tex.mipWidth > 0 ? tex.mipWidth : 1);
        const float xl = pj.cxScr - pj.halfW;          // left X
        const float xr = pj.cxScr + pj.halfW;          // right X
        float ya = pj.cyScr - pj.halfH;
        float yb = pj.cyScr + pj.halfH;
        const float yt = ya < yb ? ya : yb;            // top (min Y)
        const float ybo = ya < yb ? yb : ya;           // bottom (max Y)
        const RgbzVertex tl{xl, yt, 0.0f,    0.0f,    0};
        const RgbzVertex tr{xr, yt, 1.0f * W, 0.0f,    0};
        const RgbzVertex bl{xl, ybo, 0.0f,    1.0f * W, 0};
        const RgbzVertex br{xr, ybo, 1.0f * W, 1.0f * W, 0};
        // Same winding family as the working golden (TL -> TR -> BL is CW in the
        // y-down screen): triangle A = (TL,TR,BL), triangle B = (TR,BR,BL).
        const RgbzVertex t0[3] = {tl, tr, bl};
        const RgbzVertex t1[3] = {tr, br, bl};
        int a = RasterizeBlendTriangle(fb, t0, tex, pal, blend);
        int b = RasterizeBlendTriangle(fb, t1, tex, pal, blend);
        if (a || b) ++drawn;
    }
    return drawn;
}

} // namespace guild::render::fxrecon3

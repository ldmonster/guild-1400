#include "render/render_leaves6.h"

#include <cstring>

// Reused reconstructed sibling: the x87 round-toward-zero chop behind
// VIBE_Coord_ConvertX (0x5c6b08), defined in render/particle.cpp. DrawLineClipped
// truncates each clipped endpoint through it (the original calls ConvertX then
// reads the integer-part of the FP register). Extern-declared, NOT redefined.
namespace guild::render {
int TruncToward(double);   // particle.cpp
} // namespace guild::render

namespace guild::render {
namespace {

// ----- inert default hook implementations (defined IN the library) ----------
i32 DefaultDrawSpan(i32 x0, i32 /*y0*/, i32 /*x1*/, i32 /*y1*/, i16 /*color*/) {
    return x0;   // original returns a pixel index; inert returns x0.
}
void DefaultAssignMeshData(void* /*obj*/) {}
void DefaultTransformPivot(void* /*obj*/, const float* src, float* dst) {
    dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];   // identity
}
void* DefaultFindByHandle(const char* /*name*/) { return nullptr; }
i32 DefaultCreateObjectAnim(void* /*cam*/, i32 /*firstFrame*/,
                            const i32* /*desc*/, i32 /*count*/, i32 /*stride*/) {
    return 0;
}
void DefaultTransformChain(void* /*obj*/, const void* /*src*/, float* dst) {
    dst[0] = 0.0f; dst[1] = 0.0f; dst[2] = 0.0f;
}
i32 DefaultSurfaceFill(void* /*surface*/, const i32* /*destRect*/, i32 /*color*/) {
    return 0;   // success
}

RenderLeaves6Hooks g_hooks = {
    &DefaultDrawSpan, &DefaultAssignMeshData, &DefaultTransformPivot,
    &DefaultFindByHandle, &DefaultCreateObjectAnim, &DefaultTransformChain,
    &DefaultSurfaceFill,
};

} // namespace

void InstallRenderLeaves6Hooks(const RenderLeaves6Hooks& h) {
    g_hooks.drawSpan         = h.drawSpan         ? h.drawSpan         : &DefaultDrawSpan;
    g_hooks.assignMeshData   = h.assignMeshData   ? h.assignMeshData   : &DefaultAssignMeshData;
    g_hooks.transformPivot   = h.transformPivot   ? h.transformPivot   : &DefaultTransformPivot;
    g_hooks.findByHandle     = h.findByHandle     ? h.findByHandle     : &DefaultFindByHandle;
    g_hooks.createObjectAnim = h.createObjectAnim ? h.createObjectAnim : &DefaultCreateObjectAnim;
    g_hooks.transformChain   = h.transformChain   ? h.transformChain   : &DefaultTransformChain;
    g_hooks.surfaceFill      = h.surfaceFill      ? h.surfaceFill      : &DefaultSurfaceFill;
}

const RenderLeaves6Hooks& CurrentRenderLeaves6Hooks() { return g_hooks; }

// ===========================================================================
// 0x435434 — VIBE_Render_DrawLineClipped
//
// Original args (register): a1@eax=x0, a2@edx=y0, a3@ecx=x1, a4@ebx=y1, a5=color.
// The bounds compared are dword_75FB40 (b40), _44 (b44), _48 (b48), _4C (b4C).
// Locals: v18=x0, v17=x1, v16=y1; v19=y1-x0... — the original mixes them, but the
// arithmetic is exactly: slopes v14 = (y1-x0_local)/(x1-y0_local) and its inverse
// v13, then a chain of single-edge clips that each interpolate the *other* coord.
// We reproduce the control flow and arithmetic verbatim (variable names kept to
// match the decompile: v18,a2,v17,v16 are the running endpoints).
// ===========================================================================
i32 DrawLineClipped(const ClipBounds& clip, i32 x0, i32 y0, i32 x1, i32 y1,
                    i16 color) {
    // The decompile aliases: v18=a1(x0), v16=a4(y1), v17=a3(x1); a2 stays y0.
    i32 v18 = x0;
    i32 v16 = y1;
    i32 v17 = x1;
    i32 a1  = x0;          // also the reject return value (result = a1 initially)
    i32 a2  = y0;

    const i32 b40 = clip.b40, b44 = clip.b44, b48 = clip.b48, b4C = clip.b4C;

    // v19 = y1 - x0 ; result0 = x1 - y0 ; slopes.
    const double v19 = static_cast<double>(y1 - x0);
    const double r0  = static_cast<double>(x1 - y0);
    const double v14 = v19 / r0;   // 0x43546d
    const double v13 = r0 / v19;   // 0x435479

    i32 result = a1;

    // Trivial-reject chain (0x435485..0x4354cd): the segment must straddle each
    // bound. Each guard's else-branch returns the running `result`.
    if (x0 >= b40 || b40 <= y1) {            // 0x435485 (a1 vs b40 ; b40 vs a4)
        result = v18;
        if (v18 <= b48 || b48 >= y1) {       // 0x43549d
            result = a2;
            if (y0 >= b44 || b44 <= x1) {    // 0x4354b5
                result = a2;
                if (y0 <= b4C || b4C >= x1) {// 0x4354cd
                    // ----- clip endpoint #1: x against [b40,b48] (0x4354df) -----
                    if (v18 >= b40) {
                        if (v18 > b48) {     // 0x435603 false-branch => clip hi
                            double v9 = static_cast<double>(a2)
                                      - static_cast<double>(v18 - b48) * v13; // 0x435623
                            v18 = b48;
                            (void)TruncToward(v9);
                            a2 = static_cast<i32>(v9);
                        }
                        // else: v18 in-range, fall through to LABEL_12
                    } else {
                        double v9 = static_cast<double>(b40 - v18) * v13
                                  + static_cast<double>(a2);               // 0x435505
                        v18 = b40;
                        (void)TruncToward(v9);
                        a2 = static_cast<i32>(v9);
                    }
                    // ----- LABEL_12: clip y against [b44,b4C] (0x435521) -----
                    if (a2 >= b44) {
                        if (a2 > b4C) {       // 0x435636 false => clip hi
                            double v10 = static_cast<double>(v18)
                                       - static_cast<double>(a2 - b4C) * v14; // 0x435656
                            a2 = b4C;
                            (void)TruncToward(v10);
                            v18 = static_cast<i32>(v10);
                        }
                    } else {
                        double v10 = static_cast<double>(b44 - a2) * v14
                                   + static_cast<double>(v18);               // 0x435547
                        a2 = b44;
                        (void)TruncToward(v10);
                        v18 = static_cast<i32>(v10);
                    }
                    // ----- LABEL_15: clip x1 against [b40,b48] (0x435562) -----
                    if (y1 >= b40) {
                        if (y1 > b48) {       // 0x435669 false => clip hi
                            double v11 = static_cast<double>(v17)
                                       - static_cast<double>(y1 - b48) * v13; // 0x435689
                            v16 = b48;
                            (void)TruncToward(v11);
                            v17 = static_cast<i32>(v11);
                        }
                    } else {
                        double v11 = static_cast<double>(b40 - y1) * v13
                                   + static_cast<double>(v17);               // 0x435588
                        v16 = b40;
                        (void)TruncToward(v11);
                        v17 = static_cast<i32>(v11);
                    }
                    // ----- LABEL_18: clip y1 against [b44,b4C] (0x4355a3) -----
                    if (v17 >= b44) {
                        if (v17 > b4C) {      // 0x43569c false => clip hi
                            double v12 = static_cast<double>(v16)
                                       - static_cast<double>(v17 - b4C) * v14; // 0x4356bc
                            v17 = b4C;
                            (void)TruncToward(v12);
                            v16 = static_cast<i32>(v12);
                            return g_hooks.drawSpan(v18, a2, v17, v16, color); // 0x4355ed
                        }
                        // both in-range -> draw (0x43569c)
                        return g_hooks.drawSpan(v18, a2, v17, v16, color);
                    } else {
                        double v12 = static_cast<double>(b44 - v17) * v14
                                   + static_cast<double>(v16);               // 0x4355c9
                        v17 = b44;
                        (void)TruncToward(v12);
                        v16 = static_cast<i32>(v12);
                        return g_hooks.drawSpan(v18, a2, v17, v16, color);   // 0x4355ed
                    }
                }
            }
        }
    }
    return result;   // 0x4355f3
}

// ===========================================================================
// 0x426a30 — VIBE_Mesh_DrawBoundingBox
//
// Original: only runs when object class byte (+533) == 4; else returns 1.
// Walks resident submeshes (those with +4 LOD <= cutoff). The FIRST qualifying
// submesh seeds both min (v15..) and max (v22..) from its corner (+72). Each
// subsequent qualifying submesh refines per-axis min/max. Then 4 derived corners
// (min, {v22[0],min1,min2}, {v22[0],min1,v23}, {min0,min1,v23}) are transformed
// through the bone-chain pivot into outA..outD. Returns 0.
// ===========================================================================
i32 MeshDrawBoundingBox(void* obj, i32 objClass, float lodCutoff,
                        const SubmeshCorner* submeshes, i32 count,
                        float outA[3], float outB[3], float outC[3],
                        float outD[3]) {
    if (objClass != 4)   // 0x426a3d
        return 1;

    g_hooks.assignMeshData(obj);   // 0x426a51

    float vMin[3] = {0, 0, 0};   // v15,v16,v17
    float vMax[3] = {0, 0, 0};   // v22[0],v22[1],v23

    // First qualifying submesh seeds min==max==corner (0x426afc..426b2d).
    i32 i = 0;
    for (; i < count; ++i) {
        if (submeshes[i].lod <= static_cast<double>(lodCutoff)) {
            vMin[0] = submeshes[i].corner[0];
            vMin[1] = submeshes[i].corner[1];
            vMin[2] = submeshes[i].corner[2];
            vMax[0] = submeshes[i].corner[0];
            vMax[1] = submeshes[i].corner[1];
            vMax[2] = submeshes[i].corner[2];
            break;   // 0x426b31
        }
    }
    // Remaining qualifying submeshes refine per-axis (0x426a78 loop, body
    // iterates the 3 axes: min = min(min,corner); max = max(max,corner)).
    for (; i < count; ++i) {
        if (submeshes[i].lod <= static_cast<double>(lodCutoff)) {
            for (int k = 0; k < 3; ++k) {
                const float c = submeshes[i].corner[k];
                if (static_cast<double>(vMin[k]) >= c) vMin[k] = c; // 0x426a9a
                if (vMax[k] <= static_cast<double>(c)) vMax[k] = c; // 0x426abe
            }
        }
    }

    // Derive the 4 corners exactly as 0x426b45..426bd2 and transform each.
    float corner[3];
    // outA: (min0, min1, min2)
    corner[0] = vMin[0]; corner[1] = vMin[1]; corner[2] = vMin[2];
    g_hooks.transformPivot(obj, corner, outA);   // 0x426b63
    // outB: (max0, min1, min2)
    corner[0] = vMax[0]; corner[1] = vMin[1]; corner[2] = vMin[2];
    g_hooks.transformPivot(obj, corner, outB);   // 0x426b8a
    // outC: (max0, min1, max2)
    corner[0] = vMax[0]; corner[1] = vMin[1]; corner[2] = vMax[2];
    g_hooks.transformPivot(obj, corner, outC);   // 0x426bb1
    // outD: (min0, min1, max2)
    corner[0] = vMin[0]; corner[1] = vMin[1]; corner[2] = vMax[2];
    g_hooks.transformPivot(obj, corner, outD);   // 0x426bd8

    return 0;   // 0x426a44
}

// ===========================================================================
// 0x423c70 — VIBE_Surface_ColorFillRect
//
// Original args (register): a1@eax=x, a2@edx=y, a3@ecx=h, a4@ebx=w, a5=surface.
// Null surface -> 0. If +32 (vendor obj) set: clamp x to >= +36 (leftClampX),
// build dest rect [x, y, min(x+w, width), min(y+h, height)], and (when the
// origin is in-bounds) issue the vendor fill of that rect. Else (no vendor
// object): linear-fill path. The original also brackets the work with
// Decompression_Finalize / DecompressState_Blob (a lock/unlock dance) which has
// no effect on the deterministic rect math — omitted. Returns 1.
// ===========================================================================
i32 SurfaceColorFillRect(SurfaceView* surface, i32 x, i32 y, i32 h, i32 w,
                         i32 color, SurfaceFillRect* outRect) {
    if (!surface)            // 0x423c81
        return 0;

    if (surface->vendorObj) {    // 0x423c8a (true => vendor path)
        if (x < surface->leftClampX)   // 0x423cd0
            x = surface->leftClampX;
        i32 x0 = x;                    // v9[0]
        i32 y0 = y;                    // v9[1]
        i32 x1 = x + w;                // v10
        i32 y1 = y + h;                // v11
        if (surface->width - 1 < x + w)   // 0x423cf4
            x1 = surface->width;
        if (surface->height - 1 < y1)     // 0x423d07
            y1 = surface->height;
        if (x < surface->width && y < surface->height) {  // 0x423d18
            if (outRect) {
                outRect->x0 = x0; outRect->y0 = y0;
                outRect->x1 = x1; outRect->y1 = y1;
                outRect->issued = true;
            }
            i32 destRect[4] = {x0, y0, x1, y1};
            g_hooks.surfaceFill(surface->vendorObj, destRect, color);  // 0x423d4c
        } else if (outRect) {
            outRect->issued = false;
        }
    } else {
        // Linear-fill path (0x423c9c): clears width*height bytes. We record the
        // full-surface rect through the hook for observability.
        if (outRect) {
            outRect->x0 = 0; outRect->y0 = 0;
            outRect->x1 = surface->width; outRect->y1 = surface->height;
            outRect->issued = true;
        }
        i32 destRect[4] = {0, 0, surface->width, surface->height};
        g_hooks.surfaceFill(nullptr, destRect, color);
    }
    return 1;   // 0x423cb5
}

// ===========================================================================
// 0x428a84 — VIBE_Render_DrawTextLabels3D (camera-relative coordinate kernel).
//
// For each resolved+visible label the original computes (0x428c21..428c9e):
//   entry.a[axis] = obj.posA[axis] - cam.worldPos[axis]     (+92.. minus +76..)
//   entry.b[axis] = obj.posB[axis] - cam.trans[axis]        (+144.. minus +132..)
// Unresolved / invisible labels are skipped. Returns the written count.
// ===========================================================================
i32 ComputeLabelEntries(const CameraView& cam, const LabelObject* labels,
                        i32 count, LabelEntry* out, i32 cap) {
    i32 n = 0;
    for (i32 k = 0; k < count; ++k) {
        if (!labels[k].visible)       // +529 & 1 == 0 => skip (0x428c18)
            continue;
        if (n >= cap)
            break;
        for (int axis = 0; axis < 3; ++axis) {
            out[n].a[axis] = labels[k].posA[axis] - cam.worldPos[axis];
            out[n].b[axis] = labels[k].posB[axis] - cam.trans[axis];
        }
        ++n;
    }
    return n;
}

// ===========================================================================
// 0x428d30 — VIBE_Render_DrawObjectMarkers3D (camera-relative subtract).
//
// The original transforms the marker through the bone chain, then subtracts
// (0x428e35..428e72):
//   entry.a[axis] = worldPosA[axis] - cam.worldPos[axis]   (-= a1[19..21], +76..)
//   entry.b[axis] = worldPosB[axis] - cam.trans[axis]      (-= a1[33..35], +132..)
// ===========================================================================
LabelEntry ComputeMarkerEntry(const CameraView& cam, const float worldPosA[3],
                              const float worldPosB[3]) {
    LabelEntry e;
    for (int axis = 0; axis < 3; ++axis) {
        e.a[axis] = worldPosA[axis] - cam.worldPos[axis];
        e.b[axis] = worldPosB[axis] - cam.trans[axis];
    }
    return e;
}

} // namespace guild::render

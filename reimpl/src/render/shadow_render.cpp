#include "render/shadow_render.h"
#include <cstring>

// =============================================================================
// guild::render flat-shadow rasterizer — implementation. See shadow_render.h.
// All arithmetic is the original's 16.16 fixed point with the
// `(acc + 0xFFFF) >> 16` top-left ceil idiom.
// =============================================================================
namespace guild::render {

namespace {
// Truncate-toward-zero of a float (VIBE_Coord_ConvertX @0x5c6b08, chop mode).
inline int TruncToward(double x) { return (int)x; }

// 16.16 edge slope + start, shared by ComputeEdgeSlope / InterpolateEdgeZ.
// Returns the start X (the value the original stores into dword_13FC5D8 / 5BC).
inline int EdgeSetup(int py0, int py1, int px0, int px1, int& outSlope) {
    int dy = py1 - py0;            // dword_13FC59C[a2]-[a1]
    int slope;
    int dx = px1 - px0;
    if (dy >= 0x10000)
        // (dx << 16) / dy.  `(long long)dx << 16` is UB when dx<0; the x86 form
        // is an arithmetic value (`dx` scaled by 2^16), so multiply by 65536 to
        // get the identical result with no signed-shift UB.
        slope = (int)(((long long)dx * 65536) / dy);
    else
        slope = (int)((unsigned long long)((0x40000000 / dy) * (long long)dx) >> 14);
    outSlope = slope;
    // start = px0 + (slope * (ceil16(py0) - py0)) >> 16
    // `ceil16(py0)` is the x86 idiom `(py0 + 0xFFFF) & 0xFFFF0000`; the decompiler
    // rendered the mask as `>> 16 << 16`, whose `<< 16` is UB for negatives. The
    // mask form is bit-identical to the original instruction and UB-free.
    long long frac = (long long)(((py0 + 0xFFFF) & ~0xFFFF) - py0);
    int start = px0 + (int)((unsigned long long)((long long)slope * frac) >> 16);
    return start;
}
} // namespace

// ---------------------------------------------------------------------------
// gilde.exe 0x603d00 — VIBE_Raster_ComputeEdgeSlope (LEFT edge).
// ---------------------------------------------------------------------------
int ComputeEdgeSlope(ShadowRasterState& s, int a1, int a2) {
    int start = EdgeSetup(s.py[a1], s.py[a2], s.px[a1], s.px[a2], s.leftDxDy);
    s.leftX = start;   // dword_13FC5D8
    return start;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5f6a8c — VIBE_Raster_InterpolateEdgeZ (RIGHT edge).
// ---------------------------------------------------------------------------
int InterpolateEdgeZ(ShadowRasterState& s, int a1, int a2) {
    int start = EdgeSetup(s.py[a1], s.py[a2], s.px[a1], s.px[a2], s.rightDxDy);
    s.rightX = start;  // dword_13FC5BC
    return start;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x603da8 — VIBE_Raster_FillSpans. `count` scanlines.
//   each row: v4 = ceil16(leftX) ; spanLen = ceil16(rightX) - v4 ;
//             if spanLen > 0 fill [v4 .. v4+spanLen) ;
//             leftX += leftDxDy ; rightX = rightDxDy + rightX ;
//             dstRow += pitch.
// 8bpp uses memset(fillValue); 16bpp writes word fillValue.
// ---------------------------------------------------------------------------
void FillSpans(ShadowRasterState& s, const ShadowSurface& surf, int count) {
    int v1 = s.rightX;               // dword_13FC5BC
    // Memory-safety window for the destination surface (the original shadow
    // surface is square — width==height — so dstRow never leaves the buffer and
    // the span never exceeds the row; these guards are no-ops on that in-bounds
    // path and only fire when a caller hands a smaller-than-expected surface).
    // surf.height/width==0 means "unbounded" (legacy callers that don't fill them).
    const bool boundRows = surf.pixels && surf.height > 0;
    const u8* rowEnd =
        boundRows ? surf.pixels + (size_t)surf.pitch * surf.height : nullptr;
    const int rowW = surf.width;     // valid pixels/words per row (0 == unbounded)
    if (s.is8bpp) {
        for (int v3 = 0; v3 < count; ++v3) {
            int v4 = (s.leftX + 0xFFFF) >> 16;
            s.rightX = v1;
            s.spanLen = ((v1 + 0xFFFF) >> 16) - v4;
            if (s.spanLen > 0) {
                int x0 = v4, len = s.spanLen;
                if (rowW > 0) {                 // clamp the span into [0, width)
                    if (x0 < 0) { len += x0; x0 = 0; }
                    if (x0 + len > rowW) len = rowW - x0;
                }
                if (len > 0 && (!boundRows || (s.dstRow >= surf.pixels &&
                                               s.dstRow < rowEnd)))
                    std::memset(s.dstRow + x0, s.fillValue, (size_t)len);
            }
            s.leftX += s.leftDxDy;
            v1 = s.rightDxDy + s.rightX;
            s.dstRow += surf.pitch;  // dword_7626FC
        }
    } else {
        for (int v5 = 0; v5 < count; ++v5) {
            int v6 = (s.leftX + 0xFFFF) >> 16;
            int v7 = ((v1 + 0xFFFF) >> 16) - v6;
            s.rightX = v1;
            s.spanLen = v7;
            if (v7 > 0) {
                int x0 = v6, len = v7;
                if (rowW > 0) {                 // clamp the span into [0, width)
                    if (x0 < 0) { len += x0; x0 = 0; }
                    if (x0 + len > rowW) len = rowW - x0;
                }
                if (len > 0 && (!boundRows || (s.dstRow >= surf.pixels &&
                                               s.dstRow < rowEnd))) {
                    u16* p = (u16*)(s.dstRow + 2 * x0);
                    u16 word = (u16)s.fillValue;
                    while (len--) *p++ = word;
                }
            }
            v1 = s.rightDxDy + s.rightX;
            s.leftX += s.leftDxDy;
            s.dstRow += 2 * surf.width; // dword_13FC5D4 += 2*dword_7626F8
        }
    }
    s.rightX = v1;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x603ed4 — VIBE_Shadow_RasterizeTriangle. Faithful structure:
//   - signed-area test picks forward (CCW) or reverse (CW) vertex iteration;
//     the reverse branch only runs if (tri.backFlag) else returns.
//   - convert each vertex's float screen X,Y to 16.16, store px[]/py[], track
//     the minimum Y (v8) and its vertex index (a4 = top vertex).
//   - validation: every px>=0 and py in [0, clip]; otherwise bail.
//   - pick left/right walk vertices from kEdgeNext/kEdgePrev, set up edges,
//     fill the two sub-triangles.
// `clip` is the surface width as a 16.16 bound (a3 << 16 in the original).
// ---------------------------------------------------------------------------
void RasterizeTriangle(ShadowRasterState& s, const ShadowTri& tri,
                       ShadowSurface& surf) {
    const float SC = kScreenToFixed; // flt_62C6C0 = 65536.0
    int a3 = surf.width;             // clip width (a3<<16 used below)
    int v8 = a3 << 16;               // running min-Y, seeded to clip
    int a4 = 0;                      // top vertex index

    // Signed-area sign decides iteration order (winding). The original compares
    //   (x0-x2)*(y0-y1) > (x0-x1)*(y0-y2).
    double x0 = tri.x[0], x1 = tri.x[1], x2 = tri.x[2];
    double y0 = tri.y[0], y1 = tri.y[1], y2 = tri.y[2];
    bool reverse = (x0 - x2) * (y0 - y1) > (x0 - x1) * (y0 - y2);

    if (reverse) {
        if (!tri.backFlag)
            return;
        // iterate v18 = tri verts 2,1,0  (do { ... --v18 } while idx+1<3)
        for (int k = 0; k < 3; ++k) {
            int src = 2 - k;
            int xv = TruncToward((double)tri.x[src] * SC);
            int yv = TruncToward((double)tri.y[src] * SC);
            s.px[k] = xv;
            s.py[k] = yv;
            if (v8 >= yv) { a4 = k; v8 = yv; }
        }
    } else {
        // iterate v4 = tri verts 0,1,2
        for (int k = 0; k < 3; ++k) {
            int xv = TruncToward((double)tri.x[k] * SC);
            int yv = TruncToward((double)tri.y[k] * SC);
            s.px[k] = xv;
            s.py[k] = yv;
            if (v8 >= yv) { a4 = k; v8 = yv; }
        }
    }

    // Validation: px>=0 and py in [0, clip-1]  (v16 = (a3<<16)-1).
    int v16 = (a3 << 16) - 1;
    for (int v15 = 0; v15 < 3; ++v15) {
        if (s.px[v15] < 0) return;       // dword_13FC5B0[v15] >= 0 required
        if (v16 < s.px[v15]) return;
        int yv = s.py[v15];
        if (yv < 0 || v16 < yv) return;
    }

    // Winding / apex resolution (faithful to the disasm at 0x60400b..0x6041eb).
    // The unified setup at loc_604034 walks four vertex indices held in
    //   apex  = edi  (LEFT-edge base + topRow vertex)
    //   leftV = esi  (LEFT-edge target / first sub-edge)
    //   rGuard= ebp  (RIGHT-edge guard base)
    //   rightV= ecx  (RIGHT-edge target / second sub-edge)
    // selected by which of the three top vertices share the minimum Y (v8).
    int apex;       // edi
    int leftV;      // esi  (left edge target)
    int rightV;     // ecx  (right edge target)
    int rGuard;     // ebp  (right-edge guard base vertex)
    if (v8 == s.py[kEdgeNext[a4]]) {
        // equal path (0x604025): edi=a4, esi=prev[a4], ebp=next[a4],
        //   ecx=next[next[a4]] (== prev[a4] for a 3-cycle). Flat top: the right
        //   edge runs next[a4] -> next[next[a4]], NOT a4 -> next[a4].
        apex = a4;
        leftV = kEdgePrev[a4];                 // esi = T[2*a4+1]
        rGuard = kEdgeNext[a4];                // ebp = T[2*a4]
        rightV = kEdgeNext[kEdgeNext[a4]];     // ecx = T[2*next[a4]]
    } else {
        int prev = kEdgePrev[a4]; // edx
        if (v8 == s.py[prev]) {
            // 0x604181: edi=prev, esi=prev[prev], ebp=a4, ecx=next[a4].
            apex = prev; rGuard = a4;
            leftV = kEdgePrev[prev];
            rightV = kEdgeNext[a4];
        } else {
            // 0x604197: edi=a4, esi=prev, ebp=a4, ecx=next[a4].
            apex = a4; rGuard = a4;
            leftV = prev;
            rightV = kEdgeNext[a4];
        }
    }

    // LEFT edge guard + setup: py[leftV] - py[apex] > 0   (ComputeEdgeSlope(edi,esi)).
    if (s.py[leftV] - s.py[apex] <= 0)
        return;
    ComputeEdgeSlope(s, apex, leftV);
    // RIGHT edge guard + setup: py[rightV] - py[rGuard] > 0 (InterpolateEdgeZ(ebp,ecx)).
    if (s.py[rightV] - s.py[rGuard] <= 0)
        return;
    InterpolateEdgeZ(s, rGuard, rightV);

    int topRow = (s.py[apex] + 0xFFFF) >> 16; // ceil16(v8); py[apex]==v8 here.
    s.is8bpp = !surf.is16bpp;            // byte_140A220 = (a2 <= 8)
    if (s.is8bpp) {
        s.fillValue = 1;                  // dword_13FC5E0 = 1
        s.dstRow = surf.pixels + (size_t)surf.pitch * topRow;
    } else {
        s.fillValue = 0xFFFF;             // dword_13FC5E0 = 0xFFFF
        s.dstRow = surf.pixels + (size_t)(2 * surf.width) * topRow;
    }

    // First span block (0x6040C4..0x60411a): top -> ceil16(min(py[leftV],py[rightV])).
    // v36 = (py[esi] < py[ecx]); the count uses the SMALLER of the two bottoms.
    bool v36 = s.py[leftV] < s.py[rightV]; // var_1C = py[esi] < py[ecx]
    int firstBottom = v36 ? s.py[leftV] : s.py[rightV]; // min(py[leftV],py[rightV])
    FillSpans(s, surf, ((firstBottom + 0xFFFF) >> 16) - topRow);

    // Second span block (0x60412b..0x6041eb). Re-set up whichever sub-edge
    // remains and fill ceil16(larger) - ceil16(smaller). The disasm orders the
    // subtraction as (edx - eax) where edx/eax depend on v36:
    //   v36 : ComputeEdgeSlope(esi,ecx); count = ceil16(py[ecx]) - ceil16(py[esi])
    //  !v36 : InterpolateEdgeZ(ecx,esi); count = ceil16(py[esi]) - ceil16(py[ecx])
    if (s.py[leftV] != s.py[rightV]) {
        int count;
        if (v36) {
            // 0x6041D6: ComputeEdgeSlope(eax=esi=leftV, edx=ecx=rightV) — LEFT edge.
            ComputeEdgeSlope(s, leftV, rightV);
            count = ((s.py[rightV] + 0xFFFF) >> 16) - ((s.py[leftV] + 0xFFFF) >> 16);
        } else {
            // 0x60413b: InterpolateEdgeZ(eax=ecx=rightV, edx=esi=leftV) — RIGHT edge.
            InterpolateEdgeZ(s, rightV, leftV);
            count = ((s.py[leftV] + 0xFFFF) >> 16) - ((s.py[rightV] + 0xFFFF) >> 16);
        }
        FillSpans(s, surf, count);
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5f363c VIBE_Shadow_RenderMeshShadow tri-loop tail (0x5f3f38):
//   v78 = 0;
//   for ( j = *(_DWORD **)(v108 + 4);                 // mesh.tris
//         v78 < *(_DWORD *)(v108 + 12);               // mesh.triCount
//         j = (_DWORD *)(v80 + 40) )                  // stride 40 bytes
//     VIBE_Shadow_RasterizeTriangle(j, *(_BYTE *)(v96 + 124),  // surface bpp
//                                   *(_DWORD *)(v96 + 116),     // surface width
//                                   v78++);                     // tri index
// One ShadowRasterState (the 13FCxxxx accumulator globals) is shared across the
// whole loop, exactly as the original re-uses the process globals per triangle.
// ---------------------------------------------------------------------------
int RenderObjectShadow(const ShadowMeshTri* tris, int count, ShadowSurface& surf) {
    ShadowRasterState s;            // dword_13FCxxxx accumulator block
    int v78 = 0;                    // tri index (the original's v78)
    for (; v78 < count; ++v78)      // for ( ; v78 < mesh.triCount; ... )
        RasterizeTriangle(s, tris[v78].v, surf);
    return v78;
}

} // namespace guild::render

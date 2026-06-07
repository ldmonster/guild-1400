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
        slope = (int)(((long long)dx << 16) / dy);
    else
        slope = (int)((unsigned long long)((0x40000000 / dy) * (long long)dx) >> 14);
    outSlope = slope;
    // start = px0 + (slope * (ceil16(py0) - py0)) >> 16
    long long frac = (long long)(((py0 + 0xFFFF) >> 16 << 16) - py0);
    int start = px0 + (int)((unsigned long long)(slope * frac) >> 16);
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
    if (s.is8bpp) {
        for (int v3 = 0; v3 < count; ++v3) {
            int v4 = (s.leftX + 0xFFFF) >> 16;
            s.rightX = v1;
            s.spanLen = ((v1 + 0xFFFF) >> 16) - v4;
            if (s.spanLen > 0)
                std::memset(s.dstRow + v4, s.fillValue, (size_t)s.spanLen);
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
                u16* p = (u16*)(s.dstRow + 2 * v6);
                u16 word = (u16)s.fillValue;
                while (v7--) *p++ = word;
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

    // Winding / apex resolution (faithful to the disasm at 0x60401f, register
    // mapping: apex=edi, leftTarget=esi(=prev), rightTarget=ecx(=next),
    // rightGuardBase=ebp). The three cases handle a flat top edge.
    int apex;       // edi
    int leftV;      // esi  (left edge target)
    int rightV;     // ecx  (right edge target)
    int rGuard;     // ebp  (right-edge guard base vertex)
    if (v8 == s.py[kEdgeNext[a4]]) {
        // equal path (0x604025): ebp=a4, esi=prev, ecx=next, edi=a4.
        apex = a4; rGuard = a4;
        leftV = kEdgePrev[a4];
        rightV = kEdgeNext[a4];
    } else {
        int prev = kEdgePrev[a4]; // edx
        if (v8 == s.py[prev]) {
            // 0x604181: ebp=a4, ecx=next, esi=prev(prev), edi=prev.
            apex = prev; rGuard = a4;
            leftV = kEdgePrev[prev];
            rightV = kEdgeNext[a4];
        } else {
            // 0x604197: ebp=a4, esi=prev, edi=a4; ecx=next (unchanged).
            apex = a4; rGuard = a4;
            leftV = prev;
            rightV = kEdgeNext[a4];
        }
    }

    // LEFT edge guard + setup: py[leftV] - py[apex] > 0.
    if (s.py[leftV] - s.py[apex] <= 0)
        return;
    ComputeEdgeSlope(s, apex, leftV);   // ComputeEdgeSlope(edi, esi)
    // RIGHT edge guard + setup: py[rightV] - py[rGuard] > 0.
    if (s.py[rightV] - s.py[rGuard] <= 0)
        return;
    InterpolateEdgeZ(s, rGuard, rightV); // InterpolateEdgeZ(ebp, ecx)

    int topRow = (s.py[apex] + 0xFFFF) >> 16; // ebx = apex's 16.16 Y (ebx+0FFFFh)
    s.is8bpp = !surf.is16bpp;            // byte_140A220 = (a2 <= 8)
    if (s.is8bpp) {
        s.fillValue = 1;                  // dword_13FC5E0 = 1
        s.dstRow = surf.pixels + (size_t)surf.pitch * topRow;
    } else {
        s.fillValue = 0xFFFF;             // dword_13FC5E0 = 0xFFFF
        s.dstRow = surf.pixels + (size_t)(2 * surf.width) * topRow;
    }

    // First span block: top -> ceil16(py[leftV]).  (esi=leftV in the disasm.)
    bool v36 = s.py[leftV] < s.py[rightV]; // var_1C = py[esi] < py[ecx]
    FillSpans(s, surf, ((s.py[leftV] + 0xFFFF) >> 16) - topRow);

    // Second span block: continue along whichever sub-edge remains. Both the
    // v36 (LEFT) and !v36 (RIGHT) branches converge on the same span count
    // ceil16(py[rightV]) - ceil16(py[leftV]); they differ only in which edge is
    // re-set up (the disasm both jmp to loc_604150).
    if (s.py[leftV] != s.py[rightV]) {
        if (v36) {
            // 0x6041D6: ComputeEdgeSlope(eax=leftV, edx=rightV) — LEFT edge.
            ComputeEdgeSlope(s, leftV, rightV);
        } else {
            // 0x60413b: InterpolateEdgeZ(eax=rightV, edx=leftV) — RIGHT edge.
            InterpolateEdgeZ(s, rightV, leftV);
        }
        FillSpans(s, surf,
                  ((s.py[rightV] + 0xFFFF) >> 16) - ((s.py[leftV] + 0xFFFF) >> 16));
    }
}

} // namespace guild::render

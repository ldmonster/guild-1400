#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — flat-fill shadow triangle rasterizer. Faithful 1:1
// reconstruction of the self-contained software triangle-fill used to splat a
// shadow into a target surface (d3_engine.c, the "flat / shadow triangle" path):
//
//   0x603ed4  VIBE_Shadow_RasterizeTriangle  (vertex sort + edge setup + fill)
//   0x603d00  VIBE_Raster_ComputeEdgeSlope   (left-edge 16.16 slope + start X)
//   0x5f6a8c  VIBE_Raster_InterpolateEdgeZ   (right-edge 16.16 slope + start X)
//   0x603da8  VIBE_Raster_FillSpans          (per-scanline span memset, 8/16bpp)
//
// FIXED POINT: vertices carry float screen X,Y at +16/+20; they are converted to
// 16.16 by multiplying by 65536.0 (flt_62C6C0) and truncating toward zero
// (VIBE_Coord_ConvertX). All edge walking is 16.16; pixel rows are produced by
// the `(acc + 0xFFFF) >> 16` ceil-of-fixed idiom (top-left fill rule).
//
// GLOBALS modelled as ShadowRasterState (original addresses in comments):
//   dword_13FC5B0[3]  px[]   16.16 vertex X
//   dword_13FC59C[3]  py[]   16.16 vertex Y
//   dword_13FC5E8     leftDxDy   (ComputeEdgeSlope slope)
//   dword_13FC5C8     rightDxDy  (InterpolateEdgeZ slope)
//   dword_13FC5D8     leftX      (left accumulator 16.16)
//   dword_13FC5BC     rightX     (right accumulator 16.16)
//   dword_13FC5E0     fillValue  (1 for 8bpp, 0xFFFF for 16bpp)
//   dword_13FC5DC     dstRow8    (8bpp dest row byte ptr)
//   dword_13FC5D4     dstRow16   (16bpp dest row byte ptr)
//   dword_13FC588     spanLen    (scratch)
//   byte_140A220      is8bpp
// SURFACE base/pitch/width come from the locked target (dword_7626F0/FC/F8).
//
// The vertex-ordering tables dword_5AC540/5AC544 (recovered byte-for-byte) pick
// the prev/next vertex given the top vertex index, see kEdgeNext/kEdgePrev.
// =============================================================================
namespace guild::render {

// 65536.0 (0x47800000) — float screen coord -> 16.16 fixed scale.
constexpr float kScreenToFixed = 65536.0f;

// gilde.exe 0x5AC540 — single vertex-ordering table T, indexed [topVertex].
// Recovered via get_bytes: dwords at 0x5AC540 = {1,2,2,0,0,1,...}. Because
// dword_5AC544 == dword_5AC540 + 4 bytes, the original's two accesses
// dword_5AC540[2*a4] and dword_5AC544[2*a4] read T[2*a4] and T[2*a4+1]:
//   kEdgeNext[a4] = T[2*a4]   = {1, 2, 0}   (the next vertex CW)
//   kEdgePrev[a4] = T[2*a4+1] = {2, 0, 1}   (the previous vertex CW)
// i.e. the cyclic (next, prev) of each triangle vertex.
constexpr int kEdgeNext[3] = {1, 2, 0}; // T[2*a4]
constexpr int kEdgePrev[3] = {2, 0, 1}; // T[2*a4+1]

// A small shadow surface: 8-bit OR 16-bit, row-major, `pitch` bytes per row.
struct ShadowSurface {
    u8* pixels = nullptr;  // dword_7626F0 base
    int pitch = 0;         // dword_7626FC (8bpp) / 2*dword_7626F8 (16bpp)
    int width = 0;         // dword_7626F8 (pixels per row)
    int height = 0;
    bool is16bpp = false;  // a2 > 8 in the original
};

// One triangle of the shadow mesh: three projected screen-space vertices (the
// rasterizer reads vertex+16/+20). `winding` is the +38 flag bit2 (0x4) gate the
// original checks for the back-facing branch.
struct ShadowTri {
    float x[3];      // vertex +16 (screen X)
    float y[3];      // vertex +20 (screen Y)
    bool backFlag;   // *((BYTE*)v4+38) & 4
};

// The mutable raster state (the 13FCxxxx accumulator globals).
struct ShadowRasterState {
    int px[3] = {};    // dword_13FC5B0
    int py[3] = {};    // dword_13FC59C
    int leftDxDy = 0;  // dword_13FC5E8
    int rightDxDy = 0; // dword_13FC5C8
    int leftX = 0;     // dword_13FC5D8
    int rightX = 0;    // dword_13FC5BC
    int fillValue = 0; // dword_13FC5E0
    u8* dstRow = nullptr; // dword_13FC5DC / dword_13FC5D4
    int spanLen = 0;   // dword_13FC588
    bool is8bpp = false; // byte_140A220
};

// gilde.exe 0x603d00 — VIBE_Raster_ComputeEdgeSlope. Sets leftDxDy + leftX for
// the edge from vertex a1 to a2 (the LEFT edge). Returns leftX.
int ComputeEdgeSlope(ShadowRasterState& s, int a1, int a2);

// gilde.exe 0x5f6a8c — VIBE_Raster_InterpolateEdgeZ. Sets rightDxDy + rightX for
// the edge a1->a2 (the RIGHT edge). Returns rightX.
int InterpolateEdgeZ(ShadowRasterState& s, int a1, int a2);

// gilde.exe 0x603da8 — VIBE_Raster_FillSpans. Fills `count` scanlines, advancing
// both edge accumulators, memset-ing [leftX,rightX) per row into `surf`.
void FillSpans(ShadowRasterState& s, const ShadowSurface& surf, int count);

// gilde.exe 0x603ed4 — VIBE_Shadow_RasterizeTriangle. Converts the triangle's
// float screen verts to 16.16, sorts by Y (finding the top vertex), sets up the
// left/right edges, and fills the two sub-triangles into `surf`. `clipW`/`clipH`
// bound the raster to the surface (the original passes a3=width as a 16.16 clip).
void RasterizeTriangle(ShadowRasterState& s, const ShadowTri& tri,
                       ShadowSurface& surf);

// ---------------------------------------------------------------------------
// PER-OBJECT SHADOW RENDER ENTRY — the geometry tail of
// gilde.exe 0x5f363c VIBE_Shadow_RenderMeshShadow (loc 0x5f3f0b..0x5f3f8b):
//
//     if ( *(_DWORD *)(v96 + 96) )                       // surface bound
//     {
//       VIBE_Render_LockSurfaceRegion(...);              // 0x5f3f0b
//       dword_7626F0/FC/F8 = locked surface base/pitch/width;   // 0x5f3f21
//       for ( j = mesh.tris; v78 < mesh.triCount; j += 40 )     // 0x5f3f38
//         VIBE_Shadow_RasterizeTriangle(j, surf.bpp, surf.width, v78++); // 0x5f3f4c
//       VIBE_Render_UnlockSurface(...);                  // 0x5f3f67
//       if ( *(BYTE*)(*(a8+8)+16) )                       // copy-out flag
//         VIBE_Render_CopySurfacePixels(...);            // 0x5f3f8b
//     }
//
// This is the clean entry the live 3D frame calls per shadow-casting object: a
// projected-silhouette MESH (each triangle's three vertices already projected to
// the ground and screen-spaced — vertex .x/.y are the +16/+20 screen coords the
// rasterizer reads) is splatted, one VIBE_Shadow_RasterizeTriangle per triangle,
// into the object's shadow `surf`. The lock/unlock/copy-out are the platform
// surface ops (the original's DirectDraw lock; here the caller owns `surf`).
//
// A shadow mesh triangle: three projected screen-space vertices + the per-tri
// back-face flag (+38 bit2) the rasterizer's reverse-winding branch gates on.
struct ShadowMeshTri {
    ShadowTri v;   // x[3]/y[3] screen verts + backFlag
};

// Splat every triangle of a projected shadow mesh into `surf`. Faithful to the
// tri loop tail of RenderMeshShadow: iterates [0, count), calling
// RasterizeTriangle(tri[i], surf) in order with a shared ShadowRasterState
// (the original re-uses the 13FCxxxx accumulator globals across triangles).
// Returns the number of triangles processed (== count). This is the per-object
// render entry the frame drives once the silhouette has been projected.
int RenderObjectShadow(const ShadowMeshTri* tris, int count, ShadowSurface& surf);

} // namespace guild::render

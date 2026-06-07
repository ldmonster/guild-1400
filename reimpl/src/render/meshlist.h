#pragma once
#include "guild/common/types.h"
#include "render/geometry_types.h"
#include "render/clip.h"
#include "render/surface.h"

// =============================================================================
// guild::render — the SOFTWARE draw-list FLUSH that completes the render
// pipeline (deferred by the scene agent). Faithful 1:1 reconstruction of:
//
//   0x5AEC88  VIBE_Render_RasterizeMeshList  (the sorted draw-list flush)
//
// It consumes the radix-sorted PolyList back-to-front (dword_13FC770 entries at
// dword_13FC584), and per polygon:
//   - decides the SPAN-DISPATCH index (the high byte of the entry key, key>>24):
//     normally 4 (opaque textured) but 3 when the texture is translucent
//     ( *(tex+108) - (*(tex+110)&1) < 0xFF ),
//   - if the poly straddles a clip plane or is flagged for clipping, runs
//     VIBE_Render_ClipPolygonToPlane, RE-PROJECTS the surviving vertices through
//     the projection scalars (flt_13FCD0C/D10/D18 + flt_13FCAF8), then
//     TRIANGLE-FAN rasterizes the clipped polygon by calling the dispatch slot
//     once per fan triangle (k = 0..outCount-3),
//   - otherwise rasterizes the polygon directly through the dispatch slot,
//   - DDraw lock/unlock bracket the flush (routed through the present shim).
//
// THE PATCHED-SPAN DISPATCH TABLE  dword_13D8780[...]  (recovered)
// ---------------------------------------------------------------------------
// VIBE_Render_InitEngineDevice (0x5AF984) initialises all seven slots
//   dword_13D8780,8784,8788,878C,8790,8794  (and the 7th used by the flush)
// to VIBE_Raster_NullStub13 (0x5F6EE8, an empty fn). The texture-bind path then
// patches the live slots to the real raster variants. The flush only ever
// indexes slots 4 (opaque) and 3 (translucent). We model the table as an
// explicit 7-entry function-pointer array (default = NullStub) and plug in the
// reconstructed textured-triangle raster fns at slots 3/4.
//
// THE PROJECTION SCALARS (recovered; runtime values, 0 at static time)
// ---------------------------------------------------------------------------
//   flt_13FCD0C  X projection scale   (screenX = D0C * x * (1/z) + D18)
//   flt_13FCD18  X projection offset
//   flt_13FCAF8  Y projection scale   (screenY = AF8 * y * (1/z) + D10)
//   flt_13FCD10  Y projection offset
// These are written by VIBE_Render_SetupViewTransform / BuildViewMatrix; the
// flush re-projects each clipped vertex (whose +76 flag byte >= 0) with them.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Span-fill dispatch: the patched-span function-pointer table dword_13D8780.
// Each entry rasterizes ONE triangle given a Polygon (3 vertex ptrs read from
// the projected/clipped Vertex records) into the active framebuffer surface,
// returning nonzero to CONTINUE the fan (the original kept looping while the
// dispatch returned nonzero). NullStub returns nonzero (continue) but draws
// nothing — exactly the engine's pre-bind state.
// ---------------------------------------------------------------------------
using SpanFillFn = int (*)(Surface* fb, const Polygon& tri);

// gilde.exe 0x5F6EE8 — VIBE_Raster_NullStub13. Empty span fn (draws nothing).
int SpanFillNullStub(Surface* fb, const Polygon& tri);

// Slot 4 — opaque textured triangle (wraps VIBE_Raster_RasterizeTexturedTriangle
// 0x5F7D58: reads the projected screen x/y from each vertex +16/+20 and the
// light byte +66, fills the affine-shaded triangle).
int SpanFillTexturedOpaque(Surface* fb, const Polygon& tri);

// Slot 3 — translucent textured triangle. Same geometry path as slot 4; the
// translucency is a span raster-op the original selected via the patched inner
// span. We reuse the textured-triangle rasterizer (the blend differs only in the
// inner span op, which the existing raster module models as a span variant).
int SpanFillTexturedBlend(Surface* fb, const Polygon& tri);

// The 7-entry dispatch table (indexed by key>>24 ∈ {3,4} in the flush).
struct SpanDispatch {
    SpanFillFn slot[7];   // dword_13D8780[0..6]
    SpanDispatch();       // all = NullStub, slot[4]=opaque, slot[3]=blend
};

// Projection scalars (flt_13FCD0C/D10/D18/AF8) for the clipped-vertex reproject.
struct ProjectScalars {
    float xScale;   // flt_13FCD0C
    float xOffset;  // flt_13FCD18
    float yScale;   // flt_13FCAF8
    float yOffset;  // flt_13FCD10
};

// The sorted draw list the flush consumes (snapshot of the PolyList globals).
//   entries = dword_13FC584 base,  count = dword_13FC770.
struct MeshList {
    const DrawListEntry* entries;  // dword_13FC584
    i32                  count;    // dword_13FC770
};

// gilde.exe 0x5AEC88 — VIBE_Render_RasterizeMeshList.
// Flush the sorted `list` into `fb`, clipping straddling polys against `ctx`
// (re-projecting survivors with `proj`) and dispatching each triangle through
// `disp`. `scratch` is the clip ping-pong/pool. DDraw lock/unlock are routed via
// the present shim (AcquireBackBuffer/UnlockBackBuffer; forward-declared in the
// .cpp). Returns the number of polygons drawn (entries iterated).
int RasterizeMeshList(const MeshList& list, Surface* fb, const SpanDispatch& disp,
                      const ClipContext& ctx, const ProjectScalars& proj,
                      ClipScratch& scratch);

} // namespace guild::render

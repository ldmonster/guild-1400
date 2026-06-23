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
// THE SPAN DISPATCH TABLE  dword_13D8780[...]  (VERIFIED wave-5)
// ---------------------------------------------------------------------------
// The table is SIX dword slots: dword_13D8780,8784,8788,878C,8790,8794 (indices
// 0..5). The flush indexes it with `(*(int*)&v45[1]) >> 24`, where v45[4] = 4
// (opaque) by default and 3 when the bound texture is translucent
// (*(tex+108) - (*(tex+110)&1) < 0xFF) — so the live index is 3 or 4.
//
// SLOT-MAP / installer audit (xrefs_to 0x13D8780..0x13D8794, wave-5):
//   * The ONLY writer of any of the six slots is VIBE_Render_InitEngineDevice
//     @0x5AF984 (0x5AFB87..0x5AFBA5), which sets ALL six to
//     VIBE_Raster_NullStub13 (0x5F6EE8, `retn`).
//   * There is NO second installer. Nothing ever patches a real raster leaf
//     into slot 3/4/etc. The wave-4 belief that "the texture-bind path patches
//     the live slots" is FALSE — grep/xrefs confirm no such writer exists, and
//     0x5F6C30 (RasterizeMirrorTriangle) / 0x5F721A (masked span) have no static
//     refs precisely because they are NOT reached through this table.
//   => In the real binary the software flush 0x5AEC88 dispatches slots 3/4 to
//      NullStub13: it draws NOTHING. The actual textured triangle rendering goes
//      through the D3D path VIBE_Render_DrawTexturedTriangles @0x5AE434 (builds
//      vertex buffers, calls VIBE_Render_DrawTriangleList) — which does NOT use
//      this table at all. 0x5AEC88 is the alternate software/DDraw-Lock flush.
//
// NOTE: dword_13D8798 (which follows slot 5) is NOT a 7th dispatch slot. It is
// the 3-entry CLIP-INPUT vertex-pointer array dword_13D8798[0]/879C/87A0, seeded
// with the poly's three vertices before VIBE_Render_ClipPolygonToPlane in both
// 0x5AEC88 and 0x5AE434 (xrefs: 0x5AD813/0x5AD87A in the clipper, 0x5AE989,
// 0x5AED8F). It happens to be adjacent in memory; it is not a function pointer.
//
// For this reconstruction we model the table as an explicit 6-entry pointer
// array (default = NullStub). Slots 3/4 are wired to the reconstructed textured
// leaf as a HOST CHOICE so the software flush actually produces pixels for the
// portable/headless renderer (the binary's own slots are NullStub — see above);
// this is documented and behaviour is otherwise the 1:1 flush control flow.
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

// The 6-entry dispatch table (indexed by key>>24 ∈ {3,4} in the flush).
// dword_13D8780[0..5]; the binary fills all six with NullStub13 and never
// patches them (wave-5). slot[3]/slot[4] are wired to the textured leaf here as
// the host renderer choice so the software flush produces pixels.
struct SpanDispatch {
    SpanFillFn slot[6];   // dword_13D8780[0..5]
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

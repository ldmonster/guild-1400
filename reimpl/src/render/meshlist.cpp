#include "render/meshlist.h"
#include "render/raster.h"

// =============================================================================
// guild::render software draw-list flush — implementation. See meshlist.h for
// the module overview, the recovered dispatch table, and the projection
// scalars. The control flow (per-poly dispatch-index decision, clip-vs-direct
// branch, clipped-vertex reprojection, triangle-fan loop) mirrors the Hex-Rays
// pseudocode of gilde.exe 0x5AEC88 verbatim.
// =============================================================================

// ---------------------------------------------------------------------------
// The DDraw lock/unlock pair that brackets the flush is the present layer
// (dd_vesa.c). Per CONVENTIONS we route it through the shim and forward-declare
// the boundary here. The flush calls AcquireBackBuffer() before the loop and
// UnlockBackBuffer() after; in this reconstruction the framebuffer Surface* is
// passed in directly (the lock just yields the backbuffer pixel pointer), so the
// shim hooks are thin and overridable for the test.
namespace guild::render::present_shim {
// gilde.exe 0x4345D4 — VIBE_Render_AcquireBackBuffer (Lock the offscreen surface
// / fetch ppvBits). gilde.exe 0x434680 — VIBE_Render_UnlockBackBuffer (Unlock).
// Default no-op hooks; a test backend can install its own to record the bracket.
bool AcquireBackBuffer();
int  UnlockBackBuffer(int status);
} // namespace guild::render::present_shim

namespace guild::render {

// Weak default shim impls (the real present backend overrides these). They model
// the lock/unlock as success no-ops; the Surface* the flush writes is supplied
// by the caller (the lock would otherwise hand back ppvBits / pitch).
namespace present_shim {
bool AcquireBackBuffer() { return true; }
int  UnlockBackBuffer(int status) { return status; }
} // namespace present_shim

// ---------------------------------------------------------------------------
// Dispatch table slots.
// ---------------------------------------------------------------------------

// gilde.exe 0x5F6EE8 — VIBE_Raster_NullStub13. Draws nothing; returns nonzero so
// the fan loop keeps iterating (the engine's pre-texture-bind state).
int SpanFillNullStub(Surface* /*fb*/, const Polygon& /*tri*/) { return 1; }

// Shared geometry path: read each vertex's PROJECTED screen x/y (+16/+20 in the
// 80-byte Vertex record, i.e. Vertex::screenX/screenY) and the light byte (+66,
// Vertex::lightIdx), then call the reconstructed textured-triangle rasterizer.
static int RasterTri(Surface* fb, const Polygon& tri) {
    if (!fb || !tri.v0 || !tri.v1 || !tri.v2)
        return 1;
    const Vertex* vp[3] = {tri.v0, tri.v1, tri.v2};
    RasterVertex rv[3];
    for (int i = 0; i < 3; ++i) {
        rv[i].x = vp[i]->screenX;   // +0x10 projected screen x
        rv[i].y = vp[i]->screenY;   // +0x14 projected screen y
        rv[i].light = vp[i]->lightIdx; // +0x42 light/shade index
    }
    RasterizeTexturedTriangle(fb, rv);
    return 1;  // continue the fan
}

// Slot 4 — opaque textured triangle.
int SpanFillTexturedOpaque(Surface* fb, const Polygon& tri) { return RasterTri(fb, tri); }
// Slot 3 — translucent textured triangle (same geometry; blend is an inner-span op).
int SpanFillTexturedBlend(Surface* fb, const Polygon& tri) { return RasterTri(fb, tri); }

SpanDispatch::SpanDispatch() {
    for (int i = 0; i < 7; ++i)
        slot[i] = &SpanFillNullStub;        // VIBE_Raster_NullStub13 default
    slot[4] = &SpanFillTexturedOpaque;      // opaque (key>>24 == 4)
    slot[3] = &SpanFillTexturedBlend;       // translucent (key>>24 == 3)
}

// ---------------------------------------------------------------------------
// Per-poly helpers mirroring the flush's inline tests.
// ---------------------------------------------------------------------------

// The poly's three vertex "clip" flag bytes (+76 in each Vertex record). When
// ((v0|v1|v2) & 0x3F) != 0 the poly straddles a frustum plane and must be
// clipped (mirrors v46 |= ...[76] ; (... & 0x3F) != 0 in the flush).
inline u8 VertexClipByte(const Vertex* v) { return ((const u8*)v)[76]; }

// gilde.exe 0x5AEC88 — VIBE_Render_RasterizeMeshList
int RasterizeMeshList(const MeshList& list, Surface* fb, const SpanDispatch& disp,
                      const ClipContext& ctx, const ProjectScalars& proj,
                      ClipScratch& scratch) {
    // dword_13FC4FC = 0;  VIBE_Render_AcquireBackBuffer();
    present_shim::AcquireBackBuffer();

    int drawn = 0;
    int lastStatus = 0;

    // Iterate back-to-front: v44 = count-1 .. 0 (the original walks the sorted
    // list from the last entry; the radix sort already ordered it).
    for (i32 idx = list.count - 1; idx >= 0; --idx) {
        const DrawListEntry& e = list.entries[idx];
        Polygon* poly = e.poly;        // v2 = *(DrawListEntry+4)
        if (!poly)
            continue;
        ++drawn;

        // --- dispatch-index decision (v45[4]) --------------------------------
        // Default 4 (opaque). The original consults the texture record (poly+20):
        // translucent when ( tex[108] - (tex[110]&1) < 0xFF ). We carry the
        // translucency on the polygon's flag bytes: flags38 bit0 => translucent.
        int dispIdx = 4;
        bool translucent = (poly->flags38 & 0x01) != 0; // tex translucency proxy
        if (translucent)
            dispIdx = 3;

        // --- clip-vs-direct decision -----------------------------------------
        // CLIP when any vertex clip byte has a frustum bit set (&0x3F), or the
        // clip context's +4 flag is set (here: a nonzero plane count forces the
        // straddle test through the clipper).
        u8 clipMask = (u8)(VertexClipByte(poly->v0) | VertexClipByte(poly->v1) |
                           VertexClipByte(poly->v2));
        bool needClip = (clipMask & 0x3F) != 0;

        if (!needClip) {
            // Direct path: dispatch the polygon as a single triangle.
            lastStatus = disp.slot[dispIdx](fb, *poly);
            continue;
        }

        // --- clip path -------------------------------------------------------
        // Seed the clip list with the poly's 3 vertex pointers (dword_13D8798).
        scratch.listPtrs[0][0] = poly->v0;   // dword_13D8798[0]
        scratch.listPtrs[0][1] = poly->v1;   // dword_13D879C
        scratch.listPtrs[0][2] = poly->v2;   // dword_13D87A0

        Vertex** clipped = ClipPolygonToPlane(scratch, ctx, 3);
        if (!clipped || scratch.outCount < 3)
            continue;  // fully clipped away (< 3 survivors)

        // dword_649DA0 += dword_649D74 - 3;  (extra-triangle stat; not modeled)

        // Re-project each surviving vertex whose +76 flag byte is >= 0 (sign bit
        // clear) through the projection scalars. The original:
        //   r = 1/z;  screenX = D0C*x*r + D18;  screenY = AF8*y*r + D10;
        for (i32 k = 0; k < scratch.outCount; ++k) {
            Vertex* v = clipped[k];
            if ((i8)VertexClipByte(v) >= 0) {     // *(char*)(v+76) >= 0
                float r = 1.0f / v->z;            // 1.0 / v20[2]
                float sx = proj.xScale * v->x * r + proj.xOffset; // D0C*x*r + D18
                float sy = r * (proj.yScale * v->y) + proj.yOffset; // r*(AF8*y)+D10
                v->screenX = sx;                  // v20[4]  (+0x10)
                v->screenY = sy;                  // v20[5]  (+0x14)
            }
        }

        // Triangle-fan the clipped polygon: fan apex = clipped[0], then
        // (clipped[k+1], clipped[k+2]) for k = 0..outCount-3. The original
        // continues the loop while the dispatch returns nonzero.
        Polygon fanTri = *poly;          // copy the 40-byte poly template
        fanTri.v0 = clipped[0];          // fan apex
        for (i32 k = 0; k + 2 < scratch.outCount; ++k) {
            fanTri.v1 = clipped[k + 1];
            fanTri.v2 = clipped[k + 2];
            lastStatus = disp.slot[dispIdx](fb, fanTri);
            if (!lastStatus)
                break;  // dispatch asked to stop the fan
        }
    }

    // VIBE_Render_UnlockBackBuffer(...)
    present_shim::UnlockBackBuffer(lastStatus);
    return drawn;
}

} // namespace guild::render

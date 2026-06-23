#include "play/map_view.h"

#include "play/ui_recon5_panels.h"   // play::MapViewSortMarkersByScreenY (REAL sort @0x54457e)
#include "gui/mapview.h"             // gui::MapView_ComputeMarkerScreenPos / g_mapWidth/Height
#include "gui/menu_render.h"         // gui::MenuFillRect (REAL fill leaf -> SurfaceDrawHLine)
#include "render/surface.h"          // render::SurfaceDrawRectOutline (REAL outline leaf)

#include <algorithm>
#include <cmath>

namespace guild::play {

// ===========================================================================
// Hooks (asset-backed Landkarte2 background blit) — inert default in the library,
// per the build-model convention (mirrors play/hud_render).
// ===========================================================================
namespace {
MapViewHooks g_hooks;   // drawBackground == nullptr => inert (backdrop fill)

// Project a marker through the REAL gui::MapView_ComputeMarkerScreenPos and return
// its FULL-MAP screen pixel (mapW/2 + dx + pan, mapH/2 + dz + pan).  This is the
// exact value the dispatcher computes before subtracting the scroll offset.
inline void ProjectFull(const OverviewMarker& mk, const OverviewCamera& cam,
                        float& outX, float& outY) {
    gui::MapMarker m{};
    m.worldX = mk.worldX;
    m.worldZ = mk.worldZ;
    gui::MapView_ComputeMarkerScreenPos(m, cam.panX, cam.panY, cam.cameraOrigX);
    outX = m.screenX;
    outY = m.screenY;
}
} // namespace

void SetMapViewHooks(const MapViewHooks& hooks) { g_hooks = hooks; }
const MapViewHooks& GetMapViewHooks() { return g_hooks; }

// ===========================================================================
// Projection.
// ===========================================================================
OverviewXY MapView_ProjectMarker(const OverviewMarker& mk, const OverviewCamera& cam) {
    float sx, sy;
    ProjectFull(mk, cam, sx, sy);
    // Viewport-local pixel = full-map pixel - scroll offset (the dispatcher's
    // child window scrolls the marker layer by the scroll offset).
    int x = static_cast<int>(sx) - cam.scrollX;
    int y = static_cast<int>(sy) - cam.scrollY;
    OverviewXY r;
    r.x = x;
    r.y = y;
    r.inView = (x >= 0 && x < kOverviewViewW && y >= 0 && y < kOverviewViewH);
    return r;
}

WorldXZ MapView_ClickToWorld(int vx, int vy, const OverviewCamera& cam) {
    // Forward affine (origin scale dbl_624058 low-bytes == 0, perspective "bow"
    // dropped — the original does not bow-correct a click pick):
    //   screenX = mapW/2 + worldX·5.33 + panX
    //   screenY = mapH/2 + worldZ·5.33 + panY
    // and the visible viewport pixel = screen - scroll, so screen = vx + scroll.
    const double kScale = static_cast<double>(gui::kMarkerWorldScale); // 5.33
    double sx = static_cast<double>(vx + cam.scrollX);
    double sy = static_cast<double>(vy + cam.scrollY);
    WorldXZ w;
    w.x = static_cast<float>((sx - static_cast<double>(gui::g_mapWidth / 2)
                              - static_cast<double>(cam.panX)) / kScale);
    w.z = static_cast<float>((sy - static_cast<double>(gui::g_mapHeight / 2)
                              - static_cast<double>(cam.panY)) / kScale);
    return w;
}

int MapView_PickMarker(const std::vector<OverviewMarker>& markers,
                       const OverviewCamera& cam, int vx, int vy, int dotSize) {
    const int half = dotSize / 2;
    int hit = -1;
    // Draw order = Y-sorted (caller's order); the last covering marker is on top.
    for (int i = 0; i < static_cast<int>(markers.size()); ++i) {
        OverviewXY p = MapView_ProjectMarker(markers[i], cam);
        int x0 = p.x - half;
        int y0 = p.y - half;
        if (vx >= x0 && vx < x0 + dotSize && vy >= y0 && vy < y0 + dotSize)
            hit = i;
    }
    return hit;
}

// ===========================================================================
// Render.
// ===========================================================================
OverviewRenderResult MapView_RenderOverview(render::Surface& surf,
                                            std::vector<OverviewMarker> markers,
                                            const OverviewCamera& cam,
                                            int viewX, int viewY,
                                            int viewW, int viewH,
                                            int dotSize,
                                            const OverviewPalette& pal) {
    OverviewRenderResult res;
    if (!surf.pixels || viewW <= 0 || viewH <= 0) return res;

    // -----------------------------------------------------------------------
    // 1. Background (the "Misc\Landkarte2" artwork).  Asset edge -> hook; the
    //    inert default fills the viewport with the backdrop colour so the marker
    //    layer is always composited over a non-blank base.
    // -----------------------------------------------------------------------
    bool drew = false;
    if (g_hooks.drawBackground)
        drew = g_hooks.drawBackground(&surf, viewX, viewY, viewW, viewH,
                                      cam.scrollX, cam.scrollY, g_hooks.userData);
    if (!drew)
        gui::MenuFillRect(&surf, viewX, viewY, viewW, viewH, pal.bgR, pal.bgG, pal.bgB);
    res.backgroundDrawn = 1;

    // -----------------------------------------------------------------------
    // 2. Markers.  Project every marker (REAL gui::MapView_ComputeMarkerScreenPos),
    //    sort ascending by the projected screenY via the REAL selection sort
    //    (play::MapViewSortMarkersByScreenY @0x54457e) so nearer (lower-Y) markers
    //    paint first, then draw each centred dot clipped to the viewport.
    // -----------------------------------------------------------------------
    const int n = static_cast<int>(markers.size());
    if (n > 0) {
        // Build the dispatcher's 6-dword marker records (only screenY is the sort
        // key) carrying the original index so we can recover MarkerKind/entity.
        std::vector<MapMarker> recs(n);
        std::vector<int> idx(n);
        for (int i = 0; i < n; ++i) {
            float sx, sy;
            ProjectFull(markers[i], cam, sx, sy);
            recs[i].obj     = i;               // reuse +0 to carry the source index
            recs[i].entity  = markers[i].entity;
            recs[i].screenX = static_cast<i32>(sx);
            recs[i].screenY = static_cast<i32>(sy);
            recs[i].worldX  = markers[i].worldX;
            recs[i].worldZ  = markers[i].worldZ;
            idx[i] = i;
        }
        // REAL selection sort (swaps whole 6-dword records ascending by screenY).
        MapViewSortMarkersByScreenY(recs.data(), n);

        const int half = dotSize / 2;
        for (int i = 0; i < n; ++i) {
            int src = recs[i].obj;             // recovered source index
            // Viewport-local, centred (the dispatcher's screenX-=w/2, screenY-=h/2).
            int cx = recs[i].screenX - cam.scrollX;
            int cy = recs[i].screenY - cam.scrollY;
            int x0 = cx - half;
            int y0 = cy - half;
            // Clip: skip markers whose centre is outside the viewport.
            if (cx < 0 || cx >= viewW || cy < 0 || cy >= viewH) continue;

            int ki = static_cast<int>(markers[src].kind);
            if (ki < 0 || ki > 5) ki = 0;
            gui::MenuFillRect(&surf, viewX + x0, viewY + y0, dotSize, dotSize,
                              pal.dotR[ki], pal.dotG[ki], pal.dotB[ki]);
            render::SurfaceDrawRectOutline(&surf, viewX + x0, viewY + y0,
                                           dotSize, dotSize,
                                           pal.edgeR, pal.edgeG, pal.edgeB);
            ++res.markersDrawn;
        }
    }

    return res;
}

} // namespace guild::play

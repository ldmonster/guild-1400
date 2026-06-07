// =============================================================================
// guild::play — FULL CITY-VIEW FRAME COMPOSITOR implementation. See city_frame.h.
//
// Layer order (engine: DrawUniverseAndStats @0x5b3bbc):
//   clear(sky) -> terrain floor -> scene objects (over terrain) -> HUD (over all)
// into ONE 32bpp ARGB surface, then render::PresentFrame to a headless device.
// =============================================================================
#include "play/city_frame.h"

#include "render/surface.h"          // SurfaceCreate / SetPixelRgb / GetPixelRgb
#include "render/present.h"          // render::PresentFrame / PresentState / PresentMode
#include "gui/menu_render.h"         // gui::MenuFillRect (real fill leaf), Argb8888
#include "shim/IGraphicsDevice.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace guild::play {

// ===========================================================================
// Layer colour helpers.
// ===========================================================================
void CityFrameGroundRgb(u8 shade, const CityFramePalette& pal, u8& r, u8& g, u8& b) {
    // The terrain shade ramps the GREEN channel of an earthy floor so a ground
    // pixel reads as green-tinted (recognizable vs sky/objects). Bias the shade up
    // a touch so even dark tiles are clearly non-sky.
    int gg = (int)shade + 32;
    if (gg > 255) gg = 255;
    r = pal.groundBaseR;
    g = (u8)gg;
    b = pal.groundBaseB;
}

void CityFrameObjectRgb(u8 light, const CityFramePalette& pal, u8& r, u8& g, u8& b) {
    // Warm structure colour modulated by the object's light byte (so distinct
    // objects keep distinct shades) but kept clearly warm (R>G>B) — unlike the
    // green ground or the blue sky.
    int m = (int)light;
    auto mod = [m](u8 base) {
        int v = (base * (96 + (m >> 1))) / 192;   // ~0.5x..1.16x of base
        if (v < 0) v = 0; if (v > 255) v = 255;
        return (u8)v;
    };
    r = mod(pal.objR);
    g = mod(pal.objG);
    b = mod(pal.objB);
}

namespace {

// Read one 32bpp ARGB pixel as true R,G,B. The reconstructed SurfaceGetPixelRgb
// returns the raw little-endian bytes [B,G,R] for a 32bpp surface (it does not
// re-apply the channel format on the 32bpp read path), so we extract the channels
// from the packed dword directly to get a stable R,G,B regardless of that quirk.
inline void ReadRgb32(const render::Surface* s, int x, int y, u8& r, u8& g, u8& b) {
    const u32 px = reinterpret_cast<const u32*>(s->pixels)[(size_t)s->widthPx * y + x];
    r = (u8)((px >> 16) & 0xff);   // R@16 (gui::Argb8888 pack)
    g = (u8)((px >> 8)  & 0xff);   // G@8
    b = (u8)( px        & 0xff);   // B@0
}

// Count target pixels that differ from the sky-clear colour.
int CountNonSky(const render::Surface* s, u8 skyR, u8 skyG, u8 skyB) {
    int n = 0;
    for (int y = 0; y < s->height; ++y)
        for (int x = 0; x < s->width; ++x) {
            u8 r, g, b; ReadRgb32(s, x, y, r, g, b);
            if (r != skyR || g != skyG || b != skyB) ++n;
        }
    return n;
}

// Composite the 8bpp terrain shade surface into the 32bpp target as ground tint.
// Returns the number of ground pixels written. `bg` is the terrain background byte
// (cleared value); a pixel left at `bg` was not painted by the floor walk and is
// left as sky so the terrain does not blanket the whole frame with a flat fill.
int CompositeTerrain(render::Surface* target, const render::Surface* terrain,
                     u8 bg, const CityFramePalette& pal) {
    int painted = 0;
    const int w = terrain->width < target->width ? terrain->width : target->width;
    const int h = terrain->height < target->height ? terrain->height : target->height;
    for (int y = 0; y < h; ++y) {
        const u8* row = terrain->pixels + (size_t)y * terrain->widthPx;
        for (int x = 0; x < w; ++x) {
            u8 shade = row[x];
            if (shade == bg) continue;          // untouched -> stays sky
            u8 r, g, b;
            CityFrameGroundRgb(shade, pal, r, g, b);
            render::SurfaceSetPixelRgb(target, x, y, r, g, b);
            ++painted;
        }
    }
    return painted;
}

// Fill one WorldObject quad's pixel bbox into the target as an object colour,
// OVER whatever (terrain/sky) is there. Returns the rows MenuFillRect filled.
int FillObjectQuad(render::Surface* target, const WorldObject& o,
                   const CityFramePalette& pal) {
    int x0 = (int)std::lround(o.x0 < o.x1 ? o.x0 : o.x1);
    int z0 = (int)std::lround(o.z0 < o.z1 ? o.z0 : o.z1);
    int x1 = (int)std::lround(o.x0 < o.x1 ? o.x1 : o.x0);
    int z1 = (int)std::lround(o.z0 < o.z1 ? o.z1 : o.z0);
    int w = x1 - x0; if (w < 1) w = 1;
    int h = z1 - z0; if (h < 1) h = 1;
    u8 r, g, b;
    CityFrameObjectRgb(o.light, pal, r, g, b);
    return gui::MenuFillRect(target, x0, z0, w, h, r, g, b);
}

} // namespace

// ===========================================================================
// ComposeCityFrame — the layer chain into one surface.
// ===========================================================================
CityFrameStats ComposeCityFrame(render::Surface* target, const CameraControl& camera,
                                const CityFrameScene& scene,
                                const CityFramePalette& pal) {
    CityFrameStats st{};
    if (!target || !target->pixels) return st;
    st.fbW = target->width;
    st.fbH = target->height;

    // --- clear to sky (back-most) ------------------------------------------
    for (int y = 0; y < target->height; ++y)
        for (int x = 0; x < target->width; ++x)
            render::SurfaceSetPixelRgb(target, x, y, pal.skyR, pal.skyG, pal.skyB);

    // --- LAYER 1: terrain floor (play::terrain_render) ---------------------
    // Render the floor to its native 8bpp shade surface (the real RenderTerrain
    // walk), then composite the shade ramp into the target as ground tint.
    if (scene.terrain.valid()) {
        const u8 bg = 0;
        TerrainRenderStats ts{};
        render::Surface* floor = RenderTerrainToSurface(
            target->width, target->height, scene.terrain, scene.terrainView, bg, &ts);
        if (floor) {
            st.terrainDrawn  = true;
            st.terrainTiles  = ts.tilesDrawn;
            st.terrainTris   = ts.trisDrawn;
            st.terrainPixels = CompositeTerrain(target, floor, bg, pal);
            render::SurfaceDestroy(floor);
        }
    }

    // --- LAYER 2: scene objects at real transforms (play::world_render) ----
    // Build the object draw list from the live entity arrays via WorldRenderer
    // (its build() applies the real/grid placement hooks), recentered on the
    // camera eye and scaled by the camera zoom; then fill each quad OVER terrain.
    {
        WorldRenderer wr;
        WorldRenderer::Options opt = scene.worldOpt;
        opt.fbW = target->width;
        opt.fbH = target->height;
        opt.emitTerrain = false;            // the floor is layer 1; objects only here
        // Carry the city camera into the world placement: eye recenters the layout,
        // zoom tightens the world->pixel scale (the city-view dolly feel).
        opt.eyeX = camera.eye[0];
        opt.eyeZ = camera.eye[2];
        if (opt.useRealPlacement && opt.pixelsPerUnit <= 0.0f)
            opt.pixelsPerUnit = 1.0f + camera.zoom;   // zoom-scaled world units

        LoadedWorld lw;
        WorldDrawList dl = wr.build(opt, lw);
        st.objectsBuilt = dl.sceneObjects();

        for (int i = 0; i < lw.objectCount; ++i) {
            const WorldObject& o = lw.objects[i];
            // skip a fully off-frame quad
            float maxx = o.x0 > o.x1 ? o.x0 : o.x1;
            float maxz = o.z0 > o.z1 ? o.z0 : o.z1;
            float minx = o.x0 < o.x1 ? o.x0 : o.x1;
            float minz = o.z0 < o.z1 ? o.z0 : o.z1;
            if (maxx < 0 || maxz < 0 || minx >= target->width || minz >= target->height)
                continue;
            int rows = FillObjectQuad(target, o, pal);
            if (rows > 0) {
                ++st.objectsDrawn;
                // pixels painted: rows * clamped width (re-measure via bbox)
                int x0 = (int)std::lround(minx), x1 = (int)std::lround(maxx);
                int w = x1 - x0; if (w < 1) w = 1;
                if (x0 < 0) { w += x0; x0 = 0; }
                if (x0 + w > target->width) w = target->width - x0;
                if (w < 0) w = 0;
                st.objectPixels += rows * w;
            }
        }
    }

    // --- LAYER 3: HUD overlay (play::hud_render), OVER everything -----------
    if (scene.drawHud) {
        // Snapshot the world frame BEFORE the HUD pass, so the HUD pixel count is the
        // number of pixels the overlay actually CHANGED — correct even when the world
        // already fills the frame (a non-sky delta would read zero there).
        const size_t nbytes = (size_t)target->pitch * target->height;
        std::vector<u8> before(nbytes);
        std::memcpy(before.data(), target->pixels, nbytes);

        HudRenderResult hr = RenderHud(*target, scene.hud,
                                       scene.hudBarOriginX, scene.hudBarOriginY,
                                       scene.hudCaptionX, scene.hudCaptionY,
                                       scene.hudMapOriginX, scene.hudMapOriginY,
                                       scene.hudPal);

        int changed = 0;
        const u32* a = reinterpret_cast<const u32*>(before.data());
        const u32* b = reinterpret_cast<const u32*>(target->pixels);
        const int npx = target->widthPx * target->height;
        for (int i = 0; i < npx; ++i)
            if (a[i] != b[i]) ++changed;

        st.hudDrawn         = (hr.barSlotsDrawn + hr.captionGlyphs + hr.markersDrawn) > 0;
        st.hudBarSlots      = hr.barSlotsDrawn;
        st.hudCaptionGlyphs = hr.captionGlyphs;
        st.hudMarkers       = hr.markersDrawn;
        st.hudPixels        = changed;
    }

    st.nonBlankPixels = CountNonSky(target, pal.skyR, pal.skyG, pal.skyB);
    return st;
}

// ===========================================================================
// RenderCityFrame — compose + present (the top-level frame entry).
// ===========================================================================
CityFrameStats RenderCityFrame(const CameraControl& camera,
                               const CityFrameScene& scene,
                               int fbW, int fbH,
                               shim::IGraphicsDevice& device,
                               render::Surface** outFrame,
                               const CityFramePalette& pal) {
    CityFrameStats st{};
    if (fbW <= 0 || fbH <= 0) return st;

    // One 32bpp ARGB target (the natural readback format; gui::Argb8888 channel map).
    render::Surface* frame = render::SurfaceCreate(fbW, fbH, 32, gui::Argb8888());
    if (!frame) return st;

    st = ComposeCityFrame(frame, camera, scene, pal);

    // Present the finished frame through the REAL present dispatch. The Lock+Blt
    // mode (mode 2) Lock()s the device backbuffer and qmemcpy's the software
    // framebuffer into it (exactly the engine's ppvBits copy), then present()s. The
    // device must be init()'d to fbW x fbH x 32bpp so the flat copy lands 1:1.
    render::PresentState ps{};
    ps.framebuffer = frame->pixels;
    ps.copyBytes   = frame->pitch * frame->height;
    ps.width       = fbW;
    ps.height      = fbH;
    st.presented = render::PresentFrame(device, render::PresentMode::DDrawLockBlt, ps);
    st.presentCount = 1;   // one frame presented this call

    if (outFrame) *outFrame = frame;
    else render::SurfaceDestroy(frame);
    return st;
}

} // namespace guild::play

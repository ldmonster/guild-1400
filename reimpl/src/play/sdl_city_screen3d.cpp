// guild::play — native 3D CHOOSECITY screen. See sdl_city_screen3d.h.
#include "play/sdl_city_screen3d.h"

#include "io/archive_mount.h"
#include "play/city_info.h"
#include "play/menu_assets.h"
#include "play/real_texture_source.h"
#include "play/scene_view.h"
#include "render/font.h"
#include "render/perf_overlay.h"
#include "render/surface.h"
#include "render/bmp.h"
#include "render/text_raster.h"
#include "render/text_cp1251.h"
#include "render/types.h"
#include "shim/IFileSystem.h"
#include "shim/IGraphicsDevice.h"
#include "shim/IPlatform.h"
#include "shim_impl/disk_filesystem.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace guild::play {
namespace {

constexpr int kVkEscape = 0x1B;
constexpr int kVkReturn = 0x0D;

std::string Upper(std::string s) {
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

const std::uint8_t* GlyphMap() {
    static std::uint8_t table[256];
    static bool init = false;
    if (!init) { render::FontInitGlyphTable(table); init = true; }
    return table;
}

// Draw a string onto a 32bpp render::Surface (same setup sdl_city_screen uses).
void DrawLabel(render::Surface& s, int x, int y, const char* text,
               std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    if (!text || !*text) return;
    render::PresentGlobals pg;
    pg.mode         = render::PresentBackend::DDrawLockBlt;
    pg.ppvBits      = reinterpret_cast<std::uintptr_t>(s.pixels);
    pg.dibPitch     = s.pitch;
    pg.dibStride    = s.widthPx;
    pg.screenHeight = s.height;
    pg.pitchExtra   = 4;
    pg.lockBitDepth = 32;
    pg.primary      = nullptr;
    render::DrawText(x, y, reinterpret_cast<const std::uint8_t*>(text), r, g, b,
                     GlyphMap(), pg, s.fmt);
}

// Copy a 32bpp render::Surface to the device backbuffer (16/32bpp).
void BlitSurfaceToDevice(render::Surface* src, shim::IGraphicsDevice& dev) {
    shim::Surface* bb = dev.backbuffer();
    if (!bb || !bb->pixels || !src || !src->pixels) return;
    const int srcW = src->widthPx ? src->widthPx : src->width;
    const int W = std::min(bb->width, srcW), H = std::min(bb->height, src->height);
    auto* base = static_cast<std::uint8_t*>(bb->pixels);
    for (int y = 0; y < H; ++y) {
        const auto* s = reinterpret_cast<const std::uint32_t*>(
            static_cast<const std::uint8_t*>(src->pixels) + (std::size_t)y * src->pitch);
        if (bb->bpp == 32) {
            std::memcpy(base + (std::size_t)y * bb->pitch, s, (std::size_t)W * 4);
        } else if (bb->bpp == 16) {
            auto* row = reinterpret_cast<std::uint16_t*>(base + (std::size_t)y * bb->pitch);
            for (int x = 0; x < W; ++x) {
                const std::uint32_t c = s[x];
                const std::uint32_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, bl = c & 0xFF;
                row[x] = (std::uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (bl >> 3));
            }
        }
    }
}

} // namespace

// Render ONE settled frame of the New-Game desk scene (Menu/ChooseCity.ed3 at the A2
// pick camera) into `outFb` (W x H, 32bpp). No city pick markers / tower / camera
// flight — just the desk + map, the shared backdrop the New-Game sub-screens (difficulty
// / history / character) overlay their own parchment form onto. Returns false if the
// scene/textures/device render are unavailable (caller falls back to a flat backdrop).
bool RenderNewGameDeskBackdrop(shim::IGraphicsDevice& device, const std::string& gameDir,
                               int W, int H, render::Surface* outFb, const char* camDummy) {
    if (gameDir.empty() || !outFb) return false;
    shim::DiskFileSystem fs(gameDir);
    io::ArchiveMount scenes, objects;
    std::vector<u8> ed3;
    if (!scenes.Mount(&fs, "Resources/scenes.BIN", /*caseInsensitive=*/true) ||
        !objects.Mount(&fs, "Resources/Objects.BIN", /*caseInsensitive=*/true) ||
        !scenes.OpenMember("Menu/ChooseCity.ed3", ed3) || ed3.empty())
        return false;
    std::vector<SceneObjectInst> scene = ParseSceneObjects(ed3.data(), ed3.size());
    if (scene.size() < 5) return false;

    RealTextureSource tex;
    const bool haveTex = fs.exists("Resources/Textures.BIN") &&
                         tex.Mount(&fs, "Resources/Textures.BIN");

    PerspCamera cam;                                   // settled camera (A2 = map desk, B2 = shelf)
    if (const char* e = std::getenv("GUILD_CAM_DUMMY")) camDummy = e;  // investigation override
    if (!BuildSceneCamera(ed3.data(), ed3.size(), scene, camDummy, cam) &&
        !BuildSceneCamera(ed3.data(), ed3.size(), scene, "dummy_A1", cam)) {
        cam.eye[0] = -90; cam.eye[1] = 78; cam.eye[2] = 172;
        cam.target[0] = -90; cam.target[1] = 40; cam.target[2] = 240;
        cam.engineProjection = true;
    }

    PerspRenderOptions ropt;
    ropt.clearFirst = true; ropt.clearR = 0x10; ropt.clearG = 0x12; ropt.clearB = 0x20;
    {
        render::SkyAmbient amb = ComputeSceneAmbient(ed3.data(), ed3.size(), 0, 0.0f, 1.0f);
        ropt.bakedLighting = true;
        ropt.ambientRGB[0] = amb.r; ropt.ambientRGB[1] = amb.g; ropt.ambientRGB[2] = amb.b;
    }
    ropt.shadows = true; ropt.bilinear = true; ropt.backfaceCull = 1;

    constexpr int kSS = 2;
    const int W2 = W * kSS, H2 = H * kSS;
    render::Surface* ssFb = render::SurfaceCreate(W2, H2, 32);
    render::Scene3DDrawList dl =
        BuildSceneDrawList(scene, objects, cam, W2, H2, ropt, haveTex ? &tex : nullptr);
    dl.geometryId = 1;
    UpdateSceneDrawListCamera(dl, cam, W2, H2);
    bool ok = true;
    if (!device.renderScene3D(dl, outFb)) {            // CPU fallback: rasterise 2x -> downsample
        if (!ssFb) { ok = false; }
        else {
            render::RasterizeDrawList(dl, ssFb);
            for (int y = 0; y < H; ++y) {
                auto* d = reinterpret_cast<std::uint32_t*>(
                    static_cast<std::uint8_t*>(outFb->pixels) + (std::size_t)y * outFb->pitch);
                for (int x = 0; x < W; ++x) {
                    int r = 0, g = 0, b = 0;
                    for (int j = 0; j < kSS; ++j) {
                        auto* s = reinterpret_cast<std::uint32_t*>(
                            static_cast<std::uint8_t*>(ssFb->pixels) + (std::size_t)(y * kSS + j) * ssFb->pitch);
                        for (int i = 0; i < kSS; ++i) {
                            const std::uint32_t p = s[x * kSS + i];
                            r += (p >> 16) & 0xFF; g += (p >> 8) & 0xFF; b += p & 0xFF;
                        }
                    }
                    const int n = kSS * kSS;
                    d[x] = 0xFF000000u | ((std::uint32_t)(r / n) << 16) | ((std::uint32_t)(g / n) << 8) | (b / n);
                }
            }
        }
    }
    if (ssFb) render::SurfaceDestroy(ssFb);
    return ok;
}

CityScreenResult RunCityScreen3D(shim::IGraphicsDevice& device, shim::IPlatform& plat,
                                 const CityScreenConfig& cfg) {
    CityScreenResult res;
    const int W = cfg.fbW, H = cfg.fbH;

    shim::DiskFileSystem fs(cfg.gameDir);
    io::ArchiveMount scenes, objects;
    std::vector<u8> ed3;
    std::vector<SceneObjectInst> scene;
    const bool haveScene =
        !cfg.gameDir.empty() &&
        scenes.Mount(&fs, "Resources/scenes.BIN", /*caseInsensitive=*/true) &&
        objects.Mount(&fs, "Resources/Objects.BIN", /*caseInsensitive=*/true) &&
        scenes.OpenMember("Menu/ChooseCity.ed3", ed3) && !ed3.empty();
    if (haveScene) scene = ParseSceneObjects(ed3.data(), ed3.size());

    // No 3D scene available -> the 2D list front keeps New Game working everywhere.
    if (!haveScene || scene.size() < 5)
        return RunCityScreen(device, plat, cfg);

    // VIBE_Menu_RunChooseCity @0x52e6d8 — the real ChooseCity object model:
    //   * one INVISIBLE pick-point marker per city (VIBE_Map_SpawnCityPointMarker,
    //     registered "stadt_<city>"): these are pick targets only, never drawn;
    //   * exactly ONE animated tower (VIBE_Object_AttachToUniverseNode "sp_STADTTURM"
    //     + LoadObjectAnimation "wimpel_STADTTURM.baf"): it starts on the default city
    //     and MOVES to whichever city the player picks. The tower position/rotation
    //     offsets (dword_52762C/527648 pos, dword_52763C/527658 rot) are all ZERO in
    //     the binary, so the tower sits exactly at the selected marker's position.
    std::map<int, int> objToCity;          // scene index -> cfg.cities index
    std::vector<float> cityX, cityY, cityZ; // per-cfg-city marker position (-> tower target)
    cityX.assign(cfg.cities.size(), 0.0f);
    cityY.assign(cfg.cities.size(), 0.0f);
    cityZ.assign(cfg.cities.size(), 0.0f);
    std::vector<char> cityHasMarker(cfg.cities.size(), 0);
    int firstCity = -1;                    // default city the tower starts on
    for (int ci = 0; ci < (int)cfg.cities.size(); ++ci) {
        const std::string dummy = Upper("dummy_" + cfg.cities[ci].first);
        for (const auto& d : scene) {
            if ((d.type == 2 || d.type == 3) && Upper(d.name) == dummy) {
                SceneObjectInst m;            // invisible per-city pick marker
                m.name = "stadt_" + cfg.cities[ci].first;
                m.type = 4; m.mesh = "sp_STADTTURM"; m.hasMesh = true; m.noRender = true;
                m.pos[0] = d.pos[0]; m.pos[1] = d.pos[1]; m.pos[2] = d.pos[2];
                scene.push_back(std::move(m));
                objToCity[(int)scene.size() - 1] = ci;
                cityX[ci] = d.pos[0]; cityY[ci] = d.pos[1]; cityZ[ci] = d.pos[2];
                cityHasMarker[ci] = 1;
                if (firstCity < 0) firstCity = ci;
                break;
            }
        }
    }

    // The single moving tower (drawn), seated on the default city to start.
    int towerIdx = -1;
    if (firstCity >= 0) {
        SceneObjectInst t;
        t.name = "sp_STADTTURM"; t.type = 4; t.mesh = "sp_STADTTURM"; t.hasMesh = true;
        t.noPick = true;          // the tower is drawn but is not itself a city pick target
        t.pos[0] = cityX[firstCity]; t.pos[1] = cityY[firstCity]; t.pos[2] = cityZ[firstCity];
        scene.push_back(std::move(t));
        towerIdx = (int)scene.size() - 1;
    }

    RealTextureSource tex;
    const bool haveTex = fs.exists("Resources/Textures.BIN") &&
                         tex.Mount(&fs, "Resources/Textures.BIN");

    // The REAL pick camera (BuildSceneCamera, reconstructing PointToBoneLocalSpace):
    // the cutscene ends at dummy_A2 — eye = its +92 (+144 translation), orientation =
    // the scene MegaCam look (camPos->camTarget). Fall back to A1, then a marker
    // framing only if the cutscene dummies/header are absent.
    PerspCamera cam;
    if (!BuildSceneCamera(ed3.data(), ed3.size(), scene, "dummy_A2", cam) &&
        !BuildSceneCamera(ed3.data(), ed3.size(), scene, "dummy_A1", cam)) {
        cam.eye[0] = -90; cam.eye[1] = 78; cam.eye[2] = 172;
        cam.target[0] = -90; cam.target[1] = 40; cam.target[2] = 240;
        cam.engineProjection = true;
    }

    render::Surface* fb = render::SurfaceCreate(W, H, 32);
    if (!fb) return RunCityScreen(device, plat, cfg);

    // The real per-city info window data (VIBE_Menu_RunChooseCity renders the
    // hovered city's _STADTAUSWAHL_<CITY>_BESCHR description). Loads the localized
    // text DB once; absent text just yields a caption-only panel.
    CityInfoText cityText;
    cityText.Load(&fs);
    CityInfoGfx cityGfx;
    cityGfx.Load(&fs);   // gilde.gfx parchment panel + crest + _AUSWAHL button
    // The baked-gold _FONT (the main-menu button font) for the title + info + button.
    MenuFont menuFont;
    if (!cfg.gameDir.empty()) menuFont.Load(fs, "gfx/gilde.gfx", "_FONT");
    const MenuFont* mfp = menuFont.loaded() ? &menuFont : nullptr;

    PerspRenderOptions ropt;
    ropt.clearFirst = true; ropt.clearR = 0x10; ropt.clearG = 0x12; ropt.clearB = 0x20;
    // The reconstructed engine lighting (VIBE_Light_BuildObjectCache): bake the scene's
    // light nodes (the two warm candles, the blue window light, the sun) per vertex over
    // the .ed3 band-0 ambient — the warm, candle-lit Secretariat. (The parent-composed
    // world transforms put the lights in the right place, so this is no longer dark.)
    {
        render::SkyAmbient amb = ComputeSceneAmbient(ed3.data(), ed3.size(), 0, 0.0f, 1.0f);
        ropt.bakedLighting = true;
        ropt.ambientRGB[0] = amb.r; ropt.ambientRGB[1] = amb.g; ropt.ambientRGB[2] = amb.b;
    }
    // Project grounded contact shadows for the scene props onto the surfaces they rest on.
    ropt.shadows = true;
    // Bilinear texture filtering: removes the NEAREST texel shimmer on the detailed walls/
    // floor as the cutscene camera flies (NEAREST flips texels sub-pixel -> flicker).
    ropt.bilinear = true;
    // D3D fixed-function backface cull (the engine's RS cull mode): the camera sits
    // INSIDE the enclosed room, so the near walls' back-faces must be dropped — without
    // this a single near-wall back-face paints over the whole frame (the office/table).
    ropt.backfaceCull = 1;

    // The A_Stadtwahl cutscene flight (CameraFlightEnhanced 1500 ticks): pan the camera
    // dummy_A0 -> dummy_A1 -> dummy_A2 over `introFrames`, then settle at A2 for the pick.
    const std::vector<std::string> kFlight = {"dummy_A0", "dummy_A1", "dummy_A2"};
    const int introFrames = (cfg.introFrames > 0) ? cfg.introFrames : 0;

    int selected = firstCity;   // default city the tower starts on (click moves it, confirm commits)
    int frame = 0;
    bool prevLeft = false;
    bool prevF11 = false;
    std::uint32_t lastClickMs = 0;
    int lastClickCity = -1;

    // Tower glide: the picked city slides the tower across the map via a smooth
    // object-anim (1:1 VIBE_Anim_CreateObjectAnim), not a teleport. Animate over a
    // fixed wall-clock duration so it is framerate-independent.
    bool towerGliding = false;
    float glFrom[3] = {0, 0, 0}, glTo[3] = {0, 0, 0};
    std::uint32_t glStartMs = 0;
    constexpr float kGlideMs = 500.0f;
    InfoWindowLayout infoLayout{};   // last frame's info-window + choose-button rect

    // FPS: build the static scene's draw list ONCE (the expensive mesh decode + texture
    // build + lighting bake), then per frame only re-aim the camera. `geomId` bumps when
    // the tower moves (rare, on click) so the GPU backend re-uploads only then.
    // Supersample the 3D scene 2x then downsample, to remove the camera-motion aliasing
    // shimmer (grazing-angle walls + triangle edges) that mip/bilinear alone can't (and
    // that the software Vulkan ICD has no anisotropic filtering for). The GPU path renders
    // at the draw-list resolution internally and downsamples on readback; the CPU fallback
    // rasterises into `ssFb` and we box-downsample here.
    constexpr int kSS = 2;
    const int W2 = W * kSS, H2 = H * kSS;
    render::Surface* ssFb = render::SurfaceCreate(W2, H2, 32);
    unsigned geomId = 1;
    render::Scene3DDrawList dl =
        BuildSceneDrawList(scene, objects, cam, W2, H2, ropt, haveTex ? &tex : nullptr);
    dl.geometryId = geomId;
    auto rebuildDrawList = [&]() {
        dl = BuildSceneDrawList(scene, objects, cam, W2, H2, ropt, haveTex ? &tex : nullptr);
        dl.geometryId = ++geomId;
    };
    // Box-downsample ssFb (W2xH2) into fb (WxH) for the CPU fallback path.
    auto downsample = [&]() {
        if (!ssFb) return;
        for (int y = 0; y < H; ++y) {
            auto* d = reinterpret_cast<std::uint32_t*>(
                static_cast<std::uint8_t*>(fb->pixels) + (std::size_t)y * fb->pitch);
            for (int x = 0; x < W; ++x) {
                int r = 0, g = 0, b = 0;
                for (int j = 0; j < kSS; ++j) {
                    auto* s = reinterpret_cast<std::uint32_t*>(
                        static_cast<std::uint8_t*>(ssFb->pixels) + (std::size_t)(y * kSS + j) * ssFb->pitch);
                    for (int i = 0; i < kSS; ++i) {
                        const std::uint32_t p = s[x * kSS + i];
                        r += (p >> 16) & 0xFF; g += (p >> 8) & 0xFF; b += p & 0xFF;
                    }
                }
                const int n = kSS * kSS;
                d[x] = 0xFF000000u | ((std::uint32_t)(r / n) << 16) | ((std::uint32_t)(g / n) << 8) | (b / n);
            }
        }
    };

    for (;;) {
        if (cfg.maxFrames >= 0 && frame >= cfg.maxFrames) break;

        shim::MouseState ms{};
        plat.getMouse(ms);

        // F11 cycles the in-game performance overlay (off -> FPS -> +ms -> +graph).
        const bool f11 = plat.keyDown(0x7A);
        if (f11 && !prevF11) render::GlobalPerfOverlay().CycleDetail();
        prevF11 = f11;

        // --- camera: fly the cutscene during the intro, then hold at the A2 pick view ---
        const bool intro = frame < introFrames;
        PerspCamera frameCam = cam;   // settled = the static A2 camera
        if (intro) {
            const float t = 1500.0f * (float)frame / (float)introFrames;
            CameraFlightAt(scene, kFlight, 1500.0f, t, frameCam);
        }

        // --- advance the tower glide (1:1 object-anim slide to the picked city) ---
        if (towerGliding && towerIdx >= 0) {
            float fr = (kGlideMs > 0.0f) ? (float)(plat.timeMs() - glStartMs) / kGlideMs : 1.0f;
            if (fr >= 1.0f) { fr = 1.0f; towerGliding = false; }
            float p[3];
            SampleTowerGlide(glFrom, glTo, fr, p);
            scene[towerIdx].pos[0] = p[0]; scene[towerIdx].pos[1] = p[1]; scene[towerIdx].pos[2] = p[2];
            rebuildDrawList();           // tower moved this frame -> regenerate geometry
        }

        // --- render the 3D scene ---
        // Re-aim the prebuilt draw list at this frame's camera (cheap), then render it on
        // the GPU (the Vulkan pipeline, IGraphicsDevice::renderScene3D) when the device
        // supports it; otherwise the CPU reference rasteriser (headless / portable build).
        // Both consume the SAME list, so the image is identical.
        UpdateSceneDrawListCamera(dl, frameCam, W2, H2);
        if (!device.renderScene3D(dl, fb)) {       // GPU renders 2x internally -> fb
            render::RasterizeDrawList(dl, ssFb);   // CPU: rasterise 2x then downsample
            downsample();
        }

        // --- pick the tower under the cursor (only once the flight has settled) ---
        int picked = intro ? -1 : PickNearestObject(scene, objects, frameCam, W, H,
                                       (float)ms.x, (float)ms.y);
        int hovCity = -1;
        if (picked >= 0) {
            auto it = objToCity.find(picked);
            if (it != objToCity.end()) hovCity = it->second;
        }
        res.hoveredItem = hovCity;

        // The info window (VIBE_Menu_RunChooseCity), 1:1: the real _PERGAMENT_MB
        // parchment panel at the form's bottom-centre rect, the city's _STADTWAPPEN
        // crest, the _STADTAUSWAHL_<CITY>_BESCHR description, and the _AUSWAHL choose
        // button (gfx 1210). Shows the hovered (else selected) city. The returned
        // button rect is the confirm hit-target (dword_75BF38 == 1210).
        const int infoCity = (hovCity >= 0) ? hovCity : selected;
        if (infoCity >= 0 && infoCity < (int)cfg.cities.size())
            infoLayout = RenderCityInfoWindow(fb, W, H, cityText, cityGfx,
                                              cfg.cities[infoCity].first, mfp);

        // Top title bar — a DARK semi-transparent box (same width/x as the bottom
        // info box: 548 wide at x=120, frida) with the screen title (_M0_STADT+0)
        // centered in GOLD (the same gold as the button captions).
        {
            const float sx = W / 800.0f, sy = H / 600.0f;
            const int bx0 = (int)(120 * sx), by0 = (int)(8 * sy);
            const int bw0 = (int)(548 * sx), bh0 = (int)(38 * sy);
            const int rw = fb->widthPx ? fb->widthPx : fb->width;
            for (int yy = by0; yy < by0 + bh0; ++yy) {
                if (yy < 0 || yy >= fb->height) continue;
                auto* row = reinterpret_cast<std::uint32_t*>(
                    static_cast<std::uint8_t*>(fb->pixels) + (std::size_t)yy * fb->pitch);
                for (int xx = bx0; xx < bx0 + bw0; ++xx) {
                    if (xx < 0 || xx >= rw) continue;
                    const std::uint32_t c = row[xx];
                    const int r = (((c >> 16) & 0xFF) * 72) / 256 + (18 * 184) / 256;
                    const int g = (((c >> 8)  & 0xFF) * 72) / 256 + (14 * 184) / 256;
                    const int b = (((c)       & 0xFF) * 72) / 256 + (10 * 184) / 256;
                    row[xx] = 0xFF000000u | ((std::uint32_t)r << 16) | ((std::uint32_t)g << 8) | (std::uint32_t)b;
                }
            }
            const std::string title = cityText.ScreenTitle();
            if (!title.empty()) {
                if (mfp) {
                    const int tw = mfp->MeasureWidth(title.c_str());
                    const int th = mfp->lineHeight() > 0 ? mfp->lineHeight() : 17;
                    mfp->DrawText(reinterpret_cast<std::uint32_t*>(fb->pixels),
                                  fb->widthPx ? fb->widthPx : fb->width, fb->height,
                                  bx0 + (bw0 - tw) / 2, by0 + (bh0 - th) / 2, title.c_str(),
                                  1, 0, 0, 0, /*modulate=*/false);   // baked gold
                } else {
                    const int estW = (int)title.size() * 6;
                    render::DrawTextCp1251(fb, bx0 + (bw0 - estW) / 2, by0 + (bh0 - 7) / 2, title.c_str(), 235, 200, 100);
                }
            }
        }

        // In-game performance overlay (Steam-style FPS/frametime), composited last so
        // it sits on top of the scene + HUD. Off unless GUILD_PERF_OVERLAY / F11.
        render::GlobalPerfOverlay().Frame(plat.timeMs());
        render::GlobalPerfOverlay().Draw(fb);

        // One-shot framebuffer dump for visual verification (GUILD_CITY_DUMP=path).
        // Fires only after GUILD_CITY_DUMP_AT frames (default 300 ≈ 5 s at 60 fps),
        // so the intro/camera animation has played out before the snapshot.
        if (const char* mp = std::getenv("GUILD_CITY_DUMP")) {
            static bool s_dumped = false;
            int dumpAt = 300;
            if (const char* da = std::getenv("GUILD_CITY_DUMP_AT")) { int v = std::atoi(da); if (v > 0) dumpAt = v; }
            if (!s_dumped && res.framesPresented >= dumpAt) {
                s_dumped = true;
                std::vector<std::uint8_t> rgb((std::size_t)W * H * 3);
                const std::uint32_t* px = reinterpret_cast<const std::uint32_t*>(fb->pixels);
                for (std::size_t i = 0; i < (std::size_t)W * H; ++i) {
                    const std::uint32_t c = px[i];
                    rgb[i*3] = (c>>16)&0xFF; rgb[i*3+1] = (c>>8)&0xFF; rgb[i*3+2] = c&0xFF;
                }
                std::vector<std::uint8_t> bmp = render::BmpSave24Bit(W, H, rgb.data());
                if (FILE* f = std::fopen(mp, "wb")) { std::fwrite(bmp.data(), 1, bmp.size(), f); std::fclose(f); }
            }
        }

        BlitSurfaceToDevice(fb, device);
        device.present();
        ++res.framesPresented;

        // --- input ---
        if (!plat.pumpMessages()) { res.back = true; res.backByWindow = true; break; }
        plat.getMouse(ms);
        const bool leftEdge = ms.left && !prevLeft;
        prevLeft = ms.left;

        // During the intro flight, a click/Enter/Esc SKIPS to the settled pick view
        // (it does not select/cancel); otherwise the flight just advances a frame.
        if (intro) {
            if (leftEdge || plat.keyDown(kVkEscape) || plat.keyDown(kVkReturn))
                frame = introFrames;       // jump to the settled A2 pick view
            else
                ++frame;
            if (cfg.frameCapMs > 0) plat.sleepMs((std::uint32_t)cfg.frameCapMs);
            continue;
        }

        if (plat.keyDown(kVkEscape)) { res.back = true; res.backByEsc = true; break; }
        if (plat.keyDown(kVkReturn) && selected >= 0) {
            res.confirmed = true; res.cityIndex = selected;
            res.cityName = cfg.cities[selected].first;
            res.cityPath = cfg.cities[selected].second;
            break;
        }

        // The _AUSWAHL choose button (dword_75BF38 == 1210): clicking it confirms the
        // currently selected city, the same as Enter.
        if (leftEdge && infoLayout.valid && selected >= 0 &&
            ms.x >= infoLayout.btnX && ms.x < infoLayout.btnX + infoLayout.btnW &&
            ms.y >= infoLayout.btnY && ms.y < infoLayout.btnY + infoLayout.btnH) {
            res.confirmed = true; res.cityIndex = selected;
            res.cityName = cfg.cities[selected].first;
            res.cityPath = cfg.cities[selected].second;
            break;
        }

        if (leftEdge) {
            // Recompute the pick on the click frame's cursor.
            int clickPick = PickNearestObject(scene, objects, cam, W, H,
                                              (float)ms.x, (float)ms.y);
            int clickCity = -1;
            if (clickPick >= 0) {
                auto it = objToCity.find(clickPick);
                if (it != objToCity.end()) clickCity = it->second;
            }
            if (clickCity >= 0 && cityHasMarker[clickCity]) {
                const std::uint32_t now = plat.timeMs();
                const bool dbl = (clickCity == selected) && (clickCity == lastClickCity) &&
                                 (now - lastClickMs <= 400u);
                // Start the tower's smooth slide from its current spot to the picked
                // city (1:1: VIBE_Anim_CreateObjectAnim object-anim, not a teleport).
                if (towerIdx >= 0 && clickCity != selected) {
                    glFrom[0] = scene[towerIdx].pos[0]; glFrom[1] = scene[towerIdx].pos[1]; glFrom[2] = scene[towerIdx].pos[2];
                    glTo[0] = cityX[clickCity]; glTo[1] = cityY[clickCity]; glTo[2] = cityZ[clickCity];
                    glStartMs = now; towerGliding = true;
                }
                selected = clickCity;
                lastClickCity = clickCity;
                lastClickMs = now;
                if (dbl) {
                    res.confirmed = true; res.cityIndex = clickCity;
                    res.cityName = cfg.cities[clickCity].first;
                    res.cityPath = cfg.cities[clickCity].second;
                    break;
                }
            }
        }

        if (cfg.frameCapMs > 0) plat.sleepMs((std::uint32_t)cfg.frameCapMs);
        ++frame;
    }

    if (ssFb) render::SurfaceDestroy(ssFb);
    render::SurfaceDestroy(fb);
    return res;
}

} // namespace guild::play

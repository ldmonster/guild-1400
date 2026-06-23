// guild::play — native 3D CHOOSECITY screen. See sdl_city_screen3d.h.
#include "play/sdl_city_screen3d.h"

#include "io/archive_mount.h"
#include "play/city_info.h"
#include "play/real_texture_source.h"
#include "play/scene_view.h"
#include "render/font.h"
#include "render/perf_overlay.h"
#include "render/surface.h"
#include "render/text_raster.h"
#include "render/types.h"
#include "shim/IFileSystem.h"
#include "shim/IGraphicsDevice.h"
#include "shim/IPlatform.h"
#include "shim_impl/disk_filesystem.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
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
                                              cfg.cities[infoCity].first);

        // In-game performance overlay (Steam-style FPS/frametime), composited last so
        // it sits on top of the scene + HUD. Off unless GUILD_PERF_OVERLAY / F11.
        render::GlobalPerfOverlay().Frame(plat.timeMs());
        render::GlobalPerfOverlay().Draw(fb);

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

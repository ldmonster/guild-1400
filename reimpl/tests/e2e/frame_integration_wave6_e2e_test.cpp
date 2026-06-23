// =============================================================================
// WAVE-6 W6-INTEGRATE — the full world-entity render frame, wired into the live
// CityView3D universe frame in BeginUniverseFrame @0x5b3900's exact order.
//
// GUARDED real-asset e2e over the WHOLE REAL AUGSBURG city: loads the city, binds
// the live objects to their scene nodes, and renders frames with the wave-6
// Options ON, asserting EACH feature contributes:
//   * SKY      — the time-of-day backdrop fills the surface (RenderSky @0x5b3953)
//   * SUN      — ComputeSunState reports the day/night band + brightness
//   * LIGHT    — dynamicLight re-lights objects (day vs night frame differs)
//   * SHADOWS  — drop shadows splat darkened pixels (sun above horizon)
//   * FOG      — per-pixel span fog blends the textured frame toward the sky colour
//   * LOD      — per-distance node LOD pick runs per object
//   * WEATHER  — the snow/rain overlay draws in winter
// and that the FULL-SCENE preset frame DIFFERS from the bare preset (all features
// off) and is DETERMINISTIC across reruns.
//
// Clean skip when the real game dir is absent (GUILD_GAME_DIR).
// =============================================================================
#include "test.h"

#include "app/wiring.h"
#include "io/save_world_load.h"
#include "play/city_view3d.h"
#include "render/surface.h"
#include "shim_impl/disk_filesystem.h"
#include "sim/entity.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace guild;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/guild-1400/reimpl/europe_guild_1400_original";
}

bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Gilde.INI") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty") &&
           fs.exists("Resources/scenes.BIN") &&
           fs.exists("Resources/Objects.BIN");
}

std::vector<u8> Snapshot(render::Surface* s) {
    std::vector<u8> out;
    if (!s) return out;
    for (int y = 0; y < s->height; ++y)
        for (int x = 0; x < s->width; ++x) {
            u8 px[3];
            render::SurfaceGetPixelRgb(s, x, y, px);
            out.push_back(px[0]); out.push_back(px[1]); out.push_back(px[2]);
        }
    return out;
}

bool LoadAugsburg(shim::DiskFileSystem& fs, play::CityView3D& view) {
    app::RealGameAssets assets = app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI");
    if (!assets.vfsBound) return false;
    sim::ResetEntityArrays();
    io::WorldState world{};
    std::vector<u8> sceneBlob;
    if (!io::LoadWorldEx("Resources/gamedata/Cities/AUGSBURG.cty", world, &sceneBlob))
        return false;
    if (!view.Init(&fs) || !view.mounted()) return false;
    if (!view.LoadCityFromWorld(sceneBlob)) return false;
    view.BindWorldObjects();
    return !view.instances().empty();
}

} // namespace

// ---------------------------------------------------------------------------
TEST(FrameIntegrationWave6E2E, FullSceneFeaturesContribute) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] FrameIntegrationWave6E2E.FullSceneFeaturesContribute: "
                    "real game dir absent (%s)\n", GameDir().c_str());
        CHECK(true);
        return;
    }
    shim::DiskFileSystem fs(GameDir());
    play::CityView3D view;
    CHECK(LoadAugsburg(fs, view));

    play::CityCamera3D cam = view.OverviewCamera();

    // --- BARE preset: all wave-6 features OFF (the byte-identical baseline) -----
    play::CityView3D::Options bare;
    bare.fbW = 160; bare.fbH = 120;
    bare.worldDay = 0; bare.worldHour = 12; bare.worldMinute = 0;  // noon (day)
    play::CityView3D::Result rb = view.RenderFrame(cam, bare);
    std::vector<u8> bareFrame = Snapshot(view.surface());
    CHECK(!rb.skyDrawn);
    CHECK(rb.sunBand < 0);
    CHECK(!rb.fogApplied);
    CHECK(rb.shadowPixels == 0);
    CHECK(rb.nonClearPixels > 0);   // objects do draw

    // --- FULL preset: every wave-6 feature ON ----------------------------------
    play::CityView3D::Options full = bare;
    full.sky = true; full.dynamicLight = true; full.shadows = true;
    full.fog = true; full.lodSelect = true; full.worldSprites = true;
    full.particles = true; full.mirror = true; full.weather = true;
    full.terrain = view.hasGround(); full.water = view.hasGround();
    play::CityView3D::Result rf = view.RenderFrame(cam, full);
    std::vector<u8> fullFrame = Snapshot(view.surface());
    std::printf("[w6-e2e] sun band=%d brightness=%d sky=%d skyColor=%06X "
                "litObj=%d lodObj=%d shadowCasters=%d shadowPx=%d fog=%d\n",
                rf.sunBand, rf.sunBrightness, (int)rf.skyDrawn, rf.skyColor,
                rf.litObjects, rf.lodObjects, rf.shadowCasters, rf.shadowPixels,
                (int)rf.fogApplied);

    // SUN: noon -> a daytime band (0..2) with positive brightness.
    CHECK(rf.sunBand >= 0);
    CHECK(rf.sunBrightness > 0);
    // SKY: the backdrop was drawn (a time-of-day colour).
    CHECK(rf.skyDrawn);
    // LIGHT: objects were re-lit; LOD: every drawable evaluated the LOD pick.
    CHECK(rf.litObjects > 0);
    CHECK(rf.lodObjects > 0);
    // SHADOWS: the sun is above the horizon at noon -> casters splat shadow pixels.
    CHECK(rf.shadowCasters > 0);
    CHECK(rf.shadowPixels > 0);
    // FOG: enabled this frame.
    CHECK(rf.fogApplied);

    // The full-scene frame DIFFERS from the bare preset (features contributed).
    CHECK(bareFrame.size() == fullFrame.size());
    int diff = 0;
    for (std::size_t i = 0; i < bareFrame.size(); ++i)
        if (bareFrame[i] != fullFrame[i]) ++diff;
    std::printf("[w6-e2e] full-vs-bare differing channels=%d / %zu\n",
                diff, bareFrame.size());
    CHECK(diff > 0);

    // DETERMINISM: the geometry features (sky/light/shadow/fog/lod) are a pure
    // function of the inputs — re-rendering the same preset (weather OFF so no
    // animated overlay advances) yields a BYTE-IDENTICAL frame.
    play::CityView3D::Options det = full;
    det.weather = false; det.particles = false; det.mirror = false;
    std::vector<u8> a, b;
    (void)view.RenderFrame(cam, det); a = Snapshot(view.surface());
    (void)view.RenderFrame(cam, det); b = Snapshot(view.surface());
    CHECK(a == b);
}

// ---------------------------------------------------------------------------
TEST(FrameIntegrationWave6E2E, DayNightLightingDiffers) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] FrameIntegrationWave6E2E.DayNightLightingDiffers: "
                    "real game dir absent\n");
        CHECK(true);
        return;
    }
    shim::DiskFileSystem fs(GameDir());
    play::CityView3D view;
    CHECK(LoadAugsburg(fs, view));
    play::CityCamera3D cam = view.OverviewCamera();

    play::CityView3D::Options day;
    day.fbW = 160; day.fbH = 120;
    day.sky = true; day.dynamicLight = true;
    day.worldDay = 0; day.worldHour = 12; day.worldMinute = 0;   // noon
    play::CityView3D::Result rd = view.RenderFrame(cam, day);
    std::vector<u8> dayFrame = Snapshot(view.surface());

    play::CityView3D::Options night = day;
    night.worldHour = 2; night.worldMinute = 0;                   // 02:00 (night)
    play::CityView3D::Result rn = view.RenderFrame(cam, night);
    std::vector<u8> nightFrame = Snapshot(view.surface());

    std::printf("[w6-e2e] day brightness=%d band=%d  night brightness=%d band=%d\n",
                rd.sunBrightness, rd.sunBand, rn.sunBrightness, rn.sunBand);
    // Noon is brighter than 02:00 (the genuine UpdateBrightness 0..600 ramp).
    CHECK(rd.sunBrightness > rn.sunBrightness);
    // The day/night frames differ (sky colour + ambient scale change).
    CHECK(dayFrame.size() == nightFrame.size());
    int diff = 0;
    for (std::size_t i = 0; i < dayFrame.size(); ++i)
        if (dayFrame[i] != nightFrame[i]) ++diff;
    CHECK(diff > 0);
}

// ---------------------------------------------------------------------------
TEST(FrameIntegrationWave6E2E, WeatherOverlayDrawsInWinter) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] FrameIntegrationWave6E2E.WeatherOverlayDrawsInWinter: "
                    "real game dir absent\n");
        CHECK(true);
        return;
    }
    shim::DiskFileSystem fs(GameDir());
    play::CityView3D view;
    CHECK(LoadAugsburg(fs, view));
    play::CityCamera3D cam = view.OverviewCamera();

    play::CityView3D::Options opt;
    opt.fbW = 160; opt.fbH = 120;
    opt.weather = true;
    opt.worldDay = 3;        // day % 4 == 3 -> winter (snow path)
    opt.worldHour = 12;
    // Animate several frames so the seeded flake field projects into the viewport.
    int maxDrops = 0;
    for (int f = 0; f < 8; ++f) {
        play::CityView3D::Result r = view.RenderFrame(cam, opt);
        if (r.weatherDrops > maxDrops) maxDrops = r.weatherDrops;
    }
    std::printf("[w6-e2e] winter weather max drops=%d\n", maxDrops);
    CHECK(maxDrops > 0);   // the snow field drew flakes
}

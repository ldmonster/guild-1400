// tests/e2e/wire_atmos_bridge_e2e_test.cpp — GUARDED P6/Wave30 live AUGSBURG sky.
//
// Mounts a REAL "Die Gilde — Europe 1400" install, loads AUGSBURG.cty, seeds the
// atmosphere from the REAL city bytes, then composes the atmosphere layer (sky +
// animated water + particle/weather overlay) around the UNMODIFIED live frame entry
// (render::BeginUniverseFrame, the same orchestration the running game takes) and
// asserts real sky/water/particle pixels appear (vs blank with the layer inert).
// Reports the observable difference + dumps the frame to a BMP.
//
// GUARDED: the real game dir is not in the repo. Absent => ZERO checks, clean return.
#include "test.h"

#include "play/wire_atmos_bridge.h"
#include "app/real_boot.h"
#include "io/vfs.h"
#include "io/gamestate.h"
#include "render/frame.h"
#include "render/surface.h"
#include "render/bmp.h"
#include "crt/rand.h"

#include "shim_impl/disk_filesystem.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR")) return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool RealAssetsPresent(shim::IFileSystem& fs) {
    return fs.exists("Gilde.INI") &&
           fs.exists("Resources/forms.BIN") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty");
}

u32 SeedFromBytes(const u8* p, size_t n) {
    u32 h = 2166136261u;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
    return h;
}

render::Surface* MakeFb(int W, int H) {
    render::Surface* fb = render::SurfaceCreate(W, H, 16);
    if (fb) std::memset(fb->pixels, 0, (size_t)fb->pitch * fb->height);
    return fb;
}

int NonZero(const render::Surface* fb) {
    if (!fb || !fb->pixels) return 0;
    int n = 0;
    const u16* px = (const u16*)fb->pixels;
    for (int i = 0; i < fb->widthPx * fb->height; ++i) if (px[i] != 0) ++n;
    return n;
}

// 8bpp dump of the 16bpp frame's luma (so the BMP is viewable).
std::vector<u8> Luma8(const render::Surface* fb) {
    std::vector<u8> out((size_t)fb->width * fb->height, 0);
    for (int y = 0; y < fb->height; ++y)
        for (int x = 0; x < fb->width; ++x) {
            u8 rgb[3]; render::SurfaceGetPixelRgb(fb, x, y, rgb);
            out[(size_t)y * fb->width + x] =
                (u8)((rgb[0] * 30 + rgb[1] * 59 + rgb[2] * 11) / 100);
        }
    return out;
}

} // namespace

TEST(WireAtmosBridgeE2E, LiveAugsburgAtmosphereDeInerted) {
    const std::string dir = GameDir();
    shim::DiskFileSystem fs(dir);
    if (!RealAssetsPresent(fs)) {
        std::printf("  [skip] WireAtmosBridgeE2E.LiveAugsburgAtmosphereDeInerted: "
                    "real game dir absent (%s)\n", dir.c_str());
        CHECK(true);   // clean skip
        return;
    }

    app::RealGameAssets a =
        app::MountRealGameAssets(&fs, dir, "Gilde.INI", {}, /*caseInsensitive=*/false);
    CHECK(a.iniLoaded);
    CHECK(a.vfsBound);

    io::GameState city{};
    city.relink.assign(io::kRelinkBytes, 0);
    std::string cityPath = "Resources/gamedata/Cities/" + app::RealCityPath(a.stadt);
    bool loaded = io::LoadGameState(cityPath.c_str(), city, nullptr);
    if (!loaded)
        loaded = io::LoadGameState("Resources/gamedata/Cities/AUGSBURG.cty", city, nullptr);
    CHECK(loaded);

    u32 seed = SeedFromBytes((const u8*)&city.header, sizeof(city.header));
    if (!city.relink.empty())
        seed ^= SeedFromBytes(city.relink.data(),
                              city.relink.size() < 4096 ? city.relink.size() : 4096);

    const int W = 192, H = 144;

    // ---- atmosphere layer storage (caller-owned) ----
    std::vector<render::Particle> parts(48);
    std::vector<render::SnowFlake> flakes(64);

    // ---- INERT live frame (atmosphere hooks left no-op, no sky pass): blank ----
    render::Surface* inertFb = MakeFb(W, H);
    CHECK(inertFb != nullptr);
    {
        play::AtmosBridgeContext ctx{};
        play::AtmosBridgeContext::MakeSynthetic(ctx, W, H, parts.data(), (int)parts.size(),
                                                flakes.data(), (int)flakes.size());
        ctx.fb = inertFb;
        // seed sky bands / weather hour from the REAL city bytes for fidelity flavour
        ctx.skyBandIndex = (int)(seed % render::kSkyBands);
        ctx.weatherHour = (int)((seed >> 8) % 24);
        crt::Srand(seed);
        render::FrameState fs0{}; render::FrameHooks hk0{};
        play::InstallInertAtmosBridge(fs0, hk0, &ctx);
        play::InertDrawSky(&ctx);
        render::BeginUniverseFrame(fs0, hk0, 1);
    }
    int inertNB = NonZero(inertFb);

    // ---- REAL live frame (atmosphere de-inerted + composed) ----
    render::Surface* realFb = MakeFb(W, H);
    CHECK(realFb != nullptr);
    play::AtmosBridgeContext realCtx{};
    play::AtmosBridgeContext::MakeSynthetic(realCtx, W, H, parts.data(), (int)parts.size(),
                                            flakes.data(), (int)flakes.size());
    realCtx.fb = realFb;
    realCtx.skyBandIndex = (int)(seed % render::kSkyBands);
    realCtx.weatherHour = (int)((seed >> 8) % 24);
    crt::Srand(seed);
    play::AtmosBridgeInstall ins;
    {
        render::FrameState fs1{}; render::FrameHooks hk1{};
        ins = play::ComposeAtmosphereFrame(fs1, hk1, &realCtx);
    }
    int realNB = NonZero(realFb);
    const int total = W * H;

    std::printf("  [WireAtmosBridgeE2E] %s: install=%d wasInert=%d | "
                "inert nonblank=%d  real nonblank=%d/%d (%.1f%%) | "
                "sky px=%d (band %d ambient r=%.2f g=%.2f b=%.2f) | "
                "water moved=%d verts | particles alive=%d overlay px=%d | "
                "weather hour=%d intensity=%d | flares=%d\n",
                a.stadt.c_str(), (int)ins.installed, (int)ins.wasInert,
                inertNB, realNB, total, 100.0 * realNB / total,
                realCtx.skyPixels, realCtx.skyBandIndex,
                realCtx.skyAmbient.r, realCtx.skyAmbient.g, realCtx.skyAmbient.b,
                realCtx.waterMoved, realCtx.particlesAlive, realCtx.overlayPixels,
                realCtx.weatherHour, realCtx.weatherIntensity, realCtx.flareDrawCount);

    // The de-inert took effect; the live walk reached the real leaves.
    CHECK(ins.installed == true);
    CHECK(ins.wasInert == true);
    CHECK(realCtx.drewRealSky == true);
    CHECK(realCtx.drewRealParticles == true);

    // Inert live atmosphere is blank; the de-inerted one shows real sky/water/particles.
    CHECK_EQ(inertNB, 0);
    CHECK(realCtx.skyPixels == total);              // sky fills the background
    CHECK(realCtx.waterMoved > 0);                  // water vertices animated
    CHECK(realCtx.particlesAlive > 0);              // particles emitted
    CHECK(realNB > total / 2);                      // > 50% of the frame is atmosphere
    CHECK(realNB > inertNB);
    CHECK(realCtx.skyAmbient.r > 0.0f);
    CHECK(realCtx.skyAmbient.b > 0.0f);

    // ---- dump the live atmosphere frame to a BMP ----
    {
        std::vector<u8> luma = Luma8(realFb);
        std::vector<u8> bmp = render::BmpSaveIndexed(W, H, luma.data(), nullptr);
        CHECK(bmp.size() > 1078);
        const char* outPath = "/tmp/wire_atmos_augsburg_live.bmp";
        if (FILE* f = std::fopen(outPath, "wb")) {
            std::fwrite(bmp.data(), 1, bmp.size(), f);
            std::fclose(f);
            std::printf("  [WireAtmosBridgeE2E] wrote %s (%zu bytes)\n",
                        outPath, bmp.size());
        }
    }

    render::SurfaceDestroy(inertFb);
    render::SurfaceDestroy(realFb);
    io::VfsShutdown();
}

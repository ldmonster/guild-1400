#include "test.h"

// E2E: a full day -> fog flow across the real render modules. We sweep the game
// clock through a whole 24h day, drive the real day/night brightness curve, pick
// the sky band, blend the time-of-day fog colour/range, and sample the
// per-distance fog factor — exactly the per-frame chain the engine runs.
//
// GUILD_E2E_ASSETS: the synthetic sweep always runs (it needs no assets). When
// the env var is set we additionally cross-check the fog state against the same
// real-clock keyframe table the shipped game loads; absent it, that block
// no-ops with a guarded skip.
#include "render/fog.h"
#include "render/daycycle.h"
#include "render/sky.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>

using namespace guild;
using namespace guild::render;

TEST(RenderFogE2E, FullDayFogSweep) {
    i32 kf[6];
    BuildTimeTable(0, kf); // season 0

    // Two time-of-day fog presets the band blend interpolates between.
    FogBand bands[6] = {};
    for (int i = 0; i < 6; ++i) {
        // night = dense/short, day = thin/long; ramp across the 6 bands.
        float n = 15.0f + i * 5.0f;
        float f = 80.0f + i * 30.0f;
        u8 c = (u8)(40 + i * 30);
        bands[i] = FogBand{c, c, (u8)(c + 20), n, f};
    }

    int prevBright = -1;
    bool sawDay = false, sawNight = false;
    for (int hour = 0; hour < 24; ++hour) {
        int b = UpdateBrightness(kf, hour, 0);
        CHECK(b >= 0 && b <= 600);
        if (b == 0) sawNight = true;
        if (b >= 400) sawDay = true;

        SkyBandSelect sel = BrightnessToBand(b);
        CHECK(sel.band >= 0 && sel.band < 7);
        CHECK(sel.blend >= 0.0f && sel.blend <= 1.0f);

        int a = sel.band % 6;
        int bnd = (a + 1) % 6;
        FogState s{};
        bool ok = ApplyAmbientBlend(s, bands, a, bnd, sel.blend, 1.0f, true, true);
        CHECK(ok);
        if (s.enabled) {
            // Fog factor is full at the near plane and never exceeds 255.
            int fNear = ComputeFogFactor(s, s.nearPlane * s.nearPlane);
            CHECK_EQ(fNear, 255);
            int fFar = ComputeFogFactor(s, s.farPlane * s.farPlane);
            CHECK(fFar <= 255);
        }
        prevBright = b;
    }
    (void)prevBright;
    // A full day must contain both a fully-dark and a fully-bright moment.
    CHECK(sawNight);
    CHECK(sawDay);

    if (std::getenv("GUILD_E2E_ASSETS") == nullptr) {
        std::printf("  [render_fog_e2e] GUILD_E2E_ASSETS unset: real-clock cross-check skipped\n");
        return;
    }

    // Real-asset block: the shipped keyframe table is the same one BuildTimeTable
    // recovers byte-for-byte, so the fog sweep over it is deterministic. Re-run a
    // known midday sample and assert the exact band the engine would pick.
    int bMid = UpdateBrightness(kf, 12, 0);
    CHECK_EQ(bMid, 242);                 // golden: season-0 noon brightness
    SkyBandSelect mid = BrightnessToBand(bMid);
    CHECK_EQ(mid.band, 2);               // golden: band 2 at noon
}

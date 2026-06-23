// E2E: play::SessionAtmos over full simulated days — the session atmosphere
// flow exactly as the gilde.exe frame loop drives it: new-day seed ->
// Sky_InitScene weather regen -> per-frame Weather_UpdateSky +
// DayCycle_UpdateBrightness + the render-side layer fade step, at one game
// minute per frame (the wiring.cpp dayCycleAndOutdoorMusic cadence).
#include "play/session_atmos.h"
#include "render/daycycle.h"
#include "sim/gametime.h"
#include "tests/framework/test.h"

#include <cmath>

using namespace guild;
using namespace guild::play;

// A full day at 1 minute per frame: the brightness curve must follow the
// piecewise 0..600 ramp (monotone within the day), the band must walk
// 0 -> 6 without skipping, and every frame's weather params must match the
// reconstructed cores over the generated arc.
TEST(SessionAtmosE2E, FullDaySweep) {
    SessionAtmos a{};
    a.climateRainProb[0] = 50;
    a.climateRainProb[1] = 50;

    sim::GameTime t{};
    t.day = 0;
    t.hour = 0;
    t.minute = 0;
    t.second = 0;

    u32 ms = 0;
    int prevBright = 0;
    int maxBright = 0;
    int rebuilds = 0;
    int prevBand = 0;
    bool sawSunrise = false, sawSunset = false;

    for (int frame = 0; frame < 24 * 60; ++frame) {
        ms += 16;
        a.Frame(t, /*seed=*/2026u, ms);

        // Brightness follows the real curve for this exact clock.
        CHECK_EQ(a.brightness,
                 render::UpdateBrightness(a.dayKeyframes, t.hour, t.minute));
        CHECK(a.brightness >= prevBright);   // season-0 curve is non-decreasing
        prevBright = a.brightness;
        if (a.brightness > maxBright) maxBright = a.brightness;

        // The latched band only moves at rebuilds, and only forward by 1.
        CHECK(a.band >= prevBand && a.band - prevBand <= 1);
        prevBand = a.band;
        if (a.lightingRebuilt) ++rebuilds;
        if (a.sunEvent && a.sunHeight > 0.0f) sawSunrise = true;
        if (a.sunEvent && a.sunHeight < 0.0f) sawSunset = true;

        // Weather params re-derive from the day arc each frame.
        int h = t.hour;
        CHECK_EQ(a.weatherIntensity, render::WeatherIntensity(a.day.arc, h));
        CHECK_EQ((int)a.weatherCategory,
                 (int)render::CategoryFor(a.weatherIntensity));
        CHECK_EQ(a.windSnowGrow,
                 a.snowPresent
                     ? render::SnowGrowAmount(a.day.windX[h], a.weatherIntensity)
                     : 0);
        // Wind vectors stay on the unit circle (sin/cos walk).
        float w2 = a.day.windX[h] * a.day.windX[h]
                 + a.day.windY[h] * a.day.windY[h];
        CHECK(std::fabs(w2 - 1.0f) < 1e-4f);
        // Dark-layer fade target tracks the overcast law.
        int expTarget = a.weatherIntensity
                            ? (a.weatherIntensity << 6) / 1000 + 96
                            : 0;
        CHECK_EQ((int)a.dark.fadeTarget, expTarget);
        CHECK(a.overcast >= 0.0f && a.overcast <= 1.0f);

        sim::GameTimeAdvance(&t, 0, 0, 1);   // +1 game minute per frame
    }

    CHECK_EQ(maxBright, 600);                // reached full night
    CHECK_EQ(prevBand, 6);                   // walked all 7 bands
    CHECK(rebuilds >= 7);                    // at least one rebuild per band
    CHECK(sawSunrise);
    CHECK(sawSunset);
    CHECK_EQ(a.sunPhase, 2);                 // risen then set

    // Day rollover: a new seed regenerates the weather day deterministically.
    SessionAtmos b = a;
    sim::GameTime t2 = t;                    // t is now day 1, 00:00
    CHECK_EQ(t2.day, 1);
    a.Frame(t2, 2027u, ms + 16);
    b.Frame(t2, 2027u, ms + 16);
    for (int h = 0; h < 24; ++h) {
        CHECK_EQ(a.day.arc[h], b.day.arc[h]);
        CHECK_EQ(a.day.thunder[h], b.day.thunder[h]);
    }
    // Sky_InitScene resets byte_631D9C to 0 but NOT the band latch
    // dword_631D98 (still 6): the 6 -> 0 band change at the init-tail
    // UpdateBrightness fires the day sun-height walk -> phase 1.
    CHECK_EQ(a.sunPhase, 1);
    CHECK(a.sunHeight >= 0.3f && a.sunHeight <= 0.9f);
}

// Multi-day: seasons rotate day % 4 and the keyframe table follows
// kDayKeyframeMinutes; winter (season 3) days carry snow.
TEST(SessionAtmosE2E, SeasonsAndWinterSnow) {
    SessionAtmos a{};
    for (int s = 0; s < 4; ++s) a.climateRainProb[s] = 80;

    sim::GameTime t{};
    t.hour = 12;
    t.minute = 0;
    u32 ms = 0;
    for (int day = 0; day < 8; ++day) {
        t.day = day;
        ms += 16;
        a.Frame(t, 100u + (u32)day, ms);
        int season = day % 4;
        i32 kf[6];
        render::BuildTimeTable(season, kf);
        for (int i = 0; i < 6; ++i) CHECK_EQ(a.dayKeyframes[i], kf[i]);
        if (season == 3) {
            CHECK(a.day.mode == 1 || a.day.mode == 2);
            CHECK(a.snowPresent);
            // Winter never rolls thunder.
            for (int h = 0; h < 24; ++h) CHECK_EQ(a.day.thunder[h], 0);
        } else {
            CHECK_EQ(a.day.mode, 0);
            CHECK(!a.snowPresent);
            CHECK(a.rainPresent);
        }
        // Arc values are always the banded septet.
        for (int h = 0; h < 24; ++h) {
            i32 v = a.day.arc[h];
            CHECK(v == 0 || v == 149 || v == 150 || v == 499 || v == 500
                  || v == 999 || v == 1000);
        }
    }
}

// Thunderstorm day end-to-end: a heavy hour with a thunder code eventually
// flashes; during the flash the fog application switches to rows 0 -> 2 at
// t = 1.0 and the lighting rebuild is suppressed, then restores.
TEST(SessionAtmosE2E, ThunderstormFlow) {
    SessionAtmos a{};
    a.climateRainProb[1] = 0;
    sim::GameTime t{};
    t.day = 1;
    t.hour = 15;
    t.minute = 0;

    u32 ms = 16;
    a.Frame(t, 9000u, ms);
    a.day.arc[15] = 999;       // heavy hour (runtime world data)
    a.day.thunder[15] = 2;

    bool flashed = false;
    int flashFrames = 0;
    for (int frame = 0; frame < 6000; ++frame) {
        ms += 16;
        a.ThunderTick(t, ms);  // VIBE_Weather_RenderAndThunder
        a.Frame(t, 9000u, ms);
        if (a.flashActive) {
            flashed = true;
            ++flashFrames;
            CHECK_EQ(a.fogRowA, 0);
            CHECK_EQ(a.fogRowB, 2);
            CHECK(std::fabs(a.fogBlendT - 1.0f) < 1e-6f);
            CHECK(!a.lightingRebuilt);
        }
    }
    CHECK(flashed);
    // The 8..15ms window at 16ms frames is visible for at most one frame each.
    CHECK(flashFrames >= 1);
    // After the storm window the fog returns to the overcast blend.
    CHECK_EQ(a.fogRowB, 1);
    CHECK(a.overcast > 0.0f); // the dark layer faded in over the storm
}

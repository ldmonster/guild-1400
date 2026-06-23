// Unit tests for play::SessionAtmos — the session day/night + weather
// atmosphere state machine (gilde.exe 0x4b2504 / 0x4c0040 / 0x4b1e94 /
// 0x5efca8 / 0x5ef7cc / 0x4c05ac). Golden vectors follow the recovered
// formulas; the weather-day reference is an independent re-derivation over the
// shared CRT LCG so the draw ORDER and banding are pinned.
#include "play/session_atmos.h"
#include "crt/rand.h"
#include "render/daycycle.h"
#include "tests/framework/test.h"

#include <cmath>
#include <cstring>

using namespace guild;
using namespace guild::play;

namespace {

// Reference twin of the CRT LCG draws (crt::RandNext: state*1103515245+12345,
// bits 16..30) so the test does not depend on the unit under test.
struct RefRng {
    u32 s;
    int next() {
        s = s * 1103515245u + 12345u;
        return (int)((s >> 16) & 0x7FFF);
    }
    int mod(u16 n) { return n ? next() % n : 0; }
    double fscaled() { return (double)next() * (1.0 / 32767.0); }
};

bool fnear(float a, float b, float tol = 1e-6f) {
    return std::fabs(a - b) <= tol;
}

} // namespace

// ---------------------------------------------------------------------------
// dbl_61DD30 — the wind full-circle constant is the binary 6.283185
// (0x401921FB53C8D4F1, get_bytes @0x61DD30), NOT exactly 2*pi.
// ---------------------------------------------------------------------------
TEST(SessionAtmos, WindCircleConstantBits) {
    double d = kWindFullCircle;
    u64 bits = 0;
    std::memcpy(&bits, &d, 8);
    CHECK_EQ(bits, 0x401921FB53C8D4F1ull);
}

// ---------------------------------------------------------------------------
// BuildWeatherDay vs an independent re-derivation of the 0x4b2146 draw order.
// ---------------------------------------------------------------------------
TEST(SessionAtmos, BuildWeatherDayMatchesReference) {
    const u32 seed = 1234u;
    const i32 prob = 60;

    crt::Srand(seed);
    WeatherDayState w{};
    BuildWeatherDay(w, /*season=*/0, prob);

    RefRng r{seed};
    // season != 3 -> mode 0, no roll.
    CHECK_EQ(w.mode, 0);
    for (int h = 0; h < 24; ++h) {
        i32 v = 0;
        if (r.mod(100) < prob)
            v = r.mod((u16)(10 * prob));
        i32 banded;
        if (v > 700)      banded = (v & 1) + 999;
        else if (v > 500) banded = (v & 1) + 499;
        else if (v > 250) banded = (v & 1) + 149;
        else              banded = 0;
        CHECK_EQ(w.arc[h], banded);
        i32 th = (banded >= 999) ? (i32)r.mod(3) : 0;
        CHECK_EQ(w.thunder[h], th);
        // Banding preserves the pre-band parity in the offset.
        CHECK(banded == 0 || banded == 149 || banded == 150 || banded == 499
              || banded == 500 || banded == 999 || banded == 1000);
    }
    // Wind walk: angle kept as float between iterations.
    float angle = (float)(r.fscaled() * kWindFullCircle);
    float drift = (r.next() <= 0x3FFF) ? 0.175f : -0.175f;
    for (int h = 0; h < 24; ++h) {
        CHECK(fnear(w.windX[h], (float)std::sin((double)angle)));
        CHECK(fnear(w.windY[h], (float)std::cos((double)angle)));
        angle = (float)(r.fscaled() * (double)drift + (double)angle);
    }
}

TEST(SessionAtmos, BuildWeatherDayWinterModeRoll) {
    // season 3: one RandomModulo(100) decides mode 1 (roll >= prob/2) or 2.
    const i32 prob = 80;
    for (u32 seed = 1; seed <= 32; ++seed) {
        crt::Srand(seed);
        RefRng r{seed};
        int expMode = (r.mod(100) >= prob / 2) ? 1 : 2;
        crt::Srand(seed);
        WeatherDayState w{};
        BuildWeatherDay(w, /*season=*/3, prob);
        CHECK_EQ(w.mode, expMode);
        // Winter days never roll thunder (mode != 0).
        for (int h = 0; h < 24; ++h) CHECK_EQ(w.thunder[h], 0);
    }
}

TEST(SessionAtmos, BuildWeatherDayZeroProbIsDry) {
    crt::Srand(99);
    WeatherDayState w{};
    BuildWeatherDay(w, 1, 0);
    for (int h = 0; h < 24; ++h) {
        CHECK_EQ(w.arc[h], 0);
        CHECK_EQ(w.thunder[h], 0);
    }
}

// ---------------------------------------------------------------------------
// SkyLayer fade — VIBE_Sky_SetLayerFade @0x5efca8 + the 0x5ef7cc step.
// ---------------------------------------------------------------------------
TEST(SessionAtmos, SkyLayerFadeGolden) {
    SkyLayer l{};
    l.fade = 0;
    SkyLayerSetFade(l, 255, 1500.0f);
    CHECK_EQ(l.fadeFrom, 0);
    CHECK_EQ(l.fadeTarget, 255);
    CHECK(fnear(l.fadeProgress, 0.0f));
    CHECK(fnear(l.fadeStepPerMs, 1.0f / 1500.0f));

    SkyLayerStep(l, 750.0f);          // progress 0.5 -> trunc(255*0.5) = 127
    CHECK_EQ((int)l.fade, 127);
    SkyLayerStep(l, 1500.0f);         // progress clamps at 1.0, step zeroed
    CHECK_EQ((int)l.fade, 255);
    CHECK(fnear(l.fadeProgress, 1.0f));
    CHECK(fnear(l.fadeStepPerMs, 0.0f));

    // Fade back down from 255 to 96 over 1000ms.
    SkyLayerSetFade(l, 96, 1000.0f);
    CHECK_EQ(l.fadeFrom, 255);
    SkyLayerStep(l, 500.0f);          // trunc(255 + (96-255)*0.5) = trunc(175.5)
    CHECK_EQ((int)l.fade, 175);
    SkyLayerStep(l, 500.0f);
    CHECK_EQ((int)l.fade, 96);

    // duration == 0: freeze at the current byte (progress 1, from=target=cur).
    SkyLayerSetFade(l, 7, 0.0f);
    CHECK_EQ(l.fadeFrom, 96);
    CHECK_EQ(l.fadeTarget, 96);
    CHECK(fnear(l.fadeProgress, 1.0f));
    SkyLayerStep(l, 100.0f);
    CHECK_EQ((int)l.fade, 96);
}

TEST(SessionAtmos, SkyLayerScrollGolden) {
    SkyLayer l{};
    // SetLayerScrollSpeed: stored = -speed * 1e-6 (flt_62C164).
    SkyLayerSetScrollSpeed(l, 20.0f);
    CHECK(fnear(l.scrollSpeed, -20.0f * 1.0e-6f));
    SkyLayerSetScrollSpeed(l, 0.0f);
    CHECK(fnear(l.scrollSpeed, 0.0f));
    // scrollPos = fmod(speed*dt + pos, 1.0)
    SkyLayerSetScrollSpeed(l, 30.0f);
    l.scrollPos = 0.5f;
    SkyLayerStep(l, 10000.0f);        // 0.5 + (-3e-5 * 1e4) = 0.5 - 0.3 = 0.2
    CHECK(fnear(l.scrollPos, 0.2f, 1e-5f));
}

// ---------------------------------------------------------------------------
// Brightness 24h sweep through Frame() — season 0 keyframes (390..1290 min).
// Golden brightness values from the 0x4b2504 piecewise curve.
// ---------------------------------------------------------------------------
TEST(SessionAtmos, BrightnessSweepSeason0) {
    SessionAtmos a{};
    a.climateRainProb[0] = 0;         // dry day: dark layer fades to 0
    sim::GameTime t{};
    t.day = 0;                        // season 0
    u32 ms = 0;

    struct Gold { int hour, minute, bright, band; };
    const Gold gold[] = {
        {0, 0, 0, 0},     {6, 30, 0, 0},   {7, 30, 50, 0},  {8, 30, 100, 1},
        {9, 30, 150, 1},  {10, 30, 200, 2},{12, 0, 242, 2}, {14, 0, 300, 3},
        {17, 30, 400, 4}, {18, 30, 450, 4},{19, 30, 500, 5},{20, 30, 550, 5},
        {21, 30, 600, 6}, {23, 0, 600, 6},
    };
    for (const Gold& g : gold) {
        t.hour = (u16)g.hour;
        t.minute = g.minute;
        ms += 16;
        a.Frame(t, /*seed=*/42u, ms);
        CHECK_EQ(a.brightness, g.bright);
        CHECK_EQ(a.band, g.band);
        // band/blend must agree with the daycycle core.
        render::SkyBandSelect s = render::BrightnessToBand(g.bright);
        CHECK_EQ(a.band, s.band);
        CHECK(fnear(a.blend, s.blend));
    }
}

// Hysteresis: settled transition + |delta| < 10 -> fog-only frame (no rebuild);
// delta >= 10 or a band change -> rebuild with force 1.
TEST(SessionAtmos, BrightnessHysteresis) {
    SessionAtmos a{};
    a.climateRainProb[2] = 0;
    sim::GameTime t{};
    t.day = 2;                        // season 2 (kf: 7:00 9:00 11:00 ...)
    t.hour = 8; t.minute = 0;         // mid kf0..kf1 -> 50
    a.Frame(t, 7u, 16);
    CHECK_EQ(a.brightness, 50);
    // The init-day frame runs UpdateBrightness twice (Sky_InitScene tail +
    // the frame-loop call); the first rebuilds, the second skips (delta 0).
    CHECK_EQ(a.lightRebuilds, 1);
    CHECK(!a.lightingRebuilt);
    CHECK_EQ((int)a.refreshForce, 1); // force of the init rebuild

    t.minute = 5;                     // 100*(3900)/7200 = 54: delta < 10
    a.Frame(t, 7u, 32);
    CHECK_EQ(a.brightness, 54);
    CHECK(!a.lightingRebuilt);        // skipped: |50-54| < 10
    CHECK_EQ(a.lightRebuilds, 1);
    CHECK_EQ(a.fogRowA, 0);
    CHECK_EQ(a.fogRowB, 1);

    t.minute = 15;                    // 62 -> |50-62| >= 10: rebuild
    a.Frame(t, 7u, 48);
    CHECK_EQ(a.brightness, 62);
    CHECK(a.lightingRebuilt);
    CHECK_EQ(a.lightRebuilds, 2);
    // Settled transition + |delta| < 100 -> the SOFT window (dbl_61DD60):
    // force = dword_62D4E8 (0), i.e. a non-forced RefreshAllObjects.
    CHECK_EQ((int)a.refreshForce, 0);
}

// Sunrise/sunset sun-height walk (VIBE_Light_SetSunHeight @0x4b24b0):
// first band change -> phase 1, h in [0.3, 0.9]; band >= 3 at phase 1 ->
// phase 2, h in [-0.9, -0.3].
TEST(SessionAtmos, SunriseSunsetEvents) {
    SessionAtmos a{};
    a.climateRainProb[0] = 0;
    sim::GameTime t{};
    t.day = 0;
    u32 ms = 0;

    t.hour = 5; t.minute = 0;         // brightness 0, band 0 (= initial lastBand)
    a.Frame(t, 5u, ms += 16);
    CHECK_EQ(a.sunPhase, 0);
    CHECK(!a.sunEvent);

    t.hour = 9; t.minute = 0;         // 150 -> band 1: SUNRISE
    a.Frame(t, 5u, ms += 16);
    CHECK(a.sunEvent);
    CHECK_EQ(a.sunPhase, 1);
    CHECK(a.sunHeight >= 0.3f && a.sunHeight <= 0.9f);

    t.hour = 11; t.minute = 0;        // 219 -> band 2: change but 2 < 3 -> no walk
    a.Frame(t, 5u, ms += 16);
    CHECK(!a.sunEvent);
    CHECK_EQ(a.sunPhase, 1);

    t.hour = 15; t.minute = 0;        // 342 -> band 3: SUNSET
    a.Frame(t, 5u, ms += 16);
    CHECK(a.sunEvent);
    CHECK_EQ(a.sunPhase, 2);
    CHECK(a.sunHeight >= -0.9f && a.sunHeight <= -0.3f);

    t.hour = 19; t.minute = 0;        // band 4: phase already 2 -> no more events
    a.Frame(t, 5u, ms += 16);
    CHECK(!a.sunEvent);
    CHECK_EQ(a.sunPhase, 2);
}

// ---------------------------------------------------------------------------
// Overcast chain: weather intensity -> dark-layer fade target
// (intensity<<6)/1000 + 96 over 1000ms (disasm @0x4c057d) -> fade byte ->
// overcast = fade * (1/255) -> ApplyAmbientBlend t.
// ---------------------------------------------------------------------------
TEST(SessionAtmos, OvercastFromDarkLayer) {
    SessionAtmos a{};
    a.climateRainProb[1] = 0;         // dry generated day -> dark fade stays 0
    sim::GameTime t{};
    t.day = 1;
    t.hour = 12; t.minute = 0;
    a.Frame(t, 11u, 16);              // init day
    CHECK_EQ((int)a.dark.fade, 0);
    // Force a heavy hour at noon (runtime world data in the original).
    a.day.arc[12] = 999;

    // The fade is re-armed EVERY weather frame (SetLayerFade resets progress
    // with from = current), then stepped by dt — an exponential approach.
    a.Frame(t, 11u, 32);              // dt 16: trunc(159 * 0.016) = 2
    CHECK_EQ(a.weatherIntensity, 999);
    CHECK_EQ((int)a.weatherCategory, (int)render::kWeatherHeavy);
    // target = (999 << 6)/1000 + 96 = 63 + 96 = 159 over 1000ms
    CHECK_EQ((int)a.dark.fadeTarget, 159);
    CHECK_EQ((int)a.dark.fade, 2);
    CHECK(fnear(a.overcast, 0.0f));   // brightness ran before the fade step

    a.Frame(t, 11u, 532);             // dt 500: trunc(2 + 157*0.5) = 80
    CHECK_EQ((int)a.dark.fade, 80);
    CHECK(fnear(a.overcast, 2.0f * kOvercastScale, 1e-6f));
    CHECK(fnear(a.fogBlendT, a.overcast));

    a.Frame(t, 11u, 1032);            // dt 500: trunc(80 + 79*0.5) = 119
    CHECK_EQ((int)a.dark.fade, 119);
    CHECK(fnear(a.overcast, 80.0f * kOvercastScale, 1e-6f));

    // Clear weather -> target 0 over 2500ms.
    a.day.arc[12] = 0;
    a.Frame(t, 11u, 1048);
    CHECK_EQ((int)a.dark.fadeTarget, 0);
    CHECK_EQ(a.weatherIntensity, 0);
}

// Weather params: hourly + wind-scaled grow counts and cloud scroll, against
// the render::Weather* cores.
TEST(SessionAtmos, WeatherParamsMatchCores) {
    SessionAtmos a{};
    sim::GameTime t{};
    t.day = 3;                        // season 3 -> winter (snow modes)
    t.hour = 6; t.minute = 0;
    a.climateRainProb[3] = 90;
    a.Frame(t, 77u, 16);
    CHECK(a.snowPresent);             // mode 1 or 2 both have snow
    CHECK_EQ(a.rainPresent, a.day.mode != 1);

    // Pin the hour data and re-frame.
    a.day.arc[5] = 150; a.day.arc[6] = 501; a.day.arc[7] = 100;
    a.day.windX[6] = -0.6f; a.day.windY[6] = 0.8f;
    a.Frame(t, 77u, 32);
    int inten = render::WeatherIntensity(a.day.arc, 6);
    CHECK_EQ(a.weatherIntensity, inten);
    CHECK_EQ(inten, 501);
    CHECK_EQ((int)a.weatherCategory, (int)render::CategoryFor(inten));
    CHECK_EQ(a.windSnowGrow, render::SnowGrowAmount(-0.6f, inten));
    CHECK_EQ(a.hourlySnowGrow, 501);
    if (a.rainPresent) {
        // snow + rain: arc odd -> arc/5
        CHECK_EQ(a.hourlyRainGrow, (501 & 1) ? 501 / 5 : 0);
        CHECK_EQ(a.windRainGrow, render::RainGrowAmount(-0.6f, inten));
    } else {
        CHECK_EQ(a.hourlyRainGrow, 0);
        CHECK_EQ(a.windRainGrow, 0);
    }
    float mag = render::CloudScrollMagnitude(-0.6f, 0.8f, inten);
    CHECK(fnear(a.cloudScroll, mag, 1e-4f));
    CHECK(fnear(a.cloudScrollFront, (float)((double)mag * 0.75), 1e-4f));
    CHECK(fnear(a.cloudScrollBack, (float)((double)mag * 1.5), 1e-4f));
    // Layer speeds: stored = -speed * 1e-6.
    CHECK(fnear(a.mid.scrollSpeed, -mag * 1.0e-6f, 1e-9f));
}

// ---------------------------------------------------------------------------
// Lightning: ThunderTick rolls (1/300 during thunder hours), flash window ->
// fog rows 0 -> 2 at t = 1.0, then the restore latch (dword_631DD8).
// ---------------------------------------------------------------------------
TEST(SessionAtmos, LightningFlashAndRestore) {
    SessionAtmos a{};
    sim::GameTime t{};
    t.day = 1;
    t.hour = 12; t.minute = 0;
    a.Frame(t, 3u, 16);
    a.day.arc[12] = 999;
    a.day.thunder[12] = 1;

    // Roll until the 1/300 hits (seeded -> deterministic, bounded).
    u32 ms = 32;
    bool fired = false;
    for (int i = 0; i < 4000 && !fired; ++i)
        fired = a.ThunderTick(t, ms += 16);
    CHECK(fired);
    CHECK(a.flashDurMs >= 8 && a.flashDurMs <= 15);   // RandomModulo(8) + 8
    CHECK_EQ(a.flashStartMs, ms);

    // Inside the window: the flash fog blend (rows 0 -> 2, t = 1.0).
    a.Frame(t, 3u, ms + 1);
    CHECK(a.flashActive);
    CHECK_EQ(a.fogRowA, 0);
    CHECK_EQ(a.fogRowB, 2);
    CHECK(fnear(a.fogBlendT, 1.0f));
    CHECK(!a.lightingRebuilt);

    // Window expired: restored to the overcast blend rows 0 -> 1.
    a.Frame(t, 3u, ms + a.flashDurMs + 1);
    CHECK(!a.flashActive);
    CHECK_EQ(a.fogRowB, 1);
}

TEST(SessionAtmos, ThunderNeedsThunderHour) {
    SessionAtmos a{};
    sim::GameTime t{};
    t.day = 1; t.hour = 10; t.minute = 0;
    a.Frame(t, 9u, 16);
    for (int h = 0; h < 24; ++h) a.day.thunder[h] = 0;
    for (int i = 0; i < 1000; ++i)
        CHECK(!a.ThunderTick(t, 32 + (u32)i));
}

// Sun-ray decision: hour 11..17, rain previous hour, none now, minute > 30,
// RandomModulo(128) > 0x60 (and option off).
TEST(SessionAtmos, SunRayDecision) {
    SessionAtmos a{};
    sim::GameTime t{};
    t.day = 1; t.hour = 13; t.minute = 40;
    a.Frame(t, 21u, 16);
    for (int h = 0; h < 24; ++h) { a.day.arc[h] = 0; a.day.thunder[h] = 0; }
    a.day.arc[12] = 150;              // rain in the previous hour
    a.day.arc[13] = 0;                // dry now
    bool everFired = false;
    for (int i = 0; i < 64; ++i) {
        a.ThunderTick(t, 32 + (u32)i);
        everFired = everFired || a.sunRaysFired;
    }
    CHECK(everFired);                 // 31/128 per tick -> hits within 64 draws

    // Wrong hour window -> never.
    t.hour = 9;
    a.day.arc[8] = 150; a.day.arc[9] = 0;
    bool fired9 = false;
    for (int i = 0; i < 64; ++i) {
        a.ThunderTick(t, 2000 + (u32)i);
        fired9 = fired9 || a.sunRaysFired;
    }
    CHECK(!fired9);
}

// ---------------------------------------------------------------------------
// Ambient + fog application params (the runtime band tables supplied).
// ---------------------------------------------------------------------------
TEST(SessionAtmos, AmbientAndFogRows) {
    SessionAtmos a{};
    a.hasSkyBands = true;
    for (int b = 0; b < render::kSkyBands; ++b)
        a.skyBands[b] = render::SkyBandColor{(float)(b * 30), (float)(b * 20),
                                             (float)(b * 10)};
    a.hasBandFog = true;
    for (int b = 0; b < render::kSkyBands; ++b)
        for (int i = 0; i < 6; ++i) {
            a.bandFog[b][i].colorR = (u8)(10 * b + i);
            a.bandFog[b][i].colorG = (u8)(5 * b + i);
            a.bandFog[b][i].colorB = (u8)(2 * b + i);
            a.bandFog[b][i].nearVal = 100.0f + 10.0f * b;
            a.bandFog[b][i].farVal = 1000.0f + 100.0f * b;
        }
    a.climateRainProb[0] = 0;
    sim::GameTime t{};
    t.day = 0; t.hour = 12; t.minute = 0;   // brightness 242 -> band 2 blend .42
    a.Frame(t, 8u, 16);
    CHECK_EQ(a.band, 2);

    render::SkyAmbient ref =
        render::BlendBandLighting(a.skyBands, a.band, a.blend, 1.0f);
    CHECK(fnear(a.ambient.r, ref.r));
    CHECK(fnear(a.ambient.g, ref.g));
    CHECK(fnear(a.ambient.b, ref.b));
    CHECK(fnear(a.ambient.luma, ref.luma));

    // Current fog row 0 = lerp(band2 row0, band3 row0, blend).
    int expR = render::LerpChannelTrunc(a.bandFog[2][0].colorR,
                                        a.bandFog[3][0].colorR, a.blend);
    CHECK_EQ((int)a.fogCurrent[0].colorR, expR);
    CHECK(fnear(a.fogCurrent[0].nearVal,
                120.0f * (1.0f - a.blend) + 130.0f * a.blend, 1e-3f));
    CHECK(a.fogApplied);
    // The applied fog blend was rows 0 -> 1 at t = overcast (dry day -> dark
    // layer fade 0 -> overcast 0 -> row 0 verbatim).
    CHECK_EQ(a.fogRowA, 0);
    CHECK_EQ(a.fogRowB, 1);
    CHECK(fnear(a.fogBlendT, 0.0f));
    CHECK_EQ(a.fog.color & 0xFF, (i32)a.fogCurrent[0].colorB);
}

// New-day reseed: same seed -> identical weather day; the next calendar day
// with another seed regenerates.
TEST(SessionAtmos, NewDayReseeds) {
    sim::GameTime t{};
    t.day = 4; t.hour = 0; t.minute = 0;

    SessionAtmos a{}, b{};
    a.climateRainProb[0] = b.climateRainProb[0] = 70;
    a.Frame(t, 555u, 16);
    b.Frame(t, 555u, 16);
    for (int h = 0; h < 24; ++h) {
        CHECK_EQ(a.day.arc[h], b.day.arc[h]);
        CHECK_EQ(a.day.thunder[h], b.day.thunder[h]);
        CHECK(fnear(a.day.windX[h], b.day.windX[h]));
    }

    // Day rolls over -> regen (deterministic from the new seed).
    i32 oldArc0 = a.day.arc[0];
    (void)oldArc0;
    t.day = 5;
    a.Frame(t, 556u, 32);
    b.Frame(t, 556u, 32);
    for (int h = 0; h < 24; ++h) CHECK_EQ(a.day.arc[h], b.day.arc[h]);
    CHECK_EQ(a.peakIntensity, b.peakIntensity);
}

// Golden-vector unit tests for the wave-6 day/night DRIVER: the consolidated
// time -> sun-state query render::ComputeSunState and its leaf pieces.
//
// Vectors derived directly from the gilde.exe decompile (reference of record):
//   0x4b2438  VIBE_DayCycle_BuildTimeTable    (season -> 6 keyframe seconds)
//   0x4b2504  VIBE_DayCycle_UpdateBrightness  (brightness core 0x4b253b..0x4b283b)
//   band = trunc(brightness*0.01)%7, blend = frac   (0x4b2671..0x4b274b)
//   0x4b28f0  sunrise/sunset split (band<3 -> raise=0, else raise=1)
//   0x4b24b0  VIBE_Light_SetSunHeight envelope (dbl_61DD48/40/38 = 0.3/-0.3/0.6)
//   0x58339c  VIBE_GameTime_GetSeasonFromDay (day % 4)
//
// The recovered keyframe table dword_4AD160 (get_bytes @0x4ad160) is the source
// of the BuildTimeTable expectations below.
#include "render/daycycle.h"
#include "tests/framework/test.h"

#include <cmath>

using namespace guild;
using namespace guild::render;

namespace {
bool Near(float a, float b, float e = 1e-5f) { return std::fabs(a - b) <= e; }
} // namespace

// --- BuildTimeTable: season -> seconds-of-day thresholds --------------------
// Season 0 minutes-of-day: 390 510 630 1050 1170 1290 -> *60 seconds.
TEST(SunDayCycle, BuildTimeTableSeason0) {
    i32 kf[6];
    BuildTimeTable(0, kf);
    CHECK_EQ(kf[0], 390 * 60);   // 06:30 -> 23400
    CHECK_EQ(kf[1], 510 * 60);   // 08:30 -> 30600
    CHECK_EQ(kf[2], 630 * 60);   // 10:30 -> 37800
    CHECK_EQ(kf[3], 1050 * 60);  // 17:30 -> 63000
    CHECK_EQ(kf[4], 1170 * 60);  // 19:30 -> 70200
    CHECK_EQ(kf[5], 1290 * 60);  // 21:30 -> 77400
}

// Season index wraps mod 4 (the original uses season & 3 / day % 4).
TEST(SunDayCycle, BuildTimeTableSeasonWraps) {
    i32 a[6], b[6];
    BuildTimeTable(3, a);
    BuildTimeTable(7, b); // 7 & 3 == 3
    for (int i = 0; i < 6; ++i) CHECK_EQ(a[i], b[i]);
    // Season 3 minutes: 480 600 690 960 1020 1140.
    CHECK_EQ(a[0], 480 * 60);
    CHECK_EQ(a[2], 690 * 60);
    CHECK_EQ(a[5], 1140 * 60);
}

// --- UpdateBrightness: piecewise-linear 0..600 ramp -------------------------
TEST(SunDayCycle, UpdateBrightnessSeason0) {
    i32 kf[6];
    BuildTimeTable(0, kf);
    CHECK_EQ(UpdateBrightness(kf, 0, 0), 0);     // pre-dawn, t < kf0
    CHECK_EQ(UpdateBrightness(kf, 6, 30), 0);    // exactly kf0 -> 0
    CHECK_EQ(UpdateBrightness(kf, 7, 30), 50);   // mid kf0..kf1 -> 100*(450-390)/120
    CHECK_EQ(UpdateBrightness(kf, 9, 30), 150);  // mid kf1..kf2 -> 100 + 100*60/120
    CHECK_EQ(UpdateBrightness(kf, 12, 0), 242);  // plateau slope kf2..kf3
    CHECK_EQ(UpdateBrightness(kf, 18, 30), 450); // dusk kf3..kf4
    CHECK_EQ(UpdateBrightness(kf, 20, 30), 550); // kf4..kf5
    CHECK_EQ(UpdateBrightness(kf, 21, 30), 600); // >= kf5 -> 600
    CHECK_EQ(UpdateBrightness(kf, 23, 0), 600);  // night plateau
}

// hour read as u16 (3600 * (unsigned __int16)hour); minute is a plain dword.
TEST(SunDayCycle, UpdateBrightnessHourCast) {
    i32 kf[6];
    BuildTimeTable(0, kf);
    // 12:00 with the noon plateau slope: t = 43200, kf2=37800, kf3=63000.
    // 200*(43200-37800)/(63000-37800) + 200 = 200*5400/25200 + 200 = 42 + 200.
    CHECK_EQ(UpdateBrightness(kf, 12, 0), 242);
}

// --- BrightnessToBand: trunc(b*0.01) % 7, blend = frac ----------------------
TEST(SunDayCycle, BrightnessToBand) {
    auto b0 = BrightnessToBand(0);
    CHECK_EQ(b0.band, 0);
    CHECK(Near(b0.blend, 0.0f));

    auto b150 = BrightnessToBand(150); // 1.5 -> band 1, blend 0.5
    CHECK_EQ(b150.band, 1);
    CHECK(Near(b150.blend, 0.5f, 1e-4f));

    auto b242 = BrightnessToBand(242); // 2.42 -> band 2, blend 0.42
    CHECK_EQ(b242.band, 2);
    CHECK(Near(b242.blend, 0.42f, 1e-4f));

    auto b600 = BrightnessToBand(600); // 6.0 -> band 6, blend 0
    CHECK_EQ(b600.band, 6);
    CHECK(Near(b600.blend, 0.0f));
}

// --- SunRegimeForBand: the 0x4b28f0 sunrise/sunset split --------------------
TEST(SunDayCycle, SunRegimeSunriseBands) {
    for (int band = 0; band < 3; ++band) {
        SunElevationRegime r = SunRegimeForBand(band);
        CHECK_EQ(r.raise, 0);                  // day branch
        CHECK(Near(r.elevLo, 0.3f));           // dbl_61DD48
        CHECK(Near(r.elevHi, 0.9f));           // 0.3 + 0.6 span
        CHECK(r.elevLo > 0.0f);                // sun above horizon
    }
}

TEST(SunDayCycle, SunRegimeSunsetBands) {
    for (int band = 3; band < 7; ++band) {
        SunElevationRegime r = SunRegimeForBand(band);
        CHECK_EQ(r.raise, 1);                  // night branch
        CHECK(Near(r.elevHi, -0.3f));          // -dbl_61DD40
        CHECK(Near(r.elevLo, -0.9f));          // -0.3 - 0.6 span
        CHECK(r.elevHi < 0.0f);                // sun below horizon
    }
}

// Envelope constants match the recovered doubles exactly.
TEST(SunDayCycle, SunEnvelopeConstants) {
    CHECK(Near((float)kSunDayBase, 0.3f));
    CHECK(Near((float)kSunNightBase, 0.3f));
    CHECK(Near((float)kSunSpan, 0.6f));
}

// Recovered scalar pins (get_bytes): the brightness->band scale dbl_61DD68 = 0.01.
// BrightnessToBand multiplies by this before truncating to the band index; a drift
// would shift every band boundary. Pinned to the literal recovered value here so
// the constant is golden independent of the trunc/frac math that consumes it.
TEST(SunDayCycle, RecoveredBandScaleConstant) {
    CHECK(kBandScale == 0.01);          // dbl_61DD68
    // The scale applied to brightness 100 lands exactly on band boundary 1.0.
    CHECK(Near((float)((double)100 * kBandScale), 1.0f));
}

// --- ComputeSunState: the full composed handoff -----------------------------
TEST(SunDayCycle, ComputeSunStateNoonDay0) {
    // day 0 -> season 0; 12:00 -> brightness 242 -> band 2 (sunrise regime).
    SunState st = ComputeSunState(/*day=*/0, /*hour=*/12, /*minute=*/0);
    CHECK_EQ(st.season, 0);
    CHECK_EQ(st.keyframes[2], 630 * 60);
    CHECK_EQ(st.brightness, 242);
    CHECK_EQ(st.band, 2);
    CHECK(Near(st.blend, 0.42f, 1e-4f));
    CHECK_EQ(st.regime.raise, 0);              // band 2 < 3 -> sunrise/day
}

TEST(SunDayCycle, ComputeSunStateNightDay0) {
    // 23:00 -> brightness 600 -> band 6 -> sunset/night regime.
    SunState st = ComputeSunState(0, 23, 0);
    CHECK_EQ(st.brightness, 600);
    CHECK_EQ(st.band, 6);
    CHECK_EQ(st.regime.raise, 1);
    CHECK(st.regime.elevHi < 0.0f);
}

TEST(SunDayCycle, ComputeSunStateSeasonFromDay) {
    // day % 4 selects the season; day 3 and day 7 share season 3.
    SunState a = ComputeSunState(3, 12, 0);
    SunState b = ComputeSunState(7, 12, 0);
    CHECK_EQ(a.season, 3);
    CHECK_EQ(b.season, 3);
    CHECK_EQ(a.brightness, b.brightness);
    CHECK_EQ(a.band, b.band);
}

// Predawn: brightness 0 -> band 0 -> sunrise regime (the sun is rising).
TEST(SunDayCycle, ComputeSunStatePredawn) {
    SunState st = ComputeSunState(0, 3, 0);
    CHECK_EQ(st.brightness, 0);
    CHECK_EQ(st.band, 0);
    CHECK_EQ(st.regime.raise, 0);
    CHECK(st.regime.elevLo > 0.0f);
}

// =============================================================================
// WAVE-10 HARDENING — clock extremes + every season day%4.
// BuildTimeTable indexes kDayKeyframeMinutes[season & 3]; ComputeSunState feeds
// it season = day % 4. Drive hour 0/23, the hour-cast wraparound, and every
// season so the [4][6] table is never indexed out of range. No goldens change.
// =============================================================================

// Hour 0 (midnight) -> well before kf0 -> brightness 0 -> band 0, every season.
TEST(SunDayCycleHarden, Hour0AllSeasons) {
    for (int day = 0; day < 4; ++day) {
        SunState st = ComputeSunState(day, 0, 0);
        CHECK_EQ(st.season, day % 4);
        CHECK_EQ(st.brightness, 0);
        CHECK_EQ(st.band, 0);
        CHECK_EQ(st.regime.raise, 0);
    }
}

// Hour 23 (last hour) -> past kf5 -> brightness 600 -> band 6, every season.
TEST(SunDayCycleHarden, Hour23AllSeasons) {
    for (int day = 0; day < 4; ++day) {
        SunState st = ComputeSunState(day, 23, 0);
        CHECK_EQ(st.brightness, 600);
        CHECK_EQ(st.band, 6);
        CHECK_EQ(st.regime.raise, 1);     // band 6 -> sunset/night
    }
}

// Every season day%4 (and the wrap day=4..7) selects an in-range keyframe row.
TEST(SunDayCycleHarden, EverySeasonDayMod4) {
    for (int day = 0; day < 8; ++day) {
        SunState st = ComputeSunState(day, 12, 0);
        CHECK_EQ(st.season, day % 4);
        // keyframes must be the recovered table row for that season (in-range read).
        i32 kf[6];
        BuildTimeTable(st.season, kf);
        for (int i = 0; i < 6; ++i) CHECK_EQ(st.keyframes[i], kf[i]);
        // band/blend are always in their documented ranges.
        CHECK(st.band >= 0 && st.band < 7);
        CHECK(st.blend >= 0.0f && st.blend < 1.0f);
    }
}

// BuildTimeTable with a large/negative season index: the `& 3` mask keeps the
// row read in bounds (no OOB into kDayKeyframeMinutes[4][6]).
TEST(SunDayCycleHarden, BuildTimeTableSeasonMaskInBounds) {
    i32 a[6], b[6];
    BuildTimeTable(0, a);
    BuildTimeTable(4, b);              // 4 & 3 == 0
    for (int i = 0; i < 6; ++i) CHECK_EQ(a[i], b[i]);
    i32 c[6], d[6];
    BuildTimeTable(3, c);
    BuildTimeTable(-1 & 3, d);         // explicit mask, season 3
    for (int i = 0; i < 6; ++i) CHECK_EQ(c[i], d[i]);
}

// UpdateBrightness with the hour read as u16: hour 0x10000 (65536) aliases to 0
// via (unsigned __int16)hour, so t = 0 -> brightness 0. Pins the documented cast.
TEST(SunDayCycleHarden, UpdateBrightnessHourU16Wrap) {
    i32 kf[6];
    BuildTimeTable(0, kf);
    CHECK_EQ(UpdateBrightness(kf, 0x10000, 0), 0);   // 65536 & 0xFFFF == 0
    CHECK_EQ(UpdateBrightness(kf, 0x10000 + 12, 0),  // aliases to hour 12
             UpdateBrightness(kf, 12, 0));
}

// BrightnessToBand wraps the whole part mod 7 (a brightness > 600 would push the
// raw band past 6; the % 7 keeps the index in [0,6] with no OOB downstream).
TEST(SunDayCycleHarden, BrightnessToBandWrapsModSeven) {
    auto b700 = BrightnessToBand(700);   // 7.0 -> 7 % 7 == 0
    CHECK_EQ(b700.band, 0);
    CHECK(Near(b700.blend, 0.0f));
    auto b1450 = BrightnessToBand(1450); // 14.5 -> 14 % 7 == 0, blend 0.5
    CHECK_EQ(b1450.band, 0);
    CHECK(Near(b1450.blend, 0.5f, 1e-4f));
    auto bneg = BrightnessToBand(-150);  // -1.5 -> trunc toward zero -1 ; -1 % 7
    CHECK(bneg.band <= 0);               // C++ % of negative: in (-7,0]; in range
    CHECK(bneg.band > -7);
}

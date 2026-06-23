// Golden-vector unit tests for the weather ambient-loop crossfade arithmetic.
// Vectors derived from the gilde.exe decompile (reference of record).
#include "tests/framework/test.h"
#include "render/weather_recon.h"

#include <cmath>
#include <cstring>

using namespace guild;
using namespace guild::render;

namespace {
bool close(float a, float b) { return std::fabs(a - b) < std::fabs(b) * 1e-4f + 1e-4f; }
constexpr float A3C = 2600.0f, A40 = 88.9000015258789f, A44 = 387400.0f,
                A48 = 127.0f, A4C = 910000.0f, A50 = 0.0003846153849735856f,
                A54 = 1300000.0f, S7 = 0.7f;
}

TEST(WeatherReconSetup, MixSetup) {
    RainMixSetup s = WeatherRainMixSetup(600.0f);
    CHECK(close(s.height_delta, 600.0f));
    CHECK(close(s.inv_height, A3C - 600.0f));        // 2000
    CHECK(close(s.base_gain, (A3C - 600.0f) * A40)); // 2000 * 88.9
}

TEST(WeatherReconHeavy, HeavyTier) {
    RainMixSetup s = WeatherRainMixSetup(600.0f);
    RainHeavyPans p = WeatherRainHeavyPans(s);
    CHECK(close(p.main, s.base_gain * A50));
    CHECK(close(p.near_, S7 * A48 * 600.0f * A50));
    CHECK(close(p.far_,  S7 * A48 * (A3C - 600.0f) * A50));
}

TEST(WeatherReconMid, MidTier) {
    RainMixSetup s = WeatherRainMixSetup(400.0f);
    const int intensity = 700;   // t = 201
    RainMidPans p = WeatherRainMidPans(s, intensity);
    const float t = 201.0f, denom = 500.0f * A3C;
    CHECK(close(p.a, t * A40 * s.inv_height / A54));
    CHECK(close(p.b, (S7 * A48 * t * 400.0f) / denom));
    CHECK(close(p.c, (S7 * A48 * (A3C - 400.0f) * (500.0f - t)) / denom));
}

TEST(WeatherReconLight, LightTier) {
    RainMixSetup s = WeatherRainMixSetup(300.0f);
    const int intensity = 300;   // t = 151
    RainLightPans p = WeatherRainLightPans(s, intensity);
    const float t = 151.0f, denom = 350.0f * A3C;
    CHECK(close(p.a, t * A40 * s.inv_height / A4C));
    CHECK(close(p.b, (S7 * A48 * t * 300.0f) / denom));
    CHECK(close(p.c, (S7 * A48 * (A3C - 300.0f) * (350.0f - t)) / denom));
}

TEST(WeatherReconDrizzle, DrizzleTier) {
    RainMixSetup s = WeatherRainMixSetup(200.0f);
    const int intensity = 100;
    RainDrizzlePans p = WeatherRainDrizzlePans(s, intensity);
    const float rate = 100.0f, denom = 149.0f * A3C;
    CHECK(close(p.main, s.base_gain * rate / A44));
    CHECK(close(p.b, (S7 * A48 * 200.0f * rate) / denom));
    CHECK(close(p.c, (S7 * A48 * (A3C - 200.0f) * rate) / denom));
}

TEST(WeatherReconSeason, SeasonSuffix) {
    CHECK_EQ(std::strcmp(WeatherSeasonSuffix(3), "_SNOW"), 0);      // winter
    CHECK_EQ(std::strcmp(WeatherSeasonSuffix(2), "_HERBST"), 0);    // autumn
    CHECK_EQ(std::strcmp(WeatherSeasonSuffix(0), "_FRUEHLING"), 0); // spring
    CHECK_EQ(std::strcmp(WeatherSeasonSuffix(1), ""), 0);           // summer
}

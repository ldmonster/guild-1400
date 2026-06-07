#include "render/weather.h"
#include "render/particle.h" // TruncToward
#include "util/math_random.h" // RandomModulo
#include <cmath>

namespace guild::render {

// gilde.exe 0x4c0040 (intensity core). v0 = arc[(h+23)%24]; if v0 <= arc[h] then
// v0 = arc[h]; v1 = (h+1)%24; if v0 <= arc[v1] then v2 = arc[v1] else v2 = v0.
// == max of the three neighbouring hours.
int WeatherIntensity(const i32 arc[24], int h) {
    int prev = arc[(h + 23) % 24];
    int cur = arc[h % 24];
    int v0 = prev <= cur ? cur : prev;
    int nxt = arc[(h + 1) % 24];
    return v0 <= nxt ? nxt : v0;
}

// VIBE_Snow_GrowFlakeList(..., (int)(2.0 * -windX * intensity)).
// The original truncates via VIBE_Coord_ConvertX (toward zero).
int SnowGrowAmount(float windX, int intensity) {
    return TruncToward(kWxSnowRate * -(double)windX * (double)intensity);
}

int RainGrowAmount(float windX, int intensity) {
    return TruncToward(kWxRainRate * -(double)windX * (double)intensity);
}

float CloudScrollMagnitude(float windX, float windY, int intensity) {
    double mag = std::sqrt((double)windX * windX + (double)windY * windY);
    return (float)(mag * (double)(intensity + 150) * kWxScroll);
}

WeatherCategory CategoryFor(int intensity) {
    if (intensity >= 150)
        return kWeatherHeavy;
    if (intensity >= 50)
        return kWeatherMedium;
    return kWeatherFair;
}

int CloudVariantCount(WeatherCategory category) {
    switch (category) {
    case kWeatherHeavy:  return 2; // sky_schwer
    case kWeatherMedium: return 3; // Sky_Mittel
    case kWeatherFair:   return 4; // Sky_Schoen
    }
    return 4;
}

int SelectCloudLayerIndex(WeatherCategory category, int current) {
    int n = CloudVariantCount(category);
    // RandomModulo is read as an unsigned __int16 result in the original.
    int idx = (unsigned short)guild::util::RandomModulo((u16)n);
    // Reroll-by-advance: if the rolled variant matches the one already shown,
    // bump it by one (the original compares the texture name; within a pool the
    // names are 1:1 with the index, so name-match == index-match).
    if (idx == current)
        ++idx;
    return idx;
}

} // namespace guild::render

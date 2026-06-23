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

// gilde.exe 0x4c0040 — per-frame weather state / rain gate.
WeatherFrame WeatherUpdate(const i32 arc[24], const float windX[24],
                           const float windY[24], int hour) {
    WeatherFrame wf{};
    wf.hour = hour;                       // dword_11BC1C4 = HIWORD(clock) /*0x4c0057*/
    const i32 code = arc[hour];           // dword_11BC038[hour*4]
    // Rain gate: bit0 of the weather code. /*0x4c0085 test byte,1*/
    wf.rainActive = (code & 1) != 0;
    // Signed division by 5 (idiv). /*0x4c009a..0x4c00a2*/
    wf.rainSpawn = wf.rainActive ? (code / 5) : 0;
    // When no snow system exists the original passes the raw code. /*0x4c0416*/
    wf.rainSpawnNoSnow = code;

    const float wx = windX[hour];         // dword_11BC100[hour] /*0x4c00c1*/
    const float wy = windY[hour];         // dword_11BC160[hour] /*0x4c00d2*/

    // intensity = peak-of-3 (arc[(h+23)%24], arc[h], arc[(h+1)%24]). /*0x4c00f0..*/
    wf.intensity = WeatherIntensity(arc, hour);

    // Grow amounts (op==2). trunc toward zero via VIBE_Coord_ConvertX path.
    wf.snowGrow = SnowGrowAmount(wx, wf.intensity); // trunc(2.0 * -wx * I)
    wf.rainGrow = RainGrowAmount(wx, wf.intensity); // trunc(0.5 * -wx * I)

    // Base cloud scroll magnitude + the two layer multipliers. /*0x4c0222,374,3c7*/
    wf.scrollMag  = CloudScrollMagnitude(wx, wy, wf.intensity);
    wf.scrollFast = (float)((double)wf.scrollMag * kWxScrollFast); // *0.75
    wf.scrollBack = (float)((double)wf.scrollMag * kWxScrollBack); // *1.5

    wf.category = CategoryFor(wf.intensity);
    return wf;
}

} // namespace guild::render

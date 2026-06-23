#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — weather state core (gilde.exe d3_sky.c / weather).
//
//   0x4c0040  VIBE_Weather_UpdateSky
//
// The full function drives the snow/rain particle grow-lists, swaps sky-layer
// cloud textures by intensity category (string-named "Sky_Schoen/Mittel/Schwer"),
// and sets per-layer scroll/fade — all coupled to the snow/rain/sky-layer
// subsystems and a 24-hour weather-arc array (dword_11BC038). Those driving
// calls are out of scope (documented in the report). The RECOVERABLE, testable
// core reproduced here is the intensity + wind/scroll arithmetic:
//
//   intensity = max(arc[(h+23)%24], arc[h], arc[(h+1)%24])   (peak of 3 hours)
//   snowGrow  = trunc( 2.0  * -windX * intensity )           (dbl_61E4B0 = 2.0)
//   rainGrow  = trunc( 0.5  * -windX * intensity )           (dbl_61E4B8 = 0.5)
//   scrollMag = sqrt(windX^2 + windY^2) * (intensity+150) * 0.125  (61E4C0)
//   layer scroll speeds: fast = scrollMag*0.75 (61E4C8), back = scrollMag*1.5
//                        (61E4D0); mid layer = scrollMag.
//   category: intensity>=150 -> heavy ; >=50 -> medium ; else fair.
// =============================================================================
namespace guild::render {

// Recovered double constants (get_bytes @0x61E4B0).
constexpr double kWxSnowRate  = 2.0;   // dbl_61E4B0
constexpr double kWxRainRate  = 0.5;   // dbl_61E4B8
constexpr double kWxScroll    = 0.125; // dbl_61E4C0
constexpr double kWxScrollFast= 0.75;  // dbl_61E4C8 (front cloud layer)
constexpr double kWxScrollBack= 1.5;   // dbl_61E4D0 (back cloud layer)

enum WeatherCategory { kWeatherFair = 0, kWeatherMedium = 1, kWeatherHeavy = 2 };

// peak-of-3 intensity for hour `h` over the 24-entry weather arc.
int WeatherIntensity(const i32 arc[24], int h);

// trunc(2.0 * -windX * intensity) — snow flake grow count this frame.
int SnowGrowAmount(float windX, int intensity);
// trunc(0.5 * -windX * intensity) — rain drop grow count this frame.
int RainGrowAmount(float windX, int intensity);

// sqrt(windX^2 + windY^2) * (intensity+150) * 0.125 — base cloud scroll speed.
float CloudScrollMagnitude(float windX, float windY, int intensity);

// intensity -> texture-set category.
WeatherCategory CategoryFor(int intensity);

// ---------------------------------------------------------------------------
// Cloud-layer texture selection by intensity (the VIBE_Weather_UpdateSky tail).
// Per category the original picks one of N named cloud textures with
// VIBE_Math_RandomModulo(N); if the rolled index names the SAME texture that is
// currently on the layer (StrCmpNoCase match) it advances by one (++idx) to
// force a visible change. The pools (from the engine string table):
//   fair   (intensity <  50): "Sky_Schoen_01".. (4 variants), RandomModulo(4)
//   medium (intensity >= 50): "Sky_Mittel_01".. (3 variants), RandomModulo(3)
//   heavy  (intensity >=150): "sky_schwer_01".. (2 variants), RandomModulo(2)
// SelectCloudLayerIndex returns the chosen variant index for `category`, given
// the index `current` already shown (or -1 = none). Uses crt::RandNext via
// util::RandomModulo for fidelity.
// ---------------------------------------------------------------------------
int SelectCloudLayerIndex(WeatherCategory category, int current);

// Variant counts per category (4 / 3 / 2).
int CloudVariantCount(WeatherCategory category);

// ---------------------------------------------------------------------------
// gilde.exe 0x4c0040 VIBE_Weather_UpdateSky — the per-frame weather STATE
// driver that gates rain/snow. Pure decision core, reconstructed 1:1 from the
// disassembly; the subsystem dispatch (Snow/Rain GrowList, Sky layer texture/
// fade/scroll) is left to the caller (it owns those subsystem handles).
//
// The current hour is HIWORD(qword_13CE852) (game-time clock); the engine stores
// it to dword_11BC1C4 then indexes three 24-entry arrays by it:
//   arc[h]   = dword_11BC038[h]   weather code (bit0 = "is rain", rest intensity)
//   windX[h] = dword_11BC100[h]
//   windY[h] = dword_11BC160[h]
//
// WeatherFrame captures exactly what 0x4c0040 computes before it dispatches:
//   hour        : HIWORD(clock)
//   rainActive  : (arc[hour] & 1) != 0   (the rain gate @0x4c0085)
//   rainSpawn   : rainActive ? arc[hour]/5 : 0   (signed idiv @0x4c009f-a2)
//                 — the per-frame "spawn" count handed to VIBE_Rain_GrowDropList
//                   (op==1) when a rain system + snow system both exist;
//                   when no snow system exists the original passes arc[hour]
//                   directly (rainSpawnNoSnow).
//   intensity   : peak-of-3 = max(arc[(h+23)%24], arc[h], arc[(h+1)%24])
//   snowGrow    : trunc(2.0 * -windX * intensity)   (op==2 GrowList arg)
//   rainGrow    : trunc(0.5 * -windX * intensity)
//   scrollMag   : sqrt(windX^2+windY^2) * (intensity+150) * 0.125
//   category    : CategoryFor(intensity)
// ---------------------------------------------------------------------------
struct WeatherFrame {
    int   hour;            // dword_11BC1C4
    bool  rainActive;      // arc[hour] & 1
    int   rainSpawn;       // arc[hour] / 5   (when snow system present)
    int   rainSpawnNoSnow; // arc[hour]       (when no snow system)
    int   intensity;       // peak-of-3
    int   snowGrow;        // trunc(2.0 * -windX * intensity)
    int   rainGrow;        // trunc(0.5 * -windX * intensity)
    float scrollMag;       // base cloud scroll speed
    float scrollFast;      // scrollMag * 0.75  (front layer, dword_11BC1D0)
    float scrollBack;      // scrollMag * 1.5   (back  layer, dword_11BC1D8)
    WeatherCategory category;
};

// Compute the weather frame for `hour` (0..23) from the three 24-entry arrays.
// `hour` is HIWORD of the game clock (the engine reads it raw; callers pass it
// already reduced mod 24 by the clock, but intensity wraps the neighbours mod 24
// regardless). This is the gating decision 0x4c0040 makes each frame; the caller
// then drives the rain/snow grow lists and sky layers from these fields.
WeatherFrame WeatherUpdate(const i32 arc[24], const float windX[24],
                           const float windY[24], int hour);

} // namespace guild::render

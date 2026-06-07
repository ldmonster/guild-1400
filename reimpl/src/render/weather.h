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

} // namespace guild::render

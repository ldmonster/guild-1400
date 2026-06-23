#pragma once
// Weather ambient-loop / seasonal-mesh reconstruction for Die Gilde (gilde.exe).
//
//   gilde.exe 0x57f190 VIBE_Weather_UpdateAmbientLoops
//   gilde.exe 0x505df4 VIBE_Weather_ApplySeasonalMeshes
//
// VIBE_Weather_UpdateAmbientLoops is a large rain/storm ambient-loop crossfade
// mixer.  It is almost entirely coupled to the audio voice manager
// (VIBE_Audio_StartVoiceSample / SetVoicePan / StopVoice, rules 5) and to a set
// of voice-handle globals.  The genuinely pure, faithfully reconstructable
// content is the per-voice *gain/pan crossfade arithmetic* that selects between
// rain-intensity tiers (intensity thresholds 149 / 499 / 999) and the
// height-attenuated mix weights.  Those are reconstructed 1:1 here; the audio
// dispatch itself is left to the integration layer (rule 8: not faked).
//
// VIBE_Weather_ApplySeasonalMeshes is pure orchestration of texture path
// building / floor-layer texture assignment per season; it contains no
// reconstructable arithmetic (only season dispatch + string-suffix selection),
// so it is documented in the .cpp and its season->suffix mapping exposed.

#include "guild/common/types.h"

namespace guild {
namespace render {

// gilde.exe 0x57f190 head — common crossfade setup.
//   v93   = listenerHeight - VIBE_Terrain_AverageAreaHeight(...)   [caller-provided]
//   v89   = flt_625A3C(2600) - v93        (inverted height term)
//   v65   = v89 * flt_625A40(88.9)        (base gain)
struct RainMixSetup {
    float height_delta;   // v93
    float inv_height;     // v89 = 2600 - v93
    float base_gain;      // v65 = v89 * 88.9
};
RainMixSetup WeatherRainMixSetup(float height_delta);

// gilde.exe 0x57f190 — heavy-rain tier (intensity >= 999).
// Computes the three primary loop pan values for the saturated-rain case:
//   pan_main = v65 * flt_625A50(0.000384615)
//   pan_near = 0.69999999 * flt_625A48(127) * v93 * flt_625A50
//   pan_far  = 0.69999999 * flt_625A48(127) * (2600 - v93) * flt_625A50
// (Each is later truncated toward zero and handed to the audio mixer.)
struct RainHeavyPans {
    float main;   // -> dword_123529C voice
    float near_;  // -> dword_12352B4 voice
    float far_;   // -> dword_1235284 voice
};
RainHeavyPans WeatherRainHeavyPans(const RainMixSetup& s);

// gilde.exe 0x57f190 — mid-rain tier (499 <= intensity < 999).
// t = intensity - 499; weights blend over the 500-wide window.
//   pan_a = t * flt_625A40(88.9) * v89 / flt_625A54(1300000)
//   pan_b = 0.69999999 * 127 * t * v93 / (500 * 2600)
//   pan_c = 0.69999999 * 127 * (2600 - v93) * (500 - t) / (500 * 2600)
struct RainMidPans {
    float a;
    float b;
    float c;
};
RainMidPans WeatherRainMidPans(const RainMixSetup& s, int intensity);

// gilde.exe 0x57f190 — light-rain tier (149 <= intensity < 499).
// t = intensity - 149; window width 350.
//   pan_a = t * 88.9 * v89 / flt_625A4C(910000)
//   pan_b = 0.69999999 * 127 * t * v93 / (350 * 2600)
//   pan_c = 0.69999999 * 127 * (2600 - v93) * (350 - t) / (350 * 2600)
struct RainLightPans {
    float a;
    float b;
    float c;
};
RainLightPans WeatherRainLightPans(const RainMixSetup& s, int intensity);

// gilde.exe 0x57f190 — drizzle tier (intensity < 149).
//   rate    = (float)intensity
//   pan_main= v65 * rate / flt_625A44(387400)
//   pan_b   = 0.69999999 * flt_625A48(127) * v93 * rate / (149 * 2600)
//   pan_c   = 0.69999999 * 127 * (2600 - v93) * rate / (149 * 2600)
struct RainDrizzlePans {
    float main;
    float b;
    float c;
};
RainDrizzlePans WeatherRainDrizzlePans(const RainMixSetup& s, int intensity);

// gilde.exe 0x505df4 — season -> texture-name suffix used by
// VIBE_Weather_ApplySeasonalMeshes.  Season codes come from
// VIBE_GameTime_GetSeasonFromDay: 0=spring,1=summer,2=autumn,3=winter.
//   3 (winter) -> "_SNOW"
//   2 (autumn) -> "_HERBST"
//   0 (spring) -> "_FRUEHLING"
//   1 (summer) -> "" (base texture, no suffix)
const char* WeatherSeasonSuffix(int season);

} // namespace render
} // namespace guild

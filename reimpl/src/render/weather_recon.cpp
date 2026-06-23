// Weather ambient-loop crossfade arithmetic reconstruction (gilde.exe).
// Translated 1:1 from the Hex-Rays decompile (reference of record).
//
// The crossfade math runs on the x87 stack in the original (80-bit intermediate
// precision).  We reconstruct the exact operand order and the exact IEEE-754
// constants; results are stored back to 32-bit floats at the same points the
// original spills them, so the observable (truncated) pan integers match.

#include "render/weather_recon.h"

namespace guild {
namespace render {

namespace {
// Exact IEEE-754 constants (recovered with get_bytes):
constexpr float kF625A3C = 2600.0f;                 // flt_625A3C (0x45228000)
constexpr float kF625A40 = 88.9000015258789f;       // flt_625A40 (0x42B1CCCD)
constexpr float kF625A44 = 387400.0f;               // flt_625A44 (0x48BD2900)
constexpr float kF625A48 = 127.0f;                  // flt_625A48 (0x42FE0000)
constexpr float kF625A4C = 910000.0f;               // flt_625A4C (0x495E2B00)
constexpr float kF625A50 = 0.0003846153849735856f;  // flt_625A50 (0x39C9A634)
constexpr float kF625A54 = 1300000.0f;              // flt_625A54 (0x499EB100)

// Hex-Rays prints the inline 0.7f as "0.69999999" (its double value).
constexpr float kSeven10 = 0.7f;                    // 0x3F333333
} // namespace

// gilde.exe 0x57f190 head — common crossfade setup.
RainMixSetup WeatherRainMixSetup(float height_delta) {
    RainMixSetup s;
    s.height_delta = height_delta;                          // v93
    s.inv_height   = kF625A3C - height_delta;               // v89 = 2600 - v93 /*0x57f2ef*/
    s.base_gain    = s.inv_height * kF625A40;               // v65 = v89 * 88.9   /*0x57f30a*/
    return s;
}

// gilde.exe 0x57f190 — heavy-rain tier (intensity >= 999).
RainHeavyPans WeatherRainHeavyPans(const RainMixSetup& s) {
    RainHeavyPans p;
    p.main = s.base_gain * kF625A50;                                  // v77 /*0x57f32a*/
    p.near_ = kSeven10 * kF625A48 * s.height_delta * kF625A50;        // v82 /*0x57f38b*/
    const float inv = kF625A3C - s.height_delta;                      // v63 = 2600 - v93 /*0x57f3d7*/
    p.far_ = kSeven10 * kF625A48 * inv * kF625A50;                    // v76 /*0x57f3f8*/
    return p;
}

// gilde.exe 0x57f190 — mid-rain tier (499 <= intensity < 999).
RainMidPans WeatherRainMidPans(const RainMixSetup& s, int intensity) {
    const float t = static_cast<float>(intensity - 499);             // v20/v91 /*0x57f66e*/
    RainMidPans p;
    p.a = t * kF625A40 * s.inv_height / kF625A54;                    // v86 /*0x57f6a1*/
    const float denom = 500.0f * kF625A3C;                           // 500*2600
    p.b = (kSeven10 * kF625A48 * t * s.height_delta) / denom;        // v62/v79 /*0x57f71a*/
    const float inv = kF625A3C - s.height_delta;                     // v60 = 2600 - v93
    const float remain = 500.0f - t;                                 // v59 = 500 - t
    p.c = (kSeven10 * kF625A48 * inv * remain) / denom;              // v72/v68 /*0x57f7a7*/
    return p;
}

// gilde.exe 0x57f190 — light-rain tier (149 <= intensity < 499).
RainLightPans WeatherRainLightPans(const RainMixSetup& s, int intensity) {
    const float t = static_cast<float>(intensity - 149);             // v31/v92 /*0x57fa10*/
    RainLightPans p;
    p.a = t * kF625A40 * s.inv_height / kF625A4C;                    // v80 /*0x57fa43*/
    const float denom = 350.0f * kF625A3C;                           // 350*2600
    p.b = (kSeven10 * kF625A48 * t * s.height_delta) / denom;        // v83/v87 /*0x57fab9*/
    const float inv = kF625A3C - s.height_delta;                     // v55 = 2600 - v93
    const float remain = 350.0f - t;                                 // v64 = 350 - t
    p.c = (kSeven10 * kF625A48 * inv * remain) / denom;              // v84/v73 /*0x57fb48*/
    return p;
}

// gilde.exe 0x57f190 — drizzle tier (intensity < 149).
RainDrizzlePans WeatherRainDrizzlePans(const RainMixSetup& s, int intensity) {
    const float rate = static_cast<float>(intensity);                // v90 /*0x57fda4*/
    RainDrizzlePans p;
    p.main = s.base_gain * rate / kF625A44;                          // v53 /*0x57fdce*/
    const float denom = 149.0f * kF625A3C;                           // 149*2600 (v70/v78)
    p.b = (kSeven10 * kF625A48 * s.height_delta * rate) / denom;     // v88/v70 /*0x57fe3f*/
    const float inv = kF625A3C - s.height_delta;                     // v58 = 2600 - v93
    p.c = (kSeven10 * kF625A48 * inv * rate) / denom;                // v69/v78 /*0x57fec6*/
    return p;
}

// gilde.exe 0x505df4 — season -> texture suffix.
const char* WeatherSeasonSuffix(int season) {
    switch (season) {
        case 3: return "_SNOW";        // winter   /*0x505e19*/
        case 2: return "_HERBST";      // autumn   /*0x505f5c*/
        case 0: return "_FRUEHLING";   // spring   /*0x5060c3 default path*/
        case 1: return "";             // summer (base texture, no suffix) /*0x506224*/
        default: return "";
    }
}

} // namespace render
} // namespace guild

#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — day/night cycle brightness curve (gilde.exe d3_sky.c).
//
//   0x4b2438  VIBE_DayCycle_BuildTimeTable    (season -> 6 keyframe times)
//   0x4b2504  VIBE_DayCycle_UpdateBrightness  (time-of-day -> 0..600 brightness)
//
// RECOVERED TABLE (dword_4AD160, 4 seasons x 6 keyframe minutes-of-day, verified
// via get_bytes). BuildTimeTable splits each minute value into h:m and stores
// seconds-of-day (3600*h + 60*m) as the 6 thresholds the brightness curve uses.
// UpdateBrightness then piecewise-linearly interpolates a 0..600 brightness:
//   t<kf0           -> 0
//   kf0..kf1        -> 0   + 100*(t-kf0)/(kf1-kf0)
//   kf1..kf2        -> 100 + 100*(t-kf1)/(kf2-kf1)
//   kf2..kf3        -> 200 + 200*(t-kf2)/(kf3-kf2)   (full-day plateau slope)
//   kf3..kf4        -> 400 + 100*(t-kf3)/(kf4-kf3)
//   kf4..kf5        -> 500 + 100*(t-kf4)/(kf5-kf4)
//   t>=kf5          -> 600
// where t = seconds-of-day = 3600*hour + 60*minute. The 600-step value then
// drives the sky-band index (brightness * 0.01 -> band 0..6, blend = frac).
// =============================================================================
namespace guild::render {

// Keyframe minutes-of-day, dword_4AD160 [season][6]. Recovered byte-for-byte.
//   season 0: 6:30  8:30 10:30 17:30 19:30 21:30
//   season 1: 5:00  7:00  9:00 19:00 21:00 23:00
//   season 2: 7:00  9:00 11:00 17:00 19:00 21:00
//   season 3: 8:00 10:00 11:30 16:00 17:00 19:00
extern const i32 kDayKeyframeMinutes[4][6];

// gilde.exe 0x4b2438 — build the 6 seconds-of-day keyframe thresholds for a
// season into `outSeconds[6]`. Each = 3600*hour + 60*minute (hour = min/60,
// minute = min%60). Faithful to the byte split the original does (it stores h,m
// as bytes then recombines; identical to a straight seconds conversion).
void BuildTimeTable(int season, i32 outSeconds[6]);

// gilde.exe 0x4b2504 (brightness core) — map a time-of-day to the 0..600
// brightness step using the 6 keyframe thresholds. `hour`/`minute` are the
// wall-clock fields the original read from the world-time record (+4 = hour u16,
// +6 = minute dword). t = 3600*hour + 60*minute.
int UpdateBrightness(const i32 keyframes[6], int hour, int minute);

// flt_61DD68 = 0.01 — brightness-step -> sky-band index scale. The original does
// (double)brightness * 0.01, truncates toward zero (the band), keeps the
// fraction as the cross-band blend, and bands wrap mod 7.
constexpr double kBandScale = 0.01; // dbl_61DD68

// Returns the band index (0..6) and blend fraction for a brightness step, exactly
// as VIBE_DayCycle_UpdateBrightness computes them before calling BlendBandLighting.
struct SkyBandSelect { int band; float blend; };
SkyBandSelect BrightnessToBand(int brightness);

// =============================================================================
// Consolidated time -> sun-state query (the wave-6 day/night DRIVER handoff).
//
// This is the single pure entry the sky / lighting / shadow passes consume. It
// composes the leaf math already reconstructed here and in the band/light
// modules — there is NO new engine behaviour, it is the same chain
// VIBE_DayCycle_UpdateBrightness @0x4b2504 walks before its stateful relight,
// extracted as a deterministic function of the world clock:
//
//   season  = day % 4                        (VIBE_GameTime_GetSeasonFromDay 0x58339c)
//   kf[6]   = BuildTimeTable(season)         (VIBE_DayCycle_BuildTimeTable    0x4b2438)
//   bright  = UpdateBrightness(kf,hour,min)  (brightness core @0x4b253b..0x4b283b)
//   band,bl = BrightnessToBand(bright)       (band = trunc(bright*0.01)%7, blend=frac)
//
// plus the sun-elevation REGIME the original picks at 0x4b26bb..0x4b28f8 when the
// band changes: bands 0..2 take the SUNRISE walk (raise=0 -> positive elevation,
// 0.3..0.9), bands 3..6 take the SUNSET walk (raise=1 -> negative elevation,
// -0.9..-0.3). The actual per-light elevation float is RNG-driven and produced by
// render::SetSunHeight @0x4b24b0 (owned by render_leaves2); this query reports the
// REGIME (raise flag) and the deterministic [lo,hi] elevation envelope so the
// shadow pass can derive the sun-direction sign without re-deriving the band math.
// =============================================================================

// Sun-elevation regime: which VIBE_Light_SetSunHeight @0x4b24b0 branch the current
// band selects, and the deterministic elevation envelope that branch samples in.
//   raise == 0 -> sunrise/day  : elevation in [+0.3, +0.9)  (dbl_61DD48 + r*dbl_61DD38)
//   raise == 1 -> sunset/night : elevation in [-0.9, -0.3)  (dbl_61DD40 - r*dbl_61DD38)
struct SunElevationRegime {
    int   raise;       // the `raise` arg the day-cycle passes to SetSunHeight (0 day / 1 night)
    float elevLo;      // inclusive low bound of the sampled elevation
    float elevHi;      // exclusive high bound of the sampled elevation
};

// dbl_61DD48 / 61DD40 / 61DD38 — sun-height envelope constants (VIBE_Light_SetSunHeight
// @0x4b24b0). Recovered byte-for-byte (get_bytes @0x61dd38). The day branch is
// 0.3 + r*0.6, the night branch is -0.3 - r*0.6, with r = RandomFloatScaled() in
// [0,1). Span 0.6 mirrors render_leaves2::kSunRandSpan (same dbl_61DD38).
constexpr double kSunDayBase   = 0.3;  // dbl_61DD48
constexpr double kSunNightBase = 0.3;  // |dbl_61DD40| (night base, negated)
constexpr double kSunSpan      = 0.6;  // dbl_61DD38

// Map a band index (0..6) to the sun-elevation regime the day-cycle relight uses.
// Bands 0,1,2 -> sunrise (raise=0, positive). Bands 3..6 -> sunset (raise=1,
// negative). This is the (v11%7 < 3) split at 0x4b28f0.
SunElevationRegime SunRegimeForBand(int band);

// Full deterministic sun/day-cycle state for a world time. Everything here is a
// pure function of (day, hour, minute) — the RNG-sampled per-light elevation is
// NOT included (it is produced live by SetSunHeight); instead `regime` gives the
// branch + envelope. This is the struct the sky/light/shadow agents consume.
struct SunState {
    int   season;     // day % 4
    i32   keyframes[6]; // BuildTimeTable(season) seconds-of-day thresholds
    int   brightness; // 0..600 (UpdateBrightness)
    int   band;       // 0..6 sky band (trunc(brightness*0.01) % 7)
    float blend;      // cross-band blend fraction [0,1)
    SunElevationRegime regime; // sun-elevation branch + envelope for `band`
};

// gilde.exe composed chain (0x58339c -> 0x4b2438 -> 0x4b253b -> band split) —
// compute the full time-of-day sun state. `day` selects the season; `hour`/`minute`
// are the wall-clock fields from the world-time record (a1+4 / a1+6 in 0x4b2504).
SunState ComputeSunState(i32 day, int hour, int minute);

} // namespace guild::render

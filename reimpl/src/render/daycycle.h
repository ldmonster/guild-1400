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

} // namespace guild::render

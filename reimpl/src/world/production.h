#pragma once
// Production output integration over game-time for the Guild economy (gilde.exe).
//
// Translated functions:
//   VIBE_Production_ComputeOutputOverTime  0x59064c
//   VIBE_Production_ComputeDailyHourOutput 0x590a3c
//
// Both integrate "work minutes" between a start and an end game-time across a
// per-weekday daily work window. The float helper VIBE_Coord_ConvertX 0x5c6b08
// is a frndint-with-truncation rounder (round toward zero); every
//   v=x; VIBE_Coord_ConvertX(); (int)v   ==   trunc(x)
// in the decompiled output is therefore just truncation toward zero, which we
// reproduce with a (long long) cast on float intermediates.
//
// Recovered constants (byte-for-byte from the binary):
//   flt_6476FC[4] start hour per weekday = {8, 7, 8, 9}      (window open)
//   flt_64770C[4] end   hour per weekday = {20, 21, 20, 19}  (window close)
//   flt_626A04    minute scale          = 60.0
//   ComputeDailyHourOutput uses a fixed window [6,23] h with scale flt_626A0C=60.
#include "guild/common/types.h"

namespace guild::world {

// A game timestamp as the production code reads it: day + (hour, minute) of day.
// (The original packs {hour:u16, minute:i32, second:i32}; production only uses
// minute-of-day = 60*hour + minute and the day counter.)
struct ProdTime {
    i32 day;
    i32 hour;
    i32 minute;
};

// Per-weekday work-window tables (gilde.exe flt_6476FC / flt_64770C).
extern const float kWorkStartHour[4];  // {8, 7, 8, 9}
extern const float kWorkEndHour[4];    // {20, 21, 20, 19}
constexpr float kMinuteScale = 60.0f;  // flt_626A04

// gilde.exe 0x59064c — VIBE_Production_ComputeOutputOverTime.
// Returns the number of in-window work minutes between `start` and `end`,
// summing each day's clamped [start*60, end*60] window. The weekday selecting
// the window is start.day % 4 (the original indexes flt_6476FC[a1[0] % 4]).
//   - if `pauseMode` (orig: word_63C740 & 0x80) the windows are ignored and the
//     plain minute difference is returned.
//   - if start is strictly after end, returns 0.
int ProductionComputeOutputOverTime(const ProdTime& start, const ProdTime& end,
                                    bool pauseMode);

// gilde.exe 0x590a3c — VIBE_Production_ComputeDailyHourOutput.
// Same integration with a FIXED window [6,23] hours (scale flt_626A0C = 60),
// no weekday selection and no pause-mode branch.
int ProductionComputeDailyHourOutput(const ProdTime& start, const ProdTime& end);

} // namespace guild::world

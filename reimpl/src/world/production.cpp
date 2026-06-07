#include "world/production.h"

namespace guild::world {

// gilde.exe flt_6476FC / flt_64770C (recovered).
const float kWorkStartHour[4] = {8.0f, 7.0f, 8.0f, 9.0f};
const float kWorkEndHour[4]   = {20.0f, 21.0f, 20.0f, 19.0f};

namespace {

// trunc(x) toward zero — what every "v=x; VIBE_Coord_ConvertX(); (int)v" does.
inline int truncToZero(float x) { return static_cast<int>(static_cast<long long>(x)); }

// Plain minute difference between two timestamps (gilde.exe VIBE_GameTime_DiffMinutes
// path used when pause-mode is set): days*1440 + (60*hour+min) delta.
inline int diffMinutes(const ProdTime& a, const ProdTime& b) {
    long long am = static_cast<long long>(a.day) * 1440 + 60 * a.hour + a.minute;
    long long bm = static_cast<long long>(b.day) * 1440 + 60 * b.hour + b.minute;
    return static_cast<int>(bm - am);
}

// start strictly after end? (gilde.exe VIBE_GameTime_Compare(a1,a2) > 0)
inline bool startAfterEnd(const ProdTime& a, const ProdTime& b) {
    if (a.day != b.day) return a.day > b.day;
    int am = 60 * a.hour + a.minute;
    int bm = 60 * b.hour + b.minute;
    return am > bm;
}

// Integrate clamped daily window minutes from `start` to `end`, where the open
// edge is `startMin` and the close edge is `endMin` (in minutes-of-day). This
// reproduces the three-part structure of the originals:
//   (1) first (partial) day from the start time-of-day to the window close,
//   (2) full middle days contributing the whole window,
//   (3) the final day from the window open to the end time-of-day.
int integrateWindow(const ProdTime& start, const ProdTime& end,
                    float startMin, float endMin) {
    int total = 0;
    int startTod = 60 * start.hour + start.minute;
    int endTod   = 60 * end.hour + end.minute;

    if (start.day >= end.day) {
        // Same day: clamp both edges to the window and take the span.
        float lo = static_cast<float>(startTod);
        if (lo < startMin) lo = startMin;
        if (lo > endMin)   lo = endMin;
        float hi = static_cast<float>(endTod);
        if (hi < startMin) hi = startMin;
        if (hi > endMin)   hi = endMin;
        int span = truncToZero(hi) - truncToZero(lo);
        return span > 0 ? span : 0;
    }

    // (1) first partial day: from current time-of-day up to window close.
    {
        float lo = static_cast<float>(startTod);
        if (lo < startMin) lo = startMin;   // max(startTod, open)
        if (lo > endMin)   lo = endMin;      // min(.., close)
        total += truncToZero(endMin) - truncToZero(lo);
    }

    // (2) full middle days: whole window each.
    int fullWindow = truncToZero(endMin) - truncToZero(startMin);
    for (int day = start.day + 1; day < end.day; ++day)
        total += fullWindow;

    // (3) final day: from window open up to the end time-of-day.
    {
        float hi = static_cast<float>(endTod);
        if (hi < startMin) hi = startMin;    // max(open, endTod)
        if (hi > endMin)   hi = endMin;       // min(.., close)
        total += truncToZero(hi) - truncToZero(startMin);
    }
    return total;
}

} // namespace

// gilde.exe 0x59064c — VIBE_Production_ComputeOutputOverTime.
int ProductionComputeOutputOverTime(const ProdTime& start, const ProdTime& end,
                                    bool pauseMode) {
    int weekday = ((start.day % 4) + 4) % 4;  // a1[0] % 4 (guard negative days)
    float startMin = kWorkStartHour[weekday] * kMinuteScale;  // flt_6476FC*60
    float endMin   = kWorkEndHour[weekday]   * kMinuteScale;  // flt_64770C*60

    if (pauseMode)                          // word_63C740 & 0x80
        return diffMinutes(start, end);
    if (startAfterEnd(start, end))          // Compare(a1,a2) > 0
        return 0;
    return integrateWindow(start, end, startMin, endMin);
}

// gilde.exe 0x590a3c — VIBE_Production_ComputeDailyHourOutput.
//   fixed window [6,23] hours, scale flt_626A0C == 60.0.
int ProductionComputeDailyHourOutput(const ProdTime& start, const ProdTime& end) {
    if (startAfterEnd(start, end))
        return 0;
    const float kScale = 60.0f;  // flt_626A0C
    float startMin = 6.0f  * kScale;
    float endMin   = 23.0f * kScale;
    return integrateWindow(start, end, startMin, endMin);
}

} // namespace guild::world

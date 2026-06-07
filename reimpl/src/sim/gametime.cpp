#include "sim/gametime.h"

namespace guild::sim {

// gilde.exe 0x583150 — VIBE_GameTime_Advance.
//
// Faithful translation of the Hex-Rays pseudocode. The original addresses raw
// byte offsets off a base pointer (+0 day, +4 hour word, +6 minute dword,
// +10 second dword); we use the GameTime struct fields (same offsets). The
// division/modulo are signed truncating (C semantics match the original's
// idiv), and the result hour is what the function returns.
int GameTimeAdvance(GameTime* rec, int addDays, int addSeconds, int addMinutes) {
    // v6 = addSeconds + second; second = v6
    i32 v6 = addSeconds + rec->second;
    rec->second = v6;

    // v7 = (i64)v6 (sign-extended); minute += v7/60
    i64 v7 = static_cast<i64>(v6);
    i32 v8 = rec->second;            // == v6
    rec->minute += static_cast<i32>(v7 / 60);

    // v9 = addMinutes + minute; second = v8 % 60; minute = v9
    i32 v9 = addMinutes + rec->minute;
    rec->second = v8 % 60;
    i32 hourLo = static_cast<i32>(rec->hour); // LOWORD(v8) = *(a1+4)
    rec->minute = v9;

    // hour = v9/60 + hour
    rec->hour = static_cast<u16>(v9 / 60 + hourLo);

    // result = addDays + (u16)hour
    int result = addDays + static_cast<int>(rec->hour);

    // minute %= 60; then carry hours -> days (wrap 24)
    rec->minute %= 60;
    for (; result >= 24; ++rec->day)
        result -= 24;

    if (result < 0) {
        do {
            result += 24;
            --rec->day;
        } while (result < 0);
        rec->hour = static_cast<u16>(result);
    } else {
        rec->hour = static_cast<u16>(result);
    }
    return result;
}

// gilde.exe 0x583230 — VIBE_GameTime_Compare. Day field first (raw dword), then
// total seconds-of-day. Returns -1 / 0 / +1.
int GameTimeCompare(const GameTime* a, const GameTime* b) {
    if (a->day < b->day)
        return -1;
    if (b->day < a->day)
        return 1;
    i32 av = 3600 * static_cast<i32>(a->hour) + 60 * a->minute + a->second;
    i32 bv = 3600 * static_cast<i32>(b->hour) + 60 * b->minute + b->second;
    if (av < bv)
        return -1;
    return av > bv;            // 1 if a>b else 0
}

// gilde.exe 0x5832bc — VIBE_GameTime_DiffMinutes. (b - a) in minutes; seconds
// field is intentionally ignored (matches the original arithmetic exactly).
int GameTimeDiffMinutes(const GameTime* a, const GameTime* b) {
    return (b->minute - a->minute)
         + 1440 * (b->day - a->day)
         + 60 * (static_cast<i32>(b->hour) - static_cast<i32>(a->hour));
}

} // namespace guild::sim

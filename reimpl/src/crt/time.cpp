#include "crt/time.h"

namespace guild::crt {

namespace {

// ---------------------------------------------------------------------------
// Cumulative day-of-year tables recovered verbatim from the binary.
//
//   _days  (normal year): dword_62CEC6+2  @0x62cec8
//   _lpdays (leap year):  dword_62CEE0+2  @0x62cee2
//
// Each is _days[i] = number of days before month i (i in 0..12, where entry 12
// is the day count of the whole year). The original reads 16-bit entries and,
// for the month search, reads _days[i+1] via a 32-bit load shifted right 16.
// ---------------------------------------------------------------------------
constexpr i16 kDays[13] = {
    0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365,
};
constexpr i16 kLpDays[13] = {
    0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366,
};

// Days from the 1900 serial origin (1900-01-01) to the Unix epoch (1970-01-01).
// Used by gmtime as the day-zero offset (dayOrigin in ConvertToTmFields).
constexpr int kEpochDayOrigin = 25567;

} // namespace

// gilde.exe 0x606900 — VIBE_Time_IsLeapYear  (__usercall, eax = (year@eax))
bool IsLeapYear(u32 year) {
    if ((year & 3) != 0)
        return false;
    if (year % 100u != 0)
        return true;
    return (year % 400u) == 0;
}

// gilde.exe 0x606d70 — VIBE_Time_DaysSinceEpochForYear  (__usercall, eax = (yearMinus1900@eax))
// Faithful copy of the original mixed signed/unsigned day arithmetic. The +1899
// terms convert the years-since-1900 value into a 1-based year count for the
// leap-day corrections; the -460 folds in the 1900-origin century corrections.
u32 DaysSinceEpochForYear(int yearMinus1900) {
    int y = yearMinus1900 + 1899;
    return (static_cast<u32>(y) >> 2)
           - static_cast<u32>(y) / 100u
           + static_cast<u32>(y) / 400u
           - 460u
           + 365u * static_cast<u32>(yearMinus1900);
}

// gilde.exe 0x606dc4 — VIBE_Time_ConvertToTmFields
//   (__usercall, eax = (out@ecx, seconds@edx, dayOrigin@eax, tzOffsetSecs@ebx))
TmFields* ConvertToTmFields(TmFields* out, u32 seconds, int dayOrigin, int tzOffsetSecs) {
    u32 daySecs;   // seconds-of-day, after applying tz offset (v5)
    u32 totalDays; // serial day index from the 1900 origin (v6)

    // 0x15180 = 86400 (seconds/day). Borrow a day when the tz offset would push
    // the seconds-of-day negative, exactly as the original branch does.
    if (seconds >= 0xA8C0u /* 43200 */ || tzOffsetSecs <= 0) {
        daySecs = seconds - static_cast<u32>(tzOffsetSecs);
        totalDays = (seconds - static_cast<u32>(tzOffsetSecs)) / 86400u + static_cast<u32>(dayOrigin);
    } else {
        daySecs = seconds + 86400u - static_cast<u32>(tzOffsetSecs);
        totalDays = daySecs / 86400u + static_cast<u32>(dayOrigin) - 1u;
    }

    // Time-of-day fields. 0xE10 = 3600, 0x3C = 60.
    out->hour = static_cast<int>(daySecs % 86400u / 3600u);
    out->min  = static_cast<int>(daySecs % 86400u % 3600u / 60u);
    out->sec  = static_cast<int>(daySecs % 86400u % 3600u % 60u);

    // Estimate the year from the serial day, then walk back while the day-of-year
    // is negative. 0x16D = 365. The original tracks the years-since-1900 value in
    // v7 and the full year in v9 in lockstep.
    int yearMinus1900 = static_cast<int>(totalDays / 365u);
    int yday = static_cast<int>(totalDays) - static_cast<int>(DaysSinceEpochForYear(yearMinus1900));
    if (yday < 0) {
        int fullYear = yearMinus1900 + 1899;
        do {
            int leap = IsLeapYear(static_cast<u32>(fullYear)) ? 1 : 0;
            --yearMinus1900;
            yday += leap + 365;
            --fullYear;
        } while (yday < 0);
    }

    out->year = yearMinus1900;
    out->yday = yday;

    const i16* table = kDays;
    if (IsLeapYear(static_cast<u32>(yearMinus1900 + 1900)))
        table = kLpDays;

    // Month search: start from yday/31 (a lower bound), bump if past month end.
    int mon = yday / 31;
    if (yday >= table[mon + 1])
        ++mon;
    out->mon  = mon;
    out->mday = yday - table[mon] + 1;

    // 1900-01-01 was a Monday; the serial origin makes (totalDays+1)%7 the weekday
    // with 0 == Sunday.
    out->wday = static_cast<int>((totalDays + 1u) % 7u);
    return out;
}

// gilde.exe 0x606ecc — VIBE_Time_GmtimeFromUnix  (__usercall, eax = (unixTime@eax, out@edx))
TmFields* GmtimeFromUnix(const u32* unixTime, TmFields* out) {
    out->isdst = 0;
    return ConvertToTmFields(out, *unixTime, kEpochDayOrigin, 0);
}

TmFields Gmtime(u32 unixTime) {
    TmFields out{};
    GmtimeFromUnix(&unixTime, &out);
    return out;
}

// gilde.exe 0x5fdff0 — VIBE_Crt_MakeTimeFromTm  (__usercall, eax = (tm@eax))  — UTC portion.
//
// The original also consults the global timezone/DST state (VIBE_Time_TzSet,
// dword_64AFC4 == tz offset, dword_64AFCC == dst bias, VIBE_Time_IsInDaylightSaving).
// That machinery is loaded from the host OS at runtime and is not reimplemented
// here (see deferred list in the module header). With the recovered runtime tz
// offset being zero in the static image, the UTC path below is the faithful
// behaviour for isdst<=0 / no active DST rules. The field normalization, the
// 1970..2038 range guard, and the day arithmetic are translated 1:1.
i32 MakeTimeUtc(const TmFields* tm) {
    const i16* table = kDays;

    int year = tm->year; // years since 1900 (a1[5])
    int mon = tm->mon % 12;
    if (year < -184844639)
        return -1;

    int yearWalk = tm->mon / 12 + year;
    while (mon < 0) {
        mon += 12;
        --yearWalk;
    }
    if (yearWalk < 0)
        return -1;

    if (IsLeapYear(static_cast<u32>(yearWalk + 1900)))
        table = kLpDays;

    // Day serial from the 1900 origin. The original computes the leap-day count
    // inline via shifts equivalent to floor division by 4/100/400.
    int days = tm->mday
               + (yearWalk + 299) / 400
               + 365 * yearWalk
               + ((yearWalk + 3) >> 2)        // floor((yearWalk+3)/4)
               - (yearWalk + 99) / 100
               + table[mon]
               - 1;

    int secOfDay = 60 * (60 * tm->hour + tm->min) + tm->sec;
    while (secOfDay < 0) {
        secOfDay += 86400;
        --days;
    }

    // UTC: no timezone offset (dword_64AFC4 == 0 in the recovered snapshot) and
    // isdst<=0 means no DST bias is subtracted.
    while (secOfDay < 0) {
        --days;
        secOfDay += 86400;
    }

    // Range guard: 25566 is the serial day of 1969-12-31; 25567 is 1970-01-01.
    if (days < 25566)
        return -1;
    if (days != 25566)
        return 86400 * (days - kEpochDayOrigin) + secOfDay;

    // Exactly on the day before the epoch: only representable if the seconds roll
    // it into 1970-01-01 (and only when a positive tz offset existed, which it
    // does not here) — matches the original's dword_64AFC4 <= 0 rejection.
    int v = secOfDay - 86400;
    if (v < 0)
        return -1;
    return v;
}

i32 MakeTimeUtc(int year, int mon, int mday, int hour, int min, int sec) {
    TmFields tm{};
    tm.year = year - 1900;
    tm.mon = mon - 1; // accept 1-based month, convert to tm_mon
    tm.mday = mday;
    tm.hour = hour;
    tm.min = min;
    tm.sec = sec;
    tm.isdst = -1;
    return MakeTimeUtc(&tm);
}

DstConstants RecoveredDstConstants() {
    // dword_14552F0/F4/F8 — runtime snapshot in the static image.
    return DstConstants{-3600, 1, -3600};
}

// ---------------------------------------------------------------------------
// TimeBase  (winmm multimedia-timer dispatcher in the original).
// ---------------------------------------------------------------------------

TimeBase::TimeBase(shim::IPlatform* platform) : platform_(platform) {}

int TimeBase::findSlot(TimeBaseProc proc) const {
    for (int i = 0; i < kMaxProcs; ++i)
        if (slots_[i].proc == proc)
            return i;
    return -1;
}

// gilde.exe 0x44e240 — VIBE_TimeBase_StartTimer  (__usercall, eax = (delayMs@eax, periodic@edx))
// The original probes the device caps and refuses delays below wPeriodMin; we
// model a 1 ms floor (the smallest meaningful interval). On success the original
// reseeds the RNG from timeGetTime; here we just record the clock baseline.
int TimeBase::StartTimer(u32 delayMs, int periodic) {
    if (delayMs < 1u)
        return 0;
    delayMs_ = delayMs;
    periodic_ = (periodic != 0);
    running_ = true;
    lastTickMs_ = platform_ ? platform_->timeMs() : 0u;
    return 1;
}

// gilde.exe 0x44e2c4 — VIBE_TimeBase_StopTimer
void TimeBase::StopTimer() {
    running_ = false;
}

// gilde.exe 0x44e308 — VIBE_TimeBase_RegisterProc  (__usercall, eax = (proc@eax, intervalTicks@edx))
int TimeBase::RegisterProc(TimeBaseProc proc, int intervalTicks) {
    // Mirror the original's do/while stride-3 scan: count slots (1-based) until an
    // empty slot is found OR all 32 are examined. When the registry is full the
    // loop stops at slot 31 and the entry is OVERWRITTEN (the v4>32 error path is
    // unreachable given the v5<96 guard) — preserve that behaviour exactly.
    int count = 0;
    int i = 0;
    while (true) {
        ++count;
        bool occupied = (slots_[i].proc != nullptr);
        i += 1; // original advances v5 by 3 dwords == one slot
        if (!occupied || i >= kMaxProcs)
            break;
    }
    if (count > kMaxProcs) {
        // VIBE_ErrorLog_ReportMessage("tb_OpenFunction: Too many TimeBaseProcs!")
        return 0;
    }
    int idx = count - 1;
    slots_[idx].proc = proc;
    slots_[idx].interval = intervalTicks;
    slots_[idx].paused = 0;
    return 1;
}

// gilde.exe 0x44e370 — VIBE_TimeBase_UnregisterProc  (__usercall, eax = (proc@eax))
int TimeBase::UnregisterProc(TimeBaseProc proc) {
    int idx = findSlot(proc);
    if (idx < 0)
        return 0;
    slots_[idx].proc = nullptr;
    return 1;
}

// gilde.exe 0x44e3ac — VIBE_TimeBase_SetProcInterval  (__usercall, eax = (proc@eax, pause@edx))
// Writes the gate flag at slot+2 (dword_B537D0); the reverser's name is a
// misnomer. A non-zero value pauses the proc.
int TimeBase::SetProcPaused(TimeBaseProc proc, int pause) {
    int idx = findSlot(proc);
    if (idx < 0)
        return 0;
    slots_[idx].paused = pause;
    return 1;
}

// gilde.exe 0x44e3e4 — VIBE_TimeBase_IsProcActive  (__usercall, eax = (proc@eax))
int TimeBase::IsProcPaused(TimeBaseProc proc) {
    int idx = findSlot(proc);
    if (idx < 0)
        return 0;
    return slots_[idx].paused ? 1 : 0;
}

// gilde.exe 0x44e130 — fptc (the winmm timer callback)
void TimeBase::Tick() {
    if (reentryGuard_)
        return; // dword_62EB48 re-entry guard
    reentryGuard_ = true;
    ++masterTicks_; // dword_62EB44

    // dword_62EB4C (a global pause flag) defaults false here.
    for (int i = 0; i < kMaxProcs; ++i) {
        const Slot& s = slots_[i];
        if (s.proc && s.interval != 0 && (tickCounter_ % static_cast<u32>(s.interval)) == 0u
            && s.paused == 0) {
            s.proc();
        }
    }
    if (tickCounter_ % 3u == 0u)
        ++div3_; // dword_62EB3C
    if ((tickCounter_ & 1u) != 0u)
        ++div2_; // dword_62EB40

    // Slow-motion: advance the interval clock once per 20 ticks when enabled.
    if (!slowMode_) {
        ++tickCounter_;
    } else {
        u32 v = (slowCounter_ + 1u) % 20u;
        ++slowCounter_;
        if (v == 0u)
            ++tickCounter_;
    }

    reentryGuard_ = false;
}

void TimeBase::PumpFromClock() {
    // Both StartTimer modes are continuous in the original: a2 == 0 arms a
    // TIME_PERIODIC timer (@0x44e28c fuEvent = (a2 == 0)); a2 != 0 arms a
    // one-shot that fptc re-arms each tick (@0x44e209, dword_62EB50). So the
    // pump never self-stops; only StopTimer @0x44e2c4 ends the ticking.
    if (!running_ || !platform_ || delayMs_ == 0)
        return;
    u32 now = platform_->timeMs();
    while (now - lastTickMs_ >= delayMs_) {
        lastTickMs_ += delayMs_;
        Tick();
    }
}

} // namespace guild::crt

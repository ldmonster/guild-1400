#pragma once
#include "guild/common/types.h"
#include "shim/IPlatform.h"

// CRT calendar/time core from gilde.exe, plus the winmm-backed "TimeBase"
// multimedia-timer dispatcher.
//
// Two independent pieces live here:
//
//  1. Pure calendar math (self-contained, translated 1:1):
//       VIBE_Time_IsLeapYear            @0x606900
//       VIBE_Time_DaysSinceEpochForYear @0x606d70
//       VIBE_Time_ConvertToTmFields     @0x606dc4   (gmtime core: secs -> tm)
//       VIBE_Time_GmtimeFromUnix        @0x606ecc
//       VIBE_Crt_MakeTimeFromTm (UTC part) @0x5fdff0 / VIBE_Time_MakeTime @0x1425f0f
//     The month/day cumulative tables (_days / _lpdays) are recovered exactly
//     from dword_62CEC6 (normal) and dword_62CEE0 (leap). The epoch is the
//     Unix epoch; the original counts days from a 1900-based serial (25567 =
//     days from 1900-01-01 to 1970-01-01) and reuses it as the gmtime origin.
//
//  2. TimeBase: a periodic-callback dispatcher driven in the original by the
//     winmm multimedia timer (timeSetEvent/timeGetTime, callback `fptc`
//     @0x44e130). Here it is driven by shim::IPlatform::timeMs() and a manual
//     Tick(), so it is fully testable against a mock clock. The DST constants
//     recovered from the binary (_timezone/_daylight/_dstbias) are exposed but
//     the OS-coupled tz-rule machinery is documented as deferred (see .cpp).
namespace guild::crt {

// Broken-down time, field order matching the original 9-int tm-like record the
// CRT routines fill. Offsets are the int index used by the pseudocode.
struct TmFields {
    int sec;     // [0]  tm_sec    0..59
    int min;     // [1]  tm_min    0..59
    int hour;    // [2]  tm_hour   0..23
    int mday;    // [3]  tm_mday   1..31
    int mon;     // [4]  tm_mon    0..11
    int year;    // [5]  tm_year   years since 1900
    int wday;    // [6]  tm_wday   0=Sunday
    int yday;    // [7]  tm_yday   0..365
    int isdst;   // [8]  tm_isdst
};

// VIBE_Time_IsLeapYear @0x606900 (__usercall, eax = (year@eax)).
// Gregorian rule; `year` is the full year (e.g. 1972).
bool IsLeapYear(u32 year);

// VIBE_Time_DaysSinceEpochForYear @0x606d70 (__usercall, eax = (yearMinus1900@eax)).
// Number of days from the 1900 serial origin to the start of `yearMinus1900`
// (a years-since-1900 value), via the original 365*y + leap-day arithmetic.
u32 DaysSinceEpochForYear(int yearMinus1900);

// VIBE_Time_ConvertToTmFields @0x606dc4
//   (__usercall, eax = (out@ecx, seconds@edx, dayOrigin@eax, tzOffsetSecs@ebx)).
// The gmtime/localtime core: decompose `seconds` (a count of seconds whose
// day-zero is `dayOrigin` days before the 1900 serial origin) minus a timezone
// offset `tzOffsetSecs` into `out`. For UTC gmtime use dayOrigin=25567, tz=0.
// Returns `out`.
TmFields* ConvertToTmFields(TmFields* out, u32 seconds, int dayOrigin, int tzOffsetSecs);

// VIBE_Time_GmtimeFromUnix @0x606ecc (__usercall, eax = (unixTime@eax, out@edx)).
// Convert a Unix time_t (seconds since 1970-01-01 UTC) to UTC broken-down time.
TmFields* GmtimeFromUnix(const u32* unixTime, TmFields* out);

// Convenience wrapper around GmtimeFromUnix.
TmFields Gmtime(u32 unixTime);

// VIBE_Crt_MakeTimeFromTm @0x5fdff0 (__usercall, eax = (tm@eax)), UTC portion.
// Inverse of gmtime: turn broken-down fields back into a Unix time_t, treating
// the input as UTC (isdst<=0, no tz/DST adjustment). Normalizes out-of-range
// month/day/hour/min/sec exactly as the original. Returns -1 on under/overflow
// of the representable range (matches the original's 1970..2038 guard).
i32 MakeTimeUtc(const TmFields* tm);

// Convenience: build a Unix time_t from explicit UTC field values.
i32 MakeTimeUtc(int year, int mon, int mday, int hour, int min, int sec);

// DST/timezone constants recovered from the binary's runtime snapshot
// (dword_14552F0/F4/F8 == _timezone/_daylight/_dstbias). Provided for fidelity;
// the active-rule machinery they feed is OS-coupled and not reimplemented here.
struct DstConstants {
    i32 timezoneSecs;  // _timezone  @0x14552F0 == -3600
    i32 daylight;      // _daylight  @0x14552F4 ==  1
    i32 dstBiasSecs;   // _dstbias   @0x14552F8 == -3600
};
DstConstants RecoveredDstConstants();

// ---------------------------------------------------------------------------
// TimeBase: periodic-callback dispatcher (winmm timer in the original).
// ---------------------------------------------------------------------------

using TimeBaseProc = void (*)();

class TimeBase {
public:
    // 32-slot registry, mirroring dword_B537C8 (3 dwords per slot in the binary).
    static constexpr int kMaxProcs = 32;

    // The platform supplies the monotonic millisecond clock (timeGetTime). The
    // pointer must outlive the TimeBase.
    explicit TimeBase(shim::IPlatform* platform);

    // VIBE_TimeBase_StartTimer @0x44e240 (__usercall, eax = (delayMs@eax, periodic@edx)).
    // Begin the timer with `delayMs` between ticks. `periodic` mirrors the
    // original's repeat flag (non-zero => keep rescheduling). Returns 1 on
    // success, 0 if `delayMs` is below the device minimum. The original also
    // reseeds the RNG from timeGetTime on success; we record the start time.
    int StartTimer(u32 delayMs, int periodic);

    // VIBE_TimeBase_StopTimer @0x44e2c4.
    void StopTimer();

    // VIBE_TimeBase_RegisterProc @0x44e308 (__usercall, eax = (proc@eax, intervalTicks@edx)).
    // Add `proc` firing every `intervalTicks` ticks. Returns 1, or 0 if full.
    int RegisterProc(TimeBaseProc proc, int intervalTicks);

    // VIBE_TimeBase_UnregisterProc @0x44e370 (__usercall, eax = (proc@eax)).
    int UnregisterProc(TimeBaseProc proc);

    // VIBE_TimeBase_SetProcInterval @0x44e3ac (__usercall, eax = (proc@eax, pause@edx)).
    // Despite the reverser's name, this writes the per-proc gate flag at slot+2:
    // a non-zero value pauses the proc (the tick skips it). Returns 1 if found.
    int SetProcPaused(TimeBaseProc proc, int pause);

    // VIBE_TimeBase_IsProcActive @0x44e3e4 (__usercall, eax = (proc@eax)).
    // Returns 1 if the proc's gate flag (slot+2) is non-zero, i.e. paused.
    int IsProcPaused(TimeBaseProc proc);

    // The original's `fptc` callback @0x44e130: advance one tick, fire any due
    // procs, and bump the shared frame counters. Pump this whenever the mock
    // clock has advanced by at least the configured delay; in tests, call it
    // once per simulated timer interval.
    void Tick();

    // Drive the dispatcher from the platform clock: issue one Tick() for each
    // whole `delayMs` interval elapsed since the last call (or StartTimer).
    void PumpFromClock();

    // Shared frame counters bumped by Tick() (consumed by other modules):
    u32 MasterTicks() const { return masterTicks_; }   // dword_62EB44, every tick
    u32 ThrottledTicks() const { return tickCounter_; } // dword_62EB38, interval clock
    u32 Div3Counter() const { return div3_; }           // dword_62EB3C
    u32 Div2Counter() const { return div2_; }           // dword_62EB40

    // byte_62EB54 / dword_62EB58: when enabled, throttle the interval clock to
    // advance once per 20 ticks (slow-motion mode in the original).
    void SetSlowMode(bool on) { slowMode_ = on; }

private:
    struct Slot {
        TimeBaseProc proc = nullptr; // +0x00  dword_B537C8
        int interval = 0;            // +0x04  dword_B537CC (ticks between fires)
        int paused = 0;              // +0x08  dword_B537D0 (non-zero => skip)
    };

    int findSlot(TimeBaseProc proc) const;

    shim::IPlatform* platform_;
    Slot slots_[kMaxProcs];

    bool running_ = false;
    bool periodic_ = false;
    bool reentryGuard_ = false; // dword_62EB48
    bool slowMode_ = false;     // byte_62EB54
    u32  delayMs_ = 0;          // uDelay
    u32  lastTickMs_ = 0;       // baseline for PumpFromClock

    u32 masterTicks_ = 0; // dword_62EB44
    u32 tickCounter_ = 0; // dword_62EB38
    u32 div3_ = 0;        // dword_62EB3C
    u32 div2_ = 0;        // dword_62EB40
    u32 slowCounter_ = 0; // dword_62EB58
};

} // namespace guild::crt

#pragma once
// gilde.exe — MSVC CRT POSIX-TZ rule parsing + DST transition helpers
// (the __tzset / _isindst support routines), reconstructed 1:1 from the
// VIBE_Time cluster. These are pure integer/string math with NO platform
// coupling; the OS-coupled pieces (LoadTimeZoneFromSystem @0x606f54 via
// GetTimeZoneInformation, GmtimeToTlsBuffer @0x606eec via TLS, and
// VIBE_Time_GetLocalTime @0x142216c) are deliberately NOT reconstructed here
// (see the cluster manifest for deferral reasons).
//
// Reconstructed here:
//   VIBE_Time_ParseDecimal             @0x6070b0
//   VIBE_Time_ParseTzName              @0x6070dc
//   VIBE_Time_ParseTzRule              @0x6071f4
//   VIBE_Time_ClearTzInitFlag          @0x606f10
//   VIBE_Time_SetTzInitFlag            @0x606f2c
//   VIBE_Time_DstTransitionDayOfYear   @0x60693c
//   VIBE_Time_IsAfterDstStart          @0x606a34
//
// VIBE_Time_IsLeapYear @0x606900 is already reconstructed in src/crt/time.cpp
// (guild::crt::IsLeapYear); it is reused here, not redefined.
#include "guild/common/types.h"
#include "crt/time.h"   // guild::crt::IsLeapYear, TmFields

namespace guild::crt {

using namespace guild;

// MSVC __tzrule_struct as laid out by VIBE_Time_ParseTzRule (byte offsets
// recovered from the disassembly: [ecx+00]=sec ... [ecx+20]=type flag).
struct TzRule {
    i32 sec   = 0;   // +0x00  transition time, seconds component
    i32 min   = 0;   // +0x04  transition time, minutes component
    i32 hour  = 2;   // +0x08  transition time, hours component (default 2)
    i32 week  = 0;   // +0x0C  M-rule week-of-month (1..5)
    i32 month = 0;   // +0x10  M-rule month index (0..11; stored as parsed-1)
    i32 pad14 = 0;   // +0x14  (unused by parser; e.g. transition-day cache)
    i32 wday  = 0;   // +0x18  M-rule weekday (0=Sunday)
    i32 jday  = 0;   // +0x1C  J-rule / n-rule day-of-year
    i32 type  = -1;  // +0x20  rule type: -1=none, 1=J (Julian), 0=M (m.w.d)
};
static_assert(sizeof(TzRule) == 36, "TzRule must be 36 bytes (9 dwords)");

// gilde.exe 0x6070b0 — VIBE_Time_ParseDecimal (__usercall, eax=p, edx=out).
// Parse a run of ASCII digits at `p` into `*out`, returning the pointer past the
// last digit consumed. Faithful to the original loop (accumulator i, byte cast).
const char* TimeParseDecimal(const char* p, i32* out);

// gilde.exe 0x6070dc — VIBE_Time_ParseTzName
//   (__usercall, eax=p, edx=nameOut(>=129 bytes), ebx=offsetOut).
// Copy the time-zone abbreviation at `p` (optionally prefixed by ':') into
// `nameOut` (clamped to 128 chars + NUL), then parse the optional signed
// [+/-]hh[:mm[:ss]] UTC offset into *offsetOut (seconds; negated when the sign
// was '-'). Returns the pointer past the parsed offset. *offsetOut is left
// unchanged if no leading digit follows the name/sign.
const char* TimeParseTzName(const char* p, char* nameOut, i32* offsetOut);

// gilde.exe 0x6071f4 — VIBE_Time_ParseTzRule (__usercall, eax=p, edx=ruleOut).
// Parse a POSIX-TZ DST transition rule (Jn / n / Mm.w.d, optional /hh[:mm[:ss]]
// time) at `p` into `ruleOut`. Returns the pointer past the rule. The default
// transition time is 02:00:00 (hour=2).
const char* TimeParseTzRule(const char* p, TzRule* ruleOut);

// gilde.exe 0x606f10 — VIBE_Time_ClearTzInitFlag.
// dword_64AFD0: clears the low two bits; returns the previous bit0. `flagWord`
// is the in/out holder for dword_64AFD0 (routed by the caller; default 0).
i32 TimeClearTzInitFlag(u32* flagWord);

// gilde.exe 0x606f2c — VIBE_Time_SetTzInitFlag.
// dword_64AFD0: clears the low two bits then sets bit0; returns previous bit0.
i32 TimeSetTzInitFlag(u32* flagWord);

// gilde.exe 0x60693c — VIBE_Time_DstTransitionDayOfYear
//   (__usercall, eax=rule, edx=yearMinus1900).
// Compute the day-of-year (0-based, matching tm_yday) on which the rule fires
// for the given year. Handles the three rule types:
//   type==1 (J, 1..365, Feb-29 never counted): returns rule.jday - 1
//   type==2 (n, 0..365, Feb-29 counted):        returns rule.jday
//   type==0 (Mm.w.d):  computes the w-th weekday d of month m, clamping w==5 to
//                      the last occurrence, using a weekday anchor from the
//                      month's first day (via MakeTimeUtc) and the month-length
//                      tables. `makeTm` supplies the weekday-of-month-start
//                      lookup (== guild::crt::MakeTimeUtc round-trip), defaulting
//                      to the reconstructed CRT implementation.
// NOTE: the original encodes J as type 1 and n as type 2 here, but the parser
// stores J as type 1 and n via the same path; the `type` field carried in TzRule
// uses the parser's convention (1=J, 0=M). The n-rule (no prefix) is parsed with
// type left at the J/none value; see the .cpp for the exact mapping.
i32 TimeDstTransitionDayOfYear(const TzRule* rule, int yearMinus1900);

// gilde.exe 0x606a34 — VIBE_Time_IsAfterDstStart
//   (__usercall, eax=ruleEnd, edx=tzState, ebx=yearMinus1900).
// Returns whether the DST-end transition occurs after the DST-start transition
// for the year, used to decide the southern-hemisphere ordering. `startMonth`
// is the start rule's month (tzState+16 in the original) and `startTypeIsM`
// mirrors the dword at tzState+32 (rule type). When both rules are M-type and
// in different months, the comparison short-circuits on the month; otherwise it
// compares the resolved day-of-year of end vs. start.
bool TimeIsAfterDstStart(const TzRule* endRule, const TzRule* startRule,
                         int yearMinus1900);

} // namespace guild::crt

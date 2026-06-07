#pragma once
#include "guild/common/types.h"

// guild::crt — the CRT `asctime` formatter and a date-field comparator recovered
// 1:1 from gilde.exe. Pure (no OS/heap); the asctime path writes a fixed 26-byte
// "Www Mmm dd hh:mm:ss yyyy\n\0" string from a `struct tm`-shaped 9-int record.
//
// Provenance:
//   FormatTwoDigits     0x5e55c0  VIBE_Crt_FormatTwoDigits
//   FormatAsctime       0x5e55f0  VIBE_Crt_FormatAsctime
//   CompareDateFields   0x606d3c  VIBE_Time_CompareDateFields
namespace guild::crt {

// The `struct tm` layout the binary uses (9 ints, matching MSVC):
//   [0]=tm_sec [1]=tm_min [2]=tm_hour [3]=tm_mday [4]=tm_mon [5]=tm_year(since1900)
//   [6]=tm_wday [7]=tm_yday [8]=tm_isdst
struct TmRec {
    int sec, min, hour, mday, mon, year, wday, yday, isdst;
};

// VIBE_Crt_FormatTwoDigits @0x5e55c0 — write the 2-digit decimal of `value`
// (0..99) into buf[pos] and buf[pos+1] as ASCII ('0'+tens, '0'+ones). Returns the
// ones-digit char. (__usercall value@eax, pos@edx, buf@ebx.)
char FormatTwoDigits(int value, int pos, char* buf);

// VIBE_Crt_FormatAsctime @0x5e55f0 — write the 26-byte asctime string for `tm`
// into `buf`: "Www Mmm dd hh:mm:ss yyyy\n\0". A leading-zero day is rendered as a
// space (buf[8]). The year is split as (year/100 + 19) and (year % 100), each via
// FormatTwoDigits (so tm_year is the MSVC since-1900 value, e.g. 100 -> "2000").
// Returns `buf`.
char* FormatAsctime(const TmRec* tm, char* buf);

// VIBE_Time_CompareDateFields @0x606d3c — return 1 if the date in `a` is strictly
// BEFORE `b`, else 0, comparing [2] (year), then [1] (month), then [0] (day).
// (The original takes two int[3]-shaped pointers ordered {day, month, year}.)
int CompareDateFields(const int* a, const int* b);

} // namespace guild::crt

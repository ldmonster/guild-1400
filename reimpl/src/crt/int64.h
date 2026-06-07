#pragma once
#include "guild/common/types.h"

// CRT 64-bit integer helpers from gilde.exe, namespace guild::crt.
//
// These are the compiler/CRT support routines the MSVC printf/scanf paths use to
// divide, modulo and stringify 64-bit values on the 32-bit x86 target. The two
// "divmod" cores are the classic `_aulldiv`/`_aullrem`-style long-division
// helpers; the string converters are the `_ui64toa`/`_i64toa` radix routines.
namespace guild::crt {

// byte_64C198 @0x64c198 — recovered verbatim: the base-36 digit table
// "0123456789abcdefghijklmnopqrstuvwxyz" used by the 64-bit radix converters.
extern const char kDigit36Table[37];

// gilde.exe 0x1427b10 — VIBE_Math_UInt64Divide  (__stdcall(u64 a, u64 b)).
// Unsigned 64-bit / 64-bit -> 32-bit quotient (the original returns only the low
// 32 bits in eax; callers use it where the quotient is known to fit in 32 bits).
// Faithful to the shift-normalise + estimate-correct long division.
u32 UInt64Divide(u64 a, u64 b);

// gilde.exe 0x1427b80 — VIBE_Math_UInt64Modulo  (__stdcall(u64 a, u64 b)).
// Unsigned 64-bit % 64-bit -> 64-bit remainder.
u64 UInt64Modulo(u64 a, u64 b);

// gilde.exe 0x5e57e7 — VIBE_Math_UnsignedLongLongDivide  (__usercall:
// edx:eax = dividend, ecx = high divisor, ebx = low divisor).  Divides the
// 64-bit dividend by the 64-bit divisor (ecx:ebx) and returns the 64-bit
// quotient.  When the high divisor word is zero (the radix-conversion case) the
// dividend's high word is replaced by the remainder so the caller can read both
// quotient (returned) and remainder (out-param). We expose an explicit divmod.
u64 UnsignedLongLongDivide(u64 dividend, u32 divisor_hi, u32 divisor_lo,
                           u64* remainder);

// gilde.exe 0x609550 — VIBE_String_UInt64ToString  (__usercall: eax=&value,
// edx=dst, ebx=radix).  Writes `*value` in base `radix` (2..36) into `dst` as a
// NUL-terminated string using kDigit36Table. Returns dst.
char* UInt64ToString(u64 value, char* dst, unsigned radix);

// gilde.exe 0x6095e8 — VIBE_String_Int64ToString  (__usercall: eax=&value,
// edx=dst, ebx=radix).  Like UInt64ToString but, for radix 10 only, emits a
// leading '-' for negative values and stringifies the magnitude. Returns dst.
char* Int64ToString(i64 value, char* dst, unsigned radix);

} // namespace guild::crt

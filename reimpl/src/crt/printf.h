#pragma once
#include "guild/common/types.h"
#include <cstdarg>
#include <cstddef>

// CRT printf / sprintf formatting engine.
//
// Reconstructed from the MSVC-style `_output` core in gilde.exe:
//   VIBE_Crt_FormatStringCore   @0x6051f0  — the engine driver (parse + emit)
//   VIBE_Crt_FormatConversion   @0x605a18  — per-conversion value formatting
//   VIBE_Crt_ParseFormatSpec    @0x605598  — width/precision/length parsing
//   VIBE_Crt_ParseFormatFlags   @0x6056ec  — flag (-, +, space, #, 0) parsing
//   VIBE_String_UIntToString    @0x609640  — unsigned -> radix string (digit table)
//   VIBE_String_UInt64ToString  @0x609550  — unsigned 64-bit -> radix string
//   VIBE_Crt_StrToUpper         @0x606020  — uppercase the digit run (X/P/E/G)
//   VIBE_Crt_ComputeZeroPadding @0x605970  — derive zero-pad width
//   VIBE_Crt_Sprintf_0          @0x5cba00  — public int sprintf(dst, fmt, ...) (788 callers)
//   VIBE_Crt_Vsprintf           @0x5f8554  — sprintf core over a va_list
//   VIBE_Buffer_PutChar         @0x5f8540  — buffer output sink
//
// The original engine emits via a caller-supplied character sink (register
// ecx). For sprintf the sink writes to a destination buffer and counts. We
// expose a vsnprintf-style core that writes into a bounded buffer, plus the
// classic unbounded Sprintf entry that mirrors VIBE_Crt_Sprintf_0.
//
// Supported conversions (1:1 with the integer/string path of the original):
//   %d %i %u %o %x %X %p %c %s %%  with flags (- + space # 0), field width
//   (incl. '*'), precision (incl. '*' and '.'), and the h / l / I64 / L length
//   modifiers. Hex uses the original lowercase digit table "0123456789abcdef"
//   then uppercases for %X/%P.
//
// Deferred (see printf.cpp notes and the final report): the original float path
// (VIBE_Crt_FormatFixedFloat @0x605848 / VIBE_Crt_FormatExponentFloat @0x6060be)
// formats 16.16 FIXED-POINT values via VIBE_AnimationState_Update @0x5d92ec, not
// IEEE doubles, so it does not match C's snprintf("%f", double). It is stubbed.
// Wide-char (%C/%S/lc/ls), locale double-byte, %n, and far-pointer (N/F) paths
// are likewise out of scope for the standard byte-string test surface.
namespace guild::crt {

// vsnprintf-style core. Writes up to `cap-1` chars plus a NUL into `dst`
// (when cap>0) and returns the number of characters that the full output would
// contain (like C99 vsnprintf). Faithful to the conversion semantics of
// VIBE_Crt_FormatStringCore @0x6051f0.
int Vsnprintf(char* dst, std::size_t cap, const char* fmt, std::va_list ap);

// Bounded snprintf wrapper.
int Snprintf(char* dst, std::size_t cap, const char* fmt, ...);

// Unbounded sprintf — mirrors VIBE_Crt_Sprintf_0 @0x5cba00: writes the whole
// formatted string plus a terminating NUL into `dst` and returns the length.
int Sprintf(char* dst, const char* fmt, ...);

// Public digit table recovered from byte_64C1C0 (0x64c1c0): "0123456789abcdef".
extern const char kDigitTable[17];

} // namespace guild::crt

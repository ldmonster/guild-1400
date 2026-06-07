#pragma once
#include "guild/common/types.h"
#include <cstdarg>

// CRT scanf engine (the MSVC `_input` core) from gilde.exe, namespace guild::crt.
//
//   VIBE_Crt_ScanFormatCore   @0x5fd070 — the driver: walks the format string,
//                                          matches literals/whitespace, dispatches
//                                          conversions, counts assignments
//   VIBE_Crt_ScanParseSpec    @0x5fd324 — '*' suppress, width, N/F/h/l/I64/L mods
//   VIBE_Crt_ScanSkipWhitespace @0x5fd428
//   VIBE_Crt_ScanReadInteger  @0x5fdb10 — %d/%i/%u/%o/%x/%p, base 0/8/10/16, 64-bit
//   VIBE_Crt_ScanReadFloat    @0x5fd838 — %e/%f/%g (collect digits, strtod)
//   VIBE_Crt_ScanReadString   @0x5fd550 — %s (whitespace-delimited)
//   VIBE_Crt_ScanReadChars    @0x5fd468 — %c (fixed count, default 1)
//   VIBE_Crt_ScanReadCharSet  @0x5fd74c / BuildCharSet @0x5fd708 — %[...]
//   VIBE_Crt_ScanStoreCount   @0x5fd6a8 — %n
//   VIBE_Crt_HexDigitValue    @0x5fdf30 — 0..15 or 16
//   VIBE_Crt_Sscanf @0x5e54c8 / StringScanCore @0x5e54a0 — string-backed entry
//
// The original drives a get/unget character source (function pointers in a small
// stream struct); the public game entry is sscanf over a C string. We expose the
// sscanf entry plus the va_list core.
namespace guild::crt {

// gilde.exe 0x5e54a0/0x5fd070 — sscanf over a C string. Returns the number of
// successfully assigned conversions, or -1 (EOF) if input ended before any.
int Vsscanf(const char* src, const char* fmt, std::va_list ap);

// gilde.exe 0x5e54c8 — variadic sscanf.
int Sscanf(const char* src, const char* fmt, ...);

} // namespace guild::crt

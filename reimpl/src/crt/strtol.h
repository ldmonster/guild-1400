#pragma once
#include "guild/common/types.h"

// CRT numeric parsing (atoi / strtol / strtoul) and the string case operations
// from gilde.exe, namespace guild::crt. This is the "parsing" half of the module;
// the character-class tables and case primitives live in ctype.{h,cpp}.
//
// errno: the original stores errno in the per-thread CRT block
// (off_64A90C()+4 for EINVAL via VIBE_Runtime_SetErrnoEinval, and dword_145A1DC
// for ERANGE). We model a single thread-local int, Errno(), holding the standard
// errno values the routines set (EINVAL=22, ERANGE=34).
namespace guild::crt {

// Standard errno values the parsers set (matches MSVC <errno.h>).
constexpr int kEINVAL = 22;
constexpr int kERANGE = 34;

// Thread-local errno model. SetErrno()/Errno() mirror the per-thread CRT slot.
int& Errno();

// gilde.exe 0x1421c05 — VIBE_Crt_Atoi.  Classic atoi: skip leading whitespace
// (kPctype bit 0x08), optional +/-, then base-10 digits with intentional int
// wraparound. Returns the signed int value.
int Atoi(const char* s);

// gilde.exe 0x60b350 — VIBE_Crt_StrToLong  (__usercall: eax=str, edx=endptr,
// ecx=signed-flag, ebx=base).  The C-runtime strtol/strtoul core:
//   * skips whitespace (kStrtolCtype), optional sign;
//   * base 0 auto-detects 0x->16, 0->8, else 10; base 16 skips a 0x prefix;
//   * accumulates with 32-bit unsigned wraparound and overflow tracking;
//   * on overflow sets errno=ERANGE(0x0E here) and clamps: signed -> LONG_MIN/MAX,
//     unsigned -> ULONG_MAX (returns the value before negation);
//   * `signed_flag`!=0 means treat as signed (strtol); 0 means unsigned (strtoul).
// Returns the (possibly negated) 32-bit result; *endptr (if non-null) gets the
// end of the consumed digits, or the original string if none were valid.
u32 StrToLong(const char* str, const char** endptr, int signed_flag, int base);

// gilde.exe 0x60b4b0 — VIBE_Crt_StrToLongAuto  (__usercall, eax/edx/ebx).
// Thin wrapper: StrToLong(str, endptr, /*signed*/1, base).
u32 StrToLongAuto(const char* str, const char** endptr, int base);

// gilde.exe 0x606020 — VIBE_Crt_StrToUpper (__usercall, eax=str).
// In-place ASCII strupr: maps 'a'..'z' -> uppercase for the whole string.
// Returns the (uppercased) value of the final character processed, matching the
// original's al return (the last VIBE_Util_CharToUpper result).
char* StrToUpper(char* s);

// gilde.exe 0x142225f — VIBE_Strtol_Parse  (__cdecl: str, endptr, base, mode).
// The C standard library strtol/strtoul (distinct from the StrToLong above; this
// is the externally exported `strtol`). `mode` bit 0 selects unsigned (strtoul).
// Uses kPctype for classification and VIBE_Locale_ToUpper for letter digits.
// On overflow sets errno=ERANGE and clamps to LONG_MIN/MAX or ULONG_MAX.
u32 Strtol_Parse(const char* str, const char** endptr, unsigned base, int mode);

// gilde.exe 0x1422248 — VIBE_Strtol_Wrapper.  strtol: Strtol_Parse(s, e, base, 0).
long Strtol(const char* str, const char** endptr, int base);

// gilde.exe 0x1422467 — VIBE_Strtoul_Wrapper. strtoul: Strtol_Parse(s, e, base, 1).
unsigned long Strtoul(const char* str, const char** endptr, int base);

// ---- String case operations (deferred String_* group, modelled here) ----------
// These belong to the String module but were assigned to this agent. Their MBCS
// code-page branch (CaseCodePage()!=0) allocates via the Mem module and calls a
// String copy thunk; that path is deferred. In the C locale (default) they do the
// in-place ASCII fold shown below, which is exact.

// gilde.exe 0x142a446 — VIBE_String_ToUpper.  In-place ASCII strupr; returns s.
char* StringToUpper(char* s);

// gilde.exe 0x142a56d — VIBE_String_ToLower.  In-place ASCII strlwr; returns s.
char* StringToLower(char* s);

// gilde.exe 0x142a610 — VIBE_String_CompareNoCaseN.  Case-insensitive strncmp over
// at most n bytes. Returns -1 / 0 / 1 (sign of the first differing folded byte),
// or 0 when n==0. ASCII fold in the C locale; LocaleToLower otherwise.
int StringCompareNoCaseN(const char* a, const char* b, int n);

} // namespace guild::crt

#pragma once
// =====================================================================================
// Locale reconstruction (string/locale-logic cluster).
//
// Faithful 1:1 translations of two gilde.exe locale routines:
//
//   gilde.exe 0x5a33a0 — VIBE_Locale_CopyLanguageString  (__usercall, eax=s, ecx=?)
//   gilde.exe 0x609f90 — VIBE_Locale_SetupMbcsCodePage   (__usercall, eax=codepage)
//
// Both touch process-global state in the original. We reproduce the observable logic
// exactly; the only OS-dependent inputs of SetupMbcsCodePage (GetACP / GetOEMCP /
// GetCPInfo) are routed through an injectable callback table so the portable build
// links with no Win32 dependency (CLAUDE.md operational note: no direct OS calls in
// src/). The DEFAULT callbacks are inert (return a C-locale style answer), exactly the
// shape the original would observe under a single-byte ANSI code page.
// =====================================================================================
#include "guild/common/types.h"

namespace guild::util {

// -------------------------------------------------------------------------------------
// gilde.exe 0x5a33a0 — VIBE_Locale_CopyLanguageString
//
// Upper-cases the language name `name` (the original copies it 2 bytes at a time into a
// 0x40-byte stack buffer, then VIBE_Util_StrToUpper), then linearly compares it against
// the five known language names laid out as 5 x 64-byte records starting at 0x5a3260:
//
//   [0] "GERMAN"  [1] "ENGLISH"  [2] "FRENCH"  [3] "ITALIAN"  [4] "SPANISH"
//
// Returns the matched index (0..4), or 0xFFFF when no match within the 5 entries.
// Side effect: writes the result (0xFFFF first, then the matched index) to the global
// word_649D48 (the "active language id"); we mirror that via LanguageIdGlobal().
//
// The 0x40-byte stack buffer in the original means names longer than ~63 chars overrun
// in the binary; callers only ever pass short language names, so we reproduce the exact
// compare/scan but use a bounded local buffer of the same 0x40 size.
u32 LocaleCopyLanguageString(const char* name);

// word_649D48 — the active-language id global written by the routine above.
u16& LanguageIdGlobal();

// -------------------------------------------------------------------------------------
// gilde.exe 0x609f90 — VIBE_Locale_SetupMbcsCodePage
//
// Configures the MBCS lead-byte table (byte_140A9D0, 257 bytes) and the active code page
// (CodePage / dword_140A9C0) for the requested code page `cp`:
//
//   cp == 0xFFFFFFFF  -> use ACP   (GetACP)
//   cp == 0xFFFFFFFE  -> use OEMCP (GetOEMCP)
//   cp == 0xFFFFFFFD  -> SBCS reset: clear lead-byte table, dbcs=0, CodePage=0; return 0
//   cp == 0xFFFFFFFC  -> hard-code CP 932 (Shift-JIS) lead ranges 0x81..0x9F, 0xE0..0xFC
//   otherwise         -> GetCPInfo(cp) and mark its LeadByte ranges
//
// Returns 0 on success, 1 if GetCPInfo fails. When cp resolves to 1 (the "no code page"
// fallback) the active CodePage is set from GetOEMCP.
//
// The Win32 inputs are injected (see MbcsHooks); defaults are inert C-locale answers.
struct MbcsHooks {
    // GetACP() — system ANSI code page. Default: 1252 (typical Western).
    unsigned int (*getACP)();
    // GetOEMCP() — system OEM code page. Default: 437.
    unsigned int (*getOEMCP)();
    // GetCPInfo(cp, leadByteRanges) — fill up to 12 LeadByte bytes (0-terminated pairs)
    // for code page `cp`. Return non-zero on success, 0 on failure. `out` points at a
    // 12-byte buffer (matching _cpinfo::LeadByte). Default: success, no lead bytes (SBCS).
    int (*getCPInfo)(unsigned int cp, u8* leadByteOut12);
};

// Mutable singleton hook table (default = inert C-locale answers). Tests/integration may
// install real Win32-backed hooks.
MbcsHooks& MbcsHookTable();

// The MBCS state the routine maintains (byte_140A9D0 lead-byte table, dword_140A9C0 dbcs
// flag, CodePage). Exposed so callers/tests can observe the configured state.
struct MbcsState {
    u8  leadByte[257];   // byte_140A9D0 — 1 where a byte is a DBCS lead byte
    int dbcs;            // dword_140A9C0 — 1 when a multibyte code page is active
    int codePage;        // CodePage      — the resolved active code page
};
MbcsState& MbcsStateGlobal();

int LocaleSetupMbcsCodePage(unsigned int cp);

} // namespace guild::util

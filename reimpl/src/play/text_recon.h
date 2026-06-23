#pragma once
// =====================================================================================
// Text reconstruction (UI string-logic cluster).
//
// Faithful 1:1 translations of gilde.exe text routines that are PURE string/table
// logic, plus injectable hooks for the file/stream-coupled members so the cluster links
// standalone (CLAUDE.md: no direct OS/VFS calls in src/; coupled leaves get inert
// default hooks).
//
//   gilde.exe 0x527c68 — VIBE_Text_FormatBuildVersionString  (__usercall, eax=outBuf)
//   gilde.exe 0x44add4 — VIBE_Text_LookupLabelEntry          (__usercall, eax=name)
//   gilde.exe 0x5e9eb0 — VIBE_Text_PrintfWrapper             (varargs -> flush stub)
//   gilde.exe 0x5e9e30 — VIBE_Text_ReadLine                  (__usercall, eax=buf, ebx=FILE)
//
// Deferred (UI/VFS-coupled, see text_recon.cpp + the cluster report):
//   gilde.exe 0x44ae24 — VIBE_Text_GenerateLabelDefines      (file-emitter dev tool)
//   gilde.exe 0x5a161c — VIBE_Text_RenderCreditsBlock        (window/textDB dispatcher)
// =====================================================================================
#include "guild/common/types.h"

namespace guild::play {

// -------------------------------------------------------------------------------------
// gilde.exe 0x527c68 — VIBE_Text_FormatBuildVersionString
//
// Builds the on-screen version string into `outBuf`. The original:
//   1. qmemcpy's the 12-entry, 4-byte-stride month-name table @0x5271c2
//      ("Jan","Feb",...,"Dec") into a stack array, and a single space (0x0020) after it.
//   2. Copies the compiled build date string "Oct 10 2002" (@0x622984) into a buffer and
//      VIBE_Text_StrtokWhitespace's it into 3 tokens (month / day / year) at a 16-byte
//      stride array.
//   3. Linearly compares the month-name table against the first token to find the month
//      index (0..11). NB: in the original this index is COMPUTED BUT DISCARDED.
//   4. sprintf(outBuf, byte_622990) — where byte_622990 is the fixed German-codepage
//      version string with NO conversion specifiers, so the call simply copies it.
//
// The net observable effect: outBuf receives the constant version string. We reproduce
// the full token/compare logic for fidelity (it has no effect on output) and then emit
// the exact recovered bytes. Returns the byte count written by the sprintf (= strlen of
// the constant string), matching VIBE_Crt_Sprintf_0's return.
int TextFormatBuildVersionString(char* outBuf);

// The exact recovered version-string bytes @0x622990 (German code page; not UTF-8).
// 22 bytes + NUL: "Gilde\xB0 - Versi\xB0 1.03\xF1".
extern const unsigned char kVersionStringBytes[23];

// The build date string @0x622984 used by the routine ("Oct 10 2002").
extern const char kBuildDateString[];

// -------------------------------------------------------------------------------------
// gilde.exe 0x44add4 — VIBE_Text_LookupLabelEntry
//
// Linear scan of a label-name table (unk_77F6B0, 0x3FFF entries x 80-byte stride) using
// case-insensitive compare (VIBE_Util_StrCmpNoCase); on the first match returns the
// parallel int value table entry dword_76BEB0[i]. Returns -1 when not found.
//
// The two tables are a loaded resource bin, so they are injected (LabelTableHooks);
// the default is an empty table (returns -1), matching an unloaded label DB.
struct LabelTableHooks {
    // Number of valid label entries to scan (<= 0x3FFF). Default 0.
    int count;
    // entryName(i) — the NUL-terminated name of label record i.
    const char* (*entryName)(int i);
    // entryValue(i) — the parallel value (dword_76BEB0[i]).
    int (*entryValue)(int i);
};
LabelTableHooks& LabelTableHookTable();

int TextLookupLabelEntry(const char* name);

// -------------------------------------------------------------------------------------
// gilde.exe 0x5e9eb0 — VIBE_Text_PrintfWrapper
//
// In the binary this is a (broken / stripped) printf-to-stream wrapper that effectively
// just flushes the debug stream (VIBE_File_Flush(&unk_64A5AA)) and ignores its varargs.
// We reproduce the observable no-op-with-flush behavior via an injectable sink. Returns
// the flush result (0 by default).
struct DebugSinkHooks {
    // flush() — flush the debug stream. Default returns 0.
    int (*flush)();
};
DebugSinkHooks& DebugSinkHookTable();

int TextPrintfWrapper(const char* fmt, ...);

// -------------------------------------------------------------------------------------
// gilde.exe 0x5e9e30 — VIBE_Text_ReadLine
//
// fgets-equivalent: reads characters from a stream into `buf` until a newline (0x0A) is
// stored, EOF (-1), or the size limit is reached, NUL-terminating. Mirrors the CRT
// implementation (it toggles the stream's 0x30 mode bits around the read and restores
// them). Returns `buf`, or nullptr at EOF with nothing read (or when the stream's 0x20
// error bit is set).
//
// The stream is injected (StreamReadHooks): getc() yields the next byte or -1 at EOF;
// the mode-bit save/restore is modeled by streamMode()/setStreamMode(). `size` is the
// buffer capacity (the original derives the remaining count from ecx; callers pass the
// buffer size). Default hooks model an immediately-EOF stream.
struct StreamReadHooks {
    int  (*getc)();                       // VIBE_Crt_GetcCookedText @0x5fb970 -> byte or -1
    int  (*streamMode)();                 // read *(stream+12) flags
    void (*setStreamMode)(int newMode);   // write *(stream+12) flags
};
char* TextReadLine(char* buf, int size, StreamReadHooks& hooks);

} // namespace guild::play

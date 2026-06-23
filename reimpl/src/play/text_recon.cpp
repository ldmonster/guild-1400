// =====================================================================================
// Text reconstruction — implementation. See text_recon.h for the spec.
// =====================================================================================
#include "play/text_recon.h"

#include <cstdarg>
#include <cstring>

namespace guild::play {

// -------------------------------------------------------------------------------------
// Recovered data (byte-for-byte from gilde.exe).
// -------------------------------------------------------------------------------------

// byte_622990 @0x622990 — fixed German-codepage version string, NO format specifiers:
//   C3 E8 EB FC E4 E8 B0 20 2D 20 C2 E5 F0 F1 E8 B0 20 31 2E 30 33 F1 00
const unsigned char kVersionStringBytes[23] = {
    0xC3, 0xE8, 0xEB, 0xFC, 0xE4, 0xE8, 0xB0, 0x20, 0x2D, 0x20, 0xC2,
    0xE5, 0xF0, 0xF1, 0xE8, 0xB0, 0x20, 0x31, 0x2E, 0x30, 0x33, 0xF1, 0x00,
};

// aOct102002 @0x622984 — the compiled build date.
const char kBuildDateString[] = "Oct 10 2002";

// Month-name table @0x5271c2 — 12 entries, 4-byte stride: "Jan".."Dec".
// gilde.exe 0x5271c2 — word_5271C2 (table base, copied as 12 dwords).
static const char kMonthNames[12][4] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};

// VIBE_Util_StrCmp @0x5d3f10 — byte strcmp, 0 == equal (local faithful copy).
static int UtilStrCmp(const char* a, const char* b) {
    for (;; ++a, ++b) {
        unsigned char ca = static_cast<unsigned char>(*a);
        unsigned char cb = static_cast<unsigned char>(*b);
        if (ca != cb)
            return ca < cb ? -1 : 1;
        if (ca == 0)
            return 0;
    }
}

// VIBE_Util_StrCmpNoCase @0x5cb8f0 — stricmp, 0 == equal (ASCII case fold).
static int UtilStrCmpNoCase(const char* a, const char* b) {
    for (;; ++a, ++b) {
        unsigned char ca = static_cast<unsigned char>(*a);
        unsigned char cb = static_cast<unsigned char>(*b);
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<unsigned char>(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<unsigned char>(cb + 32);
        if (ca != cb)
            return ca < cb ? -1 : 1;
        if (ca == 0)
            return 0;
    }
}

// VIBE_Text_StrtokWhitespace @0x5e9cd0 — strtok over the given delimiter set. A faithful
// local copy: the same persistent-cursor strtok the original drives with " " (space).
// We model only what FormatBuildVersionString needs: tokenize on space.
namespace {
struct StrtokState { char* cursor = nullptr; };
char* StrtokWhitespace(StrtokState& st, char* str, const char* delims) {
    char* s = str ? str : st.cursor;
    if (!s)
        return nullptr;
    auto isDelim = [&](char c) {
        for (const char* d = delims; *d; ++d)
            if (*d == c) return true;
        return false;
    };
    while (*s && isDelim(*s))
        ++s;
    if (!*s) {
        st.cursor = nullptr;
        return nullptr;
    }
    char* tok = s;
    while (*s && !isDelim(*s))
        ++s;
    if (*s) {
        *s = '\0';
        st.cursor = s + 1;
    } else {
        st.cursor = nullptr;
    }
    return tok;
}
} // namespace

// gilde.exe 0x527c68 — VIBE_Text_FormatBuildVersionString
int TextFormatBuildVersionString(char* outBuf) {
    // Tokenize the build date into three 16-byte-stride slots (month/day/year).
    char dateCopy[256];
    {
        // The original copies aOct102002 2 bytes at a time; equivalent to strcpy here.
        const char* src = kBuildDateString;
        char* dst = dateCopy;
        char c0;
        do {
            c0 = src[0];
            dst[0] = c0;
            if (!c0)
                break;
            char c1 = src[1];
            src += 2;
            dst[1] = c1;
            dst += 2;
            if (!c1)
                break;
        } while (true);
    }

    char tokens[3][16];
    std::memset(tokens, 0, sizeof(tokens));
    StrtokState st;
    char* tok = StrtokWhitespace(st, dateCopy, " ");
    int ti = 0;
    while (tok && ti < 3) {
        // copy token into tokens[ti] (2-byte loop in the original == strcpy).
        std::strcpy(tokens[ti], tok);
        ++ti;
        tok = StrtokWhitespace(st, nullptr, " ");
    }

    // Compare month-name table against the first token to find the index 0..11.
    // (Computed in the original, then DISCARDED — kept for fidelity.)
    int monthIndex = 0;
    for (; monthIndex < 12; ++monthIndex) {
        if (UtilStrCmp(kMonthNames[monthIndex], tokens[0]) == 0)
            break;
    }
    (void)monthIndex;

    // sprintf(outBuf, byte_622990) — the format has no conversion specifiers, so it is a
    // verbatim copy of the recovered version-string bytes. Return the written length.
    std::memcpy(outBuf, kVersionStringBytes, sizeof(kVersionStringBytes));  // includes NUL
    return static_cast<int>(std::strlen(reinterpret_cast<const char*>(kVersionStringBytes)));
}

// -------------------------------------------------------------------------------------
// LookupLabelEntry
// -------------------------------------------------------------------------------------
LabelTableHooks& LabelTableHookTable() {
    static LabelTableHooks g_hooks{0, nullptr, nullptr};  // empty label DB by default
    return g_hooks;
}

// gilde.exe 0x44add4 — VIBE_Text_LookupLabelEntry
int TextLookupLabelEntry(const char* name) {
    LabelTableHooks& h = LabelTableHookTable();
    if (!h.entryName || !h.entryValue)
        return -1;  // unk_77F6B0 absent
    int i = 0;
    // The original caps the scan at 0x3FFF entries (the table capacity).
    int limit = h.count < 0x3FFF ? h.count : 0x3FFF;
    while (i < limit) {
        if (UtilStrCmpNoCase(h.entryName(i), name) == 0)
            return h.entryValue(i);
        ++i;
    }
    return -1;
}

// -------------------------------------------------------------------------------------
// PrintfWrapper
// -------------------------------------------------------------------------------------
static int DefaultFlush() { return 0; }

DebugSinkHooks& DebugSinkHookTable() {
    static DebugSinkHooks g_hooks{DefaultFlush};
    return g_hooks;
}

// gilde.exe 0x5e9eb0 — VIBE_Text_PrintfWrapper
// The binary body ignores its varargs and just flushes the debug stream.
int TextPrintfWrapper(const char* fmt, ...) {
    (void)fmt;
    // The varargs are consumed/ignored exactly as the original (which never reads them).
    return DebugSinkHookTable().flush();
}

// -------------------------------------------------------------------------------------
// ReadLine
// -------------------------------------------------------------------------------------
// gilde.exe 0x5e9e30 — VIBE_Text_ReadLine
char* TextReadLine(char* buf, int size, StreamReadHooks& hooks) {
    // Save the stream's 0x30 mode bits and clear them (and 0xCF mask) for the read.
    int savedBits = hooks.streamMode() & 0x30;
    hooks.setStreamMode(hooks.streamMode() & 0xCF);

    char* dst = buf;          // edx, starts at buf (esi)
    int count = size;         // ecx
    int last = -1;            // var_14 (the last getc result); -1 => "nothing read yet"
    for (;;) {
        // dec ecx; if (ecx <= 0) break;  — pre-decrement, so at most size-1 chars.
        --count;
        if (count <= 0)
            break;
        last = hooks.getc();
        if (last == -1)
            break;
        *dst++ = static_cast<char>(last);
        if (static_cast<unsigned char>(last) == 0x0A)
            break;
    }

    char* result = buf;
    if (last == -1 && (dst == buf || (hooks.streamMode() & 0x20) != 0)) {
        result = nullptr;
    } else {
        *dst = '\0';
    }

    // Restore the saved 0x30 mode bits.
    hooks.setStreamMode(hooks.streamMode() | savedBits);
    return result;
}

// =====================================================================================
// DEFERRED — reported, not implemented (CLAUDE.md rule 8: omit + report rather than
// fake). These are NOT pure string/locale logic; they are UI/VFS-coupled dispatchers.
//
//   gilde.exe 0x44ae24 — VIBE_Text_GenerateLabelDefines
//       A development-time emitter: opens f3_textindex.h (write), then a_obj.h and
//       a_geb.h (read) via VIBE_File_OpenStream, reads each line with VIBE_Text_ReadLine,
//       splits the first comma-delimited field, looks it up via VIBE_Text_LookupLabelEntry
//       and writes "#define <name> <id>" lines with VIBE_Crt_Fputs. It is entirely VFS /
//       file-stream coupled (3 streams, fputs) and produces no in-memory value — its
//       per-line parse reuses TextReadLine + StringGetDelimitedField + TextLookupLabelEntry
//       (all reconstructed here / in crt). Wiring it requires the VFS file-stream object
//       (VIBE_File_OpenStream @0x5d4488, VIBE_Crt_Fputs @0x5e54f0, VIBE_Vfs_CloseAndFreeEntry
//       @0x5d90c0); deferred to the I/O owner.
//
//   gilde.exe 0x5a161c — VIBE_Text_RenderCreditsBlock  (io_Text credits variant)
//       A ~7.2 KB in-place buffer-rewrite dispatcher (the same family as the already-
//       reconstructed RenderRichString @0x59d6e8 / RenderFormattedMessage @0x59f99c in
//       src/gui/text/richtext.*). It is coupled to: the per-city window/object slot array
//       (dword_67EB80 stride 238, child-object icon batching at +161, slot +225 counter),
//       the text database (dword_8C36B0 / dword_8C379C/3790/37A8/37E0), random-text
//       lookup, game-time (VIBE_GameTime_PackToRecord), and window children removal
//       (VIBE_Window_RemoveChildren). It is NOT a pure string leaf. An inert default hook
//       already exists at src/gui/credits_run.h (CreditsRunHost::TextRenderCreditsBlock);
//       the string-production half is covered by richtext.*. Deferred to the GUI/textDB
//       owner per the existing richtext deferral.
// =====================================================================================

} // namespace guild::play

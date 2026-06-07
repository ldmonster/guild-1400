#include "world/history_second_pass.h"

#include <cstring>

// Faithful 1:1 port of the chronicle second-pass cores (gilde.exe 0x4fe0ec /
// 0x4fe0b0). The bracket collapser reproduces VIBE_History_ParseTextReal's two
// in-place MemMove rewrites byte-for-byte; the orchestrator reproduces
// VIBE_History_ParseLabelPasses' three-stage gate. The engine's label-table fetch
// (dword_8C36B0), the "%s" text render (VIBE_Text_RenderFormattedMessage) and the
// downstream first/second commandline parsers are leaves driven by the caller.

namespace guild::world {

// VIBE_Util_MemMove (gilde.exe 0x5d9310): a forward/backward-safe memmove. The
// original always copies strlen(src)+1 bytes (text + NUL) so we mirror that with
// std::memmove over the same span.
static void HistoryMemMove(char* dst, const char* src) {
    std::memmove(dst, src, std::strlen(src) + 1);
}

// gilde.exe 0x4fe0ec — VIBE_History_ParseTextReal (bracket-region collapse core).
HistoryCollapseResult HistoryCollapseLabel(const char* label, char* out) {
    // The original copies the label into a 6096-byte scratch then walks it in place.
    // We copy into `out` and rewrite there (out is the caller's scratch).
    if (!label) {
        if (out)
            out[0] = '\0';
        return HistoryCollapseResult::kRendered;
    }
    std::strcpy(out, label);

    // v2  == the open-region '[' pointer (null when no region is open)
    // v17 == the '#' marker pointer (null until a '#' is seen in the open region)
    char* regionStart = nullptr;  // v2
    char* marker       = nullptr; // v17

    // if ( v14[0] ) — only walk a non-empty label.
    char* p = out;                // v8
    if (*p) {
        for (;;) {
            char c = *p;          // v9
            if (c == '[') {       // *v8 == 91 -> start region
                if (regionStart)  // already open (v2 != 0) -> Syntax Error
                    return HistoryCollapseResult::kSyntaxErr; // LABEL_21
                regionStart = p;  // v2 = v8
                ++p;              // ++v8 (LABEL_11)
            } else if (c == '#') { // v9 == 35 -> escape marker
                if (!regionStart || marker) // '#' with no open region OR duplicated
                    return HistoryCollapseResult::kSyntaxErr; // LABEL_21
                marker = p;        // v17 = v8
                ++p;               // LABEL_11
            } else if (c == ']') { // v9 == 93 -> close region
                if (!regionStart || !marker) // ']' before '[' or before '#'
                    return HistoryCollapseResult::kSyntaxErr; // LABEL_21
                // The original computes the resume pointer (v8 = v17 - 1) AFTER both
                // left-shifting moves. v17 points at '#'; both moves shift the tail
                // left by one byte each, but v17/v2 lie BEFORE the spans that move,
                // so their addresses are unaffected. The resume byte is therefore
                // the char immediately before the old '#' slot.
                char* resume = marker - 1;
                // Collapse "[keep#drop]" -> "keep":
                //   move text after ']' onto '#'  (deletes "#drop]")
                HistoryMemMove(marker, p + 1);
                //   move text after '[' onto '['  (deletes "[")
                HistoryMemMove(regionStart, regionStart + 1);
                marker = nullptr;       // v17 = 0
                regionStart = nullptr;  // v2  = 0
                // v8 = v17 - 1, then fall straight into the LABEL_12 NUL test (the
                // ']' path does NOT take the ++v8 LABEL_11 step). The next loop
                // iteration re-reads *v8.
                p = resume;
                if (!*p)               // LABEL_12 reached directly from the ']' branch
                    break;
                continue;              // re-read *v8 at the loop top
            } else {
                ++p;               // ordinary char (LABEL_11)
            }

            if (!*p)               // LABEL_12: if (!*v8) goto LABEL_13
                break;
        }
    }

    // LABEL_13: render only when no region remained open and no residual marker.
    //   if ( !v2 && !v17 && !v16 ) render("%s", v14);
    // On a fully-collapsed (or region-free) label both pointers are null here, so
    // the engine renders. We already hold the collapsed text in `out`.
    if (!regionStart && !marker)
        return HistoryCollapseResult::kRendered;
    // A still-open region at end-of-string is the original's silent no-render case
    // (not a sprintf syntax error, just no output). Report it as a syntax error so
    // callers don't render a half-collapsed label.
    return HistoryCollapseResult::kSyntaxErr;
}

// gilde.exe 0x4fe0b0 — VIBE_History_ParseLabelPasses (3-stage gate).
int HistoryRunLabelPasses(bool firstPassOk, bool secondPassOk, bool commandlineFlag,
                          bool* ranCommandline) {
    if (ranCommandline)
        *ranCommandline = false;
    // result = ParseTextFirstPass(...); if (!result) return result;
    if (!firstPassOk)
        return 0;
    // result = ParseTextSecondPass(...); if (!result) return result;
    if (!secondPassOk)
        return 0;
    // if ( a3 ) ParseCommandlineSecondPass(...);   (fire-and-forget)
    if (commandlineFlag && ranCommandline)
        *ranCommandline = true;
    return 1;
}

} // namespace guild::world

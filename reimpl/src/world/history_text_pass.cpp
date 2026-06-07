#include "world/history_text_pass.h"
#include "sim/script_lexer.h"   // guild::sim::IsDigitClass (byte_64A208[c+1] & 0x20)

#include <cstring>

// Faithful 1:1 port of VIBE_History_ParseTextSecondPass (gilde.exe 0x4fdcec): the
// chronicle-label text walker that resolves embedded "_<ctx>--<digits>"
// substitution tokens via ParseContext. The original's four 1024-byte sprintf
// scratch buffers (v44..v47) only ever held the error message, so they are folded
// away here; the token scratch (v49/v50) and the ParseContext output buffer
// (v48/v51) are the `token` string and the resolver result. ParseContext itself
// (and the group-row fetch from ParseCommandlineFirstPass) are engine leaves
// surfaced through the resolver hook.

namespace guild::world {

namespace {
// byte_64A208[(unsigned __int8)(c + 1)] & 0x20 — the digit-class test the original
// uses both to gate the post-'--' run and to consume it. Reuse the verbatim table.
inline bool IsDigitClass(char c) {
    return guild::sim::IsDigitClass(static_cast<guild::u8>(c));
}
}  // namespace

// gilde.exe 0x4fdcec — VIBE_History_ParseTextSecondPass.
HistoryTextResult HistoryParseTextSecondPass(const std::string& labelText,
                                             std::string& out,
                                             const HistorySubstResolver& resolve) {
    out.clear();

    // v53 == "inside a substitution token"; v15 == count of '-' separators seen.
    bool inTok = false;     // v53
    int  dashes = 0;        // v15
    std::string token;      // the v49/v50 token scratch (starts with '_')

    const char* p = labelText.c_str();   // v16
    while (*p) {
        char c = *p;
        if (!inTok) {
            if (c == '_') {
                // a3 = v58; reset token buffer; v49 = *v16 ('_'); v53 = 1; v15 = 0.
                token.clear();
                token.push_back('_');
                dashes = 0;
                inTok = true;
                ++p;
            } else {
                // ordinary char: *v54++ = *v16++ (copy through to output).
                out.push_back(c);
                ++p;
            }
            continue;
        }

        // --- inside a substitution token (v53 != 0) ----------------------------
        if (c == ' ') {
            // "Syntax Error in Label %i < %s >" -> return 0.
            return HistoryTextResult::kSyntaxErr;
        }

        if (c == '-') {                  // v23 == 45
            // ++a3; ++v15; ++v16; *(a3-1) = 45; then the > 2 gate.
            token.push_back('-');
            ++dashes;
            ++p;
            if (dashes > 2)              // if ( v15 > 2 ) -> Syntax Error
                return HistoryTextResult::kSyntaxErr;
            continue;
        }

        if (dashes >= 2) {
            // The first char of the post-'--' run must be digit-class.
            if (!IsDigitClass(c))        // (byte_64A208[c+1] & 0x20) == 0 -> error
                return HistoryTextResult::kSyntaxErr;
            // while ( byte_64A208[*v16+1] & 0x20 ) { ++a3; ++v16; *(a3-1) = ch; }
            while (*p && IsDigitClass(*p)) {
                token.push_back(*p);
                ++p;
            }
            // *a3 = 0; ParseContext(&v49, v51, Pass).
            HistorySubstResult r = resolve ? resolve(token)
                                           : HistorySubstResult{};
            if (!r.ok)
                return HistoryTextResult::kSyntaxErr;   // ParseContext failed
            // append the resolved replacement (v51) onto the output (v54).
            out += r.text;
            // v33 = *v16++; *v54++ = v33 — copy the terminating char through.
            if (*p) {
                out.push_back(*p);
                ++p;
            }
            dashes = 0;
            inTok = false;
            continue;
        }

        // before the 2nd '-': ordinary token-head char (++a3; *(a3-1) = v23).
        token.push_back(c);
        ++p;
    }

    // --- end-of-text token handling ------------------------------------------
    if (inTok && dashes < 2)             // open token without two separators
        return HistoryTextResult::kSyntaxErr;

    if (inTok && dashes == 2) {
        // v53 == 1 && v15 == 2: resolve the trailing token via ParseContext.
        HistorySubstResult r = resolve ? resolve(token) : HistorySubstResult{};
        if (!r.ok)
            return HistoryTextResult::kSyntaxErr;
        out += r.text;
    }

    // *v54 = 0; return 1.
    return HistoryTextResult::kOk;
}

} // namespace guild::world

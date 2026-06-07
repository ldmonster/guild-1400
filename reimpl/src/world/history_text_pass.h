#pragma once
// History / Chronicle — the TEXT ("fake") second pass: the chronicle-label
// substitution-token resolver that turns "_<ctx>--<name>" placeholders embedded in
// a label's display text into resolved role/value strings.
//
// Recovered 1:1 from gilde.exe:
//   * VIBE_History_ParseTextSecondPass 0x4fdcec — the per-label text walker. It
//     copies the label text, runs ParseCommandlineFirstPass to obtain the group
//     row, then scans the text character by character: ordinary chars pass through
//     to the output; a '_' opens a substitution token; inside a token a literal
//     space is a syntax error, up to two '-' separators are accumulated, and after
//     the second '-' the trailing digit-class run is read and the whole token is
//     resolved through ParseContext (whose result is appended to the output).
//     Returns 1 on success, 0 on a syntax error.
//
// This completes the chronicle text pipeline alongside world/history_full.h
// (FirstPass validator + ParseContext token classifier) and
// world/history_commandline_pass.h (the commandline second pass + command table).
//
// ParseContext's full role-name resolution (funcs_4FD5D2 dispatch + the engine's
// He/text leaves) is engine-coupled; it is surfaced here through a host hook so
// the text-walker's tokenize/validate/substitute control flow runs 1:1 while the
// substitution body stays mockable.
#include "guild/common/types.h"

#include <string>
#include <functional>

namespace guild::world {

// ===========================================================================
// Text second pass  (gilde.exe 0x4fdcec — VIBE_History_ParseTextSecondPass).
// ===========================================================================

// The substitution resolver hook — mirrors VIBE_History_ParseContext (0x4fd44c).
// `token` is the full substitution token (the '_' marker through the trailing
// digit run, e.g. "_NEW0--BUERGERMEISTER"); the hook returns the resolved
// replacement string to splice into the output, or sets `ok=false` to mark the
// token unresolved (the original's `if (!ParseContext(...)) return 0`). A null
// hook resolves every token to the empty string (drops the placeholder).
struct HistorySubstResult {
    std::string text;     // the resolved replacement (v48/v51 buffer contents)
    bool ok = true;       // ParseContext success (false aborts the whole pass)
};
using HistorySubstResolver =
    std::function<HistorySubstResult(const std::string& token)>;

enum class HistoryTextResult : int {
    kOk         = 1,   // text fully resolved -> output produced
    kSyntaxErr  = 0,   // malformed substitution token / unresolved -> aborted
};

// Resolve every "_<ctx>--<digits>" substitution token in `labelText`, writing the
// resolved display text into `out`. Returns kOk on success (and `out` holds the
// resolved text), kSyntaxErr on the first malformed token (the original sprintf's
// a "Syntax Error" message and returns 0 with no usable output).
//
// Token grammar enforced (faithful to the original):
//   * '_' opens a token; while open:
//       - a literal ' ' is a syntax error;
//       - '-' is a separator, at most TWO are allowed (a third is a syntax error);
//       - before the 2nd '-' any non-space/non-'-' char extends the token head;
//       - the char immediately after the 2nd '-' MUST be digit-class (the
//         byte_64A208[c+1] & 0x20 test) or it is a syntax error; the digit-class
//         run is then consumed and the token is resolved via `resolve`.
//   * a token still open at end-of-text with fewer than two '-' is a syntax error;
//     a token open with exactly two '-' is resolved at end-of-text.
HistoryTextResult HistoryParseTextSecondPass(const std::string& labelText,
                                             std::string& out,
                                             const HistorySubstResolver& resolve = nullptr);

} // namespace guild::world

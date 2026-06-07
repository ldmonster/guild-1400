#pragma once
// History / Chronicle — the SECOND-PASS chronicle text resolvers: the "real"
// bracket-collapse renderer (the `.esc`-style [ .. # .. ] region collapser the
// engine runs to turn a stored label into display text) and the pass orchestrator
// that drives FirstPass -> SecondPass -> CommandlineSecondPass.
//
// world/history_full.h already recovers the FIRST-pass syntax validator
// (VIBE_History_ParseTextFirstPass 0x4fd220) and the token classifier
// (VIBE_History_ParseContext 0x4fd44c); this module completes the chronicle
// label pipeline with the second-pass cores:
//
//   * VIBE_History_ParseTextReal   0x4fe0ec — the bracket-region COLLAPSER. A
//     well-formed label region "[<keep>#<drop>]" is rewritten to "<keep>" by two
//     in-place moves (drop the "#<drop>]" tail, then drop the leading "["); a label
//     with no region passes through verbatim. Syntax errors (an unbalanced or
//     mis-ordered marker) abort with no output. Recovered here as a pure
//     string->string transform; the engine's table lookup + text render are leaves.
//
//   * VIBE_History_ParseLabelPasses 0x4fe0b0 — the per-label pass ORCHESTRATOR:
//     run FirstPass; if it succeeds run SecondPass; if that succeeds and the
//     "commandline" flag is set, run CommandlineSecondPass; return success only
//     when every required stage passed. Modeled as a 3-stage gate over caller
//     supplied stage results so the control flow is exactly the original's.
#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Bracket-region collapse  (gilde.exe 0x4fe0ec — VIBE_History_ParseTextReal).
// ===========================================================================
// The grammar of a chronicle label region (validated by FirstPass):
//   '[' opens a region (only one open region permitted, no nesting),
//   '#' (once, inside an open region) marks the keep/drop split,
//   ']' closes a region that has been opened AND marked.
// On a ']' the original collapses the region in place: it first removes the
// "#<drop>]" run (VIBE_Util_MemMove of the text after '#' over '#'... wait: it
// moves the text after ']' onto '#', deleting "#<drop>]"), then removes the
// leading '[' (moves the text after '[' onto '['), leaving just "<keep>". After
// the collapse the scan resumes just before where '[' was so a following region
// is handled too. Result codes the original distinguishes:
enum class HistoryCollapseResult : int {
    kRendered  = 0,  // label fully collapsed (or had no region) -> engine renders it
    kSyntaxErr = 1,  // unbalanced / mis-ordered '[' '#' ']' -> aborted, no output
};

// Collapses every well-formed "[keep#drop]" region of `label` in place, writing the
// resolved text into `out` (must hold at least strlen(label)+1 bytes; the result is
// never longer than the input). Returns kRendered when the whole label was
// well-formed (this is when the original calls the text renderer), kSyntaxErr on the
// first malformed bracket state (the original sprintf's "Syntax Error" and returns
// without rendering). A label with no '[' '#' ']' at all is copied verbatim and
// returns kRendered.
HistoryCollapseResult HistoryCollapseLabel(const char* label, char* out);

// ===========================================================================
// Per-label pass orchestrator  (gilde.exe 0x4fe0b0 — VIBE_History_ParseLabelPasses).
// ===========================================================================
// The original chains three stages on one label index:
//   r = ParseTextFirstPass(label, idx+1);
//   if (r) { r = ParseTextSecondPass(.., idx+2, ..);
//            if (r) { if (commandlineFlag) ParseCommandlineSecondPass(idx+2);
//                     return 1; } }
//   return r;
// i.e. FirstPass gates SecondPass; SecondPass gates the (optional, fire-and-forget)
// commandline pass; success requires First && Second. `firstPassOk` / `secondPassOk`
// are the boolean results of those stages; `commandlineFlag` is the original's `a3`.
// Returns 1 on full success, else the failing stage's 0 (faithful to `return r`).
// `ranCommandline` (optional) is set true iff the commandline pass would fire.
int HistoryRunLabelPasses(bool firstPassOk, bool secondPassOk, bool commandlineFlag,
                          bool* ranCommandline = nullptr);

} // namespace guild::world

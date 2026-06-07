#pragma once
// Tutorial — the step-chain PROGRESSION rules core. The original tutorial system
// (gilde.exe VIBE_Tutorial_* @0x596fa4+) builds a chain of chapter records, each
// holding an array of fixed-size step entries, and walks them one step at a time,
// driving highlight-arrow forms + narrated voice. The GUI (form create/position,
// VIBE_Tutorial_DrawHighlightArrow), voice playback and the .esc-bound chapter
// builders (InitChapter1..5Steps, ~hard-coded step tables) are DEFERRED (see the
// module report). Recovered here byte-for-byte are the deterministic data rules:
//
//   * the chapter/step record layout (from VIBE_Tutorial_AdvanceStepForms 0x5978d8
//     and VIBE_Tutorial_BuildChapterChain 0x597bd8),
//   * the step-advance state machine (current step vs total, phase-byte change
//     detection, form-position selection, end-of-chapter transition),
//   * the active/inactive gate (VIBE_Tutorial_IsInactive 0x597b94),
//   * the chain free walk (VIBE_Tutorial_FreeStepChain 0x597e30).
#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Step entry (16 bytes). The chapter's step array is addressed as 16*step + base
// (from AdvanceStepForms: *(... + 16*stepIndex + N)). Recovered fields:
//   +0   (dword) opaque id / handle
//   +1>>24 (byte) phase byte: when it changes between steps the highlight form is
//          rebuilt; equal phase reuses the form (the v3[+61]>>24 compare).
//   +4   (byte)  form-position code: 0=left, 1=right, 2=top, 3=bottom; >=4 ends.
//   +8   (dword) text id rendered into the highlight form.
//   +12  (dword) voice-sample pointer/id (0 == silent step).
// ===========================================================================
enum TutorialFormPos : u8 {
    kTutPosLeft   = 0,  // tutorial\left_form
    kTutPosRight  = 1,  // tutorial\right_form
    kTutPosTop    = 2,  // tutorial\top_form
    kTutPosBottom = 3,  // tutorial\bottom_form
};
constexpr int kTutorialStepStride = 16;
constexpr u8  kTutorialPosEnd     = 4;   // form-pos >= 4 => no more steps (-4)

struct TutorialStep {
    u32 id;        // +0
    u8  phase;     // +1>>24 (BYTE3 of the +0 dword in the original; kept explicit)
    u8  pad2[2];   // +2
    u8  formPos;   // +4   TutorialFormPos
    u8  pad5[3];   // +5
    u32 textId;    // +8
    u32 voiceId;   // +12  (0 == no voice)
};

// ===========================================================================
// Chapter record. AdvanceStepForms reads:
//   +80 (dword) total step count   (v0+80; 0 == no steps -> stop)
//   +84 (dword) on-step callback   (called when entering/leaving; opaque)
//   +88 (dword) step array base    (16-byte stride)
// BuildChapterChain links chapters via +112 (next pointer); FreeStepChain frees
// +88 / +100 owned allocations and recurses +112.
// ===========================================================================
constexpr int kTutChapterStepCount = 80;   // +80
constexpr int kTutChapterStepArray = 88;   // +88
constexpr int kTutChapterNext      = 112;  // +112

struct TutorialChapter {
    int                 stepCount;  // +80
    const TutorialStep* steps;      // +88 (decoded step array)
    TutorialChapter*    next;       // +112 (chain link)
};

// ===========================================================================
// Tutorial runtime state (subset of off_5953F0 the advance logic touches):
//   idx 5  (+20) current chapter record
//   idx 6  (+24) active-chapter request
//   idx 7  (+28) -1 sentinel reset by SetActiveChapter
//   idx 15 (+60) current step index
//   +61>>24 last-step phase byte (the phase-change compare base)
//   +64    state flags byte (set to 4 on form-destroy)
// ===========================================================================
struct TutorialState {
    bool             active;      // dword_649CD0 (IsInactive returns it == 0)
    TutorialChapter* chapter;     // +20 current chapter
    int              stepIndex;   // +60 current step
    u8               lastPhase;   // +61 (phase byte of the last shown step)
    bool             formOpen;    // +17 != -1 (a highlight form is up)
};

// gilde.exe 0x597b94 — VIBE_Tutorial_IsInactive: true when not active.
bool TutorialIsInactive(const TutorialState& st);

// gilde.exe 0x597b5c — VIBE_Tutorial_SetActiveChapter: when active, arm a new
// active chapter request (resets the +7 sentinel to -1). Returns -4 if inactive,
// else 0.
int TutorialSetActiveChapter(TutorialState& st, TutorialChapter* chapter);

// Result of one advance step (distinct values for testability; the original
// collapses these to the two return codes -4 and 0 — see TutorialAdvanceReturn).
enum class TutorialAdvance : int {
    kNoChapter   = 1,  // no current chapter (v0 == 0)            -> returns -4
    kStopNoSteps = 2,  // stepCount == 0                          -> returns 0
    kEndChapter  = 3,  // stepIndex >= total -> chapter done       -> returns 0
    kReuseForm   = 4,  // same phase byte -> reuse the form        -> returns 0
    kRebuildForm = 5,  // phase changed, formPos < 4 -> new form   -> returns 0
    kEndOfSteps  = 6,  // phase changed, formPos >= 4 -> stop       -> returns -4
};

// gilde.exe 0x5978d8 — VIBE_Tutorial_AdvanceStepForms (decision portion).
// Classifies what the advance does for the current state WITHOUT touching the GUI:
//   * no chapter            -> kNoChapter
//   * stepCount == 0        -> kStopNoSteps
//   * stepIndex >= total    -> kEndChapter (the v1 <= idx path destroys the form)
//   * phase byte unchanged  -> kReuseForm  (the >>24 equal path)
//   * phase changed:
//       formPos >= 4        -> kEndOfSteps
//       else                -> kRebuildForm (new form-position selected)
// The caller advances st.stepIndex / st.lastPhase via TutorialCommitStep.
TutorialAdvance TutorialClassifyAdvance(const TutorialState& st);

// The original's actual return code for a classified advance (-4 or 0).
int TutorialAdvanceReturn(TutorialAdvance a);

// Selects the highlight-form resource name for a step's form-position code.
// Returns nullptr for the end sentinel (>= 4).
const char* TutorialFormResource(u8 formPos);

// Records that a step was shown: store its phase as lastPhase and mark the form
// open. (Mirrors writing v3[+61] and the form-id assignment.)
void TutorialCommitStep(TutorialState& st, const TutorialStep& step);

// gilde.exe 0x597e30 — VIBE_Tutorial_FreeStepChain: count chapters reachable via
// the +112 chain from `head` (the recursion the freer walks). Returns the chain
// length (0 for null). Pure traversal (no frees; the engine owns the allocator).
int TutorialChainLength(const TutorialChapter* head);

} // namespace guild::world

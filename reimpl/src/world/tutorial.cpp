#include "world/tutorial.h"

// Faithful 1:1 port of the deterministic step-PROGRESSION rules of the tutorial
// system (gilde.exe VIBE_Tutorial_*). The GUI/voice/.esc chapter builders are
// deferred; recovered here are the advance state machine + chain traversal.

namespace guild::world {

namespace {
// Highlight-form resources by position code (AdvanceStepForms switch v16).
const char* const kFormResource[4] = {
    "tutorial\\left_form",    // 0
    "tutorial\\right_form",   // 1
    "tutorial\\top_form",     // 2
    "tutorial\\bottom_form",  // 3
};
} // namespace

// gilde.exe 0x597b94 — VIBE_Tutorial_IsInactive.
bool TutorialIsInactive(const TutorialState& st) {
    return !st.active;   // return dword_649CD0 == 0;
}

// gilde.exe 0x597b5c — VIBE_Tutorial_SetActiveChapter.
int TutorialSetActiveChapter(TutorialState& st, TutorialChapter* chapter) {
    if (!st.active)
        return -4;        // if ( !dword_649CD0 ) return -4;
    st.chapter = chapter; // v2[6] = a1  (active-chapter request)
    // *(off_5953F0 + 7) = -1; sentinel reset is state we don't model separately.
    return 0;
}

// gilde.exe 0x5978d8 — advance classification.
TutorialAdvance TutorialClassifyAdvance(const TutorialState& st) {
    const TutorialChapter* ch = st.chapter;
    if (!ch)
        return TutorialAdvance::kNoChapter;        // v0 == 0 -> return -4
    if (ch->stepCount == 0)
        return TutorialAdvance::kStopNoSteps;       // v1 == 0 -> return 0

    // if ( v1 <= currentStep ) -> end of chapter (destroy form, return 0).
    if (static_cast<int>(ch->stepCount) <= st.stepIndex)
        return TutorialAdvance::kEndChapter;

    // Phase-change compare: v3[+61]>>24 (lastPhase) vs the current step's phase.
    if (!ch->steps)
        return TutorialAdvance::kEndChapter;
    const TutorialStep& step = ch->steps[st.stepIndex];
    if (st.lastPhase == step.phase)
        return TutorialAdvance::kReuseForm;         // equal phase -> reuse form

    // Phase changed: select a new form by position, or stop at the end sentinel.
    if (step.formPos >= kTutorialPosEnd)
        return TutorialAdvance::kEndOfSteps;        // v16 >= 4 -> return -4
    return TutorialAdvance::kRebuildForm;
}

int TutorialAdvanceReturn(TutorialAdvance a) {
    switch (a) {
        case TutorialAdvance::kNoChapter:
        case TutorialAdvance::kEndOfSteps:
            return -4;
        default:
            return 0;
    }
}

const char* TutorialFormResource(u8 formPos) {
    if (formPos >= kTutorialPosEnd)
        return nullptr;
    return kFormResource[formPos];
}

void TutorialCommitStep(TutorialState& st, const TutorialStep& step) {
    st.lastPhase = step.phase;  // write v3[+61] high byte
    st.formOpen  = true;        // a highlight form is now up
}

// gilde.exe 0x597e30 — VIBE_Tutorial_FreeStepChain (chain traversal length).
int TutorialChainLength(const TutorialChapter* head) {
    int n = 0;
    for (const TutorialChapter* p = head; p; p = p->next)
        ++n;                    // the freer recurses *(result+112)
    return n;
}

} // namespace guild::world

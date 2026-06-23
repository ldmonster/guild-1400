// Faithful 1:1 ports of the final two VIBE_Tutorial_* runtime-panel leaves.
// See tutorial_recon3_stepvoice.h for the source-function map, field offsets and
// per-core argument/return semantics. Engine Form/Text/Voice/Audio leaves are the
// same glue the world/tutorial_mission cores route through; the deterministic
// guard/return chain, text-branch select and voice-(re)start decision are pure.
#include "play/tutorial_recon3_stepvoice.h"

namespace guild::play {

// ===========================================================================
// 0x597300 — VIBE_Tutorial_ShowStepWithVoice.
// ===========================================================================

// Guard chain:  no step -> -4 ;  SetDialogTexts() == -4 -> -4 (propagated) ;  else 0.
int TutorialShowStepGuard(int stepValid, int dialogTexts) {
    if (!stepValid)              // if (!*((_DWORD*)off+5)) return -4;
        return -4;
    if (dialogTexts == -4)       // result = SetDialogTexts(); if (result != -4) {...}
        return -4;               // else return result (== -4)
    return 0;
}

// Text-branch select.
StepTextBranch TutorialShowStepTextBranch(int stepKind, u8 phaseByte) {
    // v2 = *(step+8); v3 = off[1];
    // if ((v2==1||v2==2) && (v3==10||v3==11)) -> fixed pair; else -> step pair.
    const bool kindOk  = (stepKind == 1 || stepKind == 2);
    const bool phaseOk = (phaseByte == 10 || phaseByte == 11);
    if (kindOk && phaseOk)
        return StepTextBranch::kFixedPair;
    return StepTextBranch::kStepPair;
}

// Voice (re)start decision (taken only on the kStepPair branch when *(step+16)).
StepVoiceAction TutorialShowStepVoiceAction(int hasVoice, int voiceHandle) {
    if (!hasVoice)               // if (!*(step+16)) -> leave voice alone
        return StepVoiceAction::kNoVoice;
    if (voiceHandle)             // if (*(off+16)) { stop if playing; off[4]=0; }
        return StepVoiceAction::kStopThenPlay;
    return StepVoiceAction::kPlayOnly;
}

// ===========================================================================
// 0x5975c8 — VIBE_Tutorial_HideReminderPanel (pure glue, hooks-routed).
// ===========================================================================
namespace {
TutorialStepVoiceHooks  g_inertStepVoiceHooks{};
TutorialStepVoiceHooks* g_stepVoiceHooks = &g_inertStepVoiceHooks;
} // namespace

void SetTutorialStepVoiceHooks(TutorialStepVoiceHooks* hooks) {
    g_stepVoiceHooks = hooks ? hooks : &g_inertStepVoiceHooks;
}
const TutorialStepVoiceHooks& GetTutorialStepVoiceHooks() { return *g_stepVoiceHooks; }

int TutorialHideReminderPanel(int reminderForm, int panelObjects) {
    return g_stepVoiceHooks->HideReminderPanel(reminderForm, panelObjects);
}

} // namespace guild::play

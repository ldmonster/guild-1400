// Golden-vector unit tests for the final two VIBE_Tutorial_* runtime-panel leaves:
//   VIBE_Tutorial_ShowStepWithVoice  0x597300 (guard / text-branch / voice cores)
//   VIBE_Tutorial_HideReminderPanel  0x5975c8 (hooks-routed glue)
// Vectors are derived directly from the Hex-Rays decompile branches.
#include "test.h"
#include "play/tutorial_recon3_stepvoice.h"

using namespace guild::play;

// ---------------------------------------------------------------------------
// ShowStepWithVoice — guard chain.
// ---------------------------------------------------------------------------
TEST(TutorialRecon3, ShowStepGuard_NoStep) {
    // !step -> -4, regardless of dialog result.
    CHECK_EQ(TutorialShowStepGuard(0, 0), -4);
    CHECK_EQ(TutorialShowStepGuard(0, -4), -4);
}

TEST(TutorialRecon3, ShowStepGuard_DialogFailPropagates) {
    // step present but SetDialogTexts() == -4 -> propagate -4.
    CHECK_EQ(TutorialShowStepGuard(1, -4), -4);
}

TEST(TutorialRecon3, ShowStepGuard_Ok) {
    // step present, dialog ok (0) -> 0. Any non -4 dialog result is "ok" here.
    CHECK_EQ(TutorialShowStepGuard(1, 0), 0);
    CHECK_EQ(TutorialShowStepGuard(7, 0), 0);
}

// ---------------------------------------------------------------------------
// ShowStepWithVoice — text branch.
// ---------------------------------------------------------------------------
TEST(TutorialRecon3, TextBranch_FixedPair) {
    // kind in {1,2} AND phase in {10,11} -> fixed pair.
    CHECK(TutorialShowStepTextBranch(1, 10) == StepTextBranch::kFixedPair);
    CHECK(TutorialShowStepTextBranch(1, 11) == StepTextBranch::kFixedPair);
    CHECK(TutorialShowStepTextBranch(2, 10) == StepTextBranch::kFixedPair);
    CHECK(TutorialShowStepTextBranch(2, 11) == StepTextBranch::kFixedPair);
}

TEST(TutorialRecon3, TextBranch_StepPair_WrongKind) {
    // kind not in {1,2} -> step pair even with matching phase.
    CHECK(TutorialShowStepTextBranch(0, 10) == StepTextBranch::kStepPair);
    CHECK(TutorialShowStepTextBranch(3, 11) == StepTextBranch::kStepPair);
}

TEST(TutorialRecon3, TextBranch_StepPair_WrongPhase) {
    // matching kind but phase not in {10,11} -> step pair.
    CHECK(TutorialShowStepTextBranch(1, 0)  == StepTextBranch::kStepPair);
    CHECK(TutorialShowStepTextBranch(1, 9)  == StepTextBranch::kStepPair);
    CHECK(TutorialShowStepTextBranch(2, 12) == StepTextBranch::kStepPair);
}

TEST(TutorialRecon3, FixedTextIds) {
    // The two rich-string ids the fixed branch renders into windows 1 and 2.
    CHECK_EQ(static_cast<int>(kStepFixedTextWin1), 0x1D6B);
    CHECK_EQ(static_cast<int>(kStepFixedTextWin2), 0x1D6C);
}

// ---------------------------------------------------------------------------
// ShowStepWithVoice — voice (re)start decision.
// ---------------------------------------------------------------------------
TEST(TutorialRecon3, VoiceAction_NoVoice) {
    // *(step+16) == 0 -> leave voice untouched (regardless of mounted handle).
    CHECK(TutorialShowStepVoiceAction(0, 0)   == StepVoiceAction::kNoVoice);
    CHECK(TutorialShowStepVoiceAction(0, 123) == StepVoiceAction::kNoVoice);
}

TEST(TutorialRecon3, VoiceAction_PlayOnly) {
    // has voice, no mounted handle -> just play.
    CHECK(TutorialShowStepVoiceAction(1, 0) == StepVoiceAction::kPlayOnly);
}

TEST(TutorialRecon3, VoiceAction_StopThenPlay) {
    // has voice and a mounted handle -> stop (if playing) + clear, then play.
    CHECK(TutorialShowStepVoiceAction(1, 55) == StepVoiceAction::kStopThenPlay);
}

TEST(TutorialRecon3, VoiceFixedArgs) {
    // Fixed args passed to VIBE_Voice_PlayPositionalSample(0xFFFFFFF9,0,-1,name).
    CHECK_EQ(kStepVoiceChannel, -7);   // 0xFFFFFFF9 as signed int
    CHECK_EQ(kStepVoiceFlag, 0);
    CHECK_EQ(kStepVoiceLoop, -1);
}

// ---------------------------------------------------------------------------
// HideReminderPanel — hooks routing.
// ---------------------------------------------------------------------------
TEST(TutorialRecon3, HideReminder_InertDefault) {
    SetTutorialStepVoiceHooks(nullptr);           // restore inert default
    // Inert default returns 0 (like the original SetObjectsVisible success tail).
    CHECK_EQ(TutorialHideReminderPanel(0x100, 0x200), 0);
}

namespace {
struct CapturingHooks : TutorialStepVoiceHooks {
    int form = 0, objs = 0, calls = 0;
    int HideReminderPanel(int reminderForm, int panelObjects) override {
        form = reminderForm; objs = panelObjects; ++calls; return 42;
    }
};
} // namespace

TEST(TutorialRecon3, HideReminder_RoutesToHooks) {
    CapturingHooks h;
    SetTutorialStepVoiceHooks(&h);
    int r = TutorialHideReminderPanel(0x1111, 0x2222);
    CHECK_EQ(r, 42);
    CHECK_EQ(h.calls, 1);
    CHECK_EQ(h.form, 0x1111);
    CHECK_EQ(h.objs, 0x2222);
    SetTutorialStepVoiceHooks(nullptr);           // leave inert default installed
}

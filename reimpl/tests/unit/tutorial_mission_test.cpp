#include "test.h"
#include "world/tutorial_mission.h"

#include <cstring>

using namespace guild;
using namespace guild::world;

// ===========================================================================
// Tutorial state reset.
// ===========================================================================
TEST(TutorialMissionReset, ResetChapterPointer) {
    TutorialRuntime rt;
    rt.panelHost = 999;
    rt.chapterId = 7;
    TutorialResetChapterPointer(rt);
    CHECK_EQ(rt.panelHost, 0);
    CHECK_EQ(rt.chapterId, -1);
}

TEST(TutorialMissionReset, ShutdownClearsFields) {
    TutorialRuntime rt;
    rt.phaseByte = 10; rt.voiceHandle = 55; rt.stepValid = 1;
    rt.panelHost = 3; rt.chapterId = 2; rt.sliderForm = 88;
    TutorialShutdownReset(rt);
    CHECK_EQ((int)rt.phaseByte, 0);
    CHECK_EQ(rt.voiceHandle, 0);
    CHECK_EQ(rt.stepValid, 0);
    CHECK_EQ(rt.panelHost, 0);
    CHECK_EQ(rt.chapterId, -1);
    CHECK_EQ(rt.sliderForm, -1);
}

// ===========================================================================
// Panel guards (-4 / -1 / 0).
// ===========================================================================
TEST(TutorialMissionGuard, OpenMainEventPanel) {
    // no host
    CHECK_EQ(TutorialOpenMainEventPanelResult(0, false, true), -4);
    // host already open
    CHECK_EQ(TutorialOpenMainEventPanelResult(5, true, true), -4);
    // create failed
    CHECK_EQ(TutorialOpenMainEventPanelResult(5, false, false), -1);
    // success
    CHECK_EQ(TutorialOpenMainEventPanelResult(5, false, true), 0);
}

TEST(TutorialMissionGuard, CloseEventPanel) {
    CHECK_EQ(TutorialCloseEventPanelResult(0, true), -4);   // no host
    CHECK_EQ(TutorialCloseEventPanelResult(5, false), -4);  // nothing mounted
    CHECK_EQ(TutorialCloseEventPanelResult(5, true), 0);    // close it
}

TEST(TutorialMissionGuard, SetDialogTexts) {
    CHECK_EQ(TutorialSetDialogTextsResult(0, true), -4);    // no host
    CHECK_EQ(TutorialSetDialogTextsResult(5, false), -4);   // no mounted panel
    CHECK_EQ(TutorialSetDialogTextsResult(5, true), 0);
}

// ===========================================================================
// Reminder / Done panel show-gate.
// ===========================================================================
TEST(TutorialMissionPanel, ClassifyShow) {
    CHECK(TutorialClassifyShowPanel(0, 100, 200) == TutorialPanelAction::kSkip);    // no step
    CHECK(TutorialClassifyShowPanel(1, 0, 200)   == TutorialPanelAction::kSkip);    // no text
    CHECK(TutorialClassifyShowPanel(1, 100, 0)   == TutorialPanelAction::kShowSilent);
    CHECK(TutorialClassifyShowPanel(1, 100, 200) == TutorialPanelAction::kShowVoiced);
}

// ===========================================================================
// CheckStateAndStopVoice state machine.
// ===========================================================================
TEST(TutorialMissionVoice, CheckState) {
    // no panel
    CHECK(TutorialCheckStateAndStopVoice(0, false, true, -1, 0, 0, 0)
          == TutorialVoiceCheck::kNoPanel);
    // wrong screen
    CHECK(TutorialCheckStateAndStopVoice(5, true, false, -1, 0, 0, 0)
          == TutorialVoiceCheck::kIgnore);
    // dialogResult != -1, click matches chapterId -> stopped
    CHECK(TutorialCheckStateAndStopVoice(5, true, true, 1210, 42, 42, 0)
          == TutorialVoiceCheck::kStopped);
    // dialogResult != -1, click mismatches -> ignore
    CHECK(TutorialCheckStateAndStopVoice(5, true, true, 1210, 7, 42, 0)
          == TutorialVoiceCheck::kIgnore);
    // dialogResult == -1, menuState == 28 -> stopped
    CHECK(TutorialCheckStateAndStopVoice(5, true, true, -1, 0, 42, 28)
          == TutorialVoiceCheck::kStopped);
    // dialogResult == -1, menuState != 28 -> ignore
    CHECK(TutorialCheckStateAndStopVoice(5, true, true, -1, 0, 42, 27)
          == TutorialVoiceCheck::kIgnore);
}

// ===========================================================================
// Progress slider.
// ===========================================================================
TEST(TutorialMissionSlider, Geometry) {
    CHECK_EQ(TutorialSliderLeftX(1024), 312);   // 512 - 200
    CHECK_EQ(TutorialSliderLeftX(800), 200);    // 400 - 200
}

TEST(TutorialMissionSlider, ValueGolden) {
    CHECK_EQ(TutorialProgressSliderValue(100, 100, 1000), 400);   // now==start
    CHECK_EQ(TutorialProgressSliderValue(600, 100, 1000), 200);   // half elapsed
    CHECK_EQ(TutorialProgressSliderValue(350, 100, 1000), 300);   // quarter
    CHECK_EQ(TutorialProgressSliderValue(1100, 100, 1000), 0);    // exhausted
    CHECK_EQ(TutorialProgressSliderValue(1101, 100, 1000), 0);    // past end (clamp)
    CHECK_EQ(TutorialProgressSliderValue(100, 100, 0), 0);        // zero duration guard
}

TEST(TutorialMissionSlider, ActiveGate) {
    CHECK(TutorialSliderActive(1, 10) == true);
    CHECK(TutorialSliderActive(0, 10) == false);   // no step
    CHECK(TutorialSliderActive(1, 9)  == false);   // phase != 10
}

// ===========================================================================
// Highlight-arrow 4-phase machine.
// ===========================================================================
TEST(TutorialMissionArrow, TravelFraction) {
    CHECK(TutorialArrowTravelFraction(150, 100, 200.0f) == 0.25f);
    CHECK(TutorialArrowTravelFraction(100, 100, 200.0f) == 0.0f);
    CHECK(TutorialArrowTravelFraction(100, 100, 0.0f)   == 0.0f);   // guard
}

TEST(TutorialMissionArrow, PhaseMachine) {
    TutorialRuntime rt;
    rt.arrowPhase = 2; rt.arrowTick = 100;
    // approach 50, travel 200, hold 30
    // now=120 < 100+50 -> stay in phase 2
    CHECK(TutorialArrowStep(rt, 120, 50.0f, 200.0f, 30.0f) == TutorialArrowResult::kStay);
    CHECK_EQ(rt.arrowPhase, 2);
    // now=150 >= 150 -> advance to 3, then 150 < 150+200 -> travel
    CHECK(TutorialArrowStep(rt, 150, 50.0f, 200.0f, 30.0f) == TutorialArrowResult::kTravel);
    CHECK_EQ(rt.arrowPhase, 3);
    CHECK_EQ(rt.arrowTick, 150);
    // now=349 < 150+200 -> still travel
    CHECK(TutorialArrowStep(rt, 349, 50.0f, 200.0f, 30.0f) == TutorialArrowResult::kTravel);
    // now=350 >= 350 -> advance to phase 4, 350 < 350+30 -> stay (hold)
    CHECK(TutorialArrowStep(rt, 350, 50.0f, 200.0f, 30.0f) == TutorialArrowResult::kStay);
    CHECK_EQ(rt.arrowPhase, 4);
    CHECK_EQ(rt.arrowTick, 350);
    // now=380 >= 350+30 -> wrap to phase 2, kAdvance
    CHECK(TutorialArrowStep(rt, 380, 50.0f, 200.0f, 30.0f) == TutorialArrowResult::kAdvance);
    CHECK_EQ(rt.arrowPhase, 2);
    CHECK_EQ(rt.arrowTick, 380);
}

// ===========================================================================
// OpenActiveCharBuilding guard.
// ===========================================================================
TEST(TutorialMissionBuilding, ProceedGuard) {
    CHECK(TutorialOpenBuildingProceeds(1, false, 0) == false);  // nonzero arg short-circuits
    CHECK(TutorialOpenBuildingProceeds(0, false, 2) == true);   // no active char -> proceed
    CHECK(TutorialOpenBuildingProceeds(0, true, 5)  == true);   // class != 2 -> proceed
    CHECK(TutorialOpenBuildingProceeds(0, true, 2)  == false);  // gated class -> stop
}

// ===========================================================================
// Mission reward summary.
// ===========================================================================
TEST(TutorialMissionReward, BodyTextId) {
    CHECK_EQ(MissionRewardBodyTextId(0x1000), 0x1002);
}

TEST(TutorialMissionReward, VoiceSample) {
    char buf[64];
    CHECK(std::strcmp(MissionRewardVoiceSample(buf, sizeof buf, 5),
                      "_AUFTRAEGE_ERFOLG_HS_05") == 0);
    CHECK(std::strcmp(MissionRewardVoiceSample(buf, sizeof buf, 12),
                      "_AUFTRAEGE_ERFOLG_HS_12") == 0);
    // null/zero-cap guard returns out unchanged (no crash).
    CHECK(MissionRewardVoiceSample(nullptr, 0, 1) == nullptr);
}

TEST(TutorialMissionReward, Timeout) {
    CHECK_EQ(MissionRewardTimeoutDeadline(1000), 1250u);
}

TEST(TutorialMissionReward, SkipRequested) {
    CHECK(MissionRewardSkipRequested(1210, 42, 42) == true);
    CHECK(MissionRewardSkipRequested(-1, 42, 42)   == false);  // no dialog event
    CHECK(MissionRewardSkipRequested(1210, 7, 42)  == false);  // wrong object
}

// ===========================================================================
// History-list click hit-tests.
// ===========================================================================
TEST(TutorialMissionHistory, ChooseHistoryHit) {
    int opts[6] = {10, 20, 30, 40, 50, 60};
    CHECK_EQ(MissionChooseHistoryHit(opts, 99, 99), -1);  // cancel
    CHECK_EQ(MissionChooseHistoryHit(opts, 99, 10), 0);
    CHECK_EQ(MissionChooseHistoryHit(opts, 99, 60), 5);
    CHECK_EQ(MissionChooseHistoryHit(opts, 99, 30), 2);
    CHECK_EQ(MissionChooseHistoryHit(opts, 99, 12345), kMissionHistoryNoChange);
}

TEST(TutorialMissionHistory, RewardHit) {
    int opts[5] = {11, 22, 33, 44, 55};
    CHECK_EQ(MissionHistoryRewardHit(opts, 77, 88, 77), -1);  // cancel
    CHECK_EQ(MissionHistoryRewardHit(opts, 77, 88, 88), -1);  // back
    CHECK_EQ(MissionHistoryRewardHit(opts, 77, 88, 11), 0);
    CHECK_EQ(MissionHistoryRewardHit(opts, 77, 88, 55), 4);
    CHECK_EQ(MissionHistoryRewardHit(opts, 77, 88, 999), kMissionHistoryNoChange);
}

TEST(TutorialMissionHistory, RewardDisableCount) {
    CHECK_EQ(MissionHistoryRewardDisableCount(-1), 0);
    CHECK_EQ(MissionHistoryRewardDisableCount(0), 0);
    CHECK_EQ(MissionHistoryRewardDisableCount(3), 3);
    CHECK_EQ(MissionHistoryRewardDisableCount(5), 5);
    CHECK_EQ(MissionHistoryRewardDisableCount(9), 5);   // clamp
}

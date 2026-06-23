// Golden-vector unit tests for src/sim/event_recon2.h
// (Event / NpcEvent state-machine trigger / step / outcome cores).
//
// Each vector is derived directly from the Hex-Rays decompile: the same
// constants, the same comparison directions, the same RNG-draw plumbing.
#include "sim/event_recon2.h"
#include "tests/framework/test.h"

#include <cmath>
#include <string_view>

using namespace guild;
using namespace guild::sim;

namespace {
bool feq(float a, float b) { return std::fabs(a - b) < 1e-4f; }
}

// ---------------------------------------------------------------------------
// RunSimDiseases — disease outbreak roll (0x4d766c)
// ---------------------------------------------------------------------------
TEST(Event2ReconDisease, SeverityScale) {
    // 0x4d77be: v27 = rand * 100.0
    CHECK(feq(DiseaseSeverity(0.0f), 0.0f));
    CHECK(feq(DiseaseSeverity(0.5f), 50.0f));
    CHECK(feq(DiseaseSeverity(0.999f), 99.9f));
}

TEST(Event2ReconDisease, WorkstationPenalty) {
    // category-2: count * 0.0125 * 20 ; else count * 0.02 * 20.
    CHECK(feq(DiseaseWorkstationPenalty(0, true), 0.0f));
    CHECK(feq(DiseaseWorkstationPenalty(10, true), 10 * 0.0125f * 20.0f)); // 2.5
    CHECK(feq(DiseaseWorkstationPenalty(10, false), 10 * 0.02f * 20.0f));  // 4.0
    CHECK(feq(DiseaseWorkstationPenalty(4, false), 1.6f));
}

TEST(Event2ReconDisease, EmptyThreshold) {
    // 0x4d792a: 25.0 - rand*20*0.5  -> rand=0 => 25 ; rand=1 => 15.
    CHECK(feq(DiseaseEmptyThreshold(0.0f), 25.0f));
    CHECK(feq(DiseaseEmptyThreshold(1.0f), 15.0f));
    CHECK(feq(DiseaseEmptyThreshold(0.5f), 20.0f));
}

TEST(Event2ReconDisease, OutbreakGate) {
    // 0x4d7858: severity < threshold.
    CHECK(DiseaseOutbreakFires(10.0f, 25.0f));
    CHECK(!DiseaseOutbreakFires(25.0f, 25.0f));   // strict <
    CHECK(!DiseaseOutbreakFires(30.0f, 25.0f));
}

TEST(Event2ReconDisease, RingWalkAndSeed) {
    // 0x4d7744: (slot + stride) % 768.
    CHECK_EQ(DiseaseNextSlot(0, 1), 1);
    CHECK_EQ(DiseaseNextSlot(767, 1), 0);
    CHECK_EQ(DiseaseNextSlot(700, 0x2FF), (700 + 0x2FF) % 768);
    // 0x4d76cd: stride = table[rand%16]; start = rand%768.
    DiseaseRunSeed s = DiseaseSeedRun(0, 100);
    CHECK_EQ(s.stride, 0x1u);
    CHECK_EQ(s.start, 100u);
    CHECK_EQ(DiseaseSeedRun(8, 0).stride, 0x2EDu);
    CHECK_EQ(DiseaseSeedRun(15, 0).stride, 0x2FFu);
}

TEST(Event2ReconDisease, SweepComplete) {
    CHECK(!DiseaseSweepComplete(767));
    CHECK(DiseaseSweepComplete(768));
    CHECK(DiseaseSweepComplete(800));
    CHECK_EQ(kDiseaseStepBudget, 12);
}

// ---------------------------------------------------------------------------
// BardCreateScriptStep (0x4d66b4)
// ---------------------------------------------------------------------------
TEST(Event2ReconBard, ScheduleAndGates) {
    // 0x4d6735: now + 3 + rand(4).
    CHECK_EQ(BardSchedDayOffset(0), 3);
    CHECK_EQ(BardSchedDayOffset(3), 6);
    // flag gates.
    CHECK(BardCase0Enabled(0));
    CHECK(!BardCase0Enabled(0x4));
    CHECK(BardCase0Enabled(0x2));   // bit2 set, bit4 (==4) clear -> enabled
    CHECK(!BardCase1Enabled(0x4));
    // 0x4d683a: minute >= 20 finishes the script step.
    CHECK(!BardScriptFinished(19));
    CHECK(BardScriptFinished(20));
    CHECK(BardScriptFinished(21));
    // case 3 demolition: (flag & 2)!=0.
    CHECK(BardCase3Demolish(0x2));
    CHECK(BardCase3Demolish(0x6));
    CHECK(!BardCase3Demolish(0x4));
}

// ---------------------------------------------------------------------------
// BroadcastWinnerPointsStep (0x4d9a60)
// ---------------------------------------------------------------------------
TEST(Event2ReconWinner, AdvanceAndRankKind) {
    // 0x4d9e89: minute >= 0x15.
    CHECK(!WinnerPointsAdvance(20));
    CHECK(WinnerPointsAdvance(21));
    // 0x4d9c77: 6/7 -> 1 else 2.
    CHECK_EQ(WinnerPointsRankKind(6), (u8)1);
    CHECK_EQ(WinnerPointsRankKind(7), (u8)1);
    CHECK_EQ(WinnerPointsRankKind(5), (u8)2);
    CHECK_EQ(WinnerPointsRankKind(0), (u8)2);
}

// ---------------------------------------------------------------------------
// OfficeMatchmakingStep (0x4da978)
// ---------------------------------------------------------------------------
TEST(Event2ReconOffice, ClassifyAndText) {
    CHECK(OfficeMatchClassify(12) == OfficeMatchPhase::Finalize);
    CHECK(OfficeMatchClassify(6)  == OfficeMatchPhase::Free);
    CHECK(OfficeMatchClassify(13) == OfficeMatchPhase::Free);
    CHECK(OfficeMatchClassify(0)  == OfficeMatchPhase::Step);
    CHECK(OfficeMatchClassify(5)  == OfficeMatchPhase::Step);
    // 0x4da997: flag ? 3361 : 3359.
    CHECK_EQ(OfficeMatchResultTextId(true), 3361);
    CHECK_EQ(OfficeMatchResultTextId(false), 3359);
}

TEST(Event2ReconOffice, PromotionGate) {
    // 0x4dabab: flag172==1 && def in {9,6} && rand3!=0.
    CHECK(OfficePromotionFires(1, 9, 1));
    CHECK(OfficePromotionFires(1, 6, 2));
    CHECK(!OfficePromotionFires(1, 9, 0));   // rand3==0 blocks
    CHECK(!OfficePromotionFires(0, 9, 1));   // flag!=1 blocks
    CHECK(!OfficePromotionFires(1, 7, 1));   // def not 6/9
}

TEST(Event2ReconOffice, VotePenaltyAndNotify) {
    // 0x4dace3: -(3*def + rand(3*def)).
    CHECK_EQ(OfficeVotePenalty(2, 0), -6);
    CHECK_EQ(OfficeVotePenalty(2, 5), -11);
    CHECK_EQ(OfficeVotePenalty(9, 0), -27);
    // notify text id.
    CHECK_EQ(OfficeMatchNotifyTextId(0, false), 3360);
    CHECK_EQ(OfficeMatchNotifyTextId(0, true), 3360);
    CHECK_EQ(OfficeMatchNotifyTextId(1, true), 3363);
    CHECK_EQ(OfficeMatchNotifyTextId(1, false), 3362);
}

// ---------------------------------------------------------------------------
// InitBetriebRun (0x4ef900)
// ---------------------------------------------------------------------------
TEST(Event2ReconInitBetrieb, Classify) {
    CHECK(InitBetriebClassify(-3, false) == InitBetriebOutcome::Pass);
    CHECK(InitBetriebClassify(-2, false) == InitBetriebOutcome::Free);
    CHECK(InitBetriebClassify(-1, false) == InitBetriebOutcome::Free);
    CHECK(InitBetriebClassify(1, true)   == InitBetriebOutcome::Continue);
    CHECK(InitBetriebClassify(0, false)  == InitBetriebOutcome::Continue);
    CHECK(InitBetriebClassify(0, true)   == InitBetriebOutcome::ProcessReady);
    CHECK_EQ(kInitBetriebFoundingFee, 3000);
}

// ---------------------------------------------------------------------------
// AllocProduktion / AllocWorkActorAction favorability multiplier (0x4f2a80/0x4f2530)
// ---------------------------------------------------------------------------
TEST(Event2ReconAlloc, FavorMultiplier) {
    // (favor - 0.5) * 0.25.
    CHECK(feq(AllocProductionFavorMul(0.5), 0.0f));
    CHECK(feq(AllocProductionFavorMul(2.5), 0.5f));
    CHECK(feq(AllocWorkActorFavorMul(0.5), 0.0f));
    CHECK(feq(AllocWorkActorFavorMul(4.5), 1.0f));
}

// ---------------------------------------------------------------------------
// OpenBuildingDialogRun (0x4f1d48)
// ---------------------------------------------------------------------------
TEST(Event2ReconBuildingDialog, GatesAndRng) {
    // 0x4f1db0: hour < 22 keeps the dialog phase open.
    CHECK(BuildingDialogHourOpen(21));
    CHECK(!BuildingDialogHourOpen(22));
    CHECK(!BuildingDialogHourOpen(23));
    // 0x4f1e1a: take object only when rand(4)!=0.
    CHECK(!BuildingDialogTakeObject(0));
    CHECK(BuildingDialogTakeObject(1));
    CHECK(BuildingDialogTakeObject(3));
    // 0x4f1e77: minute += rand(2).
    CHECK_EQ(BuildingDialogMinuteBump(10, 0), (u16)10);
    CHECK_EQ(BuildingDialogMinuteBump(10, 1), (u16)11);
}

// ---------------------------------------------------------------------------
// OpenHelpEventsForKind (0x4f1a00)
// ---------------------------------------------------------------------------
TEST(Event2ReconHelpIni, GroupMapping) {
    CHECK(HelpEventIniForGroup(1) == HelpEventIni::Handwerk);
    CHECK(HelpEventIniForGroup(2) == HelpEventIni::Handwerk);
    CHECK(HelpEventIniForGroup(3) == HelpEventIni::Handwerk);
    CHECK(HelpEventIniForGroup(4) == HelpEventIni::Wirt);
    CHECK(HelpEventIniForGroup(5) == HelpEventIni::Alpa);
    CHECK(HelpEventIniForGroup(6) == HelpEventIni::Alpa);
    CHECK(HelpEventIniForGroup(7) == HelpEventIni::Kirche);
    CHECK(HelpEventIniForGroup(11) == HelpEventIni::Dieb);
    CHECK(HelpEventIniForGroup(8) == HelpEventIni::None);
    CHECK(HelpEventIniForGroup(0) == HelpEventIni::None);
    // names.
    const char* hw = HelpEventIniName(HelpEventIni::Handwerk);
    CHECK(hw && std::string_view(hw) == "help_events_hw");
    CHECK(std::string_view(HelpEventIniName(HelpEventIni::Dieb)) == "help_events_dieb");
    CHECK(HelpEventIniName(HelpEventIni::None) == nullptr);
}

// ---------------------------------------------------------------------------
// AllocSlotResetAction / AllocGebaeudeBauen dedup gates (0x4f41bc / 0x4f5110)
// ---------------------------------------------------------------------------
TEST(Event2ReconDedup, Gates) {
    // slot-reset dedup: 0x4f4307.
    CHECK(SlotResetIsDuplicate(true, true));
    CHECK(!SlotResetIsDuplicate(true, false));
    CHECK(!SlotResetIsDuplicate(false, true));
    // build dedup: foreign handler with state < 5 conflicts.
    CHECK(BuildDedupConflicts(true, 4));
    CHECK(!BuildDedupConflicts(true, 5));
    CHECK(!BuildDedupConflicts(true, 6));
    CHECK(!BuildDedupConflicts(false, 0));
}

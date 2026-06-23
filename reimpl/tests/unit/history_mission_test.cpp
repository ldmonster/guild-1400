// Unit tests for the VIBE_Mission_Run*Dialog drivers (history_mission.{h,cpp}).
// Golden vectors for the per-tick decode helpers were computed offline (python).
#include "test.h"

#include "world/history_mission.h"

using namespace guild;
using namespace guild::world;

namespace {
MissionDialogFrame MakeFrame(i32 last, i32 click, i32 skip, u8 menu) {
    MissionDialogFrame f;
    f.lastDialogResult = last;
    f.clickedObjectId  = click;
    f.skipGate         = skip;
    f.menuState        = menu;
    return f;
}
}  // namespace

// ---------------------------------------------------------------------------
// MissionSpecialStep — 0x5387c8 inner loop.
// ---------------------------------------------------------------------------
TEST(HistoryMissionSpecial, IdleWhenNothing) {
    int conf = 0;
    CHECK(!MissionSpecialStep(MakeFrame(kMissionDialogNone, -1, 0, 0), &conf));
    CHECK_EQ(conf, 0);
}
TEST(HistoryMissionSpecial, DeclineExitsNoConfirm) {
    int conf = 0;
    CHECK(MissionSpecialStep(MakeFrame(kMissionDialogDecline, -1, 0, 0), &conf));
    CHECK_EQ(conf, 0);
}
TEST(HistoryMissionSpecial, AcceptExitsConfirmed) {
    int conf = 0;
    CHECK(MissionSpecialStep(MakeFrame(kMissionDialogAccept, -1, 0, 0), &conf));
    CHECK_EQ(conf, 1);
}
TEST(HistoryMissionSpecial, SkipGateExits) {
    int conf = 0;
    CHECK(MissionSpecialStep(MakeFrame(kMissionDialogNone, -1, 1, 0), &conf));
    CHECK_EQ(conf, 0);
}

// ---------------------------------------------------------------------------
// MissionAckStep — 0x539e8c / 0x53a41c.
// ---------------------------------------------------------------------------
TEST(HistoryMissionAck, ExitConditions) {
    CHECK(!MissionAckStep(MakeFrame(kMissionDialogNone, -1, 0, 0)));
    CHECK(MissionAckStep(MakeFrame(kMissionDialogNone, -1, 1, 0)));   // skip gate
    CHECK(MissionAckStep(MakeFrame(kMissionDialogAccept, -1, 0, 0))); // 1210
    // decline (1155) is NOT an ack-exit (only skip gate / accept close it)
    CHECK(!MissionAckStep(MakeFrame(kMissionDialogDecline, -1, 0, 0)));
}

// ---------------------------------------------------------------------------
// MissionChooseStep — 0x538950.
// ---------------------------------------------------------------------------
TEST(HistoryMissionChoose, ExitOnMenuState) {
    CHECK(MissionChooseStep(MakeFrame(kMissionDialogNone, 0, 0, 1), 0) ==
          MissionChooseAction::kExit);
}
TEST(HistoryMissionChoose, ExitOnDecline) {
    CHECK(MissionChooseStep(MakeFrame(kMissionDialogDecline, 0, 0, 0), 0) ==
          MissionChooseAction::kExit);
}
TEST(HistoryMissionChoose, ActivateOnMatch) {
    CHECK(MissionChooseStep(MakeFrame(kMissionDialogAccept, 5, 0, 0), 5) ==
          MissionChooseAction::kActivate);
}
TEST(HistoryMissionChoose, IdleNoClick) {
    CHECK(MissionChooseStep(MakeFrame(kMissionDialogNone, 5, 0, 0), 5) ==
          MissionChooseAction::kIdle);
}
TEST(HistoryMissionChoose, IdleClickMismatch) {
    CHECK(MissionChooseStep(MakeFrame(kMissionDialogAccept, 7, 0, 0), 5) ==
          MissionChooseAction::kIdle);
}

// ---------------------------------------------------------------------------
// MissionCompletionStep — 0x53ac34 guard.
// ---------------------------------------------------------------------------
TEST(HistoryMissionCompletion, ExitWhenCleared) {
    CHECK(MissionCompletionStep(-1));
    CHECK(!MissionCompletionStep(7));
}

// ---------------------------------------------------------------------------
// MissionOfferStep — 0x53a854.
// ---------------------------------------------------------------------------
TEST(HistoryMissionOffer, IdleWithoutClick) {
    CHECK(MissionOfferStep(MakeFrame(kMissionDialogNone, 9, 0, 0), 1, 2, 3) ==
          MissionOfferAction::kIdle);
}
TEST(HistoryMissionOffer, GivePath) {
    CHECK(MissionOfferStep(MakeFrame(kMissionDialogAccept, 1, 0, 0), 1, 2, 3) ==
          MissionOfferAction::kGive);
}
TEST(HistoryMissionOffer, DeclinePath) {
    CHECK(MissionOfferStep(MakeFrame(kMissionDialogAccept, 2, 0, 0), 1, 2, 3) ==
          MissionOfferAction::kDecline);
}
TEST(HistoryMissionOffer, AbandonPath) {
    auto a = MissionOfferStep(MakeFrame(kMissionDialogAccept, 3, 0, 0), 1, 2, 3);
    CHECK(a == MissionOfferAction::kAbandon);
    CHECK(MissionOfferTriggersReload(a));
    CHECK(!MissionOfferTriggersReload(MissionOfferAction::kDecline));
    CHECK(!MissionOfferTriggersReload(MissionOfferAction::kGive));
}
TEST(HistoryMissionOffer, GiveButtonAbsent) {
    // historySeed >= 4 -> give button id is -1; a click matching it is idle.
    CHECK(MissionOfferStep(MakeFrame(kMissionDialogAccept, 1, 0, 0), -1, 2, 3) ==
          MissionOfferAction::kIdle);
}

// ---------------------------------------------------------------------------
// W16: RewardSummary / Completion / Offer driver bodies (0x539fd8/53ac34/53a854).
// ---------------------------------------------------------------------------
TEST(HistoryMissionCompletion, OutcomeSwitch) {
    // gilde.exe 0x53ad50 switch(v7) (reuses MissionDecodeCompletion from mission_rules).
    CHECK(MissionDecodeCompletion(1) == MissionCompletionOutcome::kFailure);
    CHECK(MissionDecodeCompletion(2) == MissionCompletionOutcome::kInfo);
    CHECK(MissionDecodeCompletion(3) == MissionCompletionOutcome::kLoadSession);
    CHECK(MissionDecodeCompletion(0) == MissionCompletionOutcome::kNone);
    CHECK(MissionDecodeCompletion(99) == MissionCompletionOutcome::kNone);
}

TEST(HistoryMissionOffer, GiveButtonPresentGate) {
    // v31 < 4 (unsigned). historySeed -1 (unsigned huge) -> absent.
    CHECK(MissionOfferGiveButtonPresent(0));
    CHECK(MissionOfferGiveButtonPresent(3));
    CHECK(!MissionOfferGiveButtonPresent(4));
    CHECK(!MissionOfferGiveButtonPresent(-1));
}

// A tiny scripted hook harness: RunFrameLoop returns `frames` nonzero ticks, then
// 0; on the configured tick it publishes a frame snapshot that fires the decode.
namespace {
struct OfferHarness {
    int framesLeft = 0;
    int tick = 0;                        // dword_62EB38 — advanced each frame
    MissionDialogFrame fireFrame;        // published every tick
    int forms = 0, destroys = 0, frameLoopCalls = 0;
    static OfferHarness* g;
    static int  CreateForm(const char*) { ++g->forms; return 77; }
    static void Center(int) {}
    static void Select(int, int) {}
    static int  Render(unsigned) { return 0; }
    static int  RenderArg(unsigned, unsigned) { return 0; }
    static int  GetChild(int, int) { return 5; }
    static int  AudioInit() { return 0; }          // audio off -> timed-wait path
    // Each frame advances the game-tick counter (as the engine's RunFrameLoop does),
    // so the audio-off RewardSummary deadline (tick0 + 250) is eventually reached.
    static int  RunLoop(int, int) { ++g->frameLoopCalls; g->tick += 300; return g->framesLeft-- > 0; }
    static void Destroy(int) { ++g->destroys; }
    static void ReadFrame(MissionDialogFrame* o) { *o = g->fireFrame; }
    static int  ReadTick() { return g->tick; }
    static void Flush(int) {}
    MissionDialogHooks Install() {
        g = this;
        MissionDialogHooks h{};
        h.createForm = &CreateForm; h.centerChildWindows = &Center;
        h.selectWindow = &Select;   h.renderText = &Render;
        h.renderTextArg = &RenderArg; h.getChildObjectId = &GetChild;
        h.audioIsInitialized = &AudioInit; h.runFrameLoop = &RunLoop;
        h.destroyForm = &Destroy;   h.readFrame = &ReadFrame;
        h.readGameTick = &ReadTick; h.voiceQueueFlushAll = &Flush;
        return h;
    }
};
OfferHarness* OfferHarness::g = nullptr;
}  // namespace

TEST(HistoryMissionOffer, BodyDeclineClosesAndReturnsAction) {
    OfferHarness hz;
    // Large budget: RewardSummary's audio-off loops exit on the tick deadline; the
    // offer's frame loop runs until the decode (decline click) breaks it.
    hz.framesLeft = 1000;
    hz.fireFrame = MakeFrame(kMissionDialogAccept, 2, 0, 0);  // click == declineId(2)
    MissionDialogHooks h = hz.Install();
    SetMissionDialogHooks(&h);
    // give present (seed 0), decline=2, abandon=3.
    MissionOfferAction a = MissionRunOfferDialog(nullptr, 0, /*seed*/0, 1, 2, 3);
    SetMissionDialogHooks(nullptr);
    CHECK(a == MissionOfferAction::kDecline);
    CHECK(!MissionOfferTriggersReload(a));
    // Offer creates+destroys its own form AND calls RewardSummary (another form):
    // both forms are destroyed, so destroys == 2.
    CHECK_EQ(hz.destroys, 2);
}

TEST(HistoryMissionCompletion, BodyRunsAndReturnsOutcome) {
    OfferHarness hz;
    hz.framesLeft = 2;
    hz.fireFrame = MakeFrame(kMissionDialogNone, -1, 0, 0);
    MissionDialogHooks h = hz.Install();
    SetMissionDialogHooks(&h);
    // activeMissionId == -1 -> MissionCompletionStep latches close immediately.
    auto out = MissionRunCompletionDialog(/*outcome*/2, /*active*/-1, 0);
    SetMissionDialogHooks(nullptr);
    CHECK(out == MissionCompletionOutcome::kInfo);
}

TEST(HistoryMissionReward, BodyTimedPathDestroysForm) {
    OfferHarness hz;
    hz.framesLeft = 0;            // RunFrameLoop returns 0 immediately each line
    hz.fireFrame = MakeFrame(kMissionDialogNone, -1, 0, 0);
    MissionDialogHooks h = hz.Install();
    SetMissionDialogHooks(&h);
    int r = MissionRunRewardSummary(nullptr, 0);
    SetMissionDialogHooks(nullptr);
    CHECK_EQ(r, 77);             // returns the form handle Form_Destroy got
    CHECK_EQ(hz.destroys, 1);
}

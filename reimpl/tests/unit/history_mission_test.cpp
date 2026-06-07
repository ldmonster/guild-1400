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

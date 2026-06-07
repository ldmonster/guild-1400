// Unit tests for the second batch of game dialog/window builders:
//   action_dialog     : sabotage/spy/beat-up/abduct/free-prisoner layout + cost + wiring
//   violation_dialog  : report-at-office fee range + confirm/cancel wiring
//   evidence_dialog   : browse card list + detail-sheet seal layout strides
//   talent_dialog     : point->level threshold ladder + status-line selection + train wiring
//   bard_dialog       : recite gate selection + verdict line + recite command
//   feast_dialog      : course/drink menus + guest table + confirm/remove wiring
//   pamphlet_board    : entry slot pairing + sign wiring
//   quickchat_window  : open/close/refresh state machine
//   loading_screen    : form selection + bar percentage math
//   stammbaum_window  : family-tree node placement math + click resolution
#include "gui/action_dialog.h"
#include "gui/violation_dialog.h"
#include "gui/evidence_dialog.h"
#include "gui/talent_dialog.h"
#include "gui/bard_dialog.h"
#include "gui/feast_dialog.h"
#include "gui/pamphlet_board.h"
#include "gui/quickchat_window.h"
#include "gui/loading_screen.h"
#include "gui/stammbaum_window.h"
#include "tests/framework/test.h"

#include <cstring>

using namespace guild::gui;

// ===========================================================================
// action_dialog
// ===========================================================================
namespace {
struct ActSink : ActionCommandSink {
    enum Kind { kNone, kStart, kAbduct, kFree, kSpy };
    Kind kind = kNone;
    int actor = 0, target = 0, cmdKind = 0, cost = 0, op = 0;
    const char* label = nullptr;
    void reset() { kind = kNone; actor = target = cmdKind = cost = op = 0; label = nullptr; }
    void StartAction(int a, int t, int k, int c, const char* l) override {
        kind = kStart; actor = a; target = t; cmdKind = k; cost = c; label = l;
    }
    void Abduct(int s, int t, int k, int o) override {
        kind = kAbduct; actor = s; target = t; cmdKind = k; op = o;
    }
    void FreePrisoner(int t, int k, int) override { kind = kFree; target = t; cmdKind = k; }
    void RearmSpy(int) override { kind = kSpy; }
};
} // namespace

TEST(GuiDlg2Action, SabotageCostLayoutAndConfirm) {
    ActSink sink; ActionDialog_SetCommandSink(&sink);
    ActionState s{};
    s.actorEntity = 7; s.targetEntity = 13;
    s.wealthSelf = 1000; s.wealthTarget = 2000;
    // cost = 1000*0.02 + 2000*0.05 = 20 + 100 = 120.
    ActionLayout l = ActionDialog_BuildSabotage(s);
    CHECK(l.preflight == ActionPreflight::kOk);
    CHECK(std::strcmp(l.form, kFormActPergament) == 0);
    CHECK_EQ(l.slot, 1);
    CHECK_EQ(l.bodyText, kTextSabotageBody);
    CHECK_EQ(l.cost, 120);
    CHECK_EQ(l.cmdKind, kActKindSabotage);
    CHECK(l.actionLabel && std::strcmp(l.actionLabel, kActionSabotage) == 0);
    CHECK_EQ(l.seals.seal0, 2);
    CHECK_EQ(l.seals.seal1, 23);
    // Confirm needs OK id + the confirm object.
    sink.reset();
    CHECK(!ActionDialog_Dispatch(l, s, kActClickOK, 999999)); // wrong object
    CHECK_EQ(sink.kind, ActSink::kNone);
    sink.reset();
    CHECK(ActionDialog_Dispatch(l, s, kActClickOK, l.confirmObj));
    CHECK_EQ(sink.kind, ActSink::kStart);
    CHECK_EQ(sink.cmdKind, kActKindSabotage);
    CHECK_EQ(sink.cost, 120);
    CHECK_EQ(sink.target, 13);
    // Cancel ends loop without command.
    sink.reset();
    CHECK(ActionDialog_Dispatch(l, s, kActClickCancel, 0));
    CHECK_EQ(sink.kind, ActSink::kNone);
}

TEST(GuiDlg2Action, SabotagePreflightGates) {
    ActionState s{};
    s.wealthSelf = 100; s.hasResources = false;
    CHECK(ActionDialog_BuildSabotage(s).preflight == ActionPreflight::kNoResources);
    s.hasResources = true; s.busy = true;
    CHECK(ActionDialog_BuildSabotage(s).preflight == ActionPreflight::kInProgress);
    s.busy = false; s.pairedReverse = false;
    CHECK(ActionDialog_BuildSabotage(s).preflight == ActionPreflight::kBusyTarget);
}

TEST(GuiDlg2Action, SpyCostAndLimits) {
    ActionState s{};
    s.wealthSelf = 4000; s.wealthTarget = 6000;
    // cost = 4000*0.005 + 6000*0.005 = 49 (float truncation, matches the original).
    ActionLayout l = ActionDialog_BuildSpy(s);
    CHECK(std::strcmp(l.form, kFormActSpionage) == 0);
    CHECK_EQ(l.slot, 2);
    CHECK_EQ(l.bodyText, kTextSpyBody);
    CHECK_EQ(l.cost, 49);
    // too many running spies.
    s.runningSpies = 5;
    CHECK(ActionDialog_BuildSpy(s).preflight == ActionPreflight::kTooMany);
    s.runningSpies = 0; s.busy = true;
    CHECK(ActionDialog_BuildSpy(s).preflight == ActionPreflight::kInProgress);
}

TEST(GuiDlg2Action, BeatUpSelfWealthOnly) {
    ActionState s{};
    s.wealthSelf = 10000; s.wealthTarget = 99999; // target ignored
    // cost = 10000 * 0.0085 = 85.
    ActionLayout l = ActionDialog_BuildBeatUp(s);
    CHECK_EQ(l.cost, 85);
    CHECK_EQ(l.bodyText, kTextBeatUpBody);
    CHECK_EQ(l.seals.seal0, 3);
    CHECK_EQ(l.seals.seal1, 20);
}

TEST(GuiDlg2Action, AbductConfirmAndFree) {
    ActSink sink; ActionDialog_SetCommandSink(&sink);
    ActionState s{}; s.actorEntity = 1; s.targetEntity = 2;
    // no targets -> busy gate.
    CHECK(ActionDialog_BuildConfirmAbduct(s, 0).preflight == ActionPreflight::kBusyTarget);
    ActionLayout l = ActionDialog_BuildConfirmAbduct(s, 3);
    CHECK(l.preflight == ActionPreflight::kOk);
    CHECK_EQ(l.cmdKind, kActKindAbduct);
    CHECK(l.cancelObj != -1);
    sink.reset();
    CHECK(ActionDialog_Dispatch(l, s, 0, l.confirmObj));
    CHECK_EQ(sink.kind, ActSink::kAbduct);
    CHECK_EQ(sink.op, -2);
    // free prisoner gates.
    ActionState f{}; f.targetEntity = 9; f.freeHeldByUs = false;
    CHECK(ActionDialog_BuildConfirmFreePrisoner(f).preflight == ActionPreflight::kNotHeld);
    f.freeHeldByUs = true; f.freeState = 4;
    CHECK(ActionDialog_BuildConfirmFreePrisoner(f).preflight == ActionPreflight::kTooLate);
    f.freeState = 1;
    ActionLayout lf = ActionDialog_BuildConfirmFreePrisoner(f);
    CHECK(lf.preflight == ActionPreflight::kOk);
    CHECK_EQ(lf.cmdKind, kActKindFree);
    sink.reset();
    CHECK(ActionDialog_Dispatch(lf, f, kActClickOK, lf.confirmObj));
    CHECK_EQ(sink.kind, ActSink::kFree);
    CHECK_EQ(sink.target, 9);
}

// ===========================================================================
// violation_dialog
// ===========================================================================
namespace {
struct VioSink : ViolationCommandSink {
    enum Kind { kNone, kReport };
    Kind kind = kNone;
    int target = 0, building = 0, fee = 0; int notified = 0;
    void reset() { kind = kNone; target = building = fee = 0; notified = 0; }
    void Report(int t, int b, int f, int) override { kind = kReport; target = t; building = b; fee = f; }
    void NotifyOffice(int, int) override { ++notified; }
};
} // namespace

TEST(GuiDlg2Violation, FeeRangeAndConfirm) {
    VioSink sink; ViolationDialog_SetCommandSink(&sink);
    ViolationState s{};
    s.targetEntity = 5; s.building = 8;
    s.wealthSelf = 100000; s.wealthTarget = 100000; // wealth = 200000
    s.currencyHeld = 50;   // affordability cap
    // fee0 = 200000 * 0.0007843137 = 156.86 -> 156, clamped to 50.
    ViolationLayout l = ViolationDialog_Build(s);
    CHECK(std::strcmp(l.form, kFormViolation) == 0);
    CHECK_EQ(l.slot, kVioSlot);
    CHECK_EQ(l.feeMax, 50);
    CHECK_EQ(l.feeDefault, 50); // clamped
    CHECK_EQ(l.seal0, kVioSeal0);
    CHECK_EQ(l.seal1, kVioSeal1);
    // Confirm on the confirm object with resources -> report.
    sink.reset();
    CHECK(ViolationDialog_Dispatch(l, s, l.confirmObj, 50));
    CHECK_EQ(sink.kind, VioSink::kReport);
    CHECK_EQ(sink.fee, 50);
    CHECK_EQ(sink.notified, 0); // rank not 6/7
    // Cancel object ends loop, no report.
    sink.reset();
    CHECK(ViolationDialog_Dispatch(l, s, l.cancelObj, 0));
    CHECK_EQ(sink.kind, VioSink::kNone);
}

TEST(GuiDlg2Violation, NoResourcesAndRankNotify) {
    VioSink sink; ViolationDialog_SetCommandSink(&sink);
    ViolationState s{}; s.targetEntity = 1; s.building = 2; s.currencyHeld = 1000;
    s.wealthSelf = 0; s.wealthTarget = 0; s.hasResources = false;
    ViolationLayout l = ViolationDialog_Build(s);
    sink.reset();
    CHECK(!ViolationDialog_Dispatch(l, s, l.confirmObj, 10)); // no money: loop continues
    CHECK_EQ(sink.kind, VioSink::kNone);
    s.hasResources = true; s.targetRank = 7;
    sink.reset();
    CHECK(ViolationDialog_Dispatch(l, s, l.confirmObj, 10));
    CHECK_EQ(sink.notified, 1);
}

// ===========================================================================
// evidence_dialog
// ===========================================================================
TEST(GuiDlg2Evidence, BrowseCardStride) {
    std::vector<EvidencePerson> ppl = {
        {100, 2, 50}, {0, 1, 1} /*unresolved -> skipped*/, {101, 1, 9},
    };
    EvBrowseLayout l = EvidenceDialog_BuildBrowse(ppl);
    CHECK(std::strcmp(l.form, kFormEvBrowse) == 0);
    CHECK(!l.empty);
    CHECK_EQ((int)l.cards.size(), 2); // the 0-entity person was skipped
    CHECK_EQ(l.cards[0].entity, 100);
    CHECK_EQ(l.cards[0].cardY, 10);
    CHECK_EQ(l.cards[0].labelY, 55);
    CHECK_EQ(l.cards[1].cardY, 10 + 105);
    CHECK_EQ(l.cards[1].labelY, 55 + 105);
    // Click resolution.
    CHECK_EQ(EvidenceDialog_DispatchBrowse(l, l.cards[1].objectId), 101);
    CHECK_EQ(EvidenceDialog_DispatchBrowse(l, 424242), -1);
    // Empty list.
    EvBrowseLayout e = EvidenceDialog_BuildBrowse({});
    CHECK(e.empty);
}

TEST(GuiDlg2Evidence, DetailSealLayout) {
    std::vector<EvidenceRow> rows = { {7, 3}, {9, 1} };
    EvDetailLayout l = EvidenceDialog_BuildDetails(/*target=*/55, rows);
    CHECK(std::strcmp(l.form, kFormEvDetails) == 0);
    CHECK_EQ(l.targetEntity, 55);
    CHECK_EQ((int)l.rows.size(), 2);
    // Row 0 at y 10, 3 seals at x 340/356/372.
    CHECK_EQ(l.rows[0].nameY, 10);
    CHECK_EQ((int)l.rows[0].seals.size(), 3);
    CHECK_EQ(l.rows[0].seals[0].x, 340);
    CHECK_EQ(l.rows[0].seals[1].x, 356);
    CHECK_EQ(l.rows[0].seals[2].x, 372);
    CHECK_EQ(l.rows[0].seals[0].y, 10);
    // Row 1 at y 50, 1 seal.
    CHECK_EQ(l.rows[1].nameY, 50);
    CHECK_EQ((int)l.rows[1].seals.size(), 1);
    CHECK_EQ(l.rows[1].seals[0].y, 50);
    // No rows -> no form.
    EvDetailLayout e = EvidenceDialog_BuildDetails(1, {});
    CHECK(e.form == nullptr);
}

// ===========================================================================
// talent_dialog
// ===========================================================================
namespace {
struct TalSink : TalentCommandSink {
    int trained = 0, building = 0, level = 0;
    void reset() { trained = building = level = 0; }
    void Train(int b, int l) override { ++trained; building = b; level = l; }
};
} // namespace

TEST(GuiDlg2Talent, PointToLevelLadder) {
    CHECK_EQ(TalentDialog_PointsToLevel(0), 1);
    CHECK_EQ(TalentDialog_PointsToLevel(0x29), 1);
    CHECK_EQ(TalentDialog_PointsToLevel(0x2A), 2);
    CHECK_EQ(TalentDialog_PointsToLevel(0x53), 2);
    CHECK_EQ(TalentDialog_PointsToLevel(0x54), 3);
    CHECK_EQ(TalentDialog_PointsToLevel(0x7E), 5);
    CHECK_EQ(TalentDialog_PointsToLevel(0xA8), 7);
    CHECK_EQ(TalentDialog_PointsToLevel(0xD2), 10);
    CHECK_EQ(TalentDialog_PointsToLevel(0xFF), 10);
}

TEST(GuiDlg2Talent, StatusSelectionAndTrain) {
    TalSink sink; TalentDialog_SetCommandSink(&sink);
    TalentState s{};
    s.talent = 3; s.points = 0x60 /* -> level 3 */; s.building = 42;
    s.availableLevel = 5; s.hasTrainingFlag = true;
    TalentLayout l = TalentDialog_Build(s);
    CHECK(std::strcmp(l.form, kFormTalent) == 0);
    CHECK_EQ(l.nameTextId, 4810 + 3);
    CHECK_EQ(l.descTextId, 4822 + 3);
    CHECK_EQ(l.requiredLevel, 3);
    CHECK_EQ(l.statusText, kTextTalCanTrain); // 4803
    CHECK(l.canTrain);
    sink.reset();
    CHECK(TalentDialog_Dispatch(l, s, kTalClickOK));
    CHECK_EQ(sink.trained, 1);
    CHECK_EQ(sink.building, 42);
    CHECK_EQ(sink.level, 3);
    // Cancel.
    sink.reset();
    CHECK(TalentDialog_Dispatch(l, s, kTalClickCancel));
    CHECK_EQ(sink.trained, 0);
    // Trainer present -> 4806, can't train.
    s.hasTrainer = true;
    TalentLayout lt = TalentDialog_Build(s);
    CHECK_EQ(lt.statusText, kTextTalTrainer);
    CHECK(!lt.canTrain);
    // Maxed -> 4805.
    s.hasTrainer = false; s.points = 0xFC;
    CHECK_EQ(TalentDialog_Build(s).statusText, kTextTalMaxed);
    // Level beyond affordable -> 4807.
    s.points = 0xD2 /* level 10 */; s.availableLevel = 3;
    CHECK_EQ(TalentDialog_Build(s).statusText, kTextTalCantJump);
}

// ===========================================================================
// bard_dialog
// ===========================================================================
namespace {
struct BardSink : BardCommandSink {
    int recited = 0, self = 0, lawByte = 0;
    void reset() { recited = self = lawByte = 0; }
    void Recite(int s, int l) override { ++recited; self = s; lawByte = l; }
};
} // namespace

TEST(GuiDlg2Bard, GateSelection) {
    BardState s{};
    s.sceneValid = false;
    CHECK(BardDialog_Build(s).gate == BardGate::kNoScene);
    CHECK_EQ(BardDialog_Build(s).gateText, kTextBardNoScene);
    s.sceneValid = true; s.activeScene = 2; s.lawFlagRecited = true;
    CHECK(BardDialog_Build(s).gate == BardGate::kRecent);
    s.lawFlagRecited = false; s.zoomScale = 0;
    CHECK(BardDialog_Build(s).gate == BardGate::kNoZoom);
}

TEST(GuiDlg2Bard, ReciteAndVerdict) {
    BardSink sink; BardDialog_SetCommandSink(&sink);
    BardState s{};
    s.sceneValid = true; s.activeScene = 2; s.zoomScale = 1;
    s.self = 11; s.poemLawByte = 77; s.poemVerseId = 5; s.verdict = 1;
    BardLayout l = BardDialog_Build(s);
    CHECK(l.gate == BardGate::kOk);
    CHECK_EQ(l.announceText, kTextBardAnnounce);
    CHECK_EQ(l.lawByte, 77);
    // verdict==1 -> 6712 (verse+446)
    CHECK_EQ(l.verdictText, kTextBardVerdictPos);
    CHECK_EQ(l.verdictArg, 5 + 446);
    bool recited = false;
    sink.reset();
    CHECK(BardDialog_Dispatch(l, s, kBardClickOK, &recited));
    CHECK(recited);
    CHECK_EQ(sink.recited, 1);
    CHECK_EQ(sink.self, 11);
    CHECK_EQ(sink.lawByte, 77);
    // Verdict 0 / other.
    s.verdict = 0;
    CHECK_EQ(BardDialog_Build(s).verdictText, kTextBardVerdictNeu);
    CHECK_EQ(BardDialog_Build(s).verdictArg, 5 + 4810);
    s.verdict = 2;
    CHECK_EQ(BardDialog_Build(s).verdictText, kTextBardVerdictNeg);
}

// ===========================================================================
// feast_dialog
// ===========================================================================
namespace {
struct FeastSink : FeastCommandSink {
    int held = 0, invites = 0, course = 0, drink = 0, guestCount = 0;
    void reset() { held = invites = course = drink = guestCount = 0; }
    void HoldFeast(int, int c, int d, int g) override { ++held; course = c; drink = d; guestCount = g; }
    void InviteGuest(int, int) override { ++invites; }
};
} // namespace

TEST(GuiDlg2Feast, MenusAndChoice) {
    FeastMenuLayout course = FeastDialog_BuildCourseMenu();
    CHECK(std::strcmp(course.form, kFormFeast) == 0);
    CHECK_EQ(course.header, kTextFeastCourseHdr);
    CHECK_EQ(course.base, kTextFeastCourseBase);
    CHECK_EQ(FeastDialog_DispatchMenu(course, course.childIds[2]), 2);
    CHECK_EQ(FeastDialog_DispatchMenu(course, 99999), -1);
    // Drink menu only when wine cellar.
    FeastState s{}; s.hasWineCellar = false;
    CHECK(FeastDialog_BuildDrinkMenu(s).form == nullptr);
    s.hasWineCellar = true;
    FeastMenuLayout drink = FeastDialog_BuildDrinkMenu(s);
    CHECK_EQ(drink.base, kTextFeastDrinkBase);
}

TEST(GuiDlg2Feast, TableBuildAndDispatch) {
    FeastSink sink; FeastDialog_SetCommandSink(&sink);
    FeastState s{};
    s.building = 9; s.course = 1; s.drink = 2;
    s.guestEntities[0] = 100; s.guestEntities[2] = 200;
    s.guestRanks[2] = 6; // rank-6 guest -> kind 9
    FeastTableLayout t = FeastDialog_BuildTable(s);
    CHECK_EQ(t.guestCount, 2);
    CHECK(t.removeObj[0] != -1);
    CHECK_EQ(t.removeObj[1], -1);
    CHECK(t.removeObj[2] != -1);
    CHECK(!t.addDisabled);
    CHECK(!t.confirmDisabled);
    // Remove guest 0.
    CHECK_EQ(FeastDialog_DispatchTable(t, s, t.removeObj[0]), 0);
    // Add button.
    CHECK_EQ(FeastDialog_DispatchTable(t, s, t.addObj), -2);
    // Cancel.
    CHECK_EQ(FeastDialog_DispatchTable(t, s, t.cancelObj), -4);
    // Confirm -> hold + 2 invites.
    sink.reset();
    CHECK_EQ(FeastDialog_DispatchTable(t, s, t.confirmObj), -3);
    CHECK_EQ(sink.held, 1);
    CHECK_EQ(sink.guestCount, 2);
    CHECK_EQ(sink.course, 1);
    CHECK_EQ(sink.invites, 2);
    // Full table disables add; empty disables confirm.
    FeastState full{};
    for (int i = 0; i < kFeastMaxGuests; ++i) full.guestEntities[i] = 10 + i;
    CHECK(FeastDialog_BuildTable(full).addDisabled);
    FeastState empty{};
    CHECK(FeastDialog_BuildTable(empty).confirmDisabled);
}

// ===========================================================================
// pamphlet_board
// ===========================================================================
namespace {
struct PamSinkReal : PamphletCommandSink {
    int signed_ = 0, handler = 0;
    void reset() { signed_ = handler = 0; }
    void Sign(int h) override { ++signed_; handler = h; }
};
} // namespace

TEST(GuiDlg2Pamphlet, EntrySlotsAndSign) {
    PamSinkReal sink; PamphletBoard_SetCommandSink(&sink);
    std::vector<Pamphlet> ps = {
        {10, 1000, 0}, {0, 1001, 1} /*unresolved author -> skipped*/,
        {11, 1002, 2}, {12, 1003, 0}, {13, 1004, 0}, {14, 1005, 0} /*5th -> dropped*/,
    };
    PamphletLayout l = PamphletBoard_Build(ps);
    CHECK(std::strcmp(l.form, kFormPamphlet) == 0);
    CHECK_EQ((int)l.entries.size(), 4); // capped at 4, skipping the unresolved one
    CHECK_EQ(l.entries[0].oddSlot, 1);
    CHECK_EQ(l.entries[0].evenSlot, 2);
    CHECK_EQ(l.entries[1].oddSlot, 3);
    CHECK_EQ(l.entries[1].evenSlot, 4);
    CHECK_EQ(l.entries[3].oddSlot, 7);
    CHECK_EQ(l.entries[0].handler, 1000);
    CHECK_EQ(l.entries[1].handler, 1002); // the 0-author entry was skipped
    // Sign requires the skill check.
    sink.reset();
    CHECK_EQ(PamphletBoard_Dispatch(l, l.entries[1].objectId, /*skill=*/false), 1002);
    CHECK_EQ(sink.signed_, 0); // skill failed -> no command, still returns handler
    sink.reset();
    CHECK_EQ(PamphletBoard_Dispatch(l, l.entries[1].objectId, /*skill=*/true), 1002);
    CHECK_EQ(sink.signed_, 1);
    CHECK_EQ(sink.handler, 1002);
    CHECK_EQ(PamphletBoard_Dispatch(l, 7777, true), -1);
}

// ===========================================================================
// quickchat_window
// ===========================================================================
TEST(GuiDlg2QuickChat, StateMachine) {
    // The original compares (now - 570) >= msgTick unsigned, so use realistic large ticks
    // (the in-game counter is never near 0 where the unsigned wrap would occur).
    QuickChatState s{};
    // No message + closed -> nothing.
    s.formId = -1; s.msgTick = -1; s.now = 100000;
    CHECK(QuickChat_Update(s) == QuickChatAction::kNone);
    // Fresh message + closed -> create.
    s.msgTick = 100000; s.now = 100100; // age 100 < 570 -> not expired
    CHECK(QuickChat_Update(s) == QuickChatAction::kCreate);
    // Open + fresh -> refresh.
    s.formId = 5;
    CHECK(QuickChat_Update(s) == QuickChatAction::kRefresh);
    // Open + expired (570 ticks elapsed) -> destroy.
    s.now = 100000 + 570; // now-570 == msgTick -> expired
    CHECK(QuickChat_Update(s) == QuickChatAction::kDestroy);
    // Open + full console active -> destroy.
    s.now = 100100; s.fullConsoleActive = true;
    CHECK(QuickChat_Update(s) == QuickChatAction::kDestroy);
}

// ===========================================================================
// loading_screen
// ===========================================================================
TEST(GuiDlg2Loading, FormAndBarMath) {
    CHECK(std::strcmp(Loading_FormFor(0), kFormLoadingGame) == 0);
    CHECK(std::strcmp(Loading_FormFor(kLoadingNetFlag), kFormLoadingNet) == 0);
    CHECK(std::strcmp(Loading_FormFor(0x4 | 0x1), kFormLoadingNet) == 0);
    CHECK_EQ(Loading_BarValueForPercent(0), 0);
    CHECK_EQ(Loading_BarValueForPercent(50), 582 * 50 / 100); // 291
    CHECK_EQ(Loading_BarValueForPercent(100), 582);
    CHECK_EQ(kLoadingFadeFrames, 30);
}

// ===========================================================================
// stammbaum_window (family-tree node placement math)
// ===========================================================================
TEST(GuiDlg2Stammbaum, NodePlacementMath) {
    // canvasWidth=400 -> center 200; nodeWidth=40 -> half 20.
    Family fam{};
    fam.focus.entity  = 1;
    fam.spouse.entity = 2;
    fam.father.entity = 3;
    fam.mother.entity = 4;
    fam.children = { {5,0}, {6,0} }; // even count = 2
    FamilyTreeLayout l = Stammbaum_BuildLayout(fam, /*w=*/400, /*nw=*/40, /*title=*/0);
    CHECK(std::strcmp(l.form, kFormStammbaum) == 0);
    CHECK_EQ(l.titleText, kStammTitleDefault);
    CHECK_EQ(l.center, 200);
    // self at center-(nw+90) = 200-130 = 70; portrait +2.
    CHECK_EQ(l.self.x, 70);
    CHECK_EQ(l.self.portraitX, 72);
    // spouse at center+90 = 290.
    CHECK_EQ(l.spouse.x, 290);
    CHECK(!l.spouse.empty);
    // two parents: father center-(nw+16)=144, mother center+16=216.
    CHECK_EQ((int)l.parents.size(), 2);
    CHECK_EQ(l.parents[0].x, 144);
    CHECK_EQ(l.parents[1].x, 216);
    // even children: child0 = center-nw-20 = 140; child1 = center+20 = 220.
    CHECK_EQ((int)l.children.size(), 2);
    CHECK_EQ(l.children[0].x, 140);
    CHECK_EQ(l.children[1].x, 220);
}

TEST(GuiDlg2Stammbaum, SingleParentOddChildrenAndClicks) {
    Family fam{};
    fam.focus.entity = 1;
    // unmarried -> spouse empty; single parent (father only).
    fam.father.entity = 3;
    fam.children = { {5,0}, {6,0}, {7,0} }; // odd count = 3
    FamilyTreeLayout l = Stammbaum_BuildLayout(fam, 400, 40, /*title=*/42);
    CHECK_EQ(l.titleText, 42); // explicit title arg used
    CHECK(l.spouse.empty);
    // single parent centred at center-half = 200-20 = 180.
    CHECK_EQ((int)l.parents.size(), 1);
    CHECK_EQ(l.parents[0].x, 180);
    // odd children: child0 = center-half = 180; child1 = nw+center-half+40 = 40+180+40=260;
    //               child2 = center-half-40-nw = 180-40-40 = 100.
    CHECK_EQ((int)l.children.size(), 3);
    CHECK_EQ(l.children[0].x, 180);
    CHECK_EQ(l.children[1].x, 260);
    CHECK_EQ(l.children[2].x, 100);
    // Click resolution: clicking child1's object returns entity 6.
    CHECK_EQ(Stammbaum_DispatchClick(l, l.children[1].objectId), 6);
    CHECK_EQ(Stammbaum_DispatchClick(l, l.self.objectId), 1);
    CHECK_EQ(Stammbaum_DispatchClick(l, l.parents[0].objectId), 3);
    // empty spouse object is not clickable.
    CHECK_EQ(Stammbaum_DispatchClick(l, l.spouse.objectId), -1);
    CHECK_EQ(Stammbaum_DispatchClick(l, 123456), -1);
}

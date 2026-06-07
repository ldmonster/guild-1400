#include "test.h"

// Unit coverage for the remaining VIBE_Interaction_*/VIBE_Dialog_* slice
// (sim/interaction4.{h,cpp}). Golden vectors for the gate formulas computed with
// python against the recovered float constants; the Dialog/panel FSMs are exercised
// via installed hooks + the dialog trace.
#include "sim/interaction4.h"

#include <cmath>
#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {
// Test hook state.
double g_priceRet = 0.0;
int   g_scaleRet = 0;       // raw bits returned as the score's int reinterpretation
int   g_lastEmitAction = 0;
const char* g_lastEmitTag = nullptr;

double HPrice(i16, u8) { return g_priceRet; }
int  HScale(int, int) { return g_scaleRet; }
void HEmit(const char* tag, int action) { g_lastEmitTag = tag; g_lastEmitAction = action; }

int   AsBits(float f) { int x; std::memcpy(&x, &f, sizeof x); return x; }

void Setup() {
    ResetInteraction4Hooks();
    ResetI4DialogTrace();
    g_priceRet = 0.0; g_scaleRet = 0;
    g_lastEmitAction = 0; g_lastEmitTag = nullptr;
}
} // namespace

// --- EvalGiveGift ----------------------------------------------------------
TEST(Interaction4, GiveGiftGateRejects) {
    Setup();
    g_i4Hooks.marketPrice = HPrice;
    // wrong armed
    CHECK_EQ(EvalGiveGift(0, 2, 0, 1, true, 100, 1.0f, 7), 0);
    // wrong event mode
    CHECK_EQ(EvalGiveGift(0, 3, 1, 1, true, 100, 1.0f, 7), 0);
    // bad drag kind
    CHECK_EQ(EvalGiveGift(0, 2, 1, 2, true, 100, 1.0f, 7), 0);
    // unresolved target
    CHECK_EQ(EvalGiveGift(0, 2, 1, 1, false, 100, 1.0f, 7), 0);
}

TEST(Interaction4, GiveGiftCashVsPrice) {
    Setup();
    g_i4Hooks.marketPrice = HPrice;
    g_priceRet = 50.0;
    // cash 100 >= price 50 -> 11
    CHECK_EQ(EvalGiveGift(0, 2, 1, 7, true, 100, 1.0f, 7), 11);
    // cash 40 < price 50 -> 0
    CHECK_EQ(EvalGiveGift(0, 2, 1, 7, true, 40, 1.0f, 7), 0);
    // price factor 0.5 scales cash 100 -> 50, still >= 50 -> 11
    CHECK_EQ(EvalGiveGift(0, 2, 1, 4, true, 100, 0.5f, 7), 11);
    // price factor 0.4 scales cash 100 -> 40, < 50 -> 0
    CHECK_EQ(EvalGiveGift(0, 2, 1, 4, true, 100, 0.4f, 7), 0);
    // factor >= 1.0 ignored (no scaling).
    CHECK_EQ(EvalGiveGift(0, 2, 1, 1, true, 60, 1.5f, 7), 11);
}

// --- FindNearestTarget -----------------------------------------------------
TEST(Interaction4, FindNearestRevalidate) {
    Setup();
    g_i4Hooks.scaleByActionType = HScale;
    g_scaleRet = AsBits(10.0f);
    // budget 20 >= score 10 -> accept (15).
    FindTargetResult r = FindNearestRevalidate(0, 50, 20.0);
    CHECK_EQ(r.code, 15);
    CHECK(std::fabs(r.score - 10.0f) < 1e-4f);
    // budget 5 < score 10 -> reject.
    r = FindNearestRevalidate(0, 50, 5.0);
    CHECK_EQ(r.code, 0);
    // zero score -> reject regardless of budget.
    g_scaleRet = AsBits(0.0f);
    r = FindNearestRevalidate(0, 50, 1000.0);
    CHECK_EQ(r.code, 0);
}

TEST(Interaction4, FindNearestPickActionTypeAndBudget) {
    Setup();
    g_i4Hooks.scaleByActionType = HScale;
    // cash 100 -> budget 94.9999. dist 60 (>=50) -> actionType 30.
    g_scaleRet = AsBits(50.0f);
    FindTargetResult r = FindNearestPick(60, 0, 100);
    CHECK_EQ(r.chosenActionType, 30);
    CHECK_EQ(r.code, 15); // 50 <= 94.99
    // dist 10 (<50) -> actionType 50.
    r = FindNearestPick(10, 0, 100);
    CHECK_EQ(r.chosenActionType, 50);
    // score over budget -> reject.
    g_scaleRet = AsBits(200.0f);
    r = FindNearestPick(10, 0, 100);
    CHECK_EQ(r.code, 0);
    // zero score -> reject.
    g_scaleRet = AsBits(0.0f);
    r = FindNearestPick(10, 0, 100);
    CHECK_EQ(r.code, 0);
}

// --- RequestSellObject -----------------------------------------------------
TEST(Interaction4, RequestSellObjectModes) {
    Setup();
    g_i4Hooks.emitCommand = HEmit;
    // unresolved slot -> 0.
    CHECK_EQ(RequestSellObject(-1, 2, false, true, 100), 0);
    // wrong event mode -> 0.
    CHECK_EQ(RequestSellObject(5, 3, false, true, 100), 0);
    // currency mode, entity resolved -> 57.
    CHECK_EQ(RequestSellObject(5, 2, true, true, 100), 57);
    CHECK_EQ(g_lastEmitAction, 57);
    // currency mode, entity unresolved -> 0.
    CHECK_EQ(RequestSellObject(5, 2, true, false, 100), 0);
    // plain mode -> 11.
    CHECK_EQ(RequestSellObject(5, 2, false, true, 100), 11);
    CHECK_EQ(g_lastEmitAction, 11);
}

// --- PerformBroadcastSummon ------------------------------------------------
TEST(Interaction4, BroadcastSummonGatesAndCount) {
    Setup();
    g_i4Hooks.emitCommand = HEmit;
    int count = -1;
    // hall unresolved -> 0.
    CHECK_EQ(PerformBroadcastSummon(false, true, true, nullptr, 0, 100, &count), 0);
    CHECK_EQ(count, 0);
    // market unresolved -> 0.
    CHECK_EQ(PerformBroadcastSummon(true, false, true, nullptr, 0, 100, &count), 0);
    // both resolved, not kind5 -> 25, no broadcast.
    CHECK_EQ(PerformBroadcastSummon(true, true, false, nullptr, 0, 100, &count), 25);
    CHECK_EQ(count, 0);
    // kind5 with mixed types: only 6 and 7 count, capped at maxBroadcast.
    u8 types[6] = {6, 1, 7, 7, 2, 6};
    CHECK_EQ(PerformBroadcastSummon(true, true, true, types, 6, 100, &count), 25);
    CHECK_EQ(count, 4); // four of {6,7,7,6}
    CHECK_EQ(PerformBroadcastSummon(true, true, true, types, 6, 2, &count), 25);
    CHECK_EQ(count, 2); // capped
}

// --- building gate formulas (golden vectors) -------------------------------
TEST(Interaction4, EnterBuildingWorthReject) {
    Setup();
    CHECK_EQ(EnterBuildingWorthReject(1000, 200, 800), true);
    CHECK_EQ(EnterBuildingWorthReject(1000, 900, 800), false);
    CHECK_EQ(EnterBuildingWorthReject(2000, 100, 500), true);
    // degenerate worth/room -> no reject.
    CHECK_EQ(EnterBuildingWorthReject(0, 0, 0), false);
}

TEST(Interaction4, LeaveBuildingSaleReject) {
    Setup();
    CHECK_EQ(LeaveBuildingSaleReject(1000, 500), true);  // 0.5 >= 0.25
    CHECK_EQ(LeaveBuildingSaleReject(1000, 800), false); // 0.2 < 0.25
    CHECK_EQ(LeaveBuildingSaleReject(1000, 760), false); // 0.24 < 0.25
    CHECK_EQ(LeaveBuildingSaleReject(1000, 750), true);  // 0.25 >= 0.25
    CHECK_EQ(LeaveBuildingSaleReject(0, 0), false);
}

TEST(Interaction4, BuildingActionLeavePreGate) {
    Setup();
    CHECK_EQ(BuildingActionLeavePreGate(1, 10), true);   // 0.1 < 0.33
    CHECK_EQ(BuildingActionLeavePreGate(5, 10), false);  // 0.5 >= 0.33
    CHECK_EQ(BuildingActionLeavePreGate(3, 10), true);   // 0.3 < 0.33
    CHECK_EQ(BuildingActionLeavePreGate(0, 0), false);
}

TEST(Interaction4, BuildingActionRankGate) {
    Setup();
    // level1Count over capacity -> reject.
    CHECK_EQ(BuildingActionRankGate(5, 2, 16, 0), true);
    // gameTimeLo%8 (16%8==0) == entityLow3 (8&7==0) -> NOT reject.
    CHECK_EQ(BuildingActionRankGate(1, 2, 16, 8), false);
    // gameTimeLo%8 (17%8==1) != entityLow3 (8&7==0) -> reject.
    CHECK_EQ(BuildingActionRankGate(1, 2, 17, 8), true);
}

// --- DispatchPanelEvent FSM -------------------------------------------------
TEST(Interaction4, PanelInactiveOrNoStep) {
    Setup();
    TutorialPanel p;
    p.active = false;
    CHECK(DispatchPanelEvent(&p, 5, 0, 0, 100) == &p);
    p.active = true;
    p.step = nullptr;
    CHECK(DispatchPanelEvent(&p, 5, 0, 0, 100) == &p);
    CHECK_EQ(p.eventCount, 0);
}

TEST(Interaction4, PanelAdvancedMode1Confirm) {
    Setup();
    PanelStep step;
    step.mode = 2;
    step.mode2 = 1;
    step.callback = 0;
    step.targetId = (5 << 24); // targetId>>24 == 5
    TutorialPanel p;
    p.active = true;
    p.subState = 4;
    p.step = &step;
    // flag==0, eventIdHi==5 matches -> subState 5, auxId set.
    DispatchPanelEvent(&p, 5, 77, 0, 1234);
    CHECK_EQ((int)p.subState, 5);
    CHECK_EQ(p.lastTick, 1234);
    CHECK_EQ(p.auxId, 77);
    CHECK_EQ(p.eventCount, 1);
}

TEST(Interaction4, PanelAdvancedMode3State9ToState10) {
    Setup();
    PanelStep step;
    step.mode = 2;
    step.mode2 = 3;
    step.callback = 0;
    step.altTargetId = 42;
    TutorialPanel p;
    p.active = true;
    p.subState = 9;
    p.step = &step;
    // eventIdHi == altTargetId(42) -> transition 9 -> 10.
    DispatchPanelEvent(&p, 42, 0, 0, 555);
    CHECK_EQ((int)p.subState, 10);
    CHECK_EQ((int)p.combinedState, 10);
    CHECK_EQ(p.eventCount, 1);
}

TEST(Interaction4, PanelAdvancedCallbackRecorded) {
    Setup();
    PanelStep step;
    step.mode = 2;
    step.callback = 0xDEAD; // non-zero -> custom handler path
    TutorialPanel p;
    p.active = true;
    p.subState = 4;
    p.step = &step;
    DispatchPanelEvent(&p, 1, 0, 0, 1);
    CHECK(p.callbackInvoked);
    CHECK_EQ(p.eventCount, 0); // callback path returns before bumping
}

TEST(Interaction4, PanelSimpleDwellAdvance) {
    Setup();
    PanelStep step;
    step.mode = 3;        // dwell mode
    step.callback = 0;
    step.dwellLimit = 100;
    step.nextState = 7;
    TutorialPanel p;
    p.active = true;
    p.subState = 4;
    p.lastTick = 10;
    p.step = &step;
    // dwellLimit(100)+lastTick(10)=110 < nowTick(200) -> advance to 7.
    DispatchPanelEvent(&p, 0, 0, 0, 200);
    CHECK_EQ((int)p.subState, 7);
    CHECK_EQ(p.auxFlag, 1);
    // not enough dwell -> no change.
    p.subState = 4; p.lastTick = 10;
    DispatchPanelEvent(&p, 0, 0, 0, 50);
    CHECK_EQ((int)p.subState, 4);
}

// --- Dialog FSMs via hooks + trace -----------------------------------------
TEST(Interaction4, DialogAttackCommandBusyGate) {
    Setup();
    bool busy = true;
    g_i4Hooks.checkActiveCharFlag = [] { return true; };
    Dialog_AttackCommand(100, 0);
    (void)busy;
    CHECK(!g_i4DialogTrace.barOpened); // gated out
}

TEST(Interaction4, DialogAttackCommandConfirmFlow) {
    Setup();
    g_i4Hooks.emitCommand = HEmit;
    g_i4Hooks.checkActiveCharFlag = [] { return false; };
    g_i4Hooks.showMessageBox = [](int, int, char) { return true; };
    Dialog_AttackCommand(100, 0);
    CHECK(g_i4DialogTrace.barOpened);
    CHECK(g_i4DialogTrace.messageBoxShown);
    CHECK(g_i4DialogTrace.selectionCleared);
    CHECK(g_i4DialogTrace.barClosed);
    CHECK_EQ(g_lastEmitAction, 108);
    // declined: bar opens+closes but no command/selection.
    Setup();
    g_i4Hooks.emitCommand = HEmit;
    g_i4Hooks.checkActiveCharFlag = [] { return false; };
    g_i4Hooks.showMessageBox = [](int, int, char) { return false; };
    Dialog_AttackCommand(100, 0);
    CHECK(g_i4DialogTrace.barOpened);
    CHECK(!g_i4DialogTrace.messageBoxShown);
    CHECK(!g_i4DialogTrace.selectionCleared);
    CHECK(g_i4DialogTrace.barClosed);
}

TEST(Interaction4, DialogTavernStammtisch) {
    Setup();
    // no node -> nothing.
    Dialog_TavernStammtischDispatch(false, true, false);
    CHECK_EQ(g_i4DialogTrace.tavernJoin, 0);
    CHECK_EQ(g_i4DialogTrace.tavernLeave, 0);
    // node + handler, not seated -> join.
    Dialog_TavernStammtischDispatch(true, true, false);
    CHECK_EQ(g_i4DialogTrace.tavernJoin, 1);
    // node + handler, seated -> leave.
    Dialog_TavernStammtischDispatch(true, true, true);
    CHECK_EQ(g_i4DialogTrace.tavernLeave, 1);
}

TEST(Interaction4, DialogTavernDarkCorner) {
    Setup();
    Dialog_TavernDarkCornerDispatch(false, true);
    CHECK_EQ(g_i4DialogTrace.tavernBrowse, 0);
    Dialog_TavernDarkCornerDispatch(true, true);
    CHECK_EQ(g_i4DialogTrace.tavernBrowse, 1);
    Dialog_TavernDarkCornerDispatch(true, false);
    CHECK_EQ(g_i4DialogTrace.tavernBuy, 1);
}

TEST(Interaction4, DialogRobberCampFlows) {
    Setup();
    g_i4Hooks.checkActiveCharFlag = [] { return true; };
    CHECK_EQ(Dialog_RobberCampCheckAndShow(true), 0); // busy
    Setup();
    g_i4Hooks.checkActiveCharFlag = [] { return false; };
    CHECK_EQ(Dialog_RobberCampCheckAndShow(false), 0); // no camp
    CHECK(!g_i4DialogTrace.barOpened);
    Setup();
    g_i4Hooks.checkActiveCharFlag = [] { return false; };
    CHECK_EQ(Dialog_RobberCampCheckAndShow(true), 1);
    CHECK(g_i4DialogTrace.barOpened);
    CHECK_EQ(g_i4DialogTrace.panelKind, 1);
    CHECK(g_i4DialogTrace.selectionCleared);
    // ShowBar is unconditional.
    Setup();
    CHECK_EQ(Dialog_RobberCampShowBar(), 1);
    CHECK(g_i4DialogTrace.barOpened);
    CHECK(g_i4DialogTrace.barClosed);
}

TEST(Interaction4, DialogSpionageConfirm) {
    Setup();
    Dialog_SpionageConfirm();
    CHECK_EQ(g_i4DialogTrace.officeWindow, 1);
}

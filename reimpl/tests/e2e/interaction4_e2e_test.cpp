#include "test.h"

// e2e: exercise a cross-function interaction flow — an autonomous actor evaluates a
// gift, then a sell, then attempts a broadcast summon, then a confirm-dialog drives
// the attack command — wiring all leaves through Interaction4Hooks and asserting the
// emitted action codes / command trace across the whole chain.
#include "sim/interaction4.h"

#include <cmath>

using namespace guild;
using namespace guild::sim;

namespace {
double g_price = 0.0;
double Price(i16, u8) { return g_price; }

struct EmitLog { int count = 0; int lastAction = 0; const char* lastTag = nullptr; } g_log;
void Emit(const char* tag, int action) { g_log = {g_log.count + 1, action, tag}; }
} // namespace

// A wealthy actor: gift accepted, then sells the same object, then summons workers.
TEST(Interaction4E2E, EconomyActionChain) {
    ResetInteraction4Hooks();
    ResetI4DialogTrace();
    g_log = EmitLog{};
    g_i4Hooks.marketPrice = &Price;
    g_i4Hooks.emitCommand = &Emit;
    g_price = 30.0;

    // 1) Give a gift: cash 100 >= price 30 -> 11.
    char gift = EvalGiveGift(0, 2, 1, 7, true, 100, 1.0f, 7);
    CHECK_EQ(gift, 11);

    // 2) Sell the object (plain mode, resolved slot) -> 11 + a command.
    char sell = RequestSellObject(5, 2, false, true, 30);
    CHECK_EQ(sell, 11);
    CHECK_EQ(g_log.lastAction, 11);

    // 3) Broadcast summon: hall + market resolved, actor kind 5, two craft buildings.
    u8 types[3] = {6, 2, 7};
    int bc = -1;
    int summon = PerformBroadcastSummon(true, true, true, types, 3, 100, &bc);
    CHECK_EQ(summon, 25);
    CHECK_EQ(bc, 2);
    CHECK_EQ(g_log.lastAction, 25);
    CHECK(g_log.count >= 2);
}

// A poor actor is rejected at the gift gate and never reaches the sell.
TEST(Interaction4E2E, PoorActorRejected) {
    ResetInteraction4Hooks();
    g_i4Hooks.marketPrice = &Price;
    g_price = 500.0;

    char gift = EvalGiveGift(0, 2, 1, 1, true, 50, 1.0f, 7);
    CHECK_EQ(gift, 0); // cash 50 < price 500
}

// Confirm-dialog flow: a tutorial panel advances, then the attack confirm dialog
// runs end-to-end (bar open -> confirm -> command emit -> selection clear -> close).
TEST(Interaction4E2E, PanelThenAttackConfirm) {
    ResetInteraction4Hooks();
    ResetI4DialogTrace();
    g_log = EmitLog{};
    g_i4Hooks.emitCommand = &Emit;
    g_i4Hooks.checkActiveCharFlag = [] { return false; };
    g_i4Hooks.showMessageBox = [](int, int, char) { return true; };

    // Panel: advanced mode-3, state 4 -> state 9 on a matching event (flag==0).
    PanelStep step;
    step.mode = 2;
    step.mode2 = 3;
    step.callback = 0;
    step.targetId = (3 << 24);
    TutorialPanel panel;
    panel.active = true;
    panel.subState = 4;
    panel.step = &step;
    DispatchPanelEvent(&panel, 3, 0, 0, 100);
    CHECK_EQ((int)panel.subState, 9);

    // Then the attack confirm dialog drives the full orchestration.
    Dialog_AttackCommand(4876, 0);
    CHECK(g_i4DialogTrace.barOpened);
    CHECK(g_i4DialogTrace.messageBoxShown);
    CHECK(g_i4DialogTrace.selectionCleared);
    CHECK(g_i4DialogTrace.barClosed);
    CHECK_EQ(g_log.lastAction, 108); // cm_Angriff slot-reset attack command
}

// Building gate chain: enter rejected, then a profitable leave proceeds.
TEST(Interaction4E2E, BuildingGateChain) {
    ResetInteraction4Hooks();
    // Worth gate rejects an unprofitable enter.
    CHECK_EQ(EnterBuildingWorthReject(1000, 200, 800), true);
    // A sale with a healthy margin is rejected by the high-margin guard...
    CHECK_EQ(LeaveBuildingSaleReject(1000, 500), true);  // 0.5 >= 0.25 -> reject
    // ...but a thin margin proceeds.
    CHECK_EQ(LeaveBuildingSaleReject(1000, 800), false); // 0.2 < 0.25 -> proceed
    // Building-action pre-gate fires on a low occupancy ratio.
    CHECK_EQ(BuildingActionLeavePreGate(2, 10), true);   // 0.2 < 0.33
}

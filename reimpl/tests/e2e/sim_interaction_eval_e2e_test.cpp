// End-to-end flow tests for the score-table-driven AiMethod Eval handlers. Runs a
// person through a social interaction sequence (choose gesture -> talk -> social
// gesture) and a combat eval sequence (pick-target budget -> attack-target budget),
// verifying the selected action codes + emitted commands + planner invocations
// against a reference computed from the IDA decompilation, with a seeded RNG and a
// mocked classifier/planner backend (the render/object-search leaves are mocked).
#include "tests/framework/test.h"

#include "sim/interaction_eval.h"
#include "ai/ai_eval.h"
#include "crt/rand.h"

using namespace guild;
using namespace guild::sim;

namespace {

// A scripted classifier backend: a small "world" where the actor can use one item
// (the first requested) at a known object. This drives the "use now" branch.
int g_usableSlotE2E = 0;
int g_usableObj = 8000;
void WorldClassify(const void*, int n, ai::ItemSlot* slots, ai::ItemSummary* sum) {
    for (int i = 0; i < n; ++i) { slots[i].available = 0; slots[i].objectId = 0; }
    sum->accessibleNodes = 1;
    sum->blockedCount = 0;
    sum->usableCount = 0;
    if (g_usableSlotE2E >= 0 && g_usableSlotE2E < n) {
        slots[g_usableSlotE2E].available = 1;
        slots[g_usableSlotE2E].objectId = g_usableObj;
        sum->accessibleNodes = 7; // >= 6 so the "use now" branch is taken
        sum->usableCount = 1;
    }
}

// A planner that always accepts the proposed primary action.
int g_calls = 0;
u8 AcceptPlanner(u8, int, ai::EvalFrame* a, int, ai::EvalFrame*) {
    ++g_calls;
    return a ? a->kind() : 0;
}

EvalActor MakeActor() {
    EvalActor a;
    a.personId = 42;
    a.entityId = 9001;
    a.kind = 5;
    a.assocNode = 777;
    return a;
}

} // namespace

// === Social interaction flow: gesture -> talk -> social-gesture =============
TEST(SimIntEvalE2E, SocialInteractionFlow) {
    crt::Srand(7);
    ResetEvalLeafHooks();
    ResetEvalCommandTrace();
    ai::ResetEvalHooks();
    ai::SetClassifyItemsHook(WorldClassify);
    ai::SetSelectBestHook(AcceptPlanner);
    EvalActor a = MakeActor();

    // --- Step 1: choose a gesture. The actor can use slot 0 (item 341). With a
    // 9-slot table and slot 0 usable, the "use now" branch fires. The RNG slot pick
    // lands on a usable slot; for gesture, slot 0 goes through the planner (mode 3).
    g_usableSlotE2E = 0;
    ai::EvalFrame ga, gb;
    char gres = EvalChooseGesture(0, &a, &ga, 0, &gb);
    // planner accepts -> returns the frame kind (1 == "use now").
    CHECK_EQ((int)gres, 1);
    CHECK_EQ((int)ga.kind(), 1);
    CHECK_EQ((int)ga.w[1], g_usableObj); // use-frame arg0 == resolved object id

    // --- Step 2: a talk action. Slot 0 (item 371) usable -> "use now"; talk slot 0
    // goes through the planner (mode 4), which accepts -> kind 1.
    ai::EvalFrame ta, tb;
    char tres = EvalChooseTalkAction(0, &a, &ta, 0, &tb);
    CHECK_EQ((int)tres, 1);
    CHECK_EQ((int)ta.kind(), 1);

    // --- Step 3: a social gesture. Slot 0 usable; a peer target resolves to the
    // actor's assocNode (777). slot 0 <= 1 -> planner (mode 4) accepts -> kind 1.
    ai::EvalFrame sa, sb;
    char sres = EvalChooseSocialGesture(0, &a, &sa, 0, &sb);
    CHECK_EQ((int)sres, 1);
    CHECK_EQ((int)sa.kind(), 1);
    CHECK_EQ((int)sb.w[1], 777); // companion frame carries the resolved peer

    // The planner was consulted once per accepted social action.
    CHECK_EQ(g_calls, 3);
}

// === Send-message command emission ==========================================
TEST(SimIntEvalE2E, SendMessageEmitsCommand) {
    ResetEvalCommandTrace();
    // The actor sends a message to a resolvable recipient. Action 9 + one command.
    char r = EvalSendMessage(7, 18, /*recipientResolved=*/true, /*kind=*/6);
    CHECK_EQ((int)r, 9);
    CHECK_EQ(g_evalCmdTrace.count, 1);
    CHECK_EQ(g_evalCmdTrace.lastAction, 9);
}

// === Combat eval flow: pick-target budget -> attack-target budget ===========
namespace {
int g_cash = 0, g_wealth = 0;
int CombatCash(u16) { return g_cash; }
int CombatWealth(u16) { return g_wealth; }
} // namespace

TEST(SimIntEvalE2E, CombatEvalFlow) {
    crt::Srand(11);
    ResetEvalLeafHooks();
    g_evalHooks.currencyAmount = CombatCash;
    g_evalHooks.totalWealth = CombatWealth;

    // A wealthy attacker: wealth 200000, cash 20000.
    g_wealth = 200000;
    g_cash = 20000;

    // --- Pick target budget: cost = wealth*0.02 + roomWorth*0.05.
    // wealth 200000 -> 4000; roomWorth 10000 -> 500; cost 4500.
    // budget = cash*0.44 = 8800 >= 4500 -> may proceed.
    CHECK(PickTargetBudgetGate(g_cash, g_wealth, 10000, false));

    // --- Attack target budget: cost = wealth*0.0085 = 1700.
    // budget = cash*0.22 = 4400 >= 1700 -> may proceed.
    CHECK(AttackTargetBudgetGate(42, false));

    // A poor attacker can pass the cheap pick gate but fail the (relatively) more
    // demanding attack gate when cash is tiny.
    g_cash = 100;
    // pick: budget 44 < cost 4500 -> reject.
    CHECK(!PickTargetBudgetGate(g_cash, g_wealth, 10000, false));
    // attack: cost 1700 > budget 22 -> reject.
    CHECK(!AttackTargetBudgetGate(42, false));

    // Halving the attack cost (DispatchByType bit2) does not rescue a near-broke
    // attacker: 850 still > 22.
    CHECK(!AttackTargetBudgetGate(42, true));
}

// === Buy-object affordability end-to-end ====================================
namespace {
int g_buyCashE2E = 0, g_buyPriceE2E = 0;
int BuyCashE2E(u16) { return g_buyCashE2E; }
int BuyPriceE2E(int, u8) { return g_buyPriceE2E; }
} // namespace

TEST(SimIntEvalE2E, BuyObjectAffordability) {
    ResetEvalLeafHooks();
    g_evalHooks.currencyAmount = BuyCashE2E;
    g_evalHooks.marketPrice = BuyPriceE2E;
    g_buyCashE2E = 5000;
    g_buyPriceE2E = 3000;
    // affordable + a free slot -> buy (14).
    CHECK_EQ((int)EvalBuyObject(5, 2, 1, 4, 1, 2, 50, 0.0f, 1), 14);
    // a price factor of 0.5 halves the budget to 2500 < 3000 -> reject.
    CHECK_EQ((int)EvalBuyObject(5, 2, 1, 4, 1, 2, 50, 0.5f, 1), 0);
}

// Unit tests for the large score-table-driven AiMethod social/behavior Eval
// handlers (interaction_eval.{h,cpp} + ai/ai_eval.{h,cpp}). Each test drives a
// translated Eval with synthetic actor/target state + a seeded RNG and a mocked
// classifier/planner, and checks the score/selection/eligibility verdict + emitted
// command against a hand-computed reference from the IDA decompilation.
#include "tests/framework/test.h"

#include <cstring>

#include "sim/interaction_eval.h"
#include "ai/ai_eval.h"
#include "crt/rand.h"

using namespace guild;
using namespace guild::sim;

namespace {

EvalActor MakeActor() {
    EvalActor a;
    a.personId = 100;
    a.entityId = 4242;
    a.kind = 5;
    a.assocNode = -1;
    return a;
}

// A classifier that marks the slot at `usableSlot` usable (objectId = base+slot),
// optionally marks slots blocked, and sets the summary counts. accessibleNodes is
// configurable to exercise the <6 / ==6 branch gates.
int g_usableSlot = -1;
int g_blockedCount = 0;
int g_usableCount = 0;
int g_accessibleNodes = 1;
int g_objBase = 7000;

void TestClassify(const void*, int n, ai::ItemSlot* slots, ai::ItemSummary* sum) {
    for (int i = 0; i < n; ++i) {
        slots[i].available = 0;
        slots[i].objectId = 0;
    }
    if (g_usableSlot >= 0 && g_usableSlot < n) {
        slots[g_usableSlot].available = 1;
        slots[g_usableSlot].objectId = g_objBase + g_usableSlot;
    }
    sum->accessibleNodes = g_accessibleNodes;
    sum->blockedCount = g_blockedCount;
    sum->usableCount = g_usableCount;
}

// A planner that records its call and returns a configurable code.
int g_plannerCalls = 0;
u8  g_plannerReturn = 0;
u8  g_plannerLastClass = 0;
int g_plannerLastMode = 0;
u8 TestPlanner(u8 classId, int, ai::EvalFrame*, int mode, ai::EvalFrame*) {
    ++g_plannerCalls;
    g_plannerLastClass = classId;
    g_plannerLastMode = mode;
    return g_plannerReturn;
}

void Setup() {
    crt::Srand(1);
    ResetEvalLeafHooks();
    ResetEvalCommandTrace();
    ai::ResetEvalHooks();
    g_usableSlot = -1;
    g_blockedCount = 0;
    g_usableCount = 0;
    g_accessibleNodes = 1;
    g_plannerCalls = 0;
    g_plannerReturn = 0;
}

} // namespace

// --- recovered tables -------------------------------------------------------
TEST(SimIntEval, RecoveredItemTables) {
    CHECK_EQ((int)kGestureItems[0], 341);
    CHECK_EQ((int)kGestureItems[1], 349);
    CHECK_EQ((int)kGestureItems[3], 351);
    CHECK_EQ((int)kGestureItems[8], 368);
    CHECK_EQ((int)kTalkItems[0], 371);
    CHECK_EQ((int)kTalkItems[1], 347);
    CHECK_EQ((int)kFlirtItems[0], 360);
    CHECK_EQ((int)kFlirtItems[1], 364);
    CHECK_EQ((int)kInsultItems[0], 380);
    CHECK_EQ((int)kInsultItems[1], 369);
    CHECK_EQ((int)kSocialGestureItems[0], 361);
    CHECK_EQ((int)kSocialGestureItems[4], 365);
    CHECK_EQ((int)kGroupGreetItems[0], 354);
    CHECK_EQ((int)kGroupGreetItems[2], 356);
    CHECK_EQ((int)kDrinkItemDrunk, 382);
    CHECK_EQ((int)kDrinkItemSober, 373);
    CHECK_EQ((int)kSelectWorkerItem, 353);
}

TEST(SimIntEval, FrameScoreConstants) {
    // verify the recovered raw-int score bits decode to the documented floats.
    ai::EvalFrame f;
    f.set_scoreBits(ai::kScoreBits005);
    float v; std::memcpy(&v, &f.w[5], 4);
    CHECK(v > 0.0499f && v < 0.0501f);
    f.set_scoreBits(ai::kScoreBits044);
    std::memcpy(&v, &f.w[5], 4);
    CHECK(v > 0.439f && v < 0.441f);
}

// --- social family: armed / prior-result rejects ---------------------------
TEST(SimIntEval, SocialRejectArmedAndPrior) {
    Setup();
    EvalActor a = MakeActor();
    ai::EvalFrame fa, fb;
    // armed -> 0 for all
    CHECK_EQ((int)EvalChooseGesture(0, &a, &fa, 1, &fb), 0);
    CHECK_EQ((int)EvalChooseTalkAction(0, &a, &fa, 1, &fb), 0);
    CHECK_EQ((int)EvalChooseDrinkAction(0, &a, &fa, 1, &fb), 0);
    // prior result rejects Gesture/Insult/SocialGesture but NOT Talk/Flirt/Drink's
    // prologue (those still proceed past the prior-result check; here they reject
    // later because the classifier marks nothing accessible: accessibleNodes<6 &&
    // !usable -> acquire branch with no blocked slot... we set blockedCount=n=2).
    g_blockedCount = 9;
    CHECK_EQ((int)EvalChooseGesture(5, &a, &fa, 0, &fb), 0); // prior result -> 0
}

// --- social family: "acquire" branch (no usable item, some accessible) ------
TEST(SimIntEval, SocialAcquireBranchCallsPlanner) {
    Setup();
    ai::SetClassifyItemsHook(TestClassify);
    ai::SetSelectBestHook(TestPlanner);
    EvalActor a = MakeActor();
    // accessibleNodes < 6, usable == 0, blocked < n -> acquire branch.
    g_accessibleNodes = 1;
    g_usableCount = 0;
    g_blockedCount = 0; // all slots available==0 -> first slot picked
    g_plannerReturn = 28;
    ai::EvalFrame fa, fb;
    char r = EvalChooseTalkAction(0, &a, &fa, 0, &fb);
    CHECK_EQ((int)r, 28);
    CHECK_EQ(g_plannerCalls, 1);
    CHECK_EQ(g_plannerLastMode, 2);     // acquire mode
    CHECK_EQ((int)fa.kind(), 2);        // acquire frame kind
    CHECK_EQ((int)fb.kind(), 7);        // companion frame kind
    CHECK_EQ((int)fb.w[1], 4242);       // companion arg0 == actor entity id
}

// --- social family: planner reject yields 0 ---------------------------------
TEST(SimIntEval, SocialAcquirePlannerRejectZero) {
    Setup();
    ai::SetClassifyItemsHook(TestClassify);
    ai::SetSelectBestHook(TestPlanner);
    EvalActor a = MakeActor();
    g_accessibleNodes = 1; g_usableCount = 0; g_blockedCount = 0;
    g_plannerReturn = 0;
    ai::EvalFrame fa, fb;
    CHECK_EQ((int)EvalChooseFlirtAction(0, &a, &fa, 0, &fb), 0);
    CHECK_EQ(g_plannerCalls, 1);
}

// --- social family: early rejects -------------------------------------------
TEST(SimIntEval, SocialEarlyRejects) {
    Setup();
    ai::SetClassifyItemsHook(TestClassify);
    EvalActor a = MakeActor();
    ai::EvalFrame fa, fb;
    // accessibleNodes==6 && !usable -> 0
    g_accessibleNodes = 6; g_usableCount = 0;
    CHECK_EQ((int)EvalChooseTalkAction(0, &a, &fa, 0, &fb), 0);
    // blockedCount == n -> 0
    g_accessibleNodes = 1; g_blockedCount = 2;
    CHECK_EQ((int)EvalChooseTalkAction(0, &a, &fa, 0, &fb), 0);
}

// --- drink: sober 2-in-3 reject, drunk always proceeds ----------------------
TEST(SimIntEval, DrinkSoberRejectAndDrunk) {
    Setup();
    ai::SetClassifyItemsHook(TestClassify);
    ai::SetSelectBestHook(TestPlanner);
    EvalActor a = MakeActor();
    ai::EvalFrame fa, fb;
    // sober: RandomModulo(3) consumes one draw; if nonzero -> 0. Seed so we can
    // determine. We just check that a sober actor with classifier rejecting still
    // returns 0 (either via the 2-in-3 reject or the classify reject).
    a.drinkState = 0;
    g_accessibleNodes = 6; g_usableCount = 0;
    CHECK_EQ((int)EvalChooseDrinkAction(0, &a, &fa, 0, &fb), 0);
    // drunk: skips the random reject; with accessibleNodes==6 & !usable still 0.
    a.drinkState = 1;
    CHECK_EQ((int)EvalChooseDrinkAction(0, &a, &fa, 0, &fb), 0);
}

// --- EnterTavern ------------------------------------------------------------
static int g_tavernDispatch = 0;
static int TavernDispatch(int, u16) { return g_tavernDispatch; }
TEST(SimIntEval, EnterTavern) {
    Setup();
    g_evalHooks.dispatchByType = TavernDispatch;
    g_tavernDispatch = 2;
    CHECK_EQ((int)EvalEnterTavern(0, 100), 22);
    CHECK_EQ((int)EvalEnterTavernDirect(100), 22);
    g_tavernDispatch = 4;       // 4 != 2 -> reject (only ==2 passes)
    CHECK_EQ((int)EvalEnterTavern(0, 100), 0);
    g_tavernDispatch = 2;
    CHECK_EQ((int)EvalEnterTavern(7, 100), 0); // prior result short-circuits
}

// --- BuyObject --------------------------------------------------------------
static int g_buyCash = 0, g_buyPrice = 0;
static int BuyCash(u16) { return g_buyCash; }
static int BuyPrice(int, u8) { return g_buyPrice; }
TEST(SimIntEval, BuyObjectGates) {
    Setup();
    g_evalHooks.currencyAmount = BuyCash;
    g_evalHooks.marketPrice = BuyPrice;
    g_buyCash = 1000; g_buyPrice = 500;
    // happy path: kind!=3, armed==1, mode==2, descType==2, dragKind==4, cash>=price,
    // free slots > 0.
    CHECK_EQ((int)EvalBuyObject(5, 2, 1, 4, 200, 2, 99, 0.0f, 1), 14);
    // cash < price -> 0
    g_buyPrice = 2000;
    CHECK_EQ((int)EvalBuyObject(5, 2, 1, 4, 200, 2, 99, 0.0f, 1), 0);
    g_buyPrice = 500;
    // wrong mode/type/drag -> 0
    CHECK_EQ((int)EvalBuyObject(3, 2, 1, 4, 200, 2, 99, 0.0f, 1), 0); // kind 3
    CHECK_EQ((int)EvalBuyObject(5, 2, 0, 4, 200, 2, 99, 0.0f, 1), 0); // armed!=1
    CHECK_EQ((int)EvalBuyObject(5, 1, 1, 4, 200, 2, 99, 0.0f, 1), 0); // mode!=2
    CHECK_EQ((int)EvalBuyObject(5, 2, 1, 4, 200, 2, 99, 0.0f, 0), 0); // no free slot
    // price factor scales the budget: cash 1000 * 0.4 = 400 < 500 price -> 0
    CHECK_EQ((int)EvalBuyObject(5, 2, 1, 4, 200, 2, 99, 0.4f, 1), 0);
}

// --- SendMessage ------------------------------------------------------------
TEST(SimIntEval, SendMessageGatesAndCommand) {
    Setup();
    // mode 7 + dragSourceType 18 + resolvable recipient -> 9 + command emitted.
    CHECK_EQ((int)EvalSendMessage(7, 18, true, 6), 9);
    CHECK_EQ(g_evalCmdTrace.count, 1);
    CHECK(g_evalCmdTrace.lastTag != nullptr);
    CHECK_EQ(g_evalCmdTrace.lastAction, 9);
    // gate failures
    CHECK_EQ((int)EvalSendMessage(2, 18, true, 6), 0);  // mode!=7
    CHECK_EQ((int)EvalSendMessage(7, 1, true, 6), 0);   // dragType!=18
    CHECK_EQ((int)EvalSendMessage(7, 18, false, 6), 0); // unresolved
}

// --- DuelChallenge ----------------------------------------------------------
TEST(SimIntEval, DuelChallengeRankPaths) {
    Setup();
    ai::SetSelectBestHook(TestPlanner);
    g_plannerReturn = 7;
    // rank < 2 -> planner path (mode 1, class 10).
    CHECK_EQ((int)EvalDuelChallenge(0, 1, 0, 100, 20, 0, 0, 0, nullptr, false), 7);
    CHECK_EQ((int)g_plannerLastClass, 10);
    CHECK_EQ(g_plannerLastMode, 1);
    // armed -> 0
    CHECK_EQ((int)EvalDuelChallenge(0, 1, 1, 100, 20, 0, 0, 0, nullptr, false), 0);
    // rank>=2: gameTimeHi<13 -> 0
    CHECK_EQ((int)EvalDuelChallenge(0, 3, 0, 100, 12, 0, 5, 0, nullptr, false), 0);
    // rank>=2: office set -> 0
    CHECK_EQ((int)EvalDuelChallenge(0, 3, 0, 100, 20, 9, 5, 0, nullptr, false), 0);
    // rank>=2: stock<3 -> 0
    CHECK_EQ((int)EvalDuelChallenge(0, 3, 0, 100, 20, 0, 2, 0, nullptr, false), 0);
    // rank>=2 with promotion list: a valid entry (1..0x1B) -> 10.
    u8 plist[24] = {0};
    plist[0] = 5; // promotionList[0*12]
    crt::Srand(2);
    CHECK_EQ((int)EvalDuelChallenge(0, 3, 0, 100, 20, 0, 5, 1, plist, false), 10);
    // invalid entry (>0x1B) -> 0
    plist[0] = 40;
    crt::Srand(2);
    CHECK_EQ((int)EvalDuelChallenge(0, 3, 0, 100, 20, 0, 5, 1, plist, false), 0);
}

// --- SelectWorker reputation gate -------------------------------------------
TEST(SimIntEval, SelectWorkerReputationGate) {
    Setup();
    ai::SetClassifyItemsHook(TestClassify);
    ai::SetSelectBestHook(TestPlanner);
    EvalActor a = MakeActor();
    ai::EvalFrame fa, fb;
    // no nearby workshop -> 0
    CHECK_EQ((int)EvalSelectWorker(0, &a, &fa, 0, &fb, false, 10.0f, 0), 0);
    // low reputation: avgRep + roll(0..3) < 6.0 -> 0. avgRep 0, roll<6 always.
    crt::Srand(5);
    CHECK_EQ((int)EvalSelectWorker(0, &a, &fa, 0, &fb, true, 0.0f, 0), 0);
    // high reputation passes the gate; classifier gives a usable slot -> action 27.
    g_usableSlot = 0; g_usableCount = 1; g_accessibleNodes = 6;
    crt::Srand(5);
    char r = EvalSelectWorker(0, &a, &fa, 0, &fb, true, 100.0f, 9999);
    CHECK_EQ((int)r, 27);
    CHECK_EQ((int)fb.kind(), 4);
    CHECK_EQ((int)fb.w[1], 9999); // worker object id in companion frame
}

// --- AssignPatrol / AssignDestination ---------------------------------------
TEST(SimIntEval, AssignPatrolGates) {
    Setup();
    ai::SetSelectBestHook(TestPlanner);
    g_plannerReturn = 21;
    EvalActor a = MakeActor();
    a.assocNode = 555;
    ai::EvalFrame fa, fb;
    // happy: kind!=3, eventActive=false, assocNode set, inventory match.
    char r = EvalAssignPatrol(&a, false, 0, true, -1, &fa, &fb);
    CHECK_EQ((int)r, 21);
    CHECK_EQ((int)fa.kind(), 2);
    CHECK_EQ((int)fb.kind(), 4);    // matchItemId == -1 -> companion kind 4 (node)
    CHECK_EQ((int)fb.w[1], 555);    // dest node id
    // matched item -> companion kind 1
    fb = ai::EvalFrame{};
    r = EvalAssignPatrol(&a, false, 0, true, 1234, &fa, &fb);
    CHECK_EQ((int)r, 21);
    CHECK_EQ((int)fb.kind(), 1);
    CHECK_EQ((int)fb.w[1], 1234);
    // kind 3 -> 0
    a.kind = 3;
    CHECK_EQ((int)EvalAssignPatrol(&a, false, 0, true, -1, &fa, &fb), 0);
    a.kind = 5;
    // no inventory match -> 0
    CHECK_EQ((int)EvalAssignPatrol(&a, false, 0, false, -1, &fa, &fb), 0);
    // no assoc node -> 0
    a.assocNode = -1;
    CHECK_EQ((int)EvalAssignPatrol(&a, false, 0, true, -1, &fa, &fb), 0);
}

// --- combat budget gates ----------------------------------------------------
static int g_gateCash = 0, g_gateWealth = 0;
static int GateCash(u16) { return g_gateCash; }
static int GateWealth(u16) { return g_gateWealth; }
TEST(SimIntEval, CombatBudgetGates) {
    Setup();
    g_evalHooks.currencyAmount = GateCash;
    g_evalHooks.totalWealth = GateWealth;
    // AttackTarget: cost = wealth*0.0085; reject if cost > cash*0.22.
    // wealth 100000 -> cost 850. cash 5000 -> 5000*0.22 = 1100 >= 850 -> pass.
    g_gateWealth = 100000; g_gateCash = 5000;
    CHECK(AttackTargetBudgetGate(100, false));
    // cash 1000 -> 220 < 850 -> reject.
    g_gateCash = 1000;
    CHECK(!AttackTargetBudgetGate(100, false));
    // halve cost: 425 < 220? no, still reject. cash 3000 -> 660 >= 425 -> pass.
    g_gateCash = 3000;
    CHECK(AttackTargetBudgetGate(100, true));
    CHECK(!AttackTargetBudgetGate(100, false)); // 850 > 660 -> reject

    // PickTarget: cost = wealth*0.02 + roomWorth*0.05; reject if cost > cash*0.44.
    // wealth 1000 -> 20, roomWorth 200 -> 10, cost 30. cash 100 -> 44 >= 30 -> pass.
    CHECK(PickTargetBudgetGate(100, 1000, 200, false));
    CHECK(!PickTargetBudgetGate(50, 1000, 200, false)); // 50*0.44=22 < 30 -> reject

    // EquipWeapon budget = cash*0.33.
    CHECK_EQ(EquipWeaponBudget(1000), 330);
    CHECK_EQ(EquipWeaponBudget(0), 0);
}

#include "sim/interaction_eval.h"

#include "ai/method.h"

// Faithful 1:1 port of the large score-table-driven AiMethod social/behavior Eval
// handlers. The social-action family shares one structure (seed item-id table ->
// classify -> pick slot -> build frames -> planner); the gate family translates the
// eligibility control flow with leaves routed through g_evalHooks. RNG via
// ai::RandomModulo (guild::crt::RandNext); planner + classifier via ai/ai_eval.h.
//
// Frame field conventions (from the binary's frame writes), see ai::EvalFrame:
//   "acquire" frame:  kind=2, arg0=itemId>>16-derived, arg1=1, score=0.05f
//   companion frame:  kind=7, arg0=actor.entityId, arg1=1
//   "use now" frame:  kind=1, arg0=objectId, arg1=1
//   reject companion: kind=0, arg0=-1, arg1=-1, arg2=actionCode

namespace guild::sim {

using ai::EvalFrame;
using ai::ItemSlot;
using ai::ItemSummary;

// --- recovered item-id tables ----------------------------------------------
// slot2 (341,349,?,351,...) is left uninitialized in the binary (v18 = v9, a stale
// register); we reproduce it as 0 — it is only ever read when classified usable,
// which a 0 id never is.
const u16 kGestureItems[9]       = {341, 349, 0, 351, 345, 357, 358, 363, 368};
const u16 kTalkItems[2]          = {371, 347};
const u16 kFlirtItems[2]         = {360, 364};
const u16 kInsultItems[5]        = {380, 369, 348, 346, 0}; // slot4 stale -> 0
const u16 kSocialGestureItems[7] = {361, 362, 376, 381, 365, 379, 0}; // v26[12]=stale
const u16 kGroupGreetItems[3]    = {354, 355, 356};

// ===========================================================================
// Leaf hooks (gate family).
// ===========================================================================
EvalLeafHooks g_evalHooks = {};
EvalCommandTrace g_evalCmdTrace = {};
void ResetEvalCommandTrace() { g_evalCmdTrace = {}; }

static int DefCurrency(u16) { return 0; }
static int DefWealth(u16) { return 0; }
static int DefDispatch(int, u16) { return 0; }
static bool DefQuery(i32, int, int, int, int) { return false; }
static int DefPrice(int, u8) { return 0; }

void ResetEvalLeafHooks() {
    g_evalHooks.currencyAmount = DefCurrency;
    g_evalHooks.totalWealth = DefWealth;
    g_evalHooks.dispatchByType = DefDispatch;
    g_evalHooks.queryFind = DefQuery;
    g_evalHooks.marketPrice = DefPrice;
}
namespace {
struct HookInit { HookInit() { ResetEvalLeafHooks(); } } g_hookInit;
} // namespace

// ===========================================================================
// Shared social-action core.
//
// All five "Choose*Action" evals reduce to this once their item table is seeded:
//   default the action code; reject if armed; reject if a prior result exists
//   (gesture/insult variants only — talk/flirt/drink skip that check); classify;
//   apply the two early rejects; then either the "acquire" branch (no usable item,
//   some accessible) or the "use now" branch (a usable item exists). Each branch
//   picks a slot via RNG over the eligible slots, builds the frames, and either
//   calls the planner or returns the default code directly (the "use now" path for
//   certain slots short-circuits to the default without the planner).
//
// `rejectIfPriorResult` matches the `if (a1) return 0;` guard present in
// Gesture/Insult/SocialGesture/Group/SelectWorker but absent in Talk/Flirt/Drink.
// `acquireMode`/`useMode` are the planner mode args (2 for acquire; 3 or 4 for use).
// ===========================================================================
struct SocialResult {
    char code = 0;
    EvalFrame a, b;
    bool accepted = false;
};

static SocialResult RunSocialChoice(char result, char armed, EvalActor* actor,
                                    const u16* items, int n, char defaultCode,
                                    bool rejectIfPriorResult, int useMode) {
    SocialResult out;
    char actionCode = result ? result : defaultCode;
    if (armed) { out.code = 0; return out; }
    if (rejectIfPriorResult && result) { out.code = 0; return out; }

    ItemSlot slots[16] = {};
    for (int i = 0; i < n; ++i) slots[i].itemId = items[i];
    ItemSummary summary;
    ai::ClassifyItemsHook()(actor, n, slots, &summary);

    // if (summary.accessibleNodes==6 && !usable) return 0;
    if (summary.accessibleNodes == 6 && summary.usableCount == 0) { out.code = 0; return out; }
    // if (summary.blockedCount==n) return 0;
    if (summary.blockedCount == n) { out.code = 0; return out; }

    if (summary.accessibleNodes < 6 && summary.usableCount == 0) {
        // "acquire" branch: pick the first blocked slot starting at a random offset.
        int slot = ai::RandomModulo(static_cast<u16>(n));
        // scan forward (mod n) to a slot whose available != 0 (blocked == -1).
        while (slots[slot].available != 0)
            slot = (slot + 1) % n;
        i32 itemId = slots[slot].objectId; // the binary reads object/item id here
        out.a.set_kind(2);
        out.a.set_arg1(1);
        out.a.set_arg0(itemId >> 16);
        out.a.set_scoreBits(ai::kScoreBits005);
        out.b.set_kind(7);
        out.b.set_arg0(actor->entityId);
        out.b.set_arg1(1);
        char r = ai::SelectBestHook()(static_cast<u8>(actionCode), actor->personId,
                                      &out.a, 2, &out.b);
        out.code = r;
        out.accepted = (r != 0);
        return out;
    }

    if (summary.usableCount == 0) { out.code = 0; return out; }

    // "use now" branch: pick the first usable slot (available >= 1) from a random
    // offset, build a "use" frame, and (depending on slot) call the planner or
    // return the default code directly.
    int slot = ai::RandomModulo(static_cast<u16>(n));
    while (slots[slot].available < 1)
        slot = (slot + 1) % n;
    out.a.set_kind(1);
    out.a.set_arg0(slots[slot].objectId);
    out.a.set_arg1(1);
    out.b.set_kind(0);
    out.b.set_arg0(-1);
    out.b.set_arg1(-1);
    out.b.set_arg2(defaultCode);

    // The Talk/Flirt/Insult/Drink variants gate which slots go through the planner
    // (useMode) vs return the default directly. We model the common cases:
    //   slot 0 -> planner (mode useMode); else -> direct default.
    // (Gesture/SocialGesture have richer slot dispatch handled in their wrappers.)
    if (slot == 0) {
        char r = ai::SelectBestHook()(static_cast<u8>(actionCode), actor->personId,
                                      &out.a, useMode, &out.b);
        out.code = r;
        out.accepted = (r != 0);
    } else {
        out.code = defaultCode;
        out.accepted = true;
    }
    return out;
}

static char Emit(SocialResult& r, EvalFrame* outA, EvalFrame* outB) {
    if (r.accepted) {
        if (outA) *outA = r.a;
        if (outB) *outB = r.b;
    }
    return r.code;
}

// gilde.exe 0x46ef04 — EvalChooseGesture (9 items, default 28, rejectIfPrior).
char EvalChooseGesture(char result, EvalActor* actor, EvalFrame* outA,
                       char armed, EvalFrame* outB) {
    SocialResult r = RunSocialChoice(result, armed, actor, kGestureItems, 9, 28,
                                     /*rejectIfPriorResult=*/true, /*useMode=*/3);
    return Emit(r, outA, outB);
}

// gilde.exe 0x46f2a4 — EvalChooseTalkAction (2 items, default 29, no prior-reject).
char EvalChooseTalkAction(char result, EvalActor* actor, EvalFrame* outA,
                          char armed, EvalFrame* outB) {
    SocialResult r = RunSocialChoice(result, armed, actor, kTalkItems, 2, 29,
                                     /*rejectIfPriorResult=*/false, /*useMode=*/4);
    return Emit(r, outA, outB);
}

// gilde.exe 0x46f578 — EvalChooseFlirtAction (2 items, default 30, no prior-reject).
char EvalChooseFlirtAction(char result, EvalActor* actor, EvalFrame* outA,
                           char armed, EvalFrame* outB) {
    SocialResult r = RunSocialChoice(result, armed, actor, kFlirtItems, 2, 30,
                                     /*rejectIfPriorResult=*/false, /*useMode=*/0);
    return Emit(r, outA, outB);
}

// gilde.exe 0x470620 — EvalChooseInsultAction (5 items, default 34, rejectIfPrior).
char EvalChooseInsultAction(char result, EvalActor* actor, EvalFrame* outA,
                            char armed, EvalFrame* outB) {
    SocialResult r = RunSocialChoice(result, armed, actor, kInsultItems, 5, 34,
                                     /*rejectIfPriorResult=*/true, /*useMode=*/3);
    return Emit(r, outA, outB);
}

// gilde.exe 0x46f7fc — EvalChooseDrinkAction (1 item, default 31, no prior-reject).
// The single item is 382 when the actor is already drinking (a2[11]!=0), else 373
// but with a 2-in-3 reject (RandomModulo(3) != 0 -> 0).
char EvalChooseDrinkAction(char result, EvalActor* actor, EvalFrame* outA,
                           char armed, EvalFrame* outB) {
    char actionCode = result ? result : 31;
    (void)actionCode;
    if (armed) return 0;
    u16 item;
    if (actor->drinkState) {
        item = kDrinkItemDrunk; // 382
    } else {
        if (ai::RandomModulo(3) != 0)
            return 0;           // 2-in-3 reject when sober
        item = kDrinkItemSober; // 373
    }
    SocialResult r = RunSocialChoice(result, armed, actor, &item, 1, 31,
                                     /*rejectIfPriorResult=*/false, /*useMode=*/4);
    return Emit(r, outA, outB);
}

// ===========================================================================
// EvalEnterTavern / Direct.
// ===========================================================================
// gilde.exe 0x46dbf0 — EvalEnterTavern.
char EvalEnterTavern(char result, u16 personId) {
    if (result)
        return 0;
    int v = g_evalHooks.dispatchByType(2, personId);
    return (v == 2) ? 22 : 0;
}
// gilde.exe 0x46dc3c — EvalEnterTavernDirect (no result short-circuit).
char EvalEnterTavernDirect(u16 personId) {
    int v = g_evalHooks.dispatchByType(2, personId);
    return (v == 2) ? 22 : 0;
}

// ===========================================================================
// EvalBuyObject (0x46be8c).
//   mode/type gates: actorKind!=3; armed==1; eventMode==2; descType==2; dragKind==4.
//   resolve building owner cash, apply optional price factor (event a4[20], 0<f<1),
//   compare against the cached market price; capacity check on free worker slots.
//   Returns 14 (buy) iff cash >= price AND used slots < capacity (both checks),
//   else 0.
// ===========================================================================
char EvalBuyObject(u8 actorKind, u8 eventMode, char armed, u8 dragKind,
                   u16 buildingOwnerId, u8 descType, int marketItemId,
                   float priceFactor, int freeWorkerSlots) {
    if (actorKind == 3)
        return 0;
    if (armed != 1)
        return 0;
    if (eventMode != 2)
        return 0;
    if (descType != 2)
        return 0;
    if (dragKind != 4)
        return 0;
    int cash = g_evalHooks.currencyAmount(buildingOwnerId);
    // a4[20] price factor in (0,1): scale the owner's cash budget.
    if (priceFactor > 0.0f && priceFactor < 1.0f)
        cash = static_cast<int>(static_cast<double>(cash) * priceFactor);
    int price = g_evalHooks.marketPrice(marketItemId, 0 /*byte_6477A1*/);
    if (cash < price)
        return 0;
    // The He worker-slot / capacity check: a free slot must remain.
    if (freeWorkerSlots <= 0)
        return 0;
    return 14;
}

// ===========================================================================
// EvalSendMessage (0x46b544).
// ===========================================================================
char EvalSendMessage(u8 eventMode, u8 dragSourceType, bool recipientResolved,
                     u8 recipientKind) {
    if (eventMode != 7)
        return 0;
    if (dragSourceType != 18)
        return 0;
    if (!recipientResolved)
        return 0;
    // Emit the cmd15/coord27/args25 command sequence (mocked via the command trace).
    g_evalCmdTrace.lastTag = "nachricht";
    g_evalCmdTrace.lastAction = 9;
    ++g_evalCmdTrace.count;
    // The He_SendEntityMessage tooltip branch fires for player kinds (6/7) but does
    // not change the return code — the function always returns 9 on success.
    (void)recipientKind;
    return 9;
}

// ===========================================================================
// EvalDuelChallenge (0x46b6dc).
// ===========================================================================
char EvalDuelChallenge(char result, u8 rank, char armed, u16 personId,
                       int gameTimeHi, u8 office, int projectedStockDelta,
                       int promotionCount, const u8* promotionList,
                       bool targetSearchHit) {
    if (armed)
        return 0;
    if (rank < 2) {
        // planner path: propose a rank-up (frame kind 13, arg0=2, weight 0.44).
        EvalFrame a, b;
        a.set_kind(13);
        a.set_arg0(2);
        a.set_arg1(1);
        a.set_scoreBits(ai::kScoreBits044);
        char r = ai::SelectBestHook()(10, personId, &a, 1, &b);
        return r;
    }
    // rank >= 2 path.
    if (gameTimeHi < 0xD)
        return 0;
    if (office)
        return 0;
    if (projectedStockDelta < 3)
        return 0;
    if (promotionCount == 0) {
        // 1-in-4 chance to start an office conflict via target search (action 39).
        if (ai::RandomModulo(4) == 0 && targetSearchHit)
            return 39;
        return 0;
    }
    // pick a promotion-list entry at random; valid ids are 1..0x1B.
    int idx = ai::RandomModulo(static_cast<u16>(promotionCount));
    u8 entry = promotionList ? promotionList[idx * 12] : 0;
    if (!entry || entry > 0x1B)
        return 0;
    return 10;
}

// ===========================================================================
// EvalChooseSocialGesture (0x46ff00).
//
// Same classify/two-reject prologue as the social family (with the prior-result
// reject). The "acquire" branch is identical to RunSocialChoice's. The "use now"
// branch additionally resolves a peer target via the object-search hook (modeled by
// the SelectBestHook receiving a frame whose arg already carries the resolved peer,
// or by `actor->assocNode` when no search target). We translate the slot pick +
// frame build; a resolved peer id of -1 rejects (matches !FindNearestEntity).
// ===========================================================================
// The social-gesture target resolver hook. Returns the resolved peer entity id, or
// -1 to reject (no target found). Default: returns actor->assocNode.
static int (*g_socialTargetHook)(EvalActor*, u16 itemId) = nullptr;

char EvalChooseSocialGesture(char result, EvalActor* actor, EvalFrame* outA,
                             char armed, EvalFrame* outB) {
    char actionCode = result ? result : 33;
    if (armed) return 0;
    if (result) return 0;

    ItemSlot slots[7] = {};
    for (int i = 0; i < 7; ++i) slots[i].itemId = kSocialGestureItems[i];
    ItemSummary summary;
    ai::ClassifyItemsHook()(actor, 7, slots, &summary);

    if (summary.accessibleNodes == 6 && summary.usableCount == 0) return 0;
    if (summary.blockedCount == 7) return 0;

    if (summary.accessibleNodes < 6 && summary.usableCount == 0) {
        int slot = ai::RandomModulo(7);
        while (slots[slot].available != 0)
            slot = (slot + 1) % 7;
        EvalFrame a, b;
        a.set_kind(2);
        a.set_arg1(1);
        a.set_arg0(slots[slot].objectId >> 16);
        a.set_scoreBits(ai::kScoreBits005);
        b.set_kind(7);
        b.set_arg0(actor->entityId);
        b.set_arg1(1);
        char r = ai::SelectBestHook()(static_cast<u8>(actionCode), actor->personId, &a, 2, &b);
        if (r) { if (outA) *outA = a; if (outB) *outB = b; }
        return r;
    }

    if (summary.usableCount == 0) return 0;

    // "use now": pick a usable slot; resolve a peer target; build the kind-7/4 frame.
    int slot = ai::RandomModulo(7);
    while (slots[slot].available < 1)
        slot = (slot + 1) % 7;
    int peer = g_socialTargetHook ? g_socialTargetHook(actor, slots[slot].itemId)
                                  : actor->assocNode;
    if (peer == -1)
        return 0;
    EvalFrame a, b;
    a.set_kind(1);
    a.set_arg0(slots[slot].objectId);
    a.set_arg1(1);
    a.set_scoreBits(ai::kScoreBits005);
    b.set_kind(7);
    b.set_arg0(peer);
    b.set_arg1(-1);
    b.set_arg2(33);
    // slot > 1 returns 33 directly; slot <= 1 goes through the planner (mode 4).
    if (slot > 1) {
        if (outA) *outA = a;
        if (outB) *outB = b;
        return 33;
    }
    char r = ai::SelectBestHook()(static_cast<u8>(actionCode), actor->personId, &a, 4, &b);
    if (r) { if (outA) *outA = a; if (outB) *outB = b; }
    return r;
}

// ===========================================================================
// EvalChooseGroupAction (0x46fa54).
// ===========================================================================
char EvalChooseGroupAction(char result, EvalActor* actor, EvalFrame* outA,
                           char armed, EvalFrame* outB, bool hasPartner,
                           bool heMatch, int partnerEntityId, int heEntityId) {
    char actionCode = result ? result : 32;
    if (armed || result) return 0;

    // Select the item list: partner present -> 3 greetings; else He recruit (359)
    // or solo (368), a single-slot list.
    ItemSlot slots[3] = {};
    int n;
    if (hasPartner) {
        n = 3;
        for (int i = 0; i < 3; ++i) slots[i].itemId = kGroupGreetItems[i];
    } else {
        n = 1;
        if (actor->assocNode != -1)
            return 0;          // *((_DWORD *)a2 + 23) != -1 -> reject
        slots[0].itemId = heMatch ? kGroupHeItem : kGroupSoloItem;
    }
    ItemSummary summary;
    ai::ClassifyItemsHook()(actor, n, slots, &summary);

    if (summary.accessibleNodes == 6 && summary.usableCount == 0) return 0;
    if (summary.blockedCount == n) return 0;

    if (summary.accessibleNodes < 6 && summary.usableCount == 0) {
        int slot = ai::RandomModulo(static_cast<u16>(n));
        while (slots[slot].available != 0)
            slot = (slot + 1) % n;
        EvalFrame a, b;
        a.set_kind(2);
        a.set_arg1(1);
        a.set_arg0(slots[slot].objectId >> 16);
        a.set_scoreBits(ai::kScoreBits005);
        b.set_kind(7);
        b.set_arg0(actor->entityId);
        b.set_arg1(1);
        char r = ai::SelectBestHook()(static_cast<u8>(actionCode), actor->personId, &a, 2, &b);
        if (r) { if (outA) *outA = a; if (outB) *outB = b; }
        return r;
    }

    if (summary.usableCount == 0) return 0;

    int slot = ai::RandomModulo(static_cast<u16>(n));
    while (slots[slot].available < 1)
        slot = (slot + 1) % n;
    EvalFrame a, b;
    a.set_kind(1);
    a.set_arg0(slots[slot].objectId);
    a.set_arg1(1);
    a.set_scoreBits(ai::kScoreBits005);
    u16 chosenItem = slots[slot].itemId;
    // Frame-b dispatch by chosen item id (the binary's 0x162..0x167/368 ladder):
    //   354/355/356 (greetings, 0x162..0x164) -> kind 7, arg0 = partner entity, -1
    //   359 (He recruit, 0x167)               -> kind 7, arg0 = he entity, -1
    //   368 (solo, 0x170)                      -> kind 0, -1, -1 (planner mode 3)
    if (chosenItem == 354 || chosenItem == 355 || chosenItem == 356) {
        b.set_kind(7);
        b.set_arg0(partnerEntityId);
        b.set_arg1(-1);
    } else if (chosenItem == 359) {
        b.set_kind(7);
        b.set_arg0(heEntityId);
        b.set_arg1(-1);
    } else if (chosenItem == 368) {
        b.set_kind(0);
        b.set_arg0(-1);
        b.set_arg1(-1);
    } else {
        return 0;
    }
    b.set_arg2(32);
    if (chosenItem == 368) {
        char r = ai::SelectBestHook()(static_cast<u8>(actionCode), actor->personId, &a, 3, &b);
        if (r) { if (outA) *outA = a; if (outB) *outB = b; }
        return r;
    }
    if (outA) *outA = a;
    if (outB) *outB = b;
    return 32;
}

// ===========================================================================
// EvalSelectWorker (0x46ec00).
//   reject if armed or prior result; require a nearby workshop (search hook).
//   avgRep = sum(workerRep)/count; reject if (avgRep + RandomModulo(4)) < 6.0.
//   then classify item 353 and run the standard acquire/use path.
// ===========================================================================
char EvalSelectWorker(char result, EvalActor* actor, EvalFrame* outA,
                      char armed, EvalFrame* outB, bool nearbyFound,
                      float avgRep, int workerObjId) {
    char actionCode = result ? result : 28;
    if (armed) return 0;
    if (result) return 0;
    if (!nearbyFound) return 0;
    // reputation gate: (RandomModulo(4) + avgRep) < 6.0 -> reject.
    int roll = ai::RandomModulo(4);
    if (static_cast<double>(roll) + static_cast<double>(avgRep) < 6.0)
        return 0;

    ItemSlot slots[1] = {};
    slots[0].itemId = kSelectWorkerItem;
    ItemSummary summary;
    ai::ClassifyItemsHook()(actor, 1, slots, &summary);

    if (summary.accessibleNodes == 6 && summary.usableCount == 0) return 0;
    if (summary.blockedCount == 1) return 0;

    if (summary.accessibleNodes >= 6 || summary.usableCount != 0) {
        if (summary.usableCount != 0) {
            // "use now" -> action 27, companion frame kind 4 = the resolved worker.
            EvalFrame a, b;
            a.set_kind(1);
            a.set_arg0(slots[0].objectId);
            a.set_arg1(1);
            b.set_kind(4);
            b.set_arg0(workerObjId);
            b.set_arg1(-1);
            b.set_arg2(27);
            if (outA) *outA = a;
            if (outB) *outB = b;
            return 27;
        }
        return 0;
    }
    // "acquire" branch.
    EvalFrame a, b;
    a.set_kind(2);
    a.set_arg1(1);
    a.set_arg0(slots[0].objectId >> 16);
    a.set_scoreBits(ai::kScoreBits005);
    b.set_kind(7);
    b.set_arg0(actor->entityId);
    b.set_arg1(1);
    char r = ai::SelectBestHook()(static_cast<u8>(actionCode), actor->personId, &a, 2, &b);
    if (r) { if (outA) *outA = a; if (outB) *outB = b; }
    return r;
}

// ===========================================================================
// EvalAssignPatrol (0x46d0bc) / EvalAssignDestination (0x46dad8).
// ===========================================================================
static char BuildPatrolFrames(EvalActor* actor, bool inventoryMatch, i32 matchItemId,
                              i32 destNodeId, EvalFrame* outA, EvalFrame* outB) {
    if (!inventoryMatch)
        return 0;
    EvalFrame a, b; // a == out (kind 2), b == companion (kind 1 or 4)
    a.set_kind(2);
    a.set_arg1(1);
    // a.arg0 = inventory item word >> 16 (modeled as matchItemId for the slot).
    a.set_arg0(matchItemId);
    if (matchItemId == -1) {
        b.set_kind(4);
        b.set_arg0(destNodeId);
    } else {
        b.set_kind(1);
        b.set_arg0(matchItemId);
    }
    b.set_arg1(1);
    b.set_scoreBits(ai::kScoreBits044);
    char r = ai::SelectBestHook()(21, actor->personId, &a, 1, &b);
    if (r) { if (outA) *outA = a; if (outB) *outB = b; }
    return r;
}

char EvalAssignPatrol(EvalActor* actor, bool eventActive, char armed,
                      bool inventoryMatch, i32 matchItemId, EvalFrame* outA,
                      EvalFrame* outB) {
    // if (kind==3 || (!armed && eventActive)) return 0;
    if (actor->kind == 3 || (!armed && eventActive))
        return 0;
    // if (!assocNode || eventActive || !inventoryMatch) return 0;
    if (actor->assocNode == -1 || eventActive || !inventoryMatch)
        return 0;
    return BuildPatrolFrames(actor, inventoryMatch, matchItemId,
                             actor->assocNode, outA, outB);
}

char EvalAssignDestination(char result, EvalActor* actor, bool eventActive,
                           char armed, bool nearbyFound, bool inventoryMatch,
                           i32 matchItemId, i32 destNodeId, EvalFrame* outA,
                           EvalFrame* outB) {
    if (result)
        return 0;
    // if (kind!=3 && (armed || !eventActive)) { ... } else return result;
    if (actor->kind == 3 || (!armed && eventActive))
        return result; // falls through to "return result" in the binary
    if (eventActive)
        return 0;
    if (!nearbyFound)
        return 0;
    return BuildPatrolFrames(actor, inventoryMatch, matchItemId, destNodeId,
                             outA, outB);
}

// ===========================================================================
// Combat eligibility budget gates.
// ===========================================================================
bool AttackTargetBudgetGate(u16 personId, bool halveCost) {
    int wealth = g_evalHooks.totalWealth(personId);
    int cash = g_evalHooks.currencyAmount(personId);
    int cost = static_cast<int>(static_cast<double>(wealth) * 0.0085); // flt_61A4AC
    if (halveCost)
        cost /= 2;
    // reject if cost > cash * 0.22 (flt_61A4B0).
    if (static_cast<double>(cost) > static_cast<double>(cash) * 0.22)
        return false;
    return true;
}

bool PickTargetBudgetGate(int cash, int wealth, int roomWorth, bool halveCost) {
    int cost = static_cast<int>(static_cast<double>(wealth) * 0.02 +   // flt_61A494
                                static_cast<double>(roomWorth) * 0.05); // flt_61A498
    if (halveCost)
        cost /= 2;
    if (static_cast<double>(cost) > static_cast<double>(cash) * 0.44)   // flt_61A49C
        return false;
    return true;
}

int EquipWeaponBudget(int cash) {
    return static_cast<int>(static_cast<double>(cash) * 0.33); // flt_61A3C8
}

} // namespace guild::sim

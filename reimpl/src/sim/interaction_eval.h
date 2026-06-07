#pragma once
// gilde.exe — the large score-table-driven social/behavior AiMethod Eval handlers
// (the ones the interaction agent deferred). namespace guild::sim.
//
// These are the VIBE_Interaction_Eval* functions that drive a person's autonomous
// social and combat behavior. Each is a desirability evaluator: it inspects the
// actor + (resolved) target state, builds one or two 24-byte action-descriptor
// frames, runs the behavior planner (VIBE_AiMethod_SelectBestRecursive, modeled as
// a hook in ai/ai_eval.h), and returns the chosen interaction action code (0 ==
// reject). The common __userpurge ABI is (al = result-so-far, edx = actor person*,
// ecx = primary out-frame, bl = armed/phase, +stack secondary out-frame).
//
// The "social-action" family (Gesture/Talk/Flirt/Drink/Insult/SocialGesture/Group/
// SelectWorker) shares one structure: seed a per-action item-id list (the recovered
// gesture/talk/... string-id TABLES), classify which the actor can use
// (ClassifyAccessibleItems hook), pick a slot (office-rank-biased or RNG), build the
// frames, and call the planner. Determinism-critical RNG via ai::RandomModulo.
//
// The eligibility/range/cash-gate family (PickTarget/AttackTarget/EnterBuilding/
// BuyObject/GiveGift/EquipWeapon/DuelChallenge/SendMessage) is heavier: it gates on
// wealth, distance, relation and law records. We translate the RULE control flow
// 1:1; the render/object-search/coord/relation/He leaves are forward-declared and
// routed through mockable hooks (see interaction_eval_hooks below); the leaves
// themselves are deferred (listed in the module report).
//
// Translated Eval functions (absolute addresses, imagebase 0x400000):
//   0x46ef04 VIBE_Interaction_EvalChooseGesture        -> EvalChooseGesture
//   0x46f2a4 VIBE_Interaction_EvalChooseTalkAction     -> EvalChooseTalkAction
//   0x46f578 VIBE_Interaction_EvalChooseFlirtAction    -> EvalChooseFlirtAction
//   0x46f7fc VIBE_Interaction_EvalChooseDrinkAction    -> EvalChooseDrinkAction
//   0x470620 VIBE_Interaction_EvalChooseInsultAction   -> EvalChooseInsultAction
//   0x46ff00 VIBE_Interaction_EvalChooseSocialGesture  -> EvalChooseSocialGesture
//   0x46fa54 VIBE_Interaction_EvalChooseGroupAction    -> EvalChooseGroupAction
//   0x46ec00 VIBE_Interaction_EvalSelectWorker         -> EvalSelectWorker
//   0x46d0bc VIBE_Interaction_EvalAssignPatrol         -> EvalAssignPatrol
//   0x46dad8 VIBE_Interaction_EvalAssignDestination    -> EvalAssignDestination
//   0x46dbf0 VIBE_Interaction_EvalEnterTavern          -> EvalEnterTavern
//   0x46dc3c VIBE_Interaction_EvalEnterTavernDirect    -> EvalEnterTavernDirect
//   0x46be8c VIBE_Interaction_EvalBuyObject            -> EvalBuyObject
//   0x46b544 VIBE_Interaction_EvalSendMessage          -> EvalSendMessage
//   0x46b6dc VIBE_Interaction_EvalDuelChallenge        -> EvalDuelChallenge
//   0x46af90 VIBE_Interaction_EvalEquipWeapon          -> EvalEquipWeapon (gate slice)
//   0x46e8a0 helpers from interaction_handlers reused.
#include "guild/common/types.h"
#include "ai/ai_eval.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Actor view (the `a2` person record the Eval fns index by word/dword). The
// originals address it as unsigned __int16*; we model the fields the social and
// gate evals read. The person record is the 536-byte master record (see recon).
//   word[0]   (*a2)           : person id (planner key)
//   dword[1]  (a2+4)          : entity id (frame arg)
//   byte[2]   (a2+2)          : record kind (3 == "in combat/jail" branch gate)
//   dword[11] (a2+44)         : drink-state flag (Drink uses a2[11])
//   dword[23] (a2+92)         : associated scene-node person id (-1 == none)
//   byte[360] (a2+360)        : office/role byte (gesture office-rank gate)
//   word[242] (a2[242])       : flag word (bit 2 gate in combat branch)
// ---------------------------------------------------------------------------
struct EvalActor {
    u16  personId   = 0;   // *a2
    i32  entityId   = 0;   // a2+4   (dword index 1)
    u8   kind       = 0;   // a2+2
    i32  drinkState = 0;   // a2+44  (dword index 11)
    i32  assocNode  = -1;  // a2+92  (dword index 23)
    u8   office     = 0;   // a2+360
    u16  flagWord242 = 0;  // a2[242]
};

// ---------------------------------------------------------------------------
// Recovered per-action item-id tables (the gesture/talk/flirt/... string-id lists
// each Eval seeds before classifying). Verbatim from the binary's v16/v18/v26
// initializers. The id is a "RECSTAT" gesture/object string id.
// ---------------------------------------------------------------------------
// EvalChooseGesture (0x46ef04): 9 slots, default action 28.
extern const u16 kGestureItems[9];   // {341,349,351,345,357,358,363,368,?}
// EvalChooseTalkAction (0x46f2a4): 2 slots, default action 29.
extern const u16 kTalkItems[2];      // {371,347}
// EvalChooseFlirtAction (0x46f578): 2 slots, default action 30.
extern const u16 kFlirtItems[2];     // {360,364}
// EvalChooseDrinkAction (0x46f7fc): 1 slot, default action 31. itemId 382 (drunk)
// or 373 (sober, 2-in-3 reject) chosen at runtime.
constexpr u16 kDrinkItemDrunk = 382;
constexpr u16 kDrinkItemSober = 373;
// EvalChooseInsultAction (0x470620): 5 slots, default action 34.
extern const u16 kInsultItems[5];    // {380,369,348,346,?}
// EvalChooseSocialGesture (0x46ff00): 7 slots, default action 33.
extern const u16 kSocialGestureItems[7]; // {361,362,376,381,365,379,?}
// EvalChooseGroupAction (0x46fa54): item list depends on context (3 or 1 slots).
extern const u16 kGroupGreetItems[3]; // {354,355,356}
constexpr u16 kGroupHeItem = 359;
constexpr u16 kGroupSoloItem = 368;
// EvalSelectWorker (0x46ec00): 1 slot, item 353.
constexpr u16 kSelectWorkerItem = 353;

// ---------------------------------------------------------------------------
// Mockable leaf hooks for the gate-family evals (range/cash/relation/law).
// Default values let the gate RULE control flow run deterministically in tests.
// ---------------------------------------------------------------------------
struct EvalLeafHooks {
    // VIBE_Person_GetCurrencyAmount @0x5915b8 — cash held by `personId`.
    int (*currencyAmount)(u16 personId) = nullptr;
    // VIBE_Person_ComputeTotalWealth @0x591f7c — total wealth of `personId`.
    int (*totalWealth)(u16 personId) = nullptr;
    // VIBE_DebugCmd_DispatchByType @0x5711ec — interaction-mode probe. The gate
    // evals read its return (2/4 == accessible) and bit 2 (halve-cost flag).
    int (*dispatchByType)(int type, u16 personId) = nullptr;
    // VIBE_GameObject_QueryFind @0x5857fc — true == a matching scene node exists.
    bool (*queryFind)(i32 nodeId, int a, int b, int c, int d) = nullptr;
    // VIBE_Building_LookupCachedMarketPrice @0x58f6b8 — cached price for an item.
    int (*marketPrice)(int itemId, u8 currencyIndex) = nullptr;
};
extern EvalLeafHooks g_evalHooks;
void ResetEvalLeafHooks();

// Command emission trace (the AI->lockstep-command boundary). The gate evals that
// emit commands (e.g. EvalSendMessage's cmd15/coord27/args25 sequence) record the
// tag + action code here so tests can assert what would be sent. Mutations go
// through this mock hook, never direct state writes.
struct EvalCommandTrace {
    const char* lastTag = nullptr;
    int lastAction = 0;
    int count = 0;
};
extern EvalCommandTrace g_evalCmdTrace;
void ResetEvalCommandTrace();

// ===========================================================================
// Social-action family. Each: (result, actor, outA, armed, outB) -> action code.
// `outA`/`outB` receive the chosen primary/secondary frames when accepted.
// ===========================================================================
char EvalChooseGesture(char result, EvalActor* actor, ai::EvalFrame* outA,
                       char armed, ai::EvalFrame* outB);
char EvalChooseTalkAction(char result, EvalActor* actor, ai::EvalFrame* outA,
                          char armed, ai::EvalFrame* outB);
char EvalChooseFlirtAction(char result, EvalActor* actor, ai::EvalFrame* outA,
                           char armed, ai::EvalFrame* outB);
char EvalChooseDrinkAction(char result, EvalActor* actor, ai::EvalFrame* outA,
                           char armed, ai::EvalFrame* outB);
char EvalChooseInsultAction(char result, EvalActor* actor, ai::EvalFrame* outA,
                            char armed, ai::EvalFrame* outB);

// ===========================================================================
// EvalEnterTavern (0x46dbf0) / Direct (0x46dc3c): probe interaction-mode 2 on the
// actor; return 22 iff DispatchByType(2)==2, else 0. (Direct ignores the result
// short-circuit.)
// ===========================================================================
char EvalEnterTavern(char result, u16 personId);
char EvalEnterTavernDirect(u16 personId);

// ===========================================================================
// EvalBuyObject (0x46be8c): mode/type gate then cash-vs-market-price gate over a
// building-owner's cash + a worker-slot capacity check. Returns 14 (buy) or 0.
// `eventMode`==*a2, `descType`==dword_13CE27C[type] role byte, `dragKind`==*a4,
// `priceFactor` == event price multiplier (a4[20] float). Resolved building +
// owner cash come through the hooks. The He worker-slot count is `usedSlots`.
// ===========================================================================
char EvalBuyObject(u8 actorKind, u8 eventMode, char armed, u8 dragKind,
                   u16 buildingOwnerId, u8 descType, int marketItemId,
                   float priceFactor, int freeWorkerSlots);

// ===========================================================================
// EvalSendMessage (0x46b544): requires event mode 7 + drag-source type 18 + a
// resolvable recipient person. Emits the cmd15/coord27/args25 command sequence
// (modeled via the command hook in interaction_handlers) and returns 9. Else 0.
// `recipientResolved` == (Person_FindRecordById(a4_targetId) != 0).
// ===========================================================================
char EvalSendMessage(u8 eventMode, u8 dragSourceType, bool recipientResolved,
                     u8 recipientKind);

// ===========================================================================
// EvalDuelChallenge (0x46b6dc): two-phase. If actor rank (a2+13) < 2: propose a
// rank-up via the planner (mode 1, action 13) and return its verdict. Else: a
// promotion-list / target-search gate that yields a duel-challenge (10) or an
// office-conflict action (39). Returns the chosen action code or 0.
//   rank<2: planner path. rank>=2: gameTimeHi>=13 && !office(+360) &&
//           projectedStock>=3 then either a promotion entry (10) or, 1-in-4,
//           a DispatchTargetSearch conflict (39).
// ===========================================================================
char EvalDuelChallenge(char result, u8 rank, char armed, u16 personId,
                       int gameTimeHi, u8 office, int projectedStockDelta,
                       int promotionCount, const u8* promotionList,
                       bool targetSearchHit);

// ===========================================================================
// EvalChooseSocialGesture (0x46ff00, 7 items, default 33) and
// EvalChooseGroupAction (0x46fa54, default 32) share the social-action shape but
// resolve a target via object-search after picking a slot. The 7-slot
// social-gesture table is kSocialGestureItems. We translate the slot-selection +
// frame-build RULE; the object-search target resolution is a hook (returns a
// resolved peer id, or -1 for "no target", which rejects).
// ===========================================================================
char EvalChooseSocialGesture(char result, EvalActor* actor, ai::EvalFrame* outA,
                             char armed, ai::EvalFrame* outB);
// EvalChooseGroupAction: `hasPartner` selects the 3-greeting list vs the 1-slot
// He/solo list; `heMatch` picks the He recruit item (359) over the solo item (368).
char EvalChooseGroupAction(char result, EvalActor* actor, ai::EvalFrame* outA,
                           char armed, ai::EvalFrame* outB, bool hasPartner,
                           bool heMatch, int partnerEntityId, int heEntityId);

// ===========================================================================
// EvalSelectWorker (0x46ec00, item 353, default 28). Finds a nearby workshop
// (object-search hook), averages its workers' reputations, and only proceeds if
// (avgRep + RandomModulo(4)) >= 6.0. Then the standard item-classify/pick path.
// `nearbyFound`/`avgRep`/`workerObjId` come from the search hook.
// ===========================================================================
char EvalSelectWorker(char result, EvalActor* actor, ai::EvalFrame* outA,
                      char armed, ai::EvalFrame* outB, bool nearbyFound,
                      float avgRep, int workerObjId);

// ===========================================================================
// EvalAssignPatrol (0x46d0bc) / EvalAssignDestination (0x46dad8): inventory-match
// based. Gates: actor.kind!=3; (armed||!eventActive); actor.assocNode set; an
// inventory item matches (CountInventoryMatch hook -> matchItemId, -1 == use node).
// Build the kind-2 acquire frame + a kind-1/4 companion, planner mode 1 (action 21).
// `eventActive` == *event byte; `matchItemId` == matched object id (or -1).
// ===========================================================================
char EvalAssignPatrol(EvalActor* actor, bool eventActive, char armed,
                      bool inventoryMatch, i32 matchItemId, ai::EvalFrame* outA,
                      ai::EvalFrame* outB);
char EvalAssignDestination(char result, EvalActor* actor, bool eventActive,
                           char armed, bool nearbyFound, bool inventoryMatch,
                           i32 matchItemId, i32 destNodeId, ai::EvalFrame* outA,
                           ai::EvalFrame* outB);

// ===========================================================================
// Combat eligibility gate slices. The full PickTarget/AttackTarget bodies are
// deeply entangled with object-search/coord/relation leaves; we translate the
// front gate (the cash/wealth cost-vs-budget check) that decides whether the
// person can even afford the action before any target search. Returns true ==
// "may proceed" (pass the budget gate), false == reject.
// ===========================================================================
// EvalAttackTarget budget gate (0x46e33c front): cost = wealth*0.0085 (via coord);
// halve if DispatchByType bit2; reject if cost > cash*0.22.
bool AttackTargetBudgetGate(u16 personId, bool halveCost);
// EvalPickTarget budget gate (0x46dcf8 tail): cost = wealth*0.02 + roomWorth*0.05;
// halve if bit2; reject if cost > cash*0.44.
bool PickTargetBudgetGate(int cash, int wealth, int roomWorth, bool halveCost);
// EvalEquipWeapon budget gate (0x46af90): cost = cash*0.33 (via coord); used as the
// equip budget. Returns the budget value (>=0).
int EquipWeaponBudget(int cash);

} // namespace guild::sim

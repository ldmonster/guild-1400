#pragma once
// gilde.exe — remaining VIBE_Interaction_* / VIBE_Dialog_* slice (Wave 14).
// namespace guild::sim.
//
// This file finishes the still-untranslated tail of the Interaction/Dialog modules
// that the earlier interaction*.cpp / interaction_eval.cpp / interaction_handlers.cpp
// passes left behind. Two clusters:
//
//   (A) The autonomous-AI building/economy evaluators
//       (EvalEnterBuilding / EvalLeaveBuilding / EvalBuildingAction / EvalGiveGift /
//        FindNearestTarget / RequestSellObject / PerformBroadcastSummon). These are
//       desirability/eligibility evaluators in the same family modeled by
//       interaction_eval.h: they inspect the actor + (resolved) target, gate on
//       wealth/relation/slot-capacity, build action-descriptor frames and either emit
//       a lockstep command or return an interaction action code (0 == reject).
//
//   (B) The interactive confirm-dialog / tavern dispatch bodies
//       (Dialog_AttackCommand / TavernStammtischDispatch / TavernDarkCornerDispatch /
//        RobberCampCheckAndShow / RobberCampShowBar / SpionageConfirm). These are
//        UI-orchestration FSMs: gate on the active-char flag, open a player bar, run a
//        message box / panel dispatcher, optionally enqueue a command, tear the bar
//        down. We translate the RULE control flow 1:1; the render/voice/selection
//        leaves are inert hooks.
//
//   (C) The tutorial-panel input FSM (DispatchPanelEvent). Fully deterministic over a
//        panel record; translated verbatim and unit-tested directly.
//
// We follow the house pattern (see interaction_eval.h, CutsceneMiscHooks, ...): the
// control flow is faithful; every cross-module leaf we do not own is routed through
// the installable Interaction4Hooks struct, whose default implementations are inert
// (defined in interaction4.cpp). Tests install their own hooks. The determinism-
// critical RNG leaf is randomModulo (game wires it to util::RandomModulo).
//
// Translated functions (absolute addresses, imagebase 0x400000):
//   0x46b8c8 VIBE_Interaction_EvalGiveGift            -> EvalGiveGift
//   0x46c0c0 VIBE_Interaction_FindNearestTarget       -> FindNearestTarget
//   0x46bcc0 VIBE_Interaction_RequestSellObject       -> RequestSellObject
//   0x46e91c VIBE_Interaction_PerformBroadcastSummon  -> PerformBroadcastSummon
//   0x46c3d0 VIBE_Interaction_EvalEnterBuilding       -> EvalEnterBuilding
//   0x46c9d4 VIBE_Interaction_EvalLeaveBuilding       -> EvalLeaveBuilding
//   0x46d1a8 VIBE_Interaction_EvalBuildingAction      -> EvalBuildingAction
//   0x595b98 VIBE_Interaction_DispatchPanelEvent      -> DispatchPanelEvent
//   0x513168 VIBE_Dialog_AttackCommand                -> Dialog_AttackCommand
//   0x5180e8 VIBE_Dialog_TavernStammtischDispatch     -> Dialog_TavernStammtischDispatch
//   0x518bf0 VIBE_Dialog_TavernDarkCornerDispatch     -> Dialog_TavernDarkCornerDispatch
//   0x5129ec VIBE_Dialog_RobberCampCheckAndShow       -> Dialog_RobberCampCheckAndShow
//   0x512668 VIBE_Dialog_RobberCampShowBar            -> Dialog_RobberCampShowBar
//   0x515e30 VIBE_Dialog_SpionageConfirm              -> Dialog_SpionageConfirm
#include "guild/common/types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Action-descriptor frame (the 24-byte "out frame" the evals write: dword[0]==kind,
// dword[1]==arg/id, dword[2]==count, float[5]==score, ...). The originals address it
// as _DWORD[6]; we model the fields the evals touch.
// ---------------------------------------------------------------------------
struct I4Frame {
    u8    kind  = 0;   // byte[0]
    i32   arg   = 0;   // dword[1] (entity/object/building id)
    i32   count = 0;   // dword[2]
    i32   mode  = 0;   // dword[4] (e.g. 57 currency mode for GiveGift)
    float score = 0.f; // float[5]
};

// ---------------------------------------------------------------------------
// Interaction4Hooks — every unreconstructed cross-module leaf, with inert defaults
// (installed once in interaction4.cpp, restorable via ResetInteraction4Hooks).
// ---------------------------------------------------------------------------
struct Interaction4Hooks {
    // VIBE_Math_RandomModulo @0x58b89c — RandNext()%n (or 0). DETERMINISM-CRITICAL.
    int  (*randomModulo)(u16 n) = nullptr;
    // VIBE_Person_GetCurrencyAmount @0x5915b8 — cash held by `personId`.
    int  (*currencyAmount)(u16 personId, u8 currency) = nullptr;
    // VIBE_Building_LookupCachedMarketPrice @0x58f6b8 — cached price (item hiword).
    double (*marketPrice)(i16 itemHiword, u8 currency) = nullptr;
    // VIBE_AiScore_ScaleByActionType @0x479d84 — score-table lookup for FindNearest.
    int  (*scaleByActionType)(int targetState, int actionType) = nullptr;
    // VIBE_GameObject_QueryFind @0x5857fc — non-zero iff a matching scene node exists.
    bool (*queryFind)(i32 nodeId, int a, int b, int c) = nullptr;
    // VIBE_Building_ComputeSalePrice @0x591480.
    int  (*computeSalePrice)(i32 buildingSlot) = nullptr;
    // VIBE_Building_SumFlaggedSlotsWorth @0x5913e0.
    int  (*sumFlaggedSlotsWorth)(i32 buildingSlot) = nullptr;
    // VIBE_BuildingValue_ComputeRoomWorth @0x59116c.
    int  (*computeRoomWorth)(i32 buildingSlot, int rank) = nullptr;
    // VIBE_AiAction_EvalMeisterTarget @0x47a110 — non-zero == target eligible.
    bool (*evalMeisterTarget)(I4Frame* frameA, u16 personId, I4Frame* frameB, int phase) = nullptr;
    // VIBE_NpcAction_EvaluateCombatTarget @0x4737fc — recurses into the combat planner.
    char (*evaluateCombatTarget)(char code, u16 personId, I4Frame* outA, I4Frame* outB) = nullptr;
    // VIBE_BuildingType_MapActionToCategory @0x58a25c.
    char (*mapActionToCategory)(u8 buildingType) = nullptr;
    // VIBE_Dialog_CheckActiveCharFlag @0x4ad5d4 — non-zero == active char busy (gate).
    bool (*checkActiveCharFlag)() = nullptr;
    // VIBE_Dialog_ShowMessageBox @0x4ad6f0 — non-zero == user confirmed.
    bool (*showMessageBox)(int textId, int kind, char arg) = nullptr;
    // Command-emission boundary: tag + action code recorded (see I4CommandTrace).
    void (*emitCommand)(const char* tag, int action) = nullptr;
    // UI orchestration leaves (PlayerBar / MapView / Selection / voice). Inert.
    void (*playerBarCreate)() = nullptr;
    void (*playerBarDestroy)() = nullptr;
    void (*panelDispatcher)(int kind) = nullptr;
    void (*selectionClearAll)() = nullptr;
    // Tavern stammtisch/dark-corner leaves. Returns true if "leave/browse" branch ran.
    void (*tavernStammtischJoin)() = nullptr;
    void (*tavernStammtischLeave)() = nullptr;
    void (*tavernDarkCornerBuy)() = nullptr;
    void (*tavernDarkCornerBrowse)() = nullptr;
    // VIBE_Amt_RunOfficeOverviewWindow @0x5575c8 (Spionage).
    void (*runOfficeOverviewWindow)() = nullptr;
    u8 currencyByte = 0; // byte_6477A1 — active-player currency index.
};
extern Interaction4Hooks g_i4Hooks;
void ResetInteraction4Hooks();

// Trace of the last UI/command action a Dialog FSM would have driven (so tests can
// assert the orchestration without real UI).
struct I4DialogTrace {
    bool barOpened = false;
    bool barClosed = false;
    int  panelKind = -1;
    bool selectionCleared = false;
    bool messageBoxShown = false;
    int  lastCommandAction = 0;
    const char* lastCommandTag = nullptr;
    int  tavernJoin = 0, tavernLeave = 0, tavernBuy = 0, tavernBrowse = 0;
    int  officeWindow = 0;
};
extern I4DialogTrace g_i4DialogTrace;
void ResetI4DialogTrace();

// ===========================================================================
// (A) AI economy evaluators.
// ===========================================================================

// gilde.exe 0x46b8c8 — EvalGiveGift. Gate: armed==1, eventMode==2, target not a
// shop-owner; resolve the drag target (object/building/person) to a "giver" person,
// compute its cash (optionally scaled by the event price factor), and reject unless
// cash >= the cached gift market price. action 57 path uses the explicit drag amount.
//   Returns 11 (gift accepted), 57 (currency-gift), or 0 (reject).
// `dragKind`==*a4 (1=object,4=building,7=person); `targetResolved` true iff the drag
// resolves; `giverCash` precomputed cash for the giver; `priceFactor` event multiplier
// (0<f<1 scales cash); `giftItemHiword` the gift's item id hiword.
char EvalGiveGift(char actionIn, u8 eventMode, char armed, u8 dragKind,
                  bool targetResolved, int giverCash, float priceFactor,
                  i16 giftItemHiword);

// gilde.exe 0x46c0c0 — FindNearestTarget. Two modes keyed by `frameKind` (the byte
// at the out-frame): kind==0 scans the 256-entry person table for the cheapest
// reachable peer of `wantedTownId`, gates the chosen score against cash*flt_61A438
// (0.0085) and writes a buy frame; kind==4 re-validates an already-picked target's
// score against the budget. Returns 15 (target found/valid) or 0.
// Modeled as: caller supplies the chosen candidate's score & budget; we apply the
// faithful accept/reject + the 50-vs-30 actionType selection.
struct FindTargetResult {
    char code = 0;       // 15 == accept, 0 == reject
    int  chosenActionType = 0; // 50 (near) or 30 (far)
    float score = 0.f;
};
// kind==4 revalidate: score = scaleByActionType(targetState, actorActionType);
//   accept iff score!=0 && budget >= score.
FindTargetResult FindNearestRevalidate(int targetState, int actorActionType, double budget);
// kind==0 search tail: bestDistShifted decides 50 (>=50) vs 30 actionType; budget =
//   cash * flt_61A438 (0.95); accept iff score!=0 && score <= budget.
FindTargetResult FindNearestPick(int bestDistShifted, int targetState, int cash);

// gilde.exe 0x46c12c — the search-loop's initial best-distance threshold seed:
//   v11 = VIBE_Math_RandomModulo(0x24) + 35  (a randomised tie-break window). Pulls
//   one RNG draw via the randomModulo hook. Returns the seed (35..70).
int FindNearestThresholdSeed();

// gilde.exe 0x46bcc0 — RequestSellObject. Resolve the drag target to a slot id;
// require event mode 2; emit the cm_RequestSellObjekt command sequence. action 57
// (currency) vs 11 (plain) differ in whether the resolved entity gate + cmd15 run.
//   Returns 57, 11, or 0. `slotId` the resolved slot (-1 == unresolved -> 0);
//   `eventMode`==*a1; `currencyMode` true iff (a1+16)==57; `entityResolved` gate;
//   `price` the cached market price.
char RequestSellObject(i32 slotId, u8 eventMode, bool currencyMode,
                       bool entityResolved, long long price);

// gilde.exe 0x46e91c — PerformBroadcastSummon. Resolve the actor's guild-hall node;
// require a matching market node; enqueue the summon command + a delta packet, then
// (if actor.kind==5) broadcast a slot-reset to up to byte_63CC1D buildings of type
// 6/7. Returns 25 (summon issued) or 0. `hallResolved`/`marketResolved` gate; we
// count the broadcast targets faithfully via the supplied building-type list.
// `buildingTypes` length `n`; only kinds 6 and 7 (capped at `maxBroadcast`) summon.
int PerformBroadcastSummon(bool hallResolved, bool marketResolved, bool actorIsFive,
                           const u8* buildingTypes, int n, int maxBroadcast,
                           int* outBroadcastCount);

// gilde.exe 0x46c3d0 — EvalEnterBuilding. Worth/cost gate that decides whether the
// actor should walk into one of its buildings to acquire a worker/room. We translate
// the cost-vs-worth accept/reject formula 1:1:
//   accept iff (worth - cost)/worth < (worth - roomWorth)/roomWorth * 0.? + 0.?
// `cost`==cash*flt_61A448; `worth`==handler worth; `roomWorth`==ComputeRoomWorth.
//   factorA == flt_61A44C, biasB == flt_61A450 (recovered below). Returns true ==
//   reject (the binary `return 0`), false == proceed to combat eval.
bool EnterBuildingWorthReject(int worth, int cost, int roomWorth);

// gilde.exe 0x46c9d4 — EvalLeaveBuilding. Symmetric "leave/sell" gate. The accept
// rule: with sale price `sale` and budget `cost`, reject iff (sale-cost)/sale >=
// flt_61A458 (0.? recovered below). Returns true == reject, false == proceed.
bool LeaveBuildingSaleReject(int sale, int cost);

// gilde.exe 0x46d1a8 — EvalBuildingAction. Aggregate building-action evaluator. The
// reusable deterministic kernel: tally the actor's buildings of action-category
// [1..12], summing slot counts (`totalSlots`), the count of such buildings
// (`buildingCount`) and how many are at slot-level 1 (`level1Count`); then the
// "should leave" pre-gate: if the actor occupies a node whose (slots+1 - baseline)/
// totalSlots < flt_61A47C, try EvalLeaveBuilding first.
struct BuildingActionTally {
    int totalSlots = 0;
    int buildingCount = 0;
    int level1Count = 0;
};
// occupiedCategoryDelta == (slots[occupied]+1 - baseline); returns true iff the
// leave pre-gate fires (delta/totalSlots < flt_61A47C).
bool BuildingActionLeavePreGate(int occupiedCategoryDelta, int totalSlots);
// The mid-game rank gate the binary applies when level1Count>0 && profession unset:
//   reject if level1Count > capacity; reject unless gameTimeLo == (entityId & 7).
//   capacity == 2 + (actor.kind==5). Returns true == reject.
bool BuildingActionRankGate(int level1Count, int capacity, i32 gameTimeLo,
                            i32 entityIdLow3);

// ===========================================================================
// (C) Tutorial-panel input FSM. Verbatim translation over the panel record.
// ===========================================================================
// The live panel lives at off_5953F0 ([0]=combinedState, [1]=subState, dword[2]=
// lastTick, dword[3]=eventCount, dword[5]=step ptr, dword[11]=auxFlag, dword[14]=
// dwell, dword[19]=auxId). The step record has byte[8]=mode (2 == "advanced"),
// dword[14]=mode2 (1/3 == dispatch modes), dword[56]=callback, dword[61]=targetId,
// dword[72]=nextState(byte), dword[76]=altTargetId.
struct PanelStep {
    u8  mode = 0;           // step+8
    i32 mode2 = 0;          // step+56*? no — step dword[14]
    i32 callback = 0;       // step+56 (non-zero == custom handler; we just record it)
    i32 targetId = 0;       // step+61 (>>24 compared to event id>>24)
    u8  nextState = 0;      // step+72
    i32 altTargetId = 0;    // step+76 (>>? compared as raw)
    i32 dwellLimit = 0;     // for the mode==!2, code==3 dwell branch
};
struct TutorialPanel {
    bool active = false;    // gated by dword_649CD0
    u8  combinedState = 0;  // panel[0]
    u8  subState = 0;       // panel[1]
    i32 lastTick = 0;       // panel dword[2]
    i32 eventCount = 0;     // panel dword[3]
    PanelStep* step = nullptr; // panel dword[5]
    i32 auxFlag = 0;        // panel dword[11]
    i32 dwellMode = 0;      // panel dword[14]
    i32 auxId = 0;          // panel dword[19]
    bool callbackInvoked = false; // set if a step callback would fire
};
// gilde.exe 0x595b98 — DispatchPanelEvent. `eventIdHi` == the incoming event id >>24
// (the binary's *(int*)&v18[1] >> 24); `auxId` and `flag` are the a2/a4 inputs;
// `nowTick` == dword_62EB38. Mutates the panel in place. Returns the panel (or null
// when inactive / no step).
TutorialPanel* DispatchPanelEvent(TutorialPanel* panel, int eventIdHi, int auxId,
                                  int flag, int nowTick);

// ===========================================================================
// (B) Confirm-dialog / tavern dispatch FSMs (UI orchestration via hooks).
// ===========================================================================
// gilde.exe 0x513168 — Dialog_AttackCommand. Gate on CheckActiveCharFlag; open bar;
// ShowMessageBox(textId); on confirm emit the slot-reset attack command + voice +
// clear selection; always close bar. Returns nothing.
void Dialog_AttackCommand(int textId, char arg);
// gilde.exe 0x5180e8 — TavernStammtischDispatch. If a stammtisch node exists for the
// actor and a handler matches: join if not already seated, else leave. Then refresh
// the HUD. `nodeExists`/`handlerMatch` gates; `alreadySeated` picks leave vs join.
void Dialog_TavernStammtischDispatch(bool nodeExists, bool handlerMatch, bool alreadySeated);
// gilde.exe 0x518bf0 — TavernDarkCornerDispatch. If a dark-corner node exists: if the
// actor is the active player -> browse, else -> buy. Then refresh HUD.
void Dialog_TavernDarkCornerDispatch(bool nodeExists, bool isActivePlayer);
// gilde.exe 0x5129ec — RobberCampCheckAndShow. Gate on CheckActiveCharFlag and a
// secondary `hasCamp` flag; build the panel desc, open bar, run MapView panel
// dispatcher, close bar, clear selection. Returns 0 (busy) or the clear result.
int Dialog_RobberCampCheckAndShow(bool hasCamp);
// gilde.exe 0x512668 — RobberCampShowBar. Unconditional: open bar, run pickpocket
// panel dispatcher, close bar, clear selection.
int Dialog_RobberCampShowBar();
// gilde.exe 0x515e30 — SpionageConfirm. Build the desc + formatted message, run the
// office-overview window, refresh HUD.
void Dialog_SpionageConfirm();

// --- recovered float constants (imagebase 0x400000) ------------------------
extern const float kFindNearestCashFactor;  // flt_61A438 = 0.0085
extern const float kEnterBuildingCashFactor; // flt_61A448
extern const float kEnterBuildingWorthFactorA; // flt_61A44C
extern const float kEnterBuildingWorthBiasB;   // flt_61A450
extern const float kLeaveBuildingSaleThreshold; // flt_61A458
extern const float kBuildingActionLeaveThreshold; // flt_61A47C

} // namespace guild::sim

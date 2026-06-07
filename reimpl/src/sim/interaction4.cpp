#include "sim/interaction4.h"

#include <cstring>

// Faithful 1:1 port of the remaining VIBE_Interaction_* / VIBE_Dialog_* slice.
// Control flow is verbatim from the Hex-Rays pseudocode; cross-module leaves are
// routed through g_i4Hooks (inert defaults below). See interaction4.h for the
// address map and per-function notes.

namespace guild::sim {

// --- recovered float constants (imagebase 0x400000) ------------------------
const float kFindNearestCashFactor      = 0.94999999f;  // flt_61A438
const float kEnterBuildingCashFactor     = 0.66000003f;  // flt_61A448
const float kEnterBuildingWorthFactorA    = -0.5f;        // flt_61A44C
const float kEnterBuildingWorthBiasB      = 0.25f;        // flt_61A450
const float kLeaveBuildingSaleThreshold   = 0.25f;        // flt_61A458
const float kBuildingActionLeaveThreshold = 0.33000001f;  // flt_61A47C

// ===========================================================================
// Hooks + traces.
// ===========================================================================
Interaction4Hooks g_i4Hooks = {};
I4DialogTrace g_i4DialogTrace = {};
void ResetI4DialogTrace() { g_i4DialogTrace = {}; }

namespace {
int  DefRandomModulo(u16) { return 0; }
int  DefCurrency(u16, u8) { return 0; }
double DefPrice(i16, u8) { return 0.0; }
int  DefScale(int, int) { return 0; }
bool DefQuery(i32, int, int, int) { return false; }
int  DefSale(i32) { return 0; }
int  DefSumWorth(i32) { return 0; }
int  DefRoomWorth(i32, int) { return 0; }
bool DefMeister(I4Frame*, u16, I4Frame*, int) { return false; }
char DefCombat(char, u16, I4Frame*, I4Frame*) { return 0; }
char DefMapCat(u8) { return 0; }
bool DefActiveFlag() { return false; }
bool DefShowMsg(int, int, char) { return false; }

void DefEmit(const char* tag, int action) {
    g_i4DialogTrace.lastCommandTag = tag;
    g_i4DialogTrace.lastCommandAction = action;
}
void DefBarCreate() { g_i4DialogTrace.barOpened = true; }
void DefBarDestroy() { g_i4DialogTrace.barClosed = true; }
void DefPanel(int kind) { g_i4DialogTrace.panelKind = kind; }
void DefClearSel() { g_i4DialogTrace.selectionCleared = true; }
void DefStammtischJoin() { ++g_i4DialogTrace.tavernJoin; }
void DefStammtischLeave() { ++g_i4DialogTrace.tavernLeave; }
void DefDarkBuy() { ++g_i4DialogTrace.tavernBuy; }
void DefDarkBrowse() { ++g_i4DialogTrace.tavernBrowse; }
void DefOfficeWindow() { ++g_i4DialogTrace.officeWindow; }
} // namespace

void ResetInteraction4Hooks() {
    g_i4Hooks.randomModulo = DefRandomModulo;
    g_i4Hooks.currencyAmount = DefCurrency;
    g_i4Hooks.marketPrice = DefPrice;
    g_i4Hooks.scaleByActionType = DefScale;
    g_i4Hooks.queryFind = DefQuery;
    g_i4Hooks.computeSalePrice = DefSale;
    g_i4Hooks.sumFlaggedSlotsWorth = DefSumWorth;
    g_i4Hooks.computeRoomWorth = DefRoomWorth;
    g_i4Hooks.evalMeisterTarget = DefMeister;
    g_i4Hooks.evaluateCombatTarget = DefCombat;
    g_i4Hooks.mapActionToCategory = DefMapCat;
    g_i4Hooks.checkActiveCharFlag = DefActiveFlag;
    g_i4Hooks.showMessageBox = DefShowMsg;
    g_i4Hooks.emitCommand = DefEmit;
    g_i4Hooks.playerBarCreate = DefBarCreate;
    g_i4Hooks.playerBarDestroy = DefBarDestroy;
    g_i4Hooks.panelDispatcher = DefPanel;
    g_i4Hooks.selectionClearAll = DefClearSel;
    g_i4Hooks.tavernStammtischJoin = DefStammtischJoin;
    g_i4Hooks.tavernStammtischLeave = DefStammtischLeave;
    g_i4Hooks.tavernDarkCornerBuy = DefDarkBuy;
    g_i4Hooks.tavernDarkCornerBrowse = DefDarkBrowse;
    g_i4Hooks.runOfficeOverviewWindow = DefOfficeWindow;
    g_i4Hooks.currencyByte = 0;
}
namespace {
struct HookInit { HookInit() { ResetInteraction4Hooks(); } } g_i4HookInit;
} // namespace

// ===========================================================================
// (A) AI economy evaluators.
// ===========================================================================

// gilde.exe 0x46b8c8 — EvalGiveGift.
//   if (armed != 1) return 0;
//   if (eventMode != 2) return 0;
//   target must resolve; the giver-cash gate vs the cached market price decides.
//   currency-gift path (actionIn == 57): uses the explicit drag amount as cash and
//   sets the frame mode to 57; otherwise computes the giver's cash. The cash is
//   scaled by the event price factor when 0 < factor < 1. Reject unless cash >= price.
char EvalGiveGift(char actionIn, u8 eventMode, char armed, u8 dragKind,
                  bool targetResolved, int giverCash, float priceFactor,
                  i16 giftItemHiword) {
    (void)actionIn; // 57-mode callers translate the returned 11 into 57 upstream.
    if (armed != 1)
        return 0;
    if (eventMode != 2)
        return 0;
    // dragKind must be one of 1 (object), 4 (building), 7 (person); anything else
    // falls through the binary's chained else to `return 0`.
    if (dragKind != 1 && dragKind != 4 && dragKind != 7)
        return 0;
    if (!targetResolved)
        return 0;

    int cash = giverCash; // for actionIn==57 the caller passes the drag amount here.
    // event price-factor scaling: only when 0 < f < 1.0f (the binary's
    // *(float*)(v10+20) > 0.0 && < 1065353216 (== 1.0f bit pattern)).
    if (priceFactor > 0.0f && priceFactor < 1.0f) {
        double scaled = static_cast<double>(cash) * priceFactor;
        cash = static_cast<int>(scaled);
    }
    double price = g_i4Hooks.marketPrice(giftItemHiword, g_i4Hooks.currencyByte);
    if (cash < static_cast<int>(price))
        return 0;
    return 11; // gift accepted (57-mode callers translate the 11 into 57 upstream)
}

// gilde.exe 0x46c0c0 — FindNearestTarget (revalidate branch, frameKind==4).
FindTargetResult FindNearestRevalidate(int targetState, int actorActionType,
                                       double budget) {
    FindTargetResult out;
    int s = g_i4Hooks.scaleByActionType(targetState, actorActionType);
    float score;
    std::memcpy(&score, &s, sizeof(float)); // score is the float reinterpretation
    out.chosenActionType = actorActionType;
    out.score = score;
    // (LODWORD(v20) & 0x7FFFFFFF) != 0  -> score has non-zero magnitude.
    int bits;
    std::memcpy(&bits, &score, sizeof(int));
    if ((bits & 0x7FFFFFFF) != 0 && budget >= static_cast<double>(score)) {
        out.code = 15;
    }
    return out;
}

// gilde.exe 0x46c0c0 — FindNearestTarget (search branch, frameKind==0 tail).
//   v11 (bestDistShifted) chooses actionType 30 when >= 50, else 50; the budget is
//   cash * flt_61A438 (0.95). Accept iff score!=0 && score <= budget.
FindTargetResult FindNearestPick(int bestDistShifted, int targetState, int cash) {
    FindTargetResult out;
    double budget = static_cast<double>(cash) * kFindNearestCashFactor;
    int actionType = (bestDistShifted >= 50) ? 30 : 50;
    out.chosenActionType = actionType;
    int s = g_i4Hooks.scaleByActionType(targetState, actionType);
    float score;
    std::memcpy(&score, &s, sizeof(float));
    out.score = score;
    int bits;
    std::memcpy(&bits, &score, sizeof(int));
    if ((bits & 0x7FFFFFFF) == 0)
        return out; // score zero -> reject
    if (static_cast<double>(score) > budget)
        return out; // over budget -> reject
    out.code = 15;
    return out;
}

// gilde.exe 0x46c12c — search-loop threshold seed (RandomModulo(0x24) + 35).
int FindNearestThresholdSeed() {
    return g_i4Hooks.randomModulo(0x24) + 35;
}

// gilde.exe 0x46bcc0 — RequestSellObject.
char RequestSellObject(i32 slotId, u8 eventMode, bool currencyMode,
                       bool entityResolved, long long price) {
    if (slotId == -1 || eventMode != 2)
        return 0;
    if (currencyMode) {
        // resolve the entity; bail if unresolved, then emit cmd15 + the request.
        if (!entityResolved)
            return 0;
        g_i4Hooks.emitCommand("cm_RequestSellObjekt", 57);
        return 57;
    }
    g_i4Hooks.emitCommand("cm_RequestSellObjekt", 11);
    (void)price;
    return 11;
}

// gilde.exe 0x46e91c — PerformBroadcastSummon.
int PerformBroadcastSummon(bool hallResolved, bool marketResolved, bool actorIsFive,
                           const u8* buildingTypes, int n, int maxBroadcast,
                           int* outBroadcastCount) {
    if (outBroadcastCount)
        *outBroadcastCount = 0;
    if (!hallResolved)
        return 0;
    if (!marketResolved)
        return 0;
    // enqueue summon + delta packet + state22/args25 (UI/command leaves).
    g_i4Hooks.emitCommand("PerformBroadcastSummon", 25);
    if (actorIsFive && buildingTypes) {
        int broadcast = 0;
        for (int i = 0; i < n; ++i) {
            if (broadcast >= maxBroadcast)
                break;
            u8 t = buildingTypes[i];
            if (t == 6 || t == 7)
                ++broadcast;
        }
        if (outBroadcastCount)
            *outBroadcastCount = broadcast;
    }
    return 25;
}

// gilde.exe 0x46c3d0 — EvalEnterBuilding worth/cost reject formula.
//   reject iff  (worth - cost)/worth  >=  (worth - roomWorth)/roomWorth * A + B
// (the binary's `if (lhs <= rhs) return 0;`, lhs == worth-side, rhs == ratio-side).
bool EnterBuildingWorthReject(int worth, int cost, int roomWorth) {
    if (worth == 0 || roomWorth == 0)
        return false;
    double rhs = static_cast<double>(worth - cost) / worth;
    double lhs = static_cast<double>(worth - roomWorth) / roomWorth
                     * kEnterBuildingWorthFactorA + kEnterBuildingWorthBiasB;
    return lhs <= rhs;
}

// gilde.exe 0x46c9d4 — EvalLeaveBuilding sale reject.
//   reject iff (sale - cost)/sale >= flt_61A458 (0.25).
bool LeaveBuildingSaleReject(int sale, int cost) {
    if (sale == 0)
        return false;
    double ratio = static_cast<double>(sale - cost) / sale;
    return ratio >= kLeaveBuildingSaleThreshold;
}

// gilde.exe 0x46d1a8 — EvalBuildingAction leave pre-gate.
//   delta = (slots[occupied]+1 - baseline); fires iff delta/totalSlots < flt_61A47C.
bool BuildingActionLeavePreGate(int occupiedCategoryDelta, int totalSlots) {
    if (totalSlots == 0)
        return false;
    double ratio = static_cast<double>(occupiedCategoryDelta) / totalSlots;
    return ratio < kBuildingActionLeaveThreshold;
}

// gilde.exe 0x46d1a8 — mid-game rank gate.
//   if (level1Count > capacity) return reject;
//   if ((int)gameTimeLo % 8 != (entityId & 7)) return reject;
bool BuildingActionRankGate(int level1Count, int capacity, i32 gameTimeLo,
                            i32 entityIdLow3) {
    if (level1Count > capacity)
        return true;
    if ((gameTimeLo % 8) != (entityIdLow3 & 7))
        return true;
    return false;
}

// ===========================================================================
// (C) Tutorial-panel input FSM (0x595b98).
// ===========================================================================
TutorialPanel* DispatchPanelEvent(TutorialPanel* panel, int eventIdHi, int auxId,
                                  int flag, int nowTick) {
    if (!panel || !panel->active)
        return panel;
    PanelStep* step = panel->step;
    if (!step)
        return panel;

    if (step->mode == 2) {
        // "advanced" panel: only subStates 4/9/10 participate.
        u8 ss = panel->subState;
        if (ss != 4 && ss != 9 && ss != 10)
            return panel;
        if (step->callback) {
            // custom callback handler — we record that it would fire and stop.
            panel->callbackInvoked = true;
            return panel;
        }
        i32 mode2 = step->mode2; // panel dword[14] mirrored onto the step view
        if (mode2 == 1) {
            if (flag) {
                if (flag == 1 && (step->targetId >> 24) == eventIdHi) {
                    panel->subState = 4;
                    panel->lastTick = nowTick;
                    panel->auxFlag = 0;
                }
            } else if ((step->targetId >> 24) == eventIdHi) {
                panel->subState = 5;
                panel->lastTick = nowTick;
                if (auxId)
                    panel->auxId = auxId;
            }
            panel->combinedState = panel->subState;
            ++panel->eventCount;
        } else if (mode2 == 3) {
            u8 sub = panel->subState;
            if (sub == 9) {
                i32 alt = step->altTargetId;
                if (alt && (eventIdHi == alt)) {
                    panel->combinedState = 10;
                    panel->subState = 10;
                    panel->lastTick = nowTick;
                    ++panel->eventCount;
                    return panel;
                }
                if ((step->targetId >> 24) == eventIdHi && flag == 1) {
                    panel->combinedState = 4;
                    panel->subState = 4;
                    panel->auxFlag = 0;
                    panel->lastTick = nowTick;
                    ++panel->eventCount;
                    return panel;
                }
            } else if (sub == 10) {
                if ((step->targetId >> 24) == eventIdHi && flag == 1) {
                    panel->combinedState = 11;
                    panel->subState = 11;
                    panel->lastTick = nowTick;
                    ++panel->eventCount;
                    return panel;
                }
            } else if (sub == 4 && (step->targetId >> 24) == eventIdHi && !flag) {
                panel->combinedState = 9;
                panel->subState = 9;
                panel->lastTick = nowTick;
                ++panel->eventCount;
                return panel;
            }
            panel->combinedState = panel->subState;
            ++panel->eventCount;
        }
        return panel;
    }

    // non-advanced panel: subStates 4 / 10 only.
    u8 ss = panel->subState;
    if (ss != 4 && ss != 10)
        return panel;
    if (step->callback) {
        panel->callbackInvoked = true;
        ++panel->lastTick; // the binary bumps a different counter here (+12)
        return panel;
    }
    if (step->mode == 3) {
        // dwell branch: advance to nextState once the dwell window elapses.
        if (step->dwellLimit + panel->lastTick < nowTick) {
            panel->combinedState = step->nextState;
            panel->subState = step->nextState;
            panel->auxFlag = 1;
            panel->lastTick = nowTick;
        }
        return panel;
    }
    if ((step->targetId >> 24) == eventIdHi) {
        if (flag)
            panel->combinedState = 11;
        else
            panel->combinedState = step->nextState;
        panel->subState = panel->combinedState;
        panel->lastTick = nowTick;
    }
    ++panel->eventCount;
    return panel;
}

// ===========================================================================
// (B) Confirm-dialog / tavern dispatch FSMs.
// ===========================================================================

// gilde.exe 0x513168 — Dialog_AttackCommand.
void Dialog_AttackCommand(int textId, char arg) {
    if (g_i4Hooks.checkActiveCharFlag())
        return; // active char busy -> no-op
    g_i4Hooks.playerBarCreate();
    g_i4DialogTrace.barOpened = true;
    if (g_i4Hooks.showMessageBox(textId, 1, arg)) {
        g_i4DialogTrace.messageBoxShown = true;
        // emit the slot-reset attack command, play the favor voice line, clear sel.
        g_i4Hooks.emitCommand("cm_Angriff", 108);
        g_i4Hooks.selectionClearAll();
        g_i4DialogTrace.selectionCleared = true;
    }
    g_i4Hooks.playerBarDestroy();
    g_i4DialogTrace.barClosed = true;
}

// gilde.exe 0x5180e8 — TavernStammtischDispatch.
void Dialog_TavernStammtischDispatch(bool nodeExists, bool handlerMatch,
                                     bool alreadySeated) {
    if (!nodeExists || !handlerMatch) {
        // both early-outs jump straight to the HUD refresh.
        return;
    }
    if (alreadySeated)
        g_i4Hooks.tavernStammtischLeave();
    else
        g_i4Hooks.tavernStammtischJoin();
    // (HUD refresh leaf is inert.)
}

// gilde.exe 0x518bf0 — TavernDarkCornerDispatch.
void Dialog_TavernDarkCornerDispatch(bool nodeExists, bool isActivePlayer) {
    if (!nodeExists)
        return;
    if (isActivePlayer)
        g_i4Hooks.tavernDarkCornerBrowse();
    else
        g_i4Hooks.tavernDarkCornerBuy();
}

// gilde.exe 0x5129ec — RobberCampCheckAndShow.
int Dialog_RobberCampCheckAndShow(bool hasCamp) {
    if (g_i4Hooks.checkActiveCharFlag())
        return 0; // busy
    if (!hasCamp)
        return 0;
    g_i4Hooks.playerBarCreate();
    g_i4DialogTrace.barOpened = true;
    g_i4Hooks.panelDispatcher(1);
    g_i4Hooks.playerBarDestroy();
    g_i4DialogTrace.barClosed = true;
    g_i4Hooks.selectionClearAll();
    g_i4DialogTrace.selectionCleared = true;
    return 1;
}

// gilde.exe 0x512668 — RobberCampShowBar.
int Dialog_RobberCampShowBar() {
    g_i4Hooks.playerBarCreate();
    g_i4DialogTrace.barOpened = true;
    g_i4Hooks.panelDispatcher(1);
    g_i4Hooks.playerBarDestroy();
    g_i4DialogTrace.barClosed = true;
    g_i4Hooks.selectionClearAll();
    g_i4DialogTrace.selectionCleared = true;
    return 1;
}

// gilde.exe 0x515e30 — SpionageConfirm.
void Dialog_SpionageConfirm() {
    // build desc + formatted message (text id 4876), run office overview, refresh HUD.
    g_i4Hooks.runOfficeOverviewWindow();
}

} // namespace guild::sim

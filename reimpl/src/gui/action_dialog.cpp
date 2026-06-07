#include "gui/action_dialog.h"

namespace guild::gui {

namespace {

ActionCommandSink  g_defaultSink;
ActionCommandSink* g_sink = &g_defaultSink;

// Stable child-object id bases.  The originals obtain these from VIBE_Form_GetChildObjectId;
// only their relative identity matters for the wiring, so we assign reproducible bases.
constexpr int kConfirmObj = 2000;
constexpr int kCancelObj  = 2001;

} // namespace

void ActionDialog_SetCommandSink(ActionCommandSink* sink) {
    g_sink = sink ? sink : &g_defaultSink;
}

// Shared cost math (gilde.exe pattern: v44 = wealth * fltA; v6 = wealth2 * fltB + v44;
// ConvertX; if (DebugToggle(27)&2) cost /= 2).  ConvertX is the screen-coord transform; in
// the headless port it is identity (it scales by 1 for the price magnitude).
int ActionDialog_ComputeCost(const ActionState& s, double rateA, double rateB) {
    double v = (double)s.wealthSelf * rateA + (double)s.wealthTarget * rateB;
    int cost = (int)v;
    if (s.halveCost)
        cost /= 2;
    return cost;
}

// gilde.exe 0x547264 — VIBE_ActionDialog_Sabotage.
//   cost = wealthSelf*0.02 + wealthTarget*0.05;  (debug 27 -> /2)
//   if (!CheckResourceAmount) abort.
//   if (same-kind handler exists) -> RenderFormattedMessage(4885) + messagebox; abort.
//   if (!pairedReverse) msg 4886; if (!pairedForward) msg 4887; if (!targetFree) msg 4888.
//   else form=perga_rolle, slot 1, body 4883 (+4900 cost), confirm child; law seals {2,23}.
ActionLayout ActionDialog_BuildSabotage(const ActionState& s) {
    ActionLayout l{};
    l.cost = ActionDialog_ComputeCost(s, kRateSabotageSelf, kRateSabotageTarget);
    if (!s.hasResources)  { l.preflight = ActionPreflight::kNoResources; return l; }
    if (s.busy)           { l.preflight = ActionPreflight::kInProgress; return l; }
    if (!s.pairedReverse || !s.pairedForward || !s.targetFree) {
        l.preflight = ActionPreflight::kBusyTarget; return l;
    }
    l.form = kFormActPergament;
    l.slot = 1;
    l.bodyText = kTextSabotageBody;
    l.confirmObj = kConfirmObj;
    l.cmdKind = kActKindSabotage;
    l.actionLabel = kActionSabotage;
    l.seals = {2, 23};
    return l;
}

// gilde.exe 0x547714 — VIBE_ActionDialog_Spy.
//   if (already spying this target) -> msg 4880, optional RearmSpy; abort.
//   else if (runningSpies >= 5) -> msg 4879; abort.
//   else cost = (wealthSelf + wealthTarget) * 0.005;  if (!CheckResource) abort.
//   form=special\spionage, slot 2, body 4877 (+4900). No law seals.
ActionLayout ActionDialog_BuildSpy(const ActionState& s) {
    ActionLayout l{};
    if (s.busy)                     { l.preflight = ActionPreflight::kInProgress; return l; }
    if (s.runningSpies >= 5)        { l.preflight = ActionPreflight::kTooMany; return l; }
    l.cost = ActionDialog_ComputeCost(s, kRateSpy, kRateSpy);
    if (!s.hasResources)            { l.preflight = ActionPreflight::kNoResources; return l; }
    l.form = kFormActSpionage;
    l.slot = 2;
    l.bodyText = kTextSpyBody;
    l.confirmObj = kConfirmObj;
    l.cmdKind = kActKindSpy;
    l.actionLabel = kActionSpionage;
    return l;
}

// gilde.exe 0x547a60 — VIBE_ActionDialog_BeatUp.
//   cost = wealthSelf * 0.0085;  (debug 27 -> /2)
//   if (same-kind handler) -> msg 4893; abort.
//   if (!CheckResource) abort.  if (!pairedReverse) msg 4894; if (!pairedForward) msg 4895.
//   form=perga_rolle, slot 1, body 4891 (+4900). Law seals {3,20}.
ActionLayout ActionDialog_BuildBeatUp(const ActionState& s) {
    ActionLayout l{};
    if (s.busy) { l.preflight = ActionPreflight::kInProgress; return l; }
    l.cost = ActionDialog_ComputeCost(s, kRateBeatUp, 0.0);
    if (!s.hasResources) { l.preflight = ActionPreflight::kNoResources; return l; }
    if (!s.pairedReverse || !s.pairedForward) {
        l.preflight = ActionPreflight::kBusyTarget; return l;
    }
    l.form = kFormActPergament;
    l.slot = 1;
    l.bodyText = kTextBeatUpBody;
    l.confirmObj = kConfirmObj;
    l.cmdKind = kActKindBeatUp;
    l.actionLabel = kActionBeatUp;
    l.seals = {3, 20};
    return l;
}

// gilde.exe 0x54891c — VIBE_ActionDialog_ConfirmAbduct.
//   skill check (gated upstream); CountMatchingEntities(target) must be > 0 else msg 4969.
//   form=perga_rolle, slot 1, body 4972 (count, plural-base 4965/4966, person).
//   confirm child + cancel child (v29). Law seals {4,18}.  Confirm: RequestBuildOp90(-2),
//   random success roll -> msg 4974/4975, slot-reset kind 46.
ActionLayout ActionDialog_BuildConfirmAbduct(const ActionState& s, int abductableCount) {
    ActionLayout l{};
    if (abductableCount <= 0) { l.preflight = ActionPreflight::kBusyTarget; return l; }
    l.form = kFormActPergament;
    l.slot = 1;
    l.bodyText = kTextAbductConfirm;
    l.confirmObj = kConfirmObj;
    l.cancelObj  = kCancelObj;
    l.cmdKind = kActKindAbduct;
    l.seals = {4, 18};
    (void)s;
    return l;
}

// gilde.exe 0x548f30 — VIBE_ActionDialog_ConfirmFreePrisoner.
//   if (!FindActionByActor) -> msg 5406 (not held); if (state > 3) -> msg 5405 (too late).
//   skill check; form=perga_rolle, slot 1, body 5407.  Law seals {4,3}.  Confirm (1210):
//   Person_QueryBegin(1,5,10) + RandomModulo(0x1D) delay; slot-reset kind 53.
ActionLayout ActionDialog_BuildConfirmFreePrisoner(const ActionState& s) {
    ActionLayout l{};
    if (!s.freeHeldByUs) { l.preflight = ActionPreflight::kNotHeld; return l; }
    if (s.freeState > 3) { l.preflight = ActionPreflight::kTooLate; return l; }
    l.form = kFormActPergament;
    l.slot = 1;
    l.bodyText = kTextFreeBody;
    l.confirmObj = kConfirmObj; // confirm matched via dword_75BF38==1210 (no object compare)
    l.cmdKind = kActKindFree;
    l.seals = {4, 3};
    return l;
}

// Wiring — replays the modal loop's per-click branch.  Returns true when the loop ends.
//   Sabotage/Spy/BeatUp: confirm needs id 1210 AND the confirm object hit; cancel = 1155.
//   Abduct: confirm object hit -> Abduct(op -2); the explicit cancel object also ends.
//   FreePrisoner: id 1210 -> FreePrisoner(kind 53); id 1155 ends.
bool ActionDialog_Dispatch(const ActionLayout& l, const ActionState& s,
                           int clickedId, int clickedObj) {
    if (l.preflight != ActionPreflight::kOk)
        return true; // no dialog was shown; nothing to dispatch

    if (clickedId == kActClickCancel)
        return true;

    switch (l.cmdKind) {
    case kActKindSabotage:
    case kActKindSpy:
    case kActKindBeatUp:
        if (clickedId == kActClickOK && l.confirmObj != -1 && clickedObj == l.confirmObj) {
            g_sink->StartAction(s.actorEntity, s.targetEntity, l.cmdKind, l.cost,
                                l.actionLabel);
            return true;
        }
        return false;

    case kActKindAbduct:
        if (clickedObj == l.confirmObj) {
            g_sink->Abduct(s.actorEntity, s.targetEntity, l.cmdKind, -2);
            return true;
        }
        if (clickedObj == l.cancelObj)
            return true;
        return false;

    case kActKindFree:
        if (clickedId == kActClickOK) {
            // delay is RandomModulo(0x1D); we pass 0 here — the sim owns the RNG.
            g_sink->FreePrisoner(s.targetEntity, l.cmdKind, 0);
            return true;
        }
        return false;
    }
    return false;
}

} // namespace guild::gui

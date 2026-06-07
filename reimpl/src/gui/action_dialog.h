#pragma once
// guild::gui — the criminal/special action dialogs (sabotage / spy / beat-up / abduct /
// free-prisoner).  All live in gilde.exe's special-action cluster 0x547264..0x5491d0.
//
// Each is a modal confirm over a target person/building:
//   1. preflight checks (already-acting handler search, skill/resource gates) that may
//      short-circuit to a VIBE_Dialog_ShowMessageBox and abort;
//   2. form  = VIBE_GameTick_Finalize(0,0,"<form name>");  VIBE_Form_CenterChildWindows;
//   3. VIBE_Form_SelectWindow(form, slot); VIBE_Text_RenderRichString(textId, ...);
//      VIBE_Form_GetChildObjectId(...) -> the dialog's clickable confirm/cancel objects;
//      VIBE_Panel_BuildLawSeals(...) decorates the parchment with seal stamps;
//   4. while (VIBE_GameLogic_RunFrameLoop(423879, ...)) {           // modal loop
//        if (dword_672230 || dword_75BF38 == 1155) cancel;          // right/cancel
//        if (dword_75BF38 == 1210 && hit confirm object) {          // OK
//          EnqueueBuildingActionStart("<action>"); QueueRequestSlotReset28(...);
//          EnqueueBuildingActionEnd();  ...  done;
//        }
//      }
//   5. VIBE_Form_Destroy(form).
//
// The cost shown is a wealth-derived sum (Person_ComputeTotalWealth scaled by a per-action
// float, optionally halved by a debug toggle) converted through VIBE_Coord_ConvertX.  We
// recover the form names, window slots, text ids, the per-action command kind byte + label
// string, the law-seal stamp ids, and the confirm/cancel wiring.  Cost math is replayed
// from a synthetic state; the frame loop / text engine / command codec are forward-declared
// and mutations go through a mockable command sink.  Click ids: 1210 (OK), 1155 (cancel).

#include "gui/types.h"

namespace guild::gui {

inline constexpr int kActClickOK     = 1210; // dword_75BF38 confirm
inline constexpr int kActClickCancel = 1155; // dword_75BF38 cancel / right button

inline constexpr int kActLoopForm = 423879;  // RunFrameLoop confirm-dialog selector

// Recovered .form names (the VIBE_GameTick_Finalize string argument).
inline constexpr const char* kFormActPergament = "misc\\perga_rolle";  // Sabotage/BeatUp/Abduct/Free
inline constexpr const char* kFormActSpionage  = "special\\spionage";  // Spy

// Recovered command-action label strings (VIBE_Command_EnqueueBuildingActionStart arg).
inline constexpr const char* kActionSabotage = "sabotage";
inline constexpr const char* kActionSpionage = "spionage";
inline constexpr const char* kActionBeatUp   = "beatup";

// Recovered per-action command "kind" byte (first byte of the action delta record).
inline constexpr int kActKindSpy      = 24; // ActionDialog_Spy
inline constexpr int kActKindSabotage = 25; // ActionDialog_Sabotage
inline constexpr int kActKindBeatUp   = 26; // ActionDialog_BeatUp
inline constexpr int kActKindAbduct   = 46; // ConfirmAbduct slot-reset kind
inline constexpr int kActKindAbductTo = 47; // AbductChooseDestination kind
inline constexpr int kActKindFree     = 53; // ConfirmFreePrisoner kind

// Per-action wealth-scaling float constants (recovered byte-exact via get_bytes).
inline constexpr double kRateSabotageSelf   = 0.019999999552965164;  // flt_62411C
inline constexpr double kRateSabotageTarget = 0.05000000074505806;   // flt_624120
inline constexpr double kRateSpy            = 0.004999999888241291;   // flt_624144 (both terms)
inline constexpr double kRateBeatUp         = 0.008500000461935997;   // flt_624150 (self only)

// Recovered RenderRichString text ids per dialog (body + the messagebox-error variants).
inline constexpr int kTextCostLine = 4900; // shared "%i coins" cost fragment

inline constexpr int kTextSabotageBody   = 4883; // Sabotage parchment body
inline constexpr int kTextSabotageDone   = 4884; // success message
inline constexpr int kTextSabotageInProg = 4885; // already sabotaging this target
inline constexpr int kTextSabotageBusyA  = 4886; // target paired-reverse missing
inline constexpr int kTextSabotageBusyB  = 4887; // target paired-forward missing
inline constexpr int kTextSabotageBusyC  = 4888; // target busy

inline constexpr int kTextSpyBody    = 4877; // Spy parchment body
inline constexpr int kTextSpyDone    = 4878; // success message
inline constexpr int kTextSpyTooMany = 4879; // >= 5 spies running
inline constexpr int kTextSpyInProg  = 4880; // already spying (offer to cancel)

inline constexpr int kTextBeatUpBody   = 4891; // BeatUp parchment body
inline constexpr int kTextBeatUpDone   = 4892; // success message
inline constexpr int kTextBeatUpInProg = 4893; // already beating this target
inline constexpr int kTextBeatUpBusyA  = 4894; // paired-reverse missing
inline constexpr int kTextBeatUpBusyB  = 4895; // paired-forward missing

inline constexpr int kTextAbductPick    = 4962; // PromptTargetSelect office-overview header
inline constexpr int kTextAbductConfirm = 4972; // ConfirmAbduct body (count, person)
inline constexpr int kTextAbductDest    = 4978; // AbductChooseDestination pick header
inline constexpr int kTextAbductDest2   = 4979; // second office-overview header
inline constexpr int kTextAbductBody    = 4980; // destination-confirm body
inline constexpr int kTextAbductDone    = 4981; // dispatch message
inline constexpr int kTextAbductNone    = 4969; // no abductable targets
inline constexpr int kTextAbductWin     = 4974; // success
inline constexpr int kTextAbductFail    = 4975; // failed

inline constexpr int kTextFreePick    = 5404; // BeginFreePrisonerPick header
inline constexpr int kTextFreeBody    = 5407; // ConfirmFreePrisoner body
inline constexpr int kTextFreeTooLate = 5405; // sentence too far along (state > 3)
inline constexpr int kTextFreeNotHeld = 5406; // target not held by us

// Law-seal stamp descriptors built by VIBE_Panel_BuildLawSeals (the v41/v25/v35/v20 init):
// {form, 2, 3, 4} window ids + two trailing seal stamp bytes (v42/v43, v26/v27, etc).
struct LawSealSpec {
    int seal0 = 0;  // first seal stamp id  (Sabotage=2, BeatUp=3, Abduct=4, Free=4)
    int seal1 = 0;  // second seal stamp id (Sabotage=23, BeatUp=20, Abduct=18, Free=3)
};

// ---------------------------------------------------------------------------
// Synthetic action state — the inputs the originals derive the dialog from.
// ---------------------------------------------------------------------------
struct ActionState {
    int actorEntity  = 0;     // the active player char's entity id
    int targetEntity = 0;     // the target person's entity id
    int wealthSelf   = 0;     // Person_ComputeTotalWealth(self)
    int wealthTarget = 0;     // Person_ComputeTotalWealth(target)
    bool halveCost   = false; // debug toggle bit 0x2 set -> cost /= 2
    bool busy        = false; // a same-kind action handler already exists -> "in progress"
    bool pairedReverse = true; // CharAction_FindPairedEntityReverse != 0
    bool pairedForward = true; // CharAction_FindPairedEntityForward != 0
    bool targetFree    = true; // CharAction_IsAnimalTargetBusy (Sabotage) — target available
    int  runningSpies  = 0;    // count of spy handlers (>=5 -> "too many", Spy only)
    bool hasResources  = true; // Dialog_CheckResourceAmount(cost)
    int  freeState     = 0;    // ConfirmFreePrisoner: sentence progress (>3 -> too late)
    bool freeHeldByUs  = true; // CharAction_FindActionByActor found the action
    int  abductHandler = 0;    // Spy re-arm: existing spy handler ptr (RearmSpy target)
};

// The widget set / preflight outcome a build produces.
enum class ActionPreflight {
    kOk,          // dialog can be shown
    kInProgress,  // same action already running (messagebox, no dialog)
    kTooMany,     // limit reached (Spy >= 5)
    kBusyTarget,  // target unavailable / unpaired (messagebox)
    kNoResources, // not enough coin (Dialog_CheckResourceAmount failed)
    kNotHeld,     // free-prisoner: we don't hold this prisoner
    kTooLate,     // free-prisoner: sentence too far along
};

struct ActionLayout {
    const char* form = nullptr; // resolved .form name (null when preflight != kOk)
    int slot = 1;               // window slot the body text goes into
    int bodyText = 0;           // RenderRichString body id
    int confirmObj = -1;        // GetChildObjectId of the confirm parchment object
    int cancelObj  = -1;        // GetChildObjectId of the cancel object (-1 if none)
    int cost = 0;               // displayed/charged cost
    int cmdKind = 0;            // command kind byte
    const char* actionLabel = nullptr; // EnqueueBuildingActionStart label (or null)
    LawSealSpec seals{};        // law-seal stamps (0,0 if none)
    ActionPreflight preflight = ActionPreflight::kOk;
};

// Mockable command sink for the dispatched actions.
struct ActionCommandSink {
    virtual ~ActionCommandSink() = default;
    // Start/reset/end action bracket (label = "sabotage"/"spionage"/"beatup", kind byte).
    virtual void StartAction(int /*actor*/, int /*target*/, int /*kind*/,
                             int /*cost*/, const char* /*label*/) {}
    // ConfirmAbduct / AbductChooseDestination: building-op 90 + slot reset (no label).
    virtual void Abduct(int /*self*/, int /*target*/, int /*kind*/, int /*op*/) {}
    // ConfirmFreePrisoner: slot-reset kind 53 (random delay).
    virtual void FreePrisoner(int /*target*/, int /*kind*/, int /*delay*/) {}
    // Spy when already-running: re-arm the existing handler (QueueRequestEntity29).
    virtual void RearmSpy(int /*handler*/) {}
};
void ActionDialog_SetCommandSink(ActionCommandSink* sink);

// ---------------------------------------------------------------------------
// Cost math (recovered): cost = (wealthSelf*rateA + wealthTarget*rateB), then ConvertX
// (identity here), then halved when the debug toggle bit is set.  For Spy both terms use
// the same rate; for BeatUp rateB is 0 (self-wealth only).
// ---------------------------------------------------------------------------
int ActionDialog_ComputeCost(const ActionState& s, double rateA, double rateB);

// Build the dialog layout + preflight for each action.
ActionLayout ActionDialog_BuildSabotage(const ActionState& s);
ActionLayout ActionDialog_BuildSpy(const ActionState& s);
ActionLayout ActionDialog_BuildBeatUp(const ActionState& s);
// abductableCount = He_CountMatchingEntities (>0 required, else "no targets").
ActionLayout ActionDialog_BuildConfirmAbduct(const ActionState& s, int abductableCount);
ActionLayout ActionDialog_BuildConfirmFreePrisoner(const ActionState& s);

// Wiring: map a clicked id+object to the dialog's action via the sink.  Returns true when
// the click ended the loop (confirmed or cancelled).
bool ActionDialog_Dispatch(const ActionLayout& l, const ActionState& s,
                           int clickedId, int clickedObj);

} // namespace guild::gui

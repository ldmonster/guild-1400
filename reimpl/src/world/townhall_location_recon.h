#pragma once
// guild::world — Rathaus (town-hall) location interaction rule-cores.
//
// Reconstructed 1:1 from gilde.exe.  The town hall is entered through
// VIBE_Building_EnterAndDispatch (0x51defc), which runs two modal contact loops
// and (from those) opens two dialogs:
//
//   VIBE_TownHall_RunContactDispatchLoop  0x51f70c — building-purchase / agenda /
//                                                    office-info contact menu.
//   VIBE_TownHall_RunLawAndApplyLoop      0x52036c — law-books / apply-for-office /
//                                                    buy-citizenship contact menu.
//   VIBE_TownHall_ShowBuildingInfoDialog  0x51f2e8 — the buy-municipal-building list.
//   VIBE_TownHall_ShowCitizenshipDialog   0x51f81c — the buy-citizenship flow.
//
// As elsewhere in the reconstruction (see gui/contact_menu.h, gui/contact_loops.h),
// each contact loop is a:
//     ResetEntries();
//     while (RunFrameLoop(kContactForm, ...)) {
//         if (handler-flag bits)        Register(...) each entry;   // DATA/LAYOUT
//         if (statistics object active) ShowStatistics(...);        // side overlay
//         if (clickedEntry)             dispatch by id;             // WIRING
//     }
// The modal frame loop, the localized-label text engine, the 3D-overlay renderer,
// the live person/building arrays and the command queue live in the io/sim/render
// clusters; they are modelled here as inert-default hooks so the pure entry-set,
// eligibility gating and the buy-rule math are translated and testable 1:1.
//
// What this module owns and translates byte-for-byte:
//   (a) each contact loop's entry set + the entry-id -> action wiring, and the exact
//       handler-flag gate bits that decide which entries appear;
//   (b) the citizenship eligibility gate (wealth >= 16000) and the citizenship
//       grant rule (deduct 16000, schedule the grant N days out, queue the
//       command requests, fan it out to the player's other family members);
//   (c) the building-info list filter (which municipal buildings are offered) and the
//       buy gate (resource-amount check).
//
// All gfx/icon id globals (dword_8CA54C, ...) are runtime asset handles, zero at load;
// they are NOT logic and are passed straight through to Register, so they are omitted.

#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Shared status-text constants (mirror gui/contact_menu.h; kept local so this
// world-side module is self-contained and does not pull the gui tree).
// ===========================================================================
namespace townhall {

// VIBE_GameLogic_RunFrameLoop selector for a contact menu (decimal 425983 == 0x67FFF).
constexpr int kContactForm = 425983;
// VIBE_GameLogic_RunFrameLoop selector for a modal dialog (decimal 423879 == 0x67887).
constexpr int kDialogForm = 423879;

// Handler-flag-word bits tested by VIBE_Interaction_TestHandlerFlagWord (0x595f1c)
// to decide which contact-menu entries this player/office is allowed to see.
constexpr int kFlagBuyBuilding   = 512;     // contact_GEB_KAUFEN      (0x200)
constexpr int kFlagAgenda        = 1024;    // contact_TAGESORDNUNG    (0x400)
constexpr int kFlagOfficeInfo    = 2048;    // contact_AMT_INFO        (0x800)
constexpr int kFlagLawBooks      = 4096;    // ob_GESETZBUCH_*         (0x1000)
constexpr int kFlagCitizenship   = 0x2000;  // contact_BUERGERRECHT
constexpr int kFlagApplyOffice   = 0x8000;  // contact_BEWERBEN

// Statistics-object opcode (byte_67225C): which statistics window the selected
// scene object opens while the building-purchase loop is running.
constexpr int kStatObjGeneral = 47;   // VIBE_Statistics_ShowGeneralWindow
constexpr int kStatObjTax     = 20;   // VIBE_Statistics_ShowTaxWindow

// Law-book section ids passed to VIBE_Gesetz_OpenLawBookSection (0x5587c0).
constexpr int kLawSectionTax        = 0x112; // ob_GESETZBUCH_STEUERN
constexpr int kLawSectionCriminal   = 0x111; // ob_GESETZBUCH_STRAFRECHT
constexpr int kLawSectionConstitution = 0x113; // ob_GESETZBUCH_VERFASSUNG

// Citizenship economics (VIBE_TownHall_ShowCitizenshipDialog, 0x51f81c).
constexpr int kCitizenshipPrice = 16000;   // wealth gate AND deducted price
constexpr int kCitizenshipMsgTooPoor = 5647; // VIBE_Text_RenderFormattedMessage id
constexpr int kCitizenshipMsgGranted = 5646;
constexpr int kCitizenshipFlagArg    = 0x4000; // QueueRequestArgs25 flag (privilege bit)
constexpr int kCitizenshipReqOpcode  = 456;    // QueueRequestArgs25 opcode (v47)
constexpr int kCitizenshipPanelEvent = 0x2E;   // VIBE_Interaction_DispatchPanelEvent code

// Family-member scan bound (VIBE_TownHall_ShowCitizenshipDialog inner loop): the scan
// index v20 runs over [0,768).  This is the number of family-record SLOTS scanned, NOT
// the number of grants — byte_63CC1D caps the emitted-grant count (see FamilyGrantCap).
constexpr int kFamilyMemberScanMax = 768;  // v20 < 768
// Family-member status bytes that mark a co-resident the grant fans out to.
// (byte_12CE912[536 * v20], stride 0x218.)
constexpr int kMemberStatusA = 6;  // byte_12CE912 == 6
constexpr int kMemberStatusB = 7;  // byte_12CE912 == 7

} // namespace townhall

// ===========================================================================
// VIBE_TownHall_RunContactDispatchLoop  0x51f70c
//
// Builds up to three entries gated by handler-flag bits, then on click dispatches:
//   contact_GEB_KAUFEN   (flag 512)  -> ShowBuildingInfoDialog
//   contact_TAGESORDNUNG (flag 1024) -> Amt_RunCandidateWindowVariantB (agenda)
//   contact_AMT_INFO     (flag 2048) -> Amt_RunCandidateWindowVariantA (office info)
// Independently, when a statistics scene-object is selected (dword_63C7B8 set), the
// loop opens a statistics window (general/tax) by byte_67225C opcode.
// ===========================================================================

// The entry-id set the contact-dispatch loop registers this frame.  An id of 0 means
// "entry not present" (its handler-flag gate was closed); the original initialises the
// non-registered ids to 0 in the else branches.
struct TownHallContactIds {
    int buyBuilding;  // contact_GEB_KAUFEN   (flag 512)
    int agenda;       // contact_TAGESORDNUNG (flag 1024)
    int officeInfo;   // contact_AMT_INFO     (flag 2048)
};

// Hook for the gated predicates / live state these loops consult.  Default: all
// handler flags open, no statistics object selected, nothing clicked.
struct TownHallGate {
    virtual ~TownHallGate() = default;
    // VIBE_Interaction_TestHandlerFlagWord(mask) (0x595f1c).
    virtual bool HandlerFlag(int /*mask*/) { return true; }
    // dword_63C7B8 — a statistics scene object is currently selected.
    virtual bool StatObjectActive() { return false; }
    // byte_67225C — which statistics object (kStatObjGeneral / kStatObjTax / other).
    virtual int  StatObjectOpcode() { return 0; }
};

// Command sink for the verbs these town-hall loops dispatch.
struct TownHallSink {
    virtual ~TownHallSink() = default;
    virtual void ShowBuildingInfoDialog(int /*obj*/) {}  // 0x51f2e8
    virtual void RunAgendaWindow(int /*obj*/) {}         // VIBE_Amt_RunCandidateWindowVariantB
    virtual void RunOfficeInfoWindow(int /*obj*/) {}     // VIBE_Amt_RunCandidateWindowVariantA
    virtual void ShowCitizenshipDialog(int /*obj*/) {}   // 0x51f81c
    virtual void OpenLawBookSection(int /*section*/) {}  // VIBE_Gesetz_OpenLawBookSection
    virtual void ShowApplicationDialog(int /*obj*/) {}   // VIBE_Office_ShowApplicationDialog
    virtual void ShowStatisticsGeneral(int /*obj*/) {}   // VIBE_Statistics_ShowGeneralWindow
    virtual void ShowStatisticsTax(int /*obj*/) {}       // VIBE_Statistics_ShowTaxWindow
};

// Register the contact-dispatch loop's entry set for this frame.  `gate` decides which
// entries are present.  Each present entry's id is taken from `registerEntry(name)`
// (the StatusText_Register leaf, mocked); absent entries get id 0.  Returns the id set.
// 1:1 with the body of 0x51f70c (entry order: buy / agenda / officeInfo).
TownHallContactIds TownHall_BuildContactDispatch(TownHallGate& gate);

// Run the per-frame statistics-overlay side effect (the dword_63C7B8 block of 0x51f70c).
void TownHall_RunStatisticsOverlay(TownHallGate& gate, int selectedObj, TownHallSink& sink);

// Dispatch a clicked entry id for the contact-dispatch loop.  Returns true if handled.
// 1:1 with the dispatch chain of 0x51f70c.
bool TownHall_DispatchContact(int clicked, const TownHallContactIds& ids,
                              int selectedObj, TownHallSink& sink);

// ===========================================================================
// VIBE_TownHall_RunLawAndApplyLoop  0x52036c
//
// Registers law-book entries (gate 4096), an apply-for-office entry (gate 0x8000) and a
// buy-citizenship entry (gated by an office-record privilege bit AND a player-status
// byte AND handler flag 0x2000).  On click:
//   ob_GESETZBUCH_STEUERN     -> OpenLawBookSection(0x112)
//   ob_GESETZBUCH_STRAFRECHT  -> OpenLawBookSection(0x111)
//   ob_GESETZBUCH_VERFASSUNG  -> OpenLawBookSection(0x113)
//   contact_BUERGERRECHT      -> ShowCitizenshipDialog
//   contact_BEWERBEN          -> ShowApplicationDialog
// ===========================================================================

struct TownHallLawIds {
    int lawTax;        // ob_GESETZBUCH_STEUERN    (gate 4096)
    int lawCriminal;   // ob_GESETZBUCH_STRAFRECHT (gate 4096)
    int lawConstitution; // ob_GESETZBUCH_VERFASSUNG (gate 4096)
    int applyOffice;   // contact_BEWERBEN         (gate 0x8000)
    int citizenship;   // contact_BUERGERRECHT     (office-privilege & status & gate 0x2000)
};

// The office-privilege / player-status inputs the citizenship gate consults.  These come
// from the live office record (dword_12CEAD8) and the player-status array (byte_12CE91D),
// indexed by the active player slot (word_63CC5C).  Modelled as a hook.
struct TownHallLawGate : TownHallGate {
    // (dword_12CEAD8[active] & 0x4000) — the office grants the citizenship privilege.
    virtual bool OfficeGrantsCitizenship() { return false; }
    // (byte_12CE91D[active] == 1) — the player slot is in the citizenship-eligible state.
    virtual bool PlayerCitizenshipState() { return false; }
};

// Register the law/apply loop's entry set for this frame.  `registerEntry` mocks the
// StatusText_Register leaf.  1:1 with 0x52036c (entry order: tax/criminal/constitution
// law books, then apply, then citizenship).
TownHallLawIds TownHall_BuildLawAndApply(TownHallLawGate& gate);

// Dispatch a clicked entry id for the law/apply loop.  Returns true if handled.
bool TownHall_DispatchLawAndApply(int clicked, const TownHallLawIds& ids,
                                  int selectedObj, TownHallSink& sink);

// ===========================================================================
// VIBE_TownHall_ShowCitizenshipDialog  0x51f81c — buy-citizenship flow.
//
// Pure rule core extracted from the dialog body:
//   * Eligibility: the player's held currency (VIBE_Person_SumCurrencyHeld over the
//     family record) must be >= 16000; otherwise show "too poor" (msg 5647) and abort.
//   * Grant: when the player confirms, deduct 16000, schedule the citizenship to take
//     effect after a delay (tutorial-active: exactly +5 months; otherwise +(rand%3+1)
//     months) and queue the grant command, then fan the same grant out to every
//     co-resident family member whose status byte is 6 or 7.
// The UI shell (form, message boxes, frame loop) and the command queue are hooks.
// ===========================================================================

// Inputs/dependencies for the citizenship rule core.  Defaults make the gate fail
// closed (no money), so a test must supply them.
struct CitizenshipDeps {
    virtual ~CitizenshipDeps() = default;
    // VIBE_Person_SumCurrencyHeld — total liquid wealth of the active player's family.
    virtual int  PlayerWealth() { return 0; }
    // VIBE_Tutorial_IsInactive — true when NOT in a tutorial (free-play timing).
    virtual bool TutorialInactive() { return true; }
    // VIBE_Math_RandomModulo(3) — 0..2 (free-play grant delay = result+1 months).
    virtual int  RandomModulo3() { return 0; }
    // The confirm button (dword_75BF38 == 1210) was pressed AND the resource-amount
    // check (VIBE_Dialog_CheckResourceAmount(16000)) passed.
    virtual bool ConfirmAndAfford() { return false; }
    // byte_63CC1D — the MAX number of family-member grants to emit.  The scan walks 768
    // family-record slots (kFamilyMemberScanMax); it stops once this many grants have been
    // emitted.  1:1 with the loop's `if (byte_63CC1D <= v48) break;`.
    virtual int  FamilyGrantCap() { return 0; }
    // byte_12CE912[536 * i] — status byte of the i-th scanned family slot (6/7 qualifies).
    virtual int  FamilyMemberStatus(int /*i*/) { return 0; }
    // True when the i-th family member is a *different* person than the buyer (v18!=v50).
    virtual bool FamilyMemberIsOther(int /*i*/) { return true; }
};

// The command queue the grant emits into.  Each call names the original it forwards to.
struct CitizenshipCommandSink {
    virtual ~CitizenshipCommandSink() = default;
    // VIBE_Command_QueueRequestSlotReset28 — schedule the citizenship grant record for a
    // person, with the computed effective game-date.  `months` is the delay applied.
    virtual void ScheduleGrant(int /*personId*/, int months) { (void)months; }
    // VIBE_Command_QueueRequest16(officeObj, buyerId, price, 0) — the payment request.
    virtual void QueuePayment(int /*officeObj*/, int /*buyerId*/, int /*price*/) {}
    // VIBE_Command_QueueRequestArgs25(buyerId, opcode, 0, 4, flag) — privilege request.
    virtual void QueuePrivilege(int /*buyerId*/, int /*opcode*/, int /*flag*/) {}
    // VIBE_Interaction_DispatchPanelEvent(0x2E, ...) — panel-refresh event.
    virtual void DispatchPanelEvent(int /*code*/, int /*arg*/) {}
};

// Result of the citizenship rule core.
struct CitizenshipOutcome {
    bool eligible = false;   // wealth gate passed (>= 16000)
    bool granted  = false;   // a confirm+afford produced a grant this run
    int  grantMonths = 0;    // the delay applied to the grant date (5, or rand%3+1)
};

// Run the citizenship eligibility gate (the wealth check at the top of 0x51f81c).
// Returns true if the buy flow may proceed; false means "too poor" (msg 5647).
bool TownHall_CitizenshipEligible(CitizenshipDeps& deps);

// Run the citizenship grant once the player confirms (the dword_75BF38==1210 block).
// Deducts the price, computes the grant delay, queues the buyer's grant + payment +
// privilege, then fans the grant out to co-resident family members (status 6/7).
// Returns the outcome.  The buyer's person id and the office object handle are passed in.
// 1:1 with the confirm branch of 0x51f81c.
CitizenshipOutcome TownHall_GrantCitizenship(CitizenshipDeps& deps,
                                             CitizenshipCommandSink& sink,
                                             int buyerPersonId, int officeObj);

// ===========================================================================
// VIBE_TownHall_ShowBuildingInfoDialog  0x51f2e8 — buy-municipal-building list.
//
// Builds a list of purchasable buildings: it iterates the active player's persons,
// and for each person whose flag byte (+90) has bit 2 set, looks up the matching
// building handler and adds a row; the buy gate is a resource-amount check
// (VIBE_Dialog_CheckResourceAmount on the building's price field +176) plus a confirm
// message box, after which it enqueues the purchase.  The list build + buy gate are the
// pure logic; the form/iteration leaves are hooks.
// ===========================================================================

struct BuildingInfoRow {
    int personFlagByte;   // *(person+90); row added only if (byte & 2)
    int buildingHandle;   // resolved handler ((_DWORD*)handler+43 == person key)
    int price;            // *(handler+176) — checked vs player resources; deducted on buy
    int buildingObj;      // the building object the purchase is enqueued for
    bool ownedByActive;   // *(building+39) == active-player slot (excluded from buy)
};

struct BuildingInfoDeps {
    virtual ~BuildingInfoDeps() = default;
    // VIBE_Dialog_CheckResourceAmount(price, resourceKind) — can the player afford it.
    virtual bool CanAfford(int /*price*/) { return false; }
    // VIBE_Dialog_ShowMessageBox(...) confirm — the player clicked "yes, buy".
    virtual bool ConfirmBuy(int /*price*/) { return false; }
};

struct BuildingInfoBuySink {
    virtual ~BuildingInfoBuySink() = default;
    // VIBE_Building_EnqueueBuyBuilding(buildingObj, activePlayer, handler, price, ...).
    virtual void EnqueueBuy(int /*buildingObj*/, int /*handler*/, int /*price*/) {}
};

// Decide whether a candidate building row is offered in the list.
// 1:1 with the inner filter of 0x51f2e8: the person's +90 flag must have bit 2 set
// (a matching handler must also exist, which the caller resolves).
bool TownHall_BuildingRowOffered(const BuildingInfoRow& row);

// Decide whether a row is buyable this frame and, if so, run the buy.  The original
// only offers the buy when the building is NOT already owned by the active player
// (*(building+39) != active slot) and the player can afford it; a confirm box then
// enqueues the purchase.  Returns true if a purchase was enqueued.
// 1:1 with the buy block of 0x51f2e8.
bool TownHall_TryBuyBuilding(const BuildingInfoRow& row, BuildingInfoDeps& deps,
                             BuildingInfoBuySink& sink);

} // namespace guild::world

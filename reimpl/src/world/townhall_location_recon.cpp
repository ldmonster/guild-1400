// guild::world — Rathaus (town-hall) interaction rule-cores.  See header for the map.
// Reconstructed 1:1 from gilde.exe; addresses on each function.
#include "world/townhall_location_recon.h"

namespace guild::world {

// ===========================================================================
// gilde.exe 0x51f70c — VIBE_TownHall_RunContactDispatchLoop  (entry-set half)
//
//   if (TestHandlerFlagWord(512))  buyBuilding = Register("contact_GEB_KAUFEN",16,...);
//   else                           buyBuilding = 0;
//   if (TestHandlerFlagWord(1024)) agenda      = Register("contact_TAGESORDNUNG",22,...);
//   else                           agenda      = 0;
//   if (TestHandlerFlagWord(2048)) officeInfo  = Register("contact_AMT_INFO",22,...);
//   else                           officeInfo  = 0;
// ===========================================================================
TownHallContactIds TownHall_BuildContactDispatch(TownHallGate& gate) {
    TownHallContactIds ids{0, 0, 0};
    // Entry ids are produced by the StatusText_Register leaf; here we model the leaf as
    // "present entries get a distinct nonzero id".  Tests inject ids via a gate that
    // also overrides this by subclassing; the default assigns sequential sentinels so
    // the dispatch chain can be exercised without the gui tree.
    int next = 1;
    if (gate.HandlerFlag(townhall::kFlagBuyBuilding)) ids.buyBuilding = next++;
    if (gate.HandlerFlag(townhall::kFlagAgenda))      ids.agenda      = next++;
    if (gate.HandlerFlag(townhall::kFlagOfficeInfo))  ids.officeInfo  = next++;
    return ids;
}

// gilde.exe 0x51f70c — the dword_63C7B8 statistics-overlay side effect.
//   if (statObjActive) {
//       if (byte_67225C == 47) Statistics_ShowGeneralWindow(...);
//       if (byte_67225C == 20) Statistics_ShowTaxWindow(...);
//   }
void TownHall_RunStatisticsOverlay(TownHallGate& gate, int selectedObj, TownHallSink& sink) {
    if (gate.StatObjectActive()) {
        if (gate.StatObjectOpcode() == townhall::kStatObjGeneral)
            sink.ShowStatisticsGeneral(selectedObj);
        if (gate.StatObjectOpcode() == townhall::kStatObjTax)
            sink.ShowStatisticsTax(selectedObj);
    }
}

// gilde.exe 0x51f70c — dispatch chain.
//   if (clicked) {
//       if (clicked == buyBuilding) ShowBuildingInfoDialog(...);
//       else if (clicked == agenda)     Amt_RunCandidateWindowVariantB(...);
//       else if (clicked == officeInfo) Amt_RunCandidateWindowVariantA(...);
//   }
// Note: the original first compares against buyBuilding (v2), then agenda (v3), then
// officeInfo (v4); a 0 id never matches because clicked is guaranteed nonzero here.
bool TownHall_DispatchContact(int clicked, const TownHallContactIds& ids,
                              int selectedObj, TownHallSink& sink) {
    if (clicked == 0)
        return false;
    if (clicked == ids.buyBuilding) { sink.ShowBuildingInfoDialog(selectedObj); return true; }
    if (clicked == ids.agenda)      { sink.RunAgendaWindow(selectedObj);        return true; }
    if (clicked == ids.officeInfo)  { sink.RunOfficeInfoWindow(selectedObj);    return true; }
    return false;
}

// ===========================================================================
// gilde.exe 0x52036c — VIBE_TownHall_RunLawAndApplyLoop  (entry-set half)
//
//   if (TestHandlerFlagWord(4096)) {
//       lawTax        = Register("ob_GESETZBUCH_STEUERN",12,...);
//       lawCriminal   = Register("ob_GESETZBUCH_STRAFRECHT",12,...);
//       lawConstitution = Register("ob_GESETZBUCH_VERFASSUNG",12,...);
//   } else { lawConstitution = lawCriminal = lawTax = 0; }
//   if (TestHandlerFlagWord(0x8000)) applyOffice = Register("contact_BEWERBEN",22,...);
//   else                             applyOffice = 0;
//   if ((office[active] & 0x4000) && (status[active] == 1) && TestHandlerFlagWord(0x2000))
//        citizenship = Register("contact_BUERGERRECHT",22,...);
//   else citizenship = 0;
//
// Original registration order: tax, criminal, constitution.  (Note the original stores
// them as a3=tax, v4=criminal, v5=constitution.)
// ===========================================================================
TownHallLawIds TownHall_BuildLawAndApply(TownHallLawGate& gate) {
    TownHallLawIds ids{0, 0, 0, 0, 0};
    int next = 1;
    if (gate.HandlerFlag(townhall::kFlagLawBooks)) {
        ids.lawTax          = next++;
        ids.lawCriminal     = next++;
        ids.lawConstitution = next++;
    }
    if (gate.HandlerFlag(townhall::kFlagApplyOffice))
        ids.applyOffice = next++;
    if (gate.OfficeGrantsCitizenship() && gate.PlayerCitizenshipState() &&
        gate.HandlerFlag(townhall::kFlagCitizenship))
        ids.citizenship = next++;
    return ids;
}

// gilde.exe 0x52036c — dispatch chain.
//   if (clicked) {
//       if      (clicked == lawTax)          OpenLawBookSection(0x112);
//       else if (clicked == lawCriminal)     OpenLawBookSection(0x111);
//       else if (clicked == lawConstitution) OpenLawBookSection(0x113);
//       else if (clicked == citizenship)     ShowCitizenshipDialog(...);
//       else if (clicked == applyOffice)     ShowApplicationDialog(...);
//   }
// (The original tests a3(tax), v4(criminal), v5(constitution), v7(citizenship),
//  v6(apply) in that order.)
bool TownHall_DispatchLawAndApply(int clicked, const TownHallLawIds& ids,
                                  int selectedObj, TownHallSink& sink) {
    if (clicked == 0)
        return false;
    if (clicked == ids.lawTax)          { sink.OpenLawBookSection(townhall::kLawSectionTax);          return true; }
    if (clicked == ids.lawCriminal)     { sink.OpenLawBookSection(townhall::kLawSectionCriminal);     return true; }
    if (clicked == ids.lawConstitution) { sink.OpenLawBookSection(townhall::kLawSectionConstitution); return true; }
    if (clicked == ids.citizenship)     { sink.ShowCitizenshipDialog(selectedObj);                    return true; }
    if (clicked == ids.applyOffice)     { sink.ShowApplicationDialog(selectedObj);                    return true; }
    return false;
}

// ===========================================================================
// gilde.exe 0x51f81c — VIBE_TownHall_ShowCitizenshipDialog
// ===========================================================================

// Eligibility gate (top of the function):
//   VIBE_Person_SumCurrencyHeld(family);
//   if (wealth < 16000) { RenderFormattedMessage(5647); ShowMessageBox(); abort; }
bool TownHall_CitizenshipEligible(CitizenshipDeps& deps) {
    return deps.PlayerWealth() >= townhall::kCitizenshipPrice;  // (v6 < 16000) -> ineligible
}

// Confirm/grant branch (dword_75BF38 == 1210 && CheckResourceAmount(16000)):
//   * compute the grant delay:
//       if (Tutorial_IsInactive()) { months = 0; delay = (RandomModulo(3)+1) months; }
//       else                       { months = 5; delay = 0 (immediate +5-month bucket); }
//     (original: VIBE_GameTime_Advance(date, v17, 0, v16) with
//        inactive: v16=0, v17=rand%3+1 ; active: v16=5, v17=0)
//   * schedule the buyer's grant for that date (QueueRequestSlotReset28);
//   * for each family member [0..byte_63CC1D): if status==6||7 and it's a different
//     person, schedule the same grant (QueueRequestSlotReset28);
//   * QueueRequest16(office, buyer, 16000, 0)   — payment
//   * QueueRequestArgs25(buyer, 456, 0, 4, 0x4000) — privilege
//   * DispatchPanelEvent(0x2E, ...)
//
// We reproduce the EXACT month arithmetic.  In free-play the call is
// GameTime_Advance(date, rand%3+1, 0, 0): the second arg (v17) is the months added.
// In tutorial it is GameTime_Advance(date, 0, 0, 5): the fourth arg (v16) is added.
// Both resolve to a number of months added to the grant's effective date; we expose it
// as `grantMonths` (= rand%3+1 in free-play, = 5 in tutorial).
CitizenshipOutcome TownHall_GrantCitizenship(CitizenshipDeps& deps,
                                             CitizenshipCommandSink& sink,
                                             int buyerPersonId, int officeObj) {
    CitizenshipOutcome out{};
    out.eligible = TownHall_CitizenshipEligible(deps);
    if (!out.eligible)
        return out;
    if (!deps.ConfirmAndAfford())
        return out;

    // Grant-delay months.
    int months;
    if (deps.TutorialInactive())
        months = deps.RandomModulo3() + 1;   // free-play: rand%3 + 1
    else
        months = 5;                          // tutorial:  fixed +5
    out.grantMonths = months;

    // Buyer's grant first.
    sink.ScheduleGrant(buyerPersonId, months);

    // Fan out to co-resident family members with status 6 or 7 (excluding the buyer).
    //
    // 1:1 with the original loop (0x51fa37): the scan index v20 runs over [0,768); the
    // emitted-grant counter v48 starts at 0 (the buyer's grant above does NOT increment
    // it).  At the top of every iteration the loop breaks when byte_63CC1D <= v48 — i.e.
    // byte_63CC1D is the MAX number of family grants to emit, not the number of slots to
    // scan.  A slot contributes a grant only when its status byte is 6 or 7 and it is a
    // different person than the buyer (v18 != v50); v48 is bumped only then.
    const int grantCap = deps.FamilyGrantCap();   // byte_63CC1D
    int emitted = 0;                              // v48
    for (int i = 0; i < townhall::kFamilyMemberScanMax; ++i) {  // v20 < 768
        if (grantCap <= emitted)                  // if (byte_63CC1D <= v48) break;
            break;
        int st = deps.FamilyMemberStatus(i);
        if ((st == townhall::kMemberStatusA || st == townhall::kMemberStatusB) &&
            deps.FamilyMemberIsOther(i)) {
            sink.ScheduleGrant(/*personId placeholder, resolved by deps in real wiring*/ i, months);
            ++emitted;
        }
    }

    // Payment + privilege + panel refresh.
    sink.QueuePayment(officeObj, buyerPersonId, townhall::kCitizenshipPrice);
    sink.QueuePrivilege(buyerPersonId, townhall::kCitizenshipReqOpcode, townhall::kCitizenshipFlagArg);
    sink.DispatchPanelEvent(townhall::kCitizenshipPanelEvent, 0);

    out.granted = true;
    return out;
}

// ===========================================================================
// gilde.exe 0x51f2e8 — VIBE_TownHall_ShowBuildingInfoDialog
// ===========================================================================

// List-build filter: a person contributes a row only if (*(person+90) & 2) and a
// matching building handler exists (the He_FindFirstHandlerByFilter loop; the caller
// supplies the resolved handle).
bool TownHall_BuildingRowOffered(const BuildingInfoRow& row) {
    return (row.personFlagByte & 2) != 0;   // (Begin[90] & 2) != 0
}

// Buy gate: a row is buyable only if the building is not already owned by the active
// player (*(building+39) != active slot) and the player can afford it; a confirm box
// then enqueues the purchase.
//   if (selected == row && *(building+39) != active) {
//       if (CheckResourceAmount(price)) {
//           RenderFormattedMessage(5102, ...);
//           if (ShowMessageBox(...,1)) EnqueueBuyBuilding(...);
//       }
//   }
bool TownHall_TryBuyBuilding(const BuildingInfoRow& row, BuildingInfoDeps& deps,
                             BuildingInfoBuySink& sink) {
    if (row.ownedByActive)            // *(building+39) == active slot -> not offered
        return false;
    if (!deps.CanAfford(row.price))   // VIBE_Dialog_CheckResourceAmount
        return false;
    if (!deps.ConfirmBuy(row.price))  // VIBE_Dialog_ShowMessageBox(...,1)
        return false;
    sink.EnqueueBuy(row.buildingObj, row.buildingHandle, row.price);
    return true;
}

} // namespace guild::world

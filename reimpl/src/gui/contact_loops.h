#pragma once
// guild::gui — the remaining in-world "contact menu" / right-click action-menu *run loops*
// (the modal frame loops that build the entry set, render the selected-object overlay, and
// dispatch the clicked entry to a game action).  These are distinct from contact_menu.{h,cpp}
// and contact_actions.{h,cpp} (which translated the per-trade *builders*): this module
// translates the full RunFrameLoop dispatchers that wire those entry sets to actions, plus
// a few more standalone action menus (robber hideout, sabotage, guild-master, tavern).
//
// Every loop follows the shared shape (see contact_menu.h):
//   ResetEntries();
//   while (RunFrameLoop(kContactForm, ...)) {
//       if (page-ready flags) Register(...) each entry;        // DATA/LAYOUT
//       UpdateSelectedObjectContact(...);                      // overlay (out of scope)
//       if (clickedEntry) dispatch by id;                      // WIRING
//   }
// and they REUSE the status-text leaves (StatusText_Register / StatusText_ResetEntries /
// kContactForm / the page-flag bits / ContactGate) translated in contact_menu.cpp.
//
// Functions translated here (all VIBE_*):
//   VIBE_Hud_RunErzAbbauContactLoop        @0x511af4  (ore search / mine)
//   VIBE_Hud_RunProductionContactDispatch  @0x511b74  (metal-production hub)
//   VIBE_ContactMenu_RunEmptyLoop          @0x514a34  (no-entry idle loop)
//   VIBE_ContactMenu_RobberHideout         @0x513274  (thieves'-guild hideout, richest)
//   VIBE_ContactMenu_SabotageActions       @0x514d78  (sabotage / beat-up / bribe)
//   VIBE_ContactMenu_GuildMasterActions    @0x515ea4  (negotiate / craft / proof / spy / exam)
//   VIBE_ContactMenu_Tavern                @0x518c50  (dice / regulars' table / dark corner)
//   VIBE_WineCellar_RunContactLoop         @0x519d74  (wine-cabinet buy)
//   VIBE_CityTreasury_RunContactLoop       @0x51f6a8  (guild treasury cash)
//
// As elsewhere, the modal frame loop, the localized-label text engine and the 3D-overlay
// renderer live in io/sim/render and are forward-declared / mocked; mutating actions are
// routed through a command sink (mockable).

#include "gui/contact_menu.h"

namespace guild::gui {

// ---------------------------------------------------------------------------
// Command sink for the verbs these loops dispatch (kept separate from
// ContactCommandSink / ContactActionSink so existing tests are untouched).  Each method
// names the original it forwards to; the selected-object handle / arg is passed through.
// ---------------------------------------------------------------------------
struct ContactLoopSink {
    virtual ~ContactLoopSink() = default;
    // Production / mine verbs (shared by the ore + metal loops):
    virtual void OpenProductionWindowA(int /*obj*/) {} // VIBE_TradePanel_OpenProductionWindowA
    virtual void OpenProductionWindowB(int /*obj*/) {} // VIBE_TradePanel_OpenProductionWindowB
    virtual void OpenProductionWindowC(int /*obj*/) {} // VIBE_TradePanel_OpenProductionWindowC
    virtual void OpenTransport(int /*obj*/) {}         // VIBE_TradeTransport_OpenPanelMode1_Thunk
    virtual void OpenStorage(int /*obj*/) {}           // VIBE_StorageDialog_Options
    virtual void RunFeast(int /*obj*/) {}              // VIBE_Panel_RunGelage
    virtual void RunTraining(int /*obj*/) {}           // VIBE_Panel_RunTrainingSelect
    // Robber-hideout verbs:
    virtual void RunThievesGuildEquipment(int /*obj*/) {} // VIBE_Panel_RunThievesGuildEquipment
    virtual void RobberCampShowBar(int /*obj*/) {}        // VIBE_Dialog_RobberCampShowBar
    virtual void RobberCampCheckAndShow() {}              // VIBE_Dialog_RobberCampCheckAndShow
    virtual void BriberyConfirm() {}                      // VIBE_Dialog_BriberyConfirm
    virtual void RobberRaidConfirm() {}                   // VIBE_Dialog_RobberRaidConfirm
    virtual void ThievesGuildBurglary() {}               // VIBE_Location_ThievesGuildBurglary (ambush)
    // Sabotage verbs:
    virtual void RunSabotage(int /*obj*/) {}        // VIBE_ActionDialog_Sabotage
    virtual void RunBeatUp(int /*obj*/) {}          // VIBE_Amt_RunOfficeOverviewWindow (verprügeln)
    virtual void ShowTalent(int /*which*/) {}       // VIBE_TalentDialog_Show
    virtual void PromptTargetSelect(int /*obj*/) {} // VIBE_ActionDialog_PromptTargetSelect (bribe)
    // Guild-master verbs:
    virtual void EvidenceBrowse(int /*obj*/) {}    // VIBE_EvidenceDialog_Browse
    virtual void ResidenceMasterExam(int /*obj*/) {} // VIBE_Location_ResidenceMasterExam
    virtual void SpionageConfirm() {}              // VIBE_Dialog_SpionageConfirm
    virtual void ResidenceMistress(int /*obj*/) {} // VIBE_Location_ResidenceMistress
    // Tavern verbs:
    virtual void TavernCardGame(int /*obj*/) {}        // VIBE_Location_TavernCardGame
    virtual void TavernStammtisch(int /*obj*/) {}      // VIBE_Dialog_TavernStammtischDispatch
    virtual void TavernDarkCorner(int /*obj*/) {}      // VIBE_Dialog_TavernDarkCornerDispatch
    virtual void TavernQueueComment(int /*obj*/) {}    // VIBE_Command_QueueRequestSlotReset28 (event 18)
    // Misc single-entry loops:
    virtual void WineCellarBuy(int /*obj*/) {}     // VIBE_WineCellar_ShowBuyDialog
    virtual void TreasuryCash(int /*obj*/) {}      // VIBE_GuildTreasury_ShowCashDialog
};
void ContactLoops_SetCommandSink(ContactLoopSink* sink);

// Independent gate for these loops (mirrors ContactMenu_SetGate; used by the handler-flag /
// QueryFind / tutorial / treasury-active predicates).  Default: all gates open / no objects.
//
// Beyond ContactGate's HandlerFlag/HasObject, two loops consult extra predicates:
//   GuildMaster: VIBE_Tutorial_IsInactive() gates the proof/spy/exam group;
//   CityTreasury: byte_12CEA76[536*activeChar] gates whether the treasury entry registers.
struct ContactLoopGate : ContactGate {
    virtual bool TutorialInactive() { return true; } // VIBE_Tutorial_IsInactive
    virtual bool TreasuryActive() { return true; }   // byte_12CEA76[536*activeChar]
};
void ContactLoops_SetGate(ContactLoopGate* gate);

// ---------------------------------------------------------------------------
// One frame's entry set, isolated for testing (the real loops run Build* then Dispatch*
// each frame; the RunFrameLoop, the form-ready flag word and the click handle are owned by
// io/sim).  Each Build* returns the registered entry ids; an id is 0 when the entry was not
// registered (page off, or gated out).  Each Dispatch* maps a clicked handle to its wired
// verb, returning true when the click was handled.  Recovered byte-for-byte per decompile.
// ---------------------------------------------------------------------------

// --- VIBE_Hud_RunErzAbbauContactLoop (0x511af4) — ore search + mine. ---------
// page 0x200: contact_SUCHEN_ERZ (search@22) + contact_ABBAUEN (mine@10).
struct ErzAbbauIds { int search; int mine; };
ErzAbbauIds Hud_BuildErzAbbau(int pageFlags);
bool Hud_DispatchErzAbbau(int clicked, const ErzAbbauIds& e);

// --- VIBE_Hud_RunProductionContactDispatch (0x511b74) — metal-production hub. -
// page 0x200: PRODUKTION_METALL (19), TRANSPORT (21), LAGER (14), GELAGE (22),
//             + training targets ob_STROHPUPPE (53) / ob_ZIELSCHEIBE (52) when present.
struct MetalProdIds {
    int production; int transport; int storage; int feast; int targetA; int targetB;
};
MetalProdIds Hud_BuildProductionMetal(int pageFlags);
bool Hud_DispatchProductionMetal(int clicked, const MetalProdIds& e);

// --- VIBE_ContactMenu_RunEmptyLoop (0x514a34) — registers nothing. -----------
// (Resets entries, then spins RunFrameLoop with no Register calls.)  Modelled as a no-op
// builder so the loop's single behaviour (reset + nothing) is testable.
void ContactMenu_BuildEmpty();

// --- VIBE_ContactMenu_RobberHideout (0x513274) — thieves'-guild hideout. ------
// page 0x200: AUSRUESTEN(13) AUF_LAUER_LEGEN(21) ANGRIFF(23) [ob_STROHPUPPE if 53]
//             [ob_ZIELSCHEIBE if 52] SCHUTZGELD_ERPRESSEN(23) GEBAEUDE_AUSSPIONIEREN(23)
//             RAUBUEBERFALL(23) REGENERATION(23).
// page 0x400: LAGER(14) GELAGE(14) TRANSPORT(21).
struct RobberHideoutIds {
    int equip;     // contact_AUSRUESTEN            -> RunThievesGuildEquipment
    int ambush;    // contact_AUF_LAUER_LEGEN       -> ThievesGuildBurglary (guarded)
    int attack;    // contact_ANGRIFF               -> RobberCampShowBar
    int targetA;   // ob_STROHPUPPE (53)            -> RunTraining
    int targetB;   // ob_ZIELSCHEIBE (52)           -> RunTraining
    int extort;    // contact_SCHUTZGELD_ERPRESSEN  -> RobberCampCheckAndShow
    int spyBuild;  // contact_GEBAEUDE_AUSSPIONIEREN-> BriberyConfirm
    int raid;      // contact_RAUBUEBERFALL         -> RobberRaidConfirm
    int regen;     // contact_REGENERATION          -> RunTraining
    int storage;   // contact_LAGER                 -> OpenStorage
    int feast;     // contact_GELAGE                -> RunFeast
    int transport; // TRANSPORT                     -> OpenTransport
};
RobberHideoutIds ContactMenu_BuildRobberHideout(int pageFlags);
// `byteEvent` is the byte_67225C trailing-trigger (==19 fires a raid each frame); pass 0
// for "no trailing event".
bool ContactMenu_DispatchRobberHideout(int clicked, const RobberHideoutIds& e, int byteEvent);

// --- VIBE_ContactMenu_SabotageActions (0x514d78). ----------------------------
// Every frame: SABOTAGE(23) VERPRUEGELN(13) BEI_NACHT_UND_NEBEL(12) BESTECHUNG(16).
struct SabotageIds { int sabotage; int beatUp; int night; int bribe; };
SabotageIds ContactMenu_BuildSabotage();
bool ContactMenu_DispatchSabotage(int clicked, const SabotageIds& e);

// --- VIBE_ContactMenu_GuildMasterActions (0x515ea4). -------------------------
// if HandlerFlag(8): VERHANDELN(12) HANDWERKSKUNST(12).
// if TutorialInactive: BEWEISBUCH(12) SPIONAGE(23) MEISTERPRUEFUNG(12).
struct GuildMasterIds {
    int negotiate;  // contact_VERHANDELN      -> ShowTalent(0)
    int craft;      // contact_HANDWERKSKUNST  -> ShowTalent(1)
    int proof;      // contact_BEWEISBUCH      -> EvidenceBrowse
    int spy;        // contact_SPIONAGE        -> SpionageConfirm
    int exam;       // contact_MEISTERPRUEFUNG -> ResidenceMasterExam
};
GuildMasterIds ContactMenu_BuildGuildMaster();
// `byteEvent`/`mistressEnabled` model the trailing else-branch: when nothing was clicked
// and mistressEnabled (dword_63C7C0) and byteEvent==45, the mistress action fires.
bool ContactMenu_DispatchGuildMaster(int clicked, const GuildMasterIds& e,
                                     bool mistressEnabled, int byteEvent);

// --- VIBE_ContactMenu_Tavern (0x518c50). -------------------------------------
// Every frame: WUERFELSPIEL(22) STAMMTISCH(22) ob_DUNKLE_ECKE(22)  (+ RegisterEinkaufContact).
struct TavernIds { int dice; int regulars; int darkCorner; };
TavernIds ContactMenu_BuildTavern();
// `byteEvent`/`eventEnabled` model the trailing dword_63C7C0 branch: event 18 queues a
// favour comment, event 23 re-runs the dark-corner dispatch.
bool ContactMenu_DispatchTavern(int clicked, const TavernIds& e,
                                bool eventEnabled, int byteEvent);

// --- VIBE_WineCellar_RunContactLoop (0x519d74). ------------------------------
int  WineCellar_BuildContact();
bool WineCellar_DispatchContact(int clicked, int wineId);

// --- VIBE_CityTreasury_RunContactLoop (0x51f6a8). ----------------------------
// Registers ob_STADTKASSE(22) only when the treasury-active gate holds.
int  CityTreasury_BuildContact();
bool CityTreasury_DispatchContact(int clicked, int treasuryId);

} // namespace guild::gui

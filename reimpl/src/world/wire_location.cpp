// See wire_location.h.  Installs real command-sink implementations into the live
// contact-menu / action-menu DI bridges (gui/contact_actions.*, gui/contact_loops.*).
#include "world/wire_location.h"

#include "gui/contact_actions.h" // ContactActionSink / ContactActions_SetCommandSink / ...SetGate
#include "gui/contact_loops.h"   // ContactLoopSink / ContactLoops_SetCommandSink / ...SetGate
#include "gui/gui_dialogs6.h"    // guild::gui::Panel_RunGelage (0x54e940)

namespace guild::world {

namespace {

// ---------------------------------------------------------------------------
// Real contact-loop command sink.  Overrides only the verbs that have a faithful,
// signature-matching 1:1 reconstruction to call; every other verb inherits the
// inert no-op default from ContactLoopSink.
// ---------------------------------------------------------------------------
struct RealContactLoopSink : guild::gui::ContactLoopSink {
    // VIBE_Panel_RunGelage @0x54e940 — exact entry point: int Panel_RunGelage(int).
    // Bound to the "feast/party" verb dispatched by the robber-hideout and
    // metal-production loops.  Panel_RunGelage runs its full reconstructed modal
    // loop over its own (inert) gui_dialogs6 hook table, so this is headless-safe.
    void RunFeast(int obj) override { guild::gui::Panel_RunGelage(obj); }

    // Verbs left inert (no monolithic signature-matching reconstruction to bind;
    // reconstructed elsewhere only as Build/Dispatch rule-cores or rule kernels):
    //   OpenProductionWindowA/B/C  VIBE_TradePanel_OpenProductionWindow{A,B,C}
    //   OpenTransport              VIBE_TradeTransport_OpenPanelMode1_Thunk (void*, not int)
    //   OpenStorage                VIBE_StorageDialog_Options            0x5461b0
    //   RunTraining                VIBE_Panel_RunTrainingSelect          0x550adc
    //   RunThievesGuildEquipment   VIBE_Panel_RunThievesGuildEquipment   0x550310
    //   RobberCampShowBar          VIBE_Dialog_RobberCampShowBar
    //   RobberCampCheckAndShow     VIBE_Dialog_RobberCampCheckAndShow
    //   BriberyConfirm             VIBE_Dialog_BriberyConfirm  (recon takes int target)
    //   RobberRaidConfirm          VIBE_Dialog_RobberRaidConfirm (recon takes int target)
    //   ThievesGuildBurglary       VIBE_Location_ThievesGuildBurglary
    //   RunSabotage                VIBE_ActionDialog_Sabotage
    //   RunBeatUp                  VIBE_Amt_RunOfficeOverviewWindow
    //   ShowTalent                 VIBE_TalentDialog_Show  (Build/Dispatch core)  0x546ce8
    //   PromptTargetSelect         VIBE_ActionDialog_PromptTargetSelect
    //   EvidenceBrowse             VIBE_EvidenceDialog_Browse (Build/Dispatch core) 0x548168
    //   ResidenceMasterExam        VIBE_Location_ResidenceMasterExam (rule kernel) 0x5159fc
    //   SpionageConfirm            VIBE_Dialog_SpionageConfirm
    //   ResidenceMistress          VIBE_Location_ResidenceMistress       0x515524
    //   TavernCardGame             VIBE_Location_TavernCardGame (rule kernel) 0x516b78
    //   TavernStammtisch           VIBE_Dialog_TavernStammtischDispatch
    //   TavernDarkCorner           VIBE_Dialog_TavernDarkCornerDispatch
    //   TavernQueueComment         VIBE_Command_QueueRequestSlotReset28
    //   WineCellarBuy              VIBE_WineCellar_ShowBuyDialog (rule core)  0x519b14
    //   TreasuryCash               VIBE_GuildTreasury_ShowCashDialog (rule core) 0x51f608
};

// ---------------------------------------------------------------------------
// Real contact-action command sink.  No verb here has a faithful, signature-matching
// monolithic reconstruction (all of OpenProductionPanel / OpenStorage / OpenTransport /
// RunStaffBook / RunMasterCertificate / RunSearchExport / RunSearchImport / AbductTargetPick /
// FreePrisonerPick / ShowTalent / InviteGuests / AbductChooseDestination / ResidenceMistress
// are reconstructed as Build/Dispatch rule-cores or take non-matching args), so this sink
// inherits all inert defaults.  It is installed anyway so the live bridge no longer points at
// the library's anonymous default instance — keeping the install surface explicit and ready
// for future leaf bindings (rule 13).
// ---------------------------------------------------------------------------
struct RealContactActionSink : guild::gui::ContactActionSink {
    // All verbs inert by default (see header / .h for the per-verb VIBE_ addresses).
};

} // namespace

void InstallRealLocationWiring() {
    // Process-lifetime sink objects the global bridge pointers reference.
    static RealContactLoopSink   loopSink;
    static RealContactActionSink actionSink;

    guild::gui::ContactLoops_SetCommandSink(&loopSink);
    guild::gui::ContactActions_SetCommandSink(&actionSink);

    // Gates: leave at the permissive default (all open / no extra objects), matching
    // the inert default used by the rest of the live wiring.  The real gate predicates
    // (handler-flag word, tutorial-inactive, treasury-active byte) live in io/sim.
    guild::gui::ContactLoops_SetGate(nullptr);    // -> library default ContactLoopGate
    guild::gui::ContactActions_SetGate(nullptr);  // -> library default ContactGate
}

} // namespace guild::world

#pragma once
// guild::gui — the remaining in-world "contact menu" builders (production trades + the
// social/criminal action menus) not covered by contact_menu.{h,cpp}.
//
// These are the same modal-frame-loop builders described in contact_menu.h: each one
//   1. ResetEntries();
//   2. while (RunFrameLoop(kContactForm, ...)) {
//        if (form-ready page flags) Register(...) each entry;     // DATA/LAYOUT
//        if (clickedEntry) dispatch by id;                        // WIRING
//      }
// and they REUSE the status-text table + register/reset leaves translated in
// contact_menu.cpp (StatusText_Register / StatusText_ResetEntries / kContactForm /
// the page-flag bits / ContactGate).  What this module adds is the entry set + the
// id->action wiring for nine more builders, recovered byte-for-byte from each decompile:
//
//   VIBE_ContactMenu_SmithProduction        @0x513954  (handler-flag-gated production)
//   VIBE_ContactMenu_StonemasonProduction   @0x514c44
//   VIBE_ContactMenu_BreweryProduction      @0x518dc4
//   VIBE_ContactMenu_PerfumeryProduction    @0x5148b0  (+ gather/search entry)
//   VIBE_ContactMenu_MixingProduction       @0x514ac8  (+ gather/search entry)
//   VIBE_ContactMenu_FlytrapTerrariumPond   @0x514a50  (collector objects -> search import)
//   VIBE_ContactMenu_ThreatLetterRhetoric   @0x514f60  (threat / pamphlet / rhetoric)
//   VIBE_ContactMenu_FeastFightSlander      @0x515044  (feast / fight / slander)
//   VIBE_ContactMenu_Mistress               @0x5159b4  (single "mistress" entry)
//
// Mutating actions are routed through a command sink (mockable); the frame loop, the
// localized-label text engine and the 3D overlay are forward-declared / mocked, exactly
// as in contact_menu.{h,cpp}.

#include "gui/contact_menu.h"

namespace guild::gui {

// ---------------------------------------------------------------------------
// Command sink for the extra actions these builders dispatch (the production
// builders also use the storage/transport/production/books verbs from
// ContactCommandSink, kept separate so existing tests are untouched).
// ---------------------------------------------------------------------------
struct ContactActionSink {
    virtual ~ContactActionSink() = default;
    // Production-builder verbs (shared by Smith/Stonemason/Brewery/Perfumery/Mixing):
    virtual void OpenProductionPanel(int /*obj*/) {}     // VIBE_TradePanel_BuildProductionWindow
    virtual void OpenStorage(int /*obj*/) {}             // VIBE_StorageDialog_Options
    virtual void OpenTransport(int /*obj*/) {}           // VIBE_TradeTransport_OpenPanelMode1
    virtual void RunStaffBook(int /*obj*/) {}            // VIBE_Personnel_RunStaffBook
    virtual void RunMasterCertificate(int /*obj*/) {}    // VIBE_Meister_RunMasterCertificateDialog
    virtual void RunSearchExport(int /*obj*/) {}         // VIBE_Location_TradeSearchExport (gather)
    virtual void RunSearchImport(int /*obj*/) {}         // VIBE_Location_TradeSearchImport (flytrap)
    // Social / criminal-action verbs:
    virtual void AbductTargetPick(int /*obj*/) {}        // VIBE_ActionDialog_BeginAbductTargetPick
    virtual void FreePrisonerPick(int /*obj*/) {}        // VIBE_ActionDialog_BeginFreePrisonerPick
    virtual void ShowTalent(int /*which*/) {}            // VIBE_TalentDialog_Show
    virtual void InviteGuests(int /*obj*/) {}            // VIBE_FeastDialog_InviteGuests
    virtual void AbductChooseDestination(int /*obj*/) {} // VIBE_ActionDialog_AbductChooseDestination
    virtual void ResidenceMistress(int /*obj*/) {}       // VIBE_Location_ResidenceMistress
};
void ContactActions_SetCommandSink(ContactActionSink* sink);

// Independent handler-flag gate for these builders (mirrors ContactMenu_SetGate; used by
// the Smith builder's per-entry gating).  Default: all gates open.
void ContactActions_SetGate(ContactGate* gate);

// ---------------------------------------------------------------------------
// Production builders.
// Each shares the standard production entry set; the per-trade differences are only the
// localized "production" label name + (for Smith) handler-flag gating + (for
// Perfumery/Mixing) an extra gather/search entry.  Returns the registered entry ids.
// ---------------------------------------------------------------------------

// The production-builder entry-id set.  An id is 0 when the entry was not registered
// (either gated off by the handler flag, or the page is not on this frame).
struct ProdEntries {
    int production;  // localized PRODUKTION_* -> OpenProductionPanel
    int storage;     // contact_LAGER         -> OpenStorage
    int transport;   // contact_TRANSPORT     -> OpenTransport
    int staffBook;   // ob_PERSONALBUCH       -> RunStaffBook
    int masterCert;  // ob_MEISTERBRIEF       -> RunMasterCertificate
    int gather;      // contact_SAMMELN       -> RunSearchExport  (perfumery/mixing only)
};

// gilde.exe 0x513954 — Smith: every entry is gated behind a handler-flag (the gate hook),
// using its per-flag mask (production 32, master-cert 128, storage 16, transport 256,
// staff 64).  Pages: 0x200 = production+master, 0x400 = storage/transport/staff.
ProdEntries ContactMenu_BuildSmith(int pageFlags);
// gilde.exe 0x514c44 — Stonemason  (ungated; page 0x200 production, 0x400 the rest).
ProdEntries ContactMenu_BuildStonemason(int pageFlags);
// gilde.exe 0x518dc4 — Brewery     (ungated; same page split as Stonemason).
ProdEntries ContactMenu_BuildBrewery(int pageFlags);
// gilde.exe 0x5148b0 — Perfumery   (page 0x200 adds the gather + production entries).
ProdEntries ContactMenu_BuildPerfumery(int pageFlags);
// gilde.exe 0x514ac8 — Mixing      (page 0x200 adds the gather + production entries).
ProdEntries ContactMenu_BuildMixing(int pageFlags);

// Localized production-label names used by each builder (so callers/tests reproduce the
// exact per-trade entry).
inline constexpr const char* kProdSchmieden  = "contact_PRODUKTION_SCHMIEDEN";
inline constexpr const char* kProdSteinmetz  = "contact_PRODUKTION_STEINMETZ";
inline constexpr const char* kProdBrauen     = "contact_PRODUKTION_BRAUEN";
inline constexpr const char* kProdParfumerie = "contact_PRODUKTION_PARFUMERIE";
inline constexpr const char* kProdMischen    = "contact_PRODUKTION_MISCHEN";

// Shared production-dispatch: maps a clicked id to its wired verb.  Returns true when the
// click was handled.
bool ContactMenu_DispatchProductionEx(int clicked, const ProdEntries& e);

// ---------------------------------------------------------------------------
// FlytrapTerrariumPond (0x514a50) — three collector objects; clicking the pond runs the
// search-import action.  Page 0x200.
// ---------------------------------------------------------------------------
struct FlytrapEntries { int flytrap; int terrarium; int pond; };
FlytrapEntries ContactMenu_BuildFlytrap(int pageFlags);
bool ContactMenu_DispatchFlytrap(int clicked, const FlytrapEntries& e);

// ---------------------------------------------------------------------------
// ThreatLetterRhetoric (0x514f60) — threat-letter / pamphlet / rhetoric (talent 4).
// (No page gate; entries register every frame.)
// ---------------------------------------------------------------------------
struct ThreatEntries { int threat; int pamphlet; int rhetoric; };
ThreatEntries ContactMenu_BuildThreat();
bool ContactMenu_DispatchThreat(int clicked, const ThreatEntries& e);

// ---------------------------------------------------------------------------
// FeastFightSlander (0x515044) — feast / fight (talent 3) / slander.
// ---------------------------------------------------------------------------
struct FeastEntries { int feast; int fight; int slander; };
FeastEntries ContactMenu_BuildFeast();
bool ContactMenu_DispatchFeast(int clicked, const FeastEntries& e);

// ---------------------------------------------------------------------------
// Mistress (0x5159b4) — a single "mistress" entry -> residence-mistress action.
// ---------------------------------------------------------------------------
int  ContactMenu_BuildMistress();
bool ContactMenu_DispatchMistress(int clicked, int mistressId);

} // namespace guild::gui

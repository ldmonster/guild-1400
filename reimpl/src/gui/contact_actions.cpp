#include "gui/contact_actions.h"

namespace guild::gui {

// Reuse the status-text leaves + gate from contact_menu.cpp (same cluster).
//   StatusText_Register / StatusText_ResetEntries — declared in contact_menu.h.
// The handler-flag gate (VIBE_Interaction_TestHandlerFlagWord) is modelled by a
// ContactGate (also declared in contact_menu.h); we keep an independent instance here
// so Smith's per-entry gating is testable without disturbing contact_menu's gate.

namespace {

ContactActionSink  g_defaultSink;
ContactActionSink* g_sink = &g_defaultSink;

ContactGate  g_defaultGate;
ContactGate* g_gate = &g_defaultGate;

} // namespace

void ContactActions_SetCommandSink(ContactActionSink* sink) {
    g_sink = sink ? sink : &g_defaultSink;
}

// A module-local gate setter (mirrors ContactMenu_SetGate but for these builders).
void ContactActions_SetGate(ContactGate* gate) {
    g_gate = gate ? gate : &g_defaultGate;
}

// ===========================================================================
// Production builders.
//
// The five production builders share the same shape.  Page 0x200 registers the
// localized production entry (gfx 19) and the master-certificate entry (Smith only
// registers master on page 0x200; the others register it on page 0x400); the
// perfumery/mixing variants additionally register a "gather/search" entry (gfx 22).
// Page 0x400 registers storage (14) / transport (21) / staff-book (12) (and, for the
// non-Smith trades, master-certificate (22)).  Recovered per-decompile below.
// ===========================================================================

// gilde.exe 0x513954 — Smith.  Every Register() is gated behind a handler-flag.
//   page 0x200: if flag(32) production; if flag(128) master.
//   page 0x400: if flag(16) storage; if flag(256) transport; if flag(64) staff.
ProdEntries ContactMenu_BuildSmith(int pageFlags) {
    ProdEntries e{};
    if (pageFlags & kFormFlagPage200) {
        e.production = g_gate->HandlerFlag(32)
                         ? StatusText_Register(kProdSchmieden, 19, nullptr) : 0;
        e.masterCert = g_gate->HandlerFlag(128)
                         ? StatusText_Register("ob_MEISTERBRIEF", 22, nullptr) : 0;
    }
    if (pageFlags & kFormFlagPage400) {
        e.storage   = g_gate->HandlerFlag(16)
                        ? StatusText_Register("contact_LAGER", 14, nullptr) : 0;
        e.transport = g_gate->HandlerFlag(256)
                        ? StatusText_Register("contact_TRANSPORT", 21, nullptr) : 0;
        e.staffBook = g_gate->HandlerFlag(64)
                        ? StatusText_Register("ob_PERSONALBUCH", 12, nullptr) : 0;
    }
    return e;
}

// gilde.exe 0x514c44 — Stonemason.  Ungated.
//   page 0x200: production (19).
//   page 0x400: master (22), storage (14), transport (21), staff (12).
ProdEntries ContactMenu_BuildStonemason(int pageFlags) {
    ProdEntries e{};
    if (pageFlags & kFormFlagPage200)
        e.production = StatusText_Register(kProdSteinmetz, 19, nullptr);
    if (pageFlags & kFormFlagPage400) {
        e.masterCert = StatusText_Register("ob_MEISTERBRIEF", 22, nullptr);
        e.storage    = StatusText_Register("contact_LAGER", 14, nullptr);
        e.transport  = StatusText_Register("contact_TRANSPORT", 21, nullptr);
        e.staffBook  = StatusText_Register("ob_PERSONALBUCH", 12, nullptr);
    }
    return e;
}

// gilde.exe 0x518dc4 — Brewery.  Ungated; same page split as Stonemason (master/staff/
// transport/storage on page 0x400, production on page 0x200).
ProdEntries ContactMenu_BuildBrewery(int pageFlags) {
    ProdEntries e{};
    if (pageFlags & kFormFlagPage200)
        e.production = StatusText_Register(kProdBrauen, 19, nullptr);
    if (pageFlags & kFormFlagPage400) {
        e.masterCert = StatusText_Register("ob_MEISTERBRIEF", 22, nullptr);
        e.staffBook  = StatusText_Register("ob_PERSONALBUCH", 12, nullptr);
        e.transport  = StatusText_Register("contact_TRANSPORT", 21, nullptr);
        e.storage    = StatusText_Register("contact_LAGER", 14, nullptr);
    }
    return e;
}

// gilde.exe 0x5148b0 — Perfumery.  Page 0x200 adds the gather entry (22) + production (19);
// page 0x400: storage (14), transport (21), master (22), staff (12).
ProdEntries ContactMenu_BuildPerfumery(int pageFlags) {
    ProdEntries e{};
    if (pageFlags & kFormFlagPage200) {
        e.gather     = StatusText_Register("contact_SAMMELN", 22, nullptr);
        e.production = StatusText_Register(kProdParfumerie, 19, nullptr);
    }
    if (pageFlags & kFormFlagPage400) {
        e.storage    = StatusText_Register("contact_LAGER", 14, nullptr);
        e.transport  = StatusText_Register("contact_TRANSPORT", 21, nullptr);
        e.masterCert = StatusText_Register("ob_MEISTERBRIEF", 22, nullptr);
        e.staffBook  = StatusText_Register("ob_PERSONALBUCH", 12, nullptr);
    }
    return e;
}

// gilde.exe 0x514ac8 — Mixing.  Same as Perfumery but the production label differs and the
// page 0x400 master/staff are registered before storage/transport.
ProdEntries ContactMenu_BuildMixing(int pageFlags) {
    ProdEntries e{};
    if (pageFlags & kFormFlagPage200) {
        e.gather     = StatusText_Register("contact_SAMMELN", 22, nullptr);
        e.production = StatusText_Register(kProdMischen, 19, nullptr);
    }
    if (pageFlags & kFormFlagPage400) {
        e.masterCert = StatusText_Register("ob_MEISTERBRIEF", 22, nullptr);
        e.staffBook  = StatusText_Register("ob_PERSONALBUCH", 12, nullptr);
        e.storage    = StatusText_Register("contact_LAGER", 14, nullptr);
        e.transport  = StatusText_Register("contact_TRANSPORT", 21, nullptr);
    }
    return e;
}

// Shared wiring (the click-dispatch tail every production builder runs).  The id order
// follows the decompiles' if/else-if chain (production, transport, staff, storage,
// master, then the optional gather/search).
bool ContactMenu_DispatchProductionEx(int clicked, const ProdEntries& e) {
    if (!clicked) return false;
    if (e.production && clicked == e.production) { g_sink->OpenProductionPanel(clicked); return true; }
    if (e.transport && clicked == e.transport)   { g_sink->OpenTransport(clicked); return true; }
    if (e.staffBook && clicked == e.staffBook)   { g_sink->RunStaffBook(clicked); return true; }
    if (e.storage   && clicked == e.storage)     { g_sink->OpenStorage(clicked); return true; }
    if (e.gather    && clicked == e.gather)      { g_sink->RunSearchExport(clicked); return true; }
    if (e.masterCert&& clicked == e.masterCert)  { g_sink->RunMasterCertificate(clicked); return true; }
    return false;
}

// ===========================================================================
// FlytrapTerrariumPond (0x514a50).
//   page 0x200: Register("ob_FLIEGENGLAS",22); Register("ob_TERRARIUM",22);
//               if (Register("ob_TEICH",22) == clicked) SearchImport.
// Only the pond entry is wired (the original tests its register result directly).
// ===========================================================================
FlytrapEntries ContactMenu_BuildFlytrap(int pageFlags) {
    FlytrapEntries e{};
    if (pageFlags & kFormFlagPage200) {
        e.flytrap   = StatusText_Register("ob_FLIEGENGLAS", 22, nullptr);
        e.terrarium = StatusText_Register("ob_TERRARIUM", 22, nullptr);
        e.pond      = StatusText_Register("ob_TEICH", 22, nullptr);
    }
    return e;
}
bool ContactMenu_DispatchFlytrap(int clicked, const FlytrapEntries& e) {
    if (!clicked) return false;
    if (e.pond && clicked == e.pond) { g_sink->RunSearchImport(clicked); return true; }
    return false;
}

// ===========================================================================
// ThreatLetterRhetoric (0x514f60).  Entries register every frame (no page gate).
//   threat   = Register("contact_DROHBRIEF",12) -> AbductTargetPick (+ edge-scroll)
//   pamphlet = Register("contact_PAMPHLET", 12) -> FreePrisonerPick
//   rhetoric = Register("contact_RHETORIK",12) -> ShowTalent(4)
// ===========================================================================
ThreatEntries ContactMenu_BuildThreat() {
    ThreatEntries e{};
    e.threat   = StatusText_Register("contact_DROHBRIEF", 12, nullptr);
    e.pamphlet = StatusText_Register("contact_PAMPHLET", 12, nullptr);
    e.rhetoric = StatusText_Register("contact_RHETORIK", 12, nullptr);
    return e;
}
bool ContactMenu_DispatchThreat(int clicked, const ThreatEntries& e) {
    if (!clicked) return false;
    if (e.threat   && clicked == e.threat)   { g_sink->AbductTargetPick(clicked); return true; }
    if (e.pamphlet && clicked == e.pamphlet) { g_sink->FreePrisonerPick(clicked); return true; }
    if (e.rhetoric && clicked == e.rhetoric) { g_sink->ShowTalent(4); return true; }
    return false;
}

// ===========================================================================
// FeastFightSlander (0x515044).
//   feast   = Register("contact_FESTGEBEN",22)   -> InviteGuests
//   fight   = Register("contact_KAMPF",12)       -> ShowTalent(3)
//   slander = Register("contact_ANSCHWAERZEN",12)-> AbductChooseDestination (+ edge-scroll)
// ===========================================================================
FeastEntries ContactMenu_BuildFeast() {
    FeastEntries e{};
    e.feast   = StatusText_Register("contact_FESTGEBEN", 22, nullptr);
    e.fight   = StatusText_Register("contact_KAMPF", 12, nullptr);
    e.slander = StatusText_Register("contact_ANSCHWAERZEN", 12, nullptr);
    return e;
}
bool ContactMenu_DispatchFeast(int clicked, const FeastEntries& e) {
    if (!clicked) return false;
    if (e.feast   && clicked == e.feast)   { g_sink->InviteGuests(clicked); return true; }
    if (e.fight   && clicked == e.fight)   { g_sink->ShowTalent(3); return true; }
    if (e.slander && clicked == e.slander) { g_sink->AbductChooseDestination(clicked); return true; }
    return false;
}

// ===========================================================================
// Mistress (0x5159b4).  A single entry; click runs the residence-mistress action.
//   v3 = Register("contact_GELIEBTE",22); if (clicked==v3) ResidenceMistress.
// (This builder does NOT call ResetEntries — preserved: it just registers each frame.)
// ===========================================================================
int ContactMenu_BuildMistress() {
    return StatusText_Register("contact_GELIEBTE", 22, nullptr);
}
bool ContactMenu_DispatchMistress(int clicked, int mistressId) {
    if (!clicked) return false;
    if (mistressId && clicked == mistressId) { g_sink->ResidenceMistress(clicked); return true; }
    return false;
}

} // namespace guild::gui

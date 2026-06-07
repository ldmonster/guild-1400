#pragma once
// guild::gui — the in-world "contact menu" / status-text builders.
//
// When the player selects a building or scene object, the game opens a small radial
// "contact menu": a set of clickable status-text entries (each an icon + a localized
// label) overlaid on the selected object.  Each entry is registered into a global
// status-text table; on a click the table reports which entry was hit (dword_631720)
// and the builder dispatches the matching game action (open storage, open a production
// panel, run a transport dialog, etc.).
//
// Two leaf primitives drive every builder:
//   VIBE_StatusText_ResetEntries  @0x4bcc4c — clear the 32-entry status-text table.
//   VIBE_StatusText_Register      @0x4bcc80 — register/look-up one entry by object
//                                             name; returns the entry's object handle
//                                             (its click id), de-duping by name and
//                                             capping at 32 entries.
//
// Each builder (VIBE_ContactMenu_*) is a modal frame loop:
//   1. ResetEntries();
//   2. while (RunFrameLoop(kContactForm, ...)) {
//        if (form-ready flags) Register(...) each entry;       // DATA/LAYOUT
//        update the selected-object overlay;
//        if (clickedEntry) dispatch by id;                     // WIRING
//      }
//
// The modal frame loop, the localized-label text engine and the 3D-object overlay
// renderer live in the io/sim/render clusters and are forward-declared / mocked here;
// what this module owns and translates 1:1 is (a) the status-text table layout and the
// register/reset logic byte-for-byte, and (b) each builder's entry set + the
// entry-id -> action wiring.  Mutating actions are routed through a command sink.

#include "gui/types.h"

namespace guild::gui {

// ---------------------------------------------------------------------------
// Status-text table — gilde.exe dword_11B5220 (and aliased parallel arrays).
//
// 32 entries, 50-dword (200-byte) stride.  Per-entry layout (byte offsets into the
// 200-byte record, recovered from VIBE_StatusText_Register):
//   +0   (dword) object handle      — dword_11B5220[50*i]; 0 == free slot; this value
//                                      is the entry's click id (compared vs dword_631720)
//   +4   (64 b)  object name         — StrNCopyPad, used for de-dup (StrCmp vs +4)
//   +67  (byte)  flag A              — byte_11B5263[200*i] (cleared on register)
//   +68  (dword) gfx / icon id       — dword_11B5264[50*i]
//   +72  (128 b) localized label     — dword_11B5220[50*i + 18] (StrNCopyPad)
//   +199 (byte)  flag B              — byte_11B52E7[200*i] (cleared on register)
// The "active/visible" flag the menu toggles lives on the *resolved object*, not the
// entry: ResetEntries clears, and Register sets, *(objectHandle + 536) = 0/1.
// ---------------------------------------------------------------------------
inline constexpr int kStatusEntryStrideDwords = 50;   // 200-byte record
inline constexpr int kStatusEntryStrideBytes  = 200;
inline constexpr int kMaxStatusEntries        = 32;   // 1600/50; "Too many Objects" at >=32
inline constexpr int kStatusTableDwords       = kMaxStatusEntries * kStatusEntryStrideDwords; // 1600

inline constexpr int kStatusOffHandle  = 0;    // +0
inline constexpr int kStatusOffName    = 4;    // +4   (64 bytes)
inline constexpr int kStatusOffFlagA   = 67;   // +67  (byte_11B5263)
inline constexpr int kStatusOffGfx     = 68;   // +68  (dword_11B5264)
inline constexpr int kStatusOffLabel   = 72;   // +72  (128 bytes)
inline constexpr int kStatusOffFlagB   = 199;  // +199 (byte_11B52E7)

inline constexpr int kStatusNameLen  = 64;
inline constexpr int kStatusLabelLen = 128;

// The object record field the menu toggles (*(handle + 536) = active flag).
inline constexpr int kObjectActiveFlagOffset = 536;

// One status-text entry (modelled as a real record; the original is a byte blob).
struct StatusEntry {
    int  handle;                 // +0  resolved object handle (== click id); 0 = free
    char name[kStatusNameLen];   // +4  object name (de-dup key)
    int  gfxId;                  // +68 icon/gfx id
    char label[kStatusLabelLen]; // +72 localized label
    char flagA;                  // +67
    char flagB;                  // +199
};
extern StatusEntry g_statusEntries[kMaxStatusEntries]; // dword_11B5220

// The contact-menu .form id (the RunFrameLoop argument, decimal 425983 == 0x67FFF).
inline constexpr int kContactForm = 425983;

// Form-ready flag word (word_631758).  A builder only registers its entries once the
// form's layout pages are ready; the bits gate which entry-group is built.
inline constexpr int kFormFlagPage200 = 0x200; // primary production page ready
inline constexpr int kFormFlagPage400 = 0x400; // secondary (storage/transport) page ready

// The clicked-entry handle (dword_631720): 0 == nothing clicked this frame; otherwise
// it equals the handle of the entry that was hit.
//
// ---------------------------------------------------------------------------
// Reset / register primitives.
// ---------------------------------------------------------------------------

// Reset the whole status table (also clears g_clickedEntry / object state). Provided
// for test setup; the original BSS is zero at load.
void ResetContactMenu();

// gilde.exe 0x4bcc4c — VIBE_StatusText_ResetEntries.
// For every entry slot: if it has a resolved object handle, clear that object's active
// flag (*(handle+536)=0); then clear the parallel "selected" array (dword_11B5158).
// Returns 1600*4 (the original returns the final loop index * 4; preserved verbatim).
int StatusText_ResetEntries();

// gilde.exe 0x4bcc80 — VIBE_StatusText_Register  (name@eax, gfxId@ebx, label@edx)
// Register a menu entry for object `name`:
//   1. If an entry with this name already exists, set its object's active flag and
//      return its handle (de-dup).
//   2. Otherwise find the first free slot; if all 32 are used, log
//      "RegisterStatusText(): Too many Objects:%s" and return 0.
//   3. Resolve the object handle for `name` (Character_RunMeshCallback); if not found,
//      log "RegisterStatusText(): Object not found: %s" and return 0.
//   4. Store handle/gfx/name/label, clear the per-entry byte flags, set the object's
//      active flag, and return the handle.
// Returns the entry's object handle (its click id), or 0 on failure.
int StatusText_Register(const char* name, int gfxId, const char* label);

// ---------------------------------------------------------------------------
// Command sink (mockable) — the game actions a clicked entry dispatches.  Each method
// names the original it forwards to.  The selected-object handle is passed through.
// ---------------------------------------------------------------------------
struct ContactCommandSink {
    virtual ~ContactCommandSink() = default;
    virtual void OpenStorage(int /*obj*/) {}                  // VIBE_StorageDialog_Options
    virtual void OpenTransport(int /*obj*/, int /*mode*/) {}  // VIBE_TradeTransport_OpenPanelMode*
    virtual void OpenProductionPanel(int /*obj*/) {}          // VIBE_TradePanel_BuildProductionWindow
    virtual void OpenProductionWindow(int /*obj*/, char /*which*/) {} // OpenProductionWindowA/B/C
    virtual void RunFeast(int /*obj*/) {}                     // VIBE_Panel_RunGelage
    virtual void RunTraining(int /*obj*/) {}                  // VIBE_Panel_RunTrainingSelect
    virtual void RunStaffBook(int /*obj*/) {}                 // VIBE_Personnel_RunStaffBook
    virtual void RunMasterCertificate(int /*obj*/) {}         // VIBE_Meister_RunMasterCertificateDialog
    virtual void RunThiefInfo(int /*obj*/) {}                 // VIBE_Location_ThiefInformationDialog
    virtual void RunTradeSearch(int /*obj*/) {}               // VIBE_Location_TradeSearchExport
};
void ContactMenu_SetCommandSink(ContactCommandSink* sink);

// ---------------------------------------------------------------------------
// One frame of a builder, isolated for testing.  Real builders run this in a loop; the
// loop, the form-ready flag, and the click handle are owned by the io/sim clusters.
//
// Each Build* function below registers its entry set into the status table for the
// given form-flag word, returning the number of entries registered (the DATA/LAYOUT
// half).  Each Dispatch* function maps a clicked-entry handle to the wired action via
// the command sink (the WIRING half), returning true when the click was handled.
//
// The entry set + wiring are recovered byte-for-byte from each builder's decompile.
// ---------------------------------------------------------------------------

// The flag-gate predicates a builder consults before registering an entry.  These come
// from VIBE_Interaction_TestHandlerFlagWord (the production builders) and
// VIBE_GameObject_QueryFind (the mine builder); modelled as a hook so the entry set is
// testable.  Default: all gates open / no extra objects.
struct ContactGate {
    virtual ~ContactGate() = default;
    virtual bool HandlerFlag(int /*mask*/) { return true; }  // Interaction_TestHandlerFlagWord
    virtual bool HasObject(int /*kind*/) { return false; }   // GameObject_QueryFind result
};
void ContactMenu_SetGate(ContactGate* gate);

// --- ContactMenu_RemoteTrade (0x5138d0) — remote buy / sell. -----------------
struct RemoteTradeIds { int remoteBuy; int sell; };
RemoteTradeIds ContactMenu_BuildRemoteTrade();
bool ContactMenu_DispatchRemoteTrade(int clicked, const RemoteTradeIds& ids);

// --- ContactMenu_InfoBooks (0x512070) — information / master cert / staff book. -
struct InfoBooksIds { int info; int masterCert; int staffBook; };
InfoBooksIds ContactMenu_BuildInfoBooks();
bool ContactMenu_DispatchInfoBooks(int clicked, const InfoBooksIds& ids);

// --- ContactMenu_CarpenterProduction (0x513b28). ----------------------------
struct ProductionIds {
    int production;   // contact_PRODUKTION_*  -> OpenProductionPanel
    int storage;      // contact_LAGER          -> OpenStorage
    int transport;    // contact_TRANSPORT      -> OpenTransport(mode 1)
    int masterCert;   // ob_MEISTERBRIEF        -> RunMasterCertificate
    int staffBook;    // ob_PERSONALBUCH        -> RunStaffBook
    int gather;       // contact_SAMMELN        -> RunTradeSearch (perfumery/mixing only)
};
// All five production builders share the same wiring; the entry-name differs only in the
// localized "production" label.  `productionName` selects it.
ProductionIds ContactMenu_BuildProduction(const char* productionName, bool hasGather);
bool ContactMenu_DispatchProduction(int clicked, const ProductionIds& ids);

// --- ContactMenu_MineProductionStone (0x511ea4) — the richest builder. -------
struct MineIds {
    int storage;      // contact_LAGER
    int mine;         // contact_ABBAU         -> OpenProductionWindow 'B'
    int search;       // contact_SUCHEN_STEIN  -> OpenProductionWindow 'A'
    int production;   // contact_PRODUKTION    -> OpenProductionWindow 'C'
    int transport;    // contact_TRANSPORT     -> OpenTransport(mode 1)
    int feast;        // contact_GELAGE        -> RunFeast
    int targetA;      // ob_STROHPUPPE (if HasObject(53)) -> RunTraining
    int targetB;      // ob_ZIELSCHEIBE(if HasObject(52)) -> RunTraining
};
MineIds ContactMenu_BuildMine();
bool ContactMenu_DispatchMine(int clicked, const MineIds& ids);

// --- ContactMenu_StorageProductionWood (0x511d28) — wood storage hub. --------
struct WoodIds {
    int storage;      // contact_LAGER         -> OpenStorage
    int production;   // contact_PRODUKTION_HOLZ -> OpenProductionWindow 'C'
    int feast;        // contact_GELAGE        -> RunFeast
    int transport;    // contact_TRANSPORT     -> OpenTransport(mode 1)
    int targetA;      // ob_ZIELSCHEIBE        -> RunTraining
    int targetB;      // ob_STROHPUPPE         -> RunTraining
};
WoodIds ContactMenu_BuildWood();
bool ContactMenu_DispatchWood(int clicked, const WoodIds& ids);

// Localized "production" label names used by ContactMenu_BuildProduction (so callers /
// tests can reproduce the exact per-trade builder).
inline constexpr const char* kProdCarpenter  = "contact_PRODUKTION_TISCHLER";
inline constexpr const char* kProdSmith      = "contact_PRODUKTION_SCHMIEDEN";
inline constexpr const char* kProdStonemason = "contact_PRODUKTION_STEINMETZ";
inline constexpr const char* kProdBrewery    = "contact_PRODUKTION_BRAUEN";
inline constexpr const char* kProdPerfumery  = "contact_PRODUKTION_PARFUMERIE";
inline constexpr const char* kProdMixing     = "contact_PRODUKTION_MISCHEN";

} // namespace guild::gui

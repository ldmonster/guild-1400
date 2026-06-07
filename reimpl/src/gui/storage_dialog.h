#pragma once
// guild::gui — the storage-building dialogs (buy a new storage slot / storage options).
//
//   VIBE_StorageDialog_NewSlot @0x545fc8 — confirm buying a new storage slot. Shows the
//     current vs. capacity for one or two slot kinds (a "278"-type storage has one slot
//     dimension; others have two — a width and a depth) and an OK/Cancel.
//   VIBE_StorageDialog_Options @0x5461b0 — the full storage-options panel (14 callers).
//     Builds the storage hub: the object-action panel (OK/Cancel via
//     Hud_BuildObjectActionPanel 1661/1714), a 2-button row (Hud_BuildButtonRow), the
//     slot-count text, and the trade-panel slot grids (InitSlotTables +
//     PopulateInventorySlots + LayoutDragSlots). Buying a slot (12800 coins) or
//     enlarging (8000 coins) enqueues a storage command after a resource check.
//
// This module recovers the form names, the window-slot / text-id layout for NewSlot, the
// two-vs-one-slot-kind branch, and the price/command constants. The Options panel's full
// per-frame rebuild + the trade-panel slot grids are handled by trade_panel.{h,cpp}; here
// we recover its layout constants and the button -> action wiring. The frame loop, text
// engine and command codec are forward-declared / mocked.

#include "gui/types.h"

namespace guild::gui {

// Recovered .form names.
inline constexpr const char* kFormNewSlot       = "Handel\\Handel_Lager_Neuer_Slot";
inline constexpr const char* kFormStorageOptions = "Handel\\Handel_Lageroptionen";

// NewSlot text ids (RenderRichString).
inline constexpr int kTextSlotIntro     = 263; // header
inline constexpr int kTextSlotWidthNeed = 264; // width-slot needs (with 8000 cost)
inline constexpr int kTextSlotDepthNeed = 265; // depth-slot needs (8000)
inline constexpr int kTextSlot278Need   = 266; // single-dimension storage need (8000)
inline constexpr int kTextSlotConfirm   = 267; // confirm prompt (cancel id 1155)
inline constexpr int kTextSlotEnough    = 268; // "have enough width" line
inline constexpr int kTextSlotEnough2   = 269; // "have enough" line
inline constexpr int kStorageTypeSingle = 278; // *slot == 278 => one-dimension storage

// Options panel text ids (the body lines RenderRichString builds).
inline constexpr int kTextOptHeader      = 246; // panel header
inline constexpr int kTextOptSlotCount   = 255; // current slot-count line
inline constexpr int kTextOptEnlarge     = 270; // "max slots" replacement line
inline constexpr int kTextOptBuyConfirm  = 271; // buy-slot confirm message (12800)

// Options panel button click ids (from Hud_BuildObjectActionPanel / Hud_BuildButtonRow).
inline constexpr int kStorageActionOk   = 1661; // Hud_BuildObjectActionPanel base
inline constexpr int kStorageActionCancel = 1714;
inline constexpr int kStorageNewSlotGfx = 1715; // alt cancel gfx (NewSlot panel)

// Prices.
inline constexpr int kSlotEnlargeCost = 8000;  // enlarge an existing slot dimension
inline constexpr int kSlotBuyCost     = 12800; // buy a whole new slot

// Cancel click id (shared GUI constant).
inline constexpr int kStorageCancel = 1155;

// NewSlot result: which slot dimension(s) the dialog asked the player to confirm.
//   1 => the dialog returned via the "width/single" object id;
//   2 => via the "depth" object id; 0 => cancelled.
enum class NewSlotResult { kCancel = 0, kWidth = 1, kDepth = 2 };

// ---------------------------------------------------------------------------
// Synthetic storage state.
//   - `single` true when *slot == 278 (one-dimension storage).
//   - usedWidth / usedDepth: current occupancy (a3 / a4 args).
//   - capWidth / capDepth: capacity (v17[576]/[577]/[578]).
// ---------------------------------------------------------------------------
struct StorageState {
    bool single = false;     // *a1 == 278
    int usedWidth = 0;       // a4 (current width usage)
    int usedDepth = 0;       // a3 (current depth usage)
    int capWidth = 0;        // v17[576] (single uses [578])
    int capDepth = 0;        // v17[577]
    int handle = 0;
};

// The widget set NewSlot builds.
struct NewSlotLayout {
    const char* form = nullptr;
    int widthObjId = -1;     // ChildObjectId of the width/single "need" line (or -1)
    int depthObjId = -1;     // ChildObjectId of the depth "need" line (or -1)
    bool needWidth = false;  // capWidth > usedWidth (a "need more" line was shown)
    bool needDepth = false;  // capDepth > usedDepth
};

// gilde.exe 0x545fc8 (layout half) — build the new-slot dialog for a storage state.
//   single (278): if (cap > used) build the single "need" line (obj at widthObjId).
//   else: if (capWidth > usedWidth) build the width line; if (capDepth > usedDepth)
//         build the depth line (obj at depthObjId).
NewSlotLayout StorageDialog_BuildNewSlot(const StorageState& s);

// gilde.exe 0x545fc8 (wiring half) — map a click to the NewSlot outcome.
//   clicking the width/single object -> kWidth; the depth object -> kDepth; cancel -> kCancel.
NewSlotResult StorageDialog_DispatchNewSlot(const NewSlotLayout& l, int clickedId,
                                            int clickedObj);

// ---------------------------------------------------------------------------
// Options panel command sink (mockable).
// ---------------------------------------------------------------------------
struct StorageCommandSink {
    virtual ~StorageCommandSink() = default;
    // Enlarge a slot dimension for `cost` (8000) — width(0) or depth(1).
    virtual void EnlargeSlot(int /*handle*/, int /*dim*/, int /*cost*/) {}
    // Buy a whole new slot for `cost` (12800).
    virtual void BuyNewSlot(int /*handle*/, int /*cost*/) {}
};
void StorageDialog_SetCommandSink(StorageCommandSink* sink);

// gilde.exe 0x5461b0 (wiring half) — the Options panel's two action buttons.
//   action-OK object (the "new slot" button v82): if the storage isn't full, enlarge
//     the deficient dimension(s) for 8000; if full, buy a new slot for 12800 via the
//     second button v84 (gated on the 0x80 flag).
// `okObj`/`buyObj` are the two action object ids; `clickedObj` is the hit object.
// `s` supplies the occupancy so the enlarge-vs-buy branch matches the original.
// Returns true when a command was dispatched.
bool StorageDialog_DispatchOptions(const StorageState& s, int okObj, int buyObj,
                                   int clickedId, int clickedObj);

} // namespace guild::gui

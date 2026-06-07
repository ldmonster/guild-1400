#pragma once
// guild::gui — the player BUY dialogs: the action-panel buy/occupant picker and the
// wine-cabinet (Weinkeller) buy dialog SHELL.
//
// Two modal "buy" screens from gilde.exe's panel cluster.  Each loads a .form, lays
// out a window/object set, then pumps the retained-mode frame loop and reacts to the
// well-known click ids (1210 = OK/select, right-button/672230 = cancel).
//
//   VIBE_Panel_RunApBuy           @0x54d9d8 — the action-panel "AP" buy/occupant
//       picker.  form = "Panel\Panel_AP".  CenterChildWindows; SelectWindow; render
//       header text 6868; PopulateObjectList builds one clickable row per matching
//       occupant.  Modal loop (RunFrameLoop 415687): right-click sets the cancel
//       flag; clicking id 1210 looks up the selected object's data ptr, stores it on
//       the matched table row, and re-renders the list ($C).  Destroy on exit.
//
//   VIBE_Form_PopulateObjectList  @0x54d8fc — the list builder RunApBuy calls.  Selects
//       the target window, clears the row id table (Light_SetGrayColorThunk over
//       dword_1232080), gathers the occupant rows (the data source, modeled as a
//       hook), renders the title (6869) then one row per occupant (6870, id 1210),
//       fetching each row's child-object id (Form_GetChildObjectId) and binding the
//       row's label via Object_SetValueOrText.
//
//   VIBE_WineCellar_ShowBuyDialog @0x519b14 — the Weinkeller buy dialog SHELL.  form =
//       "locations\wohnsitz\weinkeller".  Reads the player's held cash + the cellar's
//       child money, converts both to display coords (MoneyConvertToDisplayCoord),
//       renders the price label (5722) clamped to the affordable max, then modal loops
//       (RunFrameLoop 423879).  On 1210 it runs the BUY rule core (world::WineCellarBuy:
//       cost = MultiplyByRate(itemValue, city); affordability gate; EnqueueCmd15 leg).
//       The price arithmetic + the two-leg command are REUSED from world/trade_player.
//
// As with the other dialog clusters (building_dialog, trade_dialog) the frame loop,
// glyph/text engine and command codec live elsewhere; here we recover the .form names,
// window-slot / text-id layout, the row id table geometry, and the click->action
// wiring as a builder (LAYOUT/DATA) + dispatcher (WIRING) split that is testable in
// isolation.  The occupant data source is a mockable hook.  The money/price math is
// the now-real model in world/trade_player.{h,cpp}.

#include "gui/types.h"

#include <vector>

namespace guild::gui {

// ---------------------------------------------------------------------------
// GUI-wide click ids (shared with building_dialog / trade_dialog).
// ---------------------------------------------------------------------------
inline constexpr int kBuyClickSelect = 1210;  // dword_75BF38 OK/select
inline constexpr int kBuyClickCancel = 1155;  // right-button / abort

// ---------------------------------------------------------------------------
// Recovered .form resource names (the VIBE_GameTick_Finalize string argument).
// ---------------------------------------------------------------------------
// NOTE: inline constexpr char[] (not const char*) so the symbol is a single
// entity with one address across TUs — callers compare l.form by pointer identity.
inline constexpr char kFormApPanel[]    = "Panel\\Panel_AP";                 // RunApBuy
inline constexpr char kFormWineCellar[] = "locations\\wohnsitz\\weinkeller"; // ShowBuyDialog

// ---------------------------------------------------------------------------
// Recovered RunFrameLoop selectors (the modal-loop "form" argument).
// ---------------------------------------------------------------------------
inline constexpr int kLoopApPanel    = 415687;  // RunApBuy modal loop
inline constexpr int kLoopWineCellar = 423879;  // ShowBuyDialog modal loop

// ---------------------------------------------------------------------------
// Recovered RenderRichString text-string ids.
// ---------------------------------------------------------------------------
inline constexpr int kTextApHeader    = 6868;  // RunApBuy header line
inline constexpr int kTextApRefresh   = -1;    // re-render uses the literal "$C" (aC_5)
inline constexpr int kTextListTitle   = 6869;  // PopulateObjectList title line
inline constexpr int kTextListRow     = 6870;  // PopulateObjectList per-row line (id 1210)
inline constexpr int kTextWineTitle   = 5721;  // ShowBuyDialog title line
inline constexpr int kTextWinePrice   = 5722;  // ShowBuyDialog price label (slot 2)

// The row table the occupant list builder fills: dword_1232080[] (child-object ids,
// the +0 of each pair) parallel to dword_1232084[] (the occupant row records, +4).
// The scan bound is 2048 (PopulateObjectList) / 512 in RunApBuy's 2-stride match loop.
inline constexpr int kRowTableScanBound = 2048;
inline constexpr int kRowMatchScanBound = 512;   // RunApBuy: v4 += 2, stop at >= 512

// ===========================================================================
// VIBE_Form_PopulateObjectList (0x54d8fc) — the occupant-row list builder.
// ===========================================================================

// One built list row: a fetched child-object id bound to an occupant record handle.
struct ListRow {
    int objId  = 0;   // VIBE_Form_GetChildObjectId result -> dword_1232080[i]
    int record = 0;   // the occupant record handle      -> dword_1232084[i]
};

// The occupant data source the original gathers via VIBE_Building_PopulateOccupantList
// (0x59287c): a person/occupant enumeration filtered to the building.  We model it as a
// hook returning the per-row record handles (the values bound to dword_1232084[]).
struct OccupantSource {
    virtual ~OccupantSource() = default;
    // Occupant record handles for `building` (the PopulateOccupantList scan order).
    virtual std::vector<int> Enumerate(int /*building*/) { return {}; }
};
void BuyDialog_SetOccupantSource(OccupantSource* src);

// gilde.exe 0x54d8fc — VIBE_Form_PopulateObjectList.
// Builds one row per occupant of `building` into `outRows` (capped at the 2048 scan
// bound); each row gets a stable child-object id (objBase + i) and binds the occupant
// record handle.  Returns the row count built.  (The title line 6869 and per-row line
// 6870 are rendered by the original; the id binding + record table is what we recover.)
int BuyDialog_PopulateObjectList(int formId, int winSlot, int building,
                                 std::vector<ListRow>* outRows);

// ===========================================================================
// VIBE_Panel_RunApBuy (0x54d9d8) — the AP buy/occupant picker.
// ===========================================================================

// The layout RunApBuy builds before entering the modal loop.
struct ApBuyLayout {
    const char* form = nullptr;
    int textHeader = 0;   // 6868
    int loopForm = 0;     // 415687
    int rowCount = 0;     // PopulateObjectList result
    std::vector<ListRow> rows;  // the built rows (parallel id/record table)
};

// gilde.exe 0x54d9d8 (layout half) — build the AP buy panel for a building's occupants.
ApBuyLayout BuyDialog_BuildApBuy(int building);

// gilde.exe 0x54d9d8 (wiring half) — map a frame's click to the selection.
// Mirrors the modal body:
//   if (dword_672230) cancel;                                 // right-click -> abort
//   if (dword_75BF38 == 1210) {                               // selected a row
//     // RunApBuy scans dword_1232084[] (stride 2) for the entry whose dword_1232080[]
//     // child-id equals dword_62D22C (the clicked object); on a match it binds the
//     // selected object's data ptr onto that row (record+8) and re-renders the list.
//   }
// `clickedId` is dword_75BF38, `clickedObj` is dword_62D22C, `cancelFlag` is
// dword_672230.  On a 1210 match, writes the matched row index to *outRow and returns
// true (the loop should refresh the list); otherwise -1 / false.
bool BuyDialog_DispatchApBuy(const ApBuyLayout& l, int clickedId, int clickedObj,
                             bool cancelFlag, int* outRow, bool* outCancel);

// ===========================================================================
// VIBE_WineCellar_ShowBuyDialog (0x519b14) — the Weinkeller buy dialog SHELL.
// ===========================================================================

// The display state the dialog computes before the modal loop.  The original reads the
// player's held cash and the cellar's child money, converts both to display coords, and
// shows the price clamped to the affordable maximum.
struct WineBuyLayout {
    const char* form = nullptr;
    int textTitle = 0;     // 5721
    int textPrice = 0;     // 5722
    int loopForm = 0;      // 423879
    int priceObjId = 0;    // the price-label child-object id (Form_GetChildObjectId)
    int playerCashDisp = 0;  // MoneyConvertToDisplayCoord(playerHeld, city)
    int cellarCashDisp = 0;  // MoneyConvertToDisplayCoord(cellarMoney, city)
    int shownValue = 0;      // the value bound to the label (Object_SetValueOrText arg)
};

// gilde.exe 0x519b14 (layout half) — VIBE_WineCellar_ShowBuyDialog.
// Computes the displayed cash/price for the cellar buy dialog.  `playerHeld` is the
// player's held cash (SumCurrencyHeld), `cellarMoney` the cellar's child money
// (SumChildMoney), `city` the byte_6477A1 currency/rate context.  Mirrors the
// original's two-branch display value byte-for-byte:
//   playerDisp = ConvertToDisplayCoord(playerHeld, city);
//   cellarDisp = ConvertToDisplayCoord(cellarMoney, city);
//   // the original computes (playerDisp + cellarDisp) and compares it to cellarDisp:
//   shownValue = (playerDisp + cellarDisp > cellarDisp)  // i.e. playerDisp > 0
//              ? (playerDisp + cellarDisp)               //   -> show cellar + player
//              : cellarDisp;                             //   -> show cellar only
// (the label's second arg is `playerDisp`).  `priceObjId` is a stable label id.
WineBuyLayout BuyDialog_BuildWineCellar(int playerHeld, int cellarMoney, u8 city,
                                        int priceObjId);

// gilde.exe 0x519b14 (wiring half) — map the modal click to the buy.
//   if (dword_672230 || dword_75BF38 == 1155) cancel;
//   if (dword_75BF38 == 1210) {
//     cost = MultiplyByRate(itemValue, city);                 // world::WineCellarBuy
//     if (playerCash >= cost) EnqueueCmd15(player, seller, playerCash - cost, city);
//     else                    EnqueueCmd15(seller, player, cost - playerCash, city);
//   }
// `itemValue` is the data ptr read off the selected widget (Object_GetDataPtr); the BUY
// rule core (cost/afford + the chosen Cmd15 leg) is REUSED from world::WineCellarBuy.
// Returns the computed `cost` (0 when nothing was bought); *outAfford reports the
// affordability branch; *outCancel reports a cancel click.  `commit` forwards to the
// world command hook.
int BuyDialog_DispatchWineCellar(const WineBuyLayout& l, int itemValue, u8 city,
                                 int playerCash, int playerAccount, int sellerAccount,
                                 int clickedId, bool cancelFlag, bool commit,
                                 bool* outAfford, bool* outCancel);

} // namespace guild::gui

#pragma once
// guild::gui — the transport-cart trade dialogs.
//
// Two modal dialogs from gilde.exe's trade cluster:
//   VIBE_TradeDialog_BuyTransportCart  @0x53ef48 — buy a new transport cart.
//   VIBE_TradeDialog_ConfirmSellCarts  @0x53f2bc — confirm selling N carts.
//
// BuyTransportCart is a full panel:
//   1. if (CollectStorageBuildings(b) >= 3) -> message 238 ("max carts"), abort.
//   2. form = "Handel\Handel_Transport_neuer_Karren";
//      SelectWindow(form,?) text 213 -> 3 cart-type child objects (a radio group);
//      SelectWindow(form,0) Hud_BuildObjectActionPanel(1661, 1715);  // OK/Cancel row
//      SelectWindow(form,3) text 216 -> the price label object;
//      group = RadioGroup_Create(3, child0);   // 3 cart types
//   3. modal loop: clicking a radio (ids 1409..1411) selects a cart type; clicking the
//      OK object (1210) buys the selected type at its market price (command 16 + 17).
//
// ConfirmSellCarts is a small confirm:
//   - sum the market price of the N carts; if N>1 use a plural message, else singular;
//   - message 217 (price, count); on confirm (ShowMessageBox 257) detach+destroy each
//     cart and enqueue the sell (cmd15).
//
// The frame loop, glyph/text engine, radio-group widget and command codec live in other
// clusters; here we recover the form name, window-slot/text-id layout, the radio
// id range + price label, and the buy/sell wiring + the price content from a synthetic
// cart catalogue. Mutations go through a mockable command sink.

#include "gui/types.h"

namespace guild::gui {

// Recovered .form name + text ids.
inline constexpr const char* kFormBuyCart = "Handel\\Handel_Transport_neuer_Karren";
inline constexpr int kTextCartTypes = 213;  // 3 cart-type radio entries
inline constexpr int kTextCartPrice = 216;  // price label
inline constexpr int kTextBuyConfirm = 214; // "buy this cart?" formatted message
inline constexpr int kTextCartLabel  = 215; // per-type label refresh
inline constexpr int kTextSellCarts  = 217; // ConfirmSellCarts price/count message

// Max storage buildings before "too many" (CollectStorageBuildings >= 3 -> message 238).
inline constexpr int kMaxStorageBuildings = 3;
inline constexpr int kCartTypeCount = 3;

// Radio-button click id range for the 3 cart types (dword_75BF38 in [1409,1411]).
inline constexpr int kCartRadioFirst = 1409;
inline constexpr int kCartRadioLast  = 1411;

// The OK / confirm + cancel click ids (Hud_BuildObjectActionPanel(1661, 1715)).
inline constexpr int kClickConfirm = 1210;
inline constexpr int kClickAbort   = 1155;

// Commands.
inline constexpr int kCmdBuyCart16 = 16; // QueueRequest16 (charge price)
inline constexpr int kCmdBuyCart17 = 17; // QueueRequest17 (spawn cart)
inline constexpr int kCmdSellCart15 = 15; // EnqueueCmd15 (sell carts)

// The base item id of the first cart type (2*type + 308 in the original's price call;
// labels use 2*type + 2767 / 2151). Exposed so content tests reproduce the values.
inline constexpr int kCartItemBase = 308;
inline constexpr int kCartLabelBase = 2767;

// ---------------------------------------------------------------------------
// Synthetic cart catalogue / building state.
// ---------------------------------------------------------------------------
struct CartCatalogue {
    int prices[kCartTypeCount] = {0, 0, 0}; // ComputeMarketPrice(type) per cart type
    int storageBuildingCount = 0;           // CollectStorageBuildings(building)
    int buildingHandle = 0;
};

// The widget set BuyTransportCart builds.
struct BuyCartLayout {
    const char* form = nullptr;
    int textTypes = 0;
    int textPrice = 0;
    int radioCount = 0;          // 3 cart-type radios
    int radioIds[kCartTypeCount]{}; // the child object ids (radio group members)
    int priceObjId = 0;          // the price label object
    int radioGroup = 0;          // RadioGroup_Create handle
    bool aborted = false;        // true when CollectStorageBuildings >= 3
};

// ---------------------------------------------------------------------------
// Command sink (mockable).
// ---------------------------------------------------------------------------
struct TradeCommandSink {
    virtual ~TradeCommandSink() = default;
    // Buy cart of `type` at `price`: charge (cmd 16) then spawn (cmd 17).
    virtual void BuyCart(int /*building*/, int /*type*/, int /*price*/) {}
    // Sell `count` carts for total `price` (cmd 15).
    virtual void SellCarts(int /*count*/, int /*price*/) {}
};
void TradeDialog_SetCommandSink(TradeCommandSink* sink);

// gilde.exe 0x53ef48 (layout half) — build the buy-cart panel for a catalogue.
// When storageBuildingCount >= 3 the layout is `aborted` (message 238, no panel).
BuyCartLayout TradeDialog_BuildBuyCart(const CartCatalogue& cat);

// gilde.exe 0x53ef48 (wiring half) — map a click to the selected cart type / buy.
// `selectedType` is updated in place by radio clicks; a confirm click buys it.
// Returns true when a cart was bought (loop should refresh).
bool TradeDialog_DispatchBuyCart(const BuyCartLayout& l, const CartCatalogue& cat,
                                 int clickedId, int clickedObj, int& selectedType);

// gilde.exe 0x53f2bc — ConfirmSellCarts: total price of `count` carts.
// Mirrors the price summation: sum each cart's market price (per the catalogue), apply
// the dbl_623ED8 sell-rate scale (modelled as a rate multiply), and on confirm dispatch
// SellCarts(count, total). `confirm` stands in for the ShowMessageBox(257) result.
// Returns the computed total price (0 when count is out of [1,32]).
int TradeDialog_ConfirmSellCarts(int count, const int* cartPrices, double sellRate,
                                 bool confirm);

} // namespace guild::gui

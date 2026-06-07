#include "gui/trade_dialog.h"

namespace guild::gui {

namespace {

TradeCommandSink  g_defaultSink;
TradeCommandSink* g_sink = &g_defaultSink;

// The radio child-object ids the panel builds (GetChildObjectId(1, 0/1/2)). We assign a
// stable base; only the relative ordering matters for the wiring.
constexpr int kRadioObjBase = 2000;
constexpr int kPriceObjId   = 2100;
constexpr int kRadioGroupId = 1;

} // namespace

void TradeDialog_SetCommandSink(TradeCommandSink* sink) {
    g_sink = sink ? sink : &g_defaultSink;
}

// gilde.exe 0x53ef48 (layout half) — VIBE_TradeDialog_BuyTransportCart.
//   if (CollectStorageBuildings(b,0) >= 3) { RenderFormattedMessage(238,3); abort; }
//   form = "Handel\Handel_Transport_neuer_Karren";
//   SelectWindow(form,?) RenderRichString(213);
//   v27[0..2] = GetChildObjectId(1, 0/1/2);     // 3 cart-type radios
//   SelectWindow(form,0) Hud_BuildObjectActionPanel(1661, 1715);  // OK/Cancel
//   SelectWindow(form,3) obj = RenderRichString(216); price = GetChildObjectId(obj);
//   group = RadioGroup_Create(3, v27[0]); Selection_Update(group);
BuyCartLayout TradeDialog_BuildBuyCart(const CartCatalogue& cat) {
    BuyCartLayout l{};
    l.form = kFormBuyCart;
    if (cat.storageBuildingCount >= kMaxStorageBuildings) {
        l.aborted = true;     // RenderFormattedMessage(238,3); ShowMessageBox(0,0)
        return l;
    }
    l.textTypes = kTextCartTypes;
    l.textPrice = kTextCartPrice;
    l.radioCount = kCartTypeCount;
    for (int i = 0; i < kCartTypeCount; ++i)
        l.radioIds[i] = kRadioObjBase + i;   // GetChildObjectId(1, i)
    l.priceObjId = kPriceObjId;
    l.radioGroup = kRadioGroupId;            // RadioGroup_Create(3, radio0)
    return l;
}

// gilde.exe 0x53ef48 (wiring half).
//   if (dword_75BF38 in [1409,1411]) -> select the matching cart type (v3 = index);
//   else if (dword_75BF38 == 1210 && clicked == priceObj):
//       price = ComputeMarketPrice(type+308); if (CheckResourceAmount(price))
//         RenderFormattedMessage(214,...); if (ShowMessageBox(257)) {
//           QueueRequest16(charge); QueueRequest17(spawn cart of type); }
bool TradeDialog_DispatchBuyCart(const BuyCartLayout& l, const CartCatalogue& cat,
                                 int clickedId, int clickedObj, int& selectedType) {
    if (l.aborted) return false;

    // Radio-button selection by click-id range (1409..1411).
    if (clickedId >= kCartRadioFirst && clickedId <= kCartRadioLast) {
        // The original also matches clickedObj to v27[index]; the id range maps 1:1.
        for (int i = 0; i < kCartTypeCount; ++i) {
            if (clickedObj == l.radioIds[i]) { selectedType = i; return false; }
        }
        selectedType = clickedId - kCartRadioFirst;
        return false;
    }

    // Confirm: clicked OK on the price object -> buy the selected type.
    if (clickedId == kClickConfirm && clickedObj == l.priceObjId) {
        int type = selectedType;
        if (type < 0 || type >= kCartTypeCount) type = 0;
        int price = cat.prices[type];        // ComputeMarketPrice(type+308)
        g_sink->BuyCart(cat.buildingHandle, type, price); // cmd 16 + cmd 17
        return true;
    }
    return false;
}

// gilde.exe 0x53f2bc — VIBE_TradeDialog_ConfirmSellCarts.
//   if (count <= 32 && count) {
//     total = 0; for each cart: total += ComputeMarketPrice(cart);   // ConvertX truncates
//     total = (int)(total * dbl_623ED8);                              // sell-rate scale
//     RenderFormattedMessage(217, total, count, ...);
//     if (ShowMessageBox(257)) { detach+destroy each cart; EnqueueCmd15(total); }
//   }
int TradeDialog_ConfirmSellCarts(int count, const int* cartPrices, double sellRate,
                                 bool confirm) {
    if (count <= 0 || count > 32)
        return 0;

    int total = 0;
    for (int i = 0; i < count; ++i) {
        // The original truncates each price through Coord_ConvertX (int cast) and sums.
        total = (int)((double)cartPrices[i] + (double)total);
    }
    total = (int)((double)total * sellRate); // dbl_623ED8 sell-rate scale

    if (confirm)
        g_sink->SellCarts(count, total);      // detach+destroy + EnqueueCmd15
    return total;
}

} // namespace guild::gui

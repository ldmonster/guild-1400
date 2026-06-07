#include "gui/trade_panel.h"

#include <cstring>

namespace guild::gui {

// Original BSS: the dword_122E064.. / dword_122E764.. item-grid tables.
ItemSlot g_buySlots[kBuySlotCount];
ItemSlot g_sellSlots[kSellSlotCount];

namespace {

InventorySource  g_defaultInv;
InventorySource* g_inv = &g_defaultInv;

} // namespace

void TradePanel_SetInventorySource(InventorySource* src) {
    g_inv = src ? src : &g_defaultInv;
}

// Grid coordinate helpers (the exact expressions InitSlotTables uses).
//   buy:  dword_122E098[k] = ((k%4)<<6)+16 ; dword_122E064[k] = ((k/4)<<6)+16
//   sell: dword_122E798[k] = ((k%4)<<6)+16 ; dword_122E764[k] = ((k/4)<<6)+208
int TradePanel_SlotX(int k)     { return ((k % kSlotColumns) << 6) + kSlotInset; }
int TradePanel_BuySlotY(int k)  { return ((k / kSlotColumns) << 6) + kSlotInset; }
int TradePanel_SellSlotY(int k) { return ((k / kSlotColumns) << 6) + kSellGridYBase; }

// gilde.exe 0x50854c — VIBE_TradePanel_InitSlotTables.
// The original clears several scratch tables then both item grids. We reproduce the
// item-grid initialisation (the part this module owns): every record is zeroed to the
// init state (itemId=0, objectId/fill/stock/capacity = -1) and assigned its grid (x,y)
// via the exact ((idx%4)<<6)+inset / ((idx/4)<<6)+base expressions. The scratch
// column-state tables (dword_122EDxx) are owned by the layout functions and reset
// there; here InitSlotTables establishes the grids. Returns the final sell-grid y
// (((7/4)<<6)+208 = 272), matching the original's returned `result`.
int TradePanel_InitSlotTables() {
    // Buy grid: 16 records, 4 columns, 64-px pitch, 16-px inset.
    //   for (v12=0; v12<16; ++v12) { dword_122E098[k]=((v12%4)<<6)+16;
    //                                dword_122E064[k]=((v12/4)<<6)+16; ... = -1; }
    for (int k = 0; k < kBuySlotCount; ++k) {
        g_buySlots[k] = ItemSlot{};
        g_buySlots[k].itemId   = 0;                    // word_122E090[k] = 0
        g_buySlots[k].objectId = -1;                   // dword_122E094[k] = -1
        g_buySlots[k].x = TradePanel_SlotX(k);         // dword_122E098[k]
        g_buySlots[k].y = TradePanel_BuySlotY(k);      // dword_122E064[k]
        g_buySlots[k].fill = -1;                       // dword_122E0BC..
        g_buySlots[k].stock = -1;                      // dword_122E0C0..
        g_buySlots[k].capacity = -1;                   // dword_122E0C4..
    }
    // Sell grid: 8 records, 4 columns, same pitch/inset but y origin 208.
    //   for (v15=0; v15<8; ++v15) { dword_122E798[k]=((v15%4)<<6)+16;
    //                               dword_122E764[k]=((v15/4)<<6)+208; ... = -1; }
    int result = 0;
    for (int k = 0; k < kSellSlotCount; ++k) {
        g_sellSlots[k] = ItemSlot{};
        g_sellSlots[k].itemId   = 0;
        g_sellSlots[k].objectId = -1;
        g_sellSlots[k].x = TradePanel_SlotX(k);        // dword_122E798[k]
        g_sellSlots[k].y = TradePanel_SellSlotY(k);    // dword_122E764[k]
        g_sellSlots[k].fill = -1;
        g_sellSlots[k].stock = -1;
        g_sellSlots[k].capacity = -1;
        result = g_sellSlots[k].y;                     // last computed y
    }
    return result; // ((7/4)<<6)+208 = 272
}

// gilde.exe 0x50a794 — VIBE_TradePanel_PopulateInventorySlots (content population).
// For each shop item: pick the target grid by its sell-side flag, find the first slot
// for that item id (or the first free slot), and store the model values:
//   fill     = (stock * capacity) >> 2     (the original's unsigned `>>2`)
//   stock    = VIBE_Inventory_GetEffectiveStock(item)
//   capacity = VIBE_Inventory_GetSlotCapacity(item)
// Returns the number of slots populated.
int TradePanel_PopulateInventorySlots(const SyntheticShop& shop) {
    int filled = 0;
    for (int n = 0; n < shop.count; ++n) {
        const ShopItem& it = shop.items[n];

        ItemSlot* grid = it.sellSide ? g_sellSlots : g_buySlots;
        int gridCount  = it.sellSide ? kSellSlotCount : kBuySlotCount;

        // Find an existing slot for this item id, else the first empty slot.
        int slot = -1;
        for (int k = 0; k < gridCount; ++k) {
            if (grid[k].itemId == it.id) { slot = k; break; }
        }
        if (slot < 0) {
            for (int k = 0; k < gridCount; ++k) {
                if (grid[k].itemId == 0) { slot = k; break; }
            }
        }
        if (slot < 0)
            continue; // grid full ("if (v14 < 16)" / "if (v29 < 8)" guards)

        i32 stock    = g_inv->EffectiveStock(it.id);
        i32 capacity = g_inv->SlotCapacity(it.id);
        // The original reads stock/capacity from the live inventory but, for a synthetic
        // shop, the ShopItem carries authoritative values when the hook returns 0.
        if (stock == 0 && capacity == 0) { stock = it.stock; capacity = it.capacity; }

        grid[slot].itemId   = it.id;                                    // word_122E090[k] = *k
        // fill = (unsigned)(stock * capacity) >> 2  — preserve unsigned shift.
        grid[slot].fill     = (i32)((u32)((u32)stock * (u32)capacity) >> 2);
        grid[slot].stock    = stock;
        grid[slot].capacity = capacity;
        ++filled;
    }
    return filled;
}

// gilde.exe 0x50b600 (layout half) — VIBE_TradePanel_BuildSliderRow.
// When the row has an item (a4[0] != 0) the original lazily allocates the child widgets
// (slider/icon/button/buy-label/sell-label) and then re-lays them out at the row origin
// + the recovered offsets via VIBE_Widget_LayoutBounds(rowX + winX + dx, rowY + winY +
// dy, id). It also fills the slider value model from a4[11..13]. When the item is 0 it
// destroys every child (sets each *Id back to -1). We reproduce the coordinate math +
// id lifecycle; the widget allocation leaves are stubbed (assign sequential handles).
namespace {
int g_nextWidget = 1; // stands in for VIBE_Object_AddToWindow / AddSlider allocations
int AllocWidget() { return g_nextWidget++; }
} // namespace

void TradePanel_BuildSliderRow(SliderRow& row, u8 mode, int windowX, int windowY) {
    if (row.itemId == 0) {
        // Destroy path: clear every child widget id back to -1 (a4[4..9] = -1).
        row.iconId = row.btnId = row.buyLbl = row.sellLbl = -1;
        row.sliderId = row.childWin = -1;
        return;
    }

    // Lazy widget allocation (a4[8]/a4[4]/a4[5]/a4[6]/a4[7] == -1 -> allocate).
    if (row.sliderId == -1) row.sliderId = AllocWidget(); // AddSliderToWindow(x+6,y, ...)
    if (row.iconId   == -1) row.iconId   = AllocWidget(); // AddToWindow / AddIcon (x+15,y+6)
    if (row.btnId    == -1) {
        if (mode & (kSliderRowSell | kSliderRowBuy)) // gfx 1721 (sell) / 1720 (buy)
            row.btnId = AllocWidget();                // AddToWindow(x+15,y+58)
    }
    if ((mode & kSliderRowBuy) && row.buyLbl  == -1) row.buyLbl  = AllocWidget(); // AddTextLabel(x+16,y+64)
    if ((mode & kSliderRowSell) && row.sellLbl == -1) row.sellLbl = AllocWidget(); // AddTextLabel(x+16,y+73)

    // Slider value model: SetValueOrText(slider, min=0, max, value, ...).
    // The original quantises the slider span; reuse the slider value model.
    int min = row.min, max = row.max;
    row.value = Slider_QuantizeRange(min, max, row.value, /*clampFlag=*/false);
    row.min = min; row.max = max;

    // Lay out the slider widget at its absolute position, mirroring the original's
    //   VIBE_Widget_LayoutBounds(a4[4] + window.x + 6, a4[6] + window.y + 0, slider).
    // Here the row origin already carries the item's column, so:
    //   absX = rowX + windowX + kSliderOff.dx ; absY = rowY + windowY + kSliderOff.dy.
    row.sliderAbsX = row.rowX + windowX + kSliderOff.dx;
    row.sliderAbsY = row.rowY + windowY + kSliderOff.dy;
}

// gilde.exe 0x50ad10 (coordinate half) — VIBE_TradePanel_LayoutDragSlots.
// For each column c the row origin is laid out at:
//   x = colOriginX[c]              (dword_122EDF8/EDD0 = (winW - iconW) >> 1 in the
//                                    original; here the caller supplies the origin)
//   y = rowYBase + rowYStep * c    (v9 += 22 per buy column; v14 = 182 + 22*c sell)
// and within a row the r-th drag slot sits at x + 68*r (kDragColXStep). We fill the
// per-column origin (r = 0) here; callers add 68*r for further columns.
void TradePanel_LayoutDragSlotColumns(int cols, int rowYBase, int rowYStep,
                                      const int* colOriginX, int* outX, int* outY) {
    for (int c = 0; c < cols; ++c) {
        outX[c] = colOriginX[c];
        outY[c] = rowYBase + rowYStep * c;
    }
}

} // namespace guild::gui

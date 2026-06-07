#pragma once
// guild::gui — the trade / production panel: slot tables, slider rows, drag slots,
// item columns.
//
// gilde.exe builds the in-game trade / storage panels around a fixed bank of layout
// tables held in BSS (the dword_122Exxx region). A trade panel has:
//   - a 4x4 grid of "buy" item slots  (16 slots, base dword_122E064.. , 56-byte stride)
//   - a 4x2 grid of "sell" item slots (8  slots, base dword_122E764.. , 56-byte stride)
//   - per-grid slider rows (one slider + buy/sell price labels per visible item)
//   - drag-slot mirrors (the items the player drags between storage windows)
// VIBE_TradePanel_InitSlotTables @0x50854c zero/-1-initialises every one of these
// tables before a panel opens; the layout functions then place each slot at its grid
// coordinate and the populate function fills the model values (stock / capacity /
// price) from the building's inventory.
//
// This module recovers, byte-for-byte:
//   (a) the slot-table layout: stride, count, and the exact (x,y) grid coordinates each
//       slot is initialised to in InitSlotTables;
//   (b) the slider-row widget set + its layout strides (slider / icon / buy-label /
//       sell-label / button at the recovered +6/+15/+58/+64/+73 offsets);
//   (c) the drag-slot row coordinate math (the +8/+16/+208 column origins, the 68-px
//       column step, the 22/120-px row steps);
//   (d) the content population: stock = effectiveStock, capacity = slotCapacity,
//       fill = stock*capacity>>2, taken from a synthetic Inventory hook.
//
// The modal frame loop (VIBE_GameLogic_RunFrameLoop), the glyph/text engine
// (VIBE_Text_Render*), the widget-allocation leaves (VIBE_Object_AddToWindow,
// VIBE_Widget_AddSliderToWindow, VIBE_Widget_LayoutBounds) and the command codec
// (VIBE_Command_*) live in other clusters; they are forward-declared and routed
// through small mockable hooks so the layout/wiring/population is testable in isolation.

#include "gui/types.h"
#include "gui/slider.h"

namespace guild::gui {

// ---------------------------------------------------------------------------
// Slot-table geometry (recovered from VIBE_TradePanel_InitSlotTables @0x50854c).
//
// Two item grids, each an array of records of 56-byte (14-dword) stride:
//   buy grid : dword_122E064 base, 16 records (4 cols x 4 rows)
//   sell grid: dword_122E764 base,  8 records (4 cols x 2 rows)
// Per-record dword fields (offsets within the 56-byte record, recovered from the
// init / populate / layout decompiles):
//   +0  (word)  object/item id   (word_122E090 / word_122E790 ; 0 = empty)
//   +4  (dword) widget object id  (dword_122E094 / dword_122E794 ; -1 = none)
//   +8  (dword) x coordinate      (dword_122E098 / dword_122E798)
//   +12 (dword) y coordinate      (dword_122E064 / dword_122E764)
//   +28 (dword) "fill" = stock*capacity>>2   (dword_122E0BC / dword_122E7BC)
//   +32 (dword) effective stock              (dword_122E0C0 / dword_122E7C0)
//   +36 (dword) slot capacity                (dword_122E0C4 / dword_122E7C4)
// The init coordinates use a 4-column layout with a 64-px (1<<6) cell pitch and a
// 16-px left/top inset; the sell grid's y starts at 208 instead of 16.
// ---------------------------------------------------------------------------
inline constexpr int kSlotStrideDwords = 14;   // 56-byte record
inline constexpr int kSlotStrideBytes  = 56;

inline constexpr int kBuySlotCount  = 16;       // 4 cols x 4 rows
inline constexpr int kSellSlotCount = 8;        // 4 cols x 2 rows
inline constexpr int kSlotColumns   = 4;
inline constexpr int kSlotCellPitch = 64;       // 1<<6
inline constexpr int kSlotInset     = 16;       // grid origin inset (x and buy-y)
inline constexpr int kSellGridYBase = 208;      // sell grid y origin

// One item slot record (modelled as named fields; original is the 56-byte blob).
struct ItemSlot {
    i16 itemId = 0;     // +0   word_122E090[k]  (0 = empty slot)
    i32 objectId = -1;  // +4   dword_122E094[k] (-1 = no widget yet)
    i32 x = 0;          // +8   dword_122E098[k]  grid x
    i32 y = 0;          // +12  dword_122E064[k]  grid y
    i32 fill = -1;      // +28  dword_122E0BC[k]  stock*capacity>>2
    i32 stock = -1;     // +32  dword_122E0C0[k]  effective stock
    i32 capacity = -1;  // +36  dword_122E0C4[k]  slot capacity
};

// The two item grids (mirror of dword_122E064.. / dword_122E764..).
extern ItemSlot g_buySlots[kBuySlotCount];   // dword_122E064 base
extern ItemSlot g_sellSlots[kSellSlotCount]; // dword_122E764 base

// ---------------------------------------------------------------------------
// Drag-slot / slider-row staging tables (dword_122EDxx / dword_122EExx).
//
// InitSlotTables also clears the per-column staging tables the layout functions walk:
//   buy  column-state: dword_122EE00/04/08 (current/prev/widget), 4 entries, stride 5
//   sell column-state: dword_122EDD8/DC/E0, 2 entries, stride 5
// and two pairs of slot-id tables (dword_122EDF4/EC and 122EDC4/CC) used as scratch.
// We model only what the layout math reads: the per-column slot widget ids and the
// computed (x,y) origins. The 5-dword stride is preserved as a constant.
// ---------------------------------------------------------------------------
inline constexpr int kColStrideDwords = 5;
inline constexpr int kBuyColumns  = 4;   // dword_122EE00.. loop bound 20 step 5
inline constexpr int kSellColumns = 2;   // dword_122EDD8.. loop bound 10 step 5

// Drag-slot row layout strides (recovered from VIBE_TradePanel_LayoutDragSlots @0x50ad10).
inline constexpr int kBuyRowYStep   = 22;   // v9 += 22 per buy column
inline constexpr int kSellRowYBase  = 182;  // v14 = 182 (sell start y)
inline constexpr int kSellRowYStep  = 22;   // v14 += 22 per sell column
inline constexpr int kDragColXStep  = 68;   // 68*(v22-1) horizontal step within a row

// ---------------------------------------------------------------------------
// Slider-row widget layout (recovered from VIBE_TradePanel_BuildSliderRow @0x50b600).
//
// A slider row is a 28-dword (`a4 += 28`) record. The widgets it builds, and the
// pixel offsets each is laid out at relative to the row's (x,y) = (a4[2], a4[3]):
//   slider     : x+6,  y+0    (a4[8], VIBE_Widget_AddSliderToWindow ... w=48 h=100 ...)
//   icon/label : x+15, y+6    (a4[4])
//   button     : x+15, y+58   (a4[5]; gfx 1721 if a5&1, 1720 if a5&2)
//   buy label  : x+18, y+64   (a4[6]; "%s~ %li %s" price text)
//   sell label : x+18, y+73   (a4[7]; price text)
// The row's slider value model is filled from the item record (a4[11]=min, a4[12]=max,
// a4[13]=value) via VIBE_Object_SetValueOrText.
// ---------------------------------------------------------------------------
inline constexpr int kSliderRowStrideDwords = 28;

// Per-widget layout offsets within a slider row (x add, y add).
struct SliderRowOffsets { int dx; int dy; };
inline constexpr SliderRowOffsets kSliderOff = { 6, 0 };    // a4[8]
inline constexpr SliderRowOffsets kIconOff   = { 15, 6 };   // a4[4]
inline constexpr SliderRowOffsets kButtonOff = { 15, 58 };  // a4[5]
inline constexpr SliderRowOffsets kBuyLblOff = { 18, 63 };  // a4[6] (LayoutBounds y+63)
inline constexpr SliderRowOffsets kSellLblOff= { 18, 73 };  // a4[7]

// Slider-row build mode flags (a5): bit 0x01 = sell side, bit 0x02 = buy side.
inline constexpr u8 kSliderRowSell = 0x01;
inline constexpr u8 kSliderRowBuy  = 0x02;

// The slider widget geometry constants in VIBE_Widget_AddSliderToWindow's call.
inline constexpr int kSliderW   = 48;
inline constexpr int kSliderH   = 100;
inline constexpr int kSliderGfxA = 89;
inline constexpr int kSliderGfxB = 61;

// A slider-row model record (named subset of the 28-dword original at a4).
struct SliderRow {
    i16 itemId = 0;   // a4[0]  (word; 0 => empty row, destroyed)
    i32 rowX   = 0;   // a4[2]
    i32 rowY   = 0;   // a4[3]
    i32 iconId = -1;  // a4[4]
    i32 btnId  = -1;  // a4[5]
    i32 buyLbl = -1;  // a4[6]
    i32 sellLbl= -1;  // a4[7]
    i32 sliderId = -1;// a4[8]
    i32 childWin = -1;// a4[9]
    i32 min = 0;      // a4[11]
    i32 max = 0;      // a4[12]
    i32 value = 0;    // a4[13]
    // Absolute laid-out coordinates of the slider widget (rowX+winX+6, rowY+winY+0),
    // computed by BuildSliderRow via the LayoutBounds offsets. -1 until laid out.
    i32 sliderAbsX = -1;
    i32 sliderAbsY = -1;
};

// ---------------------------------------------------------------------------
// Synthetic shop / inventory state (the BuildBuilding/inventory source).
//
// In gilde.exe the populate routine reads the building's live inventory via
// VIBE_Inventory_GetEffectiveStock / GetSlotCapacity and the object enumerator
// VIBE_GameObject_QueryFind. Here a SyntheticShop supplies the same three values per
// item id, and a flat list of item ids, so PopulateInventorySlots is exercisable.
// ---------------------------------------------------------------------------
struct ShopItem {
    i16 id;
    i32 stock;
    i32 capacity;
    bool sellSide;   // true => routed to the sell grid (category test result)
};
struct SyntheticShop {
    const ShopItem* items = nullptr;
    int count = 0;
};

// gilde.exe 0x50854c — VIBE_TradePanel_InitSlotTables.
// Zero/-1-initialises both item grids and the column staging tables, assigning each
// slot its grid (x,y). Returns the last computed y (the original returns `result`, the
// final sell-grid y = ((7/4)<<6)+208 = 272). Recovered exactly.
int TradePanel_InitSlotTables();

// gilde.exe 0x50a794 — VIBE_TradePanel_PopulateInventorySlots (content population).
// Walks the shop's items; routes each to the buy or sell grid by its sellSide flag,
// stores stock/capacity/fill into the slot record. `fill = stock*capacity>>2` exactly
// (the original's `(stock*capacity) >> 2` unsigned shift). Returns the number of slots
// filled.
int TradePanel_PopulateInventorySlots(const SyntheticShop& shop);

// gilde.exe 0x50b600 (layout half) — VIBE_TradePanel_BuildSliderRow.
// Lays out a slider row: computes each child widget's absolute (x,y) from the row
// origin + the recovered per-widget offsets, and fills the slider value model from the
// item record. When the row's itemId is 0 the original destroys the row (clears all
// child ids to -1); we mirror that. `windowX`/`windowY` are the owning window's
// (word[2],word[3]) used by VIBE_Widget_LayoutBounds. Returns the laid-out widget ids
// via the SliderRow's *Id fields (allocated through the widget hook).
void TradePanel_BuildSliderRow(SliderRow& row, u8 mode, int windowX, int windowY);

// gilde.exe 0x50ad10 (coordinate half) — VIBE_TradePanel_LayoutDragSlots.
// Computes the drag-slot column origins for a grid: for column c (0-based) and row r
// the slot x = colOriginX + 68*r, y = rowYBase + 22*c. Fills `outX`/`outY` for each of
// `cols` columns. Used to verify the row stride math without touching widgets.
void TradePanel_LayoutDragSlotColumns(int cols, int rowYBase, int rowYStep,
                                      const int* colOriginX, int* outX, int* outY);

// Grid-coordinate helper used by InitSlotTables (exposed for tests):
//   buy slot  k: x = ((k%4)<<6)+16, y = ((k/4)<<6)+16
//   sell slot k: x = ((k%4)<<6)+16, y = ((k/4)<<6)+208
int TradePanel_SlotX(int k);
int TradePanel_BuySlotY(int k);
int TradePanel_SellSlotY(int k);

// Inventory hook (mockable) — the live-stock source the populate routine reads.
struct InventorySource {
    virtual ~InventorySource() = default;
    virtual i32 EffectiveStock(i16 /*itemId*/) { return 0; }  // VIBE_Inventory_GetEffectiveStock
    virtual i32 SlotCapacity(i16 /*itemId*/) { return 0; }    // VIBE_Inventory_GetSlotCapacity
};
void TradePanel_SetInventorySource(InventorySource* src);

} // namespace guild::gui

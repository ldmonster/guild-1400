#pragma once
// guild::gui — the market/trade ITEM-ROW panel: the 28-byte item-row slot table the
// stall/shop/inventory panels populate with icons, price labels and drag slots, plus
// the modal stall opener.  This is the GUI half the world-trade agent deferred:
//
//   VIBE_Trade_PopulateItemSlots   @0x51b57c — scan the building's objects, fill the
//       16-entry item-row slot table with an icon + price label per item, compute the
//       buy/sell price from the cached market rate, drop emptied rows, and (in drag mode)
//       wire the drag slots.  Re-sorts the visible rows when the set changed.
//   VIBE_Trade_AddItemSlotIcons    @0x51b888 — build the icon + name label + currency
//       label triple for up to 16 already-known item rows (the static board variant that
//       has no live-object scan).
//   VIBE_Trade_BuildSortedItemList @0x51b26c (LAYOUT half) — after the currency-name sort
//       (the sort core lives in world/market_stall.cpp, REUSED), reposition each visible
//       row's two widgets to their sorted row origin via Widget_LayoutBounds.
//   VIBE_MarketStall_OpenStall     @0x519830 — the modal stall opener: clear the info
//       panel + selection, dispatch the open/refresh/close panel events, and pump the
//       transport dispatcher loop until the stall object is gone.
//
// The 28-byte (7-dword) item-row slot record is a DIFFERENT table from the 56-byte
// buy/sell grid in trade_panel.h.  Each row:
//   +0  (dword) icon widget id      (-1 == empty/destroyed row)
//   +4  (dword) label widget id     (the price/name label; -1 == none)
//   +8  (dword) drag value rider    (slot[2]; preserved across the sort swaps)
//   +10 (word, hi half of +8 dword) currency id  (HIWORD(*(u32*)(slot+10)))
//   +12 (dword) item prototype id   (slot[3]; 0 == empty)
//   +20 (dword) cached data ptr     (slot[5]; from Object_GetDataPtr)
//   +24 (dword) price value         (slot[6])
//
// Widget creation routes through the REAL widget_create leaves (Input_AddIconToWindow,
// Object_AddTextLabel, Object_SetText) and the form/object setters; the live scene-object
// enumeration (GameObject_QueryFind/IterNext), the price model (Money_DivideByRate over
// the home exchange-rate table), the drag-slot edge and the modal panel-event dispatch
// are routed through small mockable hooks so the slot-fill/layout logic is testable in
// isolation.  The currency-name sort itself is REUSED from world/market_stall.

#include "gui/types.h"
#include <string>
#include <vector>

namespace guild::gui {

// ---------------------------------------------------------------------------
// Item-row slot record (the 28-byte / 7-dword original blob, named).
// ---------------------------------------------------------------------------
inline constexpr int kItemRowStrideDwords = 7;    // 28-byte record
inline constexpr int kItemRowStrideBytes  = 28;
inline constexpr int kMaxItemRowSlots     = 16;   // the 16-iteration loop bound

struct ItemRowSlot {
    i32 iconId   = -1;   // +0   icon/object widget id (-1 == empty)
    i32 labelId  = -1;   // +4   price/name label widget id
    i32 dragRider = 0;   // +8   slot[2]; HIWORD(this) is the currency id
    i16 currency = 0;    // +12  HIWORD(*(u32*)(slot+10)) — the buy/sell currency
    i32 protId   = 0;    // +12-dword slot[3]; item prototype id (0 == empty)
    i32 dataPtr  = 0;    // +20  slot[5]; cached Object_GetDataPtr handle
    i32 price    = 0;    // +24  slot[6]; computed price value
};

// ---------------------------------------------------------------------------
// Layout constants recovered from the three builders.
// ---------------------------------------------------------------------------
inline constexpr int kItemRowYStep   = 80;   // 80 * rowIndex vertical pitch
inline constexpr int kIconYBias      = 48;   // AddItemSlotIcons icon-y starts at 48
inline constexpr int kIconGfxBase    = 130;  // Input_AddIconToWindow flags arg
inline constexpr int kProtIconOffset = 206;  // protId + 206 == icon gfx id
inline constexpr int kIconXInsetW    = 48;   // ((winW - 48) >> 1) icon x
inline constexpr int kLabelXInsetW   = 190;  // ((winW - 190) >> 1) label x
inline constexpr int kNameLabelDY    = 48;   // name label y = iconY + 48
inline constexpr int kCurrLabelDY    = 63;   // currency label y = 80*row + 63
inline constexpr int kLabelWidth     = 190;  // label widget width (+20) forced to 190
inline constexpr int kSlotColor      = 67;   // Object_SetColor(.., 67)
inline constexpr int kCurrNameStride = 756;  // byte_13CD6A0 currency-name table stride

// ---------------------------------------------------------------------------
// Mockable edges (the GUI/scene leaves the builders forward to).
// ---------------------------------------------------------------------------

// The live scene-object enumeration the populate routine drives:
//   VIBE_GameObject_QueryFind(building, 1, 4, 9) -> first matching object
//   VIBE_GameObject_IterNext()                   -> next, 0 at end.
// We expose them as a single iterator the hook fills with the discovered item ids
// (each is the value compared against slot.protId).
struct ItemScanSource {
    virtual ~ItemScanSource() = default;
    // Discovered item prototype ids for `building` (the QueryFind enumeration order).
    virtual std::vector<i16> Enumerate(int /*building*/) { return {}; }
    // The current sell/buy price components for `protId` in the row currency:
    //   buy  = VIBE_Money_DivideByRate(obj+7,  currency)   (returns 0 => drop the row)
    //   sell = VIBE_Money_DivideByRate(obj+14, currency)
    // Return false to mirror the original's "buy price == 0 => destroy the row" drop.
    virtual bool Price(int /*building*/, i16 /*protId*/, i16 /*currency*/,
                       i32* outBuy, i32* outSell) { *outBuy = 0; *outSell = 0; return false; }
};
void TradeItemPanel_SetScanSource(ItemScanSource* src);

// The data-ptr acquisition the populate routine performs on each item icon:
//   VIBE_Object_GetDataPtr(iconWidget) — for a type-'A' icon this dereferences the
// icon's backing scene record (g_widgetData[icon]).  Defaults to the real
// Object_GetDataPtr; tests inject a stand-in so a bare synthetic icon (no backing
// record) doesn't dereference null.
using SlotDataFn = i32 (*)(int iconWidget);
void TradeItemPanel_SetSlotDataFn(SlotDataFn fn);

// The drag-slot edge (VIBE_DragSlot_AddItem) — records the (gfx, dataPtr) pairs added.
struct DragSlotSink {
    virtual ~DragSlotSink() = default;
    virtual void AddItem(int /*gfx*/, i32 /*dataPtr*/) {}
};
void TradeItemPanel_SetDragSink(DragSlotSink* sink);

// The modal panel-event dispatcher (VIBE_Interaction_DispatchPanelEvent) +
// transport pump (VIBE_TradeTransport_PanelDispatcher) used by OpenStall. The test
// hook records the event sequence and decides when the stall object closes.
struct StallModalHost {
    virtual ~StallModalHost() = default;
    // Resolve the clicked stall object id for (building, stallType); 0 == not found.
    virtual i32 ResolveStall(int /*building*/, i16 /*stallType*/) { return 0; }
    // VIBE_Interaction_DispatchPanelEvent(event, a, b, c).
    virtual void DispatchEvent(int /*event*/, i32 /*a*/, i32 /*b*/, int /*c*/) {}
    // One transport pump tick; return true while the stall object is still open.
    virtual bool Pump(int /*building*/) { return false; }
};

// ---------------------------------------------------------------------------
// gilde.exe 0x51b26c (LAYOUT half) — VIBE_Trade_BuildSortedItemList.
// The currency-name sort that precedes this is REUSED from world/market_stall.cpp
// (TradeBuildSortedItemList).  This routine performs the original's final pass: walk
// the sorted rows and, for each in-use row, re-lay its two widgets at the sorted row
// origin.  `slots` are the (already-sorted) rows; `winX`,`winY` are the current
// window's word[2]/word[3] origin.  Per row r the two widgets go to
//   icon : x = winX,                  y = winY + 80*r
//   label: x = winX + widthHalfBias,  y = winY + 80*r
// matching the original's two VIBE_Widget_LayoutBounds calls.  Fills `outIconXY`
// /`outLabelXY` (size = sorted-visible count) for verification, returns that count.
int TradeBuildSortedItemList_Layout(const std::vector<ItemRowSlot>& slots,
                                    int winX, int winY,
                                    std::vector<std::pair<int,int>>* outIconXY,
                                    std::vector<std::pair<int,int>>* outLabelXY);

// gilde.exe 0x51b888 — VIBE_Trade_AddItemSlotIcons.
// Builds the icon + name-label + currency-label triple for each non-empty row in
// `slots` (stops at the first row with protId == 0, max 16).  `winW` is the current
// window width (the >>16 of dword_62D298+6); `iconAsCheckbox` mirrors the original's
// a3 (nonzero => Input_AddIconToWindow checkbox icon; zero => Object_AddToWindow plain
// sprite).  `winSlot`/`formId` select the target window.  Item names come from
// `itemName(protId)`; currency names from `currName(currencyByte)`.  Returns the last
// icon-y bias (the original returns `result` = 48 + 80*count).
using NameLookupFn = std::string (*)(int id, void* ctx);
int TradeAddItemSlotIcons(std::vector<ItemRowSlot>& slots, int formId, int winSlot,
                          int winW, bool iconAsCheckbox,
                          NameLookupFn itemName, void* itemCtx,
                          NameLookupFn currName, void* currCtx);

// gilde.exe 0x51b57c — VIBE_Trade_PopulateItemSlots.
// Scans `building`'s objects (via the scan source), fills the 16-entry `slots` table:
// a newly-seen item gets the first free slot (icon + name label, color 67, label width
// 190); rows whose buy price resolves to 0 are destroyed (widgets cleared, protId 0);
// in drag mode (`dragMode`) the kept row's drag slot is wired and the other rows are
// disabled.  When the visible set changed, re-sorts via the world sort core.  `winW`
// is the current window width.  Returns the selected (dragged) row index, or -1.
// `homeCurrency`/`currName`/`currCtx` feed the re-sort's currency-name key.
int TradePopulateItemSlots(int building, std::vector<ItemRowSlot>& slots,
                           int formId, int winSlot, int winW, bool dragMode,
                           i32 homeCurrency, std::string (*currName)(i32, void*),
                           void* currCtx);

// gilde.exe 0x519830 — VIBE_MarketStall_OpenStall.
// The modal stall opener.  Resolves the stall object for (`building`,`stallType`);
// dispatches the open event (0x17), pumps the transport dispatcher until the stall
// closes, then dispatches the close (0x18) and final (0x17) events.  Returns the
// stall object id that was opened (0 if none).  `prevForm` mirrors dword_631768: when
// != -1 the host's objects are hidden during the modal and re-shown after.
i32 MarketStallOpenStall(StallModalHost& host, int building, i16 stallType,
                         int prevForm, int* outVisibleToggles);

} // namespace guild::gui

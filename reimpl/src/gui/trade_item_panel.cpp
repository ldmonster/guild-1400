// guild::gui — implementation of the market/trade item-row panel builders.
// See trade_item_panel.h for the recovered field layout and provenance.

#include "gui/trade_item_panel.h"

#include "gui/object.h"          // g_widgets, Object_GetDataPtr
#include "gui/window.h"          // g_currentWindow, Object_AddToWindow
#include "gui/form.h"            // Form_SelectWindow
#include "gui/widget_create.h"   // Input_AddIconToWindow, Object_AddTextLabel, Object_SetText
#include "gui/window_render.h"   // Widget_LayoutBounds
#include "world/market_stall.h"  // TradeBuildSortedItemList (the sort core, REUSED)

#include <utility>
#include <vector>

namespace guild::gui {

// ---------------------------------------------------------------------------
// Mockable edge hooks (defaults are inert).
// ---------------------------------------------------------------------------
namespace {
ItemScanSource* g_scanSrc = nullptr;
DragSlotSink*   g_dragSink = nullptr;
ItemScanSource  g_scanDefault;
DragSlotSink    g_dragDefault;
SlotDataFn      g_slotDataFn = nullptr;

ItemScanSource& Scan() { return g_scanSrc ? *g_scanSrc : g_scanDefault; }
DragSlotSink&   Drag() { return g_dragSink ? *g_dragSink : g_dragDefault; }
i32 SlotData(int iconWidget) {
    // VIBE_Object_GetDataPtr(iconWidget); test-overridable to avoid dereferencing a
    // bare synthetic icon's (null) backing record.
    return g_slotDataFn ? g_slotDataFn(iconWidget) : Object_GetDataPtr(iconWidget);
}
}  // namespace

void TradeItemPanel_SetScanSource(ItemScanSource* src) { g_scanSrc = src; }
void TradeItemPanel_SetDragSink(DragSlotSink* sink)    { g_dragSink = sink; }
void TradeItemPanel_SetSlotDataFn(SlotDataFn fn)       { g_slotDataFn = fn; }

// ===========================================================================
// GUI-leaf placeholders (ONE definition; the real leaves are not yet translated).
//   VIBE_Object_SetColor   @0x41e614 — writes the widget's color word (+112). The
//     per-type color side-tables (button word_67EDFC / input dword_6951B4) are
//     renderer-cluster data; the model write (+112) is faithful here.
//   VIBE_Object_SetEnabled @0x41e318 — sets +56/+76 disable mirrors to (value==0).
// ===========================================================================

// gilde.exe 0x41e614 — VIBE_Object_SetColor (idx@eax, color@edx)
static void Object_SetColor(int idx, int color) {
    if (idx < 0 || idx >= kMaxWidgets) return;
    g_widgets[idx].at<i16>(112) = static_cast<i16>(color);  // +112 color word
}

// gilde.exe 0x41e318 — VIBE_Object_SetEnabled (idx@eax, value@edx)
static void Object_SetEnabled(int idx, int value) {
    if (idx == -1 || idx >= kMaxWidgets) return;
    g_widgets[idx].disabledA() = (value == 0);  // +56
    g_widgets[idx].disabledB() = (value == 0);  // +76
}

// Current window width: dword_62D298 read at +6, high word == window w() at +8.
static int CurWinW() {
    return g_currentWindow ? g_currentWindow->w() : 0;
}

// ===========================================================================
// gilde.exe 0x51b888 — VIBE_Trade_AddItemSlotIcons.
// ===========================================================================
int TradeAddItemSlotIcons(std::vector<ItemRowSlot>& slots, int formId, int winSlot,
                          int winW, bool iconAsCheckbox,
                          NameLookupFn itemName, void* itemCtx,
                          NameLookupFn currName, void* currCtx) {
    // v6 = icon-y accumulator (0, +80 per row); result/v19 = name-label-y (48, +80).
    int iconY  = 0;        // v6
    int result = kIconYBias;  // 48; the original's `result`/LOWORD(v19) base
    int nameY  = kIconYBias;  // v19 low word (48, +80 per row)

    // `if (v8)` — bail entirely when the first row is empty (slot[0].protId == 0).
    if (slots.empty() || slots[0].protId == 0)
        return result;

    const int iconX  = (winW - kIconXInsetW) >> 1;   // ((winW-48)>>1)
    const int labelX = (winW - kLabelXInsetW) >> 1;  // ((winW-190)>>1)

    int row = 0;  // v4
    do {
        ItemRowSlot& s = slots[row];
        Form_SelectWindow(formId, winSlot);

        // a3 (iconAsCheckbox): nonzero => checkbox icon, zero => plain sprite object.
        if (iconAsCheckbox) {
            s.iconId = Input_AddIconToWindow(iconX, iconY, kIconGfxBase,
                                             s.protId + kProtIconOffset, winSlot);
        } else {
            s.iconId = Object_AddToWindow(winSlot, static_cast<i16>(kItemRowYStep * row),
                                          static_cast<i16>(iconX),
                                          s.protId + kProtIconOffset);
            // plain-sprite branch sets the +72 button flag.
            if (s.iconId >= 0 && s.iconId < kMaxWidgets)
                g_widgets[s.iconId].btnFlagB() = 1;  // +72
        }
        // both branches set the icon's +64 flag.
        if (s.iconId >= 0 && s.iconId < kMaxWidgets)
            g_widgets[s.iconId].at<i32>(64) = 1;

        // Name label: AddTextLabel((winW-190)>>1, nameY, win, ...) then SetText("%S",name).
        std::string iname = itemName ? itemName(s.protId, itemCtx) : std::string();
        s.labelId = Object_AddTextLabel(static_cast<i16>(labelX), static_cast<i16>(nameY),
                                        winSlot, iname.c_str());
        Object_SetText(s.labelId, iname.c_str());
        Object_SetColor(s.iconId, kSlotColor);
        Object_SetColor(s.labelId, kSlotColor);
        if (s.labelId >= 0 && s.labelId < kMaxWidgets) {
            g_widgets[s.labelId].at<i32>(88) = 1;             // +88 width-override flag
            g_widgets[s.labelId].at<i16>(20) = kLabelWidth;   // +20 width = 190
        }

        // Currency label: AddTextLabel((winW-190)>>1, 80*row+63, win, ...) "%s" currName.
        std::string cname = currName ? currName(s.currency, currCtx) : std::string();
        int currLbl = Object_AddTextLabel(static_cast<i16>(labelX),
                                          static_cast<i16>(kItemRowYStep * row + kCurrLabelDY),
                                          winSlot, cname.c_str());
        // (original stashes this in slot+8; keep the rider for sort parity.)
        s.dataPtr = currLbl;
        Object_SetText(currLbl, cname.c_str());
        Object_SetColor(currLbl, kSlotColor);
        if (currLbl >= 0 && currLbl < kMaxWidgets) {
            g_widgets[currLbl].at<i32>(88) = 1;
            g_widgets[currLbl].at<i16>(20) = kLabelWidth;
        }

        iconY  += kItemRowYStep;   // v6 += 80
        nameY  += kItemRowYStep;   // LOWORD(v19) += 80
        result  = nameY;           // mirrors the original's `result` walking with v19
        ++row;                     // v4
        // while (v4 < 16 && slot[v4].protId != 0)
    } while (row < kMaxItemRowSlots && row < static_cast<int>(slots.size()) &&
             slots[row].protId != 0);

    return result;
}

// ===========================================================================
// gilde.exe 0x51b57c — VIBE_Trade_PopulateItemSlots.
// ===========================================================================
int TradePopulateItemSlots(int building, std::vector<ItemRowSlot>& slots,
                           int formId, int winSlot, int winW, bool dragMode,
                           i32 homeCurrency, std::string (*currName)(i32, void*),
                           void* currCtx) {
    if (static_cast<int>(slots.size()) < kMaxItemRowSlots)
        slots.resize(kMaxItemRowSlots);

    int changed = 0;        // v38
    int selected = -1;      // v34 (the returned dragged row)
    const int iconX  = (winW - kIconXInsetW) >> 1;
    const int labelX = (winW - kLabelXInsetW) >> 1;

    // ---- pass 1: discover items, append newly-seen ids to a free slot. ----
    std::vector<i16> found = Scan().Enumerate(building);
    for (i16 protId : found) {
        // already present?
        bool present = false;
        for (int j = 0; j < kMaxItemRowSlots; ++j) {
            if (slots[j].protId == protId) { present = true; break; }
        }
        if (present) continue;

        // find the first free slot (protId == 0).
        int free = -1;
        for (int k = 0; k < kMaxItemRowSlots; ++k) {
            if (slots[k].protId == 0) { free = k; break; }
        }
        if (free < 0) continue;

        ItemRowSlot& s = slots[free];
        s.protId = protId;
        Form_SelectWindow(formId, winSlot);
        int y = kItemRowYStep * free;
        s.iconId = Input_AddIconToWindow(iconX, y, kIconGfxBase,
                                         s.protId + kProtIconOffset, winSlot);
        if (s.iconId >= 0 && s.iconId < kMaxWidgets)
            g_widgets[s.iconId].at<i32>(64) = 1;   // +64
        // name label (the original uses byte_621554 / the empty default text).
        s.labelId = Object_AddTextLabel(static_cast<i16>(labelX), static_cast<i16>(y + kNameLabelDY),
                                        winSlot, "");
        Object_SetColor(s.iconId, kSlotColor);
        Object_SetColor(s.labelId, kSlotColor);
        if (s.labelId >= 0 && s.labelId < kMaxWidgets) {
            g_widgets[s.labelId].at<i32>(88) = 1;
            g_widgets[s.labelId].at<i16>(20) = kLabelWidth;
        }
        if (dragMode && s.iconId >= 0 && s.iconId < kMaxWidgets)
            g_widgets[s.iconId].at<i32>(56) = 1;   // +56 (a5 branch)
        changed = 1;
    }

    // ---- drag pre-enable: in drag mode enable every present icon first. ----
    if (dragMode) {
        for (auto& s : slots)
            if (s.iconId != -1) Object_SetEnabled(s.iconId, 1);
    }

    // ---- pass 2: price each row; drop rows that price to 0. ----
    for (int k = 0; k < kMaxItemRowSlots; ++k) {
        ItemRowSlot& s = slots[k];
        if (s.iconId == -1) continue;

        i32 data = SlotData(s.iconId);  // VIBE_Object_GetDataPtr(icon)
        s.dataPtr = data;          // slot[5]
        i32 buy = 0, sell = 0;
        bool ok = Scan().Price(building, s.protId, s.currency, &buy, &sell);
        if (ok && buy != 0) {
            s.price = sell;        // slot[6]
            // SetValueOrText(icon, 0, sell, data, 0) then label text "%I %s".
            // (The text format is a renderer edge; the model stores the price.)
            if (s.labelId >= 0 && s.labelId < kMaxWidgets)
                g_widgets[s.labelId].at<i16>(20) = kLabelWidth;
        } else {
            // destroy the row.
            s.labelId = -1;
            s.iconId  = -1;
            s.dataPtr = 0;
            changed = 1;
            s.protId = 0;
        }

        if (dragMode && data) {
            Drag().AddItem(s.protId + kProtIconOffset, data);
            // disable every OTHER row's icon.
            for (int j = 0; j < kMaxItemRowSlots; ++j) {
                if (slots[j].iconId != -1 && k != j)
                    Object_SetEnabled(slots[j].iconId, 0);
            }
            selected = k;          // v34 = k
        }
    }

    // ---- re-sort the visible set when it changed. ----
    if (changed) {
        Form_SelectWindow(formId, winSlot);
        // Build the world-side SortSlot view and re-order via the REUSED sort core,
        // then mirror the order back onto the gui rows.
        std::vector<guild::world::SortSlot> view;
        view.reserve(slots.size());
        for (auto& s : slots) {
            guild::world::SortSlot v;
            v.id = s.iconId;
            v.secondaryId = s.labelId;
            v.currency = s.currency;
            v.payload = static_cast<int>(&s - &slots[0]);
            view.push_back(v);
        }
        guild::world::TradeBuildSortedItemList(view, homeCurrency, currName, currCtx);
        std::vector<ItemRowSlot> reordered;
        reordered.reserve(slots.size());
        int sortable = guild::world::TradeSortableCount(view);
        for (int i = 0; i < sortable; ++i)
            reordered.push_back(slots[view[i].payload]);
        for (int i = sortable; i < static_cast<int>(slots.size()); ++i)
            reordered.push_back(slots[i]);
        slots.swap(reordered);
    }

    return selected;
}

// ===========================================================================
// gilde.exe 0x51b26c (LAYOUT half) — VIBE_Trade_BuildSortedItemList.
// ===========================================================================
int TradeBuildSortedItemList_Layout(const std::vector<ItemRowSlot>& slots,
                                    int winX, int winY,
                                    std::vector<std::pair<int,int>>* outIconXY,
                                    std::vector<std::pair<int,int>>* outLabelXY) {
    if (outIconXY)  outIconXY->clear();
    if (outLabelXY) outLabelXY->clear();

    const int labelXBias = (CurWinW() - kLabelXInsetW) >> 1;  // matches the populate label x
    (void)winX;  // the original derives x from the current window origin directly

    int visible = 0;  // v28
    int n = static_cast<int>(slots.size());
    for (int i = 0; i < n; ++i) {
        const ItemRowSlot& s = slots[i];
        if (s.iconId == -1) continue;   // *v17 != -1 guard

        int rowY = winY + kItemRowYStep * visible;   // *(dword_62D298+6) + 80*v28

        // icon widget -> (winX, rowY)
        Widget_LayoutBounds(winX, rowY, s.iconId);
        if (outIconXY) outIconXY->emplace_back(winX, rowY);

        // label widget -> (winX + labelXBias, rowY)
        Widget_LayoutBounds(winX + labelXBias, rowY, s.labelId);
        if (outLabelXY) outLabelXY->emplace_back(winX + labelXBias, rowY);

        ++visible;
    }
    return visible;
}

// ===========================================================================
// gilde.exe 0x519830 — VIBE_MarketStall_OpenStall.
// ===========================================================================
i32 MarketStallOpenStall(StallModalHost& host, int building, i16 stallType,
                         int prevForm, int* outVisibleToggles) {
    int toggles = 0;

    // dword_631768 != -1 => hide the previous form's objects for the modal duration.
    if (prevForm != -1) { host.DispatchEvent(/*hide-form*/ -2, prevForm, 0, 0); ++toggles; }

    // Resolve the clicked stall object (QueryFind(building+93, 1, 0, stallType)).
    i32 stall = host.ResolveStall(building, stallType);  // dword_63174C
    if (stall) {
        // open event 0x17.
        host.DispatchEvent(0x17, stall, 0, 0);
    }

    // pump the transport dispatcher while the stall object is still open.
    while (stall && host.Pump(building)) {
        // dword_631754 = 1; loop body driven by the host.
    }

    // close (0x18) then final (0x17) events.
    host.DispatchEvent(0x18, 0, stallType, 0);
    host.DispatchEvent(0x17, 0 /*dword_63174C reset*/, stallType, 1);

    i32 opened = stall;

    if (prevForm != -1) { host.DispatchEvent(/*show-form*/ -1, prevForm, 1, 0); ++toggles; }

    if (outVisibleToggles) *outVisibleToggles = toggles;
    return opened;
}

}  // namespace guild::gui

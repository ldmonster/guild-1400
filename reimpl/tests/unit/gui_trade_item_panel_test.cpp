// Unit tests for guild::gui trade/market item-row panel builders
// (trade_item_panel.{h,cpp}).  Golden-vector checks on the recovered slot-row
// layout math, the icon/label build sequence, the price-driven row drop, and the
// sorted-layout repositioning pass.
#include "gui/trade_item_panel.h"
#include "gui/object.h"
#include "gui/window.h"
#include "gui/form.h"
#include "gui/widget_create.h"
#include "tests/framework/test.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace guild::gui;

// ---- Strong overrides of the widget_create renderer/metric edges (deterministic). ----
namespace guild::gui {
i16  GfxMetricWord(int, int byteOff)  { return byteOff == 82 ? 7 : 0; }
i32  GfxMetricDword(int, int byteOff) { return (byteOff == 78 || byteOff == 80) ? (8 << 16) : 0; }
i16  SliderTrackExtent(int, int) { return 20; }
void* SceneStateFor(int gfxId)   { return reinterpret_cast<void*>(static_cast<std::intptr_t>(0x1000 + gfxId)); }
int  GlyphAdvance(void*, int)    { return 3; }
int  ButtonBankFrameCount(int)   { return 5; }
}  // namespace guild::gui

// ---- Counting override of the layout edge (weak in window_render.cpp). ----
namespace guild::gui {
struct LayoutCall { int x, y, idx; };
}
static std::vector<guild::gui::LayoutCall>& LayoutCalls() {
    static std::vector<guild::gui::LayoutCall> v; return v;
}
namespace guild::gui {
void Widget_LayoutBounds(int x, int y, int widgetIdx) {
    LayoutCalls().push_back({x, y, widgetIdx});
}
}  // namespace guild::gui

namespace {
// Synthetic slot-data: every icon resolves to a stable non-null handle (mirrors a
// real icon's backing scene record without dereferencing a null one).
i32 FakeSlotData(int iconWidget) { return iconWidget + 0x1000; }
void Reset() {
    ResetGuiState(); ResetWidgetCreate(); LayoutCalls().clear();
    TradeItemPanel_SetSlotDataFn(FakeSlotData);
}
int OpenWin(i16 x, i16 y, i16 w, i16 h) { return Window_Create(x, y, w, h, 0); }

std::string ItemName(int id, void*) { return "item" + std::to_string(id); }
std::string CurrName(int id, void*) { return "cur" + std::to_string(id); }
std::string CurrName32(i32 id, void*) { return "cur" + std::to_string(id); }
}  // namespace

// ---------------------------------------------------------------------------
// AddItemSlotIcons: icon + name + currency triple per row, geometry & stop rule.
// ---------------------------------------------------------------------------
TEST(GuiTradeItemPanel, AddIconsBuildsTripleAndStops) {
    Reset();
    int win = OpenWin(0, 0, 400, 600);
    CHECK_EQ(win, 0);

    std::vector<ItemRowSlot> slots(kMaxItemRowSlots);
    slots[0].protId = 5;  slots[0].currency = 1;
    slots[1].protId = 9;  slots[1].currency = 2;
    // slots[2].protId == 0 -> the do/while stops after row 1.

    int ret = TradeAddItemSlotIcons(slots, /*form*/0, win, /*winW*/400,
                                    /*iconAsCheckbox*/true,
                                    ItemName, nullptr, CurrName, nullptr);
    // result walks 48 + 80*count: two rows -> 48 + 80*2 = 208.
    CHECK_EQ(ret, kIconYBias + kItemRowYStep * 2);

    // both built rows produced an icon + a name label.
    CHECK(slots[0].iconId >= 0);
    CHECK(slots[0].labelId >= 0);
    CHECK(slots[1].iconId >= 0);
    CHECK(slots[1].labelId >= 0);

    // icon x == (winW-48)>>1 == 176 ; name label x == (winW-190)>>1 == 105.
    CHECK_EQ((int)g_widgets[slots[0].iconId].x(), (400 - kIconXInsetW) >> 1);
    // row 0 name label y (winY + nameY=48); row 1 name label y == winY + 48 + 80.
    CHECK_EQ((int)g_widgets[slots[0].labelId].y(), 0 + kNameLabelDY);
    CHECK_EQ((int)g_widgets[slots[1].labelId].y(), 0 + kNameLabelDY + kItemRowYStep);

    // colour 67 + forced label width 190 + width-override flag.
    CHECK_EQ((int)g_widgets[slots[0].iconId].at<i16>(112), kSlotColor);
    CHECK_EQ((int)g_widgets[slots[0].labelId].at<i16>(20), kLabelWidth);
    CHECK_EQ((int)g_widgets[slots[0].labelId].at<i32>(88), 1);

    // icon +64 flag set on both rows.
    CHECK_EQ((int)g_widgets[slots[0].iconId].at<i32>(64), 1);
    CHECK_EQ((int)g_widgets[slots[1].iconId].at<i32>(64), 1);
}

TEST(GuiTradeItemPanel, AddIconsEmptyFirstRowBails) {
    Reset();
    int win = OpenWin(0, 0, 400, 600);
    std::vector<ItemRowSlot> slots(kMaxItemRowSlots);  // all protId == 0
    int ret = TradeAddItemSlotIcons(slots, 0, win, 400, true,
                                    ItemName, nullptr, CurrName, nullptr);
    CHECK_EQ(ret, kIconYBias);             // bails, returns base 48
    CHECK_EQ(slots[0].iconId, -1);         // nothing built
}

TEST(GuiTradeItemPanel, AddIconsPlainSpriteBranchSets72Flag) {
    Reset();
    int win = OpenWin(0, 0, 320, 480);
    std::vector<ItemRowSlot> slots(kMaxItemRowSlots);
    slots[0].protId = 3; slots[0].currency = 0;
    TradeAddItemSlotIcons(slots, 0, win, 320, /*iconAsCheckbox*/false,
                          ItemName, nullptr, CurrName, nullptr);
    CHECK(slots[0].iconId >= 0);
    // plain-sprite branch sets +72.
    CHECK_EQ((int)g_widgets[slots[0].iconId].at<i32>(72), 1);
}

// ---------------------------------------------------------------------------
// PopulateItemSlots: discover -> fill, price -> keep/drop, return selected.
// ---------------------------------------------------------------------------
namespace {
struct FakeScan : ItemScanSource {
    std::vector<i16> ids;
    // protId -> (buy, sell, ok)
    bool priceOk = true; i32 buy = 10, sell = 7;
    std::vector<i16> Enumerate(int) override { return ids; }
    bool Price(int, i16, i16, i32* b, i32* s) override {
        *b = buy; *s = sell; return priceOk;
    }
};
struct FakeDrag : DragSlotSink {
    std::vector<std::pair<int,i32>> added;
    void AddItem(int gfx, i32 data) override { added.emplace_back(gfx, data); }
};
}  // namespace

TEST(GuiTradeItemPanel, PopulateFillsNewSlots) {
    Reset();
    int win = OpenWin(0, 0, 400, 600);
    FakeScan scan; scan.ids = {5, 9, 12};
    TradeItemPanel_SetScanSource(&scan);

    std::vector<ItemRowSlot> slots(kMaxItemRowSlots);
    int sel = TradePopulateItemSlots(/*building*/1, slots, /*form*/0, win, /*winW*/400,
                                     /*dragMode*/false, /*home*/1, CurrName32, nullptr);
    CHECK_EQ(sel, -1);  // no drag mode -> no selection

    // three slots filled (protId set, icon built, price kept).
    int filled = 0;
    for (auto& s : slots) if (s.protId != 0) ++filled;
    CHECK_EQ(filled, 3);
    TradeItemPanel_SetScanSource(nullptr);
}

TEST(GuiTradeItemPanel, PopulateDropsZeroPricedRows) {
    Reset();
    int win = OpenWin(0, 0, 400, 600);
    FakeScan scan; scan.ids = {5}; scan.priceOk = false; scan.buy = 0;
    TradeItemPanel_SetScanSource(&scan);

    std::vector<ItemRowSlot> slots(kMaxItemRowSlots);
    TradePopulateItemSlots(1, slots, 0, win, 400, false, 1, CurrName32, nullptr);

    // the row that priced to 0 was destroyed.
    int alive = 0;
    for (auto& s : slots) if (s.iconId != -1) ++alive;
    CHECK_EQ(alive, 0);
    TradeItemPanel_SetScanSource(nullptr);
}

TEST(GuiTradeItemPanel, PopulateDragModeWiresSlotAndSelects) {
    Reset();
    int win = OpenWin(0, 0, 400, 600);
    FakeScan scan; scan.ids = {7}; scan.priceOk = true; scan.buy = 5; scan.sell = 4;
    FakeDrag drag;
    TradeItemPanel_SetScanSource(&scan);
    TradeItemPanel_SetDragSink(&drag);

    std::vector<ItemRowSlot> slots(kMaxItemRowSlots);
    int sel = TradePopulateItemSlots(1, slots, 0, win, 400, /*dragMode*/true,
                                     1, CurrName32, nullptr);
    CHECK(sel >= 0);                    // a row was selected
    CHECK_EQ((int)drag.added.size(), 1);
    CHECK_EQ(drag.added[0].first, 7 + kProtIconOffset);  // protId + 206 gfx
    CHECK(drag.added[0].second != 0);                    // a real data handle was passed

    TradeItemPanel_SetScanSource(nullptr);
    TradeItemPanel_SetDragSink(nullptr);
}

// ---------------------------------------------------------------------------
// BuildSortedItemList layout half: visible rows -> two LayoutBounds per row.
// ---------------------------------------------------------------------------
TEST(GuiTradeItemPanel, SortedLayoutRepositionsVisibleRows) {
    Reset();
    int win = OpenWin(0, 0, 400, 600);
    (void)win;
    std::vector<ItemRowSlot> slots(3);
    slots[0].iconId = 10; slots[0].labelId = 11;
    slots[1].iconId = -1; slots[1].labelId = -1;  // hole -> skipped
    slots[2].iconId = 20; slots[2].labelId = 21;

    std::vector<std::pair<int,int>> ic, lb;
    int vis = TradeBuildSortedItemList_Layout(slots, /*winX*/30, /*winY*/40, &ic, &lb);
    CHECK_EQ(vis, 2);   // two visible rows

    // row 0 at y = 40 + 80*0 ; row 1 (the 3rd slot) at y = 40 + 80*1.
    CHECK_EQ(ic[0].second, 40);
    CHECK_EQ(ic[1].second, 40 + kItemRowYStep);
    CHECK_EQ(ic[0].first, 30);              // icon x = winX
    // four LayoutBounds calls total (icon+label per visible row).
    CHECK_EQ((int)LayoutCalls().size(), 4);
}

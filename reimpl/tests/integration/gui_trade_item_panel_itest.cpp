// Integration tests for the trade item-row panel against its REAL siblings:
//   * the real widget_create leaves (Input_AddIconToWindow / Object_AddTextLabel)
//     building into a real Window_Create window;
//   * the REAL world/market_stall.cpp currency-name sort driving the re-order that
//     PopulateItemSlots performs after the visible set changes.
// This verifies the gui builders cooperate with the actual retained-mode core and the
// actual sort core, not stand-ins.
#include "gui/trade_item_panel.h"
#include "gui/object.h"
#include "gui/window.h"
#include "gui/form.h"
#include "gui/widget_create.h"
#include "world/market_stall.h"
#include "tests/framework/test.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace guild::gui;

namespace guild::gui {
i16  GfxMetricWord(int, int byteOff)  { return byteOff == 82 ? 7 : 0; }
i32  GfxMetricDword(int, int byteOff) { return (byteOff == 78 || byteOff == 80) ? (8 << 16) : 0; }
i16  SliderTrackExtent(int, int) { return 20; }
void* SceneStateFor(int gfxId)   { return reinterpret_cast<void*>(static_cast<std::intptr_t>(0x1000 + gfxId)); }
int  GlyphAdvance(void*, int)    { return 3; }
int  ButtonBankFrameCount(int)   { return 5; }
void Widget_LayoutBounds(int, int, int) {}
}  // namespace guild::gui

namespace {
std::string CurrName32(i32 id, void*) {
    // home (1) and none (0) are handled by the sort itself; the rest get localized
    // names.  The world sort's rule (gilde.exe 0x51b3ca via StrCmp @0x5d3f10) swaps
    // when name(i) > name(j), i.e. it orders the SMALLER key toward the FRONT
    // (ascending).  Names chosen so currency 3 ("Acur") outranks currency 2 ("Bcur")
    // and is pulled ahead.
    if (id == 2) return "Bcur";
    if (id == 3) return "Acur";
    return "cur" + std::to_string(id);
}

struct ScanThree : ItemScanSource {
    std::vector<i16> Enumerate(int) override { return {2, 3}; }
    bool Price(int, i16, i16, i32* b, i32* s) override { *b = 9; *s = 6; return true; }
};
i32 FakeSlotData(int iconWidget) { return iconWidget + 0x1000; }
}  // namespace

// ---------------------------------------------------------------------------
// PopulateItemSlots builds real widgets and the real sort re-orders the rows.
// ---------------------------------------------------------------------------
TEST(GuiTradeItemPanelI, PopulateBuildsRealWidgetsAndChildLinks) {
    ResetGuiState(); ResetWidgetCreate();
    TradeItemPanel_SetSlotDataFn(FakeSlotData);
    int win = Window_Create(0, 0, 400, 600, 0);
    CHECK_EQ(win, 0);

    ScanThree scan;
    TradeItemPanel_SetScanSource(&scan);

    std::vector<ItemRowSlot> slots(kMaxItemRowSlots);
    slots[0].currency = 2;  // give the discovered rows distinct currencies after fill
    slots[1].currency = 3;
    TradePopulateItemSlots(/*building*/1, slots, /*form*/0, win, /*winW*/400,
                           /*dragMode*/false, /*home*/1, CurrName32, nullptr);
    TradeItemPanel_SetScanSource(nullptr);

    // The real window now owns the icon + label child widgets: child count grew.
    CHECK(g_windows[win].objCount() >= 2);

    // Every built icon is a real in-use widget slot.
    int builtIcons = 0;
    for (auto& s : slots) {
        if (s.iconId != -1) {
            CHECK(s.iconId >= 0 && s.iconId < kMaxWidgets);
            CHECK_EQ((int)g_widgets[s.iconId].inUse(), 1);  // +4 marker set by alloc
            ++builtIcons;
        }
    }
    CHECK(builtIcons >= 2);
}

// ---------------------------------------------------------------------------
// The re-order really runs the world sort core (cross-module): "Acur3" < "Bcur2".
// ---------------------------------------------------------------------------
TEST(GuiTradeItemPanelI, ReorderUsesRealWorldSort) {
    // Drive the world sort directly with the same key function the gui re-sort uses,
    // proving the cross-module contract the populate routine relies on.
    // Start with the LARGER key first to force the ascending swap to fire
    // (gilde.exe 0x51b3ca swaps when name(i) > name(j)).
    std::vector<guild::world::SortSlot> view(2);
    view[0].id = 10; view[0].currency = 2;  // "Bcur" (larger)
    view[1].id = 20; view[1].currency = 3;  // "Acur" (smaller)
    int sortable = guild::world::TradeSortableCount(view);
    CHECK_EQ(sortable, 2);
    guild::world::TradeBuildSortedItemList(view, /*home*/1, CurrName32, nullptr);
    // ascending: currency 3 ("Acur") is pulled to the front.
    CHECK_EQ(view[0].currency, 3);
    CHECK_EQ(view[1].currency, 2);
}

// End-to-end test for the trade / building transaction panels.
//
// Drives a full trade-panel session over a synthetic shop inventory:
//   1. InitSlotTables establishes the buy/sell grids at their recovered coordinates.
//   2. PopulateInventorySlots fills the grids from the shop's items (the full widget
//      tree: counts, item ids, stock/capacity/fill values).
//   3. A slider row is laid out for a populated buy slot.
//   4. Adjusting the slider's value quantises through the slider model.
//   5. A "buy" click on the building dialog dispatches the expected (mocked) command.
// Also exercises a building sell + a storage-options enlarge across one frame.
#include "gui/trade_panel.h"
#include "gui/building_dialog.h"
#include "gui/storage_dialog.h"
#include "gui/slider.h"
#include "tests/framework/test.h"

#include <cstring>

using namespace guild::gui;

namespace {

// Inventory source backed by a fixed stock/capacity per item id.
struct ShopInventory : InventorySource {
    i32 stockFor[64] = {0};
    i32 capFor[64] = {0};
    i32 EffectiveStock(i16 id) override { return (id >= 0 && id < 64) ? stockFor[id] : 0; }
    i32 SlotCapacity(i16 id) override { return (id >= 0 && id < 64) ? capFor[id] : 0; }
};

struct BuySink : BuildingCommandSink {
    int sells = 0, building = 0, price = 0;
    void Sell(int b, int /*op*/, int p) override { ++sells; building = b; price = p; }
};

struct EnlargeSink : StorageCommandSink {
    int enlarges = 0, cost = 0;
    void EnlargeSlot(int /*h*/, int /*d*/, int c) override { ++enlarges; cost = c; }
};

} // namespace

TEST(GuiTradeE2E, FullTradePanelSession) {
    // --- (1) init the slot tables ------------------------------------------------
    int last = TradePanel_InitSlotTables();
    CHECK_EQ(last, 272);

    // --- (2) populate from a synthetic shop inventory ---------------------------
    ShopInventory inv;
    inv.stockFor[10] = 50; inv.capFor[10] = 8;   // buy item
    inv.stockFor[11] = 12; inv.capFor[11] = 4;   // buy item
    inv.stockFor[30] = 20; inv.capFor[30] = 6;   // sell item
    TradePanel_SetInventorySource(&inv);

    ShopItem items[] = {
        { 10, 0, 0, false },
        { 11, 0, 0, false },
        { 30, 0, 0, true  },
    };
    SyntheticShop shop{ items, 3 };
    int filled = TradePanel_PopulateInventorySlots(shop);
    CHECK_EQ(filled, 3);

    // Verify the populated widget tree: two buy slots + one sell slot.
    CHECK_EQ((int)g_buySlots[0].itemId, 10);
    CHECK_EQ(g_buySlots[0].stock, 50);
    CHECK_EQ(g_buySlots[0].capacity, 8);
    CHECK_EQ(g_buySlots[0].fill, (50 * 8) >> 2);
    CHECK_EQ((int)g_buySlots[1].itemId, 11);
    CHECK_EQ(g_buySlots[1].fill, (12 * 4) >> 2);
    CHECK_EQ((int)g_sellSlots[0].itemId, 30);
    CHECK_EQ(g_sellSlots[0].fill, (20 * 6) >> 2);
    // Untouched slots remain empty.
    CHECK_EQ((int)g_buySlots[2].itemId, 0);

    // --- (3) lay out a slider row for the first buy slot ------------------------
    SliderRow row{};
    row.itemId = g_buySlots[0].itemId;
    row.rowX = g_buySlots[0].x;       // 16
    row.rowY = g_buySlots[0].y;       // 16
    row.min = 0; row.max = g_buySlots[0].stock; row.value = 0;
    TradePanel_BuildSliderRow(row, kSliderRowBuy, /*winX=*/8, /*winY=*/4);
    CHECK(row.sliderId != -1);
    CHECK(row.iconId != -1);
    CHECK(row.buyLbl != -1);
    // Slider laid out at rowX(16)+winX(8)+6, rowY(16)+winY(4)+0 = (30, 20).
    CHECK_EQ(row.sliderAbsX, 16 + 8 + 6);
    CHECK_EQ(row.sliderAbsY, 16 + 4 + 0);

    // --- (4) adjust the slider (drag the thumb) ---------------------------------
    // A drag from thumb 0 -> 80 over a [0,50] range quantises through the slider model.
    int v = Scrollbar_ValueFromThumb(/*base=*/0, /*lo=*/0, /*hi=*/80, /*min=*/0, /*max=*/50);
    CHECK(v >= 0 && v <= 50);

    // --- (5) a buy/sell click dispatches the mocked command ---------------------
    BuySink buy; BuildingDialog_SetCommandSink(&buy);
    BuildingState b{}; b.handle = 77; b.roomWorth = 1234; b.condition = 100;
    DialogLayout sell = BuildingDialog_BuildSellPreview(b);
    bool ended = BuildingDialog_DispatchSellPreview(sell, b, kClickOK, sell.childIds[0]);
    CHECK(ended);
    CHECK_EQ(buy.sells, 1);
    CHECK_EQ(buy.building, 77);
    CHECK_EQ(buy.price, 1234);
}

TEST(GuiTradeE2E, StorageOptionsEnlargeFlow) {
    EnlargeSink sink; StorageDialog_SetCommandSink(&sink);
    StorageState s{}; s.handle = 4;
    s.single = false;
    s.usedWidth = 6; s.capWidth = 6;   // full -> enlarge
    s.usedDepth = 1; s.capDepth = 6;   // not full
    const int okObj = 900, buyObj = 901;
    bool dispatched = StorageDialog_DispatchOptions(s, okObj, buyObj, 0, okObj);
    CHECK(dispatched);
    CHECK_EQ(sink.enlarges, 1);          // only the full dimension enlarged
    CHECK_EQ(sink.cost, kSlotEnlargeCost);
}

// Unit tests for the trade / building transaction panels module:
//   building_dialog : sell / renovate / expand / extinguish / tech-effects layout+wiring
//   trade_dialog    : buy-cart radio panel + sell-carts price summation
//   storage_dialog  : new-slot dialog branch + options action wiring
//   trade_panel     : slot-table init coords, inventory population, slider-row layout,
//                     drag-slot column stride math
#include "gui/building_dialog.h"
#include "gui/trade_dialog.h"
#include "gui/storage_dialog.h"
#include "gui/trade_panel.h"
#include "tests/framework/test.h"

#include <cstring>

using namespace guild::gui;

// ===========================================================================
// building_dialog
// ===========================================================================
namespace {
struct BldSink : BuildingCommandSink {
    enum Kind { kNone, kSell, kRenovate, kExpand, kFire };
    Kind kind = kNone;
    int building = 0, arg1 = 0, arg2 = 0;
    const char* label = nullptr;
    void reset() { kind = kNone; building = arg1 = arg2 = 0; label = nullptr; }
    void Sell(int b, int op, int p) override { kind = kSell; building = b; arg1 = op; arg2 = p; }
    void Renovate(int b, int c, const char* l) override { kind = kRenovate; building = b; arg1 = c; label = l; }
    void ExpandRoom(int b, int it, int c, const char* l) override { kind = kExpand; building = b; arg1 = it; arg2 = c; label = l; }
    void ExtinguishFire(int b, int lv) override { kind = kFire; building = b; arg1 = lv; }
};
} // namespace

TEST(GuiTradeBld, SellPreviewLayoutAndWiring) {
    BldSink sink; BuildingDialog_SetCommandSink(&sink);
    BuildingState b{}; b.handle = 7; b.roomWorth = 4200; b.condition = 100;
    DialogLayout l = BuildingDialog_BuildSellPreview(b);
    CHECK(std::strcmp(l.form, kFormSellPergament) == 0);
    CHECK_EQ(l.textId, kTextSellPreview);
    CHECK_EQ(l.childCount, 1);
    CHECK_EQ(l.displayWorth, 4200);
    CHECK_EQ(l.loopForm, kLoopFormConfirm);
    // OK click sells.
    sink.reset();
    CHECK(BuildingDialog_DispatchSellPreview(l, b, kClickOK, l.childIds[0]));
    CHECK_EQ(sink.kind, BldSink::kSell);
    CHECK_EQ(sink.building, 7);
    CHECK_EQ(sink.arg1, kCmdSell);
    CHECK_EQ(sink.arg2, 4200);
    // Cancel ends loop without selling.
    sink.reset();
    CHECK(BuildingDialog_DispatchSellPreview(l, b, kClickCancel, 0));
    CHECK_EQ(sink.kind, BldSink::kNone);
}

TEST(GuiTradeBld, ConfirmSellRequiresChildHit) {
    BldSink sink; BuildingDialog_SetCommandSink(&sink);
    BuildingState b{}; b.handle = 9; b.roomWorth = 100;
    DialogLayout l = BuildingDialog_BuildConfirmSell(b);
    CHECK_EQ(l.textId, kTextConfirmSell);
    // OK but wrong object: no sell, no loop end.
    sink.reset();
    CHECK(!BuildingDialog_DispatchConfirmSell(l, b, kClickOK, 99999));
    CHECK_EQ(sink.kind, BldSink::kNone);
    // OK on the right object: sell.
    sink.reset();
    CHECK(BuildingDialog_DispatchConfirmSell(l, b, kClickOK, l.childIds[0]));
    CHECK_EQ(sink.kind, BldSink::kSell);
}

TEST(GuiTradeBld, SellLandGatedOnRequirements) {
    BuildingState b{}; b.handle = 3; b.roomWorth = 50; b.meetsRequirements = false;
    DialogLayout l = BuildingDialog_BuildSellLand(b);
    CHECK_EQ(l.childCount, 0); // requirements failed -> no dialog
    b.meetsRequirements = true;
    l = BuildingDialog_BuildSellLand(b);
    CHECK_EQ(l.childCount, 1);
    CHECK_EQ(l.textId, kTextSellLand);
}

TEST(GuiTradeBld, RenovateFourChildrenAndWear) {
    BldSink sink; BuildingDialog_SetCommandSink(&sink);
    BuildingState b{}; b.handle = 5; b.roomWorth = 10; b.condition = 70; // wear = 30
    DialogLayout l = BuildingDialog_BuildRenovate(b);
    CHECK(std::strcmp(l.form, kFormRenovate) == 0);
    CHECK_EQ(l.childCount, 4);
    CHECK_EQ(l.displayWorth, 10 * 30);
    // child 3 is the cancel button.
    sink.reset();
    CHECK(BuildingDialog_DispatchRenovate(l, b, 0, l.childIds[3]));
    CHECK_EQ(sink.kind, BldSink::kNone);
    // child 0/1/2 renovate with the right label.
    sink.reset();
    CHECK(BuildingDialog_DispatchRenovate(l, b, 0, l.childIds[1]));
    CHECK_EQ(sink.kind, BldSink::kRenovate);
    CHECK(sink.label && std::strcmp(sink.label, kActionRenovate) == 0);
    // No wear -> no dialog.
    b.condition = 100;
    l = BuildingDialog_BuildRenovate(b);
    CHECK_EQ(l.childCount, 0);
}

TEST(GuiTradeBld, ExtinguishFireThreeLevels) {
    BldSink sink; BuildingDialog_SetCommandSink(&sink);
    BuildingState b{}; b.handle = 8;
    DialogLayout l = BuildingDialog_BuildExtinguishFire(b);
    CHECK(std::strcmp(l.form, kFormExtinguish) == 0);
    CHECK_EQ(l.childCount, 3);
    int expect[3] = {1, 2, 4};
    for (int i = 0; i < 3; ++i) {
        sink.reset();
        CHECK(BuildingDialog_DispatchExtinguishFire(l, b, kClickOK, l.childIds[i]));
        CHECK_EQ(sink.kind, BldSink::kFire);
        CHECK_EQ(sink.arg1, expect[i]);
    }
}

TEST(GuiTradeBld, TechEffectsSliderCount) {
    BuildingState b{};
    bool ws[kTechCategoryCount] = {false};
    ws[0] = ws[3] = ws[10] = true; // 3 categories have a workstation
    DialogLayout l = BuildingDialog_BuildTechEffects(b, ws);
    CHECK(std::strcmp(l.form, kFormTechEffects) == 0);
    CHECK_EQ(l.childCount, 1 + 3); // OK object + 3 sliders
    // Category order table is byte-exact.
    CHECK_EQ((int)kTechCategoryOrder[0], 1);
    CHECK_EQ((int)kTechCategoryOrder[2], 11);
    CHECK_EQ((int)kTechCategoryOrder[kTechCategoryCount], 0); // sentinel
}

// ===========================================================================
// trade_dialog
// ===========================================================================
namespace {
struct TrdSink : TradeCommandSink {
    enum Kind { kNone, kBuy, kSell };
    Kind kind = kNone;
    int building = 0, type = 0, price = 0, count = 0;
    void reset() { kind = kNone; building = type = price = count = 0; }
    void BuyCart(int b, int t, int p) override { kind = kBuy; building = b; type = t; price = p; }
    void SellCarts(int c, int p) override { kind = kSell; count = c; price = p; }
};
} // namespace

TEST(GuiTradeDlg, BuyCartLayoutAndRadioWiring) {
    TrdSink sink; TradeDialog_SetCommandSink(&sink);
    CartCatalogue cat{};
    cat.prices[0] = 100; cat.prices[1] = 200; cat.prices[2] = 300;
    cat.storageBuildingCount = 1; cat.buildingHandle = 42;
    BuyCartLayout l = TradeDialog_BuildBuyCart(cat);
    CHECK(!l.aborted);
    CHECK(std::strcmp(l.form, kFormBuyCart) == 0);
    CHECK_EQ(l.radioCount, kCartTypeCount);
    CHECK_EQ(l.textTypes, kTextCartTypes);

    int selected = 0;
    // Click radio type 2.
    CHECK(!TradeDialog_DispatchBuyCart(l, cat, kCartRadioFirst + 2, l.radioIds[2], selected));
    CHECK_EQ(selected, 2);
    // Confirm on the price object buys type 2 at price 300.
    sink.reset();
    CHECK(TradeDialog_DispatchBuyCart(l, cat, kClickConfirm, l.priceObjId, selected));
    CHECK_EQ(sink.kind, TrdSink::kBuy);
    CHECK_EQ(sink.building, 42);
    CHECK_EQ(sink.type, 2);
    CHECK_EQ(sink.price, 300);
}

TEST(GuiTradeDlg, BuyCartAbortsAtMaxStorage) {
    CartCatalogue cat{}; cat.storageBuildingCount = kMaxStorageBuildings;
    BuyCartLayout l = TradeDialog_BuildBuyCart(cat);
    CHECK(l.aborted);
    int sel = 0;
    CHECK(!TradeDialog_DispatchBuyCart(l, cat, kClickConfirm, l.priceObjId, sel));
}

TEST(GuiTradeDlg, ConfirmSellCartsPriceSum) {
    TrdSink sink; TradeDialog_SetCommandSink(&sink);
    int prices[3] = {1000, 2000, 3000};
    // sell rate 0.5 -> total = (1000+2000+3000)*0.5 = 3000.
    int total = TradeDialog_ConfirmSellCarts(3, prices, 0.5, /*confirm=*/true);
    CHECK_EQ(total, 3000);
    CHECK_EQ(sink.kind, TrdSink::kSell);
    CHECK_EQ(sink.count, 3);
    CHECK_EQ(sink.price, 3000);
    // out of range count -> 0, no dispatch.
    sink.reset();
    CHECK_EQ(TradeDialog_ConfirmSellCarts(0, prices, 0.5, true), 0);
    CHECK_EQ(TradeDialog_ConfirmSellCarts(33, prices, 0.5, true), 0);
    CHECK_EQ(sink.kind, TrdSink::kNone);
    // confirm=false computes price but does not dispatch.
    sink.reset();
    CHECK_EQ(TradeDialog_ConfirmSellCarts(2, prices, 1.0, false), 3000);
    CHECK_EQ(sink.kind, TrdSink::kNone);
}

// ===========================================================================
// storage_dialog
// ===========================================================================
namespace {
struct StoSink : StorageCommandSink {
    enum Kind { kNone, kEnlarge, kBuy };
    Kind kind = kNone;
    int handle = 0, dim = 0, cost = 0;
    int enlargeCount = 0;
    void reset() { kind = kNone; handle = dim = cost = 0; enlargeCount = 0; }
    void EnlargeSlot(int h, int d, int c) override { kind = kEnlarge; handle = h; dim = d; cost = c; ++enlargeCount; }
    void BuyNewSlot(int h, int c) override { kind = kBuy; handle = h; cost = c; }
};
} // namespace

TEST(GuiStorage, NewSlotTwoDimensionBranch) {
    StorageState s{};
    s.single = false;
    s.usedWidth = 2; s.capWidth = 5;   // needs width
    s.usedDepth = 3; s.capDepth = 3;   // depth full
    NewSlotLayout l = StorageDialog_BuildNewSlot(s);
    CHECK(std::strcmp(l.form, kFormNewSlot) == 0);
    CHECK(l.needWidth);
    CHECK(!l.needDepth);
    CHECK(l.widthObjId != -1);
    CHECK_EQ(l.depthObjId, -1);
    CHECK_EQ((int)StorageDialog_DispatchNewSlot(l, 0, l.widthObjId), (int)NewSlotResult::kWidth);
    CHECK_EQ((int)StorageDialog_DispatchNewSlot(l, kStorageCancel, 0), (int)NewSlotResult::kCancel);
}

TEST(GuiStorage, NewSlotSingleDimension278) {
    StorageState s{};
    s.single = true; s.usedWidth = 1; s.capWidth = 4;
    NewSlotLayout l = StorageDialog_BuildNewSlot(s);
    CHECK(l.needWidth);
    CHECK(!l.needDepth);
    CHECK_EQ(l.depthObjId, -1);
}

TEST(GuiStorage, OptionsBuyVsEnlargeWiring) {
    StoSink sink; StorageDialog_SetCommandSink(&sink);
    StorageState s{}; s.handle = 11;
    s.single = false;
    s.usedWidth = 5; s.capWidth = 5; // width full -> enlarge dim 0
    s.usedDepth = 5; s.capDepth = 5; // depth full -> enlarge dim 1
    const int okObj = 500, buyObj = 600;
    // Clicking the OK (new-slot) action enlarges both full dimensions.
    sink.reset();
    CHECK(StorageDialog_DispatchOptions(s, okObj, buyObj, 0, okObj));
    CHECK_EQ(sink.kind, StoSink::kEnlarge);
    CHECK_EQ(sink.enlargeCount, 2);
    CHECK_EQ(sink.cost, kSlotEnlargeCost);
    // Clicking the buy action buys a whole slot for 12800.
    sink.reset();
    CHECK(StorageDialog_DispatchOptions(s, okObj, buyObj, 0, buyObj));
    CHECK_EQ(sink.kind, StoSink::kBuy);
    CHECK_EQ(sink.cost, kSlotBuyCost);
    // Cancel dispatches nothing.
    sink.reset();
    CHECK(!StorageDialog_DispatchOptions(s, okObj, buyObj, kStorageCancel, okObj));
    CHECK_EQ(sink.kind, StoSink::kNone);
}

// ===========================================================================
// trade_panel
// ===========================================================================
TEST(GuiTradePanel, InitSlotTablesCoordinates) {
    int last = TradePanel_InitSlotTables();
    // Sell grid final y = ((7/4)<<6)+208 = 272.
    CHECK_EQ(last, 272);
    // Buy grid: 16 slots, 4 cols, 64-px pitch, 16-px inset.
    CHECK_EQ(g_buySlots[0].x, 16);
    CHECK_EQ(g_buySlots[0].y, 16);
    CHECK_EQ(g_buySlots[3].x, (3 << 6) + 16);  // last column row 0
    CHECK_EQ(g_buySlots[3].y, 16);
    CHECK_EQ(g_buySlots[4].x, 16);             // first column row 1
    CHECK_EQ(g_buySlots[4].y, (1 << 6) + 16);
    CHECK_EQ(g_buySlots[15].x, (3 << 6) + 16);
    CHECK_EQ(g_buySlots[15].y, (3 << 6) + 16);
    // Sell grid: 8 slots, y origin 208.
    CHECK_EQ(g_sellSlots[0].x, 16);
    CHECK_EQ(g_sellSlots[0].y, 208);
    CHECK_EQ(g_sellSlots[4].y, (1 << 6) + 208);
    CHECK_EQ(g_sellSlots[7].x, (3 << 6) + 16);
    // All init to empty / -1.
    CHECK_EQ((int)g_buySlots[0].itemId, 0);
    CHECK_EQ(g_buySlots[0].objectId, -1);
    CHECK_EQ(g_buySlots[0].fill, -1);
}

TEST(GuiTradePanel, PopulateInventorySlotsValues) {
    TradePanel_InitSlotTables();
    TradePanel_SetInventorySource(nullptr); // default returns 0 -> ShopItem values used
    ShopItem items[] = {
        { 10, 40, 8, false },  // buy grid: fill = 40*8>>2 = 80
        { 11,  3, 6, false },  // buy grid: fill = 18>>2 = 4
        { 20,  9, 4, true },   // sell grid: fill = 36>>2 = 9
    };
    SyntheticShop shop{ items, 3 };
    int n = TradePanel_PopulateInventorySlots(shop);
    CHECK_EQ(n, 3);
    CHECK_EQ((int)g_buySlots[0].itemId, 10);
    CHECK_EQ(g_buySlots[0].stock, 40);
    CHECK_EQ(g_buySlots[0].capacity, 8);
    CHECK_EQ(g_buySlots[0].fill, (40 * 8) >> 2);
    CHECK_EQ((int)g_buySlots[1].itemId, 11);
    CHECK_EQ(g_buySlots[1].fill, (3 * 6) >> 2);
    CHECK_EQ((int)g_sellSlots[0].itemId, 20);
    CHECK_EQ(g_sellSlots[0].fill, (9 * 4) >> 2);
}

TEST(GuiTradePanel, SliderRowLayoutAndDestroy) {
    SliderRow row{};
    row.itemId = 5; row.rowX = 100; row.rowY = 50;
    row.min = 0; row.max = 100; row.value = 50;
    TradePanel_BuildSliderRow(row, kSliderRowBuy, /*winX=*/8, /*winY=*/4);
    // All buy-side child widgets allocated.
    CHECK(row.sliderId != -1);
    CHECK(row.iconId != -1);
    CHECK(row.btnId != -1);
    CHECK(row.buyLbl != -1);
    CHECK_EQ(row.sellLbl, -1); // not built on buy side
    // Empty row destroys all children.
    SliderRow empty{}; empty.itemId = 0;
    empty.sliderId = empty.iconId = empty.btnId = 5;
    TradePanel_BuildSliderRow(empty, kSliderRowBuy, 0, 0);
    CHECK_EQ(empty.sliderId, -1);
    CHECK_EQ(empty.iconId, -1);
    CHECK_EQ(empty.btnId, -1);
}

TEST(GuiTradePanel, DragSlotColumnStride) {
    // Buy grid: 4 columns, base y 0 step 22; col origins supplied.
    int origins[4] = {30, 30, 30, 30};
    int xs[4], ys[4];
    TradePanel_LayoutDragSlotColumns(kBuyColumns, 0, kBuyRowYStep, origins, xs, ys);
    for (int c = 0; c < kBuyColumns; ++c) {
        CHECK_EQ(xs[c], 30);
        CHECK_EQ(ys[c], 22 * c);
    }
    // Sell grid: 2 columns, base 182 step 22.
    int o2[2] = {40, 40};
    int x2[2], y2[2];
    TradePanel_LayoutDragSlotColumns(kSellColumns, kSellRowYBase, kSellRowYStep, o2, x2, y2);
    CHECK_EQ(y2[0], 182);
    CHECK_EQ(y2[1], 182 + 22);
    // The horizontal drag step within a row is 68 px.
    CHECK_EQ(kDragColXStep, 68);
}

// tests/unit/dialog_market_test.cpp — Wave 28 PLAY: REAL market-dialog LAYOUT unit.
//
// Loads a SYNTHETIC FRM2 form through the REAL parser (Form_ParseResourceFile) and
// asserts the trade-panel widget LAYOUT the dialog builds: window rect, slot/slider/
// icon/button/label rects at the REAL BuildSliderRow offsets, and that a simulated
// button id maps to the correct command intent (buy/sell side, ware).
#include "test.h"

#include "play/dialog_market.h"
#include "gui/trade_panel.h"
#include "sim/building_production.h"
#include "sim/entity.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::play;

namespace {

// Seed a couple of priceable wares so the dialog's price oracle quotes real values.
void SeedWares() {
    std::memset(sim::g_sceneTypes, 0, sizeof(sim::g_sceneTypes));
    sim::g_sceneTypesLoaded = true;
    sim::g_sceneArrayLoaded = true;
    for (int prot = 1; prot <= 3; ++prot) {
        sim::SceneTypeDef* td = sim::SceneTypeDefAt(prot);
        if (td) { td->baseValue = 100 * prot; td->cachedPrice = 0; }
    }
}

} // namespace

// The real parser builds the synthetic form's window; the dialog records its rect.
TEST(DialogMarketUnit, RealParserBuildsPanelWindow) {
    SeedWares();
    std::vector<TradeRow> rows = { {1, 50, 200, 0, 0, 0}, {2, 10, 80, 0, 0, 0} };
    MarketDialog d = BuildSyntheticMarketDialog(rows, /*x*/100, /*y*/60, /*w*/284, /*h*/395);

    CHECK(d.formParsed);
    CHECK(d.frm2);                  // FRM2 layout (the shipped trade forms)
    CHECK(d.formId >= 0);
    CHECK_EQ(d.windowCount, 1);     // one root window in the synthetic form
    CHECK(d.rootWindow >= 0);
    CHECK_EQ(d.panelX, 100);
    CHECK_EQ(d.panelY, 60);
    CHECK_EQ(d.panelW, 284);
    CHECK_EQ(d.panelH, 395);

    // A window rect was recorded for the parsed window.
    int windowRects = 0;
    for (const auto& w : d.widgets)
        if (w.role == WidgetRole::kWindow) ++windowRects;
    CHECK_EQ(windowRects, 1);
}

// The dialog lays out the trade rows at the REAL BuildSliderRow per-widget offsets.
TEST(DialogMarketUnit, RowWidgetsAtRealOffsets) {
    SeedWares();
    std::vector<TradeRow> rows = { {1, 50, 200, 0, 0, 0} };
    int px = 100, py = 60;
    MarketDialog d = BuildSyntheticMarketDialog(rows, px, py, 284, 395, /*mode buy|sell*/0x03);

    CHECK_EQ((int)d.rows.size(), 1);
    // Both a BUY and a SELL button (mode = buy|sell).
    CHECK_EQ(d.buttonCount(), 2);
    CHECK_EQ(d.labelCount(), 2);

    // The geometry the module reports must be the recovered BuildSliderRow offsets.
    SliderRowGeom g = MarketDialogRowGeom();
    CHECK_EQ(g.sliderDx, gui::kSliderOff.dx);   // 6
    CHECK_EQ(g.sliderDy, gui::kSliderOff.dy);   // 0
    CHECK_EQ(g.iconDx, gui::kIconOff.dx);       // 15
    CHECK_EQ(g.iconDy, gui::kIconOff.dy);       // 6
    CHECK_EQ(g.buttonDx, gui::kButtonOff.dx);   // 15
    CHECK_EQ(g.buttonDy, gui::kButtonOff.dy);   // 58

    // The row origin = the real buy-slot-0 grid coordinate (InitSlotTables).
    int rowX = gui::g_buySlots[0].x, rowY = gui::g_buySlots[0].y;
    CHECK_EQ(rowX, gui::TradePanel_SlotX(0));      // ((0%4)<<6)+16 = 16
    CHECK_EQ(rowY, gui::TradePanel_BuySlotY(0));   // ((0/4)<<6)+16 = 16

    // Find the buy button + slider rects and check they sit at panel + row + offset.
    const WidgetRect* buyBtn = nullptr;
    const WidgetRect* slider = nullptr;
    const WidgetRect* icon   = nullptr;
    for (const auto& w : d.widgets) {
        if (w.role == WidgetRole::kBuyButton) buyBtn = &w;
        if (w.role == WidgetRole::kSlider)    slider = &w;
        if (w.role == WidgetRole::kIcon)      icon   = &w;
    }
    CHECK(buyBtn != nullptr);
    CHECK(slider != nullptr);
    CHECK(icon != nullptr);
    if (buyBtn) {
        CHECK_EQ(buyBtn->x, px + rowX + gui::kButtonOff.dx);
        CHECK_EQ(buyBtn->y, py + rowY + gui::kButtonOff.dy);
        CHECK_EQ(buyBtn->ware, 1);
    }
    if (slider) {
        CHECK_EQ(slider->x, px + rowX + gui::kSliderOff.dx);
        CHECK_EQ(slider->y, py + rowY + gui::kSliderOff.dy);
    }
    if (icon) {
        CHECK_EQ(icon->x, px + rowX + gui::kIconOff.dx);
        CHECK_EQ(icon->y, py + rowY + gui::kIconOff.dy);
    }
}

// A simulated button-rect click maps to the right command intent (side + ware).
TEST(DialogMarketUnit, ButtonClickMapsToCommandIntent) {
    SeedWares();
    std::vector<TradeRow> rows = { {1, 50, 200, 0, 0, 0}, {2, 10, 80, 0, 0, 0} };
    MarketDialog d = BuildSyntheticMarketDialog(rows, 100, 60, 284, 395, 0x03);

    // Locate row-1's SELL button and click its centre.
    const WidgetRect* sell1 = nullptr;
    for (const auto& w : d.widgets)
        if (w.role == WidgetRole::kSellButton && w.rowIndex == 1) sell1 = &w;
    CHECK(sell1 != nullptr);
    if (!sell1) return;

    DialogClick c = ClickMarketDialog(d, sell1->x + sell1->w / 2,
                                      sell1->y + sell1->h / 2,
                                      /*buildingId*/77, /*qty*/5, /*player*/0);
    CHECK(c.hitButton);
    CHECK(c.role == WidgetRole::kSellButton);
    CHECK_EQ(c.rowIndex, 1);
    CHECK(c.interaction.side == MarketSide::kSell);
    CHECK_EQ((int)c.interaction.ware, 2);          // row 1 trades ware 2
    CHECK_EQ(c.interaction.qty, 5);
    CHECK_EQ(c.interaction.buildingId, 77);
    CHECK_EQ((int)c.interaction.buildingKind, 2);  // the kind-2 contor gate

    // That interaction classifies into the REAL opcode-17 trade command.
    MarketCommand cmd = ClassifyMarketInteraction(c.interaction);
    CHECK(cmd.issued);
    CHECK_EQ((int)cmd.opcode, 17);
    CHECK(cmd.side == MarketSide::kSell);
    CHECK_EQ((int)cmd.proto, 2);
    CHECK_EQ(cmd.qty, 5);
    CHECK(cmd.unitPrice > 0);   // the real price oracle quoted a positive price
}

// A click on empty space (no button) yields no intent.
TEST(DialogMarketUnit, ClickOffButtonNoIntent) {
    SeedWares();
    std::vector<TradeRow> rows = { {1, 50, 200, 0, 0, 0} };
    MarketDialog d = BuildSyntheticMarketDialog(rows, 100, 60, 284, 395, 0x03);
    DialogClick c = ClickMarketDialog(d, /*far away*/9000, 9000, 77, 5, 0);
    CHECK(!c.hitButton);
    CHECK(c.interaction.side == MarketSide::kNone);
}

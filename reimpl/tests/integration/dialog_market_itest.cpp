// tests/integration/dialog_market_itest.cpp — Wave 28 PLAY: REAL market-dialog
// RENDER + scripted BUY click -> real command.
//
// Renders the trade dialog (parsed from a synthetic FRM2 form through the REAL
// parser, laid out with the REAL trade-panel geometry) to a small software surface
// and asserts the widgets DRAW (non-clear pixels land on the widget rects); then a
// scripted click on the BUY button routes through slice_market's REAL opcode-17 buy
// command (correct fields) over a live real-format world.
#include "test.h"

#include "play/dialog_market.h"
#include "play/world_digest.h"
#include "render/surface.h"
#include "sim/building_production.h"
#include "sim/entity.h"
#include "sim/command.h"
#include "world/city.h"
#include "world/economy_tick.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::play;

namespace {

// Seed a small real-format world with priced wares + live buildings (mirrors the
// market itest seed). Returns the trader building id.
i32 SeedWorld(i16 ware) {
    std::memset(world::g_goods, 0, sizeof(world::g_goods));
    std::memset(world::g_cities, 0, sizeof(world::g_cities));
    world::g_capDivisor = 0.0f;
    world::g_cityTotalMoney = 0.0f;
    world::g_cityTotalGoods = 0.0f;
    world::SetSmoothedPriceLevel(0.0f);

    sim::ResetEntityArrays();
    std::memset(sim::g_objects, 0, sizeof(sim::g_objects));
    sim::g_sceneArrayLoaded = true;

    std::memset(sim::g_sceneTypes, 0, sizeof(sim::g_sceneTypes));
    std::memset(sim::g_sceneTypeRemap, 0, sizeof(sim::g_sceneTypeRemap));
    sim::g_sceneTypesLoaded = true;
    if (auto* td = sim::SceneTypeDefAt(ware)) {
        td->kind = 23; td->baseValue = 120; td->divisor = 4; td->cachedPrice = 0;
    }
    if (auto* td = sim::SceneTypeDefAt(ware + 1)) {
        td->kind = 23; td->baseValue = 64; td->divisor = 2; td->cachedPrice = 0;
    }

    const i32 kTraderId = 9100;
    for (int i = 0; i < 4; ++i) {
        sim::ObjectRec& o = sim::g_objects[i];
        o.alive = 1;
        o.id    = 9100 + i;
        auto* b = reinterpret_cast<sim::BuildingRec*>(&o);
        b->fillLevel = (u16)(300 + 50 * i);
        i32 zero = 0;
        std::memcpy(reinterpret_cast<u8*>(&o) + kTreasuryFieldOff, &zero, sizeof zero);
    }
    return kTraderId;
}

} // namespace

// The dialog renders: widgets land as non-clear pixels at their rects.
TEST(DialogMarketItest, RendersWidgetsToSurface) {
    SeedWorld(/*ware*/3);
    std::vector<TradeRow> rows = { {3, 200, 400, 0, 0, 0}, {4, 120, 300, 0, 0, 0} };
    // Panel near the origin so its rows fit a small surface.
    MarketDialog d = BuildSyntheticMarketDialog(rows, /*x*/2, /*y*/2, /*w*/120, /*h*/180,
                                                /*mode buy|sell*/0x03);
    CHECK(d.formParsed);
    CHECK(d.buttonCount() >= 2);

    DialogRenderStats st;
    render::Surface* surf = RenderMarketDialog(d, /*fbW*/220, /*fbH*/220, st);
    CHECK(surf != nullptr);
    if (!surf) return;

    CHECK(st.widgetsDrawn > 0);
    CHECK(st.buttonsDrawn >= 2);
    CHECK(st.nonClearPixels > 0);   // the dialog painted something

    // The BUY buttons specifically painted non-clear pixels at their rects (green).
    int buyHits = 0;
    for (const auto& w : d.widgets) {
        if (w.role != WidgetRole::kBuyButton) continue;
        u8 px[3];
        render::SurfaceGetPixelRgb(surf, w.x + w.w / 2, w.y + w.h / 2, px);
        // Green dominant (565-quantised); clear parchment is (200,180,120).
        if (px[1] > px[0] && px[1] > px[2]) ++buyHits;
    }
    CHECK(buyHits >= 1);

    render::SurfaceDestroy(surf);
}

// A scripted BUY click routes into the REAL opcode-17 buy command with the right
// fields, and the live world's treasury/stock actually move.
TEST(DialogMarketItest, BuyClickEmitsRealBuyCommand) {
    i16 ware = 3;
    i32 id = SeedWorld(ware);

    std::vector<TradeRow> rows = { {ware, 200, 400, 0, 0, 0} };
    std::vector<u8> form = MakeSyntheticTradeForm(2, 2, 120, 180);

    sim::CommandQueue q; q.Init(); q.set_standalone(true);
    DialogSliceResult r =
        RunMarketDialogSlice(form.data(), form.size(), rows,
                             /*clickRow*/0, MarketSide::kBuy,
                             /*buildingId*/id, /*qty*/30, /*player*/0,
                             /*econSeed*/0xBEEF, q, /*fbW*/220, /*fbH*/220,
                             "synthetic.form");

    // The dialog rendered + the click hit a BUY button.
    CHECK(r.dialog.formParsed);
    CHECK(r.render.nonClearPixels > 0);
    CHECK(r.click.hitButton);
    CHECK(r.click.role == WidgetRole::kBuyButton);
    CHECK_EQ(r.click.rowIndex, 0);
    CHECK(r.click.interaction.side == MarketSide::kBuy);
    CHECK_EQ((int)r.click.interaction.ware, (int)ware);

    // The REAL opcode-17 command was issued with the correct fields.
    CHECK(r.commandIssued);
    CHECK_EQ((int)r.slice.command.opcode, 17);   // VIBE_Command_QueueRequest17
    CHECK(r.slice.command.side == MarketSide::kBuy);
    CHECK_EQ((int)r.slice.command.proto, (int)ware);
    CHECK_EQ(r.slice.command.qty, 30);
    CHECK_EQ(r.slice.command.seller, id);
    CHECK(r.slice.command.unitPrice > 0);
    CHECK_EQ(r.slice.command.totalValue,
             (long long)r.slice.command.unitPrice * 30);

    // The command applied through the real codec + moved the live records.
    CHECK(r.slice.enqueued);
    CHECK(r.slice.applied);
    CHECK_EQ(r.slice.stockAfter - r.slice.stockBefore, 30);   // buy: stock up by qty
    CHECK_EQ(r.slice.treasuryBefore - r.slice.treasuryAfter,
             r.slice.command.totalValue);                     // money out
    CHECK(r.slice.tradeChangedWorld());
    CHECK(r.slice.dayChangedWorld());
}

// A SELL click routes into the SELL command (opcode 17, sell side) — money in.
TEST(DialogMarketItest, SellClickEmitsRealSellCommand) {
    i16 ware = 3;
    i32 id = SeedWorld(ware);

    std::vector<TradeRow> rows = { {ware, 500, 800, 0, 0, 0} };
    std::vector<u8> form = MakeSyntheticTradeForm(2, 2, 120, 180);

    sim::CommandQueue q; q.Init(); q.set_standalone(true);
    DialogSliceResult r =
        RunMarketDialogSlice(form.data(), form.size(), rows,
                             /*clickRow*/0, MarketSide::kSell,
                             /*buildingId*/id, /*qty*/40, /*player*/0,
                             /*econSeed*/0xBEEF, q, /*fbW*/220, /*fbH*/220,
                             "synthetic.form");

    CHECK(r.click.hitButton);
    CHECK(r.click.role == WidgetRole::kSellButton);
    CHECK(r.commandIssued);
    CHECK_EQ((int)r.slice.command.opcode, 17);
    CHECK(r.slice.command.side == MarketSide::kSell);
    CHECK_EQ(r.slice.command.qty, 40);
    // Sell: stock down by qty, money IN.
    CHECK_EQ(r.slice.stockBefore - r.slice.stockAfter, 40);
    CHECK_EQ(r.slice.treasuryAfter - r.slice.treasuryBefore,
             r.slice.command.totalValue);
}

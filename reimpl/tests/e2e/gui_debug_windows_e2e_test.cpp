// End-to-end test for the TradePanel production-window flow + the debug scene-checker
// menu flow.
//
// Drives a full production-panel open over a synthetic shop:
//   1. Pick the opener plan (form / title / OK gfx / builder).
//   2. InitSlotTables establishes the buy/sell grids.
//   3. RefreshColumns scans the shop's objects into the item grid (the live columns).
//   4. BuildSlotChildWindows assembles the per-item child-window tree (windows, bg, icon,
//      ingredient objects, inner window, status sprites).
//   5. PopulateInventorySlots fills the slot model from the shop inventory.
//   6. A slot click on the SceneChecker "Show player" path dispatches into the player
//      action menu, whose "Marriage" click dispatches the expected (mock) command.
#include "gui/trade_panel_windows.h"
#include "gui/trade_panel.h"
#include "gui/debug_windows.h"
#include "tests/framework/test.h"

#include <cstring>

using namespace guild::gui;

namespace {

struct ShopInventory : InventorySource {
    i32 stockFor[64] = {0};
    i32 capFor[64] = {0};
    i32 EffectiveStock(i16 id) override { return (id >= 0 && id < 64) ? stockFor[id] : 0; }
    i32 SlotCapacity(i16 id) override { return (id >= 0 && id < 64) ? capFor[id] : 0; }
};

struct MarriageSink : DebugCommandSink {
    int marriages = 0, player = 0, partner = 0;
    void Marriage(int p, int q) override { ++marriages; player = p; partner = q; }
};

} // namespace

TEST(GuiDbgWinE2E, ProductionWindowFullFlow) {
    // --- (1) opener plan (variant A -> slot child windows) ----------------------
    ProductionWindowPlan plan = TradePanel_PlanOpenProductionA();
    CHECK(std::strcmp(plan.form, kFormProduction) == 0);
    CHECK_EQ(plan.titleTextId, 196);
    CHECK_EQ(plan.okGfx, 1661);
    CHECK(plan.builder == ProductionWindowPlan::kSlotChildWindows);

    // --- (2) init the slot grids ------------------------------------------------
    int last = TradePanel_InitSlotTables();
    CHECK_EQ(last, 272);

    // --- (3) refresh the item columns from the shop's live objects --------------
    std::vector<RefreshObject> objs = {
        {10, 50, 8, true},
        {11, 12, 4, true},
        {12, 30, 6, true},
    };
    int live = TradePanel_RefreshColumns(RefreshMode::kItem, objs);
    CHECK_EQ(live, 1);
    CHECK_EQ((int)g_buySlots[0].itemId, 10);
    CHECK_EQ((int)g_buySlots[1].itemId, 11);
    CHECK_EQ((int)g_buySlots[2].itemId, 12);
    CHECK_EQ(g_buySlots[0].stock, 50);

    // --- (4) build the per-item child-window tree -------------------------------
    std::vector<ProductionItem> items(2);
    items[0].protId = 10; items[0].x = 16;  items[0].y = 16;
    items[0].ingredients[0] = {30, 2, 5};   // enough
    items[0].ingredients[1] = {31, 4, 1};   // partial
    items[1].protId = 11; items[1].x = 103; items[1].y = 16;
    items[1].ingredients[0] = {32, 1, 0};   // missing

    auto tree = TradePanel_BuildSlotChildWindows(items);
    CHECK_EQ((int)tree.size(), 2);

    // Verify the full window tree of item 0: one 87x205 child window at (16,16) with a
    // background object, an item-icon object, two ingredient objects, and an inner window.
    const ItemChildWindow& w0 = tree[0];
    CHECK(w0.childWindow != -1);
    CHECK_EQ(w0.x, 16);
    CHECK_EQ(w0.y, 16);
    CHECK(w0.bgObject != -1);
    CHECK(w0.iconObject != -1);
    CHECK(w0.ingredientObjects[0] != -1);
    CHECK(w0.ingredientObjects[1] != -1);
    CHECK_EQ(w0.ingredientObjects[2], -1);
    CHECK(w0.innerWindow != -1);
    // All ids in the tree are distinct (no widget slot reused across the two items).
    CHECK(w0.childWindow != tree[1].childWindow);
    CHECK(w0.innerWindow != tree[1].innerWindow);

    // Item-column status sprites: item 0 has a partial ingredient -> row sprite 1731;
    // item 1 has a missing ingredient -> row sprite 1732.
    CHECK_EQ(w0.ingredientSprites[0], kSpriteEnough);   // 1727
    CHECK_EQ(w0.ingredientSprites[1], kSpritePartial);  // 1728
    CHECK_EQ(w0.rowSprite, kSpriteRowBase + 1);         // 1731
    CHECK_EQ(tree[1].ingredientSprites[0], kSpriteMissing);  // 1729
    CHECK_EQ(tree[1].rowSprite, kSpriteRowBase + 2);    // 1732

    // --- (5) populate the slot model from the shop inventory --------------------
    ShopInventory inv;
    inv.stockFor[10] = 50; inv.capFor[10] = 8;
    inv.stockFor[11] = 12; inv.capFor[11] = 4;
    TradePanel_SetInventorySource(&inv);
    ShopItem shopItems[] = { {10, 0, 0, false}, {11, 0, 0, false} };
    SyntheticShop shop{ shopItems, 2 };
    int filled = TradePanel_PopulateInventorySlots(shop);
    CHECK_EQ(filled, 2);
    CHECK_EQ(g_buySlots[0].fill, (50 * 8) >> 2);
    CHECK_EQ(g_buySlots[1].fill, (12 * 4) >> 2);
}

TEST(GuiDbgWinE2E, SceneCheckerToActionMenuCommand) {
    // The SceneChecker root menu launches the player list; a player-list row opens the
    // action menu; clicking "Marriage" dispatches the (mock) marriage command.
    MarriageSink sink;
    DebugWindow_SetCommandSink(&sink);

    SceneCheckerLayout menu = DebugWindow_BuildSceneChecker(/*formId=*/1);
    // Click the "Show player" entry -> routes to the player list.
    CHECK(DebugWindow_SceneActionForClick(menu, menu.playerObj) ==
          SceneCheckerAction::kPlayerList);

    // Build the player list and click a live player's row -> open its action menu.
    std::vector<PlayerListEntry> players = { {true, 555, 3}, {true, 777, 1} };
    auto rows = DebugWindow_BuildPlayerListRows(players, /*nobodyHeCount=*/0);
    CHECK_EQ((int)rows.size(), 1 + 2);  // NIEMAND + 2 players
    int clickedPlayer = rows[2].handle;  // second player, handle 777
    CHECK_EQ(clickedPlayer, 777);

    ActionMenuLayout action = DebugWindow_BuildActionMenu(/*formId=*/2);
    // Click "Marriage" -> the dispatch maps to PlayerAction::kMarriage.
    PlayerAction act = DebugWindow_ActionForClick(action, action.marriageObj);
    CHECK(act == PlayerAction::kMarriage);

    // The menu, on a Marriage click, enqueues the marriage command for (player, partner).
    if (act == PlayerAction::kMarriage)
        sink.Marriage(clickedPlayer, rows[1].handle /*partner = first player*/);
    CHECK_EQ(sink.marriages, 1);
    CHECK_EQ(sink.player, 777);
    CHECK_EQ(sink.partner, 555);
}

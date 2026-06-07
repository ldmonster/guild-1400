// Unit tests for the debug/utility window builders + the TradePanel production-window
// tree assembly + the loading-screen build sequence.
#include "gui/debug_windows.h"
#include "gui/trade_panel_windows.h"
#include "gui/loading.h"
#include "tests/framework/test.h"

#include <cstring>

using namespace guild::gui;

// ===========================================================================
// Debug windows — content builders.
// ===========================================================================
TEST(GuiDbgWin, ShowDummiesLines) {
    std::vector<DummyNode> nodes = {
        {"node_ok",      true,  1},   // walkable tile -> empty status
        {"node_blocked", true,  10},  // tile 10 -> "smap-pos is blocked"
        {"node_nosmap",  false, 0},   // no tile -> "No smap or pos out of range"
        {"node_zero",    true,  0},   // tile 0 -> blocked
    };
    auto lines = DebugWindow_BuildDummies(nodes);
    // header + separator + one line per node.
    CHECK_EQ((int)lines.size(), 2 + 4);
    CHECK(lines[0].format == kDummyHeader);
    CHECK(lines[1].format == kDummySep);
    // The line encodes "fmt|name|status".
    CHECK(lines[2].format == std::string(kDummyLineFmt) + "|node_ok|");
    CHECK(lines[3].format == std::string(kDummyLineFmt) + "|node_blocked|" + kDummyBlocked);
    CHECK(lines[4].format == std::string(kDummyLineFmt) + "|node_nosmap|" + kDummyNoSmap);
    CHECK(lines[5].format == std::string(kDummyLineFmt) + "|node_zero|" + kDummyBlocked);
}

TEST(GuiDbgWin, ScriptInfoTotalMemory) {
    std::vector<ScriptEntry> s = {
        {true,  1, "a", 3, 2, 100},
        {false, 2, "b", 0, 0, 999},   // inactive -> skipped, not in total
        {true,  3, "c", 1, 4, 250},
    };
    auto lines = DebugWindow_BuildScriptInfo(s, /*runningCount=*/2);
    // "$C", count, columns, sep, 2 active rows, total = 6 lines + 2 rows
    CHECK_EQ((int)lines.size(), 4 + 2 + 1);
    CHECK(lines[0].format == "$C");
    CHECK(lines[1].format == kScriptHeaderCount);
    CHECK_EQ(lines[1].args[0], 2);
    // First active row: id 1.
    CHECK(lines[4].format == kScriptRowFmt);
    CHECK_EQ(lines[4].args[0], 1);
    // Total-Memory = 100 + 250 (inactive 999 excluded).
    const DebugLine& total = lines.back();
    CHECK(total.format == kScriptTotalFmt);
    CHECK_EQ(total.args[0], 350);
}

TEST(GuiDbgWin, MarketInfoRegionsAndRows) {
    MarketRegion r0; r0.name = "Region0";
    r0.rows = { {true, 7, 1,2,3,4, 50, 11, 22}, {false, 9, 0,0,0,0, 0, 0, 0} };
    MarketRegion r1; r1.name = "Region1";
    r1.rows = { {true, 8, 5,6,7,8, 60, 33, 44} };
    MarketRegion empty; // empty name -> stops the outer loop
    std::vector<MarketRegion> regions = {r0, r1, empty, r1};
    auto lines = DebugWindow_BuildMarketInfo(regions);
    // "$C" + (header+sep + 1 present row) + (header+sep + 1 row) -> stops at empty.
    CHECK(lines[0].format == "$C");
    CHECK(lines[1].format == kMarketRegionHeader);
    CHECK(lines[2].format == kMarketSep);
    CHECK(lines[3].format == kMarketRowFmt);
    CHECK_EQ(lines[3].args[0], 7);          // protId
    CHECK_EQ(lines[3].args[1], 50);         // marketPrice
    // Region1 follows; the empty region halts before region index 3.
    bool sawRegion1 = false;
    for (auto& l : lines) if (l.format == kMarketRowFmt && !l.args.empty() && l.args[0] == 8) sawRegion1 = true;
    CHECK(sawRegion1);
}

TEST(GuiDbgWin, PotionsLazyHeader) {
    PotionPlayer p0; p0.present = true; p0.name = "p0";
    p0.rows = { {5, 3}, {9, 1} };
    PotionPlayer p1; p1.present = false; // absent player -> skipped entirely
    PotionPlayer p2; p2.present = true; p2.name = "p2"; // present but no rows -> no header
    std::vector<PotionPlayer> players = {p0, p1, p2};
    auto lines = DebugWindow_BuildPotions(players);
    // "$C" + (header+sep + 2 rows). p2 has no rows -> no header emitted.
    CHECK_EQ((int)lines.size(), 1 + 2 + 2);
    CHECK(lines[0].format == "$C");
    CHECK(lines[1].format == kPotionHeader);
    CHECK(lines[2].format == kPotionSep);
    CHECK(lines[3].format == kPotionRowFmt);
    CHECK_EQ(lines[3].args[0], 2 * 5 + 2151);  // label = 2*itemId+2151
    CHECK_EQ(lines[3].args[1], 3);             // count
}

TEST(GuiDbgWin, PlayerListRowsAndPaging) {
    std::vector<PlayerListEntry> players;
    // 64 present players -> after the 63-row page (incl NIEMAND) the rest go right.
    for (int i = 0; i < 64; ++i)
        players.push_back({true, 100 + i, i});
    auto rows = DebugWindow_BuildPlayerListRows(players, /*nobodyHeCount=*/7);
    // NIEMAND row first.
    CHECK(rows[0].format == kPlayerNobodyFmt);
    CHECK_EQ(rows[0].handle, 0);
    CHECK_EQ(rows[0].heCount, 7);
    CHECK(!rows[0].rightColumn);
    CHECK_EQ((int)rows.size(), 1 + 64);
    // Row index 62 (rowCount hits 63) is the last left-column row; row 63 moves right.
    CHECK(!rows[62].rightColumn);
    CHECK(rows[63].rightColumn);
    CHECK(rows[1].format == kPlayerRowFmt);
    CHECK_EQ(rows[1].buttonId, kPlayerButtonId);
}

TEST(GuiDbgWin, ActionModeNames) {
    CHECK(std::strcmp(DebugWindow_ActionModeName(0), "DUMMY_NPC") == 0);
    CHECK(std::strcmp(DebugWindow_ActionModeName(5), "PLAYER") == 0);
    CHECK(std::strcmp(DebugWindow_ActionModeName(14), "DEAD_PLAYER") == 0);
    CHECK(std::strcmp(DebugWindow_ActionModeName(15), "") == 0);  // out of range
    CHECK(std::strcmp(DebugWindow_ActionModeName(-1), "") == 0);
}

TEST(GuiDbgWin, ActionMenuWiring) {
    ActionMenuLayout l = DebugWindow_BuildActionMenu(/*formId=*/42);
    CHECK_EQ(l.formId, 42);
    // Four distinct button object ids in build order.
    CHECK(l.killObj != l.showHeObj && l.showHeObj != l.marriageObj &&
          l.marriageObj != l.getChildObj);
    CHECK(DebugWindow_ActionForClick(l, l.killObj)     == PlayerAction::kKill);
    CHECK(DebugWindow_ActionForClick(l, l.showHeObj)   == PlayerAction::kShowHe);
    CHECK(DebugWindow_ActionForClick(l, l.marriageObj) == PlayerAction::kMarriage);
    CHECK(DebugWindow_ActionForClick(l, l.getChildObj) == PlayerAction::kGetChild);
    CHECK(DebugWindow_ActionForClick(l, 999999)        == PlayerAction::kNone);
}

TEST(GuiDbgWin, SceneCheckerWiring) {
    SceneCheckerLayout l = DebugWindow_BuildSceneChecker(/*formId=*/9);
    CHECK_EQ(l.formId, 9);
    CHECK(DebugWindow_SceneActionForClick(l, l.dummiesObj) == SceneCheckerAction::kDummies);
    CHECK(DebugWindow_SceneActionForClick(l, l.scriptObj)  == SceneCheckerAction::kScriptInfo);
    CHECK(DebugWindow_SceneActionForClick(l, l.memoryObj)  == SceneCheckerAction::kMemoryInfo);
    CHECK(DebugWindow_SceneActionForClick(l, l.marketObj)  == SceneCheckerAction::kMarketInfo);
    CHECK(DebugWindow_SceneActionForClick(l, l.theatreObj) == SceneCheckerAction::kTheatre);
    CHECK(DebugWindow_SceneActionForClick(l, l.potionsObj) == SceneCheckerAction::kPotions);
    CHECK(DebugWindow_SceneActionForClick(l, l.playerObj)  == SceneCheckerAction::kPlayerList);
    CHECK(DebugWindow_SceneActionForClick(l, 0)            == SceneCheckerAction::kNone);
}

TEST(GuiDbgWin, SupermapTileClassification) {
    // Floor tiles 0/10/13: gray + 32, clamped; 13 sets altFlag -1.
    CHECK_EQ(DebugWindow_ClassifySupermapTile(0, 100).color, 132);
    CHECK_EQ(DebugWindow_ClassifySupermapTile(0, 100).altFlag, 0);
    CHECK_EQ(DebugWindow_ClassifySupermapTile(13, 50).altFlag, -1);
    CHECK_EQ(DebugWindow_ClassifySupermapTile(10, 240).color, 255);  // clamp
    // Tile 11 -> color 0x60; tile 12 -> color 0.
    CHECK_EQ(DebugWindow_ClassifySupermapTile(11, 5).color, 0x60);
    CHECK_EQ(DebugWindow_ClassifySupermapTile(12, 5).color, 0);
}

// ===========================================================================
// TradePanel production-window tree assembly.
// ===========================================================================
TEST(GuiDbgWin, ProductionOkGfxByCategory) {
    CHECK_EQ(TradePanel_OkGfxForCategory(19), 1697);
    CHECK_EQ(TradePanel_OkGfxForCategory(4),  1697);
    CHECK_EQ(TradePanel_OkGfxForCategory(16), 1697);
    CHECK_EQ(TradePanel_OkGfxForCategory(8),  1665);
    CHECK_EQ(TradePanel_OkGfxForCategory(14), 1665);
    CHECK_EQ(TradePanel_OkGfxForCategory(6),  1669);
    CHECK_EQ(TradePanel_OkGfxForCategory(11), 1673);
    CHECK_EQ(TradePanel_OkGfxForCategory(13), 1673);
    CHECK_EQ(TradePanel_OkGfxForCategory(99), 1661);  // default
}

TEST(GuiDbgWin, ProductionPlans) {
    auto g = TradePanel_PlanBuildProduction(/*category=*/6);
    CHECK(std::strcmp(g.form, kFormProduction) == 0);
    CHECK_EQ(g.titleTextId, 180);
    CHECK_EQ(g.okGfx, 1669);
    CHECK_EQ(g.cancelGfx, 1716);
    CHECK(g.builder == ProductionWindowPlan::kItemRowWindows);

    auto a = TradePanel_PlanOpenProductionA();
    CHECK_EQ(a.titleTextId, 196);
    CHECK_EQ(a.okGfx, 1661);
    CHECK(a.builder == ProductionWindowPlan::kSlotChildWindows);

    auto b = TradePanel_PlanOpenProductionB();
    CHECK_EQ(b.titleTextId, 199);
    CHECK_EQ(b.itemRowMode, 1);

    auto c = TradePanel_PlanOpenProductionC();
    CHECK_EQ(c.titleTextId, 200);
    CHECK_EQ(c.itemRowMode, 2);
}

TEST(GuiDbgWin, ItemSpriteResolution) {
    ProductionItem item{};
    item.protId = 5;
    item.ingredients[0] = {10, 3, 5};   // need 3, have 5 -> enough (1727)
    item.ingredients[1] = {11, 4, 2};   // need 4, have 2 -> partial (1728), state>=1
    item.ingredients[2] = {0, 0, 0};    // empty -> skipped
    item.ingredients[3] = {12, 1, 0};   // missing -> 1729, state=2
    ItemChildWindow w{};
    TradePanel_ResolveItemSprites(item, w);
    CHECK_EQ(w.ingredientSprites[0], kSpriteEnough);
    CHECK_EQ(w.ingredientSprites[1], kSpritePartial);
    CHECK_EQ(w.ingredientSprites[2], -1);
    CHECK_EQ(w.ingredientSprites[3], kSpriteMissing);
    CHECK_EQ(w.rowState, 2);
    CHECK_EQ(w.rowSprite, kSpriteRowBase + 2);  // 1732
}

TEST(GuiDbgWin, SlotChildWindowTree) {
    std::vector<ProductionItem> items(2);
    items[0].protId = 5; items[0].x = 16; items[0].y = 16;
    items[0].ingredients[0] = {10, 1, 1};
    items[0].ingredients[1] = {11, 1, 1};
    items[1].protId = 6; items[1].x = 103; items[1].y = 16;
    // no ingredients on item 1.
    auto tree = TradePanel_BuildSlotChildWindows(items);
    CHECK_EQ((int)tree.size(), 2);
    // Each item: a child window, bg, icon, inner window all allocated and distinct.
    CHECK(tree[0].childWindow != -1 && tree[0].bgObject != -1 &&
          tree[0].iconObject != -1 && tree[0].innerWindow != -1);
    CHECK_EQ(tree[0].x, 16);
    CHECK_EQ(tree[0].y, 16);
    // Item 0 has two ingredient objects; the empty slots have none.
    CHECK(tree[0].ingredientObjects[0] != -1);
    CHECK(tree[0].ingredientObjects[1] != -1);
    CHECK_EQ(tree[0].ingredientObjects[2], -1);
    // Item 1 has no ingredient objects.
    CHECK_EQ(tree[1].ingredientObjects[0], -1);
    // Row sprites resolved (enough -> 1730).
    CHECK_EQ(tree[0].rowSprite, kSpriteRowBase + 0);
}

TEST(GuiDbgWin, ColumnSelectTables) {
    // Recovered byte-for-byte: row 0 of item table = {2,0,0,0}; row 16 = {4,4,4,4}.
    i32 out[kColTableCols];
    TradePanel_SelectColumns(kColTableItem, 0, out);
    CHECK_EQ(out[0], 2); CHECK_EQ(out[1], 0); CHECK_EQ(out[2], 0); CHECK_EQ(out[3], 0);
    TradePanel_SelectColumns(kColTableItem, 16, out);
    CHECK_EQ(out[0], 4); CHECK_EQ(out[1], 4); CHECK_EQ(out[2], 4); CHECK_EQ(out[3], 4);
    TradePanel_SelectColumns(kColTableItem, 9, out);  // {4,3,2,0}
    CHECK_EQ(out[0], 4); CHECK_EQ(out[1], 3); CHECK_EQ(out[2], 2); CHECK_EQ(out[3], 0);
    // Sell table identical to item table.
    i32 sell[kColTableCols];
    TradePanel_SelectColumns(kColTableSell, 9, sell);
    CHECK_EQ(sell[0], 4); CHECK_EQ(sell[1], 3); CHECK_EQ(sell[2], 2); CHECK_EQ(sell[3], 0);
    // Rebuild table is rotated: row 0 = {4,4,3,3}.
    TradePanel_SelectColumns(kColTableRebuild, 0, out);
    CHECK_EQ(out[0], 4); CHECK_EQ(out[1], 4); CHECK_EQ(out[2], 3); CHECK_EQ(out[3], 3);
}

TEST(GuiDbgWin, RefreshColumnsFillsSlots) {
    TradePanel_InitSlotTables();  // clears g_buySlots
    std::vector<RefreshObject> objs = {
        {10, 50, 8, true},
        {11, 12, 4, true},
    };
    int live = TradePanel_RefreshColumns(RefreshMode::kItem, objs);
    CHECK_EQ((int)g_buySlots[0].itemId, 10);
    CHECK_EQ(g_buySlots[0].stock, 50);
    CHECK_EQ(g_buySlots[0].capacity, 8);
    CHECK_EQ((int)g_buySlots[1].itemId, 11);
    CHECK_EQ(live, 1);

    // Refreshing again updates the existing slot's stock in place (no new slot).
    std::vector<RefreshObject> upd = { {10, 99, 8, true} };
    TradePanel_RefreshColumns(RefreshMode::kItem, upd);
    CHECK_EQ(g_buySlots[0].stock, 99);
    CHECK_EQ((int)g_buySlots[2].itemId, 0);  // no extra slot consumed
}

TEST(GuiDbgWin, RefreshSellGuardDropsIneligible) {
    TradePanel_InitSlotTables();
    std::vector<RefreshObject> objs = {
        {20, 5, 6, false},   // not sell-eligible -> skipped
        {21, 7, 6, true},
    };
    TradePanel_RefreshColumns(RefreshMode::kSell, objs);
    CHECK_EQ((int)g_buySlots[0].itemId, 21);   // only the eligible object landed
}

// ===========================================================================
// Loading-screen build sequence.
// ===========================================================================
TEST(GuiDbgWin, LoadingProgressPlan) {
    auto net = Loading_BuildProgressScreen(kLoadingNetFlag);
    CHECK(std::strcmp(net.form, kFormLoadingNet) == 0);
    auto single = Loading_BuildProgressScreen(0);
    CHECK(std::strcmp(single.form, kFormLoadingGame) == 0);
    CHECK_EQ(single.sliderSpan, 582);
    CHECK_EQ(single.sliderGfxA, 114);
    CHECK_EQ(single.sliderGfxB, 66);
    CHECK_EQ(single.procId, 7);
    CHECK_EQ(single.barWindowSlot, 1);
}

TEST(GuiDbgWin, LoadingBarUpdate) {
    auto u0 = Loading_BuildBarUpdate(0);
    CHECK_EQ(u0.min, 0); CHECK_EQ(u0.max, 582); CHECK_EQ(u0.value, 0);
    auto u50 = Loading_BuildBarUpdate(50);
    CHECK_EQ(u50.value, 582 * 50 / 100);  // 291
    auto u100 = Loading_BuildBarUpdate(100);
    CHECK_EQ(u100.value, 582);
}

TEST(GuiDbgWin, LoadingFadePlan) {
    auto f = Loading_BuildFadeOut();
    CHECK(std::strcmp(f.color, "BLACK") == 0);
    CHECK_EQ(f.frames, 30);
    CHECK(f.unregisterProc);
}

// Unit tests for the wave-22 chat + trade-panel reconstructions (guild::gui).
// Golden vectors derived 1:1 from the recovered logic of:
//   VIBE_ChatConsole_BuildWindow        @0x4bfc48  (recipient build + submit + persist)
//   VIBE_TradePanel_RefreshItemColumns  @0x50b1c4  (two-pass item refresh + col table)
//   VIBE_TradePanel_RefreshSellColumns  @0x50bf08  (sell refresh + 475/476 slot guard)
//
// The column-select tables dword_507C58 / dword_507D68 are byte-identical (verified via
// get_bytes); the channel template dword_4AD48C is 8x 0xFFFFFFFF and the persisted
// recipient state byte_631E80 is 8x 0x01 (both get_bytes-pinned below).
#include "gui/chatconsole.h"
#include "gui/trade_panel.h"
#include "gui/trade_panel_windows.h"

#include "tests/framework/test.h"

#include <array>
#include <vector>

using namespace guild::gui;

// ===========================================================================
// ChatConsole_BuildWindow @0x4bfc48 — recipient build + submit + persist.
// ===========================================================================

// Template / persisted-state golden pins (get_bytes).
TEST(ChatBuild, TemplateAndPersistConstants) {
    // dword_4AD48C = 8x 0xFFFFFFFF -> channel template is all -1.
    ChatConsole c;
    for (int i = 0; i < kChatChannels; ++i)
        CHECK_EQ(c.Channel(i), kChatChannelEmpty);
    // byte_631E80 boots all-1 (every channel on).
    for (int i = 0; i < kChatPersistedChannels; ++i)
        CHECK_EQ(g_chatRecipientState[i], 1);
}

// The recipient build loop selects players whose channel tag == 7, in order, up to 8.
TEST(ChatBuild, RecipientSelectionTagSeven) {
    ChatConsoleHooks h;
    // 4 tagged (7), interleaved with non-tagged entries.
    h.sceneSlots = {
        {7, 11}, {0, 99}, {7, 22}, {3, 88}, {7, 33}, {7, 44}, {1, 55},
    };
    ChatBuildResult r = ChatConsole_BuildWindow(h);
    CHECK_EQ(r.toggleCount, 4);
    CHECK_EQ(r.channels[0], 11);
    CHECK_EQ(r.channels[1], 22);
    CHECK_EQ(r.channels[2], 33);
    CHECK_EQ(r.channels[3], 44);
    // Remaining channels stay -1 (template).
    CHECK_EQ(r.channels[4], kChatChannelEmpty);
    CHECK_EQ(r.channels[7], kChatChannelEmpty);
}

// The 8-channel capacity caps the build loop (v29[8]).
TEST(ChatBuild, CapacityCap) {
    ChatConsoleHooks h;
    for (int i = 0; i < 12; ++i) h.sceneSlots.push_back({7, 100 + i});
    ChatBuildResult r = ChatConsole_BuildWindow(h);
    CHECK_EQ(r.toggleCount, 8);
    CHECK_EQ(r.channels[7], 107);
}

// Submit path assembles "$<colour>FF <text>$A" and forwards it.
TEST(ChatBuild, SubmitAssemblesLine) {
    ChatConsoleHooks h;
    h.sceneSlots = {{7, 5}};
    h.didSubmit = true;
    h.speakerColour = 7;           // dword_12CE964[..] - 1342 == 7
    h.submitText = "hallo";
    ChatBuildResult r = ChatConsole_BuildWindow(h);
    CHECK(r.submitted);
    CHECK(r.forwardedLine == std::string("$7FF hallo$A"));
    CHECK(h.lastForwardedLine == r.forwardedLine);
}

// No submit -> no forward.
TEST(ChatBuild, CancelNoForward) {
    ChatConsoleHooks h;
    h.sceneSlots = {{7, 5}};
    h.didSubmit = false;
    ChatBuildResult r = ChatConsole_BuildWindow(h);
    CHECK(!r.submitted);
    CHECK(r.forwardedLine.empty());
}

// On close, each built toggle's value is persisted back to g_chatRecipientState.
TEST(ChatBuild, PersistBack) {
    g_chatRecipientState = {1, 1, 1, 1, 1, 1, 1, 1};
    ChatConsoleHooks h;
    h.sceneSlots = {{7, 1}, {7, 2}};       // 2 toggles built
    h.toggleValues = {0, 1, 1, 1, 1, 1, 1, 1};  // user turned channel 0 off
    ChatConsole_BuildWindow(h);
    CHECK_EQ(g_chatRecipientState[0], 0);  // persisted
    CHECK_EQ(g_chatRecipientState[1], 1);
    CHECK_EQ(g_chatRecipientState[2], 1);  // untouched (no 3rd toggle built)
}

// AssembleLine prefix/suffix golden (aIff "$%iFF ", aA "$A").
TEST(ChatBuild, AssembleLineGolden) {
    CHECK(ChatConsole_AssembleLine(0, "x") == std::string("$0FF x$A"));
    CHECK(ChatConsole_AssembleLine(42, "") == std::string("$42FF $A"));
}

// ===========================================================================
// Column-select tables — byte-identical golden pin (get_bytes 0x507C58 / 0x507D68).
// ===========================================================================
TEST(TradeRefresh, ColumnTablesByteIdentical) {
    // 17 rows x 4, with the staircase pattern recovered from get_bytes.
    const i32 expected[kColTableRows][kColTableCols] = {
        {2,0,0,0}, {2,0,0,0}, {2,0,0,0}, {3,0,0,0}, {4,0,0,0},
        {3,2,0,0}, {4,2,0,0}, {4,3,0,0}, {4,4,0,0}, {4,3,2,0},
        {4,3,3,0}, {4,4,3,0}, {4,4,4,0}, {4,4,3,2}, {4,4,3,3},
        {4,4,4,3}, {4,4,4,4},
    };
    for (int r = 0; r < kColTableRows; ++r)
        for (int c = 0; c < kColTableCols; ++c) {
            CHECK_EQ(kColTableItem[r][c], expected[r][c]);
            CHECK_EQ(kColTableSell[r][c], expected[r][c]);  // identical to item table
        }
}

// Pass-0 column scratch fill: v3 += 5 BEFORE write -> scratch[5],[10],[15], 4th clamped.
TEST(TradeRefresh, ColumnScratchFill) {
    TradePanel_InitSlotTables();
    i32 scratch[16];
    // page 16 -> row {4,4,4,4}; selectors land at 5/10/15(+clamped tail).
    (void)TradePanel_RefreshItemColumns(16, {}, scratch);
    CHECK_EQ(scratch[5], 4);
    CHECK_EQ(scratch[10], 4);
    CHECK_EQ(scratch[15], 4);  // 3rd selector + clamped 4th both 4 here
    CHECK_EQ(scratch[0], 0);   // untouched window slot
    CHECK_EQ(scratch[1], 0);

    // page 5 -> row {3,2,0,0}.
    (void)TradePanel_RefreshItemColumns(5, {}, scratch);
    CHECK_EQ(scratch[5], 3);
    CHECK_EQ(scratch[10], 2);
    CHECK_EQ(scratch[15], 0);  // 3rd selector 0; 4th (clamped) also 0
}

// Item refresh pass-1: new ids fill free slots; pass-2 refreshes stock/capacity.
TEST(TradeRefresh, ItemAppendAndRefresh) {
    TradePanel_InitSlotTables();
    i32 scratch[16];
    std::vector<RefreshScanObject> objs = {
        {300, 7, 64, true},
        {301, 3, 64, true},
    };
    int live = TradePanel_RefreshItemColumns(0, objs, scratch);
    CHECK_EQ(g_buySlots[0].itemId, 300);
    CHECK_EQ(g_buySlots[0].stock, 7);     // pass-2 set effective stock
    CHECK_EQ(g_buySlots[0].capacity, 64);
    CHECK_EQ(g_buySlots[1].itemId, 301);
    CHECK_EQ(g_buySlots[1].stock, 3);
    CHECK_EQ(live, 1);                     // typed final scan: occupied + positive stock
}

// Item refresh: an existing slot whose object disappeared is cleared in pass-2.
TEST(TradeRefresh, ItemClearsVanishedSlot) {
    TradePanel_InitSlotTables();
    i32 scratch[16];
    // First populate slot 0 with id 300.
    (void)TradePanel_RefreshItemColumns(0, {{300, 5, 64, true}}, scratch);
    CHECK_EQ(g_buySlots[0].itemId, 300);
    // Now refresh with NO objects -> pass-2 finds slot 0's object gone -> clears id.
    int live = TradePanel_RefreshItemColumns(0, {}, scratch);
    CHECK_EQ(g_buySlots[0].itemId, 0);
    CHECK_EQ(live, 0);
}

// Sell refresh, non-guarded building: behaves like item (no 475/476 guard).
TEST(TradeRefresh, SellNonGuarded) {
    TradePanel_InitSlotTables();
    i32 scratch[16];
    RefreshBuildingCtx ctx; ctx.buildingType = 100;  // not 475/476
    CHECK(!ctx.isSlotGuarded());
    int live = TradePanel_RefreshSellColumns(0, ctx, {{400, 9, 32, false}}, scratch);
    CHECK_EQ(g_buySlots[0].itemId, 400);
    CHECK_EQ(g_buySlots[0].stock, 9);
    CHECK_EQ(g_buySlots[0].capacity, 32);
    CHECK_EQ(live, 1);
}

// Sell refresh, 475/476 building: an object failing the slot guard is DROPPED in pass-1.
TEST(TradeRefresh, SellGuardDropsIneligible) {
    TradePanel_InitSlotTables();
    i32 scratch[16];
    RefreshBuildingCtx ctx; ctx.buildingType = 475;
    CHECK(ctx.isSlotGuarded());
    std::vector<RefreshScanObject> objs = {
        {500, 4, 16, false},   // FindSlotByProt[1] == 0 -> not sell-eligible -> dropped
        {501, 6, 16, true},    // eligible -> kept
    };
    int live = TradePanel_RefreshSellColumns(0, ctx, objs, scratch);
    // 500 dropped; 501 should be the only occupant.
    bool has500 = false, has501 = false;
    for (int k = 0; k < kBuySlotCount; ++k) {
        if (g_buySlots[k].itemId == 500) has500 = true;
        if (g_buySlots[k].itemId == 501) has501 = true;
    }
    CHECK(!has500);
    CHECK(has501);
    CHECK_EQ(live, 1);
}

// Sell refresh 475/476: pass-2 leaves a guard-failing existing slot untouched (no refresh,
// no clear) — the original's `if (v15 && v15[1])` else falls through.
TEST(TradeRefresh, SellGuardPass2Untouched) {
    TradePanel_InitSlotTables();
    i32 scratch[16];
    RefreshBuildingCtx ctxOpen; ctxOpen.buildingType = 100;
    // Seed slot 0 with id 600 via a non-guarded refresh (stock 2).
    (void)TradePanel_RefreshSellColumns(0, ctxOpen, {{600, 2, 8, true}}, scratch);
    CHECK_EQ(g_buySlots[0].itemId, 600);
    CHECK_EQ(g_buySlots[0].stock, 2);
    // Now a 475 refresh where 600's guard fails: pass-1 finds it (sets stock to new),
    // pass-2 (guard fails) leaves it. We assert the slot is NOT cleared.
    RefreshBuildingCtx ctx475; ctx475.buildingType = 475;
    (void)TradePanel_RefreshSellColumns(0, ctx475, {{600, 5, 8, false}}, scratch);
    CHECK_EQ(g_buySlots[0].itemId, 600);   // still present (not cleared)
}

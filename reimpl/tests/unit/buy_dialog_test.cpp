// Unit (golden-vector) tests for the player buy dialogs (gui/buy_dialog).
//   VIBE_Panel_RunApBuy / VIBE_Form_PopulateObjectList (occupant picker)
//   VIBE_WineCellar_ShowBuyDialog (cellar buy display + buy math)
#include "test.h"

#include "gui/buy_dialog.h"
#include "world/trade_player.h"

#include <vector>

using namespace guild;

namespace {

// A fixed occupant source for the AP buy picker.
struct FixedOccupants : gui::OccupantSource {
    std::vector<int> handles;
    std::vector<int> Enumerate(int) override { return handles; }
};

// Records the one Cmd15 leg the cellar buy emits.
struct CapturedCmd {
    bool fired = false;
    world::TradeMoneyCommand cmd{};
};
void CaptureHook(const world::TradeMoneyCommand& c, void* ctx) {
    auto* cc = static_cast<CapturedCmd*>(ctx);
    cc->fired = true;
    cc->cmd = c;
}

} // namespace

// --- VIBE_Form_PopulateObjectList -------------------------------------------
TEST(BuyDialog, PopulateObjectList_BindsRows) {
    FixedOccupants src;
    src.handles = {11, 22, 33};
    gui::BuyDialog_SetOccupantSource(&src);

    std::vector<gui::ListRow> rows;
    int n = gui::BuyDialog_PopulateObjectList(0, 0, /*building*/ 7, &rows);

    CHECK_EQ(n, 3);
    CHECK_EQ((int)rows.size(), 3);
    // Each row binds a distinct child-object id parallel to its record handle.
    CHECK_EQ(rows[0].record, 11);
    CHECK_EQ(rows[1].record, 22);
    CHECK_EQ(rows[2].record, 33);
    CHECK(rows[0].objId != rows[1].objId);
    CHECK(rows[1].objId != rows[2].objId);
    gui::BuyDialog_SetOccupantSource(nullptr);
}

// A null record terminates the original's row loop early.
TEST(BuyDialog, PopulateObjectList_NullTerminates) {
    FixedOccupants src;
    src.handles = {5, 0, 9};   // the 0 ends the list
    gui::BuyDialog_SetOccupantSource(&src);

    std::vector<gui::ListRow> rows;
    int n = gui::BuyDialog_PopulateObjectList(0, 0, 1, &rows);
    CHECK_EQ(n, 1);
    CHECK_EQ((int)rows.size(), 1);
    CHECK_EQ(rows[0].record, 5);
    gui::BuyDialog_SetOccupantSource(nullptr);
}

// --- VIBE_Panel_RunApBuy ----------------------------------------------------
TEST(BuyDialog, ApBuy_SelectMatchesRow) {
    FixedOccupants src;
    src.handles = {100, 200, 300};
    gui::BuyDialog_SetOccupantSource(&src);

    gui::ApBuyLayout l = gui::BuyDialog_BuildApBuy(/*building*/ 42);
    CHECK_EQ(l.form, gui::kFormApPanel);
    CHECK_EQ(l.textHeader, 6868);
    CHECK_EQ(l.loopForm, 415687);
    CHECK_EQ(l.rowCount, 3);

    // Click id 1210 on the 2nd row's object id -> selects row index 1.
    int row = -99; bool cancel = true;
    bool sel = gui::BuyDialog_DispatchApBuy(l, gui::kBuyClickSelect, l.rows[1].objId,
                                            /*cancelFlag*/ false, &row, &cancel);
    CHECK(sel);
    CHECK_EQ(row, 1);
    CHECK(!cancel);

    // A click on an unknown object id matches nothing.
    row = -99;
    sel = gui::BuyDialog_DispatchApBuy(l, gui::kBuyClickSelect, /*obj*/ 999999, false,
                                       &row, &cancel);
    CHECK(!sel);
    CHECK_EQ(row, -1);

    // The right-click cancel flag aborts before any match.
    bool outc = false;
    sel = gui::BuyDialog_DispatchApBuy(l, gui::kBuyClickSelect, l.rows[0].objId,
                                       /*cancelFlag*/ true, &row, &outc);
    CHECK(!sel);
    CHECK(outc);
    gui::BuyDialog_SetOccupantSource(nullptr);
}

// --- VIBE_WineCellar_ShowBuyDialog: display math ----------------------------
TEST(BuyDialog, WineCellar_DisplayMath_Identity) {
    // rate 1 (home currency): playerHeld 250, cellar 1000.
    world::TradeSetCurrencyRateTable({1, 1});
    world::TradeSetCityCurrencyTable({0});

    gui::WineBuyLayout l = gui::BuyDialog_BuildWineCellar(250, 1000, /*city*/ 0, 7);
    CHECK_EQ(l.form, gui::kFormWineCellar);
    CHECK_EQ(l.textTitle, 5721);
    CHECK_EQ(l.textPrice, 5722);
    CHECK_EQ(l.loopForm, 423879);
    CHECK_EQ(l.priceObjId, 7);
    CHECK_EQ(l.playerCashDisp, 250);
    CHECK_EQ(l.cellarCashDisp, 1000);
    CHECK_EQ(l.shownValue, 1250);   // playerDisp > 0 -> cellar + player

    // playerHeld 0 -> playerDisp 0 -> show cellar only.
    gui::WineBuyLayout z = gui::BuyDialog_BuildWineCellar(0, 500, 0, 7);
    CHECK_EQ(z.playerCashDisp, 0);
    CHECK_EQ(z.cellarCashDisp, 500);
    CHECK_EQ(z.shownValue, 500);
}

TEST(BuyDialog, WineCellar_DisplayMath_ForeignRate) {
    // rate 5 (foreign currency): 250/5+0.5=50.5->50; 1000/5+0.5=200.5->200.
    world::TradeSetCurrencyRateTable({1, 5});
    world::TradeSetCityCurrencyTable({1});   // city 0 -> currency 1 -> rate 5

    gui::WineBuyLayout l = gui::BuyDialog_BuildWineCellar(250, 1000, /*city*/ 0, 3);
    CHECK_EQ(l.playerCashDisp, 50);
    CHECK_EQ(l.cellarCashDisp, 200);
    CHECK_EQ(l.shownValue, 250);
}

// --- VIBE_WineCellar_ShowBuyDialog: buy wiring ------------------------------
TEST(BuyDialog, WineCellar_Buy_OverdraftLeg) {
    // rate 1; itemValue 300 -> cost 300; playerCash 250 -> cannot afford.
    world::TradeSetCurrencyRateTable({1, 1});
    world::TradeSetCityCurrencyTable({0});

    CapturedCmd cc;
    world::TradeSetMoneyHook(&CaptureHook, &cc);

    gui::WineBuyLayout l = gui::BuyDialog_BuildWineCellar(250, 1000, 0, 7);
    bool afford = true, cancel = true;
    int cost = gui::BuyDialog_DispatchWineCellar(l, /*itemValue*/ 300, /*city*/ 0,
                                                 /*playerCash*/ 250, /*player*/ 1,
                                                 /*seller*/ 2, gui::kBuyClickSelect,
                                                 /*cancelFlag*/ false, /*commit*/ true,
                                                 &afford, &cancel);
    CHECK_EQ(cost, 300);
    CHECK(!afford);
    CHECK(!cancel);
    CHECK(cc.fired);
    // Overdraft leg: seller -> player, amount = cost - playerCash = 50.
    CHECK_EQ(cc.cmd.payer, 2);
    CHECK_EQ(cc.cmd.recipient, 1);
    CHECK_EQ(cc.cmd.amount, 50);

    world::TradeSetMoneyHook(nullptr, nullptr);
}

TEST(BuyDialog, WineCellar_Buy_AffordLeg) {
    // rate 1; itemValue 100 -> cost 100; playerCash 250 -> can afford.
    world::TradeSetCurrencyRateTable({1, 1});
    world::TradeSetCityCurrencyTable({0});

    CapturedCmd cc;
    world::TradeSetMoneyHook(&CaptureHook, &cc);

    gui::WineBuyLayout l = gui::BuyDialog_BuildWineCellar(250, 1000, 0, 7);
    bool afford = false, cancel = true;
    int cost = gui::BuyDialog_DispatchWineCellar(l, 100, 0, 250, 1, 2,
                                                 gui::kBuyClickSelect, false, true,
                                                 &afford, &cancel);
    CHECK_EQ(cost, 100);
    CHECK(afford);
    // Afford leg: player -> seller, amount = playerCash - cost = 150.
    CHECK_EQ(cc.cmd.payer, 1);
    CHECK_EQ(cc.cmd.recipient, 2);
    CHECK_EQ(cc.cmd.amount, 150);

    world::TradeSetMoneyHook(nullptr, nullptr);
}

TEST(BuyDialog, WineCellar_Cancel_NoCommand) {
    world::TradeSetCurrencyRateTable({1, 1});
    world::TradeSetCityCurrencyTable({0});
    CapturedCmd cc;
    world::TradeSetMoneyHook(&CaptureHook, &cc);

    gui::WineBuyLayout l = gui::BuyDialog_BuildWineCellar(250, 1000, 0, 7);

    // Right-click cancel: no command, outCancel set.
    bool afford = true, cancel = false;
    int cost = gui::BuyDialog_DispatchWineCellar(l, 100, 0, 250, 1, 2,
                                                 gui::kBuyClickSelect,
                                                 /*cancelFlag*/ true, true,
                                                 &afford, &cancel);
    CHECK_EQ(cost, 0);
    CHECK(cancel);
    CHECK(!cc.fired);

    // The cancel click id (1155) also aborts.
    cancel = false;
    cost = gui::BuyDialog_DispatchWineCellar(l, 100, 0, 250, 1, 2,
                                             gui::kBuyClickCancel, false, true,
                                             &afford, &cancel);
    CHECK_EQ(cost, 0);
    CHECK(cancel);
    CHECK(!cc.fired);

    world::TradeSetMoneyHook(nullptr, nullptr);
}

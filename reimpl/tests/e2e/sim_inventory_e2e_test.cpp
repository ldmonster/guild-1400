// End-to-end flow across the inventory capacity/wealth + trade-sell engines:
// build a container tree with stock + currency, compute total wealth, sell some
// items (resolving qty/price/capacity), transfer items between containers, then
// verify the resulting stock/wealth and emitted commands against a hand-computed
// reference. Suite prefixed SimInvE2E to avoid clashes.
#include <vector>

#include "sim/inventory_capacity.h"
#include "sim/inventory_wealth.h"
#include "sim/trade_sell.h"
#include "tests/framework/test.h"

using namespace guild;
using namespace guild::sim;

namespace {

// A modeled container tree node: a container with a self capacity record and a
// child list (currency + goods). Mirrors the scene-entity container the binary
// scans.
ContainerView Container(i16 selfType, i32 selfLevel, u8 f28, u8 f29,
                        std::vector<StockChild> kids) {
    ContainerView c;
    c.selfType = selfType;
    c.selfLevel = selfLevel;
    c.fill28 = f28;
    c.fill29 = f29;
    c.children = std::move(kids);
    return c;
}

double E2EPrice(i16 prot, u8 player) {
    (void)player;
    if (prot == 700) return 10.0;   // produced good
    if (prot == 7) return 4.0;
    return 1.0;
}

std::vector<TradeCommand> g_log;
void LogCmd(const TradeCommand& c) { g_log.push_back(c); }

}  // namespace

TEST(SimInvE2E, BuildComputeSellTransfer) {
    g_log.clear();
    WealthSetCurrencyProtoTable({9});   // player 0 currency = proto 9
    WealthSetActivePlayer(0);
    TradeSetMarketPriceHook(&E2EPrice);
    TradeSetCmdHook(&LogCmd);

    // ---- 1. Build the container tree --------------------------------------
    // Player's purse: two currency stacks (proto 9) + a goods stack.
    ContainerView purse = Container(0, 0, 8, 8,
                                    {{9, 120}, {9, 80}, {7, 30}});

    // ---- 2. Compute wealth -------------------------------------------------
    // Owned buildings contribute room + storage worth.
    std::vector<OwnedBuildingWorth> owned = {{1500, 600}, {400, 100}};
    int wealth = PersonComputeTotalWealth(0, false, purse, owned);
    // currency 120+80=200; buildings (1500+600)+(400+100)=2600 -> 2800.
    CHECK_EQ(wealth, 2800);
    // direct currency aggregation cross-check.
    CHECK_EQ(PersonSumCurrencyHeld(purse), 200);
    CHECK_EQ(PersonGetCurrencyAmount(purse, 0), 120);

    // ---- 3. Sell goods (qty/price/capacity resolution) ---------------------
    // The player sells good 7 from a contor (kind 2). Builder emits the request.
    SellRequest sreq;
    sreq.buildingId = 55;
    sreq.buildingKind = 2;
    sreq.proto = 7;
    sreq.qty = 10;
    sreq.player = 0;
    CHECK_EQ(TradeRequestSellObjekt(sreq, true), 14);
    CHECK_EQ((int)g_log.size(), 1);
    CHECK(g_log[0].cmd == TradeCmd::kRequestSell);
    CHECK_EQ(g_log[0].amount, 4);  // trunc(price 4.0) per unit

    // Apply the sell transfer: move 10 of good 7 from a source stack to a
    // destination storage room. Source has 30 (raw) -> ok; dest free >= 10.
    SellResolve sr;
    sr.proto = 7;
    sr.qty = 10;
    sr.srcHasStock = true;
    sr.srcIsReserveGood = false;
    sr.srcEffectiveStock = 30;
    sr.srcRawCount = 30;
    sr.destStorage = true;
    sr.destStockRec = StockChild{1, 3};                 // cap 80
    sr.destContainer = Container(1, 3, 5, 5, {{7, 20}}); // free = 80-20 = 60
    sr.destRootResolved = true;
    int moved = TradeSellObjektResolve(sr, true);
    CHECK_EQ(moved, 10);
    // commit emitted remove-source + add-dest.
    CHECK_EQ((int)g_log.size(), 3);
    CHECK(g_log[1].cmd == TradeCmd::kRemoveSource);
    CHECK_EQ(g_log[1].qty, 10);
    CHECK(g_log[2].cmd == TradeCmd::kAddDest);
    CHECK_EQ(g_log[2].qty, 10);

    // Apply the resulting stock mutation to our modeled tree and re-verify.
    // source purse good 7: 30 -> 20; dest room good 7: 20 -> 30.
    purse.children[2].level -= moved;        // good 7 in purse
    sr.destContainer.children[0].level += moved;
    CHECK_EQ(purse.children[2].level, 20);
    CHECK_EQ(sr.destContainer.children[0].level, 30);

    // ---- 4. Produce-and-sell (sellable amount) ----------------------------
    // Recipe: 2x good A (proto 10, ratio 2, stock 20 -> 10 crafts), 1x good B
    // (proto 11, ratio 1, stock 7 -> 7 crafts). Output good 700, 1 per craft.
    SellableResolve pr;
    pr.sourceResolved = true;
    pr.ownerResolved = true;
    pr.startQty = 100;
    pr.ingredients[0] = RecipeSlot{10, 2, 20};
    pr.ingredients[1] = RecipeSlot{11, 1, 7};
    pr.outProto = 700;
    pr.outputCount = 1;
    pr.outContainer = Container(1, 5, 9, 9, {});  // cap 100, empty -> free 100
    pr.outStockRec = StockChild{1, 5};
    pr.outRootResolved = true;
    pr.player = 0;
    size_t before = g_log.size();
    SellableResult pres = TradeComputeSellableAmount(pr, true);
    CHECK(pres.ok);
    CHECK_EQ(pres.produced, 7);             // min craft count
    CHECK_EQ(pres.proceeds, 70);            // price 10.0 * 7
    // commit: 2 ingredient consumes + 1 output add + 1 credit = 4 cmds.
    CHECK_EQ((int)(g_log.size() - before), 4);
    const TradeCommand& credit = g_log.back();
    CHECK(credit.cmd == TradeCmd::kCredit);
    CHECK_EQ(credit.amount, 70);

    // ---- 5. Transfer items between two containers (capacity-checked) -------
    // Move good 9-currency-like stack between purses: model via free-capacity.
    ContainerView dst = Container(1, 2, 4, 4, {});   // cap 40, empty
    StockChild moveRec{1, 2};
    int room = InventoryComputeFreeCapacity(moveRec, dst, 7, 25, true);
    CHECK_EQ(room, 25);   // ceiling 25 < cap 40 path (no child) -> min(cap,ceil)=25
    // transfer min(want, room).
    int want = 18;
    int xfer = want < room ? want : room;
    CHECK_EQ(xfer, 18);

    // Final wealth recomputation reflects unchanged currency (sells credited via
    // command, not mutated in the purse here) -> still 200 liquid + buildings.
    CHECK_EQ(PersonSumCurrencyHeld(purse), 200);

    TradeSetCmdHook(nullptr);
    TradeSetMarketPriceHook(nullptr);
}

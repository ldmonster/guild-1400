// Unit tests for the inventory capacity/wealth engines + the player trade-sell
// resolution (gilde.exe). Golden vectors computed with python3 (see the agent
// report). Suites are prefixed SimInv* to avoid clashes.
#include <vector>

#include "sim/inventory_capacity.h"
#include "sim/inventory_wealth.h"
#include "sim/trade_sell.h"
#include "tests/framework/test.h"

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// Capacity table + reserve rule.
// ---------------------------------------------------------------------------
TEST(SimInvCapacity, SlotCapacityTable) {
    CHECK_EQ(InventorySlotCapacity(477, 3), 25);   // 5*3+10
    CHECK_EQ(InventorySlotCapacity(477, 0), 10);   // 5*0+10
    CHECK_EQ(InventorySlotCapacity(1, 3), 80);     // level 3 special
    CHECK_EQ(InventorySlotCapacity(1, 2), 40);     // 20*2
    CHECK_EQ(InventorySlotCapacity(9, 5), 100);    // 20*5
}

TEST(SimInvCapacity, EffectiveStockReserve) {
    CHECK_EQ(InventoryEffectiveStock(42, 10), 9);
    CHECK_EQ(InventoryEffectiveStock(278, 1), 0);
    CHECK_EQ(InventoryEffectiveStock(475, 4), 3);
    CHECK_EQ(InventoryEffectiveStock(476, 4), 3);
    CHECK_EQ(InventoryEffectiveStock(9, 10), 10);  // non-reserve
}

static ContainerView MakeContainer(i16 selfType, i32 selfLevel, u8 f28, u8 f29,
                                   std::vector<StockChild> kids) {
    ContainerView c;
    c.selfType = selfType;
    c.selfLevel = selfLevel;
    c.fill28 = f28;
    c.fill29 = f29;
    c.children = std::move(kids);
    return c;
}

// ---------------------------------------------------------------------------
// FindItemStock.
// ---------------------------------------------------------------------------
TEST(SimInvCapacity, FindItemStock) {
    ContainerView c = MakeContainer(1, 2, 5, 5, {{42, 10}, {9, 50}});
    CHECK_EQ(InventoryFindItemStock(c, 42), 9);    // reserve-1
    CHECK_EQ(InventoryFindItemStock(c, 9), 50);    // non-reserve
    CHECK_EQ(InventoryFindItemStock(c, 999), 0);   // absent
}

// ---------------------------------------------------------------------------
// ComputeFreeSpaceForItem  0x59266c.
// ---------------------------------------------------------------------------
TEST(SimInvCapacity, FreeSpaceForItem) {
    // self type 1 level 2 -> cap 40. child of type 7 level 30 -> 40-30=10.
    ContainerView c = MakeContainer(1, 2, 5, 5, {{7, 30}});
    CHECK_EQ(InventoryComputeFreeSpaceForItem(c, 7, 100), 10);

    // no child of type 8: count 1 < fill28 5, cap 40 < ceiling 100 -> 40.
    CHECK_EQ(InventoryComputeFreeSpaceForItem(c, 8, 100), 40);

    // full container: count >= fill28.
    ContainerView full = MakeContainer(1, 2, 1, 1, {{7, 30}});
    CHECK_EQ(InventoryComputeFreeSpaceForItem(full, 8, 100), 0);
}

// ---------------------------------------------------------------------------
// ComputeFreeCapacity  0x5924a8.
// ---------------------------------------------------------------------------
TEST(SimInvCapacity, FreeCapacity) {
    // stock record type 1 level 2 -> cap 40; child of proto 7 eff 10 -> 30.
    StockChild st{1, 2};
    ContainerView c = MakeContainer(1, 2, 3, 3, {{7, 10}});
    CHECK_EQ(InventoryComputeFreeCapacity(st, c, 7, 100, true), 30);

    // 42 reserve good, no child, both gates open -> min(cap40, ceil100) = 40.
    StockChild st42{42, 2};
    ContainerView empty = MakeContainer(42, 2, 3, 3, {});
    CHECK_EQ(InventoryComputeFreeCapacity(st42, empty, 7, 100, true), 40);

    // non-42, no child, count 2 < fill28 3 -> 40.
    ContainerView two = MakeContainer(1, 2, 3, 3, {{8, 1}, {9, 1}});
    CHECK_EQ(InventoryComputeFreeCapacity(st, two, 7, 100, true), 40);

    // root unresolved -> 0.
    CHECK_EQ(InventoryComputeFreeCapacity(st, c, 7, 100, false), 0);
}

// ---------------------------------------------------------------------------
// ComputeCarryCapacity  0x592710.
// ---------------------------------------------------------------------------
TEST(SimInvCapacity, CarryCapacity) {
    ContainerView none = MakeContainer(0, 0, 0, 0, {});
    // money category 9 -> unlimited (ceiling).
    CHECK_EQ(InventoryComputeCarryCapacity(none, 5, 99, 0, false, 9, true), 99);
    // proto 377 -> 0.
    CHECK_EQ(InventoryComputeCarryCapacity(none, 377, 5, 0, false, 1, true), 0);
    // avatar-bearing actor (kind 6 + avatar) -> 0.
    CHECK_EQ(InventoryComputeCarryCapacity(none, 5, 5, 6, true, 1, true), 0);

    // carried child level 1, ceiling 2: 2+1=3 not >3 -> v4 = ceiling = 2.
    ContainerView c1 = MakeContainer(0, 0, 0, 0, {{5, 1}});
    CHECK_EQ(InventoryComputeCarryCapacity(c1, 5, 2, 0, false, 1, true), 2);
    // carried child level 2, ceiling 5: 5+2=7>3 -> 3-2 = 1.
    ContainerView c2 = MakeContainer(0, 0, 0, 0, {{5, 2}});
    CHECK_EQ(InventoryComputeCarryCapacity(c2, 5, 5, 0, false, 1, true), 1);

    // no carried child: ceiling 2 <= 3 -> 2.
    CHECK_EQ(InventoryComputeCarryCapacity(none, 5, 2, 0, false, 1, true), 2);
    // no carried child: ceiling 9 > 3 -> 3.
    CHECK_EQ(InventoryComputeCarryCapacity(none, 5, 9, 0, false, 1, true), 3);
    // 6 children -> 0.
    ContainerView six = MakeContainer(0, 0, 0, 0,
                                      {{1, 1}, {2, 1}, {3, 1}, {4, 1}, {6, 1}, {7, 1}});
    CHECK_EQ(InventoryComputeCarryCapacity(six, 5, 9, 0, false, 1, true), 0);
}

// ---------------------------------------------------------------------------
// Collect* engines  0x590fc0 / 0x590df8.
// ---------------------------------------------------------------------------
TEST(SimInvCapacity, CollectStorageSlots) {
    // room with 3 children: cat 23, cat 5, cat 23. mode 1 keeps cat==23.
    ContainerView room = MakeContainer(42, 2, 0, 0, {{100, 5}, {101, 8}, {102, 3}});
    std::vector<u8> cats = {23, 5, 23};
    CollectedSlots out;
    int rc = InventoryCollectStorageSlots(room, /*278*/ false, /*mode*/ 1, cats, out);
    CHECK_EQ(rc, 1);
    CHECK_EQ(out.count, 2);
    CHECK_EQ(out.types[0], (i16)100);
    CHECK_EQ(out.types[1], (i16)102);
    CHECK_EQ(out.stocks[0], 5);   // type 100 not reserve -> eff == level
    CHECK_EQ(out.caps[0], 100);   // cap 20*5
    CHECK_EQ(out.caps[1], 80);    // type 102 level 3 -> special cap 80

    // mode 0 keeps cat != 23 -> only child 1 (cat 5).
    CollectedSlots out0;
    InventoryCollectStorageSlots(room, false, 0, cats, out0);
    CHECK_EQ(out0.count, 1);
    CHECK_EQ(out0.types[0], (i16)101);
}

TEST(SimInvCapacity, CollectWorkstationSlots) {
    // workstation admits cat 23 and 37 when mode.
    ContainerView room = MakeContainer(42, 2, 0, 0, {{100, 5}, {101, 8}, {102, 3}});
    std::vector<u8> cats = {23, 37, 5};
    CollectedSlots out;
    int rc = InventoryCollectWorkstationSlots(room, false, 1, cats, out);
    CHECK_EQ(rc, 1);
    CHECK_EQ(out.count, 2);     // cat 23 and 37 kept
    CHECK_EQ(out.types[0], (i16)100);
    CHECK_EQ(out.types[1], (i16)101);
}

// ---------------------------------------------------------------------------
// Wealth aggregation  0x59152c / 0x5915b8 / 0x591f7c.
// ---------------------------------------------------------------------------
TEST(SimInvWealth, CurrencyAggregation) {
    // player 0 currency proto = 9, player 1 -> 17.
    WealthSetCurrencyProtoTable({9, 17});
    WealthSetActivePlayer(0);

    ContainerView c = MakeContainer(0, 0, 0, 0, {{9, 100}, {9, 50}, {17, 7}});
    // GetCurrencyAmount(player 0) -> first matching stack amount (100).
    CHECK_EQ(PersonGetCurrencyAmount(c, 0), 100);
    // GetCurrencyAmount(player 1) -> proto 17 stack (7).
    CHECK_EQ(PersonGetCurrencyAmount(c, 1), 7);
    // SumCurrencyHeld(active player 0) -> sum of all proto-9 stacks = 150.
    CHECK_EQ(PersonSumCurrencyHeld(c), 150);
    // absent currency -> 0.
    ContainerView none = MakeContainer(0, 0, 0, 0, {});
    CHECK_EQ(PersonGetCurrencyAmount(none, 0), 0);
}

TEST(SimInvWealth, TotalWealth) {
    WealthSetCurrencyProtoTable({9});
    WealthSetActivePlayer(0);
    ContainerView c = MakeContainer(0, 0, 0, 0, {{9, 200}});
    std::vector<OwnedBuildingWorth> b = {{1000, 500}, {300, 0}};
    // 200 + (1000+500) + (300+0) = 2000.
    CHECK_EQ(PersonComputeTotalWealth(0, false, c, b), 2000);
    // free slot -> -1.
    CHECK_EQ(PersonComputeTotalWealth(0, true, c, b), -1);
    // out-of-range index -> -1.
    CHECK_EQ(PersonComputeTotalWealth(0x300, false, c, b), -1);
}

// ---------------------------------------------------------------------------
// Trade sell request builder  0x46bff0.
// ---------------------------------------------------------------------------
static double FixedPrice(i16 prot, u8 player) {
    (void)player;
    return prot == 700 ? 12.5 : 0.0;
}

TEST(SimInvTrade, RequestSellBuilder) {
    TradeSetMarketPriceHook(&FixedPrice);
    static TradeCommand last;
    static bool got;
    got = false;
    TradeSetCmdHook([](const TradeCommand& c) { last = c; got = true; });

    SellRequest req;
    req.buildingId = 42;
    req.buildingKind = 2;  // sellable contor
    req.proto = 700;
    req.qty = 5;
    req.player = 0;
    int rc = TradeRequestSellObjekt(req, true);
    CHECK_EQ(rc, 14);
    CHECK(got);
    CHECK(last.cmd == TradeCmd::kRequestSell);
    CHECK_EQ(last.seller, 42);
    CHECK_EQ(last.buyer, -1);
    CHECK_EQ(last.qty, 5);
    CHECK_EQ(last.amount, 12);  // trunc(12.5)

    // wrong building kind -> 0, no command.
    req.buildingKind = 3;
    got = false;
    CHECK_EQ(TradeRequestSellObjekt(req, true), 0);
    CHECK(!got);

    TradeSetCmdHook(nullptr);
    TradeSetMarketPriceHook(nullptr);
}

// ---------------------------------------------------------------------------
// Sell transfer resolution  0x496b90 (deterministic core).
// ---------------------------------------------------------------------------
TEST(SimInvTrade, SellTransferResolution) {
    // storage sell: qty 5, reserve good eff 9 raw 10, dest free 30 -> 5 moved.
    SellResolve r;
    r.proto = 7;
    r.qty = 5;
    r.srcHasStock = true;
    r.srcIsReserveGood = true;
    r.srcEffectiveStock = 9;
    r.srcRawCount = 10;
    r.destStorage = true;
    r.destStockRec = StockChild{1, 2};  // cap 40
    r.destContainer = MakeContainer(1, 2, 5, 5, {{7, 10}});  // free = 40-10=30
    r.destRootResolved = true;
    CHECK_EQ(TradeSellObjektResolve(r, false), 5);

    // reserve gate: qty 10 > eff 9 -> reject.
    r.qty = 10;
    CHECK_EQ(TradeSellObjektResolve(r, false), 0);

    // raw count gate: qty 12, eff 20 raw 11 -> reject.
    r.qty = 12;
    r.srcIsReserveGood = false;
    r.srcEffectiveStock = 20;
    r.srcRawCount = 11;
    CHECK_EQ(TradeSellObjektResolve(r, false), 0);

    // carried clamp: requested 5, carry cap clamps to 2.
    SellResolve cr;
    cr.proto = 5;
    cr.qty = 5;
    cr.srcHasStock = true;
    cr.srcRawCount = 10;
    cr.destCarried = true;
    cr.carriable = true;
    cr.typeCategory = 1;
    cr.destContainer = MakeContainer(0, 0, 0, 0, {{5, 1}});  // child level 1, 5+1>3 -> 3-1=2
    CHECK_EQ(TradeSellObjektResolve(cr, false), 2);
}

// ---------------------------------------------------------------------------
// Sellable amount resolution  0x497538 (deterministic core).
// ---------------------------------------------------------------------------
TEST(SimInvTrade, SellableAmountResolution) {
    TradeSetMarketPriceHook(&FixedPrice);
    SellableResolve r;
    r.sourceResolved = true;
    r.ownerResolved = true;
    r.startQty = 100;
    r.ingredients[0] = RecipeSlot{10, 2, 20};  // 20/2 = 10 crafts
    r.ingredients[1] = RecipeSlot{11, 1, 7};   // 7/1 = 7 crafts -> min
    r.outProto = 700;
    r.outputCount = 1;
    r.outContainer = MakeContainer(1, 5, 9, 9, {});  // cap 100, empty -> free 100
    r.outStockRec = StockChild{1, 5};                // cap 100
    r.outRootResolved = true;
    r.player = 0;

    SellableResult res = TradeComputeSellableAmount(r, false);
    CHECK(res.ok);
    CHECK_EQ(res.produced, 7);
    // proceeds via fixed price 12.5 (proto 700) * (1*7) = 87.5 -> 87.
    CHECK_EQ(res.proceeds, 87);

    // missing ingredient stock -> reject.
    SellableResolve miss = r;
    miss.ingredients[0] = RecipeSlot{10, 2, 0};
    CHECK(!TradeComputeSellableAmount(miss, false).ok);

    // output-capacity limited: outCount 2, free 8 -> v14 = 4.
    SellableResolve cap = r;
    cap.outputCount = 2;
    cap.outStockRec = StockChild{1, 0};  // cap 0 -> recompute below
    cap.outContainer = MakeContainer(1, 1, 9, 9, {});  // cap 20
    cap.outStockRec = StockChild{1, 1};                // cap 20 -> free 20; /2 = 10; min(7,10)=7
    SellableResult c2 = TradeComputeSellableAmount(cap, false);
    CHECK_EQ(c2.produced, 7);

    TradeSetMarketPriceHook(nullptr);
}

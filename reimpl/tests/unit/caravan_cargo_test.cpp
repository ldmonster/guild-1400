#include "test.h"

#include "world/caravan_cargo.h"

#include <cmath>
#include <vector>

using namespace guild;
using namespace guild::world;

namespace {
bool deq(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

// Deterministic price oracle for the golden vectors: price(good,ctx) = good*10+ctx.
double GoldenPrice(i32 good, u8 ctx) {
    return static_cast<double>(good * 10 + ctx);
}

CaravanSlot mk(i32 good, i32 obj, i32 data, i32 storage = 0) {
    CaravanSlot s;
    s.goodIdPacked = good << 16; // good == packed >> 16
    s.objectId = obj;
    s.dataPtr = data;
    s.storageSlot = storage;
    return s;
}
} // namespace

TEST(CaravanCargo, InitClearsBothGrids) {
    CaravanCargoTables t;
    // Seed garbage, ensure init replaces with the proper slot counts + empties.
    t.sell.resize(3);
    t.buy.resize(1);
    CaravanInitSlotTables(t);
    CHECK_EQ(static_cast<int>(t.sell.size()), kCaravanSellSlots);
    CHECK_EQ(static_cast<int>(t.buy.size()), kCaravanBuySlots);
    for (const CaravanSlot& s : t.sell) {
        CHECK_EQ(s.objectId, -1);
        CHECK_EQ(s.storageSlot, -1);
        CHECK_EQ(s.dataPtr, 0);
        CHECK_EQ(s.goodIdPacked, -1);
    }
    for (const CaravanSlot& s : t.buy)
        CHECK_EQ(s.objectId, -1);
}

TEST(CaravanCargo, GoodIdHighWordDecode) {
    CaravanSlot s = mk(0x1234, 1, 1);
    CHECK_EQ(s.goodId(), 0x1234);
}

// Golden vector A: ownerIsMarket=true, ctx default(0); empty/obj=-1 slots skipped.
//   gilde.exe 0x53ff3c stores the unit price (var_24) and accumulator (var_2C) as
//   32-bit floats, so 50*1.1, 90*1.1, 110*1.1 each round to exactly 55/99/121 and
//   the sum is float-exact 968.0 (NOT the f64 968.0000000000002 a pure-double
//   accumulation would give).
TEST(CaravanCargo, ComputeCargoValue_OwnerMarket) {
    CaravanSetPriceHook(&GoldenPrice);
    CaravanCargoTables t;
    t.sell = {mk(5, 1, 3), mk(7, -1, 4), mk(9, 2, 2)}; // mid slot empty (obj -1)
    t.buy = {mk(11, 3, 5), mk(13, -1, 1)};             // 2nd empty
    double v = CaravanComputeCargoValue(t, /*ownerIsMarket=*/true,
                                        /*priceMul=*/0.5f,
                                        /*sourceKind71=*/false, /*ctx101=*/9,
                                        /*mode=*/1);
    CHECK(deq(v, 968.0, 1e-6));
    CaravanSetPriceHook(nullptr);
}

// Golden vector B: ownerIsMarket=false, sourceKind71=true (ctx=ctx101=4), mode=2
//   (sell-at-contor => unit re-looked-up at ctx101, ignoring priceMul/market).
//   total = 900.0
TEST(CaravanCargo, ComputeCargoValue_SellAtContor) {
    CaravanSetPriceHook(&GoldenPrice);
    CaravanCargoTables t;
    t.sell = {mk(5, 1, 3), mk(8, 2, 2)};
    t.buy = {mk(11, 3, 5)};
    double v = CaravanComputeCargoValue(t, /*ownerIsMarket=*/false,
                                        /*priceMul=*/0.5f,
                                        /*sourceKind71=*/true, /*ctx101=*/4,
                                        /*mode=*/2);
    CHECK(deq(v, 900.0, 1e-6));
    CaravanSetPriceHook(nullptr);
}

// Golden vector C: priceMul path (not market), mode=1, ctx default(0).
//   total = 250.0
TEST(CaravanCargo, ComputeCargoValue_PriceMul) {
    CaravanSetPriceHook(&GoldenPrice);
    CaravanCargoTables t;
    t.sell = {mk(4, 1, 10)};
    t.buy = {mk(6, 2, 5), mk(6, 3, 5)};
    double v = CaravanComputeCargoValue(t, /*ownerIsMarket=*/false,
                                        /*priceMul=*/0.25f,
                                        /*sourceKind71=*/false, /*ctx101=*/0,
                                        /*mode=*/1);
    CHECK(deq(v, 250.0, 1e-6));
    CaravanSetPriceHook(nullptr);
}

TEST(CaravanCargo, ComputeCargoValue_DataPtrZeroSkipped) {
    CaravanSetPriceHook(&GoldenPrice);
    CaravanCargoTables t;
    t.sell = {mk(5, 1, 0)}; // dataPtr 0 -> skipped even though obj != -1
    t.buy = {};
    double v = CaravanComputeCargoValue(t, true, 1.0f, false, 0, 1);
    CHECK(deq(v, 0.0));
    CaravanSetPriceHook(nullptr);
}

// Load-core: free space gates the line; storageSlot<0 / amount<=0 skip the slot.
TEST(CaravanCargo, LoadFromStorage_AmountAndStorageGuards) {
    CaravanSetPriceHook(&GoldenPrice);
    CaravanCargoTables t;
    // sell: slot0 movable (free 4, storage 2); slot1 free 0 -> skip; slot2 free 3
    //       but storage -1 -> skip.
    t.sell = {mk(5, 1, 9, /*storage=*/2), mk(6, 2, 9, 3), mk(7, 3, 9, -1)};
    std::vector<i32> sellFree = {4, 0, 3};
    t.buy = {mk(8, 4, 9, 1)};
    std::vector<i32> buyFree = {6};
    auto lines = CaravanLoadFromStorage(t, sellFree, buyFree, /*applyPricing=*/true,
                                        /*ownerIsMarket=*/false, /*priceMul=*/2.0f,
                                        /*priceCtx=*/0, /*sellCtx=*/0, /*mode=*/1);
    // Expect 2 lines: sell slot0 and buy slot0.
    CHECK_EQ(static_cast<int>(lines.size()), 2);
    CHECK_EQ(lines[0].goodId, 5);
    CHECK_EQ(lines[0].quantity, 4);
    CHECK_EQ(lines[0].toSlot, 2);
    // unit = price(5,0)*2.0 = 50*2 = 100
    CHECK(deq(lines[0].unitPrice, 100.0));
    CHECK_EQ(lines[1].goodId, 8);
    CHECK_EQ(lines[1].quantity, 6);
    CHECK_EQ(lines[1].toSlot, 1);
    CHECK(deq(lines[1].unitPrice, 160.0)); // price(8,0)=80 *2
    CaravanSetPriceHook(nullptr);
}

TEST(CaravanCargo, LoadFromStorage_NoPricingZeroUnit) {
    CaravanSetPriceHook(&GoldenPrice);
    CaravanCargoTables t;
    t.sell = {mk(5, 1, 9, 0)};
    auto lines = CaravanLoadFromStorage(t, {3}, {}, /*applyPricing=*/false, true,
                                        1.0f, 0, 0, 1);
    CHECK_EQ(static_cast<int>(lines.size()), 1);
    CHECK(deq(lines[0].unitPrice, 0.0));
    CHECK_EQ(lines[0].quantity, 3);
    CaravanSetPriceHook(nullptr);
}

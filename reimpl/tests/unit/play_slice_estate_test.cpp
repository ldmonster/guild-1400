// tests/unit/play_slice_estate_test.cpp — UNIT: the REAL-ESTATE (buy building) slice
// classifier + apply, on a SYNTHETIC live world (no assets).
//
//   * ClassifyEstateInteraction (the golden): (kind, price) -> the opcode-56
//     ownership command (VIBE_Command_QueueRequestQuad56).
//   * The apply reparents the bought object (owner +39, parent handle +37) and
//     debits the buyer's folded cash (Person +0x0A).
//   * The full slice is deterministic (same inputs -> identical world hashes).
#include "test.h"

#include "play/slice_estate.h"
#include "sim/entity.h"
#include "sim/command.h"
#include "world/city.h"

#include <cstring>

using namespace guild;
using namespace guild::play;

namespace {

// Seed one live building (g_objects) with a starting owner + a live buyer Person
// with starting cash, blanking the folded economy tables. Returns {objectId, buyerId}.
struct Seeded { i32 objectId; i32 buyerId; };

Seeded SeedEstateWorld(i16 startOwner, i16 buyerCash) {
    std::memset(world::g_goods, 0, sizeof(world::g_goods));
    std::memset(world::g_cities, 0, sizeof(world::g_cities));
    world::g_capDivisor = 0.0f;
    world::g_cityTotalMoney = 0.0f;
    world::g_cityTotalGoods = 0.0f;

    sim::ResetEntityArrays();
    std::memset(sim::g_objects, 0, sizeof(sim::g_objects));
    std::memset(sim::g_persons, 0, sizeof(sim::g_persons));
    std::memset(sim::g_personIds, 0, sizeof(sim::g_personIds));

    const i32 kObj = 8200;
    sim::ObjectRec& o = sim::g_objects[2];
    o.alive = 1;
    o.id    = kObj;
    std::memcpy(reinterpret_cast<u8*>(&o) + kEstateOwnerFieldOff, &startOwner, sizeof startOwner);
    i16 parent0 = 0;
    std::memcpy(reinterpret_cast<u8*>(&o) + kEstateParentFieldOff, &parent0, sizeof parent0);

    const i32 kBuyer = 5050;
    sim::Person& p = sim::g_persons[1];
    p.marker = 0;
    p.kind   = 5;
    p.id     = kBuyer;
    sim::g_personIds[1] = kBuyer;
    std::memcpy(reinterpret_cast<u8*>(&p) + kEstateCashFieldOff, &buyerCash, sizeof buyerCash);

    return {kObj, kBuyer};
}

} // namespace

// --- classifier golden: a BUY on a kind-4 building -> opcode-56 command. ---------
TEST(PlaySliceEstateUnit, ClassifyBuyCommandGolden) {
    Seeded s = SeedEstateWorld(/*startOwner=*/7, /*buyerCash=*/2000);

    EstateInteraction ei;
    ei.objectId = s.objectId;
    ei.buyerId  = s.buyerId;
    ei.newOwnerId = 42;
    ei.parentHandle = 9;
    ei.price = 800;
    ei.objectKind = 4;     // purchasable building
    ei.player = 0;

    EstateCommand c = ClassifyEstateInteraction(ei);
    CHECK(c.issued);
    CHECK_EQ((int)c.opcode, 56);             // VIBE_Command_QueueRequestQuad56
    CHECK_EQ(c.object, s.objectId);
    CHECK_EQ((int)c.newOwnerId, 42);
    CHECK_EQ((int)c.parentHandle, 9);
    CHECK_EQ(c.buyer, s.buyerId);
    CHECK_EQ(c.price, 800);
}

// --- non-purchases classify as not issued. --------------------------------------
TEST(PlaySliceEstateUnit, ClassifyRejectsNonPurchases) {
    Seeded s = SeedEstateWorld(/*startOwner=*/7, /*buyerCash=*/2000);

    EstateInteraction base;
    base.objectId = s.objectId; base.buyerId = s.buyerId; base.newOwnerId = 42;
    base.parentHandle = 9; base.price = 800; base.objectKind = 4; base.player = 0;

    // Not a purchasable building (kind != 4) -> no command (EvalBuyBuilding gate).
    { EstateInteraction ei = base; ei.objectKind = 2;
      CHECK(!ClassifyEstateInteraction(ei).issued); }
    // Zero price -> no command.
    { EstateInteraction ei = base; ei.price = 0;
      CHECK(!ClassifyEstateInteraction(ei).issued); }
}

// --- the apply: a buy reparents the object and debits the buyer's cash. ----------
TEST(PlaySliceEstateUnit, ApplyBuyReparentsAndDebitsCash) {
    Seeded s = SeedEstateWorld(/*startOwner=*/7, /*buyerCash=*/2000);
    SetEstateApplyHooks(nullptr);   // inert-default record mutation

    CHECK_EQ((int)ReadObjectOwner(s.objectId), 7);
    CHECK_EQ(ReadBuyerCash(s.buyerId), 2000);

    EstateInteraction ei;
    ei.objectId = s.objectId; ei.buyerId = s.buyerId; ei.newOwnerId = 42;
    ei.parentHandle = 9; ei.price = 800; ei.objectKind = 4; ei.player = 0;

    sim::CommandQueue q; q.Init(); q.set_standalone(true);
    EstateSliceResult r = RunEstateSlice(ei, /*econSeed=*/0x1234, q);

    CHECK(r.command.issued);
    CHECK(r.enqueued);
    CHECK(r.applied);
    // Ownership moved to the new owner; parent handle stamped.
    CHECK_EQ((int)r.ownerBefore, 7);
    CHECK_EQ((int)r.ownerAfter, 42);
    CHECK_EQ((int)r.parentAfter, 9);
    CHECK(r.ownerMoved());
    // Cash debited the price.
    CHECK_EQ(r.cashBefore - r.cashAfter, 800);
    CHECK(r.cashMoved());
}

// --- the slice is deterministic across reruns. ----------------------------------
TEST(PlaySliceEstateUnit, SliceDeterministic) {
    auto runOnce = [](EstateSliceResult& out) {
        Seeded s = SeedEstateWorld(/*startOwner=*/7, /*buyerCash=*/2000);
        EstateInteraction ei;
        ei.objectId = s.objectId; ei.buyerId = s.buyerId; ei.newOwnerId = 42;
        ei.parentHandle = 9; ei.price = 800; ei.objectKind = 4; ei.player = 0;
        sim::CommandQueue q; q.Init(); q.set_standalone(true);
        out = RunEstateSlice(ei, /*econSeed=*/0xBEEF, q);
    };

    EstateSliceResult a, b;
    runOnce(a);
    runOnce(b);

    CHECK(a.purchaseChangedWorld());   // the purchase moved the world hash
    CHECK(a.dayChangedWorld());        // the game-day moved it again
    CHECK(a.economyPasses > 0);

    CHECK_EQ(a.hashBefore, b.hashBefore);
    CHECK_EQ(a.hashAfterCommand, b.hashAfterCommand);
    CHECK_EQ(a.hashAfterDay, b.hashAfterDay);
    CHECK_EQ((int)a.ownerAfter, (int)b.ownerAfter);
    CHECK_EQ(a.cashAfter, b.cashAfter);
}

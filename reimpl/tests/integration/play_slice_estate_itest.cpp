// tests/integration/play_slice_estate_itest.cpp — INTEGRATION: the REAL-ESTATE (buy
// building) slice over a small real-format world. A BUY routes through the REAL
// sim::CommandQueue codec (the opcode-56 QueueRequestQuad56 ownership packet) and
// mutates real folded records:
//   * the bought object's owner id (object +39, written by SetObjectParent),
//   * the bought object's parent handle (object +37),
//   * the buyer's cash (Person +0x0A, debited the price by the EnqueueCmd15 leg).
// Asserts HashFullWorld differs pre/post the purchase AND the game-day, and the
// whole run is byte-identical on rerun (the determinism oracle).
#include "test.h"

#include "play/slice_estate.h"
#include "play/world_digest.h"
#include "sim/entity.h"
#include "sim/command.h"
#include "world/city.h"

#include <cstring>

using namespace guild;
using namespace guild::play;

namespace {

struct Seeded { i32 objectId; i32 buyerId; };

// Build a small real-format world: a few live buildings + a buyer Person, folded
// economy tables blanked for reproducibility. Returns the target object + buyer.
Seeded SeedRealFormatWorld(i16 startOwner, i16 buyerCash) {
    std::memset(world::g_goods, 0, sizeof(world::g_goods));
    std::memset(world::g_cities, 0, sizeof(world::g_cities));
    world::g_capDivisor = 0.0f;
    world::g_cityTotalMoney = 0.0f;
    world::g_cityTotalGoods = 0.0f;

    sim::ResetEntityArrays();
    std::memset(sim::g_objects, 0, sizeof(sim::g_objects));
    std::memset(sim::g_persons, 0, sizeof(sim::g_persons));
    std::memset(sim::g_personIds, 0, sizeof(sim::g_personIds));

    const i32 kObj = 9300;
    for (int i = 0; i < 4; ++i) {
        sim::ObjectRec& o = sim::g_objects[i];
        o.alive = 1;
        o.id    = 9300 + i;
        i16 owner = (i16)(startOwner + i);
        std::memcpy(reinterpret_cast<u8*>(&o) + kEstateOwnerFieldOff, &owner, sizeof owner);
        i16 parent0 = 0;
        std::memcpy(reinterpret_cast<u8*>(&o) + kEstateParentFieldOff, &parent0, sizeof parent0);
    }

    const i32 kBuyer = 5500;
    sim::Person& p = sim::g_persons[0];
    p.marker = 0; p.kind = 5; p.id = kBuyer;
    sim::g_personIds[0] = kBuyer;
    std::memcpy(reinterpret_cast<u8*>(&p) + kEstateCashFieldOff, &buyerCash, sizeof buyerCash);

    return {kObj, kBuyer};
}

EstateSliceResult RunBuy(i16 startOwner, i16 buyerCash, u32 econSeed) {
    Seeded s = SeedRealFormatWorld(startOwner, buyerCash);
    EstateInteraction ei;
    ei.objectId = s.objectId; ei.buyerId = s.buyerId; ei.newOwnerId = 77;
    ei.parentHandle = 12; ei.price = 1200; ei.objectKind = 4; ei.player = 0;
    sim::CommandQueue q; q.Init(); q.set_standalone(true);
    return RunEstateSlice(ei, econSeed, q);
}

} // namespace

// A buy moves the object's folded owner + the buyer's cash and steps the hashes.
TEST(PlaySliceEstateItest, BuyMutatesFoldedRecordsAndHashSteps) {
    SetEstateApplyHooks(nullptr);

    EstateSliceResult r = RunBuy(/*startOwner=*/10, /*buyerCash=*/5000, /*econSeed=*/0xC0FFEE);

    CHECK(r.command.issued);
    CHECK_EQ((int)r.command.opcode, 56);     // QueueRequestQuad56 ownership packet
    CHECK(r.enqueued);
    CHECK(r.applied);

    // Ownership transferred (real folded object +39); parent handle stamped (+37).
    CHECK_EQ((int)r.ownerBefore, 10);
    CHECK_EQ((int)r.ownerAfter, 77);
    CHECK_EQ((int)r.parentAfter, 12);
    CHECK(r.ownerMoved());

    // Buyer cash debited the price.
    CHECK_EQ(r.cashBefore - r.cashAfter, 1200);
    CHECK(r.cashMoved());

    // The purchase + the game-day each moved HashFullWorld.
    CHECK(r.purchaseChangedWorld());
    CHECK(r.dayChangedWorld());
    CHECK(r.economyPasses > 0);
}

// The full buy run is byte-identical on rerun (determinism).
TEST(PlaySliceEstateItest, RunDeterministicAcrossReruns) {
    EstateSliceResult a = RunBuy(/*startOwner=*/10, /*buyerCash=*/5000, /*econSeed=*/0x1357);
    EstateSliceResult b = RunBuy(/*startOwner=*/10, /*buyerCash=*/5000, /*econSeed=*/0x1357);

    CHECK_EQ(a.hashBefore, b.hashBefore);
    CHECK_EQ(a.hashAfterCommand, b.hashAfterCommand);
    CHECK_EQ(a.hashAfterDay, b.hashAfterDay);
    CHECK_EQ((int)a.ownerAfter, (int)b.ownerAfter);
    CHECK_EQ(a.cashAfter, b.cashAfter);

    // A different seed diverges the day trajectory.
    EstateSliceResult c = RunBuy(/*startOwner=*/10, /*buyerCash=*/5000, /*econSeed=*/0x2468);
    CHECK(c.hashAfterDay != a.hashAfterDay);
}

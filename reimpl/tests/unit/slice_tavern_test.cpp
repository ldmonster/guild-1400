// tests/unit/slice_tavern_test.cpp — the TAVERN / SOCIAL slice on a SYNTHETIC world
// (no assets). Proves:
//   * BuildTavernPacket produces the byte-exact op-83 "buy dunkle ecke" packet
//     (opcode 83, "buy " tag, player/location/target/price dwords) and gates on
//     affordability (CheckResourceAmount),
//   * IssueTavernClick applies the recruit (target relation +0x5C / superior +0x60
//     bound to the player, price debited from player cash +0x0A),
//   * the COMMAND step mutates the world; the GAME-DAY step mutates it further,
//   * the whole sequence is DETERMINISTIC across reruns (per-step hashes match).
#include "test.h"

#include "play/slice_tavern.h"
#include "play/world_digest.h"
#include "sim/entity.h"
#include "sim/person.h"
#include "sim/types.h"

#include <cstdio>
#include <cstring>

using namespace guild;
using namespace guild::play;

namespace {

TavernClick RecruitClick() {
    TavernClick c;
    c.action     = TavernAction::kRecruitThug;
    c.playerId   = 6000;   // guild-master/buyer @ slot 0 (seeded kind 10)
    c.targetId   = 6001;   // dark-corner NPC @ slot 1
    c.locationId = 4242;   // tavern/location (building) id
    c.price      = 30;     // affordable (buyer cash >= 500)
    return c;
}

u32 le32(const u8* p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

} // namespace

// ---------------------------------------------------------------------------
// The op-83 wire layout is byte-exact: opcode 83, "buy " tag, then the dwords.
// ---------------------------------------------------------------------------
TEST(SliceTavernUnit, PacketWireLayoutIsByteExact) {
    CHECK_EQ((unsigned)kTavernBuyTag, 0x62757920u);  // the v25[0] constant DarkCornerBuy writes
    // The dword is 0x62757920; stored little-endian its bytes spell ' ','y','u','b'
    // (the in-memory image), i.e. the ASCII chars of "buy " in reverse byte order.
    CHECK_EQ((kTavernBuyTag >> 0) & 0xFF, (u32)' ');
    CHECK_EQ((kTavernBuyTag >> 8) & 0xFF, (u32)'y');
    CHECK_EQ((kTavernBuyTag >> 16) & 0xFF, (u32)'u');
    CHECK_EQ((kTavernBuyTag >> 24) & 0xFF, (u32)'b');

    // Seed a buyer so BuildTavernPacket's affordability gate passes.
    TavernSliceResult r =
        RunTavernSliceSynthetic(/*seed=*/0x99, /*persons=*/4, RecruitClick(),
                                /*econSeed=*/0x1234);
    CHECK(r.seeded);

    TavernPacket pkt = BuildTavernPacket(RecruitClick());
    CHECK(pkt.built);
    CHECK_EQ((int)pkt.opcode, 83);

    u8 wire[36];
    pkt.encode(wire);
    CHECK_EQ((int)wire[0], 83);                       // opcode byte
    CHECK_EQ((unsigned)le32(&wire[16]), 0x62757920u); // "buy " tag at +16
    CHECK_EQ((int)le32(&wire[20]), RecruitClick().playerId);   // +20 player id
    CHECK_EQ((int)le32(&wire[24]), RecruitClick().locationId); // +24 location id
    CHECK_EQ((int)le32(&wire[28]), RecruitClick().targetId);   // +28 target NPC id
    CHECK_EQ((int)le32(&wire[32]), RecruitClick().price);      // +32 price
}

// ---------------------------------------------------------------------------
// Affordability gate: a price above the buyer's cash does NOT build the packet.
// ---------------------------------------------------------------------------
TEST(SliceTavernUnit, AffordabilityGate) {
    // seed a buyer (cash ~500) into the live arrays first.
    RunTavernSliceSynthetic(/*seed=*/1, /*persons=*/4, RecruitClick(),
                            /*econSeed=*/2);
    TavernClick tooDear = RecruitClick();
    tooDear.price = 100000;                  // unaffordable
    TavernPacket pkt = BuildTavernPacket(tooDear);
    CHECK(!pkt.built);
    CHECK_EQ((int)pkt.opcode, 0);
}

// ---------------------------------------------------------------------------
// IssueTavernClick: real apply binds the target + debits the buyer cash.
// ---------------------------------------------------------------------------
TEST(SliceTavernUnit, RecruitAppliesBindingAndDebit) {
    TavernSliceResult r =
        RunTavernSliceSynthetic(/*seed=*/0x55AA, /*persons=*/4, RecruitClick(),
                                /*econSeed=*/0x1234);
    std::printf("[tav-unit] issued=%d applied=%d opcode=%d relBefore=%d "
                "relAfter=%d supAfter=%d cashBefore=%d cashAfter=%d "
                "employmentAfter=%d econPasses=%d\n",
                (int)r.order.issued, (int)r.order.applied, (int)r.order.opcode,
                r.order.targetRelationBefore, r.order.targetRelationAfter,
                r.order.targetSuperiorAfter, r.order.buyerCashBefore,
                r.order.buyerCashAfter, r.order.employmentAfter, r.economyPasses);

    CHECK(r.seeded);
    CHECK(r.order.issued);
    CHECK(r.order.applied);
    CHECK_EQ((int)r.order.opcode, 83);
    // Target was unbound (-1) before, bound to the buyer after.
    CHECK_EQ(r.order.targetRelationBefore, -1);
    CHECK_EQ(r.order.targetRelationAfter, RecruitClick().playerId);
    CHECK_EQ(r.order.targetSuperiorAfter, RecruitClick().playerId);
    // The price was debited from the buyer's cash.
    CHECK_EQ(r.order.buyerCashAfter, r.order.buyerCashBefore - RecruitClick().price);
    // The command moved the folded world; the day moved it further.
    CHECK(r.commandChangedWorld());
    CHECK(r.worldChanged());
    CHECK(r.economyPasses > 0);
    CHECK(r.ok());
}

// ---------------------------------------------------------------------------
// The binding lands at +0x5C / +0x60 and the debit at +0x0A on the live records.
// ---------------------------------------------------------------------------
TEST(SliceTavernUnit, OffsetsAreFaithful) {
    using namespace guild::sim;
    RunTavernSliceSynthetic(/*seed=*/7, /*persons=*/4, RecruitClick(),
                            /*econSeed=*/9);
    // Re-issue on the seeded world and read the raw fields directly.
    IssueTavernClick(RecruitClick());
    Person* tgt = PersonFindRecordById(RecruitClick().targetId);
    CHECK(tgt != nullptr);
    if (tgt) {
        CHECK_EQ(PersonGetDword(tgt, kTvRelationOff), RecruitClick().playerId);
        CHECK_EQ((int)kTvRelationOff, 0x5C);
        CHECK_EQ((int)kTvSuperiorOff, 0x60);
        CHECK_EQ((int)kTvCashOff, 0x0A);
    }
}

// ---------------------------------------------------------------------------
// Step sequencing: seed -> command (mutates) -> day (mutates), deterministic.
// ---------------------------------------------------------------------------
TEST(SliceTavernUnit, StepSequencingDeterministic) {
    TavernStepHash a[3], b[3];
    int na = RunTavernStepsSynthetic(0xC0FFEE, 5, RecruitClick(), 0xBEEF, a, 3);
    int nb = RunTavernStepsSynthetic(0xC0FFEE, 5, RecruitClick(), 0xBEEF, b, 3);
    CHECK_EQ(na, 3);
    CHECK_EQ(nb, 3);
    // command + day each move the hash.
    CHECK(a[1].mutated);
    CHECK(a[2].mutated);
    // byte-identical across reruns.
    for (int i = 0; i < 3; ++i)
        CHECK_EQ((long long)a[i].hashAfter, (long long)b[i].hashAfter);
}

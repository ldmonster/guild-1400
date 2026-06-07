#include "test.h"

#include "sim/command_unit_orders.h"
#include "sim/command_apply3.h"
#include "sim/command.h"

#include <cstring>

// Integration: the real VIBE_Command_ExDispatchUnitOrder (command_apply3.cpp,
// opcode 0x55) is the dispatcher that, per order-code byte (+0x14), routes to one
// of the seven unit-order gestures — three of which are the leaves translated in
// command_unit_orders.cpp (order 3 conquer-flag, 5 pick-up, 7 select-sound). This
// test drives the REAL dispatcher (with the real CombatUnitField + local-player
// gate) to confirm the order codes are recognized for a live unit, then drives
// the matching leaves with the SAME packet fields to prove they compose: the
// dispatcher's order classification and the leaf's mutation agree.

using namespace guild;
using namespace guild::sim;

namespace {

// Build the order packet ExDispatchUnitOrder reads: player id @+0x10, order code
// byte @+0x14, unit id @+0x35.
CommandPacket MakeOrderPacket(i32 playerId, u8 orderCode, i32 unitId) {
    CommandPacket p{};
    std::memset(p.bytes, 0, sizeof(p.bytes));
    p.opcode() = kOp3DispatchUnitOrder; // 0x55
    p.put32(0x10, static_cast<u32>(playerId));
    p.bytes[0x14] = orderCode;
    p.put32(0x35, static_cast<u32>(unitId));
    return p;
}

UnitOrderRecord MakeRec(i32 unitId, bool active) {
    UnitOrderRecord r{}; r.unitId = unitId; r.active = active ? 1 : 0; return r;
}

} // namespace

TEST(SimUnitOrdersItest, DispatcherClassifiesThenLeafApplies) {
    ResetApply3State();
    ResetUnitOrderState();

    const i32 player = 0x55AA;
    const i32 unit   = 41;
    Apply3_SetLocalPlayerId(player);

    // Seed the SAME unit in BOTH the real combat field (for the dispatcher's
    // alive gate) and the leaf's modeled unit set.
    CombatUnit* cu = Apply3_Combat().Spawn(unit, 100, 0);
    CHECK(cu != nullptr);
    cu->actorPtr = 0xBEEF;

    UnitOrderActor ua{}; ua.unitId = unit; ua.actorPtr = 0xBEEF; ua.personType = 5;
    SeedUnit(ua);

    AckEntry ack{};

    // order 5 (pick-up) — dispatcher must accept it (returns 0, ack stamped).
    CommandPacket p5 = MakeOrderPacket(player, 5, unit);
    int rc = ExDispatchUnitOrder(p5, &ack);
    CHECK_EQ(rc, 0);
    CHECK_EQ(ack.status, 1);
    CHECK_EQ(Apply3_CharLog().lastOrderKind, 5);

    // The leaf, driven with the same unit, performs the stand-up + queue.
    CHECK(ExecPickupGroundItem(MakeRec(unit, true)));
    CHECK_EQ(UnitOrderLogRef().standUpCount, 1);
    CHECK_EQ(UnitOrderLogRef().lastStandUpActor, 0xBEEF);
    CHECK_EQ(UnitOrderLogRef().pickupActionCount, 1);
}

TEST(SimUnitOrdersItest, DispatcherGatesOnWrongPlayer) {
    ResetApply3State();
    ResetUnitOrderState();
    Apply3_SetLocalPlayerId(1000);

    AckEntry ack{}; ack.status = 9;
    // packet for a DIFFERENT player -> dispatcher returns 1 (rejected), no ack.
    CommandPacket p = MakeOrderPacket(2000, 7, 5);
    int rc = ExDispatchUnitOrder(p, &ack);
    CHECK_EQ(rc, 1);
    CHECK_EQ(ack.status, 9); // untouched
}

TEST(SimUnitOrdersItest, ConquerFlagOrderRoutesAndLeafAttaches) {
    ResetApply3State();
    ResetUnitOrderState();

    const i32 player = 7, unit = 12;
    Apply3_SetLocalPlayerId(player);
    CombatUnit* cu = Apply3_Combat().Spawn(unit, 100, 0);
    CHECK(cu != nullptr);
    cu->actorPtr = 0x30;

    UnitOrderActor ua{}; ua.unitId = unit; ua.personType = 6; SeedUnit(ua);

    AckEntry ack{};
    CommandPacket p3 = MakeOrderPacket(player, 3, unit);
    CHECK_EQ(ExDispatchUnitOrder(p3, &ack), 0);
    CHECK_EQ(Apply3_CharLog().lastOrderKind, 3);

    CHECK(ExecConquerFlag(MakeRec(unit, true)));
    CHECK_EQ(UnitOrderLogRef().lastBannerTextId, 3611);
    CHECK_EQ(UnitOrderLogRef().flagAttachCount, 1);
    CHECK_EQ(UnitOrderLogRef().lastFlagUnit, unit);
}

TEST(SimUnitOrdersItest, SelectSoundOrderRoutesAndLeafBanners) {
    ResetApply3State();
    ResetUnitOrderState();

    const i32 player = 3, unit = 0x2222;
    Apply3_SetLocalPlayerId(player);
    // order 7 in the dispatcher does NOT require an alive unit (no gate); it always
    // classifies. The leaf shows the banner for the named person.
    AckEntry ack{};
    CommandPacket p7 = MakeOrderPacket(player, 7, unit);
    CHECK_EQ(ExDispatchUnitOrder(p7, &ack), 0);
    CHECK_EQ(Apply3_CharLog().lastOrderKind, 7);

    CHECK(ExecUnitSelectSound(MakeRec(unit, true)));
    CHECK_EQ(UnitOrderLogRef().lastBannerTextId, 3610);
    CHECK_EQ(UnitOrderLogRef().lastBannerPerson, unit & 0xFFFF);
}

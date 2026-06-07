#include "test.h"

#include "sim/command_unit_orders.h"
#include "sim/command_apply3.h"
#include "sim/command.h"

#include <cstring>

// End-to-end: a full unit-order battle turn for one live unit. We drive the real
// dispatcher (ExDispatchUnitOrder) for each of the three heavy order codes, then
// run the matching translated leaf, then snap the unit's scene object to its
// order target via CharacterAttachToScene — the same sequence the dispatcher does
// (AttachToScene precedes orders 2/3/6 in the original). We verify the cumulative
// observable state across the whole flow.

using namespace guild;
using namespace guild::sim;

namespace {
CommandPacket OrderPacket(i32 player, u8 code, i32 unit) {
    CommandPacket p{};
    std::memset(p.bytes, 0, sizeof(p.bytes));
    p.opcode() = kOp3DispatchUnitOrder;
    p.put32(0x10, static_cast<u32>(player));
    p.bytes[0x14] = code;
    p.put32(0x35, static_cast<u32>(unit));
    return p;
}
UnitOrderRecord Rec(i32 unit) { UnitOrderRecord r{}; r.unitId = unit; r.active = 1; return r; }
} // namespace

TEST(SimUnitOrdersE2E, FullUnitOrderTurn) {
    ResetApply3State();
    ResetUnitOrderState();

    const i32 player = 0x101, unit = 77;
    Apply3_SetLocalPlayerId(player);

    CombatUnit* cu = Apply3_Combat().Spawn(unit, 100, 1);
    CHECK(cu != nullptr);
    cu->actorPtr = 0xCAFE;

    UnitOrderActor ua{}; ua.unitId = unit; ua.actorPtr = 0xCAFE; ua.personType = 5;
    ua.carriedObject = 0x4000; // the unit is carrying something it will drop
    UnitOrderActor* slot = SeedUnit(ua);

    AckEntry ack{};

    // 1) select-sound: pick the unit -> banner 3610.
    CommandPacket p7 = OrderPacket(player, 7, unit);
    CHECK_EQ(ExDispatchUnitOrder(p7, &ack), 0);
    CHECK(ExecUnitSelectSound(Rec(unit)));
    CHECK_EQ(UnitOrderLogRef().lastBannerTextId, 3610);

    // 2) snap actor to its move target (AttachToScene), then pick item off ground.
    float cur[3] = {10.0f, 0.0f, 10.0f};
    float tgt[3] = {200.0f, 0.0f, 10.0f}; // way out of the 8.0 tolerance
    CharacterAttachToScene(0x99, cur, cur, tgt, cur);
    CHECK_EQ(UnitOrderLogRef().scenePosUpdates, 1);
    CHECK_EQ(UnitOrderLogRef().sceneDirUpdates, 0); // dir unchanged -> no write

    CommandPacket p5 = OrderPacket(player, 5, unit);
    CHECK_EQ(ExDispatchUnitOrder(p5, &ack), 0);
    CHECK(ExecPickupGroundItem(Rec(unit)));
    CHECK_EQ(UnitOrderLogRef().standUpCount, 1);
    CHECK_EQ(UnitOrderLogRef().releaseCarriedCount, 1); // dropped the carried obj
    CHECK_EQ(slot->carriedObject, 0);
    CHECK_EQ(UnitOrderLogRef().pickupActionCount, 1);

    // 3) plant the conquer (flee) flag.
    CommandPacket p3 = OrderPacket(player, 3, unit);
    CHECK_EQ(ExDispatchUnitOrder(p3, &ack), 0);
    CHECK(ExecConquerFlag(Rec(unit)));
    CHECK_EQ(UnitOrderLogRef().lastBannerTextId, 3611);
    CHECK_EQ(UnitOrderLogRef().flagAttachCount, 1);
    CHECK(slot->flagMesh != 0);

    // Re-issuing the conquer order replaces the prior mesh (detach + reattach).
    i32 firstMesh = slot->flagMesh;
    CHECK(ExecConquerFlag(Rec(unit)));
    CHECK_EQ(UnitOrderLogRef().flagDetachCount, 1);
    CHECK_EQ(UnitOrderLogRef().flagAttachCount, 2);
    CHECK(slot->flagMesh != 0 && slot->flagMesh != firstMesh);

    // Cumulative dispatcher acks: every order stamped status 1 / slot 8.
    CHECK_EQ(ack.status, 1);
    CHECK_EQ(ack.slot, 8);
}

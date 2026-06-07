#include "sim/combat_packets.h"
#include "sim/command.h"
#include "test.h"

#include <vector>

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// combat_packets e2e — drive a full combat-order sequence through the
// Build*Packet front-ends into a live CommandQueue, then observe that the
// packets landed on the send ring in order with the correct opcodes, monotonic
// sequence counters, and reconstructible payloads. This mirrors the in-game flow
// where the player issues a march (move), a tile reposition, an attack, and a
// bare order in one turn, each producing an opcode-0x50 command packet.
// ---------------------------------------------------------------------------

namespace {

CombatOrderHandle handle(i32 key, i32 owner) {
    CombatOrderHandle h; h.slotKey = key; h.op80Owner = owner; return h;
}

// A "scene" that resolves the cross-cluster leaves deterministically: every
// world point projects to a fixed tile, the safest-tile search returns an offset
// tile, slots are always free, and the attacker carries a melee weapon (class 0).
CombatOrderContext scene(i32 tileX, i32 tileZ) {
    CombatOrderContext c;
    c.worldToTile = [tileX, tileZ](float, float, float, i32& tx, i32& tz) {
        tx = tileX; tz = tileZ; return true;
    };
    c.findSafestTile = [](i32, i32 inX, i32, i32 inZ, i32& ox, i32& oz) {
        ox = inX + 1; oz = inZ + 1; return true;
    };
    c.transformPoint = [](const float* p, float& wx, float& wy, float& wz) {
        wx = p[0]; wy = p[1]; wz = p[2];
    };
    c.findOrAllocSlot = [](i32, i32) { return true; };
    c.findObjectDef = [](i32, i16& cls, u8&) { cls = 0; return true; }; // melee
    c.findActiveTarget = [](i32, bool& has, bool& shots) { has = false; shots = true; };
    return c;
}

} // namespace

TEST(SimCombatPacketsE2E, FullOrderSequence) {
    CommandQueue q;
    q.Init();
    CommandQueue::Handler noop = [](CommandQueue&, CommandPacket&, AckEntry*) {};
    q.set_handler(80, noop);

    CombatOrderContext ctx = scene(/*tileX*/10, /*tileZ*/20);
    float bone[32] = {};
    float world[3] = {1.0f, 2.0f, 3.0f};

    // Turn: four orders for one squad token (owner 0x9000).
    i32 r1 = BuildMoveToPacket(q, handle(0x10, 0x9000), 0x1000, bone, ctx);
    i32 r2 = BuildTilePacket(q, handle(0x10, 0x9000), 0x1000, world, ctx);
    i32 r3 = BuildAttackPacket(q, handle(0x10, 0x9000), 0x1000, 0x2000, 0x55, true, ctx);
    i32 r4 = BuildSimplePacket(q, handle(0x10, 0x9000), 0x1000, ctx);

    // Four consecutive ring slots.
    CHECK_EQ(r1, 1);
    CHECK_EQ(r2, 2);
    CHECK_EQ(r3, 3);
    CHECK_EQ(r4, 4);
    CHECK_EQ(static_cast<int>(q.send_count()), 4);

    // Every packet is opcode 80, length 68, with monotonically increasing count.
    for (i32 r : {r1, r2, r3, r4}) {
        const CommandPacket& p = q.ring_slot(r);
        CHECK_EQ(static_cast<int>(p.opcode()), 80);
        CHECK_EQ(static_cast<int>(p.len()), 68);
        CHECK_EQ(static_cast<int>(p.cmd_id()), r);
        CHECK_EQ(static_cast<int>(p.count()), r);   // first send -> count == ring
        CHECK_EQ(p.get32(0x10), 0x9000u);           // op80Owner
        CHECK_EQ(p.get32(0x14), 0x1004u);           // target+4
    }

    // Per-order payload distinctions (the staging-block kind byte @+0x18).
    CHECK_EQ(static_cast<int>(q.ring_slot(r1).bytes[0x18]), 3);  // move
    CHECK_EQ(static_cast<int>(q.ring_slot(r2).bytes[0x18]), 7);  // tile
    CHECK_EQ(static_cast<int>(q.ring_slot(r3).bytes[0x18]), 2);  // attack
    CHECK_EQ(static_cast<int>(q.ring_slot(r4).bytes[0x18]), 8);  // simple

    // Move carries the projected tile (10,20).
    CHECK_EQ(static_cast<int>(q.ring_slot(r1).get32(0x24)), 10);
    CHECK_EQ(static_cast<int>(q.ring_slot(r1).get32(0x28)), 20);
    // Tile carries the safest tile. NOTE: BuildTilePacket projects only tileX
    // (v10) from the world point; the Z input to FindSafestTileInRange (v11) is
    // the original's UNINITIALISED stack slot, which our model treats as 0. So
    // the safest search sees (inX=10, inZ=0) and returns (10+1, 0+1).
    CHECK_EQ(static_cast<int>(q.ring_slot(r2).get32(0x24)), 11);
    CHECK_EQ(static_cast<int>(q.ring_slot(r2).get32(0x28)), 1);
    // Melee attack: field4 = attacker+4, secondary set.
    CHECK_EQ(q.ring_slot(r3).get32(0x24), 0x2004u);
    CHECK_EQ(static_cast<int>(q.ring_slot(r3).bytes[0x30]), 1);

    // The pending-send list threaded all four; flush them locally (standalone).
    CHECK(q.pending_head() != nullptr);
    int rc = q.FlushSendQueue();
    CHECK_EQ(rc, 0);
    // After a standalone flush the pending list is drained.
    CHECK(q.pending_head() == nullptr);
}

// A blocked order (ranged target with no shots) must NOT enqueue anything.
TEST(SimCombatPacketsE2E, BlockedRangedOrderEmitsNothing) {
    CommandQueue q;
    q.Init();
    CombatOrderContext ctx = scene(0, 0);
    ctx.findObjectDef = [](i32, i16& cls, u8&) { cls = 350; return true; };       // ranged
    ctx.findActiveTarget = [](i32, bool& has, bool& shots) { has = true; shots = false; };

    i32 r = BuildAttackPacket(q, handle(0x10, 0x9000), 0x1000, 0x2000, 0, false, ctx);
    CHECK_EQ(r, -1);
    CHECK_EQ(static_cast<int>(q.send_count()), 0);   // nothing reached the ring
    CHECK(q.pending_head() == nullptr);
}

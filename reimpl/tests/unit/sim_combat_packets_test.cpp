#include "sim/combat_packets.h"
#include "sim/command.h"
#include "test.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// combat_packets — golden-vector tests for the Build*Packet combat order
// front-ends and their shared RequestBuildOp80 sink.
//
// Wire layout of the resulting opcode-0x50 (80) packet (recovered from
// RequestBuildOp80 @0x495874 + the Build*Packet staging temps):
//   +0x00  u8   opcode = 80
//   +0x01  u16  len    = ComputePacketSize(0x50) = 68   (stamped by EnqueuePacket)
//   +0x03  u8   flag   = 0
//   +0x04  u32  cmdId  = ring slot (= (sendCount+1)&0x7FFF)
//   +0x08  u32  count  = monotonic send counter
//   +0x0C  u32  extra  = 0
//   +0x10  u32  a1     = op80Owner
//   +0x14  ...  44-byte staging block:
//            +0x14 (stage+0)  u32 targetId  = target+4
//            +0x18 (stage+4)  u8  kind      (2 attack / 3 move / 7 tile / 8 simple)
//            +0x24 (stage+16) u32 field4
//            +0x28 (stage+20) u32 field5
//            +0x2C (stage+24) u32 field6
//            +0x30 (stage+28) u8  secondary
//   +0x40  u32  localBattleCutTarget
// ---------------------------------------------------------------------------

namespace {

// Enqueue lands the first packet at ring index 1 (= (0+1)&0x7FFF).
CommandPacket& enq_slot(CommandQueue& q, i32 ring) { return q.ring_slot(ring); }

CombatOrderHandle make_handle(i32 key, i32 owner) {
    CombatOrderHandle h;
    h.slotKey = key;
    h.op80Owner = owner;
    return h;
}

// A context whose world-projection always succeeds with a fixed tile, a slot is
// always available, and (optionally) an object-def / active-target are supplied.
CombatOrderContext base_ctx(i32 tileX, i32 tileZ) {
    CombatOrderContext c;
    c.worldToTile = [tileX, tileZ](float, float, float, i32& tx, i32& tz) {
        tx = tileX; tz = tileZ; return true;
    };
    c.findOrAllocSlot = [](i32, i32) { return true; };
    return c;
}

} // namespace

// --- RequestBuildOp80 raw sink ----------------------------------------------
TEST(SimCombatPackets, Op80RawGolden) {
    CommandQueue q;
    q.Init();
    OrderStage st{};
    st.set_targetId(0x11223344);
    st.set_kind(7);
    st.set_field4(0x0A0B0C0D);
    st.set_field5(0x00000055);
    st.set_secondary(1);

    i32 ring = RequestBuildOp80(q, 0x77665544, st, 0x12345678);
    CHECK_EQ(ring, 1);

    const CommandPacket& p = enq_slot(q, ring);
    CHECK_EQ(static_cast<int>(p.opcode()), 80);
    CHECK_EQ(static_cast<int>(p.len()), 68);          // ComputePacketSize(0x50)
    CHECK_EQ(static_cast<int>(p.flag()), 0);
    CHECK_EQ(static_cast<int>(p.cmd_id()), 1);        // ring slot
    CHECK_EQ(static_cast<int>(p.count()), 1);         // send counter
    CHECK_EQ(static_cast<int>(p.extra()), 0);
    CHECK_EQ(p.get32(0x10), 0x77665544u);             // a1
    CHECK_EQ(p.get32(0x14), 0x11223344u);             // stage targetId
    CHECK_EQ(static_cast<int>(p.bytes[0x18]), 7);     // stage kind
    CHECK_EQ(p.get32(0x24), 0x0A0B0C0Du);             // stage field4
    CHECK_EQ(p.get32(0x28), 0x00000055u);             // stage field5
    CHECK_EQ(static_cast<int>(p.bytes[0x30]), 1);     // stage secondary
    CHECK_EQ(p.get32(0x40), 0x12345678u);             // cut target

    // Confirm the byte ComputePacketSize agrees out-of-band.
    CHECK_EQ(static_cast<int>(ComputePacketSizeFixed(80)), 68);
}

TEST(SimCombatPackets, Op80NoLocalBattleDefaultsMinusOne) {
    CommandQueue q;
    q.Init();
    OrderStage st{};
    i32 ring = RequestBuildOp80(q, 5, st);            // default cut target = -1
    const CommandPacket& p = enq_slot(q, ring);
    CHECK_EQ(p.get32(0x40), 0xFFFFFFFFu);
}

// --- BuildSimplePacket (kind 8) ---------------------------------------------
TEST(SimCombatPackets, BuildSimpleGolden) {
    CommandQueue q;
    q.Init();
    CombatOrderContext ctx;
    ctx.findOrAllocSlot = [](i32, i32) { return true; };

    i32 ring = BuildSimplePacket(q, make_handle(0x100, 0x4242), /*target*/0x2000, ctx);
    CHECK_EQ(ring, 1);
    const CommandPacket& p = enq_slot(q, ring);
    CHECK_EQ(static_cast<int>(p.opcode()), 80);
    CHECK_EQ(p.get32(0x10), 0x4242u);                 // op80Owner
    CHECK_EQ(p.get32(0x14), 0x2004u);                 // target+4
    CHECK_EQ(static_cast<int>(p.bytes[0x18]), 8);     // kind = simple
    // No tile / field4..6 set.
    CHECK_EQ(p.get32(0x24), 0u);
    CHECK_EQ(p.get32(0x28), 0u);
}

TEST(SimCombatPackets, BuildSimpleNoSlotMinusOne) {
    CommandQueue q;
    q.Init();
    CombatOrderContext ctx;
    ctx.findOrAllocSlot = [](i32, i32) { return false; };
    CHECK_EQ(BuildSimplePacket(q, make_handle(1, 1), 0x2000, ctx), -1);
}

// --- BuildMoveToPacket (kind 3) ---------------------------------------------
TEST(SimCombatPackets, BuildMoveGolden) {
    CommandQueue q;
    q.Init();
    CombatOrderContext ctx = base_ctx(/*tileX*/12, /*tileZ*/34);
    bool transformed = false;
    ctx.transformPoint = [&](const float*, float& wx, float& wy, float& wz) {
        transformed = true; wx = 1.0f; wy = 2.0f; wz = 3.0f;
    };
    float bone[32] = {};

    i32 ring = BuildMoveToPacket(q, make_handle(0x100, 0xABCD), 0x3000, bone, ctx);
    CHECK_EQ(ring, 1);
    CHECK(transformed);
    const CommandPacket& p = enq_slot(q, ring);
    CHECK_EQ(p.get32(0x10), 0xABCDu);
    CHECK_EQ(p.get32(0x14), 0x3004u);                 // target+4
    CHECK_EQ(static_cast<int>(p.bytes[0x18]), 3);     // kind = move
    CHECK_EQ(static_cast<int>(p.get32(0x24)), 12);    // field4 = tileX
    CHECK_EQ(static_cast<int>(p.get32(0x28)), 34);    // field5 = tileZ
}

TEST(SimCombatPackets, BuildMoveNullArgs) {
    CommandQueue q;
    q.Init();
    CombatOrderContext ctx = base_ctx(0, 0);
    float bone[4] = {};
    // null bonePoint
    CHECK_EQ(BuildMoveToPacket(q, make_handle(1, 1), 0x3000, nullptr, ctx), -1);
    // null target
    CHECK_EQ(BuildMoveToPacket(q, make_handle(1, 1), 0, bone, ctx), -1);
    // null handle (both zero)
    CHECK_EQ(BuildMoveToPacket(q, make_handle(0, 0), 0x3000, bone, ctx), -1);
}

TEST(SimCombatPackets, BuildMoveProjectionFails) {
    CommandQueue q;
    q.Init();
    CombatOrderContext ctx;
    ctx.findOrAllocSlot = [](i32, i32) { return true; };
    ctx.worldToTile = [](float, float, float, i32&, i32&) { return false; };
    float bone[4] = {};
    CHECK_EQ(BuildMoveToPacket(q, make_handle(1, 1), 0x3000, bone, ctx), -1);
}

// --- BuildTilePacket (kind 7) -----------------------------------------------
TEST(SimCombatPackets, BuildTileGolden) {
    CommandQueue q;
    q.Init();
    CombatOrderContext ctx;
    ctx.findOrAllocSlot = [](i32, i32) { return true; };
    ctx.worldToTile = [](float, float, float, i32& tx, i32& tz) {
        tx = 5; tz = 0; return true;
    };
    bool searched = false;
    ctx.findSafestTile = [&](i32 unit, i32 inX, i32 range, i32 /*inZ*/,
                             i32& outX, i32& outZ) {
        searched = true;
        CHECK_EQ(unit, 0x4000);
        CHECK_EQ(inX, 5);
        CHECK_EQ(range, 8);
        outX = 9; outZ = 7; return true;
    };
    float world[3] = {10.0f, 0.0f, 20.0f};

    i32 ring = BuildTilePacket(q, make_handle(0x100, 0xBEEF), 0x4000, world, ctx);
    CHECK_EQ(ring, 1);
    CHECK(searched);
    const CommandPacket& p = enq_slot(q, ring);
    CHECK_EQ(p.get32(0x10), 0xBEEFu);
    CHECK_EQ(p.get32(0x14), 0x4004u);                 // target+4
    CHECK_EQ(static_cast<int>(p.bytes[0x18]), 7);     // kind = tile
    CHECK_EQ(static_cast<int>(p.get32(0x24)), 9);     // field4 = safest X
    CHECK_EQ(static_cast<int>(p.get32(0x28)), 7);     // field5 = safest Z
}

TEST(SimCombatPackets, BuildTileSafestFails) {
    CommandQueue q;
    q.Init();
    CombatOrderContext ctx = base_ctx(5, 0);
    ctx.findSafestTile = [](i32, i32, i32, i32, i32&, i32&) { return false; };
    float world[3] = {};
    CHECK_EQ(BuildTilePacket(q, make_handle(1, 1), 0x4000, world, ctx), -1);
}

// --- BuildAttackPacket (kind 2) ---------------------------------------------

// Melee branch: no object-def -> only targetId/kind/field4 set, no tile.
TEST(SimCombatPackets, BuildAttackMeleeNoDef) {
    CommandQueue q;
    q.Init();
    CombatOrderContext ctx = base_ctx(/*tileX*/77, 0);
    // findObjectDef returns false -> hasDef=false -> melee branch.
    ctx.findObjectDef = [](i32, i16&, u8&) { return false; };

    i32 ring = BuildAttackPacket(q, make_handle(0x100, 0xC0DE),
                                 /*target*/0x5000, /*attacker*/0x6000,
                                 /*a5*/0x99, /*secondary*/false, ctx);
    CHECK_EQ(ring, 1);
    const CommandPacket& p = enq_slot(q, ring);
    CHECK_EQ(p.get32(0x10), 0xC0DEu);
    CHECK_EQ(p.get32(0x14), 0x5004u);                 // target+4
    CHECK_EQ(static_cast<int>(p.bytes[0x18]), 2);     // kind = attack
    CHECK_EQ(p.get32(0x24), 0x6004u);                 // field4 = attacker+4
    CHECK_EQ(p.get32(0x28), 0u);                      // field5 NOT set (no tile)
    CHECK_EQ(p.get32(0x2C), 0u);                      // field6 NOT set
    CHECK_EQ(static_cast<int>(p.bytes[0x30]), 0);     // secondary off
}

// Melee class 340 also takes the melee branch even with a def present.
TEST(SimCombatPackets, BuildAttackMeleeClass340WithSecondary) {
    CommandQueue q;
    q.Init();
    CombatOrderContext ctx = base_ctx(77, 0);
    ctx.findObjectDef = [](i32, i16& cls, u8&) { cls = 340; return true; };

    i32 ring = BuildAttackPacket(q, make_handle(0x100, 0xC0DE),
                                 0x5000, 0x6000, 0x99,
                                 /*secondary*/true, ctx);
    const CommandPacket& p = enq_slot(q, ring);
    CHECK_EQ(static_cast<int>(p.bytes[0x18]), 2);
    CHECK_EQ(p.get32(0x24), 0x6004u);                 // attacker+4
    CHECK_EQ(static_cast<int>(p.bytes[0x30]), 1);     // secondary set
}

// Ranged class 350: tile + a5 land in field5/field6.
TEST(SimCombatPackets, BuildAttackRanged350) {
    CommandQueue q;
    q.Init();
    CombatOrderContext ctx = base_ctx(/*tileX*/42, 0);
    ctx.findObjectDef = [](i32, i16& cls, u8&) { cls = 350; return true; };
    ctx.findActiveTarget = [](i32, bool& has, bool& shots) { has = false; shots = true; };

    i32 ring = BuildAttackPacket(q, make_handle(0x100, 0xC0DE),
                                 0x5000, 0x6000, /*a5*/0xAB, false, ctx);
    const CommandPacket& p = enq_slot(q, ring);
    CHECK_EQ(static_cast<int>(p.bytes[0x18]), 2);     // attack
    CHECK_EQ(p.get32(0x14), 0x5004u);                 // target+4
    CHECK_EQ(p.get32(0x24), 0x6004u);                 // field4 = attacker+4
    CHECK_EQ(static_cast<int>(p.get32(0x28)), 42);    // field5 = tileX
    CHECK_EQ(static_cast<int>(p.get32(0x2C)), 0xAB);  // field6 = a5
}

// Ranged class 372: field4=0, tile in field5, a5 in field6.
TEST(SimCombatPackets, BuildAttackRanged372) {
    CommandQueue q;
    q.Init();
    CombatOrderContext ctx = base_ctx(/*tileX*/13, 0);
    ctx.findObjectDef = [](i32, i16& cls, u8&) { cls = 372; return true; };

    i32 ring = BuildAttackPacket(q, make_handle(0x100, 0x1),
                                 0x5000, 0x6000, /*a5*/7, false, ctx);
    const CommandPacket& p = enq_slot(q, ring);
    CHECK_EQ(p.get32(0x24), 0u);                      // field4 = 0
    CHECK_EQ(static_cast<int>(p.get32(0x28)), 13);    // field5 = tileX
    CHECK_EQ(static_cast<int>(p.get32(0x2C)), 7);     // field6 = a5
}

// Ranged class 374: same fields, distinct write order (observationally equal).
TEST(SimCombatPackets, BuildAttackRanged374) {
    CommandQueue q;
    q.Init();
    CombatOrderContext ctx = base_ctx(/*tileX*/21, 0);
    ctx.findObjectDef = [](i32, i16& cls, u8&) { cls = 374; return true; };

    i32 ring = BuildAttackPacket(q, make_handle(0x100, 0x1),
                                 0x5000, 0x6000, /*a5*/3, false, ctx);
    const CommandPacket& p = enq_slot(q, ring);
    CHECK_EQ(static_cast<int>(p.bytes[0x18]), 2);
    CHECK_EQ(p.get32(0x24), 0u);                      // field4 = 0
    CHECK_EQ(static_cast<int>(p.get32(0x28)), 21);    // field5 = tileX
    CHECK_EQ(static_cast<int>(p.get32(0x2C)), 3);     // field6 = a5
}

// Ranged "no shots" gate: hasTarget && !hasShots -> banner -> return -1.
TEST(SimCombatPackets, BuildAttackRangedNoShotsGate) {
    CommandQueue q;
    q.Init();
    CombatOrderContext ctx = base_ctx(0, 0);
    ctx.findActiveTarget = [](i32, bool& has, bool& shots) { has = true; shots = false; };

    for (i16 cls : {350, 352, 372, 374}) {
        CombatOrderContext c = ctx;
        c.findObjectDef = [cls](i32, i16& wc, u8&) { wc = cls; return true; };
        CHECK_EQ(BuildAttackPacket(q, make_handle(1, 1), 0x5000, 0x6000, 0, false, c), -1);
    }
}

// Early-outs: null target, null attacker, projection fail, no slot.
TEST(SimCombatPackets, BuildAttackEarlyOuts) {
    CommandQueue q;
    q.Init();
    CombatOrderContext ctx = base_ctx(0, 0);
    ctx.findObjectDef = [](i32, i16&, u8&) { return false; };

    CHECK_EQ(BuildAttackPacket(q, make_handle(1, 1), /*target*/0, 0x6000, 0, false, ctx), -1);
    CHECK_EQ(BuildAttackPacket(q, make_handle(1, 1), 0x5000, /*attacker*/0, 0, false, ctx), -1);

    CombatOrderContext noproj = ctx;
    noproj.worldToTile = [](float, float, float, i32&, i32&) { return false; };
    CHECK_EQ(BuildAttackPacket(q, make_handle(1, 1), 0x5000, 0x6000, 0, false, noproj), -1);

    CombatOrderContext noslot = ctx;
    noslot.findOrAllocSlot = [](i32, i32) { return false; };
    CHECK_EQ(BuildAttackPacket(q, make_handle(1, 1), 0x5000, 0x6000, 0, false, noslot), -1);
}

// --- Wave-12 hardening: staging/packet bounds ------------------------------
// The 44-byte OrderStage buffer must absorb every field accessor at its max
// offset (set_secondary writes byte 28; the highest put32 is field6 at 24..27)
// without writing past the buffer; RequestBuildOp80 then copies the full 44
// bytes into the 153-byte packet at +0x14 (ending at +0x40, before the cut
// target). Drive all fields at their extremes and confirm the wire image.
TEST(SimCombatPackets, OrderStageAllFieldsMaxNoOverflow) {
    OrderStage st{};
    st.set_targetId(static_cast<i32>(0xFFFFFFFF));
    st.set_kind(0xFF);
    st.set_field4(static_cast<i32>(0x7FFFFFFF));
    st.set_field5(static_cast<i32>(0x80000000));
    st.set_field6(static_cast<i32>(0xFFFFFFFF));
    st.set_secondary(0xFF);
    // All writes land inside the 44-byte buffer (highest touched byte is +28).
    CHECK_EQ(st.get32(0), static_cast<i32>(0xFFFFFFFF));
    CHECK_EQ(static_cast<int>(st.bytes[4]), 0xFF);
    CHECK_EQ(st.get32(16), static_cast<i32>(0x7FFFFFFF));
    CHECK_EQ(st.get32(20), static_cast<i32>(0x80000000));
    CHECK_EQ(st.get32(24), static_cast<i32>(0xFFFFFFFF));
    CHECK_EQ(static_cast<int>(st.bytes[28]), 0xFF);
    // Bytes above +28 stay zero (no spill).
    for (int i = 29; i < 44; ++i)
        CHECK_EQ(static_cast<int>(st.bytes[i]), 0);

    CommandQueue q;
    q.Init();
    i32 ring = RequestBuildOp80(q, static_cast<i32>(0xFFFFFFFF), st,
                                static_cast<i32>(0x7FFFFFFF));
    CHECK_EQ(ring, 1);
    const CommandPacket& p = enq_slot(q, ring);
    // staging copied to +0x14..+0x3F; the cut target sits at +0x40 untouched by
    // the staging copy.
    CHECK_EQ(p.get32(0x14), 0xFFFFFFFFu);          // targetId
    CHECK_EQ(static_cast<int>(p.bytes[0x18]), 0xFF); // kind
    CHECK_EQ(p.get32(0x40), 0x7FFFFFFFu);          // cut target
}

// A target id at the high boundary: target+4 must wrap as the original's 32-bit
// add (no UB / no OOB), and the simple builder still produces a valid packet.
TEST(SimCombatPackets, BuildSimpleHighTargetWraps) {
    CommandQueue q;
    q.Init();
    CombatOrderContext ctx;
    ctx.findOrAllocSlot = [](i32, i32) { return true; };
    // target = 0x7FFFFFFD -> target+4 wraps to 0x80000001 (well-defined here:
    // the field is computed in i32 and stored via put32's unsigned bytes).
    i32 ring = BuildSimplePacket(q, make_handle(0x1, 0x2),
                                 static_cast<i32>(0x7FFFFFFD), ctx);
    CHECK_EQ(ring, 1);
    const CommandPacket& p = enq_slot(q, ring);
    CHECK_EQ(p.get32(0x14), 0x80000001u);
    CHECK_EQ(static_cast<int>(p.bytes[0x18]), 8);
}

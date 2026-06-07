#include "test.h"

// Unit tests for command_apply12 — the last VIBE_Command_* leaves: the label
// order builders (BuildLabeledMoveCommand @0x488d4c, BuildConquerCommand
// @0x488df4) and the cursor-click router (IssueOnObject @0x488ff0). Golden
// vectors derive from the recovered slot/staging field offsets.
#include "sim/command_apply12.h"
#include "sim/combat_packets.h"
#include "sim/command.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {
// A CombatOrderContext whose tile projection succeeds with fixed outputs and whose
// slot alloc always succeeds. transformPoint is identity (passes the world point).
CombatOrderContext MakeCtx(i32 tx, i32 tz, bool tileOk = true, bool slotOk = true) {
    CombatOrderContext c{};
    c.transformPoint = [](const float* p, float& wx, float& wy, float& wz) {
        if (p) { wx = p[0]; wy = p[1]; wz = p[2]; }
    };
    c.worldToTile = [tx, tz, tileOk](float, float, float, i32& ox, i32& oz) {
        if (!tileOk) return false;
        ox = tx; oz = tz; return true;
    };
    c.findOrAllocSlot = [slotOk](i32, i32) { return slotOk; };
    return c;
}
} // namespace

// --- CopyStrideLabel -------------------------------------------------------

TEST(CommandApply12, StrideCopyPlainString) {
    u8 dst[16];
    std::memset(dst, 0xAA, sizeof dst);
    u32 n = CopyStrideLabel(dst, sizeof dst, "WARE");
    // W A R E \0 — pair0=(W,A) pair1=(R,E) pair2=(\0)->break.
    CHECK_EQ(n, 5u);
    CHECK_EQ(dst[0], (u8)'W');
    CHECK_EQ(dst[1], (u8)'A');
    CHECK_EQ(dst[2], (u8)'R');
    CHECK_EQ(dst[3], (u8)'E');
    CHECK_EQ(dst[4], 0u);
    CHECK_EQ(dst[5], 0xAAu);   // untouched
}

TEST(CommandApply12, StrideCopyOddLengthBreaksOnFirstByte) {
    u8 dst[16];
    std::memset(dst, 0xAA, sizeof dst);
    // "AB" -> A,B then pair1 first byte is \0 -> write \0, break (3 bytes).
    u32 n = CopyStrideLabel(dst, sizeof dst, "AB");
    CHECK_EQ(n, 3u);
    CHECK_EQ(dst[0], (u8)'A');
    CHECK_EQ(dst[1], (u8)'B');
    CHECK_EQ(dst[2], 0u);
}

TEST(CommandApply12, StrideCopyRespectsCap) {
    u8 dst[4];
    u32 n = CopyStrideLabel(dst, sizeof dst, "ABCDEFGH");
    CHECK_EQ(n, 4u);            // never exceeds the cap
    CHECK_EQ(dst[0], (u8)'A');
    CHECK_EQ(dst[3], (u8)'D');
}

TEST(CommandApply12, StrideCopyNullArgs) {
    u8 dst[4];
    CHECK_EQ(CopyStrideLabel(dst, sizeof dst, nullptr), 0u);
    CHECK_EQ(CopyStrideLabel(nullptr, 4, "X"), 0u);
    CHECK_EQ(CopyStrideLabel(dst, 0, "X"), 0u);
}

// --- BuildLabeledMoveCommand (direct slot write, returns 1/0) --------------

TEST(CommandApply12, LabeledMoveWritesSlotFields) {
    CombatOrderContext ctx = MakeCtx(/*tx=*/100, /*tz=*/200);
    CombatOrderHandle h{}; h.slotKey = 7; h.op80Owner = 0;
    float bone[3] = {1.0f, 2.0f, 3.0f};
    OrderStage slot{};

    i32 r = BuildLabeledMoveCommand(h, /*target=*/0x40, bone, "sp_CONQUER", slot, ctx);
    CHECK_EQ(r, 1);
    CHECK_EQ(slot.get32(kSlotTargetId), 0x44);            // target + 4
    CHECK_EQ((int)slot.bytes[kSlotKind], (int)kOrderKindLabeledMove); // 4
    CHECK_EQ((int)slot.bytes[kSlotPreClear], 0);
    CHECK_EQ(slot.get32(kSlotTileX), 100);
    CHECK_EQ(slot.get32(kSlotTileZ), 200);
    // Label landed at +0x10.
    CHECK_EQ(slot.bytes[kSlotLabel + 0], (u8)'s');
    CHECK_EQ(slot.bytes[kSlotLabel + 9], (u8)'R');        // "sp_CONQUER"[9]
    CHECK_EQ(slot.bytes[kSlotLabel + 10], 0u);
}

TEST(CommandApply12, LabeledMoveFailsOnProjection) {
    CombatOrderContext ctx = MakeCtx(0, 0, /*tileOk=*/false);
    CombatOrderHandle h{}; h.slotKey = 1;
    float bone[3] = {0, 0, 0};
    OrderStage slot{};
    CHECK_EQ(BuildLabeledMoveCommand(h, 0x10, bone, "X", slot, ctx), 0);
}

TEST(CommandApply12, LabeledMoveFailsOnNoSlot) {
    CombatOrderContext ctx = MakeCtx(5, 6, true, /*slotOk=*/false);
    CombatOrderHandle h{}; h.slotKey = 1;
    float bone[3] = {0, 0, 0};
    OrderStage slot{};
    CHECK_EQ(BuildLabeledMoveCommand(h, 0x10, bone, "X", slot, ctx), 0);
}

// --- BuildConquerCommand (staging + RequestBuildOp80, returns slot/-1) -----

TEST(CommandApply12, ConquerEnqueuesPacket) {
    CommandQueue q; q.Init(); q.set_standalone(false);
    CombatOrderContext ctx = MakeCtx(/*tx=*/0x11, /*tz=*/0x22);
    CombatOrderHandle h{}; h.slotKey = 3; h.op80Owner = 0x99;
    float bone[3] = {1, 1, 1};

    i32 slotIdx = BuildConquerCommand(q, h, /*target=*/0x80, bone, "WARE",
                                      /*targetBusyFlag=*/0, ctx);
    CHECK_EQ(slotIdx, 1);
    CHECK_EQ(q.send_count(), 1u);

    CommandPacket& p = q.ring_slot(1);
    CHECK_EQ((int)p.opcode(), 80);
    CHECK_EQ((int)p.len(), (int)ComputePacketSize(p));
    // Owner landed at +0x10 of the packet.
    CHECK_EQ(p.get32(0x10), 0x99u);
    // Staging copied to packet +0x14. Read the staging fields back.
    const u8* stg = p.bytes + 0x14;
    CHECK_EQ((int)stg[kSlotKind], (int)kOrderKindConquer);   // 6
    CHECK_EQ((int)stg[kConquerTileXByte], 0x11);
    CHECK_EQ((int)stg[kConquerTileZByte], 0x22);
    CHECK_EQ((int)stg[kSlotLabel + 0], (int)'W');
    CHECK_EQ((int)stg[kSlotLabel + 3], (int)'E');
    // target id dword at staging +0
    u32 tid = (u32)stg[0] | ((u32)stg[1] << 8) | ((u32)stg[2] << 16) | ((u32)stg[3] << 24);
    CHECK_EQ(tid, 0x84u);   // target + 4
}

TEST(CommandApply12, ConquerRejectedWhenTargetBusy) {
    CommandQueue q; q.Init(); q.set_standalone(false);
    CombatOrderContext ctx = MakeCtx(1, 2);
    CombatOrderHandle h{}; h.slotKey = 1; h.op80Owner = 0;
    float bone[3] = {0, 0, 0};
    // *(target+0x1AC) != 0 -> early -1, nothing enqueued.
    CHECK_EQ(BuildConquerCommand(q, h, 0x40, bone, "WARE", /*busy=*/1, ctx), -1);
    CHECK_EQ(q.send_count(), 0u);
}

TEST(CommandApply12, ConquerRejectedOnProjectionFail) {
    CommandQueue q; q.Init(); q.set_standalone(false);
    CombatOrderContext ctx = MakeCtx(0, 0, /*tileOk=*/false);
    CombatOrderHandle h{}; h.slotKey = 1;
    float bone[3] = {0, 0, 0};
    CHECK_EQ(BuildConquerCommand(q, h, 0x40, bone, "WARE", 0, ctx), -1);
    CHECK_EQ(q.send_count(), 0u);
}

// --- IssueOnObject ---------------------------------------------------------

TEST(CommandApply12, IssueDisarmedReturnsZero) {
    CommandQueue q; q.Init();
    CombatOrderContext ctx{};
    CombatOrderHandle h{};
    OrderStage out{};
    SetIssueOnObjectHooks(nullptr);   // inert defaults: orderArmed == false
    CHECK_EQ((int)IssueOnObject(q, h, 0x40, ctx, out), 0);
    CHECK_EQ(q.send_count(), 0u);
}

TEST(CommandApply12, IssueGroundRaycastMove) {
    CommandQueue q; q.Init(); q.set_standalone(false);
    CombatOrderContext ctx = MakeCtx(0, 0, true);   // findOrAllocSlot ok
    CombatOrderHandle h{}; h.op80Owner = 0x55;
    OrderStage out{};

    IssueOnObjectHooks hk{};
    hk.orderArmed    = [] { return true; };
    hk.pickedObject  = [] { return 0; };            // no object -> ground branch
    hk.raycastGround = [](i32& x, i32& z) { x = 0x33; z = 0x44; return true; };
    SetIssueOnObjectHooks(&hk);

    char r = IssueOnObject(q, h, /*target=*/0x60, ctx, out);
    CHECK_EQ((int)r, 1);
    CHECK_EQ(q.send_count(), 1u);
    CommandPacket& p = q.ring_slot(1);
    CHECK_EQ((int)p.opcode(), 80);
    CHECK_EQ(p.get32(0x10), 0x55u);                 // owner
    const u8* stg = p.bytes + 0x14;
    CHECK_EQ((int)stg[kSlotKind], (int)kOrderKindGroundMove);  // 1
    // field4 @ +0x10, field5 @ +0x14
    u32 f4 = (u32)stg[0x10] | ((u32)stg[0x11] << 8) | ((u32)stg[0x12] << 16) | ((u32)stg[0x13] << 24);
    u32 f5 = (u32)stg[0x14] | ((u32)stg[0x15] << 8) | ((u32)stg[0x16] << 16) | ((u32)stg[0x17] << 24);
    CHECK_EQ(f4, 0x33u);
    CHECK_EQ(f5, 0x44u);
    SetIssueOnObjectHooks(nullptr);
}

TEST(CommandApply12, IssueGroundRaycastMissBanner) {
    CommandQueue q; q.Init(); q.set_standalone(false);
    CombatOrderContext ctx{};
    CombatOrderHandle h{};
    OrderStage out{};

    int banners = 0;
    IssueOnObjectHooks hk{};
    hk.orderArmed    = [] { return true; };
    hk.pickedObject  = [] { return 0; };
    hk.raycastGround = [](i32&, i32&) { return false; };
    hk.rejectBanner  = [&banners] { ++banners; };
    SetIssueOnObjectHooks(&hk);

    CHECK_EQ((int)IssueOnObject(q, h, 0x60, ctx, out), 0);
    CHECK_EQ(banners, 1);
    CHECK_EQ(q.send_count(), 0u);
    SetIssueOnObjectHooks(nullptr);
}

TEST(CommandApply12, IssueWareLabelConquers) {
    CommandQueue q; q.Init(); q.set_standalone(false);
    CombatOrderContext ctx = MakeCtx(0x05, 0x06);
    CombatOrderHandle h{}; h.op80Owner = 0x11;
    OrderStage out{};

    IssueOnObjectHooks hk{};
    hk.orderArmed   = [] { return true; };
    hk.pickedObject = [] { return 0x1000; };   // a pick exists
    hk.pickedIsUnit = [](i32) { return false; }; // it's a label, not a unit
    hk.pickedLabel  = [] { return "WARE_GOLD"; };
    SetIssueOnObjectHooks(&hk);

    char r = IssueOnObject(q, h, /*target=*/0x70, ctx, out);
    CHECK_EQ((int)r, 1);
    CHECK_EQ(q.send_count(), 1u);              // conquer enqueued one packet
    const u8* stg = q.ring_slot(1).bytes + 0x14;
    CHECK_EQ((int)stg[kSlotKind], (int)kOrderKindConquer);
    SetIssueOnObjectHooks(nullptr);
}

TEST(CommandApply12, IssueConquerLabelLabeledMove) {
    CommandQueue q; q.Init(); q.set_standalone(false);
    CombatOrderContext ctx = MakeCtx(0x07, 0x08);
    CombatOrderHandle h{}; h.slotKey = 2;
    OrderStage out{};

    IssueOnObjectHooks hk{};
    hk.orderArmed   = [] { return true; };
    hk.pickedObject = [] { return 0x1000; };
    hk.pickedIsUnit = [](i32) { return false; };
    hk.pickedLabel  = [] { return "sp_CONQUER"; };  // != escape, IS conquer prefix
    SetIssueOnObjectHooks(&hk);

    // sp_CONQUER prefix routes to BuildLabeledMoveCommand (direct slot, returns 1).
    char r = IssueOnObject(q, h, /*target=*/0x70, ctx, out);
    CHECK_EQ((int)r, 1);
    CHECK_EQ(q.send_count(), 0u);             // labelled move does NOT enqueue
    CHECK_EQ((int)out.bytes[kSlotKind], (int)kOrderKindLabeledMove);
    CHECK_EQ(out.get32(kSlotTileX), 0x07);
    SetIssueOnObjectHooks(nullptr);
}

TEST(CommandApply12, IssueUnitAttack) {
    CommandQueue q; q.Init(); q.set_standalone(false);
    // Attack path needs worldToTile to succeed (BuildAttackPacket projects).
    CombatOrderContext ctx = MakeCtx(0x09, 0x0A);
    ctx.findObjectDef = [](i32, i16& wc, u8&) { wc = 0; return true; }; // melee branch
    CombatOrderHandle h{}; h.op80Owner = 0x22;
    OrderStage out{};

    IssueOnObjectHooks hk{};
    hk.orderArmed      = [] { return true; };
    hk.pickedObject    = [] { return 0x2000; };
    hk.pickedIsUnit    = [](i32) { return true; };   // a battle unit
    hk.pickedUnitOwner = [](i32) { return 0x3000; };
    hk.attackAllowed   = [](i32, i32) { return true; };
    SetIssueOnObjectHooks(&hk);

    char r = IssueOnObject(q, h, /*target=*/0x80, ctx, out);
    CHECK_EQ((int)r, 1);
    CHECK_EQ(q.send_count(), 1u);
    CHECK_EQ((int)(q.ring_slot(1).bytes + 0x14)[kSlotKind], (int)kOrderPacketAttack); // 2
    SetIssueOnObjectHooks(nullptr);
}

TEST(CommandApply12, IssueUnitAttackDisallowed) {
    CommandQueue q; q.Init(); q.set_standalone(false);
    CombatOrderContext ctx = MakeCtx(0, 0);
    CombatOrderHandle h{};
    OrderStage out{};

    IssueOnObjectHooks hk{};
    hk.orderArmed      = [] { return true; };
    hk.pickedObject    = [] { return 0x2000; };
    hk.pickedIsUnit    = [](i32) { return true; };
    hk.pickedUnitOwner = [](i32) { return 0x3000; };
    hk.attackAllowed   = [](i32, i32) { return false; };   // same team, no override
    SetIssueOnObjectHooks(&hk);

    CHECK_EQ((int)IssueOnObject(q, h, 0x80, ctx, out), 0);
    CHECK_EQ(q.send_count(), 0u);
    SetIssueOnObjectHooks(nullptr);
}

#include "test.h"

// E2E: a small "player clicks to issue orders" flow exercising the cursor router
// across all of command_apply12's order kinds in sequence against one queue.
//   click on empty ground  -> ground move  (enqueued, kind 1)
//   click with a WARE label -> conquer      (enqueued, kind 6)
//   click with sp_CONQUER   -> labelled move (direct slot, NOT enqueued, kind 4)
//   click on an enemy unit  -> attack        (enqueued, kind 2)
// We assert the send ring grows only for the enqueuing branches and that each
// stamped packet carries the right order kind.
#include "sim/command_apply12.h"
#include "sim/combat_packets.h"
#include "sim/command.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {
CombatOrderContext MakeCtx(i32 tx, i32 tz) {
    CombatOrderContext c{};
    c.worldToTile     = [tx, tz](float, float, float, i32& ox, i32& oz) { ox = tx; oz = tz; return true; };
    c.findOrAllocSlot = [](i32, i32) { return true; };
    c.findObjectDef   = [](i32, i16& wc, u8&) { wc = 0; return true; }; // melee attack branch
    return c;
}
} // namespace

TEST(CommandApply12E2E, CursorOrderFlow) {
    CommandQueue q; q.Init();
    q.set_standalone(false);
    CombatOrderContext ctx = MakeCtx(/*tx=*/0x10, /*tz=*/0x20);
    CombatOrderHandle h{}; h.op80Owner = 0x1234; h.slotKey = 9;
    OrderStage out{};

    IssueOnObjectHooks hk{};
    hk.orderArmed = [] { return true; };

    // --- 1. Empty ground click -> ground move (enqueued) ---
    hk.pickedObject  = [] { return 0; };
    hk.raycastGround = [](i32& x, i32& z) { x = 0x77; z = 0x88; return true; };
    SetIssueOnObjectHooks(&hk);
    CHECK_EQ((int)IssueOnObject(q, h, 0x40, ctx, out), 1);
    CHECK_EQ(q.send_count(), 1u);
    CHECK_EQ((int)(q.ring_slot(1).bytes + 0x14)[kSlotKind], (int)kOrderKindGroundMove);

    // --- 2. WARE label -> conquer (enqueued) ---
    hk.pickedObject = [] { return 0x6000; };
    hk.pickedIsUnit = [](i32) { return false; };
    hk.pickedLabel  = [] { return "WARE"; };
    SetIssueOnObjectHooks(&hk);
    CHECK_EQ((int)IssueOnObject(q, h, 0x40, ctx, out), 1);
    CHECK_EQ(q.send_count(), 2u);
    CHECK_EQ((int)(q.ring_slot(2).bytes + 0x14)[kSlotKind], (int)kOrderKindConquer);

    // --- 3. sp_CONQUER label -> labelled move (direct slot, NOT enqueued) ---
    hk.pickedLabel = [] { return "sp_CONQUER"; };
    SetIssueOnObjectHooks(&hk);
    CHECK_EQ((int)IssueOnObject(q, h, 0x40, ctx, out), 1);
    CHECK_EQ(q.send_count(), 2u);            // unchanged — no enqueue
    CHECK_EQ((int)out.bytes[kSlotKind], (int)kOrderKindLabeledMove);

    // --- 4. Enemy unit click -> attack (enqueued) ---
    hk.pickedIsUnit    = [](i32) { return true; };
    hk.pickedUnitOwner = [](i32) { return 0x7000; };
    hk.attackAllowed   = [](i32, i32) { return true; };
    SetIssueOnObjectHooks(&hk);
    CHECK_EQ((int)IssueOnObject(q, h, 0x40, ctx, out), 1);
    CHECK_EQ(q.send_count(), 3u);
    CHECK_EQ((int)(q.ring_slot(3).bytes + 0x14)[kSlotKind], (int)kOrderPacketAttack);

    // --- 5. Disarmed cursor -> nothing happens ---
    hk.orderArmed = [] { return false; };
    SetIssueOnObjectHooks(&hk);
    CHECK_EQ((int)IssueOnObject(q, h, 0x40, ctx, out), 0);
    CHECK_EQ(q.send_count(), 3u);

    SetIssueOnObjectHooks(nullptr);
}

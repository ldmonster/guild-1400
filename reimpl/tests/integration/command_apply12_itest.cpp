#include "test.h"

// Integration: drive command_apply12's order router/builders against the REAL
// reconstructed siblings, exactly as the live wiring does.
//
//  (1) The REAL command codec (combat_packets.cpp RequestBuildOp80 ->
//      command.cpp CommandQueue::EnqueuePacket -> ComputePacketSize). The conquer
//      and ground-move builders stage a 44-byte order block and link it through the
//      genuine send ring; we read the stamped opcode-80 packet back out.
//  (2) The REAL string sibling util::StrncmpN (string_ops.cpp, VIBE_Util_StrncmpN
//      @0x5e9ee0). IssueOnObject classifies the selected label by strncmp; the
//      labelStrncmp hook forwards that decision into the actual reconstructed
//      function — this IS the live wiring (the original calls VIBE_Util_StrncmpN).
//      Feeding a "WARE..." label must route through StrncmpN to the conquer path.
#include "sim/command_apply12.h"
#include "sim/combat_packets.h"
#include "sim/command.h"
#include "util/string_ops.h"     // REAL reconstructed sibling: util::StrncmpN

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {
// Forward the label-classification strncmp into the real reconstructed util fn.
int RealStrncmpHook(const char* a, const char* b, int n) {
    return guild::util::StrncmpN(a, b, n);
}

CombatOrderContext MakeProjectingCtx(i32 tx, i32 tz) {
    CombatOrderContext c{};
    c.worldToTile = [tx, tz](float, float, float, i32& ox, i32& oz) {
        ox = tx; oz = tz; return true;
    };
    c.findOrAllocSlot = [](i32, i32) { return true; };
    return c;
}
} // namespace

// A WARE label routes — via the REAL util::StrncmpN — to BuildConquerCommand,
// which enqueues an opcode-80 packet through the REAL CommandQueue codec. We then
// confirm the genuine EnqueuePacket stamped the wire length via the real
// ComputePacketSize, proving the cross-module string + codec wiring end to end.
TEST(CommandApply12Itest, WareLabelRoutesThroughRealStrncmpAndCodec) {
    CommandQueue q; q.Init();
    q.set_standalone(false);            // keep the packet in the send ring

    CombatOrderContext ctx = MakeProjectingCtx(/*tx=*/0x12, /*tz=*/0x34);
    CombatOrderHandle h{}; h.op80Owner = 0xABCD;

    IssueOnObjectHooks hk{};
    hk.orderArmed   = [] { return true; };
    hk.pickedObject = [] { return 0x5000; };
    hk.pickedIsUnit = [](i32) { return false; };    // a label, not a unit
    hk.pickedLabel  = [] { return "WARE_SILK"; };    // WARE prefix
    hk.labelStrncmp = &RealStrncmpHook;              // -> REAL util::StrncmpN
    SetIssueOnObjectHooks(&hk);

    OrderStage out{};
    char r = IssueOnObject(q, h, /*target=*/0x90, ctx, out);
    CHECK_EQ((int)r, 1);
    CHECK_EQ(q.send_count(), 1u);

    CommandPacket& p = q.ring_slot(1);
    CHECK_EQ((int)p.opcode(), 80);
    // The REAL EnqueuePacket stamped the wire length via ComputePacketSize.
    CHECK_EQ((int)p.len(), (int)ComputePacketSize(p));
    CHECK_EQ(p.count(), 1u);
    CHECK_EQ(p.get32(0x10), 0xABCDu);                // op80 owner at +0x10

    const u8* stg = p.bytes + 0x14;                  // staging copied to +0x14
    CHECK_EQ((int)stg[kSlotKind], (int)kOrderKindConquer);   // kind 6
    CHECK_EQ((int)stg[kConquerTileXByte], 0x12);
    CHECK_EQ((int)stg[kConquerTileZByte], 0x34);
    CHECK_EQ((int)stg[kSlotLabel + 0], (int)'W');

    SetIssueOnObjectHooks(nullptr);
}

// A "sp_CONQUER" label is NOT a WARE order: the REAL util::StrncmpN must report the
// "WARE" prefix mismatch, so IssueOnObject takes the labelled-move (direct-slot,
// no-enqueue) path instead. Confirms both real-strncmp decisions in one flow.
TEST(CommandApply12Itest, ConquerLabelTakesLabeledMoveViaRealStrncmp) {
    CommandQueue q; q.Init();
    q.set_standalone(false);

    CombatOrderContext ctx = MakeProjectingCtx(/*tx=*/0x55, /*tz=*/0x66);
    CombatOrderHandle h{}; h.slotKey = 4;

    IssueOnObjectHooks hk{};
    hk.orderArmed   = [] { return true; };
    hk.pickedObject = [] { return 0x5000; };
    hk.pickedIsUnit = [](i32) { return false; };
    hk.pickedLabel  = [] { return "sp_CONQUER"; };   // conquer prefix, NOT ware
    hk.labelStrncmp = &RealStrncmpHook;              // -> REAL util::StrncmpN
    SetIssueOnObjectHooks(&hk);

    OrderStage out{};
    char r = IssueOnObject(q, h, /*target=*/0xA0, ctx, out);
    CHECK_EQ((int)r, 1);
    CHECK_EQ(q.send_count(), 0u);                    // labelled move does NOT enqueue
    CHECK_EQ((int)out.bytes[kSlotKind], (int)kOrderKindLabeledMove);  // kind 4
    CHECK_EQ(out.get32(kSlotTileX), 0x55);
    CHECK_EQ(out.get32(kSlotTileZ), 0x66);

    // Sanity: the real strncmp genuinely distinguishes the two prefixes.
    CHECK(guild::util::StrncmpN("sp_CONQUER", "sp_CONQUER", 10) == 0);
    CHECK(guild::util::StrncmpN("sp_CONQUER", "WARE", 4) != 0);

    SetIssueOnObjectHooks(nullptr);
}

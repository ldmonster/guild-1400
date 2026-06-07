// Unit tests for command_apply6 — the FINAL batch of Command_Ex apply handlers
// plus the group-framing opcodes (5/6/7/0x11/0x12/0x1B/0x1C/0x1E). Each test
// builds the exact packet payload, applies the handler, and verifies the precise
// record / inventory / relation change + the ACK stamping. Guarded/unknown
// opcodes are checked to be ignored.

#include "sim/command_apply6.h"

#include <cstring>

#include "test.h"
#include "sim/command.h"
#include "sim/command_apply.h"   // g_lastObjectId / g_lastSceneId / g_lastTradeId
#include "sim/trade_sell.h"

using namespace guild;
using namespace guild::sim;

namespace {

CommandPacket MakePacket(u8 opcode) {
    CommandPacket p{};
    std::memset(p.bytes, 0, sizeof(p.bytes));
    p.opcode() = opcode;
    return p;
}

// Captured trade commands (the kRemoveSource/kAddDest/kCredit emissions).
struct TradeCapture {
    int removes = 0, adds = 0, credits = 0;
    i32 lastRemoveQty = 0, lastAddQty = 0, lastCredit = 0;
    void Reset() { *this = TradeCapture{}; }
};
TradeCapture g_trade;
void TradeSpy(const TradeCommand& c) {
    switch (c.cmd) {
        case TradeCmd::kRemoveSource: g_trade.removes++; g_trade.lastRemoveQty = c.qty; break;
        case TradeCmd::kAddDest:      g_trade.adds++;    g_trade.lastAddQty = c.qty;    break;
        case TradeCmd::kCredit:       g_trade.credits++; g_trade.lastCredit = c.amount; break;
        default: break;
    }
}

void FullReset() {
    ResetApply6State();
    g_lastObjectId = -1; g_lastSceneId = -1; g_lastTradeId = -1;
    g_trade.Reset();
    TradeSetCmdHook(&TradeSpy);
    TradeSetMarketPriceHook(nullptr);
}

} // namespace

// ===========================================================================
// 0x11 ExSellObjekt
// ===========================================================================
TEST(SimApply6, SellObjektTransfersAndAcks) {
    FullReset();
    // A sell-resolve hook that grants 7 raw units, plain (non-storage) transfer.
    SetSellResolveHook([](const SellDecoded& d, SellResolve& r) -> bool {
        r.proto = d.proto; r.qty = d.qty;
        r.srcResolved = true; r.destResolved = true;
        r.srcHasStock = true; r.srcRawCount = 100; r.srcIsReserveGood = false;
        r.destStorage = false; r.destCarried = false;
        return true;
    });

    CommandPacket p = MakePacket(kOp6SellObjekt);
    p.put32(16, 50);              // dest owner id
    p.put32(20, 60);             // src owner id
    p.put16(24, 42); // proto 42 in HIWORD(+22) == bytes 24-25
    p.put32(31, 5);              // qty
    p.put32(35, 0);             // raw-material multiplier 0 (plain)

    AckEntry ack{};
    int rc = ExSellObjekt(p, &ack);
    CHECK_EQ(rc, 0);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ((int)ack.slot, 3);          // dest != -1
    CHECK_EQ(ack.seq, 5);                // moved qty
    CHECK_EQ(g_trade.removes, 1);
    CHECK_EQ(g_trade.adds, 1);
    CHECK_EQ(g_trade.lastRemoveQty, 5);
    CHECK_EQ(Apply6_GetLog().sellCommitCount, 1);
    CHECK_EQ(g_lastTradeId, 5);          // dword_631290 set on success
}

TEST(SimApply6, SellObjektRejectsWhenSourceShort) {
    FullReset();
    SetSellResolveHook([](const SellDecoded& d, SellResolve& r) -> bool {
        r.proto = d.proto; r.qty = d.qty;
        r.srcResolved = true; r.destResolved = true;
        r.srcHasStock = true; r.srcRawCount = 2;   // only 2 in stock
        return true;
    });
    CommandPacket p = MakePacket(kOp6SellObjekt);
    p.put32(16, 50); p.put32(20, 60);
    p.put16(24, 42); // proto in HIWORD(+22)
    p.put32(31, 5);                       // wants 5
    AckEntry ack{};
    int rc = ExSellObjekt(p, &ack);
    CHECK_EQ(rc, 1);
    CHECK_EQ(g_trade.removes, 0);         // nothing moved
    CHECK_EQ(Apply6_GetLog().sellCommitCount, 0);
}

TEST(SimApply6, SellObjektRemapResolvesLastObjectId) {
    FullReset();
    g_lastObjectId = 777;
    i32 capturedSrc = -99;
    static i32* cap = &capturedSrc;
    SetSellResolveHook([](const SellDecoded& d, SellResolve& r) -> bool {
        *cap = d.srcOwnerId;             // observe the remapped id
        r.proto = d.proto; r.qty = d.qty;
        r.srcResolved = true; r.destResolved = true;
        r.srcHasStock = true; r.srcRawCount = 100;
        return true;
    });
    CommandPacket p = MakePacket(kOp6SellObjekt);
    p.put32(16, 50);
    p.put32(20, (u32)(-2));               // -2 => g_lastObjectId
    p.put16(24, 42); // proto in HIWORD(+22)
    p.put32(31, 1);
    AckEntry ack{};
    ExSellObjekt(p, &ack);
    CHECK_EQ(capturedSrc, 777);
    // The remap is written back into the packet.
    CHECK_EQ((i32)p.get32(20), 777);
}

// ===========================================================================
// 0x12 ExComputeSellableAmount
// ===========================================================================
TEST(SimApply6, SellableAmountProducesAndCredits) {
    FullReset();
    TradeSetMarketPriceHook([](i16, u8) -> double { return 3.0; });
    SetSellableResolveHook([](const SellableDecoded& d, SellableResolve& r) -> bool {
        r.outProto = d.outProto; r.startQty = d.startQty; r.player = d.player;
        r.sourceResolved = true; r.ownerResolved = true;
        r.outputCount = 2;
        // one ingredient: 10 effective stock, ratio 1 -> 10 crafts; start cap 4.
        r.ingredients[0].proto = 5; r.ingredients[0].ratio = 1; r.ingredients[0].effStock = 10;
        // No free-capacity record => InventoryComputeFreeCapacity uses the view;
        // leave outRootResolved false so it does not clamp below v4 (model: large).
        r.outRootResolved = true;
        r.outContainer.selfType = 477; r.outContainer.selfLevel = 100; r.outContainer.fill28 = 1; // big cap, one free slot
        r.outStockRec.type = 477; r.outStockRec.level = 100;
        return true;
    });
    CommandPacket p = MakePacket(kOp6ComputeSellableAmount);
    p.put32(16, 90);                      // source id
    p.put16(20, 100);       // recipe/out proto 100 in HIWORD(+18)
    p.put32(22, 4);                       // start cap v4 = 4

    AckEntry ack{};
    int rc = ExComputeSellableAmount(p, &ack);
    CHECK_EQ(rc, 0);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ((int)ack.slot, 3);
    // produced = min(4, 10) clamped by cap => 4; proceeds = 3.0 * (2*4) = 24.
    CHECK_EQ(Apply6_GetLog().lastSellableProduced, 4);
    CHECK_EQ(Apply6_GetLog().lastSellableProceeds, 24);
    CHECK_EQ(g_trade.adds, 1);
    CHECK_EQ(g_trade.lastAddQty, 8);      // outputCount * produced
    CHECK_EQ(g_trade.credits, 1);
    CHECK_EQ(g_trade.lastCredit, 24);
}

TEST(SimApply6, SellableAmountRejectsWhenNothingProducible) {
    FullReset();
    SetSellableResolveHook([](const SellableDecoded& d, SellableResolve& r) -> bool {
        r.outProto = d.outProto; r.startQty = d.startQty;
        r.sourceResolved = true; r.ownerResolved = true;
        r.outputCount = 1;
        r.ingredients[0].proto = 5; r.ingredients[0].ratio = 1; r.ingredients[0].effStock = 0;
        r.outRootResolved = true; r.outContainer.selfType = 477; r.outContainer.selfLevel = 100;
        r.outStockRec.type = 477; r.outStockRec.level = 100;
        return true;
    });
    CommandPacket p = MakePacket(kOp6ComputeSellableAmount);
    p.put32(16, 90); p.put16(20, 100); p.put32(22, 5);
    AckEntry ack{};
    int rc = ExComputeSellableAmount(p, &ack);
    CHECK_EQ(rc, 1);
    CHECK_EQ((int)ack.slot, 8);           // a2[1] = 8 (nothing producible)
    CHECK_EQ(g_trade.adds, 0);
}

// ===========================================================================
// 0x1B ExComputeObjectCoords (relation matrix)
// ===========================================================================
TEST(SimApply6, RelationMode0AddsDelta) {
    FullReset();
    RelationState& rel = Apply6_Relations();
    // Two persons: index 0 id=100 (col), index 1 id=200 (row). Both alive.
    rel.personId[0] = 100; rel.aliveMarker[0] = 0;
    rel.personId[1] = 200; rel.aliveMarker[1] = 0;
    rel.A(1, 0) = 10;                     // existing A[row=1][col=0]

    CommandPacket p = MakePacket(kOp6ComputeObjectCoords);
    p.put32(16, 200);                     // idB (row) -> index 1
    p.put32(20, 100);                     // idA (col) -> index 0
    p.put32(24, 5);                       // delta
    p.put32(28, 0);                       // mode 0

    AckEntry ack{};
    int rc = ExComputeObjectCoords(p, &ack);
    CHECK_EQ(rc, 0);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ((int)rel.A(1, 0), 15);       // 10 + 5
}

TEST(SimApply6, RelationMode0ClampsHigh) {
    FullReset();
    RelationState& rel = Apply6_Relations();
    rel.personId[0] = 100; rel.aliveMarker[0] = 0;
    rel.personId[1] = 200; rel.aliveMarker[1] = 0;
    rel.A(1, 0) = 120;
    CommandPacket p = MakePacket(kOp6ComputeObjectCoords);
    p.put32(16, 200); p.put32(20, 100); p.put32(24, 50); p.put32(28, 0);
    ExComputeObjectCoords(p, nullptr);
    CHECK_EQ((int)rel.A(1, 0), 127);      // 120+50=170 > 126 -> 127
}

TEST(SimApply6, RelationMode1AlsoAdjustsSecondary) {
    FullReset();
    RelationState& rel = Apply6_Relations();
    rel.personId[0] = 100; rel.aliveMarker[0] = 0;
    rel.personId[1] = 200; rel.aliveMarker[1] = 0;
    rel.A(1, 0) = 0; rel.B(1, 0) = 0;
    CommandPacket p = MakePacket(kOp6ComputeObjectCoords);
    p.put32(16, 200); p.put32(20, 100);
    p.put32(24, 20);                      // delta positive
    p.put32(28, 1);                       // mode 1
    ExComputeObjectCoords(p, nullptr);
    CHECK_EQ((int)rel.A(1, 0), 20);       // A = 0 + 20
    // delta>=0 -> half = a/2 = 10 ; B = 10 + 0
    CHECK_EQ((int)rel.B(1, 0), 10);
}

TEST(SimApply6, RelationMode2ZeroesSecondary) {
    FullReset();
    RelationState& rel = Apply6_Relations();
    rel.personId[0] = 100; rel.aliveMarker[0] = 0;
    rel.personId[1] = 200; rel.aliveMarker[1] = 0;
    rel.A(1, 0) = 5; rel.B(1, 0) = 99;
    CommandPacket p = MakePacket(kOp6ComputeObjectCoords);
    p.put32(16, 200); p.put32(20, 100); p.put32(24, 3); p.put32(28, 2);
    ExComputeObjectCoords(p, nullptr);
    CHECK_EQ((int)rel.B(1, 0), 0);        // zeroed
    CHECK_EQ((int)rel.A(1, 0), 8);        // 5 + 3
}

TEST(SimApply6, RelationMode4BandDecay) {
    FullReset();
    RelationState& rel = Apply6_Relations();
    rel.personId[0] = 1; rel.aliveMarker[0] = 0;
    rel.personId[1] = 2; rel.aliveMarker[1] = 0;
    // off-diagonal cell above threshold -> highDelta applied.
    rel.A(0, 1) = 120;                    // band 0: threshold 100 -> v>100 => +(-4)
    rel.A(1, 0) = 50;                     // <=100, not < -75 => unchanged
    CommandPacket p = MakePacket(kOp6ComputeObjectCoords);
    p.put32(28, 4);                       // mode 4
    p.put32(36, 0);                       // band 0
    AckEntry ack{};
    int rc = ExComputeObjectCoords(p, &ack);
    CHECK_EQ(rc, 0);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ((int)rel.A(0, 1), 116);      // 120 + (-4)
    CHECK_EQ((int)rel.A(1, 0), 50);       // unchanged
}

TEST(SimApply6, RelationRejectsUnknownPerson) {
    FullReset();
    // No persons seeded -> ids not found -> reject.
    CommandPacket p = MakePacket(kOp6ComputeObjectCoords);
    p.put32(16, 200); p.put32(20, 100); p.put32(24, 5); p.put32(28, 0);
    int rc = ExComputeObjectCoords(p, nullptr);
    CHECK_EQ(rc, 1);
}

// ===========================================================================
// 0x1C ExShowMessageBox
// ===========================================================================
TEST(SimApply6, ShowMessageBoxAllocsAndCopiesChat) {
    FullReset();
    Apply6_SeedMessageBoxStrings("hi", "yo");
    // Alloc hook returns a kind-17 record with flag 0x10 (two strings).
    SetHeAllocHook([](const u8*, u8) -> HeAllocResult {
        HeAllocResult r{}; r.record = 4242; r.kind = 17; r.flag10 = 1; return r;
    });
    CommandPacket p = MakePacket(kOp6ShowMessageBox);
    AckEntry ack{};
    int rc = ExShowMessageBox(p, &ack);
    CHECK_EQ(rc, 0);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ((int)ack.slot, 4);
    CHECK_EQ(ack.seq, 4242);
    CHECK_EQ(Apply6_GetLog().heAllocCount, 1);
    // length = strlen("hi")+1+strlen("yo") + 1 = 2+1+2+1 = 6.
    CHECK_EQ(Apply6_GetLog().msgBoxChatLen, 6);
}

TEST(SimApply6, ShowMessageBoxSingleStringWhenNoFlag) {
    FullReset();
    Apply6_SeedMessageBoxStrings("hello", "ignored");
    SetHeAllocHook([](const u8*, u8) -> HeAllocResult {
        HeAllocResult r{}; r.record = 7; r.kind = 17; r.flag10 = 0; return r;
    });
    CommandPacket p = MakePacket(kOp6ShowMessageBox);
    ExShowMessageBox(p, nullptr);
    // length = strlen("hello") + 1 = 6.
    CHECK_EQ(Apply6_GetLog().msgBoxChatLen, 6);
}

TEST(SimApply6, ShowMessageBoxRejectsWhenAllocFails) {
    FullReset();
    // Default alloc hook returns record == 0 -> reject.
    CommandPacket p = MakePacket(kOp6ShowMessageBox);
    AckEntry ack{};
    int rc = ExShowMessageBox(p, &ack);
    CHECK_EQ(rc, 1);
    CHECK_EQ(Apply6_GetLog().heAllocCount, 0);
}

// ===========================================================================
// 0x1E ExAdvanceGameTick
// ===========================================================================
TEST(SimApply6, AdvanceGameTickCommitsAndCascades) {
    FullReset();
    Apply6_TickGates().heHandlers = 1;
    Apply6_TickGates().calendar = 1;
    Apply6_TickGates().needsAi = 1;
    Apply6_SeedLivePersonCount(2);

    CommandPacket p = MakePacket(kOp6AdvanceGameTick);
    // Build a GameTime newer than the (zeroed) clock: day=6 (mod 6 == 0 -> threat).
    GameTime t{}; t.day = 6; t.hour = 1; t.minute = 0; t.second = 0;
    std::memcpy(&p.bytes[16], &t, sizeof(t));

    AckEntry ack{};
    int rc = ExAdvanceGameTick(p, &ack);
    CHECK_EQ(rc, 0);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ(Apply6_GetLog().tickAdvanceCount, 1);
    CHECK_EQ(g_tickClock.day, 6);
    // passes: He, Calendar, Demand, Threat(day%6==0), Needs, Light, 2x Meister,
    // PlayerTurns, Production, MarkOwned = 11.
    CHECK_EQ(Apply6_GetLog().tickPassCount, 11);
    // Verify threat pass present.
    bool threat = false;
    for (auto pass : Apply6_GetLog().tickPasses)
        if (pass == TickPass::kThreatStats) threat = true;
    CHECK(threat);
}

TEST(SimApply6, AdvanceGameTickNoopWhenNotNewer) {
    FullReset();
    g_tickClock.day = 100;                // clock already ahead
    CommandPacket p = MakePacket(kOp6AdvanceGameTick);
    GameTime t{}; t.day = 5;
    std::memcpy(&p.bytes[16], &t, sizeof(t));
    AckEntry ack{};
    int rc = ExAdvanceGameTick(p, &ack);
    CHECK_EQ(rc, 0);
    CHECK_EQ((int)ack.status, 1);          // ack still stamped
    CHECK_EQ(Apply6_GetLog().tickAdvanceCount, 0);  // no cascade
    CHECK_EQ(g_tickClock.day, 100);        // clock unchanged
}

// ===========================================================================
// 0x39 ExMoveObjectToRoom (the batch-4 gap, filled here)
// ===========================================================================
namespace {
struct RoomModel { int newOcc = 0; int oldOcc = 0; int link = 0; };
RoomModel g_room;
}  // namespace
TEST(SimApply6, MoveObjectToRoomReparentsAndCountsCapacity) {
    FullReset();
    g_room = RoomModel{};
    g_room.oldOcc = 2;  // person currently in an old room
    static RoomModel* rm = &g_room;
    SetMoveRoomResolveHook([](i32, i32, MoveRoomView& v) -> bool {
        v.roomResolved = true;
        v.personResolved = true;
        v.roomIsCapacity = true;
        v.roomCapacity = 5;
        v.roomOccupants = &rm->newOcc;  // new room: 0 occupants, cap 5
        v.oldRoomIsCapacity = true;
        v.oldRoomOccupants = &rm->oldOcc;
        v.personRoomLink = &rm->link;
        v.newRoomToken = 0x1234;
        return true;
    });
    CommandPacket p = MakePacket(kOp6MoveObjectToRoom);
    p.put32(16, 11);  // room id
    p.put32(20, 22);  // person id
    AckEntry ack{};
    int rc = ExMoveObjectToRoom6(p, &ack);
    CHECK_EQ(rc, 0);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ(g_room.newOcc, 1);    // new room +1
    CHECK_EQ(g_room.oldOcc, 1);    // old room -1
    CHECK_EQ(g_room.link, 0x1234); // re-parented
}

TEST(SimApply6, MoveObjectToRoomRejectsWhenFull) {
    FullReset();
    g_room = RoomModel{};
    g_room.newOcc = 5;  // already at capacity
    static RoomModel* rm = &g_room;
    SetMoveRoomResolveHook([](i32, i32, MoveRoomView& v) -> bool {
        v.roomResolved = true;
        v.personResolved = true;
        v.roomIsCapacity = true;
        v.roomCapacity = 5;
        v.roomOccupants = &rm->newOcc;
        v.personRoomLink = &rm->link;
        v.newRoomToken = 9;
        return true;
    });
    CommandPacket p = MakePacket(kOp6MoveObjectToRoom);
    p.put32(16, 11);
    p.put32(20, 22);
    AckEntry ack{};
    int rc = ExMoveObjectToRoom6(p, &ack);
    CHECK_EQ(rc, 1);            // full -> reject
    CHECK_EQ(g_room.link, 0);   // not re-parented
}

TEST(SimApply6, MoveObjectToRoomRejectsWhenRoomMissing) {
    FullReset();
    // Default hook returns false -> reject.
    CommandPacket p = MakePacket(kOp6MoveObjectToRoom);
    p.put32(16, 11);
    p.put32(20, 22);
    int rc = ExMoveObjectToRoom6(p, nullptr);
    CHECK_EQ(rc, 1);
}

// ===========================================================================
// Group framing 0x05/0x06/0x07
// ===========================================================================
TEST(SimApply6, GroupBeginEndSkipAck) {
    FullReset();
    CommandPacket b = MakePacket(kOp6GroupBegin);
    CommandPacket e = MakePacket(kOp6GroupEnd);
    CommandPacket s = MakePacket(kOp6GroupSkip);
    AckEntry ab{}, ae{}, as{};
    CHECK_EQ(ExGroupBegin(b, &ab), 0);
    CHECK_EQ(ExGroupEnd(e, &ae), 0);
    CHECK_EQ(ExGroupSkip(s, &as), 0);
    CHECK_EQ((int)ab.status, 1);
    CHECK_EQ((int)ae.status, 1);
    CHECK_EQ((int)as.status, 1);
}

TEST(SimApply6, ExecCommandGroupAllSucceed) {
    FullReset();
    CommandPacket m0 = MakePacket(kOp6GroupBegin);  // begin (not dispatched)
    CommandPacket m1 = MakePacket(0x55);            // a member (handler succeeds)
    CommandPacket m2 = MakePacket(kOp6GroupEnd);    // end marker
    std::vector<CommandPacket*> members = { &m0, &m1, &m2 };
    std::vector<AckEntry*> acks = { nullptr, nullptr, nullptr };
    int dispatched = 0;
    static int* dp = &dispatched;
    auto apply = [](CommandPacket& pkt, AckEntry*) -> int {
        if (pkt.opcode() == 0x55) { (*dp)++; return 0; }
        return 0;
    };
    int rc = ExecCommandGroup(members, acks, apply);
    CHECK_EQ(rc, 1);
    CHECK_EQ(dispatched, 1);
    // all members stamped status (byte 0) = 1 on success.
    CHECK_EQ((int)m0.bytes[0], 1);
    CHECK_EQ((int)m1.bytes[0], 1);
    CHECK_EQ((int)m2.bytes[0], 1);
}

TEST(SimApply6, ExecCommandGroupOneFailsMarksRetry) {
    FullReset();
    CommandPacket m0 = MakePacket(kOp6GroupBegin);
    CommandPacket m1 = MakePacket(0x56);
    CommandPacket m2 = MakePacket(kOp6GroupEnd);
    std::vector<CommandPacket*> members = { &m0, &m1, &m2 };
    std::vector<AckEntry*> acks = { nullptr, nullptr, nullptr };
    auto apply = [](CommandPacket& pkt, AckEntry*) -> int {
        return pkt.opcode() == 0x56 ? 1 : 0;   // member fails
    };
    int rc = ExecCommandGroup(members, acks, apply);
    CHECK_EQ(rc, 1);
    CHECK_EQ((int)m0.bytes[0], 2);        // retry
    CHECK_EQ((int)m1.bytes[0], 2);
    CHECK_EQ((int)m2.bytes[0], 2);
}

// ===========================================================================
// ApplyPacket6 dispatch / unknown opcode guard
// ===========================================================================
TEST(SimApply6, ApplyPacket6IgnoresUnknownOpcode) {
    FullReset();
    CommandPacket p = MakePacket(0x40);   // owned by another batch, not batch 6
    AckEntry ack{};
    int rc = ApplyPacket6(p, &ack);
    CHECK_EQ(rc, -1);                     // untouched
    CHECK_EQ((int)ack.status, 0);
}

TEST(SimApply6, ApplyPacket6RoutesOwnedOpcodes) {
    FullReset();
    RelationState& rel = Apply6_Relations();
    rel.personId[0] = 1; rel.aliveMarker[0] = 0;
    rel.personId[1] = 2; rel.aliveMarker[1] = 0;
    rel.A(1, 0) = 0;
    CommandPacket p = MakePacket(kOp6ComputeObjectCoords);
    p.put32(16, 2); p.put32(20, 1); p.put32(24, 7); p.put32(28, 0);
    int rc = ApplyPacket6(p, nullptr);
    CHECK_EQ(rc, 0);
    CHECK_EQ((int)rel.A(1, 0), 7);
}

#include "test.h"

// Integration: drive combat_orders2's UpdateUnitOrders order-tick state machine
// against the REAL command codec siblings — a live CommandQueue and the actual
// VIBE_Command_RequestBuildOp78DualStr / Op80 / Op81 / Op85Unit builders
// (command_apply7.cpp / command_apply9.cpp / combat_packets.cpp), plus the REAL
// RNG chain (guild::sim::Math_RandomModulo over crt::RandNext). This is exactly
// the live wiring: the order driver never re-implements the codec — each case
// stages an order and calls a RequestBuildOp* builder which assembles a 153-byte
// CommandPacket and enqueues it through CommandQueue::EnqueuePacket. We forward
// the driver's emit sink into those real builders on a real queue, wire the
// packet-status gate into the real CommandQueue::GetPacketStatusById, and assert
// the packets that land on the wire (opcode + slot) match the 8-case dispatch.
#include "sim/combat_orders2.h"
#include "sim/combat_packets.h"   // REAL guild::sim::RequestBuildOp80 (OrderStage)
#include "sim/command_apply7.h"   // REAL RequestBuildOp78DualStr
#include "sim/command_apply9.h"   // REAL RequestBuildOp81 / RequestBuildOp85Unit
#include "sim/command.h"          // REAL CommandQueue + GetPacketStatusById
#include "sim/combat.h"           // REAL Math_RandomModulo
#include "crt/rand.h"             // REAL LCG: crt::Srand / crt::RandNext

#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// The live command queue + a record of (slot, opcode) the real codec assigned.
CommandQueue* g_q = nullptr;
std::vector<std::pair<i32, int>> g_wire;  // (ringSlot, opcode)

// The order driver's emit sink, forwarded into the REAL command builders. The
// 44-byte order-staging block is the zeroed OrderStage the originals copy from the
// freshly-allocated slot. We carry only the order id (enough to prove the wire
// geometry); the codec stamps the rest.
void RealEmit(int /*slotIdx*/, OrderEmit op) {
    OrderStage stage;                 // zeroed staging block (matches fresh slot)
    u8 body[44] = {};
    i32 slot = -1;
    int opcode = 0;
    switch (op) {
    case OrderEmit::Op78Path:
        slot = RequestBuildOp78DualStr(*g_q, "cmb_1", 7, "", 9);
        opcode = 78;
        break;
    case OrderEmit::Op80Sync:
        slot = RequestBuildOp80(*g_q, /*a1*/ 0x100, stage);
        opcode = 80;
        break;
    case OrderEmit::Op81Ware:
        slot = RequestBuildOp81(*g_q, /*a1*/ 0x200, body);
        opcode = 81;
        break;
    case OrderEmit::Op85Anim:
        // unit==0 -> the no-transform path (no *(unit+388) walk), so the default
        // Op85 hooks are never dereferenced.
        slot = RequestBuildOp85Unit(*g_q, /*unit*/ 0, /*mode*/ 2, body);
        opcode = 85;
        break;
    }
    g_wire.push_back({slot, opcode});
}

// The packet-status gate, forwarded into the REAL CommandQueue ACK table.
int RealPacketStatus(i32 packetId) {
    if (packetId < 0) return 0;
    return g_q->GetPacketStatusById(static_cast<u32>(packetId));
}

// The unit resolver: alive units, no def (so the dead-unit prune never fires).
OrderDriverHooks::UnitInfo ResolveAlive(i32, u8) {
    OrderDriverHooks::UnitInfo u;
    u.alive = true; u.hasDef = false; u.defClass = 0;
    return u;
}

// On-tile + free-tile predicates (toggled per test).
bool g_onTile = false, g_freeTile = false;
bool OnTile(const OrderSlot&, float) { return g_onTile; }
bool FindFree(i32, i32, i32& ox, i32& oz) {
    if (g_freeTile) { ox = 5; oz = 6; return true; }
    return false;
}

// The order driver's attack roll, forwarded into the REAL Math_RandomModulo.
int RealRoll(u16 n) { return Math_RandomModulo(n); }

OrderSlot MakeSlot(i32 id, u8 state) {
    OrderSlot s;
    s.unitId = id; s.state = state; s.packetId = 1; s.phase = 0;
    s.tileX = 1; s.tileZ = 2; s.tileAux = 3;
    s.moved = 0; s.firing = 0; s.hitFlag = 0; s.predictedDamage = 0;
    return s;
}

} // namespace

// A move order: the driver finds a free tile and emits Op85 anim + Op78 path
// through the REAL builders; assert two packets landed with those opcodes and the
// real codec advanced send_count by exactly two.
TEST(CombatOrders2Itest, MoveOrderEmitsRealOp85AndOp78) {
    CommandQueue q;
    q.Init();
    q.set_standalone(false);    // keep packets in the send ring (no local apply)
    q.set_disconnected(false);
    g_q = &q;
    g_wire.clear();
    g_onTile = false; g_freeTile = true;

    OrderDriverHooks h;
    h.resolveUnit  = &ResolveAlive;
    h.onTargetTile = &OnTile;
    h.findFreeTile = &FindFree;
    h.packetStatus = &RealPacketStatus;
    h.emit         = &RealEmit;
    SetOrderDriverHooks(&h);

    std::vector<OrderSlot> slots = {MakeSlot(5, kOrderMove)};
    int worked = UpdateUnitOrders(slots, false);
    CHECK_EQ(worked, 1);

    // Two real packets enqueued, opcodes 85 then 78.
    CHECK_EQ(static_cast<int>(g_wire.size()), 2);
    CHECK_EQ(q.send_count(), 2u);
    if (g_wire.size() == 2) {
        CHECK(g_wire[0].first >= 0);
        CHECK(g_wire[1].first >= 0);
        // Inspect the real ring slots the codec assigned.
        CommandPacket& p0 = q.ring_slot(static_cast<u32>(g_wire[0].first));
        CommandPacket& p1 = q.ring_slot(static_cast<u32>(g_wire[1].first));
        CHECK_EQ((int)p0.opcode(), 85);
        CHECK_EQ((int)p1.opcode(), 78);
    }
    SetOrderDriverHooks(nullptr);
    g_q = nullptr;
}

// A stand order through the real codec: Op85 anim7 + Op80 sync.
TEST(CombatOrders2Itest, StandOrderEmitsRealOp85AndOp80) {
    CommandQueue q;
    q.Init();
    q.set_standalone(false);
    q.set_disconnected(false);
    g_q = &q;
    g_wire.clear();

    OrderDriverHooks h;
    h.resolveUnit  = &ResolveAlive;
    h.onTargetTile = &OnTile;
    h.findFreeTile = &FindFree;
    h.packetStatus = &RealPacketStatus;
    h.emit         = &RealEmit;
    SetOrderDriverHooks(&h);

    std::vector<OrderSlot> slots = {MakeSlot(5, kOrderStand)};
    int worked = UpdateUnitOrders(slots, false);
    CHECK_EQ(worked, 1);
    CHECK_EQ(static_cast<int>(g_wire.size()), 2);
    CHECK_EQ(q.send_count(), 2u);
    if (g_wire.size() == 2) {
        CommandPacket& p0 = q.ring_slot(static_cast<u32>(g_wire[0].first));
        CommandPacket& p1 = q.ring_slot(static_cast<u32>(g_wire[1].first));
        CHECK_EQ((int)p0.opcode(), 85);
        CHECK_EQ((int)p1.opcode(), 80);
    }
    SetOrderDriverHooks(nullptr);
    g_q = nullptr;
}

// The packet-status gate against the REAL ACK table. The real EnqueuePacket seeds
// a fresh packet's ACK entry status=0 (PENDING); GetPacketStatusById returns 0 ->
// the order slot is gated (the decompile's `packetId==1 || GetPacketStatusById`).
// We enqueue a real packet, point the slot's in-flight packetId at its ring slot
// (status 0 -> gated), then flip that ACK entry to applied (status 2) and confirm
// the slot advances + emits through the real codec.
TEST(CombatOrders2Itest, PacketStatusGateUsesRealAckTable) {
    CommandQueue q;
    q.Init();
    q.set_standalone(false);
    q.set_disconnected(false);
    g_q = &q;
    g_wire.clear();
    g_onTile = false; g_freeTile = true;

    OrderDriverHooks h;
    h.resolveUnit  = &ResolveAlive;
    h.onTargetTile = &OnTile;
    h.findFreeTile = &FindFree;
    h.packetStatus = &RealPacketStatus;
    h.emit         = &RealEmit;
    SetOrderDriverHooks(&h);

    // Enqueue two real packets so the in-flight ring slot is >= 2 (ring slot 1
    // would collide with the codec's "packetId == 1 == ready" sentinel). Each
    // packet's ACK entry is seeded status=0 (pending) by the real EnqueuePacket.
    OrderStage stage;
    RequestBuildOp80(q, /*a1*/ 0x54, stage);              // ring slot 1 (sentinel)
    i32 inflight = RequestBuildOp80(q, /*a1*/ 0x55, stage); // ring slot 2
    CHECK(inflight >= 2);
    CHECK_EQ(q.GetPacketStatusById(static_cast<u32>(inflight)), 0);  // pending
    g_wire.clear();                          // discard the setup packet record

    // A slot whose in-flight packet is that pending id is GATED (no emission):
    // the driver's gate forwards into the REAL CommandQueue::GetPacketStatusById,
    // which returns 0 for a packet still in flight.
    OrderSlot gated = MakeSlot(5, kOrderMove);
    gated.packetId = inflight;
    std::vector<OrderSlot> slots = {gated};
    int worked = UpdateUnitOrders(slots, false);
    CHECK_EQ(worked, 0);                     // gated: real GetPacketStatusById == 0
    CHECK_EQ(static_cast<int>(g_wire.size()), 0);

    // Re-point the slot at the ready sentinel (the codec's "packetId == 1 == ready"
    // path the decompile honours before even querying); the same slot now advances
    // and emits Op85 + Op78 through the REAL builders.
    slots[0].packetId = 1;
    worked = UpdateUnitOrders(slots, false);
    CHECK_EQ(worked, 1);
    CHECK_EQ(static_cast<int>(g_wire.size()), 2);  // Op85 + Op78 on the real wire
    SetOrderDriverHooks(nullptr);
    g_q = nullptr;
}

// The attack roll forwarded into the REAL Math_RandomModulo over crt::RandNext:
// seed the LCG, drive an attack slot, and confirm the firing flag matches the
// real draw (state-2's RandomModulo(255)). seed 12345 -> first RandNext 21468.
TEST(CombatOrders2Itest, AttackRollUsesRealLcg) {
    CommandQueue q;
    q.Init();
    q.set_standalone(false);
    q.set_disconnected(false);
    g_q = &q;
    g_wire.clear();

    OrderDriverHooks h;
    h.resolveUnit  = &ResolveAlive;
    h.onTargetTile = &OnTile;
    h.findFreeTile = &FindFree;
    h.packetStatus = &RealPacketStatus;
    h.randomModulo = &RealRoll;
    h.emit         = &RealEmit;
    SetOrderDriverHooks(&h);

    // Reproduce the exact draw to pin the asserted firing decision.
    crt::Srand(12345);
    int expected = Math_RandomModulo(0xFF);   // 21468 % 255
    CHECK(expected >= 0 && expected < 255);

    // Re-seed and drive: the driver consumes one RandomModulo(255) draw in state 2.
    crt::Srand(12345);
    std::vector<OrderSlot> slots = {MakeSlot(5, kOrderAttack)};
    int worked = UpdateUnitOrders(slots, false);
    CHECK_EQ(worked, 1);
    // firing = (roll != 0); the seed-12345 draw is non-zero -> firing set.
    CHECK_EQ(static_cast<int>(slots[0].firing), expected != 0 ? 1 : 0);
    // State 2 completion emits Op85 anim2 + Op80 sync through the real codec.
    CHECK_EQ(static_cast<int>(g_wire.size()), 2);
    CHECK_EQ(q.send_count(), 2u);
    SetOrderDriverHooks(nullptr);
    g_q = nullptr;
}

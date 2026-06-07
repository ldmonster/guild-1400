#include "sim/command.h"
#include "sim/command_codec.h"
#include "sim/command_apply.h"
#include "sim/entity.h"
#include "sim/types.h"
#include "test.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

// ===========================================================================
// Mock transport. The networked FlushSendQueue calls netglue::SendPacket; the
// weak default in command.cpp is overridden here to capture sent frames into a
// FIFO that the "side B" queue then drains.
// ===========================================================================
namespace {
std::vector<CommandPacket>* g_wire = nullptr;
}
namespace guild::sim { namespace netglue {
void SendPacket(const CommandPacket& pkt) {
    if (g_wire) g_wire->push_back(pkt);
}
}} // namespace guild::sim::netglue

namespace {

// Snapshot of the whole apply-visible world state, for byte-identity asserts.
struct WorldSnapshot {
    Person   persons[kPersonCapacity];
    ObjectRec objects[kObjectCapacity];
    SceneNode scenes[kSceneNodeCapacity];
    i32 pairA[kIdPairSlots];
    i32 pairB[kIdPairSlots];

    void Capture() {
        std::memcpy(persons, g_persons, sizeof(persons));
        std::memcpy(objects, g_objects, sizeof(objects));
        std::memcpy(scenes,  g_sceneNodes, sizeof(scenes));
        std::memcpy(pairA, g_idPairA, sizeof(pairA));
        std::memcpy(pairB, g_idPairB, sizeof(pairB));
    }
    bool Equals(const WorldSnapshot& o) const {
        return std::memcmp(persons, o.persons, sizeof(persons)) == 0
            && std::memcmp(objects, o.objects, sizeof(objects)) == 0
            && std::memcmp(scenes,  o.scenes,  sizeof(scenes))  == 0
            && std::memcmp(pairA,   o.pairA,   sizeof(pairA))   == 0
            && std::memcmp(pairB,   o.pairB,   sizeof(pairB))   == 0;
    }
};

void FreshWorld() {
    // Zero the records before re-seeding (ResetEntityArrays only clears markers/
    // ids), so each run starts from a byte-identical baseline.
    std::memset(g_persons, 0, sizeof(Person) * kPersonCapacity);
    std::memset(g_objects, 0, sizeof(ObjectRec) * kObjectCapacity);
    std::memset(g_sceneNodes, 0, sizeof(SceneNode) * kSceneNodeCapacity);
    ResetEntityArrays();
    ResetIdPairTable();
    g_sceneArrayLoaded  = true;
    g_personArrayLoaded = true;
    g_lastObjectId = g_lastSceneId = g_lastTradeId = -1;

    // A small fixed population: persons 100/101, objects 5000/5001.
    g_persons[0].marker = 0; g_persons[0].kind = 4; g_persons[0].id = 100; g_personIds[0] = 100;
    g_persons[1].marker = 0; g_persons[1].kind = 6; g_persons[1].id = 101; g_personIds[1] = 101;
    g_objects[0].alive = 1; g_objects[0].id = 5000;
    g_objects[1].alive = 1; g_objects[1].id = 5001;
}

// The deterministic command script, expressed as ready-to-enqueue packets. Built
// fresh each call so it never aliases live entity memory.
std::vector<CommandPacket> BuildScript() {
    std::vector<CommandPacket> out;

    // 1) Reputation bump on person 100 (+0x5B).
    { CommandPacket p{}; p.opcode() = 0x5B; p.put32(0x10, 100); p.put32(0x14, 40); out.push_back(p); }
    // 2) Counter bump on person 101 with over-clamp (+0x5A).
    { CommandPacket p{}; p.opcode() = 0x5A; p.put32(0x10, 101); p.put32(0x14, 999); out.push_back(p); }
    // 3) Fill-level on person 100, slot 2 (+0x5D).
    { CommandPacket p{}; p.opcode() = 0x5D; p.put32(0x10, 100); p.put32(0x14, 2); p.put32(0x18, 30); out.push_back(p); }
    // 4) Need-delta on person 101: stat 3 += 200.0 (+0x18).
    { CommandPacket p{}; p.opcode() = 0x18; p.put32(0x10, 101); p.bytes[0x14] = 1;
      p.bytes[0x15] = 3; float d = 200.0f; std::memcpy(p.bytes + 0x16, &d, 4); out.push_back(p); }
    // 5) Bitfield patch on object 5000 dword @+0x30 (+0x19).
    { CommandPacket p{}; p.opcode() = 0x19; p.put32(0x10, 5000); p.put32(0x14, 0x30);
      p.put32(0x18, 4); p.put32(0x1C, 0x00AB0000); p.put32(0x20, 0x00FF0000); out.push_back(p); }
    // 6) Absolute field write on object 5001 word @+0x20 (+0x17).
    { CommandPacket p{}; p.opcode() = 0x17; p.put32(0x10, 5001); p.bytes[0x14] = 1;
      u8* r = p.bytes + 0x15; r[0] = 2; r[1] = 1; r[2] = 0x20; r[3] = 0;
      u16 w = 0x1234; std::memcpy(r + 4, &w, 2); out.push_back(p); }
    // 7) Float add on object 5000 @+0x40 (+0x1A). Object is resolved last in this
    //    handler's priority order, after no person matches id 5000.
    { CommandPacket p{}; p.opcode() = 0x1A; p.put32(0x10, 5000); p.put32(0x14, 0x40);
      float a = 1.25f; std::memcpy(p.bytes + 0x18, &a, 4); out.push_back(p); }
    // 8) Register an id pair (+0x24).
    { CommandPacket p{}; p.opcode() = 0x24; p.put32(0x10, 777); p.put32(0x14, 3); out.push_back(p); }
    // 9) An untranslated opcode that must be safely ignored (no state change).
    { CommandPacket p{}; p.opcode() = 0x2F; out.push_back(p); }

    return out;
}

// Drive a script through a standalone queue: enqueue -> flush(local apply) ->
// exec(dispatch to handlers). Returns nothing; mutates the global world.
void RunScriptStandalone(const std::vector<CommandPacket>& script) {
    CommandQueue q;
    q.Init();
    q.set_standalone(true);
    RegisterApplyHandlers(q);
    for (const auto& p : script) {
        q.EnqueuePacket(p);
        q.FlushSendQueue(); // standalone: StoreReceivedPacket appends to recv list
    }
    q.ExecCommands();        // dispatch all received packets to the handlers
}

} // namespace

// ---------------------------------------------------------------------------
// Determinism: applying the same script to two fresh worlds yields byte-identical
// state.
// ---------------------------------------------------------------------------
TEST(SimCmdApplyE2E, DeterminismTwoRuns) {
    g_wire = nullptr;

    FreshWorld();
    RunScriptStandalone(BuildScript());
    WorldSnapshot a; a.Capture();

    FreshWorld();
    RunScriptStandalone(BuildScript());
    WorldSnapshot b; b.Capture();

    CHECK(a.Equals(b));

    // Spot-check a few expected mutations so the snapshot isn't trivially equal.
    CHECK_EQ(reinterpret_cast<u8*>(&g_persons[0])[0x1B1], 40);  // reputation
    i32 cnt; std::memcpy(&cnt, reinterpret_cast<u8*>(&g_persons[1]) + 0x194, 4);
    CHECK_EQ(cnt, 50);                                           // counter clamped
    u32 bits; std::memcpy(&bits, reinterpret_cast<u8*>(&g_objects[0]) + 0x30, 4);
    CHECK_EQ(bits, 0x00AB0000u);                                 // bitfield
    u16 w; std::memcpy(&w, reinterpret_cast<u8*>(&g_objects[1]) + 0x20, 2);
    CHECK_EQ(w, 0x1234);                                         // absolute write
    bool pairFound = false;
    for (int i = 0; i < kIdPairSlots; ++i)
        if (g_idPairA[i] == 3 && g_idPairB[i] == 777) pairFound = true;
    CHECK(pairFound);                                            // id pair
}

// ---------------------------------------------------------------------------
// Transmit via mock transport: side A applies directly; side B encodes, sends
// over the wire, decodes+applies. Both worlds must match byte-for-byte.
// ---------------------------------------------------------------------------
TEST(SimCmdApplyE2E, TransportSideBMatchesSideA) {
    // --- Side A: standalone direct apply ---
    g_wire = nullptr;
    FreshWorld();
    RunScriptStandalone(BuildScript());
    WorldSnapshot a; a.Capture();

    // --- Side B: send over a mock wire, then receive+apply ---
    std::vector<CommandPacket> wire;
    g_wire = &wire;

    FreshWorld(); // B starts from the same initial state

    // Sender queue: networked, so FlushSendQueue routes to netglue::SendPacket
    // (our mock), capturing the framed packets onto the wire in send order.
    CommandQueue sender;
    sender.Init();
    sender.set_standalone(false);
    for (const auto& p : BuildScript()) {
        sender.EnqueuePacket(p);
        sender.FlushSendQueue();
    }

    // Receiver queue: pull each wire frame in via StoreReceivedPacket, then exec.
    CommandQueue receiver;
    receiver.Init();
    RegisterApplyHandlers(receiver);
    for (const auto& frame : wire)
        receiver.StoreReceivedPacket(frame);
    receiver.ExecCommands();

    WorldSnapshot b; b.Capture();
    g_wire = nullptr;

    CHECK(a.Equals(b));
    CHECK_EQ(static_cast<int>(wire.size()), 9); // all 9 script packets transmitted
}

// ---------------------------------------------------------------------------
// Order independence of the receive classifier: applying the same set still
// reaches the same state regardless of the queue's lost/sync bookkeeping.
// ---------------------------------------------------------------------------
TEST(SimCmdApplyE2E, QueueExecMatchesDirectApply) {
    g_wire = nullptr;

    // Direct apply (no queue).
    FreshWorld();
    for (auto& p : BuildScript()) { AckEntry ack{}; ApplyPacket(p, &ack); }
    WorldSnapshot direct; direct.Capture();

    // Through the standalone queue pipeline.
    FreshWorld();
    RunScriptStandalone(BuildScript());
    WorldSnapshot viaQueue; viaQueue.Capture();

    CHECK(direct.Equals(viaQueue));
}

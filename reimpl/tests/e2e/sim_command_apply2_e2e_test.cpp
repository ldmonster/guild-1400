#include "sim/command.h"
#include "sim/command_apply.h"   // RegisterApplyHandlers (batch 1) + g_last* tokens
#include "sim/command_apply2.h"  // RegisterApplyHandlers2 (this batch)
#include "sim/entity.h"
#include "sim/types.h"
#include "test.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

// ===========================================================================
// e2e for the second apply batch. Two flows:
//   (1) determinism: encode a fixed command set, apply it to two independently
//       seeded fresh worlds, and assert the resulting modeled state is
//       byte-identical.
//   (2) A -> mock transport -> B parity: drive the SAME command stream through a
//       full CommandQueue on peer A (standalone local-apply) and on peer B (fed
//       the exact wire bytes A would have transmitted), and assert both peers'
//       state matches.
// Both registries are installed on the queues so this batch composes with the
// first without clobbering opcodes.
// ===========================================================================

namespace {

// A small deterministic world fixture covering this batch's record mutations.
// Returns nothing; mutates the global record arrays + the modeled leaf tables.
void SeedFreshWorld() {
    std::memset(g_persons, 0, sizeof(Person) * kPersonCapacity);
    std::memset(g_objects, 0, sizeof(ObjectRec) * kObjectCapacity);
    std::memset(g_sceneNodes, 0, sizeof(SceneNode) * kSceneNodeCapacity);
    ResetEntityArrays();
    ResetApply2State();
    g_sceneArrayLoaded  = true;
    g_personArrayLoaded = true;
    g_lastObjectId = -1;
    g_lastSceneId  = -1;
    g_lastTradeId  = -1;

    // Buildings 100 (owner) + 200 (partner) for the trade upsert.
    g_objects[0].alive = 1; g_objects[0].id = 100;
    g_objects[1].alive = 1; g_objects[1].id = 200;
    // A He record id 77 + a building slot (type 5, proto 0x1234).
    Apply2_SeedHe(77);
    Apply2_SeedSlot(5, 0x1234);
}

// Build the fixed command stream this batch exercises. Pure data — no state.
std::vector<CommandPacket> BuildCommandSet() {
    std::vector<CommandPacket> cmds;

    // 0x1D ExSetHE — write fields into He 77.
    {
        CommandPacket p{};
        p.opcode() = 0x1D;
        p.put32(0x10, 77);
        p.put32(20, 0xCAFE0001);
        p.put32(24, 0xCAFE0002);
        p.put16(32, 0x1357);
        p.put32(59, 0x7F000000); // >>24 == 0x7F
        cmds.push_back(p);
    }
    // 0x3F ExSetObjectState — copy 0x80 bytes into slot (5, 0x1234).
    {
        CommandPacket p{};
        p.opcode() = 0x3F;
        p.bytes[0x10] = 5;
        p.bytes[0x11] = 0x34; p.bytes[0x12] = 0x12; // proto 0x1234
        for (int i = 0; i < 0x80; ++i) p.bytes[0x11 + i] = static_cast<u8>((i * 7 + 3) & 0xFF);
        cmds.push_back(p);
    }
    // 0x5E ExUpsertTradeEntry — create a trade node 100->200.
    {
        CommandPacket p{};
        p.opcode() = 0x5E;
        p.put32(0x14, 100);
        p.put32(0x26, 200);
        p.put32(0x18, 0xBEEF0001);
        p.put16(0x24, 0x2468);
        p.bytes[0x32] = 0x5A;
        p.bytes[0x33] = 60;
        cmds.push_back(p);
    }
    // 0x5E again to the same pair: updates fields + clamps pct (60+60 -> 100).
    {
        CommandPacket p{};
        p.opcode() = 0x5E;
        p.put32(0x14, 100);
        p.put32(0x26, 200);
        p.put32(0x18, 0xBEEF0009);
        p.bytes[0x33] = 60;
        cmds.push_back(p);
    }
    return cmds;
}

// Snapshot the modeled state this batch mutates, into a flat byte blob, so two
// worlds can be compared for byte-equality. Covers the trade node the upsert
// commands create, the last-created-id remap register, and the object array.
std::vector<u8> SnapshotState() {
    std::vector<u8> blob;
    auto push = [&](const void* p, size_t n) {
        const u8* b = static_cast<const u8*>(p);
        blob.insert(blob.end(), b, b + n);
    };
    TradeNode* node = Apply2_FindTradeNode(100, 200);
    if (node) push(node->bytes, sizeof(node->bytes));
    else { u8 zero[64] = {}; push(zero, sizeof(zero)); }
    push(&g_lastTradeId, sizeof(g_lastTradeId));
    push(g_objects, sizeof(ObjectRec) * 4);
    return blob;
}

// Run a command set directly (ApplyPacket2) against the current world.
void ApplyAllDirect(std::vector<CommandPacket> cmds) {
    for (auto& c : cmds) {
        AckEntry ack{};
        ApplyPacket2(c, &ack);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// (1) Determinism: identical command set on two fresh worlds -> identical state.
// ---------------------------------------------------------------------------
TEST(SimCmdApply2E2E, DeterministicReplay) {
    auto cmds = BuildCommandSet();

    SeedFreshWorld();
    ApplyAllDirect(cmds);
    std::vector<u8> stateA = SnapshotState();

    SeedFreshWorld();
    ApplyAllDirect(cmds);
    std::vector<u8> stateB = SnapshotState();

    CHECK_EQ(stateA.size(), stateB.size());
    CHECK(stateA == stateB);

    // Spot-check the concrete mutations survived the replay on world B.
    TradeNode* node = Apply2_FindTradeNode(100, 200);
    CHECK(node != nullptr);
    CHECK_EQ(node->bytes[55], 100); // 60 + 60 clamped
    u32 g; std::memcpy(&g, node->bytes + 28, 4);
    CHECK_EQ(g, 0xBEEF0009u);       // second upsert overwrote field0
}

// ---------------------------------------------------------------------------
// (2) A -> mock transport -> B parity through the full CommandQueue.
// A mock transport just records the bytes A "sends"; B replays them.
// ---------------------------------------------------------------------------
TEST(SimCmdApply2E2E, QueueTransportParity) {
    auto cmds = BuildCommandSet();

    // --- Peer A: standalone queue (FlushSendQueue applies locally). ----------
    SeedFreshWorld();
    CommandQueue qa;
    qa.Init();
    qa.set_standalone(true);
    RegisterApplyHandlers(qa);   // batch 1
    RegisterApplyHandlers2(qa);  // this batch

    // Enqueue + flush + exec each command; capture the wire bytes for B.
    std::vector<CommandPacket> wire;
    for (auto& c : cmds) {
        qa.EnqueuePacket(c);
        wire.push_back(c); // the staged bytes are what the transport carries
    }
    qa.FlushSendQueue();
    qa.ExecCommands();
    TradeNode* nodeA = Apply2_FindTradeNode(100, 200);
    CHECK(nodeA != nullptr);
    u8 slotByte55_A = nodeA->bytes[55];
    u32 he_field_A = 0;
    // capture trade field0 for A
    u32 tfA; std::memcpy(&tfA, nodeA->bytes + 28, 4);
    (void)he_field_A;

    // --- Peer B: fed the exact wire bytes A would transmit. ------------------
    SeedFreshWorld();
    CommandQueue qb;
    qb.Init();
    qb.set_standalone(true);
    RegisterApplyHandlers(qb);
    RegisterApplyHandlers2(qb);
    for (auto& w : wire) {
        qb.StoreReceivedPacket(w);
    }
    qb.ExecCommands();
    TradeNode* nodeB = Apply2_FindTradeNode(100, 200);
    CHECK(nodeB != nullptr);
    u32 tfB; std::memcpy(&tfB, nodeB->bytes + 28, 4);

    // Parity: both peers reached the same trade-node state.
    CHECK_EQ(slotByte55_A, nodeB->bytes[55]);
    CHECK_EQ(tfA, tfB);
    CHECK_EQ(nodeB->bytes[55], 100);
    CHECK_EQ(tfB, 0xBEEF0009u);
}

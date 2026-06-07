#include "sim/command.h"
#include "sim/command_apply.h"   // RegisterApplyHandlers (batch 1)
#include "sim/command_apply2.h"  // RegisterApplyHandlers2 (batch 2)
#include "sim/command_apply3.h"  // RegisterApplyHandlers3 (this batch)
#include "sim/entity.h"
#include "sim/types.h"
#include "test.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

// ===========================================================================
// e2e for the THIRD apply batch. Two flows:
//   (1) determinism: encode a fixed command set, apply it to two independently
//       seeded fresh worlds, and assert the resulting modeled state is
//       byte-identical.
//   (2) A -> mock transport -> B parity: drive the SAME command stream through a
//       full CommandQueue on peer A (standalone local-apply) and on peer B (fed
//       the exact wire bytes A would have transmitted), and assert both peers'
//       state matches.
// All three registries are installed so this batch composes with batches 1+2
// without clobbering opcodes.
// ===========================================================================

namespace {

constexpr i32 kLocalPlayer = 42;

void SeedFreshWorld() {
    std::memset(g_persons, 0, sizeof(Person) * kPersonCapacity);
    std::memset(g_objects, 0, sizeof(ObjectRec) * kObjectCapacity);
    std::memset(g_sceneNodes, 0, sizeof(SceneNode) * kSceneNodeCapacity);
    ResetEntityArrays();
    ResetApply3State();
    g_sceneArrayLoaded  = true;
    g_personArrayLoaded = true;
    g_lastObjectId = -1;
    g_lastSceneId  = -1;
    g_lastTradeId  = -1;

    Apply3_SetLocalPlayerId(kLocalPlayer);

    // Two persons (100, 200) for the cutscene-ready stamp.
    g_persons[0].marker = 0; g_persons[0].kind = 4; g_persons[0].id = 100; g_personIds[0] = 100;
    g_persons[1].marker = 0; g_persons[1].kind = 4; g_persons[1].id = 200; g_personIds[1] = 200;
    std::memset(reinterpret_cast<u8*>(&g_persons[0]) + kPfCutsceneId, 0xFF, 4); // -1
    std::memset(reinterpret_cast<u8*>(&g_persons[1]) + kPfCutsceneId, 0xFF, 4); // -1

    // A combat unit (500) for the damage apply.
    Apply3_Combat().Spawn(/*id=*/500, /*hp=*/100, /*team=*/1);
}

// Build the fixed command stream this batch exercises. Pure data — no state.
std::vector<CommandPacket> BuildCommandSet() {
    std::vector<CommandPacket> cmds;

    // 0x27 ExAllocCutsceneWithId — alloc cutscene slot id 33, set field.
    {
        CommandPacket p{};
        p.opcode() = kOp3AllocCutsceneWithId;
        p.put32(0x10, 33);
        p.put32(0x14, 0xABCDEF01);
        cmds.push_back(p);
    }
    // 0x29 ExSetCutsceneState — add participant 100 to slot 33.
    {
        CommandPacket p{};
        p.opcode() = kOp3SetCutsceneState;
        p.put32(0x10, 33);
        p.put32(0x14, 100);
        cmds.push_back(p);
    }
    // 0x29 again — add participant 200.
    {
        CommandPacket p{};
        p.opcode() = kOp3SetCutsceneState;
        p.put32(0x10, 33);
        p.put32(0x14, 200);
        cmds.push_back(p);
    }
    // 0x5F ExSetObjectField — set slot 33's master.
    {
        CommandPacket p{};
        p.opcode() = kOp3SetObjectField;
        p.put32(0x10, 33);
        p.put32(0x14, 0x0BADF00D);
        cmds.push_back(p);
    }
    // 0x59 ExCutsceneReady — stamp the cutscene id onto persons 100 + 200.
    {
        CommandPacket p{};
        p.opcode() = kOp3CutsceneReady;
        p.put32(0x10, 33);   // slot id (no person 33)
        p.put32(0x14, 100);
        p.put32(0x18, 200);
        p.put32(0x1C, -1);
        cmds.push_back(p);
    }
    // 0x52 ExApplyCombatDamage — 30 damage to unit 500.
    {
        CommandPacket p{};
        p.opcode() = kOp3ApplyCombatDamage;
        p.put32(0x10, 500);
        p.put32(0x14, 30);
        p.bytes[0x18] = 1;
        cmds.push_back(p);
    }
    // 0x52 again — another 25 damage, mark dead.
    {
        CommandPacket p{};
        p.opcode() = kOp3ApplyCombatDamage;
        p.put32(0x10, 500);
        p.put32(0x14, 25);
        p.bytes[0x18] = 0;
        cmds.push_back(p);
    }
    // 0x4B ExAppendChatLine — chat to the local player.
    {
        CommandPacket p{};
        p.opcode() = kOp3AppendChatLine;
        p.put32(0x18, kLocalPlayer); // targets local player
        const char* msg = "gg";
        std::memcpy(p.bytes + 0x30, msg, std::strlen(msg) + 1);
        cmds.push_back(p);
    }
    return cmds;
}

// Snapshot the modeled state this batch mutates into a flat byte blob.
std::vector<u8> SnapshotState() {
    std::vector<u8> blob;
    auto push = [&](const void* p, size_t n) {
        const u8* b = static_cast<const u8*>(p);
        blob.insert(blob.end(), b, b + n);
    };
    // cutscene slot 33 (full 276 bytes) or zeros.
    CutsceneSlot* s = Apply3_Cutscenes().FindById(33);
    if (s) push(s, sizeof(CutsceneSlot));
    else  { std::vector<u8> z(sizeof(CutsceneSlot), 0); push(z.data(), z.size()); }
    // persons 100 + 200 cutscene-id field.
    push(reinterpret_cast<u8*>(&g_persons[0]) + kPfCutsceneId, 4);
    push(reinterpret_cast<u8*>(&g_persons[1]) + kPfCutsceneId, 4);
    // combat unit 500 hp + alive.
    CombatUnit* u = Apply3_Combat().FindUnitById(500);
    if (u) { push(&u->hp, 4); push(&u->alive, 1); }
    else   { u8 z[5] = {}; push(z, 5); }
    // chat buffer.
    const char* chat = Apply3_ChatBuffer();
    push(chat, std::strlen(chat) + 1);
    return blob;
}

void ApplyAllDirect(const std::vector<CommandPacket>& cmds) {
    for (auto& c : cmds) {
        CommandPacket copy = c;        // handlers may remap in place
        AckEntry ack{};
        ApplyPacket3(copy, &ack);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// (1) Determinism: identical command set on two fresh worlds -> identical state.
// ---------------------------------------------------------------------------
TEST(SimCmdApply3E2E, DeterministicReplay) {
    auto cmds = BuildCommandSet();

    SeedFreshWorld();
    ApplyAllDirect(cmds);
    std::vector<u8> stateA = SnapshotState();

    SeedFreshWorld();
    ApplyAllDirect(cmds);
    std::vector<u8> stateB = SnapshotState();

    CHECK_EQ(stateA.size(), stateB.size());
    CHECK(stateA == stateB);

    // Concrete spot-checks on world B.
    CutsceneSlot* s = Apply3_Cutscenes().FindById(33);
    CHECK(s != nullptr);
    CHECK_EQ((int)s->partCount, 3); // 1 (alive gate) + 2 participants
    CHECK_EQ(s->partIds[1], 100);
    CHECK_EQ(s->partIds[2], 200);
    CHECK_EQ(s->master, 0x0BADF00D);

    i32 csA; std::memcpy(&csA, reinterpret_cast<u8*>(&g_persons[0]) + kPfCutsceneId, 4);
    CHECK_EQ(csA, 33);

    CombatUnit* u = Apply3_Combat().FindUnitById(500);
    CHECK(u != nullptr);
    CHECK_EQ(u->hp, 45);        // 100 - 30 - 25
    CHECK_EQ((int)u->alive, 0); // second damage marked dead

    CHECK(std::strcmp(Apply3_ChatBuffer(), "gg") == 0);
}

// ---------------------------------------------------------------------------
// (2) A -> mock transport -> B parity through the full CommandQueue.
// ---------------------------------------------------------------------------
TEST(SimCmdApply3E2E, QueueTransportParity) {
    auto cmds = BuildCommandSet();

    // --- Peer A: standalone queue (FlushSendQueue applies locally). ----------
    SeedFreshWorld();
    CommandQueue qa;
    qa.Init();
    qa.set_standalone(true);
    RegisterApplyHandlers(qa);
    RegisterApplyHandlers2(qa);
    RegisterApplyHandlers3(qa);

    std::vector<CommandPacket> wire;
    for (auto& c : cmds) {
        qa.EnqueuePacket(c);
        wire.push_back(c); // the staged bytes are what the transport carries
    }
    qa.FlushSendQueue();
    qa.ExecCommands();
    std::vector<u8> stateA = SnapshotState();

    // --- Peer B: fed the exact wire bytes A would transmit. ------------------
    SeedFreshWorld();
    CommandQueue qb;
    qb.Init();
    qb.set_standalone(true);
    RegisterApplyHandlers(qb);
    RegisterApplyHandlers2(qb);
    RegisterApplyHandlers3(qb);
    for (auto& w : wire)
        qb.StoreReceivedPacket(w);
    qb.ExecCommands();
    std::vector<u8> stateB = SnapshotState();

    // Parity: both peers reached the same state.
    CHECK_EQ(stateA.size(), stateB.size());
    CHECK(stateA == stateB);

    CombatUnit* u = Apply3_Combat().FindUnitById(500);
    CHECK(u != nullptr);
    CHECK_EQ(u->hp, 45);
    CHECK(std::strcmp(Apply3_ChatBuffer(), "gg") == 0);
}

// ---------------------------------------------------------------------------
// (3) Byte-identical re-encode: applying twice through fresh queues yields the
//     same snapshot (encode->apply twice -> byte-identical).
// ---------------------------------------------------------------------------
TEST(SimCmdApply3E2E, EncodeApplyTwiceByteIdentical) {
    auto cmds = BuildCommandSet();

    auto runThroughQueue = [&]() {
        SeedFreshWorld();
        CommandQueue q;
        q.Init();
        q.set_standalone(true);
        RegisterApplyHandlers3(q);
        for (auto& c : cmds) q.EnqueuePacket(c);
        q.FlushSendQueue();
        q.ExecCommands();
        return SnapshotState();
    };

    std::vector<u8> first  = runThroughQueue();
    std::vector<u8> second = runThroughQueue();
    CHECK_EQ(first.size(), second.size());
    CHECK(first == second);
}

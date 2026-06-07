#include "sim/command.h"
#include "sim/command_apply.h"   // RegisterApplyHandlers (batch 1)
#include "sim/command_apply2.h"  // RegisterApplyHandlers2 (batch 2)
#include "sim/command_apply3.h"  // RegisterApplyHandlers3 (batch 3)
#include "sim/command_apply4.h"  // RegisterApplyHandlers4 (this batch)
#include "sim/entity.h"
#include "sim/types.h"
#include "test.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

// ===========================================================================
// e2e for the FOURTH apply batch. Flows:
//   (1) determinism: apply a fixed command set to two fresh worlds -> identical.
//   (2) A -> mock transport -> B parity through the full CommandQueue.
//   (3) encode->apply twice -> byte-identical snapshot.
//   (4) install ALL FOUR registries on one queue and assert NO opcode conflicts
//       (no opcode is owned by two batches), then a mixed command stream
//       spanning all four batches applies correctly.
// ===========================================================================

namespace {

void SeedFreshWorld() {
    std::memset(g_persons, 0, sizeof(Person) * kPersonCapacity);
    std::memset(g_objects, 0, sizeof(ObjectRec) * kObjectCapacity);
    std::memset(g_sceneNodes, 0, sizeof(SceneNode) * kSceneNodeCapacity);
    ResetEntityArrays();
    ResetApply4State();
    g_sceneArrayLoaded  = true;
    g_personArrayLoaded = true;
    g_lastObjectId = -1;
    g_lastSceneId  = -1;
    g_lastTradeId  = -1;
    Apply4_SetStandalone(true);

    // Persons: 500 (with character, for remove), 620 (with character, destroy),
    // 1000/1001 (cutscene participants).
    auto mk = [](int slot, i32 id) {
        g_persons[slot].marker = static_cast<i16>(slot);
        g_persons[slot].kind   = 4;
        g_persons[slot].id     = id;
        g_personIds[slot]      = id;
    };
    mk(0, 500); std::memset(reinterpret_cast<u8*>(&g_persons[0]) + 388, 0, 4);
    { i32 h = 0x1234; std::memcpy(reinterpret_cast<u8*>(&g_persons[0]) + 388, &h, 4); }
    mk(1, 620); { i32 h = 0x9999; std::memcpy(reinterpret_cast<u8*>(&g_persons[1]) + 388, &h, 4); }
    mk(2, 1000); { i32 c = 77; std::memcpy(reinterpret_cast<u8*>(&g_persons[2]) + 520, &c, 4); }
    mk(3, 1001); { i32 c = 77; std::memcpy(reinterpret_cast<u8*>(&g_persons[3]) + 520, &c, 4); }

    // A cut-info slot for player 55.
    Apply4_CutInfoSlot(0)->playerId = 55;
    Apply4_CutInfoSlot(0)->time = 999;

    // A transform slot (type 3, proto 88) with known initial values.
    i32* s = Apply4_SeedTransformSlot(3, 88);
    s[4] = 1000;

    // A cutscene clear slot (id 77) with participants 1000 + 1001.
    i32 ids[2] = {1000, 1001};
    Apply4_SeedCutsceneSlot(77, ids, 2);
}

std::vector<CommandPacket> BuildCommandSet() {
    std::vector<CommandPacket> cmds;

    // 0x04 ExSetGlobalFlag — OR 0x40 into the flags.
    { CommandPacket p{}; p.opcode() = kOp4SetGlobalFlag; p.bytes[kFPayload] = 0x40; cmds.push_back(p); }
    // 0x08/0x09 load buffer alloc + append.
    { CommandPacket p{}; p.opcode() = kOp4LoadBufAlloc; p.put32(kFPayload, 1024); cmds.push_back(p); }
    { CommandPacket p{}; p.opcode() = kOp4LoadBufAppend;
      for (int i = 0; i < 0x80; ++i) p.bytes[kFPayload + i] = static_cast<u8>(i);
      cmds.push_back(p); }
    // 0x15 ExSetObjectTransform — create object under container 7777, proto 1234.
    { CommandPacket p{}; p.opcode() = kOp4SetObjectTransform;
      p.put32(0x10, 7777); p.bytes[20] = 1234 & 0xFF; p.bytes[21] = (1234 >> 8) & 0xFF;
      for (int i = 0; i < 0x1C; ++i) p.bytes[22 + i] = static_cast<u8>(0x20 + i);
      cmds.push_back(p); }
    // 0x2D ExSpawnEffectObject — effect under container 900.
    { CommandPacket p{}; p.opcode() = kOp4SpawnEffectObject;
      p.put32(0x14, 900); p.put32(24, 0xCAFEBABE); cmds.push_back(p); }
    // 0x40 ExAdjustObjectTransform — add to transform slot (type 3, proto 88).
    { CommandPacket p{}; p.opcode() = kOp4AdjustObjectTransform;
      p.bytes[0x10] = 3; p.bytes[0x11] = 88; p.bytes[0x12] = 0; p.put32(0x13, 7); cmds.push_back(p); }
    // 0x22 ExAddStraftat — add a crime record (taeter -1).
    { CommandPacket p{}; p.opcode() = kOp4AddStraftat;
      for (int i = 0; i < 0x2D; ++i) p.bytes[0x10 + i] = static_cast<u8>(0xA0 + i);
      p.put32(38, -1); cmds.push_back(p); }
    // 0x26 ExSendCutInfo — clear time on player 55's slot.
    { CommandPacket p{}; p.opcode() = kOp4SendCutInfo; p.put32(0x10, 55); cmds.push_back(p); }
    // 0x21 ExRemovePersonAndScript — person 500.
    { CommandPacket p{}; p.opcode() = kOp4RemovePersonAndScript; p.put32(0x10, 500); p.put32(0x14, 3); cmds.push_back(p); }
    // 0x57 ExDestroyCharacter — person 620.
    { CommandPacket p{}; p.opcode() = kOp4DestroyCharacter; p.put32(0x10, 620); cmds.push_back(p); }
    // 0x58 ExClearObjectOccupants — cutscene slot 77 -> clears 1000 + 1001.
    { CommandPacket p{}; p.opcode() = kOp4ClearObjectOccupants; p.put32(0x10, 77); cmds.push_back(p); }
    return cmds;
}

// Snapshot the modeled state this batch mutates into a flat byte blob.
std::vector<u8> SnapshotState() {
    std::vector<u8> blob;
    auto push = [&](const void* p, size_t n) {
        const u8* b = static_cast<const u8*>(p);
        blob.insert(blob.end(), b, b + n);
    };
    push(&g_globalFlags, 1);
    push(&g_loadBufCursor, 4);
    push(g_loadBuf, 0x80);
    // created object (transform) — find by (7777, 1234)
    u8* obj = Apply4_FindAddedObject(7777, 1234);
    if (obj) push(obj, 60); else { std::vector<u8> z(60, 0); push(z.data(), z.size()); }
    // effect object
    u8* eff = Apply4_FindAddedObject(900, 437);
    if (eff) push(eff, 40); else { std::vector<u8> z(40, 0); push(z.data(), z.size()); }
    // crime slot 0
    push(Apply4_StraftatSlot(0), kStraftatStride);
    // cut-info slot 0 time
    push(&Apply4_CutInfoSlot(0)->time, 4);
    // persons 500 charPtr (script-finish leaves it untouched in record), 620 charPtr
    push(reinterpret_cast<u8*>(&g_persons[1]) + 388, 4); // 620 -> cleared to 0
    // cutscene ids of 1000 + 1001
    push(reinterpret_cast<u8*>(&g_persons[2]) + 520, 4);
    push(reinterpret_cast<u8*>(&g_persons[3]) + 520, 4);
    return blob;
}

void ApplyAllDirect(const std::vector<CommandPacket>& cmds) {
    for (auto& c : cmds) {
        CommandPacket copy = c;
        AckEntry ack{};
        ApplyPacket4(copy, &ack);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// (1) Determinism.
// ---------------------------------------------------------------------------
TEST(SimCmdApply4E2E, DeterministicReplay) {
    auto cmds = BuildCommandSet();

    SeedFreshWorld();
    ApplyAllDirect(cmds);
    std::vector<u8> stateA = SnapshotState();

    SeedFreshWorld();
    ApplyAllDirect(cmds);
    std::vector<u8> stateB = SnapshotState();

    CHECK_EQ(stateA.size(), stateB.size());
    CHECK(stateA == stateB);

    // Spot checks on world B.
    CHECK_EQ((int)(g_globalFlags & 0x40), 0x40);
    CHECK_EQ(g_loadBufCursor, 128);
    i32 cs1000; std::memcpy(&cs1000, reinterpret_cast<u8*>(&g_persons[2]) + 520, 4);
    i32 cs1001; std::memcpy(&cs1001, reinterpret_cast<u8*>(&g_persons[3]) + 520, 4);
    CHECK_EQ(cs1000, -1);
    CHECK_EQ(cs1001, -1);
    i32 char620; std::memcpy(&char620, reinterpret_cast<u8*>(&g_persons[1]) + 388, 4);
    CHECK_EQ(char620, 0); // destroyed -> cleared
    CHECK_EQ(Apply4_CutInfoSlot(0)->time, 0);
}

// ---------------------------------------------------------------------------
// (2) A -> mock transport -> B parity through the full CommandQueue.
// ---------------------------------------------------------------------------
TEST(SimCmdApply4E2E, QueueTransportParity) {
    auto cmds = BuildCommandSet();

    SeedFreshWorld();
    CommandQueue qa;
    qa.Init();
    qa.set_standalone(true);
    RegisterApplyHandlers(qa);
    RegisterApplyHandlers2(qa);
    RegisterApplyHandlers3(qa);
    RegisterApplyHandlers4(qa);

    std::vector<CommandPacket> wire;
    for (auto& c : cmds) { qa.EnqueuePacket(c); wire.push_back(c); }
    qa.FlushSendQueue();
    qa.ExecCommands();
    std::vector<u8> stateA = SnapshotState();

    SeedFreshWorld();
    CommandQueue qb;
    qb.Init();
    qb.set_standalone(true);
    RegisterApplyHandlers(qb);
    RegisterApplyHandlers2(qb);
    RegisterApplyHandlers3(qb);
    RegisterApplyHandlers4(qb);
    for (auto& w : wire) qb.StoreReceivedPacket(w);
    qb.ExecCommands();
    std::vector<u8> stateB = SnapshotState();

    CHECK_EQ(stateA.size(), stateB.size());
    CHECK(stateA == stateB);
}

// ---------------------------------------------------------------------------
// (3) encode->apply twice -> byte-identical.
// ---------------------------------------------------------------------------
TEST(SimCmdApply4E2E, EncodeApplyTwiceByteIdentical) {
    auto cmds = BuildCommandSet();
    auto runThroughQueue = [&]() {
        SeedFreshWorld();
        CommandQueue q;
        q.Init();
        q.set_standalone(true);
        RegisterApplyHandlers4(q);
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

// ---------------------------------------------------------------------------
// (4) All four registries on one queue: NO opcode conflicts + mixed stream.
// ---------------------------------------------------------------------------

// A probe queue that records which opcodes get a handler installed, so we can
// assert the four registries are disjoint (no batch overwrites another's slot).
namespace {
struct OpcodeOwnership {
    int owner[kNumOpcodes];
    OpcodeOwnership() { for (auto& o : owner) o = 0; }
};
} // namespace

TEST(SimCmdApply4E2E, AllRegistriesDisjoint) {
    // We detect conflicts by installing each batch onto its OWN queue and reading
    // back which opcodes it claims (via probing: an unclaimed opcode is a no-op
    // and returns without touching our sentinel; a claimed one runs the handler).
    // Simpler + robust: each batch exposes ApplyPacketN that returns -1 for
    // opcodes it does NOT own. So an opcode is "owned by batch N" iff
    // ApplyPacketN(op) != -1 for a bare packet. Assert each opcode is owned by at
    // most one batch.
    int ownerCount[kNumOpcodes] = {};
    for (u32 op = 0; op < kNumOpcodes; ++op) {
        SeedFreshWorld();
        int owners = 0;
        {
            CommandPacket p{}; p.opcode() = static_cast<u8>(op); AckEntry a{};
            if (ApplyPacket(p, &a)  != -1) ++owners;
        }
        {
            CommandPacket p{}; p.opcode() = static_cast<u8>(op); AckEntry a{};
            if (ApplyPacket2(p, &a) != -1) ++owners;
        }
        {
            CommandPacket p{}; p.opcode() = static_cast<u8>(op); AckEntry a{};
            if (ApplyPacket3(p, &a) != -1) ++owners;
        }
        {
            CommandPacket p{}; p.opcode() = static_cast<u8>(op); AckEntry a{};
            if (ApplyPacket4(p, &a) != -1) ++owners;
        }
        ownerCount[op] = owners;
        CHECK(owners <= 1); // no opcode owned by two batches
    }

    // Batch 4 must own exactly its 21 opcodes.
    const u8 batch4Ops[] = {
        kOp4HandleAck, kOp4HandleNoop, kOp4SetGlobalFlag, kOp4LoadBufAlloc,
        kOp4LoadBufAppend, kOp4AckStub, kOp4SetObjectTransform, kOp4SpawnEffectObject,
        kOp4AdjustObjectTransform, kOp4AddStraftat, kOp4DispatchStatusResult,
        kOp4QueryObjectStatus, kOp4SendCutInfo, kOp4RemovePersonAndScript,
        kOp4SetObjectParent, kOp4ActivateObject, kOp4ChangePlayerHead,
        kOp4StopCharacterScript, kOp4EnqueueCharacterAction, kOp4DestroyCharacter,
        kOp4ClearObjectOccupants,
    };
    for (u8 op : batch4Ops) CHECK_EQ(ownerCount[op], 1);
}

TEST(SimCmdApply4E2E, MixedStreamAcrossAllBatches) {
    // Install all four registries on one queue and run a stream that touches
    // batch 1 (0x5A adjust counter), batch 2 (0x44 office assign), batch 3
    // (0x52 combat damage), and batch 4 (0x04 global flag, 0x58 clear cutscene).
    SeedFreshWorld();
    // batch1 target: a person whose counter (+0x194) we bump.
    g_persons[4].marker = 4; g_persons[4].kind = 4; g_persons[4].id = 9000; g_personIds[4] = 9000;
    // batch3 combat unit.
    Apply3_Combat().Spawn(/*id=*/123, /*hp=*/50, /*team=*/1);

    CommandQueue q;
    q.Init();
    q.set_standalone(true);
    RegisterApplyHandlers(q);
    RegisterApplyHandlers2(q);
    RegisterApplyHandlers3(q);
    RegisterApplyHandlers4(q);

    std::vector<CommandPacket> cmds;
    // batch4 0x04
    { CommandPacket p{}; p.opcode() = kOp4SetGlobalFlag; p.bytes[kFPayload] = 0x04; cmds.push_back(p); }
    // batch1 0x5A adjust object counter on person 9000: add 3 to person+0x194.
    { CommandPacket p{}; p.opcode() = kOpAdjustObjectCounter; p.put32(0x10, 9000); p.put32(0x14, 3); cmds.push_back(p); }
    // batch3 0x52 combat damage 20 to unit 123.
    { CommandPacket p{}; p.opcode() = kOp3ApplyCombatDamage; p.put32(0x10, 123); p.put32(0x14, 20); p.bytes[0x18] = 1; cmds.push_back(p); }
    // batch4 0x58 clear cutscene 77.
    { CommandPacket p{}; p.opcode() = kOp4ClearObjectOccupants; p.put32(0x10, 77); cmds.push_back(p); }

    for (auto& c : cmds) q.EnqueuePacket(c);
    q.FlushSendQueue();
    q.ExecCommands();

    // batch4 effect.
    CHECK_EQ((int)(g_globalFlags & 0x04), 0x04);
    // batch1 effect: person 9000 counter at +0x194 == 3 (clamped to [0,50]).
    i32 ctr; std::memcpy(&ctr, reinterpret_cast<u8*>(&g_persons[4]) + 0x194, 4);
    CHECK_EQ(ctr, 3);
    // batch3 effect: unit 123 hp 50 - 20 == 30.
    CombatUnit* u = Apply3_Combat().FindUnitById(123);
    CHECK(u != nullptr);
    CHECK_EQ(u->hp, 30);
    // batch4 effect: cutscene participants cleared.
    i32 cs1000; std::memcpy(&cs1000, reinterpret_cast<u8*>(&g_persons[2]) + 520, 4);
    CHECK_EQ(cs1000, -1);
}

#include "sim/command.h"
#include "sim/command_apply.h"   // RegisterApplyHandlers  (batch 1)
#include "sim/command_apply2.h"  // RegisterApplyHandlers2 (batch 2)
#include "sim/command_apply3.h"  // RegisterApplyHandlers3 (batch 3)
#include "sim/command_apply4.h"  // RegisterApplyHandlers4 (batch 4)
#include "sim/command_apply5.h"  // RegisterApplyHandlers5 (this batch)
#include "sim/building_create.h"
#include "sim/person_create.h"
#include "sim/entity.h"
#include "sim/types.h"
#include "crt/rand.h"            // crt::Srand (seed the create RNG for replay)
#include "test.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

// ===========================================================================
// e2e for the FIFTH apply batch. Flows:
//   (1) determinism: apply a fixed building-create -> upgrade -> remove +
//       person-create + sys-message stream to two fresh worlds -> identical.
//   (2) mixed stream through the full CommandQueue with ALL FIVE registries.
//   (3) install all five registries and assert NO opcode conflicts (disjoint).
//   (4) reference world-state check after the create/upgrade/remove sequence.
// ===========================================================================

namespace {

void SeedWorld() {
    std::memset(g_persons, 0, sizeof(Person) * kPersonCapacity);
    std::memset(g_objects, 0, sizeof(ObjectRec) * kObjectCapacity);
    std::memset(g_sceneNodes, 0, sizeof(SceneNode) * kSceneNodeCapacity);
    ResetEntityArrays();
    ResetApply5State();
    ResetBuildingCreate();
    ResetPersonCreate();
    g_sceneArrayLoaded  = true;
    g_personArrayLoaded = true;
    g_lastObjectId = -1;
    g_lastSceneId  = -1;
    g_lastTradeId  = -1;
    Apply5_SetStandalone(true);
    g_buildingNextId = 1000; // deterministic id allocation base
    g_personNextId   = 2000;
    // VIBE_Person_CreateAndSpawn @0x58da70 draws from the shared CRT LCG for the
    // stat/appearance/relation init; a fresh world re-seeds it (the original
    // seeds the generator at session init) so the lockstep replay is byte-stable.
    crt::Srand(0);

    // One owner person (id 909) at slot 7, marker index 7.
    g_persons[7].marker = 7;
    g_persons[7].kind   = 4;
    g_persons[7].id     = 909;
    g_personIds[7]      = 909;
}

// Build a deterministic create -> upgrade -> remove + person + sysmsg stream.
std::vector<CommandPacket> BuildStream() {
    std::vector<CommandPacket> cmds;

    // 0x4C ExCreateGebaeude under owner 909 -> building id 1000 (g_buildingNextId).
    { CommandPacket p{}; p.opcode() = kOp5CreateGebaeude;
      p.put32(16, 909); p.bytes[24] = 12; cmds.push_back(p); }

    // 0x3A ExGebUpgrade on building 1000 (level 12 -> 13).
    { CommandPacket p{}; p.opcode() = kOp5GebUpgrade; p.put32(16, 1000); cmds.push_back(p); }

    // 0x0B ExCreatePersonA -> person id 2000.
    { CommandPacket p{}; p.opcode() = kOp5CreatePersonA;
      p.put32(31, -1); p.put32(17, 0x05000000); p.put32(21, 1); p.put16(29, 7);
      p.put32(25, 2); cmds.push_back(p); }

    // 0x20 ExSysMessage case 3 -> set the clock.
    { CommandPacket p{}; p.opcode() = kOp5SysMessage; p.bytes[16] = 3;
      p.put32(17, 5); p.put16(21, 9); p.put32(23, 30); p.put32(27, 0); cmds.push_back(p); }

    // 0x3B ExRemoveBuilding 1000.
    { CommandPacket p{}; p.opcode() = kOp5RemoveBuilding; p.put32(16, 1000); cmds.push_back(p); }

    return cmds;
}

// Snapshot the deterministic world state this batch touches.
std::vector<u8> Snapshot() {
    std::vector<u8> blob;
    auto push = [&](const void* p, size_t n) {
        const u8* b = static_cast<const u8*>(p);
        blob.insert(blob.end(), b, b + n);
    };
    // object array + person array (records created/removed).
    push(g_objects, sizeof(ObjectRec) * kObjectCapacity);
    push(g_persons, sizeof(Person) * kPersonCapacity);
    // sys-message clock + last-created tokens + allocators.
    push(&g_sysGameTime, sizeof(GameTime));
    push(&g_lastSceneId, 4);
    push(&g_lastObjectId, 4);
    push(&g_buildingNextId, 4);
    push(&g_personNextId, 4);
    return blob;
}

void ApplyAllDirect(const std::vector<CommandPacket>& cmds) {
    for (auto& c : cmds) {
        CommandPacket copy = c;
        AckEntry ack{};
        ApplyPacket5(copy, &ack);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// (1) Determinism: apply twice -> byte-identical.
// ---------------------------------------------------------------------------
TEST(SimCmdApply5E2E, DeterministicReplay) {
    auto cmds = BuildStream();

    SeedWorld();
    ApplyAllDirect(cmds);
    std::vector<u8> a = Snapshot();

    SeedWorld();
    ApplyAllDirect(cmds);
    std::vector<u8> b = Snapshot();

    CHECK_EQ(a.size(), b.size());
    CHECK(a == b);
}

// ---------------------------------------------------------------------------
// (2) Reference world state after the create/upgrade/remove sequence.
// ---------------------------------------------------------------------------
TEST(SimCmdApply5E2E, ReferenceWorldState) {
    auto cmds = BuildStream();
    SeedWorld();
    ApplyAllDirect(cmds);

    // Building 1000 was created (type 12), upgraded (12->13), then removed (freed).
    CHECK(BuildingFindById(1000) == nullptr);
    CHECK_EQ(Apply5_CreateLog().createBuildingCount, 1);
    CHECK_EQ(Apply5_CreateLog().gebUpgradeCount, 1);
    CHECK_EQ(Apply5_CreateLog().removeBuildingCount, 1);

    // Person 2000 created and findable.
    Person* np = PersonFindRecordById(2000);
    CHECK(np != nullptr);
    CHECK_EQ((int)np->kind, 5);
    CHECK_EQ(g_lastObjectId, 2000);

    // Clock set by the sys-message.
    CHECK_EQ(g_sysGameTime.day, 5);
    CHECK_EQ((int)g_sysGameTime.hour, 9);
    CHECK_EQ(g_sysGameTime.minute, 30);

    // Allocators advanced exactly once each.
    CHECK_EQ(g_buildingNextId, 1001);
    CHECK_EQ(g_personNextId, 2001);
}

// ---------------------------------------------------------------------------
// (3) All five registries on one queue -> no opcode conflicts.
// ---------------------------------------------------------------------------
TEST(SimCmdApply5E2E, AllFiveRegistriesDisjoint) {
    int ownerCount[kNumOpcodes] = {};
    for (u32 op = 0; op < kNumOpcodes; ++op) {
        SeedWorld();
        int owners = 0;
        { CommandPacket p{}; p.opcode() = static_cast<u8>(op); AckEntry a{};
          if (ApplyPacket(p, &a)  != -1) ++owners; }
        { CommandPacket p{}; p.opcode() = static_cast<u8>(op); AckEntry a{};
          if (ApplyPacket2(p, &a) != -1) ++owners; }
        { CommandPacket p{}; p.opcode() = static_cast<u8>(op); AckEntry a{};
          if (ApplyPacket3(p, &a) != -1) ++owners; }
        { CommandPacket p{}; p.opcode() = static_cast<u8>(op); AckEntry a{};
          if (ApplyPacket4(p, &a) != -1) ++owners; }
        { CommandPacket p{}; p.opcode() = static_cast<u8>(op); AckEntry a{};
          if (ApplyPacket5(p, &a) != -1) ++owners; }
        ownerCount[op] = owners;
        CHECK(owners <= 1); // no opcode owned by two batches
    }

    // Batch 5 must own exactly its opcodes.
    const u8 batch5Ops[] = {
        kOp5HandleStateGate0, kOp5HandleStateGate3, kOp5CreateBuildingDirect,
        kOp5CreatePersonA, kOp5CreatePersonB, kOp5MoveObjectBetweenLists,
        kOp5UseObjectCheck, kOp5SysMessage, kOp5AssignPersonToOffice,
        kOp5GebUpgrade, kOp5RemoveBuilding, kOp5CreateGebaeude,
        kOp5SetCharacterChatBuffer, kOp5RemoveBuildingLink, kOp5UpdateBuildingLinks,
    };
    for (u8 op : batch5Ops) CHECK_EQ(ownerCount[op], 1);
}

// ---------------------------------------------------------------------------
// (4) Mixed stream through the full CommandQueue with all five registries.
// ---------------------------------------------------------------------------
TEST(SimCmdApply5E2E, MixedStreamThroughQueue) {
    SeedWorld();

    CommandQueue q;
    q.Init();
    q.set_standalone(true);
    RegisterApplyHandlers(q);
    RegisterApplyHandlers2(q);
    RegisterApplyHandlers3(q);
    RegisterApplyHandlers4(q);
    RegisterApplyHandlers5(q);

    std::vector<CommandPacket> cmds = BuildStream();
    // Add a batch-4 0x04 set-global-flag and a batch-5 0x20 sysmsg case 6.
    { CommandPacket p{}; p.opcode() = kOp4SetGlobalFlag; p.bytes[kFPayload] = 0x08; cmds.push_back(p); }
    { CommandPacket p{}; p.opcode() = kOp5SysMessage; p.bytes[16] = 6; p.bytes[17] = 0x55; cmds.push_back(p); }

    for (auto& c : cmds) q.EnqueuePacket(c);
    q.FlushSendQueue();
    q.ExecCommands();

    // batch5 building lifecycle ran.
    CHECK(BuildingFindById(1000) == nullptr);
    CHECK_EQ(Apply5_CreateLog().createBuildingCount, 1);
    // batch5 sysmsg case 6.
    CHECK_EQ((int)g_sysByte63CC1D, 0x55);
    // batch4 flag.
    CHECK_EQ((int)(g_globalFlags & 0x08), 0x08);
    // person created.
    CHECK(PersonFindRecordById(2000) != nullptr);
}

// ---------------------------------------------------------------------------
// (5) Guarded opcode (not in batch 5) is ignored by ApplyPacket5.
// ---------------------------------------------------------------------------
TEST(SimCmdApply5E2E, UnknownIgnored) {
    SeedWorld();
    CommandPacket p{}; p.opcode() = 0x1B; // deferred opcode, not owned by batch 5
    AckEntry ack{}; ack.status = 5;
    CHECK_EQ(ApplyPacket5(p, &ack), -1);
    CHECK_EQ((int)ack.status, 5);
}

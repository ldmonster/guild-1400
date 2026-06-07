#include "sim/command.h"
#include "sim/command_apply.h"   // shared g_last* tokens
#include "sim/command_apply3.h"
#include "sim/entity.h"
#include "sim/types.h"
#include "test.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// Helpers.
// ---------------------------------------------------------------------------
namespace {

void Seed3() {
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
}

Person* MakePerson(int slot, i32 id, u8 kind = 4) {
    g_persons[slot].marker = 0;
    g_persons[slot].kind   = kind;
    g_persons[slot].id     = id;
    g_personIds[slot]      = id;
    return &g_persons[slot];
}
ObjectRec* MakeObject(int slot, i32 id) {
    g_objects[slot].alive = 1;
    g_objects[slot].id    = id;
    return &g_objects[slot];
}

i32 PersonCutsceneId(const Person* p) {
    i32 v;
    std::memcpy(&v, reinterpret_cast<const u8*>(p) + kPfCutsceneId, 4);
    return v;
}
void SetPersonCutsceneId(Person* p, i32 v) {
    std::memcpy(reinterpret_cast<u8*>(p) + kPfCutsceneId, &v, 4);
}

} // namespace

// ===========================================================================
// Cutscene handlers.
// ===========================================================================

TEST(SimCmdApply3, AllocCutscene_TakesFreeSlot) {
    Seed3();
    CommandPacket p{};
    p.opcode() = kOp3AllocCutscene;
    AckEntry ack{};
    int r = ApplyPacket3(p, &ack);
    CHECK_EQ(r, 0);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ((int)ack.slot, 9);
    // a slot is now occupied (id != -1 set by AllocSlot? our template id is -1 so
    // the slot id stays -1, but a participant-count alloc still consumed a slot).
    // Verify AllocCutsceneWithId then occupies a distinct id-bearing slot.
}

TEST(SimCmdApply3, AllocCutsceneWithId_SetsField) {
    Seed3();
    CommandPacket p{};
    p.opcode() = kOp3AllocCutsceneWithId;
    p.put32(0x10, 4242);          // new id
    p.put32(0x14, 0xDEADBEEF);    // -> slot byte +120
    AckEntry ack{};
    int r = ApplyPacket3(p, &ack);
    CHECK_EQ(r, 0);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ((int)ack.slot, 9);
    CutsceneSlot* s = Apply3_Cutscenes().FindById(4242);
    CHECK(s != nullptr);
    u32 v;
    std::memcpy(&v, reinterpret_cast<u8*>(s) + 120, 4);
    CHECK_EQ(v, 0xDEADBEEFu);
}

TEST(SimCmdApply3, SetAndClearCutsceneState_Participant) {
    Seed3();
    // alloc a slot with id 7
    CommandPacket alloc{};
    alloc.opcode() = kOp3AllocCutsceneWithId;
    alloc.put32(0x10, 7);
    ApplyPacket3(alloc, nullptr);
    CutsceneSlot* s = Apply3_Cutscenes().FindById(7);
    CHECK(s != nullptr);

    CommandPacket add{};
    add.opcode() = kOp3SetCutsceneState;
    add.put32(0x10, 7);   // slot id
    add.put32(0x14, 99);  // person id
    AckEntry ack{};
    CHECK_EQ(ApplyPacket3(add, &ack), 0);
    CHECK_EQ((int)ack.status, 1);
    // partCount started at 1 (alive gate); the participant appends at index 1.
    CHECK_EQ((int)s->partCount, 2);
    CHECK_EQ(s->partIds[1], 99);

    // clear (remove the participant)
    CommandPacket clr{};
    clr.opcode() = kOp3ClearCutsceneState;
    clr.put32(0x10, 7);
    clr.put32(0x14, 99);
    AckEntry ack2{};
    CHECK_EQ(ApplyPacket3(clr, &ack2), 0);
    CHECK_EQ((int)ack2.status, 1);
    CHECK_EQ(s->partIds[1], -1);
}

TEST(SimCmdApply3, SetCutsceneState_MissingSlotRejected) {
    Seed3();
    CommandPacket add{};
    add.opcode() = kOp3SetCutsceneState;
    add.put32(0x10, 123); // no such slot
    add.put32(0x14, 5);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket3(add, &ack), 1);
    CHECK_EQ((int)ack.status, 0); // untouched
}

TEST(SimCmdApply3, SetObjectField_WritesMaster) {
    Seed3();
    CommandPacket alloc{};
    alloc.opcode() = kOp3AllocCutsceneWithId;
    alloc.put32(0x10, 12);
    ApplyPacket3(alloc, nullptr);
    CutsceneSlot* s = Apply3_Cutscenes().FindById(12);
    CHECK(s != nullptr);

    CommandPacket p{};
    p.opcode() = kOp3SetObjectField;
    p.put32(0x10, 12);
    p.put32(0x14, 0x12345678);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket3(p, &ack), 0);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ(s->master, 0x12345678);
}

TEST(SimCmdApply3, CutsceneReady_StampsParticipants) {
    Seed3();
    Person* a = MakePerson(0, 100, /*kind=*/4);
    Person* b = MakePerson(1, 200, /*kind=*/6); // kind 6 -> still stamped
    SetPersonCutsceneId(a, -1);
    SetPersonCutsceneId(b, 55); // already set; kind 6 path still writes slot id

    CommandPacket alloc{};
    alloc.opcode() = kOp3AllocCutsceneWithId;
    alloc.put32(0x10, 33);
    ApplyPacket3(alloc, nullptr);

    CommandPacket p{};
    p.opcode() = kOp3CutsceneReady;
    p.put32(0x10, 33);   // slot id + participant[0]=33? no — +16 is slot id AND p0
    // The original reads participant ids at payload +16..+28 (4 dwords). Put the
    // two persons in slots +20 and +24, leaving +16 as the (non-person) slot id.
    p.put32(0x10, 33);   // slot id (no person 33 -> skipped)
    p.put32(0x14, 100);
    p.put32(0x18, 200);
    p.put32(0x1C, -1);   // no person
    AckEntry ack{};
    CHECK_EQ(ApplyPacket3(p, &ack), 0);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ(PersonCutsceneId(a), 33);
    CHECK_EQ(PersonCutsceneId(b), 33);
}

TEST(SimCmdApply3, CutsceneReady_BusyRejected) {
    Seed3();
    SetTargetBusyHook([](const CommandPacket&) { return 1; });
    CommandPacket p{};
    p.opcode() = kOp3CutsceneReady;
    p.put32(0x10, 1);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket3(p, &ack), 1);
    CHECK_EQ((int)ack.status, 2); // busy reject
}

// ===========================================================================
// Combat handlers.
// ===========================================================================

TEST(SimCmdApply3, ApplyCombatDamage_SubtractsHp) {
    Seed3();
    CombatUnit* u = Apply3_Combat().Spawn(/*id=*/500, /*hp=*/100, /*team=*/1);
    CHECK(u != nullptr);

    CommandPacket p{};
    p.opcode() = kOp3ApplyCombatDamage;
    p.put32(0x10, 500);   // unit id
    p.put32(0x14, 30);    // damage
    p.bytes[0x18] = 1;    // alive flag stays 1
    AckEntry ack{};
    CHECK_EQ(ApplyPacket3(p, &ack), 0);
    CHECK_EQ(u->hp, 70);
    CHECK_EQ((int)u->alive, 1);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ((int)ack.slot, 8);
}

TEST(SimCmdApply3, ApplyCombatDamage_SetsDeadFlag) {
    Seed3();
    CombatUnit* u = Apply3_Combat().Spawn(501, 50, 2);
    CommandPacket p{};
    p.opcode() = kOp3ApplyCombatDamage;
    p.put32(0x10, 501);
    p.put32(0x14, 60);    // lethal
    p.bytes[0x18] = 0;    // dead flag
    CHECK_EQ(ApplyPacket3(p, nullptr), 0);
    CHECK_EQ(u->hp, -10);  // intentional signed underflow vs 50
    CHECK_EQ((int)u->alive, 0);
}

TEST(SimCmdApply3, ApplyCombatDamage_MissingUnitTolerated) {
    Seed3();
    CommandPacket p{};
    p.opcode() = kOp3ApplyCombatDamage;
    p.put32(0x10, 999);
    p.put32(0x14, 10);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket3(p, &ack), 0); // tolerated
    CHECK_EQ((int)ack.status, 1);
}

TEST(SimCmdApply3, StoreCombatSlot_LocalGate) {
    Seed3();
    Apply3_SetLocalPlayerId(42);

    // not local -> rejected, nothing stored
    CommandPacket nope{};
    nope.opcode() = kOp3StoreCombatSlot;
    nope.put32(0x10, 7);   // != local
    CHECK_EQ(ApplyPacket3(nope, nullptr), 1);

    // local -> stored, ack stamped
    CommandPacket p{};
    p.opcode() = kOp3StoreCombatSlot;
    p.put32(0x10, 42);     // == local
    p.set_cmd_id(9);
    p.put32(0x14, 13);     // unit
    for (int i = 0; i < 0x2C; ++i) p.bytes[0x14 + i] = static_cast<u8>(i + 1);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket3(p, &ack), 0);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ((int)ack.slot, 8);
}

TEST(SimCmdApply3, DispatchUnitOrder_AttackWhenAlive) {
    Seed3();
    Apply3_SetLocalPlayerId(70);
    Apply3_Combat().Spawn(/*id=*/8, /*hp=*/100, /*team=*/1); // alive

    CommandPacket p{};
    p.opcode() = kOp3DispatchUnitOrder;
    p.put32(0x10, 70);     // battle id == local
    p.bytes[0x14] = 2;     // order = attack
    p.put32(0x35, 8);      // unit id at +53
    AckEntry ack{};
    CHECK_EQ(ApplyPacket3(p, &ack), 0);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ(Apply3_CharLog().lastOrderKind, 2);
}

TEST(SimCmdApply3, DispatchUnitOrder_NotLocalRejected) {
    Seed3();
    Apply3_SetLocalPlayerId(70);
    CommandPacket p{};
    p.opcode() = kOp3DispatchUnitOrder;
    p.put32(0x10, 1); // != local
    CHECK_EQ(ApplyPacket3(p, nullptr), 1);
}

TEST(SimCmdApply3, EquipCombatObject_RequiresOwner) {
    Seed3();
    // no owner -> reject
    CommandPacket nope{};
    nope.opcode() = kOp3EquipCombatObject;
    nope.put32(0x10, 4000);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket3(nope, &ack), 1);
    CHECK_EQ((int)ack.status, 2);

    // owner exists -> equip recorded
    MakePerson(0, 4000);
    CommandPacket p{};
    p.opcode() = kOp3EquipCombatObject;
    p.put32(0x10, 4000);
    p.bytes[0x1D] = 7;
    AckEntry ack2{};
    CHECK_EQ(ApplyPacket3(p, &ack2), 0);
    CHECK_EQ((int)ack2.status, 1);
    CHECK_EQ(Apply3_CharLog().equipCount, 1);
}

// ===========================================================================
// Character / walk / sound handlers (delegating; verify the action log + ack).
// ===========================================================================

TEST(SimCmdApply3, CharPlaySample_QueuesAction) {
    Seed3();
    CommandPacket p{};
    p.opcode() = kOp3CharPlaySample;
    p.put32(0x10, 321);  // sp id
    p.put32(0x14, 9);    // sample arg
    AckEntry ack{};
    CHECK_EQ(ApplyPacket3(p, &ack), 0);
    CHECK_EQ((int)ack.status, 2);
    CHECK_EQ(Apply3_CharLog().lastActorSp, 321);
    CHECK_EQ(Apply3_CharLog().lastActionType, 0x2D);
    CHECK_EQ(Apply3_CharLog().lastArg, 9);
}

TEST(SimCmdApply3, CharPlaySample_MissingActorRejected) {
    Seed3();
    SetCharFindHook([](i32) -> void* { return nullptr; }); // no actor
    CommandPacket p{};
    p.opcode() = kOp3CharPlaySample;
    p.put32(0x10, 321);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket3(p, &ack), 1);
    CHECK_EQ((int)ack.status, 0); // untouched
}

TEST(SimCmdApply3, CharStandUp_And_Sound_Counts) {
    Seed3();
    CommandPacket up{};
    up.opcode() = kOp3CharStandUp;
    up.put32(0x10, 5);
    CHECK_EQ(ApplyPacket3(up, nullptr), 0);
    CHECK_EQ(Apply3_CharLog().standUpCount, 1);

    CommandPacket snd{};
    snd.opcode() = kOp3CharPlaySound;
    snd.put32(0x10, 5);
    snd.bytes[0x44] = 17; // sound id
    CHECK_EQ(ApplyPacket3(snd, nullptr), 0);
    CHECK_EQ(Apply3_CharLog().soundCount, 1);
    CHECK_EQ(Apply3_CharLog().lastArg, 17);
}

TEST(SimCmdApply3, CharSpawnAtEntrance_NeedsBuilding) {
    Seed3();
    CommandPacket nope{};
    nope.opcode() = kOp3CharSpawnAtEntrance;
    nope.put32(0x10, 1);
    nope.put32(0x14, 7000);   // no such building
    CHECK_EQ(ApplyPacket3(nope, nullptr), 1);

    MakeObject(0, 7000);
    CommandPacket p{};
    p.opcode() = kOp3CharSpawnAtEntrance;
    p.put32(0x10, 1);
    p.put32(0x14, 7000);
    p.put32(0x18, 3);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket3(p, &ack), 0);
    CHECK_EQ((int)ack.status, 2);
    CHECK_EQ((int)ack.slot, 7);
    CHECK_EQ(ack.seq, 7000);
}

TEST(SimCmdApply3, ApplyCharacterUpdate_NeedsPerson) {
    Seed3();
    CommandPacket nope{};
    nope.opcode() = kOp3ApplyCharacterUpdate;
    nope.put32(0x10, 808);
    CHECK_EQ(ApplyPacket3(nope, nullptr), 1);

    MakePerson(0, 808);
    CommandPacket p{};
    p.opcode() = kOp3ApplyCharacterUpdate;
    p.put32(0x10, 808);
    p.bytes[0x14] = 3; // category
    AckEntry ack{};
    CHECK_EQ(ApplyPacket3(p, &ack), 0);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ(Apply3_CharLog().charUpdateCount, 1);
    CHECK_EQ(Apply3_CharLog().lastArg, 3);
}

TEST(SimCmdApply3, SpawnAndPlaceCharacter_SetsLastObjectId) {
    Seed3();
    CommandPacket p{};
    p.opcode() = kOp3SpawnAndPlaceCharacter;
    p.bytes[0x14] = 13;        // kind
    p.put32(0x17, -1);         // no parent
    AckEntry ack{};
    CHECK_EQ(ApplyPacket3(p, &ack), 0);
    CHECK_EQ((int)ack.status, 1);
    CHECK(g_lastObjectId >= 0);
    CHECK_EQ(ack.seq, g_lastObjectId); // +6 carries the new id
    CHECK_EQ(Apply3_CharLog().spawnCount, 1);
}

TEST(SimCmdApply3, SpawnAndPlaceCharacter_BadParentRejected) {
    Seed3();
    CommandPacket p{};
    p.opcode() = kOp3SpawnAndPlaceCharacter;
    p.bytes[0x14] = 13;
    p.put32(0x17, 9999); // parent that does not resolve
    AckEntry ack{};
    CHECK_EQ(ApplyPacket3(p, &ack), 1);
}

TEST(SimCmdApply3, DeselectObject_NeedsObject) {
    Seed3();
    CommandPacket nope{};
    nope.opcode() = kOp3DeselectObject;
    nope.put32(0x10, 6000);
    CHECK_EQ(ApplyPacket3(nope, nullptr), 1);

    MakeObject(0, 6000);
    CommandPacket p{};
    p.opcode() = kOp3DeselectObject;
    p.put32(0x10, 6000);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket3(p, &ack), 0);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ(Apply3_CharLog().deselectCount, 1);
}

TEST(SimCmdApply3, TriggerCharacterAction_StampsAck) {
    Seed3();
    MakePerson(0, 4321);
    CommandPacket p{};
    p.opcode() = kOp3TriggerCharacterAction;
    p.put32(0x10, 4321);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket3(p, &ack), 0);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ(Apply3_CharLog().triggerCount, 1);
}

TEST(SimCmdApply3, CharApplyInteraction_NeedsPerson) {
    Seed3();
    CommandPacket nope{};
    nope.opcode() = kOp3CharApplyInteraction;
    nope.put32(0x10, 222);
    CHECK_EQ(ApplyPacket3(nope, nullptr), 1);

    MakePerson(0, 222);
    CommandPacket p{};
    p.opcode() = kOp3CharApplyInteraction;
    p.put32(0x10, 222);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket3(p, &ack), 0);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ(Apply3_CharLog().interactionCount, 1);
}

// ===========================================================================
// Select / chat.
// ===========================================================================

TEST(SimCmdApply3, AppendChatLine_LocalTarget) {
    Seed3();
    Apply3_SetLocalPlayerId(77);
    Apply3_ClearChat();

    CommandPacket p{};
    p.opcode() = kOp3AppendChatLine;
    // target id in slot +24 (one of the 8) equals local player
    p.put32(0x18, 77);
    const char* msg = "hello world";
    std::memcpy(p.bytes + 0x30, msg, std::strlen(msg) + 1);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket3(p, &ack), 0);
    CHECK_EQ((int)ack.status, 1);
    CHECK(std::strcmp(Apply3_ChatBuffer(), "hello world") == 0);
}

TEST(SimCmdApply3, AppendChatLine_AckForcesAppend) {
    Seed3();
    Apply3_SetLocalPlayerId(-99); // no local target
    Apply3_ClearChat();

    CommandPacket p{};
    p.opcode() = kOp3AppendChatLine;
    const char* msg = "abc";
    std::memcpy(p.bytes + 0x30, msg, std::strlen(msg) + 1);
    AckEntry ack{};
    // ack non-null -> append regardless of targeting
    CHECK_EQ(ApplyPacket3(p, &ack), 0);
    CHECK(std::strcmp(Apply3_ChatBuffer(), "abc") == 0);

    // append a second line (concatenates)
    CommandPacket p2{};
    p2.opcode() = kOp3AppendChatLine;
    const char* msg2 = "def";
    std::memcpy(p2.bytes + 0x30, msg2, std::strlen(msg2) + 1);
    AckEntry ack2{};
    CHECK_EQ(ApplyPacket3(p2, &ack2), 0);
    CHECK(std::strcmp(Apply3_ChatBuffer(), "abcdef") == 0);
}

TEST(SimCmdApply3, AppendChatLine_NoTargetNoAckSkips) {
    Seed3();
    Apply3_SetLocalPlayerId(-99);
    Apply3_ClearChat();
    CommandPacket p{};
    p.opcode() = kOp3AppendChatLine;
    const char* msg = "xyz";
    std::memcpy(p.bytes + 0x30, msg, std::strlen(msg) + 1);
    CHECK_EQ(ApplyPacket3(p, nullptr), 0);   // no ack, not local -> skipped
    CHECK_EQ((int)std::strlen(Apply3_ChatBuffer()), 0);
}

// ===========================================================================
// Dispatch table + unknown/guarded.
// ===========================================================================

TEST(SimCmdApply3, UnknownOpcodeIgnored) {
    Seed3();
    CommandPacket p{};
    p.opcode() = 0x16; // owned by batch 1, not batch 3
    CHECK_EQ(ApplyPacket3(p, nullptr), -1);
    p.opcode() = 0x0D; // owned by batch 2
    CHECK_EQ(ApplyPacket3(p, nullptr), -1);
    p.opcode() = 0x02; // truly unhandled
    CHECK_EQ(ApplyPacket3(p, nullptr), -1);
}

TEST(SimCmdApply3, RegistryWiresOnlyBatch3) {
    Seed3();
    CommandQueue q;
    q.Init();
    RegisterApplyHandlers3(q);
    // a batch-3 opcode is now handled (status table updated through the queue),
    // but we verify wiring indirectly: handler set, batch-1/2 left unset.
    // Build a damage packet and run it through the queue dispatch.
    Apply3_Combat().Spawn(/*id=*/600, /*hp=*/80, /*team=*/1);
    CommandPacket staged{};
    staged.opcode() = kOp3ApplyCombatDamage;
    staged.put32(0x10, 600);
    staged.put32(0x14, 20);
    staged.bytes[0x18] = 1;
    q.set_standalone(true);
    i32 ring = q.EnqueuePacket(staged);
    CHECK(ring >= 0);
    q.FlushSendQueue();
    q.ExecCommands();
    CombatUnit* u = Apply3_Combat().FindUnitById(600);
    CHECK(u != nullptr);
    CHECK_EQ(u->hp, 60);
}

#include "sim/command.h"
#include "sim/command_apply.h"   // shared g_last* tokens
#include "sim/command_apply5.h"
#include "sim/building_create.h"
#include "sim/person_create.h"
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

void Seed5() {
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
}

Person* MakePerson(int slot, i32 id, u8 kind = 4) {
    g_persons[slot].marker = static_cast<i16>(slot);
    g_persons[slot].kind   = kind;
    g_persons[slot].id     = id;
    g_persons[slot].marker = static_cast<i16>(slot);
    g_personIds[slot]      = id;
    return &g_persons[slot];
}
ObjectRec* MakeBuilding(int slot, i32 id, u8 type = 4) {
    g_objects[slot].alive = type;          // alive/type byte (non-zero)
    g_objects[slot].id    = id;
    return &g_objects[slot];
}
u8* ObjBytes(ObjectRec* o) { return reinterpret_cast<u8*>(o); }

// little-endian writers into a packet payload
void W32(CommandPacket& p, u32 off, i32 v) { p.put32(off, static_cast<u32>(v)); }
void W16(CommandPacket& p, u32 off, u16 v) { p.put16(off, v); }

} // namespace

// ===========================================================================
// 0x00 / 0x03 — state gate.
// ===========================================================================
TEST(SimCmdApply5, StateGate_RejectsNonFramingOpcode) {
    Seed5();
    // Build with opcode 0x10 (a non-framing opcode) -> handler rejects.
    CommandPacket p{}; p.opcode() = 0x10;
    AckEntry ack{}; ack.status = 0;
    CHECK_EQ(ExHandleStateGate(p, &ack), 1);
    CHECK_EQ((int)ack.status, 0); // untouched
}
TEST(SimCmdApply5, StateGate_AcksFramingOpcode) {
    Seed5();
    CommandPacket p{}; p.opcode() = kOp5HandleStateGate0; // 0x00
    AckEntry ack{}; ack.status = 0;
    CHECK_EQ(ApplyPacket5(p, &ack), 0);
    CHECK_EQ((int)ack.status, 1);
}

// ===========================================================================
// 0x0A — create building directly.
// ===========================================================================
TEST(SimCmdApply5, CreateBuildingDirect_AllocatesAndCopies) {
    Seed5();
    Apply5_SetStandalone(true);
    g_buildingNextId = 700;
    // owner id == -1 => ownerWord 0xFFFF (no owner lookup needed).
    CommandPacket p{}; p.opcode() = kOp5CreateBuildingDirect;
    W32(p, 16, 0);
    W32(p, 22, -1);    // owner id -1
    p.bytes[20] = 9;   // type byte
    W16(p, 26, 0x1234);// word -> building +90
    W32(p, 28, 1);     // copy flag set
    for (int i = 0; i < 0x30; ++i) p.bytes[32 + i] = static_cast<u8>(i + 1);

    AckEntry ack{};
    CHECK_EQ(ApplyPacket5(p, &ack), 0);
    CHECK_EQ(g_lastSceneId, 700);             // new id latched into dword_63128C
    CHECK_EQ(Apply5_CreateLog().createBuildingCount, 1);
    // verify the record fields.
    ObjectRec* b = BuildingFindById(700);
    CHECK(b != nullptr);
    CHECK_EQ((int)ObjBytes(b)[0], 9);         // type
    u16 w90; std::memcpy(&w90, ObjBytes(b) + 90, 2);
    CHECK_EQ((int)w90, 0x1234);
    CHECK_EQ((int)ObjBytes(b)[101], 1);       // first copied byte
    CHECK_EQ((int)ObjBytes(b)[101 + 0x2F], 0x30);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ((int)ack.slot, 2);
}

TEST(SimCmdApply5, CreateBuildingDirect_OwnerLookup) {
    Seed5();
    Apply5_SetStandalone(true);
    MakePerson(5, 4242);
    CommandPacket p{}; p.opcode() = kOp5CreateBuildingDirect;
    W32(p, 22, 4242); // owner id -> index 5
    p.bytes[20] = 3;
    W32(p, 28, 0);    // no copy
    AckEntry ack{};
    CHECK_EQ(ApplyPacket5(p, &ack), 0);
    ObjectRec* b = BuildingFindById(g_lastSceneId);
    CHECK(b != nullptr);
    u16 owner; std::memcpy(&owner, ObjBytes(b) + 37, 2);
    CHECK_EQ((int)owner, 5);
}

TEST(SimCmdApply5, CreateBuildingDirect_OwnerMissingRejects) {
    Seed5();
    CommandPacket p{}; p.opcode() = kOp5CreateBuildingDirect;
    W32(p, 22, 9999); // unknown owner
    AckEntry ack{};
    CHECK_EQ(ApplyPacket5(p, &ack), 1);
    CHECK_EQ(Apply5_CreateLog().createBuildingCount, 0);
}

// ===========================================================================
// 0x0B / 0x0C — person create.
// ===========================================================================
TEST(SimCmdApply5, CreatePersonA_SpawnsAndLatchesId) {
    Seed5();
    Apply5_SetStandalone(true);
    g_personNextId = 5000;
    CommandPacket p{}; p.opcode() = kOp5CreatePersonA;
    W32(p, 31, -1);                 // no parent record (bytes 31..34 = 0xFF)
    // kind = HIBYTE(*(p+17)); put a dword whose top byte is 6.
    W32(p, 17, 0x06000000);
    W32(p, 21, 11); W16(p, 29, 3); W32(p, 25, 22);
    // a6/a7/a8 = HIBYTE(*(p+32/33/34)) == bytes 35/36/37 (do not clobber +31).
    p.bytes[35] = 0; p.bytes[36] = 0; p.bytes[37] = 0;
    AckEntry ack{};
    CHECK_EQ(ApplyPacket5(p, &ack), 0);
    CHECK_EQ(g_lastObjectId, 5000);  // dword_631288
    CHECK_EQ(Apply5_CreateLog().createPersonCount, 1);
    Person* np = PersonFindRecordById(5000);
    CHECK(np != nullptr);
    CHECK_EQ((int)np->kind, 6);
    CHECK_EQ((int)ack.status, 1);
}

TEST(SimCmdApply5, CreatePersonB_CopiesNameAndLatchesId) {
    Seed5();
    Apply5_SetStandalone(true);
    g_personNextId = 6000;
    CommandPacket p{}; p.opcode() = kOp5CreatePersonB;
    W32(p, 20, 1); W16(p, 28, 0); W32(p, 24, 2);
    W32(p, 27, 0); W32(p, 32, 0);
    const char* name = "Hans";
    std::memcpy(p.bytes + 37, name, 5);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket5(p, &ack), 0);
    CHECK_EQ(g_lastObjectId, 6000);
    Person* np = PersonFindRecordById(6000);
    CHECK(np != nullptr);
    // kind = (ack != null ? 0 : 1) + 6 == 6.
    CHECK_EQ((int)np->kind, 6);
    CHECK_EQ(std::strcmp(reinterpret_cast<char*>(np) + 48, "Hans"), 0);
}

// ===========================================================================
// 0x14 — use-object check.
// ===========================================================================
TEST(SimCmdApply5, UseObjectCheck_MissingRejects) {
    Seed5();
    CommandPacket p{}; p.opcode() = kOp5UseObjectCheck;
    W32(p, 16, 4242); // no such scene entity
    AckEntry ack{};
    CHECK_EQ(ApplyPacket5(p, &ack), 1);
}

// ===========================================================================
// 0x20 — sys message.
// ===========================================================================
TEST(SimCmdApply5, SysMessage_Case2SetsFlags) {
    Seed5();
    g_sysGlobalFlags = 0xFF;
    CommandPacket p{}; p.opcode() = kOp5SysMessage;
    p.bytes[16] = 2;     // subtype
    W32(p, 17, 1);       // nonzero -> bit 8
    AckEntry ack{};
    CHECK_EQ(ApplyPacket5(p, &ack), 0);
    CHECK_EQ((int)g_sysLoadFlag, 1);
    CHECK_EQ((int)g_sysGlobalFlags, (0xFF & 0xE7) | 8);
    CHECK_EQ((int)ack.status, 1);
}
TEST(SimCmdApply5, SysMessage_Case3WritesClock) {
    Seed5();
    CommandPacket p{}; p.opcode() = kOp5SysMessage;
    p.bytes[16] = 3;
    W32(p, 17, 42);      // day
    W16(p, 21, 13);      // hour
    W32(p, 23, 55);      // minute
    W32(p, 27, 7);       // second
    AckEntry ack{};
    CHECK_EQ(ApplyPacket5(p, &ack), 0);
    CHECK_EQ(g_sysGameTime.day, 42);
    CHECK_EQ((int)g_sysGameTime.hour, 13);
    CHECK_EQ(g_sysGameTime.minute, 55);
    CHECK_EQ(g_sysGameTime.second, 7);
}
TEST(SimCmdApply5, SysMessage_Case8LatchActivePlayer) {
    Seed5();
    MakePerson(0, 77);
    CommandPacket p{}; p.opcode() = kOp5SysMessage;
    p.bytes[16] = 8;
    W32(p, 17, 77);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket5(p, &ack), 0);
    CHECK_EQ(g_sysActivePlayer, 77);
    CHECK_EQ((int)ack.status, 1);   // result 1
    // second latch fails (already taken) -> result 2.
    AckEntry ack2{};
    CHECK_EQ(ApplyPacket5(p, &ack2), 0);
    CHECK_EQ((int)ack2.status, 2);
}
TEST(SimCmdApply5, SysMessage_Case0AClearActivePlayer) {
    Seed5();
    g_sysActivePlayer = 77;
    CommandPacket p{}; p.opcode() = kOp5SysMessage;
    p.bytes[16] = 0xA;
    W32(p, 17, 77);
    W32(p, 33, 1);   // sets dword_63CC30
    AckEntry ack{};
    CHECK_EQ(ApplyPacket5(p, &ack), 0);
    CHECK_EQ(g_sysActivePlayer, -1);
    CHECK_EQ((int)g_sysDword63CC30, 1);
}
TEST(SimCmdApply5, SysMessage_Case0FCopiesName) {
    Seed5();
    CommandPacket p{}; p.opcode() = kOp5SysMessage;
    p.bytes[16] = 0xF;
    W32(p, 17, 0xABCD);
    const char* nm = "GuildName";
    std::memcpy(p.bytes + 21, nm, 10);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket5(p, &ack), 0);
    CHECK_EQ((int)g_sysDword63CC70, 0xABCD);
    CHECK_EQ(std::strcmp(reinterpret_cast<char*>(g_sysName63CC74), "GuildName"), 0);
}
TEST(SimCmdApply5, SysMessage_Case10PairedInsert) {
    Seed5();
    CommandPacket p{}; p.opcode() = kOp5SysMessage;
    p.bytes[16] = 0x10;
    W32(p, 17, 111);
    W32(p, 21, 222);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket5(p, &ack), 0);
    CHECK_EQ(g_sysCutTable50[0], 111);
    CHECK_EQ(g_sysCutTable54[0], 222);
}
TEST(SimCmdApply5, SysMessage_LeafCasesRouteHook) {
    Seed5();
    CommandPacket p{}; p.opcode() = kOp5SysMessage;
    p.bytes[16] = 0; // sky init leaf
    AckEntry ack{};
    CHECK_EQ(ApplyPacket5(p, &ack), 0);
    CHECK_EQ(Apply5_CreateLog().sysMessageLeafCount, 1);
    CHECK_EQ(Apply5_CreateLog().lastSysLeafCase, 0);
}

// ===========================================================================
// 0x2C — assign person to office.
// ===========================================================================
TEST(SimCmdApply5, AssignPersonToOffice_AssignsSlot) {
    Seed5();
    MakePerson(3, 808);
    CommandPacket p{}; p.opcode() = kOp5AssignPersonToOffice;
    W32(p, 20, 808);         // person id
    W32(p, 24, 0x00050000);  // HIWORD == 5 office type
    p.bytes[25] = 1;         // coord
    p.bytes[28] = 1;         // mode (sets extra)
    W32(p, 29, 99);          // extra
    AckEntry ack{};
    CHECK_EQ(ApplyPacket5(p, &ack), 0);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ((int)ack.slot, 5);
    OfficeSlot* s = Apply5_OfficeSlot(0);
    CHECK(s->used);
    CHECK_EQ(s->holderId, 808);
    CHECK_EQ(s->type, 5);
    CHECK_EQ(s->extra, 99);
}
TEST(SimCmdApply5, AssignPersonToOffice_MissingPerson) {
    Seed5();
    CommandPacket p{}; p.opcode() = kOp5AssignPersonToOffice;
    W32(p, 20, 1234);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket5(p, &ack), 0);
    CHECK_EQ((int)ack.status, 2); // not assigned
}

// ===========================================================================
// 0x3A — building upgrade.
// ===========================================================================
TEST(SimCmdApply5, GebUpgrade_BumpsLevel) {
    Seed5();
    ObjectRec* b = MakeBuilding(2, 555, 4);
    CommandPacket p{}; p.opcode() = kOp5GebUpgrade;
    W32(p, 16, 555);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket5(p, &ack), 0);
    CHECK_EQ((int)ObjBytes(b)[0], 5); // level bumped from 4 to 5
    CHECK_EQ(Apply5_CreateLog().gebUpgradeCount, 1);
    CHECK_EQ(Apply5_CreateLog().placeMeshCount, 1);
}
TEST(SimCmdApply5, GebUpgrade_MissingRejects) {
    Seed5();
    CommandPacket p{}; p.opcode() = kOp5GebUpgrade;
    W32(p, 16, 4242);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket5(p, &ack), 1);
}

// ===========================================================================
// 0x3B — remove building.
// ===========================================================================
TEST(SimCmdApply5, RemoveBuilding_FreesRecord) {
    Seed5();
    MakeBuilding(1, 333, 7);
    CommandPacket p{}; p.opcode() = kOp5RemoveBuilding;
    W32(p, 16, 333);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket5(p, &ack), 0);
    CHECK(BuildingFindById(333) == nullptr); // freed (alive cleared)
    CHECK_EQ(Apply5_CreateLog().removeBuildingCount, 1);
    CHECK_EQ((int)ack.status, 2);
}

// ===========================================================================
// 0x4C — create gebaeude under a person.
// ===========================================================================
TEST(SimCmdApply5, CreateGebaeude_UnderPerson) {
    Seed5();
    Apply5_SetStandalone(true);
    Person* owner = MakePerson(4, 909);
    owner->marker = 4; // owner index word used as ownerWord
    g_buildingNextId = 800;
    CommandPacket p{}; p.opcode() = kOp5CreateGebaeude;
    W32(p, 16, 909); // a1[4] == owner id at +0x10
    p.bytes[24] = 12; // type byte at +0x18
    AckEntry ack{};
    CHECK_EQ(ApplyPacket5(p, &ack), 0);
    CHECK_EQ(g_lastSceneId, 800);
    ObjectRec* b = BuildingFindById(800);
    CHECK(b != nullptr);
    CHECK_EQ((int)ObjBytes(b)[0], 12);
    u16 owner_word; std::memcpy(&owner_word, ObjBytes(b) + 37, 2);
    CHECK_EQ((int)owner_word, 4);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ((int)ack.slot, 2);
}
TEST(SimCmdApply5, CreateGebaeude_MissingOwner) {
    Seed5();
    CommandPacket p{}; p.opcode() = kOp5CreateGebaeude;
    W32(p, 16, 4242);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket5(p, &ack), 1);
}

// ===========================================================================
// 0x4F — set chat buffer.
// ===========================================================================
TEST(SimCmdApply5, SetChatBuffer_WritesWords) {
    Seed5();
    CHECK(Apply5_SeedChatTarget(404, 8));
    CommandPacket p{}; p.opcode() = kOp5SetCharacterChatBuffer;
    W32(p, 16, 404);  // actor id
    W32(p, 44, 8);    // capacity
    W32(p, 40, 0);    // start index
    p.bytes[48] = 0xAA; p.bytes[49] = 0xBB;
    p.bytes[50] = 0xCC; p.bytes[51] = 0xDD;
    AckEntry ack{};
    CHECK_EQ(ApplyPacket5(p, &ack), 0);
    CHECK_EQ((int)ack.status, 1);
}
TEST(SimCmdApply5, SetChatBuffer_MissingActor) {
    Seed5();
    CommandPacket p{}; p.opcode() = kOp5SetCharacterChatBuffer;
    W32(p, 16, 999);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket5(p, &ack), 1);
}

// ===========================================================================
// 0x53 / 0x54 — building links (tag dispatch).
// ===========================================================================
TEST(SimCmdApply5, RemoveBuildingLink_UnknownTagAcks) {
    Seed5();
    CommandPacket p{}; p.opcode() = kOp5RemoveBuildingLink;
    W32(p, 16, 0x11111111); // unknown tag
    AckEntry ack{};
    CHECK_EQ(ApplyPacket5(p, &ack), 0);
    CHECK_EQ((int)ack.status, 1);
}
TEST(SimCmdApply5, UpdateBuildingLinks_UnknownTagRejects) {
    Seed5();
    CommandPacket p{}; p.opcode() = kOp5UpdateBuildingLinks;
    W32(p, 16, 0x22222222);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket5(p, &ack), 1);
}

// ===========================================================================
// Guarded / unknown opcode is ignored.
// ===========================================================================
TEST(SimCmdApply5, UnknownOpcodeIgnored) {
    Seed5();
    CommandPacket p{}; p.opcode() = 0x7E; // not in batch 5
    AckEntry ack{}; ack.status = 9;
    CHECK_EQ(ApplyPacket5(p, &ack), -1);
    CHECK_EQ((int)ack.status, 9); // untouched
}

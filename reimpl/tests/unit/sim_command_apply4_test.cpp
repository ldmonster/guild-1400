#include "sim/command.h"
#include "sim/command_apply.h"  // shared g_last* tokens
#include "sim/command_apply4.h"
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

void Seed4() {
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
}

Person* MakePerson(int slot, i32 id, u8 kind = 4) {
    g_persons[slot].marker = static_cast<i16>(slot);
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

void SetPersonCharPtr(Person* p, i32 v) {
    std::memcpy(reinterpret_cast<u8*>(p) + 388, &v, 4);
}
i32 PersonCharPtr(const Person* p) {
    i32 v; std::memcpy(&v, reinterpret_cast<const u8*>(p) + 388, 4); return v;
}
void SetPersonCutsceneId(Person* p, i32 v) {
    std::memcpy(reinterpret_cast<u8*>(p) + 520, &v, 4);
}
i32 PersonCutsceneId(const Person* p) {
    i32 v; std::memcpy(&v, reinterpret_cast<const u8*>(p) + 520, 4); return v;
}

} // namespace

// ===========================================================================
// Trivial / framing-ack handlers.
// ===========================================================================

TEST(SimCmdApply4, HandleAck_StampsOk) {
    Seed4();
    CommandPacket p{}; p.opcode() = kOp4HandleAck;
    AckEntry ack{}; ack.status = 0;
    CHECK_EQ(ApplyPacket4(p, &ack), 0);
    CHECK_EQ((int)ack.status, 1);
}

TEST(SimCmdApply4, HandleNoop_ReturnsOneNoAck) {
    Seed4();
    CommandPacket p{}; p.opcode() = kOp4HandleNoop;
    AckEntry ack{}; ack.status = 7;
    CHECK_EQ(ApplyPacket4(p, &ack), 1);
    CHECK_EQ((int)ack.status, 7); // untouched
}

TEST(SimCmdApply4, SetGlobalFlag_OrsByte) {
    Seed4();
    g_globalFlags = 0x01;
    CommandPacket p{}; p.opcode() = kOp4SetGlobalFlag;
    p.bytes[kFPayload] = 0x80;
    AckEntry ack{};
    CHECK_EQ(ApplyPacket4(p, &ack), 0);
    CHECK_EQ((int)g_globalFlags, 0x81);
    CHECK_EQ((int)ack.status, 1);
    // OR again with overlapping bit is idempotent.
    p.bytes[kFPayload] = 0x01;
    ApplyPacket4(p, &ack);
    CHECK_EQ((int)g_globalFlags, 0x81);
}

TEST(SimCmdApply4, LoadBufAllocThenAppend) {
    Seed4();
    CommandPacket a{}; a.opcode() = kOp4LoadBufAlloc;
    a.put32(kFPayload, 4096);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket4(a, &ack), 0);
    CHECK_EQ(g_loadBufSize, 4096);
    CHECK_EQ(g_loadBufCursor, 0);
    CHECK_EQ((int)ack.status, 1);

    CommandPacket b{}; b.opcode() = kOp4LoadBufAppend;
    for (int i = 0; i < 0x80; ++i) b.bytes[kFPayload + i] = static_cast<u8>(i ^ 0x5A);
    CHECK_EQ(ApplyPacket4(b, &ack), 0);
    CHECK_EQ(g_loadBufCursor, 128);
    for (int i = 0; i < 0x80; ++i) CHECK_EQ((int)g_loadBuf[i], (int)(u8)(i ^ 0x5A));

    // second append lands at +128
    for (int i = 0; i < 0x80; ++i) b.bytes[kFPayload + i] = static_cast<u8>(0xFF - i);
    CHECK_EQ(ApplyPacket4(b, &ack), 0);
    CHECK_EQ(g_loadBufCursor, 256);
    CHECK_EQ((int)g_loadBuf[128], 0xFF);
    CHECK_EQ((int)g_loadBuf[128 + 1], 0xFE);
}

TEST(SimCmdApply4, AckStub_StampsOk) {
    Seed4();
    CommandPacket p{}; p.opcode() = kOp4AckStub;
    AckEntry ack{}; ack.status = 0;
    CHECK_EQ(ApplyPacket4(p, &ack), 0);
    CHECK_EQ((int)ack.status, 1);
}

// ===========================================================================
// Object create / transform / effect.
// ===========================================================================

TEST(SimCmdApply4, SetObjectTransform_CreatesAndCopies) {
    Seed4();
    CommandPacket p{}; p.opcode() = kOp4SetObjectTransform;
    p.put32(0x10, 7777);            // container id (bytes 16..19)
    p.bytes[20] = 1234 & 0xFF; p.bytes[21] = (1234 >> 8) & 0xFF; // proto = HIWORD(dword@+0x12) -> bytes 20,21
    for (int i = 0; i < 0x1C; ++i) p.bytes[22 + i] = static_cast<u8>(0x10 + i);
    p.bytes[50] = 0xAB; p.bytes[51] = 0xCD; // word -> obj+56
    p.bytes[52] = 0x5F;                      // byte -> obj+58
    AckEntry ack{};
    CHECK_EQ(ApplyPacket4(p, &ack), 0);
    CHECK_EQ((int)ack.status, 1);
    // 0x497bdf: `mov byte ptr [ebp+0], 1` writes ONLY the status byte (+0).
    // The slot (+1) and seq (+6) ack fields are left untouched (stay 0).
    CHECK_EQ((int)ack.slot, 0);

    u8* obj = Apply4_FindAddedObject(7777, 1234);
    CHECK(obj != nullptr);
    // transform bytes copied to obj+28..obj+28+0x1C
    for (int i = 0; i < 0x1C; ++i) CHECK_EQ((int)obj[28 + i], (int)(u8)(0x10 + i));
    CHECK_EQ((int)obj[56], 0xAB);
    CHECK_EQ((int)obj[57], 0xCD);
    CHECK_EQ((int)obj[58], 0x5F);
    // g_lastTradeId set to the new object id (dword @ obj+2)
    i32 objId; std::memcpy(&objId, obj + 2, 4);
    CHECK_EQ(g_lastTradeId, objId);
}

TEST(SimCmdApply4, SetObjectTransform_RemapToken) {
    Seed4();
    g_lastObjectId = 4242;
    CommandPacket p{}; p.opcode() = kOp4SetObjectTransform;
    p.put32(0x10, (u32)(-2));       // remap -> g_lastObjectId
    p.bytes[20] = 55; p.bytes[21] = 0; // proto = 55
    AckEntry ack{};
    CHECK_EQ(ApplyPacket4(p, &ack), 0);
    CHECK(Apply4_FindAddedObject(4242, 55) != nullptr);
    // packet id rewritten in place
    CHECK_EQ((i32)p.get32(0x10), 4242);
}

TEST(SimCmdApply4, SetObjectTransform_AddFailReturns1) {
    Seed4();
    SetAddObjektHook([](i32, i32, i32) -> u8* { return nullptr; });
    CommandPacket p{}; p.opcode() = kOp4SetObjectTransform;
    p.put32(0x10, 1); p.bytes[20] = 1; p.bytes[21] = 0;
    AckEntry ack{};
    CHECK_EQ(ApplyPacket4(p, &ack), 1);
}

TEST(SimCmdApply4, SpawnEffectObject_Proto437) {
    Seed4();
    CommandPacket p{}; p.opcode() = kOp4SpawnEffectObject;
    p.put32(0x14, 900);             // container id
    p.put32(24, 0xDEADBEEF);        // dword -> obj+28
    p.bytes[28] = 0x11; p.bytes[29] = 0x22; // word -> obj+32
    p.bytes[30] = 0x99;             // byte -> obj+34 (then cleared)
    AckEntry ack{};
    CHECK_EQ(ApplyPacket4(p, &ack), 0);
    CHECK_EQ((int)ack.slot, 3);
    u8* obj = Apply4_FindAddedObject(900, 437);
    CHECK(obj != nullptr);
    CHECK_EQ((int)obj[18], 64);
    u32 d; std::memcpy(&d, obj + 28, 4);
    CHECK_EQ(d, 0xDEADBEEFu);
    CHECK_EQ((int)obj[32], 0x11);
    CHECK_EQ((int)obj[33], 0x22);
    CHECK_EQ((int)obj[34], 0); // cleared
}

TEST(SimCmdApply4, AdjustObjectTransform_AddsDeltas) {
    Seed4();
    i32* slot = Apply4_SeedTransformSlot(3, 88);
    CHECK(slot != nullptr);
    slot[4] = 100; slot[5] = 200; slot[6] = 300;
    float f8 = 1.0f, f9 = 2.0f, f11 = 3.0f, f14 = 4.0f;
    std::memcpy(&slot[8], &f8, 4); std::memcpy(&slot[9], &f9, 4);
    std::memcpy(&slot[11], &f11, 4); std::memcpy(&slot[14], &f14, 4);

    CommandPacket p{}; p.opcode() = kOp4AdjustObjectTransform;
    p.bytes[0x10] = 3;                       // type byte (a1+16)
    p.bytes[0x11] = 88; p.bytes[0x12] = 0;   // proto = HIWORD(dword@+0x0F) -> bytes 0x11,0x12
    p.put32(0x13, (u32)10);                  // int delta -> [4]
    p.put32(0x17, (u32)20);                  // -> [5]
    p.put32(0x1B, (u32)(-30));               // -> [6]
    float d8 = 0.5f, d9 = -1.0f, d11 = 10.0f, d14 = 0.25f;
    std::memcpy(p.bytes + 0x1F, &d8, 4);
    std::memcpy(p.bytes + 0x23, &d9, 4);
    std::memcpy(p.bytes + 0x27, &d11, 4);
    std::memcpy(p.bytes + 0x2B, &d14, 4);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket4(p, &ack), 0);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ(slot[4], 110);
    CHECK_EQ(slot[5], 220);
    CHECK_EQ(slot[6], 270);
    float r8, r9, r11, r14;
    std::memcpy(&r8, &slot[8], 4); std::memcpy(&r9, &slot[9], 4);
    std::memcpy(&r11, &slot[11], 4); std::memcpy(&r14, &slot[14], 4);
    CHECK(r8 == 1.5f); CHECK(r9 == 1.0f); CHECK(r11 == 13.0f); CHECK(r14 == 4.25f);
}

TEST(SimCmdApply4, AdjustObjectTransform_MissReturns1) {
    Seed4();
    CommandPacket p{}; p.opcode() = kOp4AdjustObjectTransform;
    p.bytes[0x10] = 9; p.put32(0x0F, (1u << 16));
    AckEntry ack{};
    CHECK_EQ(ApplyPacket4(p, &ack), 1);
}

// ===========================================================================
// Straftat (crime) table.
// ===========================================================================

TEST(SimCmdApply4, AddStraftat_CopiesRecordAndStampsCounter) {
    Seed4();
    Apply4_SetStandalone(true);
    g_straftatCounter = 41; // ++ -> 42
    CommandPacket p{}; p.opcode() = kOp4AddStraftat;
    for (int i = 0; i < 0x2D; ++i) p.bytes[0x10 + i] = static_cast<u8>(0x60 + i);
    p.put32(0x10, 999999); // a pre-existing id that gets overwritten by counter
    p.put32(38, -1);       // taeter id = -1 (no person)
    AckEntry ack{};
    CHECK_EQ(ApplyPacket4(p, &ack), 0);
    CHECK_EQ(g_straftatCounter, 42);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ((int)ack.slot, 6);
    u8* slot = Apply4_StraftatSlot(0);
    CHECK(slot != nullptr);
    i32 stid; std::memcpy(&stid, slot, 4);
    CHECK_EQ(stid, 42); // stamped with the counter, not the payload id
    // remaining record bytes (offset 4..0x2C) match the payload copy
    for (int i = 4; i < 0x2D; ++i) CHECK_EQ((int)slot[i], (int)p.bytes[0x10 + i]);
}

TEST(SimCmdApply4, AddStraftat_NetworkedUsesPayloadId) {
    Seed4();
    Apply4_SetStandalone(false);
    CommandPacket p{}; p.opcode() = kOp4AddStraftat;
    p.put32(0x10, 7000);
    p.put32(38, -1);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket4(p, &ack), 0);
    CHECK_EQ(g_straftatCounter, 7000);
    u8* slot = Apply4_StraftatSlot(0);
    i32 stid; std::memcpy(&stid, slot, 4);
    CHECK_EQ(stid, 7000);
}

TEST(SimCmdApply4, AddStraftat_FullReturnsMinus1) {
    Seed4();
    SetStraftatFreeHook([]() { return -1; });
    CommandPacket p{}; p.opcode() = kOp4AddStraftat;
    p.put32(38, -1);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket4(p, &ack), -1);
}

TEST(SimCmdApply4, DispatchStatusResult_PropagatesCodes) {
    Seed4();
    static int s_code = 0;
    SetStraftatResolveHook([](i32, i32) { return s_code; });
    CommandPacket p{}; p.opcode() = kOp4DispatchStatusResult;
    p.put32(0x10, 5); p.put32(0x14, 6);
    AckEntry ack{};
    s_code = 0; ack.status = 0;
    CHECK_EQ(ApplyPacket4(p, &ack), 0);
    CHECK_EQ((int)ack.status, 1);
    s_code = 1; ack.status = 0;
    CHECK_EQ(ApplyPacket4(p, &ack), 1);
    CHECK_EQ((int)ack.status, 0); // unchanged on reject
    s_code = 3;
    CHECK_EQ(ApplyPacket4(p, &ack), 3);
}

TEST(SimCmdApply4, QueryObjectStatus_StatusFromCount) {
    Seed4();
    static int s_n = 1;
    SetStraftatUpdateHook([](i32, i32, i32, i32) { return s_n; });
    CommandPacket p{}; p.opcode() = kOp4QueryObjectStatus;
    AckEntry ack{};
    s_n = 2;
    CHECK_EQ(ApplyPacket4(p, &ack), 0);
    CHECK_EQ((int)ack.status, 1);
    s_n = 0;
    CHECK_EQ(ApplyPacket4(p, &ack), 0);
    CHECK_EQ((int)ack.status, 2);
}

// ===========================================================================
// Cut-info slot table.
// ===========================================================================

TEST(SimCmdApply4, SendCutInfo_FindsSlotClearsTime) {
    Seed4();
    CutInfoSlot* s = Apply4_CutInfoSlot(3);
    s->playerId = 55; s->time = 999;
    CommandPacket p{}; p.opcode() = kOp4SendCutInfo;
    p.put32(0x10, 55);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket4(p, &ack), 0);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ((int)ack.slot, 8);
    CHECK_EQ(ack.seq, 3);
    CHECK_EQ(s->time, 0);
}

TEST(SimCmdApply4, SendCutInfo_NoSlotReturns1) {
    Seed4();
    CommandPacket p{}; p.opcode() = kOp4SendCutInfo;
    p.put32(0x10, 123);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket4(p, &ack), 1);
}

// ===========================================================================
// Person / character lifecycle.
// ===========================================================================

TEST(SimCmdApply4, RemovePersonAndScript_FullFlow) {
    Seed4();
    Person* pr = MakePerson(2, 500);
    SetPersonCharPtr(pr, 0x1234); // has a character -> script finish runs
    CommandPacket p{}; p.opcode() = kOp4RemovePersonAndScript;
    p.put32(0x10, 500);
    p.put32(0x14, 9);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket4(p, &ack), 0);
    CHECK_EQ((int)ack.status, 1);
    const auto& log = Apply4_CharLog();
    CHECK_EQ(log.changeActionCount, 1);
    CHECK_EQ(log.lastChangeActionId, 500);
    CHECK_EQ(log.scriptFinishCount, 1);
    CHECK_EQ(log.lastScriptHandle, 0x1234);
    CHECK_EQ(log.buildingRemoveCount, 1);
}

TEST(SimCmdApply4, RemovePersonAndScript_NoScriptWhenNoChar) {
    Seed4();
    MakePerson(2, 501); // charPtr stays 0
    CommandPacket p{}; p.opcode() = kOp4RemovePersonAndScript;
    p.put32(0x10, 501); p.put32(0x14, 0);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket4(p, &ack), 0);
    CHECK_EQ(Apply4_CharLog().scriptFinishCount, 0);
    CHECK_EQ(Apply4_CharLog().buildingRemoveCount, 1);
}

TEST(SimCmdApply4, RemovePersonAndScript_MissingPersonLeavesAck2) {
    Seed4();
    CommandPacket p{}; p.opcode() = kOp4RemovePersonAndScript;
    p.put32(0x10, 12345);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket4(p, &ack), 0);
    CHECK_EQ((int)ack.status, 2);
    CHECK_EQ(Apply4_CharLog().changeActionCount, 0);
}

TEST(SimCmdApply4, SetObjectParent_ResolvesAndCalls) {
    Seed4();
    MakeObject(0, 800);   // owner object
    MakePerson(1, 810);   // child
    MakePerson(2, 820);   // new parent
    CommandPacket p{}; p.opcode() = kOp4SetObjectParent;
    p.put32(0x10, 800);
    p.put32(0x18, 810);
    p.put32(0x14, 820);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket4(p, &ack), 0);
    CHECK_EQ((int)ack.status, 2); // original leaves status 2
    CHECK_EQ(Apply4_CharLog().setParentCount, 1);
}

TEST(SimCmdApply4, SetObjectParent_NoOwnerReturns1) {
    Seed4();
    CommandPacket p{}; p.opcode() = kOp4SetObjectParent;
    p.put32(0x10, 1); p.put32(0x18, -1); p.put32(0x14, -1);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket4(p, &ack), 1);
}

TEST(SimCmdApply4, SetObjectParent_NoNewParentReturns2) {
    Seed4();
    MakeObject(0, 800);
    CommandPacket p{}; p.opcode() = kOp4SetObjectParent;
    p.put32(0x10, 800); p.put32(0x18, -1); p.put32(0x14, 555 /*missing*/);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket4(p, &ack), 2);
}

TEST(SimCmdApply4, ActivateObject_SpawnsChimneyForNonProduction) {
    Seed4();
    ObjectRec* o = MakeObject(0, 700);
    i32 bld = 0x55; std::memcpy(reinterpret_cast<u8*>(o) + 97, &bld, 4);
    SetIsProductionHook([](i32) { return 0; }); // not production -> smoke
    CommandPacket p{}; p.opcode() = kOp4ActivateObject;
    p.put32(0x10, 700);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket4(p, &ack), 0);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ(Apply4_CharLog().chimneyCount, 1);
}

TEST(SimCmdApply4, ActivateObject_NoSmokeForProduction) {
    Seed4();
    ObjectRec* o = MakeObject(0, 701);
    i32 bld = 0x55; std::memcpy(reinterpret_cast<u8*>(o) + 97, &bld, 4);
    SetIsProductionHook([](i32) { return 1; });
    CommandPacket p{}; p.opcode() = kOp4ActivateObject;
    p.put32(0x10, 701);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket4(p, &ack), 0);
    CHECK_EQ(Apply4_CharLog().chimneyCount, 0);
}

TEST(SimCmdApply4, ChangePlayerHead_Calls) {
    Seed4();
    MakePerson(4, 600);
    CommandPacket p{}; p.opcode() = kOp4ChangePlayerHead;
    p.put32(0x10, 600); p.put32(0x14, 77);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket4(p, &ack), 0);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ(Apply4_CharLog().changeActionCount, 1);
    CHECK_EQ(Apply4_CharLog().lastChangeActionId, 600);
}

TEST(SimCmdApply4, StopCharacterScript_FinishesWhenChar) {
    Seed4();
    Person* pr = MakePerson(5, 610);
    SetPersonCharPtr(pr, 0xABCD);
    CommandPacket p{}; p.opcode() = kOp4StopCharacterScript;
    p.put32(0x10, 610);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket4(p, &ack), 0);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ(Apply4_CharLog().scriptFinishCount, 1);
    CHECK_EQ(Apply4_CharLog().lastScriptHandle, 0xABCD);
}

TEST(SimCmdApply4, EnqueueCharacterAction_QueuesTag) {
    Seed4();
    CommandPacket p{}; p.opcode() = kOp4EnqueueCharacterAction;
    p.put32(0x10, 333);
    p.bytes[0x14] = 1;             // secondary actor flag -> tag 0x2D
    p.put32(0x28, 11); p.put32(0x2C, 22);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket4(p, &ack), 0);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ(Apply4_CharLog().queueActionCount, 1);
    CHECK_EQ(Apply4_CharLog().lastActorId, 333);
    CHECK_EQ(Apply4_CharLog().lastActionTag, 0x2D);
    // no flag -> tag 0x3A
    Seed4();
    p.bytes[0x14] = 0;
    ApplyPacket4(p, &ack);
    CHECK_EQ(Apply4_CharLog().lastActionTag, 0x3A);
}

TEST(SimCmdApply4, EnqueueCharacterAction_NoActorReturns1) {
    Seed4();
    SetCharFindAndQueueHook([](i32, i32, i32, i32) { return 0; });
    CommandPacket p{}; p.opcode() = kOp4EnqueueCharacterAction;
    p.put32(0x10, 333);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket4(p, &ack), 1);
}

TEST(SimCmdApply4, DestroyCharacter_DestroysAndClears) {
    Seed4();
    Person* pr = MakePerson(6, 620);
    SetPersonCharPtr(pr, 0x9999);
    CommandPacket p{}; p.opcode() = kOp4DestroyCharacter;
    p.put32(0x10, 620);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket4(p, &ack), 0);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ(Apply4_CharLog().charDestroyCount, 1);
    CHECK_EQ(PersonCharPtr(pr), 0); // cleared
}

TEST(SimCmdApply4, DestroyCharacter_NoCharReturns1) {
    Seed4();
    MakePerson(6, 621); // charPtr 0
    CommandPacket p{}; p.opcode() = kOp4DestroyCharacter;
    p.put32(0x10, 621);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket4(p, &ack), 1);
    CHECK_EQ((int)ack.status, 2); // entry-stamp only
}

TEST(SimCmdApply4, ClearObjectOccupants_ClearsMatchingCutsceneIds) {
    Seed4();
    Person* a = MakePerson(0, 1000);
    Person* b = MakePerson(1, 1001);
    Person* c = MakePerson(2, 1002);
    SetPersonCutsceneId(a, 77);  // matches slot id -> cleared
    SetPersonCutsceneId(b, 99);  // different -> kept
    SetPersonCutsceneId(c, 77);  // matches -> cleared
    i32 ids[3] = {1000, 1001, 1002};
    CHECK(Apply4_SeedCutsceneSlot(/*id=*/77, ids, 3));

    CommandPacket p{}; p.opcode() = kOp4ClearObjectOccupants;
    p.put32(0x10, 77);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket4(p, &ack), 0);
    CHECK_EQ((int)ack.status, 1);
    CHECK_EQ(PersonCutsceneId(a), -1);
    CHECK_EQ(PersonCutsceneId(b), 99);
    CHECK_EQ(PersonCutsceneId(c), -1);
}

TEST(SimCmdApply4, ClearObjectOccupants_MissingSlotReturns1) {
    Seed4();
    CommandPacket p{}; p.opcode() = kOp4ClearObjectOccupants;
    p.put32(0x10, 555);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket4(p, &ack), 1);
}

// ===========================================================================
// Unknown / guarded opcode is ignored (no state change).
// ===========================================================================

TEST(SimCmdApply4, UnknownOpcodeIgnored) {
    Seed4();
    g_globalFlags = 0x33;
    CommandPacket p{}; p.opcode() = 0x16; // owned by batch 1, not batch 4
    AckEntry ack{}; ack.status = 5;
    CHECK_EQ(ApplyPacket4(p, &ack), -1);
    CHECK_EQ((int)g_globalFlags, 0x33); // untouched
    CHECK_EQ((int)ack.status, 5);
}

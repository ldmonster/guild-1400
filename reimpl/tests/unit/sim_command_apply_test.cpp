#include "sim/command.h"
#include "sim/command_codec.h"
#include "sim/command_apply.h"
#include "sim/entity.h"
#include "sim/types.h"
#include "test.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// Helpers for building apply packets and seeding entity records.
// ---------------------------------------------------------------------------
namespace {

void SeedWorld() {
    // Fully zero the record arrays first: ResetEntityArrays only resets the
    // alive markers / id columns, leaving the rest of each record at its prior
    // value. The apply handlers mutate arbitrary record bytes, so a clean slate
    // is required for repeatable expectations.
    std::memset(g_persons, 0, sizeof(Person) * kPersonCapacity);
    std::memset(g_objects, 0, sizeof(ObjectRec) * kObjectCapacity);
    std::memset(g_sceneNodes, 0, sizeof(SceneNode) * kSceneNodeCapacity);
    ResetEntityArrays();
    ResetIdPairTable();
    g_sceneArrayLoaded  = true;   // ResolveEntityById early-outs to 0 otherwise
    g_personArrayLoaded = true;
    g_lastObjectId = -1;
    g_lastSceneId  = -1;
    g_lastTradeId  = -1;
}

// Install a live Person record at slot `slot` with id `id`.
Person* MakePerson(int slot, i32 id) {
    g_persons[slot].marker = 0;      // alive
    g_persons[slot].kind   = 4;
    g_persons[slot].id     = id;     // +4 column read by ResolveEntityById
    g_personIds[slot]      = id;     // parallel column read by FindRecordById
    return &g_persons[slot];
}

// Install a live Object record at slot `slot` with id `id`.
ObjectRec* MakeObject(int slot, i32 id) {
    g_objects[slot].alive = 1;
    g_objects[slot].id    = id;
    return &g_objects[slot];
}

// Build a field-patch packet (opcode 0x16 PatchAdd / 0x17 Write) from a built
// DeltaWriter payload: id @+0x10, field-count byte @+0x14, records @+0x15.
CommandPacket MakeFieldPatch(u8 opcode, i32 entityId, const DeltaWriter& dw) {
    CommandPacket p{};
    p.opcode() = opcode;
    p.put32(0x10, static_cast<u32>(entityId));
    p.bytes[0x14] = dw.field_count();
    std::memcpy(p.bytes + 0x15, dw.payload(), dw.cursor());
    return p;
}

// Build a need-delta packet (opcode 0x18): id @+0x10, entry-count @+0x14,
// 5-byte entries @+0x15. Mirrors the AiMethodWriter body but with the 0x14
// count and the apply-side per-entry [statId:1][float:4] layout.
CommandPacket MakeNeedDelta(i32 entityId, const u8* statIds, const float* deltas, int n) {
    CommandPacket p{};
    p.opcode() = 0x18;
    p.put32(0x10, static_cast<u32>(entityId));
    p.bytes[0x14] = static_cast<u8>(n);
    u8* out = p.bytes + 0x15;
    for (int i = 0; i < n; ++i) {
        out[0] = statIds[i];
        std::memcpy(out + 1, &deltas[i], 4);
        out += 5;
    }
    return p;
}

} // namespace

// ---------------------------------------------------------------------------
// 0x16 ExPatchObjectFieldsAdd — delta ADD round-trip against an Object record.
// ---------------------------------------------------------------------------
TEST(SimCmdApply, PatchObjectFieldsAdd_Object) {
    SeedWorld();
    ObjectRec* obj = MakeObject(7, 4242);
    // Seed a known byte field at +0x20 and a dword at +0x40.
    reinterpret_cast<u8*>(obj)[0x20] = 100;
    u32 base = 0x11223344; std::memcpy(reinterpret_cast<u8*>(obj) + 0x40, &base, 4);

    // Encode (new - old) deltas against the live object: byte 100->150, dword +5.
    DeltaWriter dw;
    dw.BeginDeltaPacket(obj, 4242);
    u8  newByte = 150;
    u32 newDword = base + 5;
    dw.AppendDeltaField(1, 1, 0x20, &newByte);
    dw.AppendDeltaField(4, 1, 0x40, &newDword);

    CommandPacket p = MakeFieldPatch(0x16, 4242, dw);
    AckEntry ack{}; ack.status = 0;
    int r = ApplyPacket(p, &ack);
    CHECK_EQ(r, 0);
    CHECK_EQ(ack.status, 1); // success stamp

    CHECK_EQ(reinterpret_cast<u8*>(obj)[0x20], 150);
    u32 got; std::memcpy(&got, reinterpret_cast<u8*>(obj) + 0x40, 4);
    CHECK_EQ(got, base + 5);
}

// ---------------------------------------------------------------------------
// 0x17 ExWriteObjectFields — absolute write against an Object record.
// ---------------------------------------------------------------------------
TEST(SimCmdApply, WriteObjectFields_Absolute) {
    SeedWorld();
    ObjectRec* obj = MakeObject(3, 99);
    reinterpret_cast<u8*>(obj)[0x10] = 1;

    DeltaWriter dw;
    dw.BeginDeltaPacket(obj, 99);
    u16 w = 0xBEEF;
    dw.AppendRawField(2, 1, 0x10, &w);

    CommandPacket p = MakeFieldPatch(0x17, 99, dw);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket(p, &ack), 0);
    u16 got; std::memcpy(&got, reinterpret_cast<u8*>(obj) + 0x10, 2);
    CHECK_EQ(got, 0xBEEF);
}

// ---------------------------------------------------------------------------
// 0x18 ExApplyNeedDeltas — float need add + clamp [0, 1000] on a Person.
// ---------------------------------------------------------------------------
TEST(SimCmdApply, ApplyNeedDeltas_ClampHigh) {
    SeedWorld();
    Person* per = MakePerson(5, 700);
    u8* base = reinterpret_cast<u8*>(per);
    // stat 2 -> offset 144 + 12*2 = 168; seed 990.0f.
    float seed = 990.0f; std::memcpy(base + 144 + 12 * 2, &seed, 4);

    u8 stat = 2; float add = 50.0f; // 990 + 50 = 1040 -> clamps to 1000
    CommandPacket p = MakeNeedDelta(700, &stat, &add, 1);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket(p, &ack), 0);
    float got; std::memcpy(&got, base + 168, 4);
    CHECK(got == 1000.0f);
}

TEST(SimCmdApply, ApplyNeedDeltas_ClampLow) {
    SeedWorld();
    Person* per = MakePerson(6, 701);
    u8* base = reinterpret_cast<u8*>(per);
    float seed = 10.0f; std::memcpy(base + 144 + 12 * 0, &seed, 4);

    u8 stat = 0; float add = -50.0f; // 10 - 50 = -40 -> clamps to 0
    CommandPacket p = MakeNeedDelta(701, &stat, &add, 1);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket(p, &ack), 0);
    float got; std::memcpy(&got, base + 144, 4);
    CHECK(got == 0.0f);
}

// ---------------------------------------------------------------------------
// 0x19 ExPatchObjectBitfield — clear-mask then set-bits on an Object dword.
// ---------------------------------------------------------------------------
TEST(SimCmdApply, PatchObjectBitfield_Dword) {
    SeedWorld();
    ObjectRec* obj = MakeObject(2, 555);
    u8* base = reinterpret_cast<u8*>(obj);
    u32 cur = 0x0000FF0F; std::memcpy(base + 0x30, &cur, 4);

    CommandPacket p{};
    p.opcode() = 0x19;
    p.put32(0x10, 555);
    p.put32(0x14, 0x30);        // offset
    p.put32(0x18, 4);            // width
    p.put32(0x1C, 0x00AA0000);   // set bits
    p.put32(0x20, 0x0000FFFF);   // mask to clear
    AckEntry ack{};
    CHECK_EQ(ApplyPacket(p, &ack), 0);
    // (cur & ~mask) | set = (0x0000FF0F & 0xFFFF0000) | 0x00AA0000 = 0x00AA0000
    u32 got; std::memcpy(&got, base + 0x30, 4);
    CHECK_EQ(got, 0x00AA0000u);
}

// ---------------------------------------------------------------------------
// 0x1A ExAddObjectFloatField — add to a float; person-priority resolve.
// ---------------------------------------------------------------------------
TEST(SimCmdApply, AddObjectFloatField_Person) {
    SeedWorld();
    Person* per = MakePerson(9, 808);
    u8* base = reinterpret_cast<u8*>(per);
    float cur = 1.5f; std::memcpy(base + 0x60, &cur, 4);

    CommandPacket p{};
    p.opcode() = 0x1A;
    p.put32(0x10, 808);
    p.put32(0x14, 0x60); // offset
    float add = 2.25f; std::memcpy(p.bytes + 0x18, &add, 4);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket(p, &ack), 0);
    float got; std::memcpy(&got, base + 0x60, 4);
    CHECK(got == 3.75f);
}

// ---------------------------------------------------------------------------
// 0x24 / 0x25 — id-pair register / unregister round-trip.
// ---------------------------------------------------------------------------
TEST(SimCmdApply, IdPairRegisterThenUnregister) {
    SeedWorld();
    CommandPacket reg{};
    reg.opcode() = 0x24;
    reg.put32(0x10, 1234); // id   -> column B
    reg.put32(0x14, 7);    // kind -> column A
    AckEntry ack{};
    CHECK_EQ(ApplyPacket(reg, &ack), 0);
    CHECK_EQ(ack.status, 1);
    // The pair now lives in some slot.
    bool found = false;
    for (int i = 0; i < kIdPairSlots; ++i)
        if (g_idPairA[i] == 7 && g_idPairB[i] == 1234) found = true;
    CHECK(found);

    CommandPacket unreg{};
    unreg.opcode() = 0x25;
    unreg.put32(0x10, 1234); // B
    unreg.put32(0x14, 7);    // A
    AckEntry ack2{};
    CHECK_EQ(ApplyPacket(unreg, &ack2), 0);
    CHECK_EQ(ack2.status, 1);
    found = false;
    for (int i = 0; i < kIdPairSlots; ++i)
        if (g_idPairA[i] == 7 && g_idPairB[i] == 1234) found = true;
    CHECK(!found);

    // Unregistering a missing pair returns 1 and leaves ack untouched.
    AckEntry ack3{}; ack3.status = 9;
    CHECK_EQ(ApplyPacket(unreg, &ack3), 1);
    CHECK_EQ(ack3.status, 9);
}

// ---------------------------------------------------------------------------
// 0x5A ExAdjustObjectCounter — add to person+0x194, clamp [0,50].
// ---------------------------------------------------------------------------
TEST(SimCmdApply, AdjustObjectCounter_Clamp) {
    SeedWorld();
    Person* per = MakePerson(1, 11);
    u8* base = reinterpret_cast<u8*>(per);
    i32 seed = 45; std::memcpy(base + 0x194, &seed, 4);

    CommandPacket p{};
    p.opcode() = 0x5A;
    p.put32(0x10, 11);
    p.put32(0x14, 20); // 45 + 20 = 65 -> 50
    AckEntry ack{};
    CHECK_EQ(ApplyPacket(p, &ack), 0);
    CHECK_EQ(ack.status, 1);
    i32 got; std::memcpy(&got, base + 0x194, 4);
    CHECK_EQ(got, 50);

    // Negative under-clamp.
    seed = 5; std::memcpy(base + 0x194, &seed, 4);
    p.put32(0x14, static_cast<u32>(-20)); // 5 - 20 = -15 -> 0
    CHECK_EQ(ApplyPacket(p, &ack), 0);
    std::memcpy(&got, base + 0x194, 4);
    CHECK_EQ(got, 0);

    // Unknown id -> returns 1, ack left in "in-progress" (2) state.
    p.put32(0x10, 999999);
    AckEntry ack2{};
    CHECK_EQ(ApplyPacket(p, &ack2), 1);
    CHECK_EQ(ack2.status, 2);
}

// ---------------------------------------------------------------------------
// 0x5B ExAdjustCharacterReputation — add to person+0x1B1 byte, clamp [0,254→0xFF].
// ---------------------------------------------------------------------------
TEST(SimCmdApply, AdjustCharacterReputation_Clamp) {
    SeedWorld();
    Person* per = MakePerson(4, 22);
    u8* base = reinterpret_cast<u8*>(per);
    base[0x1B1] = 200;

    CommandPacket p{};
    p.opcode() = 0x5B;
    p.put32(0x10, 22);
    p.put32(0x14, 100); // 200 + 100 = 300 > 254 -> 0xFF
    AckEntry ack{};
    CHECK_EQ(ApplyPacket(p, &ack), 0);
    CHECK_EQ(base[0x1B1], 0xFF);

    base[0x1B1] = 10;
    p.put32(0x14, static_cast<u32>(-50)); // 10 - 50 = -40 -> 0
    CHECK_EQ(ApplyPacket(p, &ack), 0);
    CHECK_EQ(base[0x1B1], 0);

    base[0x1B1] = 30;
    p.put32(0x14, 20); // 30 + 20 = 50, in range
    CHECK_EQ(ApplyPacket(p, &ack), 0);
    CHECK_EQ(base[0x1B1], 50);
}

// ---------------------------------------------------------------------------
// 0x5D ExSetObjectFillLevel — add to person[0x80+idx] byte, clamp [0,252].
// ---------------------------------------------------------------------------
TEST(SimCmdApply, SetObjectFillLevel_Clamp) {
    SeedWorld();
    Person* per = MakePerson(8, 33);
    u8* base = reinterpret_cast<u8*>(per);
    base[0x80 + 3] = 250;

    CommandPacket p{};
    p.opcode() = 0x5D;
    p.put32(0x10, 33);
    p.put32(0x14, 3);   // index
    p.put32(0x18, 10);  // 250 + 10 = 260 -> 252
    AckEntry ack{};
    CHECK_EQ(ApplyPacket(p, &ack), 0);
    CHECK_EQ(base[0x80 + 3], 252);

    // index out of range -> reject.
    p.put32(0x14, 5);
    AckEntry ack2{};
    CHECK_EQ(ApplyPacket(p, &ack2), 1);
}

// ---------------------------------------------------------------------------
// -2 remap token resolves to the last-created object id.
// ---------------------------------------------------------------------------
TEST(SimCmdApply, RemapLastObjectToken) {
    SeedWorld();
    ObjectRec* obj = MakeObject(0, 5000);
    g_lastObjectId = 5000; // dword_631288
    reinterpret_cast<u8*>(obj)[0x18] = 7;

    DeltaWriter dw;
    dw.BeginDeltaPacket(obj, 5000);
    u8 nv = 9; dw.AppendRawField(1, 1, 0x18, &nv);
    CommandPacket p = MakeFieldPatch(0x17, -2, dw); // id == -2 sentinel
    AckEntry ack{};
    CHECK_EQ(ApplyPacket(p, &ack), 0);
    CHECK_EQ(reinterpret_cast<u8*>(obj)[0x18], 9);
    // The handler rewrites the packet id field to the resolved concrete id.
    CHECK_EQ(static_cast<i32>(p.get32(0x10)), 5000);
}

// ---------------------------------------------------------------------------
// Delegating handlers + unknown-opcode safety.
// ---------------------------------------------------------------------------
TEST(SimCmdApply, EndTurnHook) {
    SeedWorld();
    static int turnCalls = 0;
    turnCalls = 0;
    SetTurnControlHook([]() -> int { ++turnCalls; return 1; });
    CommandPacket p{}; p.opcode() = 0x56;
    AckEntry ack{};
    CHECK_EQ(ApplyPacket(p, &ack), 0);
    CHECK_EQ(turnCalls, 1);
    CHECK_EQ(ack.status, 1);

    // Rejecting turn-control leaves the ack in-progress (status 2).
    SetTurnControlHook([]() -> int { return 0; });
    AckEntry ack2{};
    CHECK_EQ(ApplyPacket(p, &ack2), 0);
    CHECK_EQ(ack2.status, 2);
    SetTurnControlHook(nullptr); // restore default
}

TEST(SimCmdApply, AckCityHook) {
    SeedWorld();
    static int cityCalls = 0;
    cityCalls = 0;
    SetCityAckHook([]() { ++cityCalls; });
    CommandPacket p{}; p.opcode() = 0x41;
    AckEntry ack{};
    CHECK_EQ(ApplyPacket(p, &ack), 0);
    CHECK_EQ(cityCalls, 1);
    CHECK_EQ(ack.status, 1);
    SetCityAckHook(nullptr);
}

TEST(SimCmdApply, UnknownOpcodeIgnored) {
    SeedWorld();
    // An opcode we did not translate is a safe no-op via ApplyPacket (-1).
    CommandPacket p{}; p.opcode() = 0x2F; // ExChrWalkToDummy (deferred)
    AckEntry ack{}; ack.status = 3;
    CHECK_EQ(ApplyPacket(p, &ack), -1);
    CHECK_EQ(ack.status, 3); // untouched
}

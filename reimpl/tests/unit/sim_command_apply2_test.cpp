#include "sim/command.h"
#include "sim/command_apply.h"   // shared g_last* tokens + ResetIdPairTable
#include "sim/command_apply2.h"
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

void Seed2() {
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
}

Person* MakePerson(int slot, i32 id) {
    g_persons[slot].marker = 0;
    g_persons[slot].kind   = 4;
    g_persons[slot].id     = id;
    g_personIds[slot]      = id;
    return &g_persons[slot];
}
ObjectRec* MakeObject(int slot, i32 id) {
    g_objects[slot].alive = 1;
    g_objects[slot].id    = id;
    return &g_objects[slot];
}

} // namespace

// ---------------------------------------------------------------------------
// 0x1D ExSetHE — scatter-write 13 fields into a He record.
// ---------------------------------------------------------------------------
TEST(SimCmdApply2, SetHE_Fields) {
    Seed2();
    HeRecord* he = Apply2_SeedHe(/*id=*/77);
    CHECK(he != nullptr);

    CommandPacket p{};
    p.opcode() = 0x1D;
    p.put32(0x10, 77);
    p.put32(20, 0x11111111);
    p.put32(24, 0x22222222);
    p.put32(28, 0x33333333);
    p.put16(32, 0x4444);
    p.put32(34, 0x55555555);
    p.put32(38, 0x66666666);
    p.put32(42, 0x77777777);
    p.put16(46, 0x8888);
    p.put32(48, 0x99999999);
    p.put32(52, 0xAAAAAAAA);
    // NOTE: in the original wire layout the +56 dword (-> He+104), the +60 word
    // (-> He+108), and the +59 dword (>>24 -> He+112) OVERLAP on bytes 59..62.
    // Set the overlapping region last and derive the expectations from the bytes.
    p.put32(56, 0xBBBBBBBB);
    p.put16(60, 0xCCCC);
    p.put32(59, 0x12345678); // (0x12345678 >> 24) == 0x12; clobbers bytes 59..62

    AckEntry ack{};
    CHECK_EQ(ApplyPacket2(p, &ack), 0);
    CHECK_EQ(ack.status, 1);
    CHECK_EQ(ack.slot, 4);

    // Recompute the overlapping field expectations directly from the packet bytes
    // (this is exactly what the handler reads — overlap and all).
    u32 exp104 = p.get32(56);
    u16 exp108 = p.get16(60);
    i32 v59; std::memcpy(&v59, p.bytes + 59, 4); i32 exp112 = v59 >> 24;

    u32 g; u16 w; i32 s;
    std::memcpy(&g, he->bytes + 68, 4);  CHECK_EQ(g, 0x11111111u);
    std::memcpy(&g, he->bytes + 72, 4);  CHECK_EQ(g, 0x22222222u);
    std::memcpy(&g, he->bytes + 76, 4);  CHECK_EQ(g, 0x33333333u);
    std::memcpy(&w, he->bytes + 80, 2);  CHECK_EQ(w, 0x4444);
    std::memcpy(&g, he->bytes + 82, 4);  CHECK_EQ(g, 0x55555555u);
    std::memcpy(&g, he->bytes + 86, 4);  CHECK_EQ(g, 0x66666666u);
    std::memcpy(&g, he->bytes + 90, 4);  CHECK_EQ(g, 0x77777777u);
    std::memcpy(&w, he->bytes + 94, 2);  CHECK_EQ(w, 0x8888);
    std::memcpy(&g, he->bytes + 96, 4);  CHECK_EQ(g, 0x99999999u);
    std::memcpy(&g, he->bytes + 100, 4); CHECK_EQ(g, 0xAAAAAAAAu);
    std::memcpy(&g, he->bytes + 104, 4); CHECK_EQ(g, exp104);
    std::memcpy(&w, he->bytes + 108, 2); CHECK_EQ(w, exp108);
    std::memcpy(&s, he->bytes + 112, 4); CHECK_EQ(s, exp112);
}

TEST(SimCmdApply2, SetHE_NotFound) {
    Seed2();
    CommandPacket p{}; p.opcode() = 0x1D; p.put32(0x10, 999);
    AckEntry ack{}; ack.status = 2;
    CHECK_EQ(ApplyPacket2(p, &ack), 1);
    CHECK_EQ(ack.status, 2); // left in-progress
}

// ---------------------------------------------------------------------------
// 0x3F ExSetObjectState — copy 0x80 state bytes into a building slot.
// ---------------------------------------------------------------------------
TEST(SimCmdApply2, SetObjectState_Copy) {
    Seed2();
    // proto comes from the SAME bytes (+0x11/+0x12) that are also the first two
    // bytes of the 0x80-byte state payload (the source region begins at +0x11).
    // So the proto key is determined by the leading payload bytes. Use a pattern
    // whose first two bytes give a known proto.
    BuildingSlot* slot = Apply2_SeedSlot(/*type=*/5, /*proto=*/0x0401);
    CHECK(slot != nullptr);

    CommandPacket p{};
    p.opcode() = 0x3F;
    p.bytes[0x10] = 5;                 // type byte
    // fill the 0x80-byte source region at +0x11 with a known pattern. Bytes
    // [0x11]=1, [0x12]=4 give proto = 1 | (4<<8) = 0x0401.
    for (int i = 0; i < 0x80; ++i) p.bytes[0x11 + i] = static_cast<u8>(i * 3 + 1);

    AckEntry ack{};
    CHECK_EQ(ApplyPacket2(p, &ack), 0);
    CHECK_EQ(ack.status, 1);
    for (int i = 0; i < 0x80; ++i)
        CHECK_EQ(slot->bytes[i], static_cast<u8>(i * 3 + 1));
}

TEST(SimCmdApply2, SetObjectState_NoSlot) {
    Seed2();
    CommandPacket p{}; p.opcode() = 0x3F; p.bytes[0x10] = 9;
    AckEntry ack{};
    CHECK_EQ(ApplyPacket2(p, &ack), 1);
}

// ---------------------------------------------------------------------------
// 0x5E ExUpsertTradeEntry — create a trade node + write/clamp its fields.
// ---------------------------------------------------------------------------
TEST(SimCmdApply2, UpsertTradeEntry_Create) {
    Seed2();
    MakeObject(0, /*owner=*/100);
    MakeObject(1, /*partner=*/200);

    CommandPacket p{};
    p.opcode() = 0x5E;
    p.put32(0x14, 100);          // owner building
    p.put32(0x26, 200);          // partner building (also node+42 key)
    p.put32(0x18, 0xDEAD0001);   // -> node[+28]
    p.put32(0x1C, 0xDEAD0002);   // -> node[+32]
    p.put32(0x20, 0xDEAD0003);   // -> node[+36]
    p.put16(0x24, 0x0AAA);       // -> node[+40] word
    p.put32(0x2E, 0xDEAD0005);   // -> node[+50]
    p.bytes[0x32] = 0x42;        // -> node[+54]
    p.bytes[0x33] = 80;          // pct add (node starts 0) -> 80

    AckEntry ack{};
    CHECK_EQ(ApplyPacket2(p, &ack), 0);
    CHECK_EQ(ack.status, 1);
    // The new node bumped g_lastTradeId.
    CHECK(g_lastTradeId != -1);

    TradeNode* node = Apply2_FindTradeNode(100, 200);
    CHECK(node != nullptr);
    u32 g; u16 w;
    std::memcpy(&g, node->bytes + 28, 4); CHECK_EQ(g, 0xDEAD0001u);
    std::memcpy(&g, node->bytes + 32, 4); CHECK_EQ(g, 0xDEAD0002u);
    std::memcpy(&g, node->bytes + 36, 4); CHECK_EQ(g, 0xDEAD0003u);
    std::memcpy(&w, node->bytes + 40, 2); CHECK_EQ(w, 0x0AAA);
    std::memcpy(&g, node->bytes + 42, 4); CHECK_EQ(g, 200u);
    std::memcpy(&g, node->bytes + 50, 4); CHECK_EQ(g, 0xDEAD0005u);
    CHECK_EQ(node->bytes[54], 0x42);
    CHECK_EQ(node->bytes[55], 80);
}

TEST(SimCmdApply2, UpsertTradeEntry_UpdateAndClamp) {
    Seed2();
    MakeObject(0, 100);
    MakeObject(1, 200);

    CommandPacket p{};
    p.opcode() = 0x5E;
    p.put32(0x14, 100);
    p.put32(0x26, 200);
    p.bytes[0x33] = 70;
    CHECK_EQ(ApplyPacket2(p, nullptr), 0);

    // Second upsert to the SAME (owner, partner): finds the existing node, adds
    // 70 + 70 = 140 -> clamps to 100.
    CHECK_EQ(ApplyPacket2(p, nullptr), 0);
    TradeNode* node = Apply2_FindTradeNode(100, 200);
    CHECK(node != nullptr);
    CHECK_EQ(node->bytes[55], 100);
}

TEST(SimCmdApply2, UpsertTradeEntry_MissingEndpoint) {
    Seed2();
    MakeObject(0, 100); // owner only; partner 200 absent
    CommandPacket p{};
    p.opcode() = 0x5E;
    p.put32(0x14, 100);
    p.put32(0x26, 200);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket2(p, &ack), 1);
    CHECK_EQ(ack.status, 2); // in-progress (rejected)
}

// ---------------------------------------------------------------------------
// 0x44/0x45/0x46/0x5C — office/law delegators stamp ack from the leaf result.
// ---------------------------------------------------------------------------
TEST(SimCmdApply2, OfficeAssign_AckFromResult) {
    Seed2();
    static int calls; calls = 0;
    SetOfficeAssignHook([](const i32*) -> i32 { ++calls; return 0; }); // success
    CommandPacket p{}; p.opcode() = 0x44;
    AckEntry ack{};
    CHECK_EQ(ExAssignOffice(p, &ack), 1); // returns (ret==0)
    CHECK_EQ(calls, 1);
    CHECK_EQ(ack.status, 2);              // (ret==0)+1 == 2

    SetOfficeAssignHook([](const i32*) -> i32 { return 5; }); // failure
    AckEntry ack2{};
    CHECK_EQ(ExAssignOffice(p, &ack2), 0);
    CHECK_EQ(ack2.status, 1);             // (ret==0)+1 == 1
    SetOfficeAssignHook(nullptr);
}

TEST(SimCmdApply2, ReleaseOffice_AlwaysReturnsZero) {
    Seed2();
    static int lawCalls; lawCalls = 0;
    SetLawApplyHook([]() -> i32 { ++lawCalls; return 0; });
    CommandPacket p{}; p.opcode() = 0x46;
    AckEntry ack{};
    CHECK_EQ(ExReleaseOffice(p, &ack), 0);
    CHECK_EQ(lawCalls, 1);
    CHECK_EQ(ack.status, 2); // (0==0)+1
    SetLawApplyHook(nullptr);
}

TEST(SimCmdApply2, SwapOfficeHolders_StatusOnSuccess) {
    Seed2();
    SetOfficeSwapHook([](const i32*) -> i32 { return 0; });
    CommandPacket p{}; p.opcode() = 0x5C;
    AckEntry ack{};
    CHECK_EQ(ExSwapOfficeHolders(p, &ack), 1);
    // 0x49d1dd: `test eax,eax; jnz loc_49D1ED` -> `mov [edx],1` runs ONLY when
    // SwapHolders result != 0. result==0 keeps the entry stamp +0=2. Return (result==0).
    CHECK_EQ(ack.status, 2); // result==0 -> [edx]=1 NOT taken, stays at entry stamp 2

    SetOfficeSwapHook([](const i32*) -> i32 { return 3; });
    AckEntry ack2{};
    CHECK_EQ(ExSwapOfficeHolders(p, &ack2), 0);
    CHECK_EQ(ack2.status, 1); // result!=0 -> mov [edx],1
    SetOfficeSwapHook(nullptr);
}

// ---------------------------------------------------------------------------
// 0x0D ExBindObjectProto — estate transfer; ack tag from the returned index.
// ---------------------------------------------------------------------------
TEST(SimCmdApply2, BindObjectProto_AckTag) {
    Seed2();
    SetEstateTransferHook([](i32, const i32*) -> i32 { return 42; }); // idx 42
    CommandPacket p{}; p.opcode() = 0x0D; p.put32(0x10, 7);
    AckEntry ack{};
    CHECK_EQ(ExBindObjectProto(p, &ack), 0);
    CHECK_EQ(ack.status, 1);
    CHECK_EQ(ack.slot, 1);   // idx >= 0
    CHECK_EQ(ack.seq, 42);

    SetEstateTransferHook([](i32, const i32*) -> i32 { return -1; }); // failure
    AckEntry ack2{};
    CHECK_EQ(ExBindObjectProto(p, &ack2), 0);
    CHECK_EQ(ack2.status, 1);
    CHECK_EQ(ack2.slot, 0);  // idx < 0
    SetEstateTransferHook(nullptr);
}

// ---------------------------------------------------------------------------
// 0x0F ExRemapObjectPair — remap src/dst, move amount; observe leaf calls.
// ---------------------------------------------------------------------------
TEST(SimCmdApply2, RemapObjectPair_Move) {
    Seed2();
    g_lastObjectId = 5000; // -2 token resolves here

    static i32 removedFrom, removedAmt, addedTo, addedAmt; static u8 proto;
    removedFrom = addedTo = removedAmt = addedAmt = 0; proto = 0;
    SetRemoveObjektHook([](i32 c, i32 pr, i32 a) -> int { removedFrom = c; proto = (u8)pr; removedAmt = a; return 1; });
    SetAddObjektHook   ([](i32 c, i32, i32 a) -> int { addedTo = c; addedAmt = a; return 1; });

    CommandPacket p{};
    p.opcode() = 0x0F;
    p.put32(0x14, -2);    // src -> resolves to 5000
    p.put32(0x10, 6000);  // dst
    p.bytes[0x1C] = 17;   // proto key byte
    i32 amt = 12; std::memcpy(p.bytes + 0x1D, &amt, 4);

    AckEntry ack{};
    CHECK_EQ(ApplyPacket2(p, &ack), 0);
    CHECK_EQ(ack.status, 1);
    CHECK_EQ(removedFrom, 5000);
    CHECK_EQ(addedTo, 6000);
    CHECK_EQ(removedAmt, 12);
    CHECK_EQ(addedAmt, 12);
    CHECK_EQ(proto, 17);
    // The packet's src id was rewritten in place to the resolved value.
    CHECK_EQ(static_cast<i32>(p.get32(0x14)), 5000);

    SetRemoveObjektHook(nullptr);
    SetAddObjektHook(nullptr);
}

TEST(SimCmdApply2, RemapObjectPair_RemoveFailsShortCircuits) {
    Seed2();
    static int addCalls; addCalls = 0;
    SetRemoveObjektHook([](i32, i32, i32) -> int { return 0; }); // failure
    SetAddObjektHook   ([](i32, i32, i32) -> int { ++addCalls; return 1; });
    CommandPacket p{};
    p.opcode() = 0x0F;
    p.put32(0x14, 1); // src present
    p.put32(0x10, 2);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket2(p, &ack), 1); // remove failed
    CHECK_EQ(addCalls, 0);              // add not reached
    SetRemoveObjektHook(nullptr);
    SetAddObjektHook(nullptr);
}

// ---------------------------------------------------------------------------
// 0x10 ExUpdateObjectPair — decrement-then-add, no short-circuit.
// ---------------------------------------------------------------------------
TEST(SimCmdApply2, UpdateObjectPair_Both) {
    Seed2();
    static int decCalls, addCalls; decCalls = addCalls = 0;
    SetDecrementObjektHook([](i32, i32, i32) -> int { ++decCalls; return 1; });
    SetAddObjektHook      ([](i32, i32, i32) -> int { ++addCalls; return 1; });
    CommandPacket p{};
    p.opcode() = 0x10;
    p.put32(0x14, 11);
    p.put32(0x10, 22);
    CHECK_EQ(ApplyPacket2(p, nullptr), 0);
    CHECK_EQ(decCalls, 1);
    CHECK_EQ(addCalls, 1);
    SetDecrementObjektHook(nullptr);
    SetAddObjektHook(nullptr);
}

// ---------------------------------------------------------------------------
// 0x0E ExRemapAndValidateObject — object path -> Building_FreeAndUnlink.
// ---------------------------------------------------------------------------
TEST(SimCmdApply2, RemapValidate_ObjectFreed) {
    Seed2();
    MakeObject(2, 333);
    static int freed; freed = -1;
    SetBuildingFreeHook([](i32 id) -> int { freed = id; return 0; }); // 0 == ok
    CommandPacket p{};
    p.opcode() = 0x0E;
    p.put16(0x10, 333);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket2(p, &ack), 0);
    CHECK_EQ(ack.status, 1);
    CHECK_EQ(freed, 333);

    // Free failure -> return 1.
    SetBuildingFreeHook([](i32) -> int { return 1; });
    AckEntry ack2{};
    CHECK_EQ(ApplyPacket2(p, &ack2), 1);
    SetBuildingFreeHook(nullptr);
}

TEST(SimCmdApply2, RemapValidate_PersonRemoved) {
    Seed2();
    MakePerson(3, 444);
    static int removed; removed = -1;
    SetBuildingRemoveHook([](i32 id) { removed = id; });
    CommandPacket p{};
    p.opcode() = 0x0E;
    p.put16(0x10, 444);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket2(p, &ack), 0);
    CHECK_EQ(ack.status, 1);
    CHECK_EQ(removed, 444);
    SetBuildingRemoveHook(nullptr);
}

// ---------------------------------------------------------------------------
// 0x2B ExSelectObject — resolve + toggle; missing entity -> -1.
// ---------------------------------------------------------------------------
TEST(SimCmdApply2, SelectObject_Toggle) {
    Seed2();
    MakeObject(4, 555);
    static i32 toggledContainer, toggledSub; toggledContainer = toggledSub = 0;
    SetSelectionToggleHook([](i32 c, i32 s) -> int { toggledContainer = c; toggledSub = s; return 0; });
    CommandPacket p{};
    p.opcode() = 0x2B;
    p.put32(0x10, 555);
    p.put32(0x14, static_cast<u32>(-1)); // wantSub == -1 -> direct toggle
    p.put32(0x18, 9);                    // subId
    AckEntry ack{};
    CHECK_EQ(ApplyPacket2(p, &ack), 0);
    CHECK_EQ(ack.status, 1);
    CHECK_EQ(toggledContainer, 555);
    CHECK_EQ(toggledSub, 9);
    SetSelectionToggleHook(nullptr);
}

TEST(SimCmdApply2, SelectObject_NoEntity) {
    Seed2();
    CommandPacket p{}; p.opcode() = 0x2B; p.put32(0x10, 123456);
    AckEntry ack{}; ack.status = 7;
    CHECK_EQ(ApplyPacket2(p, &ack), -1);
    CHECK_EQ(ack.status, 2); // set to in-progress on entry, then no entity
}

// ---------------------------------------------------------------------------
// Unknown opcode (one this batch does NOT own) -> safe no-op.
// ---------------------------------------------------------------------------
TEST(SimCmdApply2, UnknownOpcodeIgnored) {
    Seed2();
    CommandPacket p{}; p.opcode() = 0x16; // batch-1's opcode, not ours
    AckEntry ack{}; ack.status = 4;
    CHECK_EQ(ApplyPacket2(p, &ack), -1);
    CHECK_EQ(ack.status, 4); // untouched
}

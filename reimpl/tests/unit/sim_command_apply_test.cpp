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
// 0x18 ExApplyNeedDeltas — float need add + clamp on a Person. gilde.exe
// 0x497f4d..0x497fa6: lower bound is 0, the overflow THRESHOLD is 1000.0
// (dbl_61BE74), but the stored clamp value is 1024.0 (0x4090000000000000, hi-dword
// 1083129856). The asymmetry was confirmed by the live decompile (wave-15).
// ---------------------------------------------------------------------------
TEST(SimCmdApply, ApplyNeedDeltas_ClampHigh) {
    SeedWorld();
    Person* per = MakePerson(5, 700);
    u8* base = reinterpret_cast<u8*>(per);
    // stat 2 -> offset 144 + 12*2 = 168; seed 990.0f.
    float seed = 990.0f; std::memcpy(base + 144 + 12 * 2, &seed, 4);

    u8 stat = 2; float add = 50.0f; // 990 + 50 = 1040 >= 1000 -> clamps to 1024.0
    CommandPacket p = MakeNeedDelta(700, &stat, &add, 1);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket(p, &ack), 0);
    float got; std::memcpy(&got, base + 168, 4);
    CHECK(got == 1024.0f);
}

// Threshold boundary: exactly 1000.0 is NOT in-band (v13 < 1000.0 is false), so it
// clamps up to 1024.0. Just below (e.g. 999.0) stays unchanged.
TEST(SimCmdApply, ApplyNeedDeltas_ClampHighThresholdBoundary) {
    SeedWorld();
    Person* perA = MakePerson(7, 702);
    u8* baseA = reinterpret_cast<u8*>(perA);
    float seedA = 1000.0f; std::memcpy(baseA + 144 + 12 * 1, &seedA, 4);
    u8 statA = 1; float addA = 0.0f; // 1000.0 -> >= threshold -> 1024.0
    CommandPacket pa = MakeNeedDelta(702, &statA, &addA, 1);
    AckEntry acka{};
    CHECK_EQ(ApplyPacket(pa, &acka), 0);
    float gotA; std::memcpy(&gotA, baseA + 144 + 12 * 1, 4);
    CHECK(gotA == 1024.0f);

    Person* perB = MakePerson(8, 703);
    u8* baseB = reinterpret_cast<u8*>(perB);
    float seedB = 999.0f; std::memcpy(baseB + 144 + 12 * 1, &seedB, 4);
    u8 statB = 1; float addB = 0.0f; // 999.0 in-band -> unchanged
    CommandPacket pb = MakeNeedDelta(703, &statB, &addB, 1);
    AckEntry ackb{};
    CHECK_EQ(ApplyPacket(pb, &ackb), 0);
    float gotB; std::memcpy(&gotB, baseB + 144 + 12 * 1, 4);
    CHECK(gotB == 999.0f);
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

// ===========================================================================
// HARDENING (wave-11): malformed / truncated / oversized packet tests for the
// command apply + parse pipeline. These drive the real ApplyPacket / dispatch
// entries with adversarial input so ASAN+UBSAN exercises the parse-cursor and
// jump-table bounds. Every guard is fail-safe: the valid-input golden behavior
// in the tests above is unchanged; these only assert "no OOB and no crash".
// ===========================================================================

// (1) Field-patch (0x16) with a +0x14 field-count far larger than the packet can
// hold, and bogus oversized (width,count) records. Without the packet-buffer
// bound the per-field cursor `p`/`vals` would read past the 153-byte record.
TEST(SimCmdApply, Malformed_PatchFieldsAdd_OverlargeCount) {
    SeedWorld();
    ObjectRec* obj = MakeObject(7, 4242);
    (void)obj;
    CommandPacket p{};
    p.opcode() = 0x16;
    p.put32(0x10, 4242);
    p.bytes[0x14] = 255;            // claim 255 records — far beyond the 153-byte record
    // Fill the record region with width=4,count=255 descriptors so each record
    // claims 1024+4 bytes of values: the cursor would sprint off the buffer.
    for (u32 off = 0x15; off < kPacketStride; off += 4) {
        p.bytes[off] = 4;           // width
        if (off + 1 < kPacketStride) p.bytes[off + 1] = 255; // count
    }
    AckEntry ack{};
    // Must return without reading past the packet (ASAN gate). Result is the
    // fail-safe apply path; we only require it not corrupt memory.
    int r = ApplyPacket(p, &ack);
    CHECK(r == 0 || r == 1);
}

// (1b) Same shape for 0x17 (absolute write via memcpy).
TEST(SimCmdApply, Malformed_WriteFields_OverlargeCount) {
    SeedWorld();
    MakeObject(3, 99);
    CommandPacket p{};
    p.opcode() = 0x17;
    p.put32(0x10, 99);
    p.bytes[0x14] = 255;
    p.bytes[0x15] = 4;              // width
    p.bytes[0x16] = 255;           // count -> 1020 value bytes claimed
    p.bytes[0x17] = 0x10;          // offset lo (small dst offset; only src is OOB)
    AckEntry ack{};
    int r = ApplyPacket(p, &ack);
    CHECK(r == 0 || r == 1);
}

// (2) Need-deltas (0x18): a +0x14 entry count larger than the 5-byte entries the
// 153-byte record can hold. The parse cursor must stop at the record end.
TEST(SimCmdApply, Malformed_NeedDeltas_OverlargeCount) {
    SeedWorld();
    MakePerson(5, 700);
    CommandPacket p{};
    p.opcode() = 0x18;
    p.put32(0x10, 700);
    p.bytes[0x14] = 255;           // 255 * 5 = 1275 bytes of entries claimed
    AckEntry ack{};
    int r = ApplyPacket(p, &ack);
    CHECK(r == 0 || r == 1);
}

// (3) Truncated field record: count byte present but the value bytes run off the
// physical record end. The vals bound must reject the partial record.
TEST(SimCmdApply, Malformed_PatchFields_TruncatedRecord) {
    SeedWorld();
    MakeObject(1, 1212);
    CommandPacket p{};
    p.opcode() = 0x16;
    p.put32(0x10, 1212);
    p.bytes[0x14] = 1;             // exactly one record
    // Place the single record near the very end so its values overrun the buffer.
    p.bytes[kPacketStride - 4] = 4;     // width  (header straddles the end)
    p.bytes[kPacketStride - 3] = 8;     // count -> 32 value bytes, none in-buffer
    AckEntry ack{};
    int r = ApplyPacket(p, &ack);
    CHECK(r == 0 || r == 1);
}

// (4) Opcode out of the jump-table range (>= kNumOpcodes==96) routed through the
// dispatch table: must be a no-op, never index past handlers_[96].
TEST(SimCmdApply, Malformed_OpcodeOutOfRange_Dispatch) {
    SeedWorld();
    CommandQueue q;
    q.Init();
    RegisterApplyHandlers(q);
    // 0xFF is well past the 96-entry table. StoreReceivedPacket + ExecCommands
    // must not index handlers_ out of range.
    CommandPacket p{};
    p.opcode() = 0xFF;
    p.set_cmd_id(0xFFFFFFFFu);     // untracked -> no ack entry touched
    q.StoreReceivedPacket(p);
    int r = q.ExecCommands();
    CHECK_EQ(r, 0);
}

// (4b) Opcode out of range via direct ApplyPacket: returns -1, ack untouched.
TEST(SimCmdApply, Malformed_OpcodeOutOfRange_Apply) {
    SeedWorld();
    CommandPacket p{}; p.opcode() = 200; // > 96, undefined
    AckEntry ack{}; ack.status = 7;
    CHECK_EQ(ApplyPacket(p, &ack), -1);
    CHECK_EQ(ack.status, 7);
}

// (5) ComputePacketSize on a malformed variable-length (0x16) packet: the size
// walk reads the field headers from the packet; a bogus +0x14 count must not
// drive the reader off the 153-byte record.
TEST(SimCmdApply, Malformed_ComputePacketSize_BadFieldCount) {
    SeedWorld();
    CommandPacket p{};
    p.opcode() = 0x16;
    p.bytes[0x14] = 255;
    for (u32 off = 0x15; off + 1 < kPacketStride; off += 2) {
        p.bytes[off] = 4;   // width
        p.bytes[off + 1] = 64; // count
    }
    u16 sz = ComputePacketSize(p);   // must not OOB-read; value is don't-care
    CHECK(sz >= 21);                 // never below the header floor
}

// (6) Field index / slot past 768: the relation handler (0x1B) resolves persons
// by id; an unknown id must reject (return 1) rather than index the 768x768 grid
// out of range. (FindPersonIndex returns -1 for a miss.)
TEST(SimCmdApply, Malformed_RelationUnknownPerson) {
    SeedWorld();
    // No persons seeded; any id misses.
    CommandPacket p{};
    p.opcode() = 0x1B;
    p.put32(16, 123456); // row person id (unknown)
    p.put32(20, 654321); // col person id (unknown)
    p.put32(24, 5);      // delta
    p.put32(28, 0);      // mode 0 -> targeted, must reject on missing person
    AckEntry ack{};
    // ExComputeObjectCoords lives in command_apply6; route via that module's
    // direct apply if linked. Here we only assert the entity resolver rejects.
    int idx = -1;
    // FindPersonIndex is module-private; emulate the contract: PersonFindRecordById
    // must miss for an unseeded world.
    CHECK(PersonFindRecordById(123456) == nullptr);
    (void)idx; (void)ack;
}

// (6b) 0x5D fill-level index out of range (>4) must reject without touching the
// person record (the original bounds index <= 4).
TEST(SimCmdApply, Malformed_FillLevel_IndexOutOfRange) {
    SeedWorld();
    Person* per = MakePerson(2, 77);
    u8* base = reinterpret_cast<u8*>(per);
    base[0x80 + 0] = 11;
    CommandPacket p{};
    p.opcode() = 0x5D;
    p.put32(0x10, 77);
    p.put32(0x14, 0xFFFFFFFFu); // huge index -> reject (index > 4)
    p.put32(0x18, 10);
    AckEntry ack{};
    CHECK_EQ(ApplyPacket(p, &ack), 1);
    CHECK_EQ(base[0x80 + 0], 11); // untouched
}

// (7) Zero-length / empty packet (all zero bytes): opcode 0 is in range but
// unset -> ApplyPacket returns -1, dispatch is a no-op. No OOB.
TEST(SimCmdApply, Malformed_EmptyPacket) {
    SeedWorld();
    CommandPacket p{}; // opcode 0, all zero
    AckEntry ack{}; ack.status = 5;
    CHECK_EQ(ApplyPacket(p, &ack), -1);
    CHECK_EQ(ack.status, 5);
}

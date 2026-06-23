// Hardening golden tests for src/sim/objectsearch.cpp — verified line-for-line
// against gilde.exe disasm/decompile at:
//   0x559d98 MatchEntityFilter
//   0x559ff8 MatchEntityFilterWithStatus
//   0x47b1d0 FindNearestEntity (probe geometry; favourability gate deferred)
//   0x47b308 FindEntitiesByCount (probe geometry; favourability gate deferred)
//   0x4784cc ObjectRing_AdvanceIterator
#include "sim/objectsearch.h"

#include <cstring>
#include <vector>

#include "tests/framework/test.h"

using namespace guild;
using namespace guild::sim;

namespace {
void put16(u8* p, int off, u16 v) { std::memcpy(p + off, &v, 2); }
void put32(u8* p, int off, i32 v) { std::memcpy(p + off, &v, 4); }
}  // namespace

// --- MatchEntityFilter (0x559d98) ------------------------------------------

// !a2 -> return 1 (always match), !a3 -> return 0.
TEST(ObjSearchHarden, MatchFilterNull) {
    u8 rec[169] = {};
    ObjectSearchContext ctx{};
    CHECK_EQ(ObjectSearchMatchEntityFilter(ctx, nullptr, rec), true);
    EntityFilter f{};
    f.ownerMode = 1;
    CHECK_EQ(ObjectSearchMatchEntityFilter(ctx, &f, nullptr), false);
}

// Empty filter (mask==0 && +12==0) -> return 1 at 0x559ea9.
TEST(ObjSearchHarden, MatchFilterEmpty) {
    u8 rec[169] = {};
    ObjectSearchContext ctx{};
    EntityFilter f{};
    f.factionMask = 0;
    f.ownerMode = 0;
    CHECK_EQ(ObjectSearchMatchEntityFilter(ctx, &f, rec), true);
}

// require-status sign (<0): rejects when +90 bit0 set OR +97 dword == 0.
//
// IMPORTANT OVERLAP (verified vs 0x559d98): the original reads the faction mask
// as *(_DWORD*)(a2+8) and the "require status" sign byte as *(char*)(a2+11) —
// byte 11 is the HIGH byte of the same dword. The reimpl mirrors this exactly:
// fb[11] (= requireStatus) is the top byte of the mask read by RdD(fb,8). So a
// negative requireStatus implies the mask's top byte is 0xFF (bits 24..31). To
// keep faction membership (v16) satisfied while the sign byte is negative, the
// record's faction bit must land in bits 24..31. We use faction bit 24.
TEST(ObjSearchHarden, MatchFilterRequireStatus) {
    u8 aiTable[589 * 4] = {};
    aiTable[589 * 0] = 24;  // record type 0 -> faction bit 24 (inside 0xFF000000)
    ObjectSearchContext ctx{};
    ctx.aiPlayerTable = aiTable;
    ctx.queryFaction = 1;

    u8 rec[169] = {};
    rec[kObjAlive] = 0;     // type 0
    put16(rec, kObjOwner, 0xFFFF);

    EntityFilter f{};
    f.ownerMode = 1;            // make filter non-empty
    f.requireStatus = -1;       // sign byte < 0 (mask top byte = 0xFF)

    // +97 == 0 -> rejected (the +97 test is unique to the non-status variant).
    rec[kObjFlags90] = 0;
    put32(rec, kObjStatus97, 0);
    CHECK_EQ(ObjectSearchMatchEntityFilter(ctx, &f, rec), false);

    // +97 nonzero, +90 bit0 clear -> not rejected; bit24 in mask -> v16=1;
    // mode1 owner==-1 -> v5 true.
    put32(rec, kObjStatus97, 5);
    CHECK_EQ(ObjectSearchMatchEntityFilter(ctx, &f, rec), true);

    // +90 bit0 set -> rejected regardless of +97.
    rec[kObjFlags90] = 1;
    CHECK_EQ(ObjectSearchMatchEntityFilter(ctx, &f, rec), false);
}

// Owner-mode switch arms 1..7 (0x559ed9 jump table).
TEST(ObjSearchHarden, MatchFilterOwnerModes) {
    u8 aiTable[589 * 4] = {};
    ObjectSearchContext ctx{};
    ctx.aiPlayerTable = aiTable;
    ctx.queryFaction = 7;
    ctx.extraFaction = 9;

    auto mk = [](u16 owner) {
        static u8 rec[169];
        std::memset(rec, 0, sizeof(rec));
        put32(rec, kObjStatus97, 1);
        put16(rec, kObjOwner, owner);
        return rec;
    };
    auto run = [&](u8 mode, u16 owner) {
        EntityFilter f{};
        f.ownerMode = mode;     // mask==0 -> v16=1
        return ObjectSearchMatchEntityFilter(ctx, &f, mk(owner));
    };

    CHECK_EQ(run(1, 0xFFFF), true);   CHECK_EQ(run(1, 7), false);
    CHECK_EQ(run(2, 0xFFFF), false);  CHECK_EQ(run(2, 7), true);
    CHECK_EQ(run(3, 7), true);  CHECK_EQ(run(3, 0xFFFF), true); CHECK_EQ(run(3, 4), false);
    CHECK_EQ(run(4, 7), true);  CHECK_EQ(run(4, 4), false);
    CHECK_EQ(run(5, 7), false); CHECK_EQ(run(5, 4), true);
    CHECK_EQ(run(6, 4), true);  CHECK_EQ(run(6, 7), false); CHECK_EQ(run(6, 0xFFFF), false);
    CHECK_EQ(run(7, 4), true);  CHECK_EQ(run(7, 7), false); CHECK_EQ(run(7, 9), false);
    CHECK_EQ(run(7, 0xFFFF), false);
}

// Faction membership: mask bit selected by aiTable[589*type].
TEST(ObjSearchHarden, MatchFilterFactionMask) {
    u8 aiTable[589 * 4] = {};
    aiTable[589 * 2] = 5;   // type 2 -> faction bit 5
    ObjectSearchContext ctx{};
    ctx.aiPlayerTable = aiTable;

    u8 rec[169] = {};
    rec[kObjAlive] = 2;     // type 2
    put32(rec, kObjStatus97, 1);
    put16(rec, kObjOwner, 0xFFFF);

    EntityFilter f{};
    f.ownerMode = 1;        // owner==-1 true
    f.factionMask = (1u << 5);   // bit 5 set -> v16=1
    CHECK_EQ(ObjectSearchMatchEntityFilter(ctx, &f, rec), true);

    f.factionMask = (1u << 4);   // bit 5 NOT set -> v16=0 -> overall false
    CHECK_EQ(ObjectSearchMatchEntityFilter(ctx, &f, rec), false);
}

// Owner-list XOR refinement (+13 bit0): a list hit forces filter-out via XOR==0.
TEST(ObjSearchHarden, MatchFilterOwnerListRefine) {
    u8 aiTable[589 * 4] = {};
    ObjectSearchContext ctx{};
    ctx.aiPlayerTable = aiTable;

    u8 rec[169] = {};
    put32(rec, kObjStatus97, 1);
    put16(rec, kObjOwner, 0x1234);

    EntityFilter f{};
    f.ownerMode = 2;        // owner != -1 -> true
    f.refineFlag = 1;       // enable refinement
    f.ownerList[0] = 0x1234;   // match -> v17 = owner ^ owner == 0 -> filtered out
    CHECK_EQ(ObjectSearchMatchEntityFilter(ctx, &f, rec), false);

    f.ownerList[0] = 0x5555;   // no match -> v17 stays 1
    CHECK_EQ(ObjectSearchMatchEntityFilter(ctx, &f, rec), true);
}

// --- MatchEntityFilterWithStatus (0x559ff8) --------------------------------

// status-bitmask reject: faction bit in 0x0F82806F AND +11 has 0x40 -> false.
TEST(ObjSearchHarden, WithStatusBitmaskReject) {
    u8 aiTable[589 * 4] = {};
    aiTable[0] = 3;   // bit3 (8) is set in 0x0F82806F
    ObjectSearchContext ctx{};
    ctx.aiPlayerTable = aiTable;
    ctx.queryFaction = 1;

    u8 rec[169] = {};
    put16(rec, kObjOwner, 0xFFFF);

    EntityFilter f{};
    f.ownerMode = 1;
    f.requireStatus = 0x40;
    CHECK_EQ(ObjectSearchMatchEntityFilterWithStatus(ctx, &f, rec), false);

    f.requireStatus = 0;
    CHECK_EQ(ObjectSearchMatchEntityFilterWithStatus(ctx, &f, rec), true);
}

// WithStatus require-status sign tests ONLY +90 bit0 (not +97).
//
// Same mask/sign overlap as MatchFilterRequireStatus: requireStatus is the high
// byte of the +8 dword mask. We use sign byte 0x80 (mask top byte 0x80 ->
// mask = 0x80000000) and faction bit 31 so membership (v15) is satisfied. Note
// bit 31 is NOT in 0x0F82806F, so the status-bitmask reject gate stays inert
// even though 0x80 lacks the 0x40 bit it would key on.
TEST(ObjSearchHarden, WithStatusRequireStatusNo97) {
    u8 aiTable[589 * 4] = {};
    aiTable[0] = 31;   // faction bit 31: in 0x80000000 mask, NOT in 0x0F82806F
    ObjectSearchContext ctx{};
    ctx.aiPlayerTable = aiTable;

    u8 rec[169] = {};
    rec[kObjAlive] = 0;            // type 0 -> faction bit 31
    put16(rec, kObjOwner, 0xFFFF);
    put32(rec, kObjStatus97, 0);   // zero +97 must NOT reject in the status variant
    rec[kObjFlags90] = 0;

    EntityFilter f{};
    f.ownerMode = 1;
    f.requireStatus = static_cast<i8>(0x80);  // sign<0, mask top byte 0x80
    CHECK_EQ(ObjectSearchMatchEntityFilterWithStatus(ctx, &f, rec), true);

    rec[kObjFlags90] = 1;   // +90 bit0 set -> rejected
    CHECK_EQ(ObjectSearchMatchEntityFilterWithStatus(ctx, &f, rec), false);
}

// --- FindNearestEntity / FindEntitiesByCount probe geometry ----------------

TEST(ObjSearchHarden, ProbeStrideTable) {
    const int expect[16] = {0x01, 0x03, 0x05, 0x07, 0x0B, 0x0D, 0x11, 0x13,
                            0xED, 0xEF, 0xF3, 0xF5, 0xF9, 0xFB, 0xFD, 0xFF};
    for (int i = 0; i < kObjectProbeStrideCount; ++i)
        CHECK_EQ(kObjectProbeStrides[i], expect[i]);
}

TEST(ObjSearchHarden, FindNearestFirstAlive) {
    std::vector<u8> arr(kSearchObjectStride * kSearchObjectCapacity, 0);
    u8 aiTable[589 * 4] = {};
    ObjectSearchContext ctx{};
    ctx.objectArray = arr.data();
    ctx.aiPlayerTable = aiTable;

    // Mark slot 5 alive with id 4242.
    u8* rec = arr.data() + kSearchObjectStride * 5;
    rec[kObjAlive] = 1;
    put32(rec, kObjId, 4242);

    // stride index 0 -> stride 1, start at 0 -> visits 0,1,2,...; slot 5 hit.
    i32 id = 0;
    bool found = ObjectSearchFindNearestEntity(ctx, /*filter*/ nullptr, 0, 0, &id);
    CHECK_EQ(found, true);
    CHECK_EQ(id, 4242);

    // No alive slots -> not found, outId -1.
    arr[kSearchObjectStride * 5 + kObjAlive] = 0;
    id = 0;
    found = ObjectSearchFindNearestEntity(ctx, nullptr, 0, 0, &id);
    CHECK_EQ(found, false);
    CHECK_EQ(id, -1);
}

TEST(ObjSearchHarden, FindByCountCollects) {
    std::vector<u8> arr(kSearchObjectStride * kSearchObjectCapacity, 0);
    u8 aiTable[589 * 4] = {};
    ObjectSearchContext ctx{};
    ctx.objectArray = arr.data();
    ctx.aiPlayerTable = aiTable;

    for (int s : {3, 7, 9}) {
        u8* rec = arr.data() + kSearchObjectStride * s;
        rec[kObjAlive] = 1;
        put32(rec, kObjId, 100 + s);
    }

    i32 out[8] = {};
    int n = ObjectSearchFindEntitiesByCount(ctx, nullptr, 0, 0, 8, out);
    CHECK_EQ(n, 3);
    // stride 1 from start 0 visits in order -> 103, 107, 109.
    CHECK_EQ(out[0], 103);
    CHECK_EQ(out[1], 107);
    CHECK_EQ(out[2], 109);

    // maxCount cap respected.
    n = ObjectSearchFindEntitiesByCount(ctx, nullptr, 0, 0, 2, out);
    CHECK_EQ(n, 2);
}

// --- ObjectRing_AdvanceIterator (0x4784cc) ---------------------------------

// count==0 path (0x4784e1): returns base (slot 0) and does NOT store the cursor.
TEST(ObjSearchHarden, RingCountZeroNoStore) {
    std::vector<u8> base(64, 0);
    int cursor = 36;            // arbitrary preexisting cursor
    ObjectRing ring{base.data(), &cursor, 1, 0};
    int r = ObjectRingAdvance(ring);
    CHECK_EQ(r, 0);             // returns base offset 0
    CHECK_EQ(cursor, 36);      // cursor LEFT UNTOUCHED (matches disasm: no store)
}

// Normal rotation by bias, wrapping modulo count.
TEST(ObjSearchHarden, RingRotateWrap) {
    std::vector<u8> base(64, 0);
    int cursor = 0;
    ObjectRing ring{base.data(), &cursor, 1, 4};
    CHECK_EQ(ObjectRingAdvance(ring), 0);   // returns old, cursor -> slot1
    CHECK_EQ(cursor, 12);
    CHECK_EQ(ObjectRingAdvance(ring), 12);  // cursor -> slot2
    CHECK_EQ(cursor, 24);
    CHECK_EQ(ObjectRingAdvance(ring), 24);  // cursor -> slot3
    CHECK_EQ(cursor, 36);
    CHECK_EQ(ObjectRingAdvance(ring), 36);  // (bias+3)%4=0 -> slot0 wrap
    CHECK_EQ(cursor, 0);
}

// Golden-vector tests for VIBE_Save_LoadCharacters @0x5a986c reconstruction.
#include "tests/framework/test.h"
#include "io/save_recon3_characters.h"

#include <cstring>
#include <vector>

using namespace guild::io;

namespace {

// little-endian append helpers ------------------------------------------------
static void putU32(std::vector<guild::u8>& b, guild::u32 v) {
    b.push_back((guild::u8)v);
    b.push_back((guild::u8)(v >> 8));
    b.push_back((guild::u8)(v >> 16));
    b.push_back((guild::u8)(v >> 24));
}
static void putI32(std::vector<guild::u8>& b, guild::i32 v) { putU32(b, (guild::u32)v); }

// One action record in on-disk order: lead(1) +12(4) +20(4) +48(32) +80(32)
// +240(160) +400(1) +36(4) +40(4) == 1+4+4+32+32+160+1+4+4 = 242 bytes.
struct DiskRec {
    guild::u8  lead = 0;
    guild::u32 f12 = 0;
    guild::u32 f20 = 0;   // handle index
    guild::u8  f48[32] = {};
    guild::u8  f80[32] = {};
    guild::u8  f240[160] = {};
    guild::u8  f400 = 0;
    guild::u32 f36 = 0;   // next-action index (or -1)
    guild::u32 f40 = 0;   // prev-action index (or -1)
};
static void putRec(std::vector<guild::u8>& b, const DiskRec& r) {
    b.push_back(r.lead);
    putU32(b, r.f12);
    putU32(b, r.f20);
    b.insert(b.end(), r.f48, r.f48 + 32);
    b.insert(b.end(), r.f80, r.f80 + 32);
    b.insert(b.end(), r.f240, r.f240 + 160);
    b.push_back(r.f400);
    putU32(b, r.f36);
    putU32(b, r.f40);
}
constexpr guild::u32 kRecBytes = 242;

static guild::u32 rdU32(const guild::u8* p) {
    return (guild::u32)p[0] | ((guild::u32)p[1] << 8) | ((guild::u32)p[2] << 16) |
           ((guild::u32)p[3] << 24);
}

// Build a buffer: countA, countB, then `n` action records (rest implicitly zero
// from the writer's dead-slot zero-fill, so only `n` need to be on disk when n ==
// slabCount; the loader reads exactly slabCount records).
static std::vector<guild::u8> buildBuf(guild::i32 countA, guild::i32 countB,
                                       const std::vector<DiskRec>& recs) {
    std::vector<guild::u8> b;
    putI32(b, countA);
    putI32(b, countB);
    for (auto& r : recs) putRec(b, r);
    return b;
}

} // namespace

// ---------------------------------------------------------------------------
TEST(SaveRecon3, ReadLoopFieldOffsetsExact) {
    // One live record at slab[0]; verify each on-disk field lands at its offset.
    DiskRec r;
    r.lead = 0x33;
    r.f12  = 0x11223344;
    r.f20  = 7;                 // handle index (kept by identity handleResolve=null)
    for (int i = 0; i < 32; ++i) r.f48[i] = (guild::u8)(0x40 + i);
    for (int i = 0; i < 32; ++i) r.f80[i] = (guild::u8)(0x80 + i);
    for (int i = 0; i < 160; ++i) r.f240[i] = (guild::u8)(i & 0xFF);
    r.f400 = 0xAB;
    r.f36  = 0xFFFFFFFF;        // -1 -> 0
    r.f40  = 0xFFFFFFFF;        // -1 -> 0

    // remaining 767 records (768-record low version) are zero on disk.
    std::vector<DiskRec> recs(kCl3_CountLow);
    recs[0] = r;

    auto buf = buildBuf(0, 0, recs);
    auto src = CharLoadSourceOpen(buf.data(), (guild::u32)buf.size());

    std::vector<guild::u8> slab(kCl3_CharSpan, 0);
    guild::u8 scratch[kCl3_SlotSize];
    CharLoadHooks hooks;        // all inert
    CharLoadResult out;

    CHECK_EQ(LoadCharacters(&src, 0x1003B, slab.data(), scratch, hooks, nullptr, &out), 1);
    CHECK(out.fullyParsed);
    CHECK_EQ(out.slabCount, (guild::u32)kCl3_CountLow);  // version < 0x10044 -> 768

    const guild::u8* rec = slab.data();
    CHECK_EQ((int)rec[9], 0x33);
    CHECK_EQ(rdU32(rec + 12), 0u);            // relink sets +12 = 0 for live record
    CHECK_EQ((int)rec[48], 0x40);
    CHECK_EQ((int)rec[48 + 31], (guild::u8)(0x40 + 31));
    CHECK_EQ((int)rec[80], 0x80);
    CHECK_EQ((int)rec[240], 0);
    CHECK_EQ((int)rec[240 + 159], (guild::u8)159);
    CHECK_EQ((int)rec[400], 0xAB);
    // +36/+40 were -1 -> mapped to 0 by relink
    CHECK_EQ(rdU32(rec + 36), 0u);
    CHECK_EQ(rdU32(rec + 40), 0u);
    // +20 handle index kept by identity resolve (non-zero so record survives)
    CHECK_EQ(rdU32(rec + 20), 7u);
}

// ---------------------------------------------------------------------------
TEST(SaveRecon3, VersionGateSlabCount) {
    std::vector<DiskRec> recsHigh(kCl3_CountHigh);  // 1280
    auto bufHigh = buildBuf(0, 0, recsHigh);
    auto srcHigh = CharLoadSourceOpen(bufHigh.data(), (guild::u32)bufHigh.size());
    std::vector<guild::u8> slabHigh(kCl3_CharSpan, 0);
    guild::u8 scratch[kCl3_SlotSize];
    CharLoadHooks hooks;
    CharLoadResult out;
    CHECK_EQ(LoadCharacters(&srcHigh, 0x10044, slabHigh.data(), scratch, hooks, nullptr, &out), 1);
    CHECK_EQ(out.slabCount, (guild::u32)kCl3_CountHigh);
    CHECK_EQ(srcHigh.pos, 8u + kCl3_CountHigh * kRecBytes);

    std::vector<DiskRec> recsLow(kCl3_CountLow);    // 768
    auto bufLow = buildBuf(0, 0, recsLow);
    auto srcLow = CharLoadSourceOpen(bufLow.data(), (guild::u32)bufLow.size());
    std::vector<guild::u8> slabLow(kCl3_CharSpan, 0);
    CHECK_EQ(LoadCharacters(&srcLow, 0x10043, slabLow.data(), scratch, hooks, nullptr, &out), 1);
    CHECK_EQ(out.slabCount, (guild::u32)kCl3_CountLow);
    CHECK_EQ(srcLow.pos, 8u + kCl3_CountLow * kRecBytes);
}

// ---------------------------------------------------------------------------
TEST(SaveRecon3, NextPrevIndexToOffsetRelink) {
    DiskRec r;
    r.lead = 5;
    r.f20  = 3;             // non-zero handle -> record survives
    r.f36  = 2;             // next idx 2 -> 404*2 = 808
    r.f40  = 5;             // prev idx 5 -> 404*5 = 2020
    std::vector<DiskRec> recs(kCl3_CountLow);
    recs[0] = r;
    auto buf = buildBuf(0, 0, recs);
    auto src = CharLoadSourceOpen(buf.data(), (guild::u32)buf.size());
    std::vector<guild::u8> slab(kCl3_CharSpan, 0);
    guild::u8 scratch[kCl3_SlotSize];
    CharLoadHooks hooks;
    CharLoadResult out;
    CHECK_EQ(LoadCharacters(&src, 0x1003B, slab.data(), scratch, hooks, nullptr, &out), 1);
    CHECK_EQ(rdU32(slab.data() + 36), 2u * kCl3_CharStride);   // 808
    CHECK_EQ(rdU32(slab.data() + 40), 5u * kCl3_CharStride);   // 2020
}

// ---------------------------------------------------------------------------
TEST(SaveRecon3, HandleResolveZeroClearsRecord) {
    // handleResolve returning 0 must zero-fill the whole record.
    DiskRec r;
    r.lead = 9;
    r.f20  = 42;
    r.f400 = 0x7F;
    std::vector<DiskRec> recs(kCl3_CountLow);
    recs[0] = r;
    auto buf = buildBuf(0, 0, recs);
    auto src = CharLoadSourceOpen(buf.data(), (guild::u32)buf.size());
    std::vector<guild::u8> slab(kCl3_CharSpan, 0);
    guild::u8 scratch[kCl3_SlotSize];
    CharLoadHooks hooks;
    hooks.handleResolve = [](guild::u32, void*) -> guild::u32 { return 0; };
    CharLoadResult out;
    CHECK_EQ(LoadCharacters(&src, 0x1003B, slab.data(), scratch, hooks, nullptr, &out), 1);
    // entire record zeroed
    for (guild::u32 i = 0; i < kCl3_CharStride; ++i)
        CHECK_EQ((int)slab[i], 0);
}

// ---------------------------------------------------------------------------
TEST(SaveRecon3, DeadRecordSkippedInRelink) {
    // lead byte 0 -> record skipped by relink: its +36 stays as read (idx, not offset).
    DiskRec r;
    r.lead = 0;            // dead
    r.f20  = 99;
    r.f36  = 2;
    std::vector<DiskRec> recs(kCl3_CountLow);
    recs[0] = r;
    auto buf = buildBuf(0, 0, recs);
    auto src = CharLoadSourceOpen(buf.data(), (guild::u32)buf.size());
    std::vector<guild::u8> slab(kCl3_CharSpan, 0);
    guild::u8 scratch[kCl3_SlotSize];
    CharLoadHooks hooks;
    CharLoadResult out;
    CHECK_EQ(LoadCharacters(&src, 0x1003B, slab.data(), scratch, hooks, nullptr, &out), 1);
    // lead 0 -> not relinked; +36 left as the raw read value (2), not 808.
    CHECK_EQ((int)slab[9], 0);
    CHECK_EQ(rdU32(slab.data() + 36), 2u);
    CHECK_EQ(rdU32(slab.data() + 20), 99u);
}

// ---------------------------------------------------------------------------
TEST(SaveRecon3, ActionVtableAndType) {
    // rec[+0] = actionVtable( i32(rec+6) >> 24 ). Put a type in the high byte of the
    // dword at +6 (spans bytes 6..9). lead is byte +9 == top byte of that dword.
    // To get (i32 @ +6) >> 24 == typeIndex, the byte at +9 must be the type and must
    // also be non-zero (lead) for the record to be live.
    DiskRec r;
    r.lead = 0x12;         // byte +9 -> high byte of dword@+6 -> type 0x12
    r.f20  = 1;            // non-zero handle
    std::vector<DiskRec> recs(kCl3_CountLow);
    recs[0] = r;
    auto buf = buildBuf(0, 0, recs);
    auto src = CharLoadSourceOpen(buf.data(), (guild::u32)buf.size());
    std::vector<guild::u8> slab(kCl3_CharSpan, 0);
    guild::u8 scratch[kCl3_SlotSize];

    static int gSeenType = -999;
    CharLoadHooks hooks;
    hooks.actionVtable = [](int t, void*) -> guild::u32 { gSeenType = t; return 0xCAFEBABE; };
    CharLoadResult out;
    CHECK_EQ(LoadCharacters(&src, 0x1003B, slab.data(), scratch, hooks, nullptr, &out), 1);
    CHECK_EQ(gSeenType, 0x12);
    CHECK_EQ(rdU32(slab.data()), 0xCAFEBABEu);
}

// ---------------------------------------------------------------------------
TEST(SaveRecon3, ShortReadAborts) {
    // Truncate after the counts -> first record's lead read fails -> return 0.
    std::vector<guild::u8> b;
    putI32(b, 0);
    putI32(b, 0);   // no records at all, but version wants 768
    auto src = CharLoadSourceOpen(b.data(), (guild::u32)b.size());
    std::vector<guild::u8> slab(kCl3_CharSpan, 0);
    guild::u8 scratch[kCl3_SlotSize];
    CharLoadHooks hooks;
    CharLoadResult out;
    CHECK_EQ(LoadCharacters(&src, 0x1003B, slab.data(), scratch, hooks, nullptr, &out), 0);
    CHECK(!out.fullyParsed);
}

// ---------------------------------------------------------------------------
TEST(SaveRecon3, ShortReadOnFirstCountAborts) {
    std::vector<guild::u8> b = {0x01, 0x02};  // < 4 bytes
    auto src = CharLoadSourceOpen(b.data(), (guild::u32)b.size());
    std::vector<guild::u8> slab(kCl3_CharSpan, 0);
    guild::u8 scratch[kCl3_SlotSize];
    CharLoadHooks hooks;
    CharLoadResult out;
    CHECK_EQ(LoadCharacters(&src, 0x1003B, slab.data(), scratch, hooks, nullptr, &out), 0);
}

// ---------------------------------------------------------------------------
TEST(SaveRecon3, SlotCountRelinkHooksFire) {
    // countA=2, countB=1; verify the per-slot reader is invoked the right number of
    // times and the person/object relinks run. Use a loadSlot that injects an id.
    static guild::u32 gIds[8];
    static int gN = 0;
    gN = 0;

    std::vector<guild::u8> b;
    putI32(b, 2);  // countA
    putI32(b, 1);  // countB
    // then 768 zero records:
    std::vector<DiskRec> recs(kCl3_CountLow);
    for (auto& r : recs) putRec(b, r);
    auto src = CharLoadSourceOpen(b.data(), (guild::u32)b.size());

    std::vector<guild::u8> slab(kCl3_CharSpan, 0);
    guild::u8 scratch[kCl3_SlotSize];

    static int gPerson = 0, gObject = 0;
    gPerson = 0; gObject = 0;
    CharLoadHooks hooks;
    hooks.loadSlot = [](CharLoadSource*, guild::u8* slot, guild::u32, void*) -> bool {
        std::memset(slot, 0, kCl3_SlotSize);
        // write id 0x55 at +300
        slot[kCl3_SlotLinkOff] = 0x55;
        return true;
    };
    hooks.findPersonById = [](guild::u32 id, void*) -> guild::u32 { gPerson++; return id + 1; };
    hooks.findObjectById = [](guild::u32 id, void*) -> guild::u32 { gObject++; return id + 2; };
    CharLoadResult out;
    CHECK_EQ(LoadCharacters(&src, 0x1003B, slab.data(), scratch, hooks, nullptr, &out), 1);
    CHECK_EQ(out.slotCountA, 2);
    CHECK_EQ(out.slotCountB, 1);
    CHECK_EQ(gPerson, 2);   // countA person relinks
    CHECK_EQ(gObject, 1);   // countB object relinks
    (void)gIds;
}

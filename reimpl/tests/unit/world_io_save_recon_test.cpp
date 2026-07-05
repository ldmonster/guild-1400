// Golden-vector tests for the WorldIo write-side serializers:
//   VIBE_WorldIo_WriteObject         @0x5e5ab4
//   VIBE_WorldIo_WriteBuildingData   @0x5e5f74
//   VIBE_WorldIo_WriteObjectCallback @0x5e61ec
// (guild::world::WorldIoWrite*S).
#include "tests/framework/test.h"
#include "world/world_io_save_recon.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::world;

namespace {

// Append helpers for building golden byte vectors.
void PutU8(std::vector<u8>& v, u8 b) { v.push_back(b); }
void PutU32(std::vector<u8>& v, u32 x) {
    v.push_back(static_cast<u8>(x));
    v.push_back(static_cast<u8>(x >> 8));
    v.push_back(static_cast<u8>(x >> 16));
    v.push_back(static_cast<u8>(x >> 24));
}
void PutStr(std::vector<u8>& v, const char* s) {
    for (; *s; ++s) v.push_back(static_cast<u8>(*s));
    v.push_back(0);
}
void SetU32(u8* rec, int off, u32 x) { std::memcpy(rec + off, &x, 4); }

bool Eq(const std::vector<u8>& a, const u8* b, u32 n) {
    if (a.size() != n) return false;
    return std::memcmp(a.data(), b, n) == 0;
}

} // namespace

// --- WriteObject, switch case 0 -------------------------------------------
TEST(SaveReconWorldIo, WriteObjectCase0Golden) {
    WorldIoSetEventNamesHook(nullptr);                  // default no-events terminator
    std::vector<u8> rec(800, 0);
    std::memcpy(rec.data(), "obj", 4);                   // name at +0 (subtype!=1 -> name=node)
    rec[529] = 0x02;                                     // flags: bit1 set, bit0 clear
    rec[533] = 0x00;                                     // subtype 0 -> switchVar 0 -> case 0
    SetU32(rec.data(), 512, 0x11223344);                 // +0x200 dwordpair
    SetU32(rec.data(), 532, 0x7F000000);                 // +0x214 ; >>24 == 0x7F
    SetU32(rec.data(), 104, 0xAABBCCDD);                 // +0x68 case-0 dword
    SetU32(rec.data(), 76, 1); SetU32(rec.data(), 80, 2); SetU32(rec.data(), 84, 3);   // vec3 @76
    SetU32(rec.data(), 92, 4); SetU32(rec.data(), 96, 5); SetU32(rec.data(), 100, 6);  // vec3 @92
    SetU32(rec.data(), 144, 7); SetU32(rec.data(), 148, 8); SetU32(rec.data(), 152, 9);// vec3 @144
    // link slots (child +508 / sibling +496 / event +468) default to 0 (null id)
    WorldIoSetLinkResolver(nullptr);

    std::vector<u8> exp;
    PutU8(exp, 1);                                       // bit1 of flags(0x02)
    PutStr(exp, "obj");                                  // name
    PutU32(exp, 0x11223344);                             // node[512]
    PutU32(exp, 0x0000007F);                             // node[532]>>24 (arith)
    PutU32(exp, 0);                                      // sext(switchVar 0)
    PutU8(exp, 0);                                       // subtype==1 ? -> 0
    PutU8(exp, 0);                                       // node[529]&1
    PutU32(exp, 0xAABBCCDD);                             // node[104]
    PutU32(exp, 1); PutU32(exp, 2); PutU32(exp, 3);      // vec3 @76
    PutU32(exp, 4); PutU32(exp, 5); PutU32(exp, 6);      // vec3 @92
    PutU32(exp, 7); PutU32(exp, 8); PutU32(exp, 9);      // vec3 @144
    PutU8(exp, 0);                                       // child present = 0
    PutU8(exp, 0);                                       // sibling present = 0
    PutU32(exp, 0);                                      // event-names terminator: the
                                                         // binary's WriteEventNames
                                                         // (0x5f4b60) emits a 4-byte 0
                                                         // count dword for a null list
                                                         // (old pin: single 0 byte)

    u8 out[256];
    WorldIoSink sink = WorldIoSinkOpen(out, sizeof(out));
    CHECK(WorldIoWriteObjectS(&sink, rec.data()));
    CHECK(Eq(exp, out, sink.pos));
}

// --- WriteObject, flags bit1 clear (only the leading byte + links) ---------
TEST(SaveReconWorldIo, WriteObjectNoBodyWhenBit1Clear) {
    WorldIoSetEventNamesHook(nullptr);
    std::vector<u8> rec(800, 0);
    rec[529] = 0x00;                                     // bit1 clear -> skip body
    WorldIoSetLinkResolver(nullptr);                     // links null

    std::vector<u8> exp;
    PutU8(exp, 0);                                       // bit1 of 0x00
    PutU8(exp, 0);                                       // child = 0
    PutU8(exp, 0);                                       // sibling = 0
    PutU32(exp, 0);                                      // event terminator (dword, 0x5f4b60)

    u8 out[64];
    WorldIoSink sink = WorldIoSinkOpen(out, sizeof(out));
    CHECK(WorldIoWriteObjectS(&sink, rec.data()));
    CHECK(Eq(exp, out, sink.pos));
}

// --- WriteObjectCallback: leading 1 byte, sibling temporarily severed ------
static std::vector<u8> g_cbSibling;
static u8* CbResolver(u32 id) { return id == 7u ? g_cbSibling.data() : nullptr; }

TEST(SaveReconWorldIo, WriteObjectCallbackSeversSibling) {
    WorldIoSetEventNamesHook(nullptr);
    std::vector<u8> rec(800, 0);
    rec[529] = 0x00;                                     // no body
    // sibling slot holds link id 7, which resolves to a record that WOULD qualify
    // as a sibling; the callback must temporarily NULL +496 so it is NOT emitted.
    g_cbSibling.assign(800, 0);
    g_cbSibling[528] = 0; g_cbSibling[529] = 0x02;       // would qualify
    SetU32(rec.data(), 496, 7u);                         // sibling link id 7
    WorldIoSetLinkResolver(CbResolver);

    std::vector<u8> exp;
    PutU8(exp, 1);                                       // callback leading 1
    PutU8(exp, 0);                                       // WriteObject: bit1 of 0x00
    PutU8(exp, 0);                                       // child = 0
    PutU8(exp, 0);                                       // sibling = 0 (severed!)
    PutU32(exp, 0);                                      // event terminator (dword, 0x5f4b60)

    u8 out[64];
    WorldIoSink sink = WorldIoSinkOpen(out, sizeof(out));
    CHECK(WorldIoWriteObjectCallbackS(&sink, rec.data()));
    CHECK(Eq(exp, out, sink.pos));
    // sibling link id restored after the call
    u32 restored; std::memcpy(&restored, rec.data() + 496, 4);
    CHECK_EQ(restored, 7u);
    WorldIoSetLinkResolver(nullptr);
}

// --- WriteBuildingData: empty building name path --------------------------
TEST(SaveReconWorldIo, WriteBuildingDataEmptyNamePath) {
    std::vector<u8> bld(8000, 0);
    bld[6628] = 0;                                       // +0x19E4 name empty
    bld[6692] = 0;                                       // +0x1A24 name empty
    // 8 fixed strings at +6756.. are all empty (zeroed)
    SetU32(bld.data(), 160, 0x01020304);
    SetU32(bld.data(), 196, 0x05060708);
    SetU32(bld.data(), 144, 0x0A0B0C0D);                 // vec3 @144 [0]
    SetU32(bld.data(), 148, 0x10111213);                 // [1]
    SetU32(bld.data(), 152, 0x14151617);                 // [2]

    std::vector<u8> exp;
    PutStr(exp, "");                                     // name +6628 (empty)
    PutU8(exp, 0);                                       // else branch: WriteByte(0)
    PutStr(exp, "");                                     // name +6692 (empty)
    for (int i = 0; i < 8; ++i) PutStr(exp, "");         // 8 fixed strings
    PutU32(exp, 0x01020304);                             // +160
    PutU32(exp, 0x05060708);                             // +196
    PutU32(exp, 0x0A0B0C0D); PutU32(exp, 0x10111213); PutU32(exp, 0x14151617); // vec3 @144

    u8 out[256];
    WorldIoSink sink = WorldIoSinkOpen(out, sizeof(out));
    CHECK(WorldIoWriteBuildingDataS(&sink, bld.data()));
    CHECK(Eq(exp, out, sink.pos));
}

// --- HARDENING (wave-12): malformed array counts must not overflow the cursor --
// A malformed building blob can carry a huge inventory count n at [edi]. The
// payload size is count*stride == n*n (and 4*n*n for the second array); in u32
// that product can wrap to a small (or zero) value, slipping a truncated/garbage
// memcpy past the sink buffer. The sink now computes the payload in 64-bit and
// fails cleanly instead of wrapping. (The original streams to a file, bounded by
// the file boundary; the byte-cursor reconstruction must bound it here.)
TEST(SaveReconWorldIo, WriteBuildingDataArrayCountOverflowFailsClean) {
    WorldIoSetLinkResolver(nullptr);
    WorldIoSetEventNamesHook(nullptr);

    std::vector<u8> bld(8000, 0);
    std::memcpy(bld.data() + 6628, "x", 2);   // name non-empty -> array path
    SetU32(bld.data(), 0, 0x10000u);          // n: n*n == 0x1_0000_0000 -> wraps to 0 in u32

    u8 out[256];
    WorldIoSink sink = WorldIoSinkOpen(out, sizeof(out));
    // Must NOT crash and must NOT silently emit a wrapped 0-byte payload as success.
    bool ok = WorldIoWriteBuildingDataS(&sink, bld.data());
    CHECK(!ok);   // sink failed on the overflowing array, no OOB write
}

TEST(SaveReconWorldIo, WriteBuildingDataArrayTooLargeFailsClean) {
    static std::vector<u8> small;
    small.assign(16, 0xAB);
    WorldIoSetLinkResolver(+[](u32 id) -> u8* { return id == 9u ? small.data() : nullptr; });
    WorldIoSetEventNamesHook(nullptr);

    std::vector<u8> bld(8000, 0);
    std::memcpy(bld.data() + 6628, "x", 2);
    SetU32(bld.data(), 0, 50000u);            // n*n == 2.5e9, larger than the buffer
    SetU32(bld.data(), 16, 9u);               // bld+16 -> the small resolved buffer

    u8 out[64];
    WorldIoSink sink = WorldIoSinkOpen(out, sizeof(out));
    bool ok = WorldIoWriteBuildingDataS(&sink, bld.data());
    CHECK(!ok);   // payload exceeds capacity -> clean failure, no over-read of `small`
    WorldIoSetLinkResolver(nullptr);
}

// --- WriteBuildingData: named building, zero rooms, no inventory arrays -----
TEST(SaveReconWorldIo, WriteBuildingDataNamedNoRooms) {
    std::vector<u8> bld(8000, 0);
    std::memcpy(bld.data() + 6628, "haus", 5);           // +0x19E4 non-empty
    SetU32(bld.data(), 0, 0);                             // n = 0 (empty arrays)
    SetU32(bld.data(), 24, 0);                            // +0x18 == 0 -> no room block
    SetU32(bld.data(), 160, 0x21222324);
    SetU32(bld.data(), 196, 0x31323334);
    SetU32(bld.data(), 144, 0x41424344);
    SetU32(bld.data(), 148, 0x51525354);
    SetU32(bld.data(), 152, 0x61626364);

    std::vector<u8> exp;
    PutStr(exp, "haus");                                 // +6628
    PutU32(exp, 0);                                      // WriteDwordPair(n=0)
    PutU32(exp, 0); PutU32(exp, 0);                      // WriteArray(count=0,stride=0)
    PutU32(exp, 0); PutU32(exp, 0);                      // WriteArray(count=0,stride=0)
    PutU8(exp, 0);                                       // WriteByte(bld[+0x18]!=0 -> 0)
    PutStr(exp, "");                                     // +6692 empty
    for (int i = 0; i < 8; ++i) PutStr(exp, "");         // 8 fixed strings
    PutU32(exp, 0x21222324);                             // +160
    PutU32(exp, 0x31323334);                             // +196
    PutU32(exp, 0x41424344); PutU32(exp, 0x51525354); PutU32(exp, 0x61626364);

    u8 out[256];
    WorldIoSink sink = WorldIoSinkOpen(out, sizeof(out));
    CHECK(WorldIoWriteBuildingDataS(&sink, bld.data()));
    CHECK(Eq(exp, out, sink.pos));
}

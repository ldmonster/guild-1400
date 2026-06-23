// Golden-vector / round-trip tests for VIBE_Save_WriteCityAndPersonTables @0x5a57f4
// (guild::io::SaveWriteCityAndPersonTables).
#include "tests/framework/test.h"
#include "io/save_world_tables_save_recon.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::io;

namespace {

struct Capture {
    std::vector<std::pair<const u8*, i32>> objCalls; // (recPtr, linkId)
};

bool RecordObjHook(SaveTableSink* s, const u8* rec, i32 link, void* user) {
    auto* c = static_cast<Capture*>(user);
    c->objCalls.emplace_back(rec, link);
    // emit a 1-byte marker so the stream advances deterministically
    u8 m = 0x5A;
    if (s->pos + 1 > s->cap) { s->ok = false; return false; }
    s->buf[s->pos++] = m;
    return true;
}

u32 RdU32(const u8* p) { u32 v; std::memcpy(&v, p, 4); return v; }

// Build a fully-zeroed environment with backing storage.
struct Env {
    std::vector<u8> person, personLink, cityIdx, cityRec, chr;
    std::vector<const u8*> handles;
    SaveTableEnv mk() {
        person.assign(static_cast<size_t>(kSwt_PersonSpan), 0);
        personLink.assign(static_cast<size_t>(kSwt_PersonSpan), 0);
        cityIdx.assign(static_cast<size_t>(kSwt_CityIndexSpan), 0);
        // record array must be large enough for any i16 index * 65; index 0 by default
        cityRec.assign(static_cast<size_t>(0x10000) * kSwt_CityRecordStride, 0);
        chr.assign(static_cast<size_t>(kSwt_CharSpan), 0);
        handles.assign(4096, nullptr);
        SaveTableEnv e{};
        e.personBase = person.data();
        e.personLink = personLink.data();
        e.cityIndexBase = cityIdx.data();
        e.cityRecordBase = cityRec.data();
        e.charBase = chr.data();
        e.handleTable = handles.data();
        return e;
    }
};

} // namespace

TEST(SaveReconSaveTables, ConstantsRecovered) {
    CHECK_EQ(kSwt_PersonStride, 0x218u);
    CHECK_EQ(kSwt_CityIndexStride, 0x43u);
    CHECK_EQ(kSwt_CityRecordStride, 0x41u);
    CHECK_EQ(kSwt_CharStride, 0x194u);
    CHECK_EQ(kSwt_PersonSpan / kSwt_PersonStride, 768u);
    CHECK_EQ(kSwt_CityIndexSpan / kSwt_CityIndexStride, 8192u);
    CHECK_EQ(kSwt_CharSpan / kSwt_CharStride, 1280u);
}

TEST(SaveReconSaveTables, EmptyTablesProduceZeroCountsAndZeroChars) {
    Env env; SaveTableEnv e = env.mk();
    Capture cap;
    std::vector<u8> buf(4 + 4 + 1280 * (1 + 4 + 4 + 32 + 32 + 160 + 1 + 4 + 4) + 16, 0xCC);
    SaveTableSink sink = SaveTableSinkOpen(buf.data(), static_cast<u32>(buf.size()));
    CHECK(SaveWriteCityAndPersonTables(&sink, e, RecordObjHook, nullptr, &cap));
    // person count (0), city count (0)
    CHECK_EQ(RdU32(buf.data() + 0), 0u);
    CHECK_EQ(RdU32(buf.data() + 4), 0u);
    CHECK_EQ(cap.objCalls.size(), static_cast<size_t>(0));
    // each of 1280 characters: leading 0 then 9 zero-field blocks (no probe -> zero path)
    const u32 perChar = 1u + 4 + 4 + 32 + 32 + 160 + 1 + 4 + 4; // 242
    CHECK_EQ(sink.pos, 8u + 1280u * perChar);
    for (u32 i = 8; i < sink.pos; ++i)
        CHECK_EQ(buf[i], static_cast<u8>(0));
}

TEST(SaveReconSaveTables, PersonCountAndOrder) {
    Env env; SaveTableEnv e = env.mk();
    // mark entries 3 and 700 as live (non-null pointer at +0); set link ids
    auto setPerson = [&](u32 i, const u8* ptr, u32 link) {
        u8* base = env.person.data() + i * kSwt_PersonStride;
        std::memcpy(base, &ptr, sizeof(ptr));
        std::memcpy(env.personLink.data() + i * kSwt_PersonStride, &link, 4);
    };
    u8 dummyA = 1, dummyB = 2;
    setPerson(3, &dummyA, 0x1111);
    setPerson(700, &dummyB, 0x2222);

    Capture cap;
    std::vector<u8> buf(2 * 1024 * 1024, 0);
    SaveTableSink sink = SaveTableSinkOpen(buf.data(), static_cast<u32>(buf.size()));
    CHECK(SaveWriteCityAndPersonTables(&sink, e, RecordObjHook, nullptr, &cap));
    CHECK_EQ(RdU32(buf.data() + 0), 2u);             // person count
    CHECK_EQ(cap.objCalls.size(), static_cast<size_t>(2));
    CHECK(cap.objCalls[0].first == &dummyA);
    CHECK_EQ(cap.objCalls[0].second, static_cast<i32>(0x1111));
    CHECK(cap.objCalls[1].first == &dummyB);
    CHECK_EQ(cap.objCalls[1].second, static_cast<i32>(0x2222));
}

TEST(SaveReconSaveTables, CityCountUsesRecordTagAndIndex) {
    Env env; SaveTableEnv e = env.mk();
    // city index entry 10 -> record index 4 ; mark record 4 as live (tag 0x1D)
    {
        i16 idx = 4;
        std::memcpy(env.cityIdx.data() + 10 * kSwt_CityIndexStride, &idx, 2);
        env.cityRec[static_cast<size_t>(4) * kSwt_CityRecordStride] = kSwt_CityLiveTag;
        // recPtr at idx+0x3B, link at idx+0x02
        u8* ent = env.cityIdx.data() + 10 * kSwt_CityIndexStride;
        static u8 cityObj = 9;
        const u8* p = &cityObj;
        std::memcpy(ent + 0x3B, &p, sizeof(p));
        u32 link = 0x3333;
        std::memcpy(ent + 0x02, &link, 4);
    }
    Capture cap;
    std::vector<u8> buf(2 * 1024 * 1024, 0);
    SaveTableSink sink = SaveTableSinkOpen(buf.data(), static_cast<u32>(buf.size()));
    CHECK(SaveWriteCityAndPersonTables(&sink, e, RecordObjHook, nullptr, &cap));
    // person count 0 at +0, city count 1 at +4
    CHECK_EQ(RdU32(buf.data() + 0), 0u);
    CHECK_EQ(RdU32(buf.data() + 4), 1u);
    CHECK_EQ(cap.objCalls.size(), static_cast<size_t>(1));
    CHECK_EQ(cap.objCalls[0].second, static_cast<i32>(0x3333));
}

// Probe COPY-path: a live character record (slot[+9]!=0) whose handle passes the
// probe emits leading=rec[9] and the full record body fields. The three written
// fields at +0x14(=20), +0x24(=36) and +0x28(=40) are the link slots in the
// 32-bit-id model: +0x14 holds the handle index (identity), +0x24/+0x28 hold a
// slab byte-offset that the pre-pass converts to a record index (offset/404) or -1
// when 0. Only the non-link field (+12) is free.
TEST(SaveReconSaveTables, CharacterCopyPathEmitsFields) {
    Env env; SaveTableEnv e = env.mk();
    const u32 slot = 2;
    u8* rec = env.chr.data() + slot * kSwt_CharStride;
    rec[9] = 0x77;                                   // leading byte on copy path
    // +0x14: handle index 5 (4-byte). handleTable[5] -> non-null for the probe.
    u32 handleIdx = 5;
    std::memcpy(rec + 0x14, &handleIdx, 4);
    static u8 handleObj = 0;
    env.handles[5] = &handleObj;
    // free field at +12
    u32 f12 = 0xA1A2A3A4;
    std::memcpy(rec + 12, &f12, 4);
    rec[400] = 0xEE;
    // +0x24 link byte-offset -> record 7 (7*404); +0x28 null (0 -> -1)
    u32 link24 = 7u * kSwt_CharStride;
    std::memcpy(rec + 0x24, &link24, 4);
    u32 link28 = 0;
    std::memcpy(rec + 0x28, &link28, 4);

    CharProbeHook probe = [](const u8* h, void*) -> bool { return h != nullptr; };

    Capture cap;
    std::vector<u8> buf(2 * 1024 * 1024, 0);
    SaveTableSink sink = SaveTableSinkOpen(buf.data(), static_cast<u32>(buf.size()));
    CHECK(SaveWriteCityAndPersonTables(&sink, e, RecordObjHook, probe, &cap));

    // character 2 is the copy path; field order is +12,+20,+48,+80,+240,+400,+36,+40
    const u32 perChar = 1u + 4 + 4 + 32 + 32 + 160 + 1 + 4 + 4; // 242
    const u32 cstart = 8u + slot * perChar;
    CHECK_EQ(buf[cstart], static_cast<u8>(0x77));            // leading = rec[9]
    CHECK_EQ(RdU32(buf.data() + cstart + 1), 0xA1A2A3A4u);   // +12 (free)
    CHECK_EQ(RdU32(buf.data() + cstart + 5), 5u);            // +20 == handle index (identity)
    const u32 afterBlocks = cstart + 1 + 4 + 4 + 32 + 32 + 160;
    CHECK_EQ(buf[afterBlocks], static_cast<u8>(0xEE));       // +400 byte
    CHECK_EQ(RdU32(buf.data() + afterBlocks + 1), 7u);          // +36 (=+0x24 offset/404 = 7)
    CHECK_EQ(RdU32(buf.data() + afterBlocks + 5), 0xFFFFFFFFu); // +40 (=+0x28 null -> -1)

    // post-pass restored the link slots to byte-offset form
    CHECK_EQ(RdU32(rec + 0x24), 7u * kSwt_CharStride);       // index 7 -> offset
    CHECK_EQ(RdU32(rec + 0x28), 0u);                         // -1 -> 0 (null)
}

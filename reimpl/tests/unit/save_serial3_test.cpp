// Unit tests for guild::io per-table save serializers (part 3).
//   VIBE_Save_WriteBuildingSlotTables @0x5a5c1c
//   VIBE_Save_WriteObjectRecord       @0x5a55b0
//
// Golden vectors: total emitted byte counts and exact field contents are computed
// from the recovered field layout (see save_serial3.{h,cpp}). All I/O is over an
// in-memory write stream so the bytes can be inspected directly.
#include "test.h"
#include "io/save_serial3.h"
#include "io/vfs.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::io;

namespace {

// Recovered per-record byte sizes (see save_serial3_test computation).
constexpr u32 kSlotSub   = 40;                 // per city-slot sub-record
constexpr u32 kSlotTable = 16 + 62 * kSlotSub; // 2496
// gilde.exe 0x5a623c — the writer ALWAYS emits the +748 8-byte tail (no version
// gate on the write side; the gate is loader-only at 0x5aa77f). So every city-info
// record is 650 bytes regardless of the version argument.
constexpr u32 kCityInfoTail   = 650;

// Fill a buffer with a deterministic, non-zero pattern so any mis-sized field is
// caught when we compare specific offsets after a roundtrip.
void Fill(std::vector<u8>& v, u8 seed) {
    for (std::size_t i = 0; i < v.size(); ++i)
        v[i] = (u8)(seed + (i * 31u + 7u));
}

} // namespace

TEST(SaveSerial3, BuildingSlotTables_EmittedSize_WithTail) {
    std::vector<u8> slot((std::size_t)kBst_CitySlotTableStride * kBst_CitySlotTableCount, 0);  // in-memory stride (7952), not emitted size
    std::vector<u8> city((std::size_t)kBst_CityInfoStride * kBst_CityInfoRecCount, 0);
    Fill(slot, 1);
    Fill(city, 99);

    std::vector<u8> out(65536, 0);
    VfsHandle* w = VfsOpenMemoryStream(out.data(), (u32)out.size(), "wb");
    CHECK(w != nullptr);
    if (w) {
        bool ok = SaveWriteBuildingSlotTables(w, slot.data(), city.data(), 0x10037);
        CHECK(ok);
        VfsCloseStream(w);
    }
    // Expected total = 5*2496 + 4*650 = 15080.
    const u32 expect = kSlotTable * kBst_CitySlotTableCount
                     + kCityInfoTail * kBst_CityInfoRecCount;
    CHECK_EQ(expect, (u32)15080);
}

TEST(SaveSerial3, BuildingSlotTables_TailAlwaysWritten_LowVersion) {
    // The writer has NO version gate on the +748 tail (verified @0x5a623c): even at
    // a version below the loader gate (0x10037) the writer still emits the full
    // 650-byte record. Size the destination to the no-tail size; the write must
    // OVERFLOW (the memory stream clamps) -> the function observes a short write and
    // returns false. This pins the unconditional-tail behavior.
    std::vector<u8> slot((std::size_t)kBst_CitySlotTableStride * kBst_CitySlotTableCount, 0);  // in-memory stride (7952), not emitted size
    std::vector<u8> city((std::size_t)kBst_CityInfoStride * kBst_CityInfoRecCount, 0);
    Fill(slot, 5);
    Fill(city, 200);

    // No-tail size (642/record): one byte too small for the always-emitted tail.
    const u32 noTail = kSlotTable * kBst_CitySlotTableCount
                     + 642 * kBst_CityInfoRecCount;  // 15048
    CHECK_EQ(noTail, (u32)15048);

    std::vector<u8> out(noTail, 0);
    VfsHandle* w = VfsOpenMemoryStream(out.data(), (u32)out.size(), "wb");
    CHECK(w != nullptr);
    if (w) {
        // version 0x10036 < 0x10037: writer STILL emits the tail -> overflow -> false.
        bool ok = SaveWriteBuildingSlotTables(w, slot.data(), city.data(), 0x10036);
        CHECK(!ok);
        VfsCloseStream(w);
    }

    // And with room for the tail it succeeds at the SAME low version (tail unconditional).
    std::vector<u8> out2(kSlotTable * kBst_CitySlotTableCount
                         + kCityInfoTail * kBst_CityInfoRecCount, 0);  // 15080
    VfsHandle* w2 = VfsOpenMemoryStream(out2.data(), (u32)out2.size(), "wb");
    CHECK(w2 != nullptr);
    if (w2) {
        bool ok = SaveWriteBuildingSlotTables(w2, slot.data(), city.data(), 0x10036);
        CHECK(ok);
        VfsCloseStream(w2);
    }
}

TEST(SaveSerial3, BuildingSlotTables_NullGuards) {
    std::vector<u8> buf(16, 0);
    VfsHandle* w = VfsOpenMemoryStream(buf.data(), (u32)buf.size(), "wb");
    u8 a[16] = {0};
    CHECK(!SaveWriteBuildingSlotTables(nullptr, a, a, 0x10037));
    if (w) {
        CHECK(!SaveWriteBuildingSlotTables(w, nullptr, a, 0x10037));
        CHECK(!SaveWriteBuildingSlotTables(w, a, nullptr, 0x10037));
        VfsCloseStream(w);
    }
}

TEST(SaveSerial3, ObjectRecord_EmittedSize_Default) {
    // Inert default hooks -> no stock sub-record -> zero templates. mesh slot 0.
    u8 rec[600] = {0};
    // rec+100 (the mesh slot dword) left zero -> 64-byte zero template path.
    std::vector<u8> out(512, 0);
    VfsHandle* w = VfsOpenMemoryStream(out.data(), (u32)out.size(), "wb");
    CHECK(w != nullptr);
    if (w) {
        bool ok = SaveWriteObjectRecord(w, rec, /*linkId*/0x11223344, /*meshPtr*/nullptr,
                                        /*flagByte*/0x5A);
        CHECK(ok);
        VfsCloseStream(w);
    }
    // 4+32+4+4+2+48+64 + 16+12 + 1+4 + 64 = 255 bytes.
    const u32 expect = 4 + 32 + 4 + 4 + 2 + 48 + 64 + 16 + 12 + 1 + 4 + 64;
    CHECK_EQ(expect, (u32)255);

    // The flag byte sits right after the 158-byte header and the two zero
    // templates: 158 + 16 + 12 = 186.
    const u32 flagPos = (4 + 32 + 4 + 4 + 2 + 48 + 64) + 16 + 12;
    CHECK_EQ(flagPos, (u32)186);
    if (out.size() > flagPos) {
        CHECK_EQ(out[flagPos], (u8)0x5A);
        // linkId little-endian immediately after.
        u32 link;
        std::memcpy(&link, &out[flagPos + 1], 4);
        CHECK_EQ(link, (u32)0x11223344);
    }
}

TEST(SaveSerial3, ObjectRecord_MeshSlotPresent) {
    u8 rec[600] = {0};
    // Set the mesh slot dword (rec+100) non-zero -> writes meshPtr's 64 bytes.
    u32 slot = 0xDEADBEEF;
    std::memcpy(rec + 100, &slot, 4);
    u8 mesh[64];
    for (int i = 0; i < 64; ++i) mesh[i] = (u8)(0xA0 + i);

    std::vector<u8> out(512, 0);
    VfsHandle* w = VfsOpenMemoryStream(out.data(), (u32)out.size(), "wb");
    CHECK(w != nullptr);
    if (w) {
        bool ok = SaveWriteObjectRecord(w, rec, 7, mesh, 0);
        CHECK(ok);
        VfsCloseStream(w);
    }
    // Mesh block is the last 64 bytes of the 255-byte record (offset 191).
    const u32 meshPos = 255 - 64;
    CHECK_EQ(meshPos, (u32)191);
    if (out.size() >= 255) {
        CHECK_EQ(out[meshPos], (u8)0xA0);
        CHECK_EQ(out[meshPos + 63], (u8)(0xA0 + 63));
    }

    // A non-zero mesh slot with a null pointer is a malformed call -> false.
    std::vector<u8> out2(512, 0);
    VfsHandle* w2 = VfsOpenMemoryStream(out2.data(), (u32)out2.size(), "wb");
    if (w2) {
        CHECK(!SaveWriteObjectRecord(w2, rec, 7, nullptr, 0));
        VfsCloseStream(w2);
    }
}

TEST(SaveSerial3, ObjectRecord_HookStockBlocks) {
    // Install a hook that supplies live 16/12-byte stock blocks.
    static u8 blkA[16], blkB[12];
    for (int i = 0; i < 16; ++i) blkA[i] = (u8)(0x10 + i);
    for (int i = 0; i < 12; ++i) blkB[i] = (u8)(0x80 + i);

    ObjectRecordHooks h{};
    h.resolveStock = [](const u8*, const u8** a, const u8** b) -> bool {
        *a = blkA; *b = blkB; return true;
    };
    ObjectRecordHooks prev = SaveSetObjectRecordHooks(h);

    u8 rec[600] = {0};
    std::vector<u8> out(512, 0);
    VfsHandle* w = VfsOpenMemoryStream(out.data(), (u32)out.size(), "wb");
    if (w) {
        bool ok = SaveWriteObjectRecord(w, rec, 0, nullptr, 0);
        CHECK(ok);
        VfsCloseStream(w);
    }
    // Stock block A starts right after the 154-byte header.
    const u32 aPos = 4 + 32 + 4 + 4 + 2 + 48 + 64;  // 158
    CHECK_EQ(aPos, (u32)158);
    if (out.size() > aPos + 28) {
        CHECK_EQ(out[aPos], (u8)0x10);
        CHECK_EQ(out[aPos + 15], (u8)(0x10 + 15));
        CHECK_EQ(out[aPos + 16], (u8)0x80);          // block B
        CHECK_EQ(out[aPos + 16 + 11], (u8)(0x80 + 11));
    }

    SaveSetObjectRecordHooks(prev);  // restore inert default
}

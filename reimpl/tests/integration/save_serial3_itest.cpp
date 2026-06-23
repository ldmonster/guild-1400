// Integration test: wire the WRITE serializer SaveWriteBuildingSlotTables
// (VIBE_Save_WriteBuildingSlotTables @0x5a5c1c) against the REAL reconstructed
// LOAD sibling LoadBuildingSlotTables (VIBE_Save_LoadBuildingSlotTables @0x5aa058,
// src/io/save_world_load.cpp) over a real VFS memory stream, and assert a
// byte-exact write -> load roundtrip at every persisted offset.
//
// This is the genuine live wiring: the original save path calls WriteBuildingSlot-
// Tables and the original load path calls LoadBuildingSlotTables over the same
// stream; here we close the loop and prove the two are exact mirrors.
#include "test.h"
#include "io/save_serial3.h"
#include "io/save_world_load.h"   // REAL LoadBuildingSlotTables sibling
#include "io/vfs.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::io;

namespace {

constexpr u32 kSlotTableBytes = (u32)kBst_CitySlotTableStride;   // 7952
constexpr u32 kCityInfoBytes  = (u32)kBst_CityInfoStride;        // 756
constexpr std::size_t kSlotTotal = (std::size_t)kSlotTableBytes * kBst_CitySlotTableCount;
constexpr std::size_t kCityTotal = (std::size_t)kCityInfoBytes  * kBst_CityInfoRecCount;

// Only the bytes the serializer actually touches should survive a roundtrip;
// the padding holes (e.g. slot-sub +2..+3, +16..+31, +62..+63) are NOT written,
// so we must compare just the serialized fields. We do that by zeroing the source
// holes too: build the source from a pattern, then clear every unwritten byte so a
// straight memcmp against the loaded buffer is valid.
void ClearSlotTableHoles(u8* t) {
    // header is 16 bytes (all written). For each of 62 sub-records (stride 128):
    // written: +0(2) +4(4) +8(4) +12(4) +32(4) +36(4) +44(4) +48(4) +52(4)
    //          +56(4) +60(2). Everything else is a hole.
    u8* sub = t + 16;
    for (int s = 0; s < kBst_CitySlotSubCount; ++s) {
        bool keep[128] = {false};
        auto mark = [&](int off, int n) { for (int i = 0; i < n; ++i) keep[off + i] = true; };
        mark(0, 2); mark(4, 4); mark(8, 4); mark(12, 4); mark(32, 4); mark(36, 4);
        mark(44, 4); mark(48, 4); mark(52, 4); mark(56, 4); mark(60, 2);
        for (int i = 0; i < 128; ++i) if (!keep[i]) sub[i] = 0;
        sub += 128;
    }
}

void ClearCityInfoHoles(u8* c) {
    bool keep[756] = {false};
    auto mark = [&](int off, int n) { for (int i = 0; i < n; ++i) keep[off + i] = true; };
    mark(0, 0x20); mark(64, 8); mark(72, 1); mark(76, 4); mark(80, 4); mark(84, 2);
    mark(88, 8); mark(96, 1); mark(97, 1);
    for (int i = 0; i < 10; ++i) mark(100 + i * 8, 8);
    for (int i = 0; i < 10; ++i) mark(180 + i * 8, 8);
    mark(260, 1); mark(261, 1);
    for (int i = 0; i < 4; ++i) mark(264 + i * 4, 4);
    for (int i = 0; i < 4; ++i) mark(280 + i * 4, 4);
    mark(296, 4);
    for (int i = 0; i < 11; ++i) mark(300 + i, 1);
    mark(311, 1);
    for (int i = 0; i < 7; ++i) mark(312 + i, 1);
    mark(319, 1); mark(320, 1); mark(321, 1); mark(322, 1);
    for (int i = 0; i < 8; ++i) mark(324 + i * 18, 18);
    mark(468, 4); mark(472, 4); mark(476, 0xD0);
    mark(748, 8);  // tail (written/read only when version >= 0x10037)
    for (int i = 0; i < 756; ++i) if (!keep[i]) c[i] = 0;
}

void Pattern(std::vector<u8>& v, u32 seed) {
    for (std::size_t i = 0; i < v.size(); ++i)
        v[i] = (u8)((i * 131u + seed * 7u + 13u) & 0xFF);
}

} // namespace

TEST(SaveSerial3Integration, BuildingSlotTables_Roundtrip_RealLoader) {
    const u32 version = 0x10037;  // includes the +748 tail

    std::vector<u8> srcSlot(kSlotTotal, 0), srcCity(kCityTotal, 0);
    Pattern(srcSlot, 1);
    Pattern(srcCity, 2);
    // Zero the unwritten holes so a direct compare validates the live fields.
    for (int i = 0; i < kBst_CitySlotTableCount; ++i)
        ClearSlotTableHoles(srcSlot.data() + (std::size_t)i * kSlotTableBytes);
    for (int i = 0; i < kBst_CityInfoRecCount; ++i)
        ClearCityInfoHoles(srcCity.data() + (std::size_t)i * kCityInfoBytes);

    // --- WRITE through the real VFS memory stream ---
    std::vector<u8> stream(64 * 1024, 0);
    VfsHandle* w = VfsOpenMemoryStream(stream.data(), (u32)stream.size(), "wb");
    CHECK(w != nullptr);
    bool wrote = false;
    if (w) {
        wrote = SaveWriteBuildingSlotTables(w, srcSlot.data(), srcCity.data(), version);
        CHECK(wrote);
        VfsCloseStream(w);
    }

    // --- LOAD back through the REAL sibling loader ---
    // Init dst to 0 so unwritten holes (which the loader never touches) match the
    // source holes we cleared above; only the serialized fields are roundtripped.
    std::vector<u8> dstSlot(kSlotTotal, 0), dstCity(kCityTotal, 0);
    VfsHandle* r = VfsOpenMemoryStream(stream.data(), (u32)stream.size(), "rb");
    CHECK(r != nullptr);
    bool loaded = false;
    if (r && wrote) {
        loaded = LoadBuildingSlotTables(r, dstSlot.data(), dstCity.data(), version);
        CHECK(loaded);
        VfsCloseStream(r);
    }

    // --- byte-exact at every serialized offset ---
    if (loaded) {
        CHECK_EQ(std::memcmp(srcSlot.data(), dstSlot.data(), kSlotTotal), 0);
        CHECK_EQ(std::memcmp(srcCity.data(), dstCity.data(), kCityTotal), 0);
        // Spot-check a couple of representative live offsets explicitly.
        CHECK_EQ(dstSlot[0], srcSlot[0]);                 // table-0 header byte 0
        CHECK_EQ(dstCity[748], srcCity[748]);             // tail byte (version-gated)
    }
}

// gilde.exe write/load ASYMMETRY at version < 0x10037: the WRITER (@0x5a623c) has
// NO version gate on the +748 tail and always emits it, while the LOADER (@0x5aa77f)
// only reads it when version >= 0x10037. So at a low version the writer's stream
// carries 8 extra bytes per city-info record that the loader does NOT consume,
// shifting every record after the first. This is a faithful quirk of the original
// binary (unreachable in practice — the live writer always runs at 0x10045). This
// test pins the asymmetry: the FIRST city-info record's pre-tail fields still load
// byte-exact, but later records desync.
TEST(SaveSerial3Integration, BuildingSlotTables_WriteLoadAsymmetry_LowVersion) {
    const u32 version = 0x10030;  // below the loader gate 0x10037

    std::vector<u8> srcSlot(kSlotTotal, 0), srcCity(kCityTotal, 0);
    Pattern(srcSlot, 7);
    Pattern(srcCity, 9);
    for (int i = 0; i < kBst_CitySlotTableCount; ++i)
        ClearSlotTableHoles(srcSlot.data() + (std::size_t)i * kSlotTableBytes);
    for (int i = 0; i < kBst_CityInfoRecCount; ++i)
        ClearCityInfoHoles(srcCity.data() + (std::size_t)i * kCityInfoBytes);

    std::vector<u8> stream(64 * 1024, 0);
    VfsHandle* w = VfsOpenMemoryStream(stream.data(), (u32)stream.size(), "wb");
    bool wrote = false;
    if (w) {
        // Writer emits the tail unconditionally even at this low version.
        wrote = SaveWriteBuildingSlotTables(w, srcSlot.data(), srcCity.data(), version);
        CHECK(wrote);
        VfsCloseStream(w);
    }
    std::vector<u8> dstSlot(kSlotTotal, 0), dstCity(kCityTotal, 0);
    VfsHandle* r = VfsOpenMemoryStream(stream.data(), (u32)stream.size(), "rb");
    bool loaded = false;
    if (r && wrote) {
        loaded = LoadBuildingSlotTables(r, dstSlot.data(), dstCity.data(), version);
        CHECK(loaded);
        VfsCloseStream(r);
    }
    if (loaded) {
        // The 5 slot tables (written before any city-info tail) roundtrip exactly.
        CHECK_EQ(std::memcmp(srcSlot.data(), dstSlot.data(), kSlotTotal), 0);
        // City-info record 0, all pre-tail bytes (0..747), roundtrips exactly.
        CHECK_EQ(std::memcmp(srcCity.data(), dstCity.data(), 748), 0);
        // The loader did NOT read the writer's +748 tail, so record 0's tail in the
        // destination stays at its init value (0), not the source pattern.
        bool tailReadBack = (std::memcmp(srcCity.data() + 748, dstCity.data() + 748, 8) == 0);
        CHECK(!tailReadBack);  // asymmetry: tail emitted by writer, skipped by loader
    }
}

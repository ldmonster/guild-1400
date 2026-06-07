// End-to-end: a mini save/load flow that exercises both serializers in sequence
// over one VFS stream, the way the top save writer drives them — write an object
// record, then the building-slot tables, then read the building-slot tables back
// through the REAL loader and confirm the object-record bytes that preceded them
// are intact (i.e. the stream cursor advanced correctly across serializers).
#include "test.h"
#include "io/save_serial3.h"
#include "io/save_world_load.h"
#include "io/vfs.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::io;

namespace {
constexpr std::size_t kSlotTotal =
    (std::size_t)kBst_CitySlotTableStride * kBst_CitySlotTableCount;
constexpr std::size_t kCityTotal =
    (std::size_t)kBst_CityInfoStride * kBst_CityInfoRecCount;

void ClearSlotHoles(u8* base) {
    for (int t = 0; t < kBst_CitySlotTableCount; ++t) {
        u8* sub = base + (std::size_t)t * kBst_CitySlotTableStride + 16;
        for (int s = 0; s < kBst_CitySlotSubCount; ++s) {
            bool keep[128] = {false};
            auto m = [&](int o, int n){ for (int i=0;i<n;++i) keep[o+i]=true; };
            m(0,2); m(4,4); m(8,4); m(12,4); m(32,4); m(36,4);
            m(44,4); m(48,4); m(52,4); m(56,4); m(60,2);
            for (int i=0;i<128;++i) if(!keep[i]) sub[i]=0;
            sub += 128;
        }
    }
}
void ClearCityHoles(u8* base, u32 version) {
    for (int r = 0; r < kBst_CityInfoRecCount; ++r) {
        u8* c = base + (std::size_t)r * kBst_CityInfoStride;
        bool keep[756] = {false};
        auto m = [&](int o, int n){ for (int i=0;i<n;++i) keep[o+i]=true; };
        m(0,0x20); m(64,8); m(72,1); m(76,4); m(80,4); m(84,2); m(88,8); m(96,1); m(97,1);
        for (int i=0;i<10;++i) m(100+i*8,8);
        for (int i=0;i<10;++i) m(180+i*8,8);
        m(260,1); m(261,1);
        for (int i=0;i<4;++i) m(264+i*4,4);
        for (int i=0;i<4;++i) m(280+i*4,4);
        m(296,4);
        for (int i=0;i<11;++i) m(300+i,1);
        m(311,1);
        for (int i=0;i<7;++i) m(312+i,1);
        m(319,1); m(320,1); m(321,1); m(322,1);
        for (int i=0;i<8;++i) m(324+i*18,18);
        m(468,4); m(472,4); m(476,0xD0);
        if (version >= 0x10037) m(748,8);
        for (int i=0;i<756;++i) if(!keep[i]) c[i]=0;
    }
}
} // namespace

TEST(SaveSerial3E2E, ObjectThenTables_StreamFlow) {
    const u32 version = 0x10037;

    // 1) An object record (inert default hooks -> zero stock blocks, no mesh).
    u8 rec[600] = {0};
    for (int i = 0; i < 600; ++i) rec[i] = (u8)(i & 0xFF);
    std::memset(rec + 100, 0, 4);   // mesh slot 0 -> zero template path

    // 2) Building-slot tables source.
    std::vector<u8> srcSlot(kSlotTotal, 0), srcCity(kCityTotal, 0);
    for (std::size_t i = 0; i < srcSlot.size(); ++i) srcSlot[i] = (u8)((i*3+1)&0xFF);
    for (std::size_t i = 0; i < srcCity.size(); ++i) srcCity[i] = (u8)((i*5+2)&0xFF);
    ClearSlotHoles(srcSlot.data());
    ClearCityHoles(srcCity.data(), version);

    std::vector<u8> stream(80 * 1024, 0);
    VfsHandle* w = VfsOpenMemoryStream(stream.data(), (u32)stream.size(), "wb");
    CHECK(w != nullptr);
    bool wroteObj = false, wroteTab = false;
    if (w) {
        wroteObj = SaveWriteObjectRecord(w, rec, /*linkId*/0x0BADF00D, nullptr, /*flag*/0x77);
        CHECK(wroteObj);
        wroteTab = SaveWriteBuildingSlotTables(w, srcSlot.data(), srcCity.data(), version);
        CHECK(wroteTab);
        VfsCloseStream(w);
    }

    // The object record occupies the first 255 bytes; tables follow.
    const u32 objSize = 255;

    // 3) Read back: skip the 255-byte object record, then load the tables through
    //    the REAL sibling and assert byte-exact reconstruction.
    VfsHandle* r = VfsOpenMemoryStream(stream.data(), (u32)stream.size(), "rb");
    CHECK(r != nullptr);
    bool loaded = false;
    std::vector<u8> dstSlot(kSlotTotal, 0), dstCity(kCityTotal, 0);
    if (r && wroteObj && wroteTab) {
        // Consume the object record bytes (read-stream, exactly objSize).
        std::vector<u8> objBack(objSize, 0);
        u32 got = VfsReadStream(objBack.data(), 1, r, objSize);
        CHECK_EQ(got, objSize);
        // The object record's first 4 bytes equal rec+0.
        CHECK_EQ(objBack[0], rec[0]);
        // Flag byte at +186, linkId right after.
        CHECK_EQ(objBack[186], (u8)0x77);
        u32 link; std::memcpy(&link, &objBack[187], 4);
        CHECK_EQ(link, (u32)0x0BADF00D);

        loaded = LoadBuildingSlotTables(r, dstSlot.data(), dstCity.data(), version);
        CHECK(loaded);
        VfsCloseStream(r);
    }
    if (loaded) {
        CHECK_EQ(std::memcmp(srcSlot.data(), dstSlot.data(), kSlotTotal), 0);
        CHECK_EQ(std::memcmp(srcCity.data(), dstCity.data(), kCityTotal), 0);
    }
}

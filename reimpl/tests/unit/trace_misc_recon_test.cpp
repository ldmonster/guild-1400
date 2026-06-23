// Golden tests for VIBE_Trace cluster (trace_misc_recon).
#include "tests/framework/test.h"
#include "util/trace_misc_recon.h"
#include <cstring>
#include <vector>

using namespace guild;
using guild::util::Trace_GetEntryByIndex;
using guild::util::Trace_IsAddressReadable;

namespace {
// Build a symbol table: 16-byte header (count at +4), then length-prefixed
// entries (u32 len; len bytes). Entry stride = len + 4.
std::vector<u8> MakeTable(const std::vector<std::vector<u8>>& entries) {
    std::vector<u8> t(16, 0);
    u32 count = static_cast<u32>(entries.size());
    std::memcpy(&t[4], &count, 4);
    for (const auto& e : entries) {
        u32 len = static_cast<u32>(e.size());
        u8 lb[4];
        std::memcpy(lb, &len, 4);
        t.insert(t.end(), lb, lb + 4);
        t.insert(t.end(), e.begin(), e.end());
    }
    return t;
}
} // namespace

TEST(MiscReconTrace, GetEntryByIndexWalksLengthPrefixed) {
    auto t = MakeTable({{1, 2, 3}, {9}, {4, 4, 4, 4, 4}});
    const u32* e0 = Trace_GetEntryByIndex(t.data(), 0);
    const u32* e1 = Trace_GetEntryByIndex(t.data(), 1);
    const u32* e2 = Trace_GetEntryByIndex(t.data(), 2);
    CHECK(e0 != nullptr);
    CHECK(e1 != nullptr);
    CHECK(e2 != nullptr);
    // e0 at table+16, len 3 -> e1 at +16 + (3+4) = +23, len 1 -> e2 at +23+(1+4)=+28
    CHECK_EQ(reinterpret_cast<const u8*>(e0) - t.data(), 16);
    CHECK_EQ(reinterpret_cast<const u8*>(e1) - t.data(), 23);
    CHECK_EQ(reinterpret_cast<const u8*>(e2) - t.data(), 28);
    CHECK_EQ(*e0, 3u); // length field of entry 0
    CHECK_EQ(*e1, 1u);
    CHECK_EQ(*e2, 5u);
}

TEST(MiscReconTrace, GetEntryByIndexOutOfRange) {
    auto t = MakeTable({{1}, {2}});
    CHECK(Trace_GetEntryByIndex(t.data(), 2) == nullptr); // index == count
    CHECK(Trace_GetEntryByIndex(t.data(), 99) == nullptr);
}

TEST(MiscReconTrace, IsAddressReadableHighTiers) {
    // protectClass > 0x10: 0x20 ok, 0x40 ok, 0x80 ok; 0x11 (>0x10,<0x20) -> not(==32) fail.
    CHECK_EQ(Trace_IsAddressReadable(0x10, 0), true);  // == 0x10 -> true (no inner test)
    CHECK_EQ(Trace_IsAddressReadable(0x20, 0), true);
    CHECK_EQ(Trace_IsAddressReadable(0x40, 0), true);
    CHECK_EQ(Trace_IsAddressReadable(0x80, 0), true);
    CHECK_EQ(Trace_IsAddressReadable(0x11, 0), false); // >0x10, <0x40, !=32
    CHECK_EQ(Trace_IsAddressReadable(0x41, 0), false); // >0x40, !=0x80
}

TEST(MiscReconTrace, IsAddressReadableLowTierUsesPageProtect) {
    // protectClass < 0x10 with class in {2,4}: result depends on probed protect.
    CHECK_EQ(Trace_IsAddressReadable(2, 2), true);  // READONLY
    CHECK_EQ(Trace_IsAddressReadable(2, 4), true);  // READWRITE
    CHECK_EQ(Trace_IsAddressReadable(4, 1), false); // not 2/4
    CHECK_EQ(Trace_IsAddressReadable(1, 2), false); // class < 2 -> fail
    CHECK_EQ(Trace_IsAddressReadable(3, 2), false); // class >2 && !=4 -> fail
}

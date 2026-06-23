// Golden-vector tests for VIBE_Table_FindEntrySlotById @0x4bad28
// (guild::world::TableFindEntrySlotById).
#include "tests/framework/test.h"
#include "world/lookup_table_save_recon.h"

#include <vector>

using namespace guild;
using guild::world::TableFindEntrySlotById;
using guild::world::kLut_StrideDwords;
using guild::world::kLut_LimitDwords;
using guild::world::kLut_EntryCount;

namespace {
// Build a 64-entry table (stride 67 dwords). Only the first dword of each entry
// is the key; the rest is left as a recognizable filler.
std::vector<u32> MakeTable() {
    std::vector<u32> t(static_cast<size_t>(kLut_EntryCount) * kLut_StrideDwords + 16, 0xDEADBEEFu);
    for (int e = 0; e < kLut_EntryCount; ++e)
        t[static_cast<size_t>(e) * kLut_StrideDwords] = 1000u + e; // distinct keys
    return t;
}
} // namespace

TEST(SaveReconLut, ConstantsRecovered) {
    CHECK_EQ(kLut_StrideDwords, 67);
    CHECK_EQ(kLut_LimitDwords, 4288);
    CHECK_EQ(kLut_EntryCount, 64);
}

TEST(SaveReconLut, FirstSlotImmediateMatchClearsAndReturnsZero) {
    auto t = MakeTable();
    // id == key[0] -> hit at result 0, returns 0, clears slot 0
    u32 r = TableFindEntrySlotById(t.data(), 1000u);
    CHECK_EQ(r, 0u);
    CHECK_EQ(t[0], 0u);
}

TEST(SaveReconLut, MiddleSlotMatchReturnsByteOffsetAndClears) {
    auto t = MakeTable();
    const int slot = 5;                       // key 1005
    u32 r = TableFindEntrySlotById(t.data(), 1005u);
    const u32 expectIndex = static_cast<u32>(slot) * kLut_StrideDwords; // dword index
    CHECK_EQ(r, expectIndex * 4u);            // returns dword index * 4
    CHECK_EQ(t[expectIndex], 0u);             // matched key cleared
    // neighbours untouched
    CHECK_EQ(t[0], 1000u);
}

TEST(SaveReconLut, LastSlotMatch) {
    auto t = MakeTable();
    const int slot = kLut_EntryCount - 1;     // 63, key 1063
    u32 r = TableFindEntrySlotById(t.data(), 1063u);
    const u32 expectIndex = static_cast<u32>(slot) * kLut_StrideDwords; // 63*67 = 4221
    CHECK_EQ(r, expectIndex * 4u);
    CHECK_EQ(t[expectIndex], 0u);
}

TEST(SaveReconLut, NoMatchReturnsTerminalOffset) {
    auto t = MakeTable();
    u32 r = TableFindEntrySlotById(t.data(), 0x7777u);
    // running dword index reaches kLut_LimitDwords (4288) -> returns 4288*4
    CHECK_EQ(r, static_cast<u32>(kLut_LimitDwords) * 4u);
    // nothing cleared
    CHECK_EQ(t[0], 1000u);
}

// HARDENING: the miss-path walk must not read past the documented 64-entry table.
// The last dword the routine reads is at index 63*67 = 4221 (it breaks once the
// running index reaches 4288, BEFORE the read). Size the table to exactly cover that
// last read (4222 dwords) so ASAN flags any over-read on the no-match scan.
TEST(SaveReconLut, MissScanStaysInBoundsOnExactTable) {
    // 4222 dwords == last-read index (4221) + 1; all keys distinct from the query.
    std::vector<u32> t(63u * kLut_StrideDwords + 1u, 0u);
    for (int e = 0; e < kLut_EntryCount && static_cast<size_t>(e) * kLut_StrideDwords < t.size(); ++e)
        t[static_cast<size_t>(e) * kLut_StrideDwords] = 2000u + e;
    u32 r = TableFindEntrySlotById(t.data(), 0xCAFEu);   // not present
    CHECK_EQ(r, static_cast<u32>(kLut_LimitDwords) * 4u);
}

TEST(SaveReconLut, QuirkFirstSlotClearedEvenWhenSearchStartsThere) {
    // The success path always zeroes dword[result]; with id==key[0] that is slot 0.
    auto t = MakeTable();
    (void)TableFindEntrySlotById(t.data(), 1000u);
    CHECK_EQ(t[0], 0u);
    // a re-query for the same id now misses (slot was cleared)
    u32 r2 = TableFindEntrySlotById(t.data(), 1000u);
    CHECK_EQ(r2, static_cast<u32>(kLut_LimitDwords) * 4u);
}

// Golden-vector tests for gilde.exe 0x5384d0 — VIBE_GesetzTable_FindByKey
// (reconstructed as guild::world::GesetzTableFindByKey).
//
// The function scans the shared 24-byte descriptor table (g_eventTable, count
// g_eventTableCount) comparing each record's value byte (+0x04) to the key and
// returns a pointer to the matching value byte, or nullptr.
//
// Golden vectors are taken from the shipped static image at gilde.exe @0x63CD48
// (loaded by EventTableLoadDefault). In that image the per-record value byte
// (record+0x04 == byte_63CD4C[24*i]) is the row's subtype id, ascending with the
// row index (mostly == i, with a couple of ids skipped near the end):
//   row 0 = 0x00 (free sentinel), 1=0x01, 2=0x02, ..., 14=0x0e, ..., 45=0x2f,
//   46=0x30, 47=0x31, verified by reading g_eventTable[i].value over the image.

#include "tests/framework/test.h"

#include "world/event.h"
#include "world/gesetztable_law_recon.h"

using namespace guild::world;
using guild::u8;

namespace {

// Build a small synthetic table so the test does not depend on the 48-row default
// image staying byte-identical; values chosen to exercise first/middle/last/dup.
void LoadSynthetic() {
    EventTableReset();
    g_eventTableCount = 5;
    g_eventTable[0].value = 0x10;
    g_eventTable[1].value = 0x20;
    g_eventTable[2].value = 0x20;  // duplicate value -> first match must win
    g_eventTable[3].value = 0x30;
    g_eventTable[4].value = 0x40;
}

} // namespace

TEST(LawReconGesetzTable, EmptyTableReturnsNull) {
    EventTableReset();                       // count == 0  (dword_5383F0 <= 0)
    CHECK(GesetzTableFindByKey(0x00) == nullptr);
    CHECK(GesetzTableFindByKey(0xFF) == nullptr);
    CHECK_EQ(GesetzTableFindIndexByKey(0x00), -1);
}

TEST(LawReconGesetzTable, FindsFirstRecord) {
    LoadSynthetic();
    u8* p = GesetzTableFindByKey(0x10);
    CHECK(p != nullptr);
    CHECK_EQ((int)*p, 0x10);
    CHECK(p == &g_eventTable[0].value);
    CHECK_EQ(GesetzTableFindIndexByKey(0x10), 0);
}

TEST(LawReconGesetzTable, FindsLastRecord) {
    LoadSynthetic();
    u8* p = GesetzTableFindByKey(0x40);
    CHECK(p != nullptr);
    CHECK(p == &g_eventTable[4].value);
    CHECK_EQ(GesetzTableFindIndexByKey(0x40), 4);
}

TEST(LawReconGesetzTable, DuplicateReturnsFirstMatch) {
    LoadSynthetic();
    // rows 1 and 2 both carry 0x20; the original's first-hit-wins scan stops at 1.
    CHECK_EQ(GesetzTableFindIndexByKey(0x20), 1);
    CHECK(GesetzTableFindByKey(0x20) == &g_eventTable[1].value);
}

TEST(LawReconGesetzTable, MissingKeyReturnsNull) {
    LoadSynthetic();
    CHECK(GesetzTableFindByKey(0x99) == nullptr);
    CHECK_EQ(GesetzTableFindIndexByKey(0x99), -1);
}

TEST(LawReconGesetzTable, ReturnedPointerIsLiveValueByte) {
    LoadSynthetic();
    u8* p = GesetzTableFindByKey(0x30);
    CHECK(p != nullptr);
    // Pointer aliases the record's value field: mutating through it is observable.
    *p = 0x31;
    CHECK_EQ((int)g_eventTable[3].value, 0x31);
    // and re-finding by the old key now misses, by the new key hits.
    CHECK(GesetzTableFindByKey(0x30) == nullptr);
    CHECK(GesetzTableFindByKey(0x31) == &g_eventTable[3].value);
}

TEST(LawReconGesetzTable, DefaultImageGoldenValues) {
    EventTableLoadDefault();                 // 48-row shipped image
    CHECK_EQ(g_eventTableCount, 48);
    // value byte == sequential row id; row 0 == 0x00 (the free-sentinel record).
    CHECK_EQ(GesetzTableFindIndexByKey(0x00), 0);
    CHECK_EQ(GesetzTableFindIndexByKey(0x01), 1);
    CHECK_EQ(GesetzTableFindIndexByKey(0x02), 2);
    CHECK_EQ(GesetzTableFindIndexByKey(0x05), 5);
    CHECK_EQ(GesetzTableFindIndexByKey(0x0e), 14);
    CHECK_EQ(GesetzTableFindIndexByKey(0x2f), 45);
    CHECK_EQ(GesetzTableFindIndexByKey(0x31), 47);   // last populated row
    // A value present in the image returns a pointer to that record's value byte.
    u8* p = GesetzTableFindByKey(0x02);
    CHECK(p != nullptr);
    CHECK_EQ((int)*p, 0x02);
    CHECK(p == &g_eventTable[2].value);
}

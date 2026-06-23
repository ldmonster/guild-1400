// WAVE-18 — first-name / dynasty-name string tables (sim/name_tables).
//
// The three runtime name pointer tables (dword_8C400C male / dword_8C4320 female /
// dword_8C4508 dynasty-female) are fixed-index SLICES of the global localized-text
// array dword_8C36B0, filled as ordinary text resources (Text_C_Personen.res) by
// VIBE_Text_LoadTextFile @0x44dba0. These tests golden-pin:
//   (a) the slice anchors/counts recovered from the literal table addresses
//       relative to 0x8C36B0 (599 / 796 / 918, counts 191 / 112 / 149);
//   (b) the .res parse + slice read on a SYNTHETIC Text_C_Personen.res that spans
//       the name id ranges (the exact compiled .res binary format BuildTextArray
//       consumes), so NameAt(kind,index) returns the expected string;
//   (c) the RandomModulo draw-width alignment (0xBF/0x70/0x95) and bounds/inert
//       fallback behavior ("" when not loaded / out of range).
//
// No real asset needed; the real-archive path is covered by the guarded e2e.
#include "tests/framework/test.h"

#include "sim/name_tables.h"
#include "gui/text/textdb.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

void PutU32(std::vector<u8>& b, std::uint32_t v) {
    b.push_back(static_cast<u8>(v & 0xFF));
    b.push_back(static_cast<u8>((v >> 8) & 0xFF));
    b.push_back(static_cast<u8>((v >> 16) & 0xFF));
    b.push_back(static_cast<u8>((v >> 24) & 0xFF));
}

// Build a compiled Text_C_Personen.res with `count` strings starting at TextDb id
// `baseIndex`. String at file index i is "N<baseIndex+i>" (so the absolute id is
// embedded in the text — makes the slice mapping checkable). Matches the byte
// layout BuildTextArray @0x44bb5c reads (see gui/text_load.h).
std::vector<u8> MakeRes(std::uint32_t baseIndex, std::uint32_t count) {
    std::vector<u8> b;
    PutU32(b, count);                 // +0  entryCount
    PutU32(b, baseIndex);             // +4  baseIndex
    PutU32(b, baseIndex + count - 1); // +8  lastIndex

    // Build the blob first to know the offsets.
    std::vector<u8> blob;
    std::vector<std::uint32_t> offs;
    for (std::uint32_t i = 0; i < count; ++i) {
        offs.push_back(static_cast<std::uint32_t>(blob.size()));
        std::string s = "N" + std::to_string(baseIndex + i);
        blob.insert(blob.end(), s.begin(), s.end());
        blob.push_back(0);
    }
    for (std::uint32_t i = 0; i < count; ++i) PutU32(b, offs[i]);   // offset[]
    // name[count][80] — give each a key "K<id>" NUL-padded to 80.
    for (std::uint32_t i = 0; i < count; ++i) {
        std::string k = "K" + std::to_string(baseIndex + i);
        for (int c = 0; c < 80; ++c)
            b.push_back(c < static_cast<int>(k.size()) ? static_cast<u8>(k[c]) : 0);
    }
    for (std::uint32_t i = 0; i < count; ++i) b.push_back(0xFF);    // tag[] = plain
    PutU32(b, static_cast<std::uint32_t>(blob.size()));             // blobSize
    b.insert(b.end(), blob.begin(), blob.end());                    // blob
    return b;
}

} // namespace

// (a) The slice anchors recovered from the binary's literal table addresses
//     relative to the canonical text base dword_8C36B0 (0x8C36B0).
TEST(NameTables, SliceAnchorsMatchBinaryAddresses) {
    // dword_8C400C = &dword_8C36B0[599]  -> (0x8C400C - 0x8C36B0)/4 == 599
    CHECK_EQ((0x8C400C - 0x8C36B0) / 4, kNameMaleBase);
    CHECK_EQ((0x8C4320 - 0x8C36B0) / 4, kNameFemaleBase);
    CHECK_EQ((0x8C4508 - 0x8C36B0) / 4, kNameDynastyBase);
    // Draw widths the call sites use: RandomModulo(0xBF/0x70/0x95).
    CHECK_EQ(kNameMaleCount, 0xBF);
    CHECK_EQ(kNameFemaleCount, 0x70);
    CHECK_EQ(kNameDynastyCount, 0x95);
}

// (b) Parse a synthetic Personen.res that spans the name ranges and read slices.
//     baseIndex 272 == the real deutsch Text_C_Personen.res value; cover through
//     the dynasty range end (918+149 = 1067).
TEST(NameTables, LoadAndSliceFromSyntheticRes) {
    NameTables_Reset();
    CHECK(!NameTables_Loaded());
    // before load every read is the inert "".
    CHECK_EQ(std::string(MaleNameAt(0)), std::string(""));

    std::vector<u8> res = MakeRes(/*baseIndex=*/272, /*count=*/806); // 272..1077
    CHECK(NameTables_LoadFromResBuffer(res.data(), res.size()));
    CHECK(NameTables_Loaded());

    // Male slice id 599 == file string "N599"; index 0 and 190 (last).
    CHECK_EQ(std::string(MaleNameAt(0)),   std::string("N599"));
    CHECK_EQ(std::string(MaleNameAt(190)), std::string("N789")); // 599+190
    // Female slice id 796.
    CHECK_EQ(std::string(FemaleNameAt(0)),   std::string("N796"));
    CHECK_EQ(std::string(FemaleNameAt(111)), std::string("N907")); // 796+111
    // Dynasty slice id 918.
    CHECK_EQ(std::string(DynastyNameAt(0)),   std::string("N918"));
    CHECK_EQ(std::string(DynastyNameAt(148)), std::string("N1066")); // 918+148

    // NameAt with the kind enum agrees with the wrappers.
    CHECK_EQ(std::string(NameAt(kNameMale, 5)),    std::string(MaleNameAt(5)));
    CHECK_EQ(std::string(NameAt(kNameFemale, 5)),  std::string(FemaleNameAt(5)));
    CHECK_EQ(std::string(NameAt(kNameDynasty, 5)), std::string(DynastyNameAt(5)));

    // The underlying TextDb is the global text array model; id 599 holds "N599".
    const auto* db = NameTables_Db();
    CHECK(db != nullptr);
    CHECK_EQ(std::string(db->Text(kNameMaleBase)), std::string("N599"));
}

// (c) Bounds + inert behavior: out-of-range index, bad kind, negative, and the
//     not-loaded fallback all return "" (never garbage).
TEST(NameTables, BoundsAndInertFallback) {
    NameTables_Reset();
    std::vector<u8> res = MakeRes(272, 806);
    CHECK(NameTables_LoadFromResBuffer(res.data(), res.size()));

    CHECK_EQ(std::string(MaleNameAt(kNameMaleCount)),     std::string("")); // == 191, OOR
    CHECK_EQ(std::string(FemaleNameAt(kNameFemaleCount)), std::string("")); // == 112, OOR
    CHECK_EQ(std::string(DynastyNameAt(kNameDynastyCount)),std::string("")); // == 149, OOR
    CHECK_EQ(std::string(NameAt(kNameMale, -1)),          std::string(""));
    CHECK_EQ(std::string(NameAt(99, 0)),                  std::string("")); // bad kind

    NameTables_Reset();
    CHECK(!NameTables_Loaded());
    CHECK_EQ(std::string(MaleNameAt(0)), std::string(""));
}

// (d) A too-short / wrong file that does not span the name ranges leaves the
//     tables un-loaded (the slices stay inert "") rather than reading short.
TEST(NameTables, ShortFileDoesNotLoad) {
    NameTables_Reset();
    // A file whose entries stop well before the male range (599) — e.g. baseIndex
    // 10, count 5 covers ids 10..14 only.
    std::vector<u8> res = MakeRes(10, 5);
    CHECK(!NameTables_LoadFromResBuffer(res.data(), res.size()));
    CHECK(!NameTables_Loaded());
    CHECK_EQ(std::string(MaleNameAt(0)), std::string(""));

    // A malformed (truncated) buffer also fails cleanly.
    CHECK(!NameTables_LoadFromResBuffer(res.data(), 8));
    CHECK(!NameTables_Loaded());
}

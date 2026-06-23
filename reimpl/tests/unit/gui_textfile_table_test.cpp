// Unit tests for the gui text-DB slot/label/tokenizer cluster:
//   VIBE_Text_StrtokWhitespace @0x5e9cd0  (+ SetBitmapBits @0x5fe5a0)
//   VIBE_Text_FindTextFileSlot @0x44d8f8
//   VIBE_Text_FreeTextFile     @0x44d940
//   VIBE_Text_FreeAllTextFiles @0x44d8a4
//   VIBE_Text_LookupLabelEntry @0x44add4
//
// Determinism: the tokenizer is bit-exact strtok, checked against libc strtok as
// the oracle on golden inputs; the slot/label scans are checked against hand-built
// golden tables.
#include "test.h"

#include "gui/text/text_strtok.h"
#include "gui/text/textfile_table.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild::gui::text;

namespace {
// Run the reconstructed tokenizer over a copy of `s`, collecting tokens.
std::vector<std::string> Tok(const std::string& s, const char* delims) {
    std::vector<char> buf(s.begin(), s.end());
    buf.push_back('\0');
    std::vector<std::string> out;
    StrtokContext ctx;
    char* t = StrtokWhitespace(ctx, buf.data(), delims);
    while (t) {
        out.emplace_back(t);
        t = StrtokWhitespace(ctx, nullptr, delims);
    }
    return out;
}

// Oracle: libc strtok over an independent copy.
std::vector<std::string> TokOracle(const std::string& s, const char* delims) {
    std::vector<char> buf(s.begin(), s.end());
    buf.push_back('\0');
    std::vector<std::string> out;
    char* t = std::strtok(buf.data(), delims);
    while (t) {
        out.emplace_back(t);
        t = std::strtok(nullptr, delims);
    }
    return out;
}
} // namespace

TEST(GuiTextFileTable, SetBitmapBitsMembership) {
    guild::u8 bm[32];
    SetBitmapBits(bm, " \t");
    // bit for ' ' (0x20): byte 4, bit 0; for '\t' (0x09): byte 1, bit 1.
    auto has = [&](unsigned char c) {
        static const guild::u8 mask[8] = {1, 2, 4, 8, 16, 32, 64, 128};
        return (bm[c >> 3] & mask[c & 7]) != 0;
    };
    CHECK(has(' '));
    CHECK(has('\t'));
    CHECK(!has('x'));
    CHECK(!has('\0'));  // NUL is never in the set
}

TEST(GuiTextFileTable, StrtokMatchesLibcGoldenVectors) {
    struct Case { const char* s; const char* d; };
    const Case cases[] = {
        {"  hello world  foo", " "},
        {"a,b,,c", ","},
        {"\none\ntwo\n", "\n"},
        {"nodelims", " "},
        {"   ", " "},
        {"ab\tcd ef", " \t"},
        {"", " "},
        {"|first|second||third|", "|"},
        {"   leading", " "},
        {"trailing   ", " "},
    };
    for (const Case& c : cases) {
        std::vector<std::string> got = Tok(c.s, c.d);
        std::vector<std::string> exp = TokOracle(c.s, c.d);
        CHECK_EQ(got.size(), exp.size());
        for (std::size_t i = 0; i < got.size() && i < exp.size(); ++i)
            CHECK(got[i] == exp[i]);
    }
}

TEST(GuiTextFileTable, StrtokNullInputNoContext) {
    // First call with null str and a fresh context returns null (no saved ptr).
    StrtokContext ctx;
    CHECK(StrtokWhitespace(ctx, nullptr, " ") == nullptr);
}

TEST(GuiTextFileTable, StrtokInPlaceNulTermination) {
    char buf[] = "key value";
    StrtokContext ctx;
    char* a = StrtokWhitespace(ctx, buf, " ");
    char* b = StrtokWhitespace(ctx, nullptr, " ");
    CHECK(a && std::strcmp(a, "key") == 0);
    CHECK(b && std::strcmp(b, "value") == 0);
    // The delimiter was overwritten with a NUL in place.
    CHECK(buf[3] == '\0');
}

TEST(GuiTextFileTable, FindSlotCaseInsensitiveAndLive) {
    TextFileTable t;
    CHECK_EQ(t.FindSlot("foo"), -1);  // empty table

    int s0 = t.Acquire("Foo");
    int s1 = t.Acquire("BarFile");
    CHECK_EQ(s0, 0);
    CHECK_EQ(s1, 1);

    // case-insensitive name match
    CHECK_EQ(t.FindSlot("foo"), 0);
    CHECK_EQ(t.FindSlot("FOO"), 0);
    CHECK_EQ(t.FindSlot("barfile"), 1);
    CHECK_EQ(t.FindSlot("missing"), -1);
}

TEST(GuiTextFileTable, FreeTextFileClearsBlobKeepsRecord) {
    TextFileTable t;
    int s = t.Acquire("data");
    t.At(s).blob = {1, 2, 3, 4};

    CHECK_EQ(t.FreeTextFile("DATA"), s);   // case-insensitive
    CHECK(t.At(s).blob.empty());
    CHECK(t.At(s).used);                    // record (name) stays live

    // Freeing an unknown name returns -1 (the FindTextFileSlot miss).
    CHECK_EQ(t.FreeTextFile("nope"), -1);
}

TEST(GuiTextFileTable, FreeAllClearsEveryRecord) {
    TextFileTable t;
    t.Acquire("a");
    t.Acquire("b");
    t.At(0).blob = {9, 9};
    t.At(1).blob = {7};

    t.FreeAllTextFiles();
    for (int i = 0; i < t.Count(); ++i) {
        CHECK(!t.At(i).used);
        CHECK(t.At(i).blob.empty());
        CHECK(t.At(i).name.empty());
    }
    CHECK_EQ(t.FindSlot("a"), -1);
    CHECK_EQ(t.FindSlot("b"), -1);
}

// DISASM @0x44d8a4: FreeAllTextFiles keys SOLELY on the blob pointer
// (dword_77BF18[i] != 0). A live slot that owns no blob must be left untouched —
// its name/indices survive. Earlier model wrongly also zeroed used-but-blobless
// slots; this golden pins the binary-faithful behavior.
TEST(GuiTextFileTable, FreeAllKeepsBloblessNamedSlots) {
    TextFileTable t;
    int s0 = t.Acquire("withblob");
    int s1 = t.Acquire("noblob");
    t.At(s0).blob = {1, 2, 3};
    // s1 has a name (live) but no blob.

    t.FreeAllTextFiles();

    // Slot with a blob: freed and fully zeroed.
    CHECK(t.At(s0).blob.empty());
    CHECK(t.At(s0).name.empty());
    CHECK(!t.At(s0).used);
    CHECK_EQ(t.FindSlot("withblob"), -1);

    // Slot with no blob: untouched, still findable.
    CHECK(t.At(s1).used);
    CHECK(t.At(s1).name == "noblob");
    CHECK_EQ(t.FindSlot("noblob"), s1);
}

TEST(GuiTextFileTable, SlotCapAt128) {
    TextFileTable t;
    CHECK_EQ(t.Count(), kTextFileSlots);  // 128
    for (int i = 0; i < kTextFileSlots; ++i) {
        std::string nm = "f" + std::to_string(i);
        CHECK_EQ(t.Acquire(nm.c_str()), i);
    }
    // Table full: a new name has no free slot.
    CHECK_EQ(t.Acquire("overflow"), -1);
}

TEST(GuiTextFileTable, LabelLookupCaseInsensitive) {
    LabelTable lt;
    lt.Add("_OB_NULL", 0);
    lt.Add("_NEV_DANKE+0", 6063);
    lt.Add("_GB_HOUSE+2", 42);

    CHECK_EQ(lt.Lookup("_OB_NULL"), 0);
    CHECK_EQ(lt.Lookup("_nev_danke+0"), 6063);  // case-folded
    CHECK_EQ(lt.Lookup("_GB_HOUSE+2"), 42);
    CHECK_EQ(lt.Lookup("_NOT_THERE"), -1);
}

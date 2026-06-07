// Unit tests for the BY-NAME asset loaders:
//   gui/form_asset  — Gfx_BuildPath / Form_LoadByName path build (VIBE_Gui_LoadGfxFile)
//   gui/text_load   — BuildTextArray (.res binary -> TextDb) + Text_BuildPath
//                     (VIBE_Text_LoadTextFile / VIBE_Text_BuildTextArray @0x44bb5c)
//
// The .gfx parse itself is covered by gui_form_loader_test; here we focus on the
// new path-building edges and the recovered .res text-DB format (byte-for-byte).
#include "tests/framework/test.h"

#include "gui/form_asset.h"
#include "gui/form_loader.h"
#include "gui/text_load.h"
#include "gui/text/textdb.h"
#include "gui/form.h"
#include "gui/object.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::gui;
using guild::gui::text::TextDb;
using guild::gui::text::BuildTextArray;
using guild::gui::text::Text_BuildPath;

// Renderer/property edge stubs (the loader TU's weak symbols + markup edges).
namespace guild::gui {
void RegisterGfxState(int) {}
} // namespace guild::gui

namespace {

// ---- Synthetic .res builder (mirrors the writer at the tail of BuildTextArray) ----
// Layout: u32 entryCount, u32 baseIndex, u32 lastIndex,
//         u32 offset[count], u8 name[count][80], u8 tag[count],
//         u32 blobSize, u8 blob[blobSize].
struct ResEntry { std::string text; std::string name; u8 tag; };

void PushU32(std::vector<u8>& b, u32 v) {
    b.push_back(static_cast<u8>(v & 0xFF));
    b.push_back(static_cast<u8>((v >> 8) & 0xFF));
    b.push_back(static_cast<u8>((v >> 16) & 0xFF));
    b.push_back(static_cast<u8>((v >> 24) & 0xFF));
}

std::vector<u8> MakeResFile(u32 baseIndex, const std::vector<ResEntry>& entries) {
    std::vector<u8> b;
    u32 count = static_cast<u32>(entries.size());
    PushU32(b, count);
    PushU32(b, baseIndex);
    PushU32(b, baseIndex + count - 1); // lastIndex

    // Build the blob + offset table (each string NUL-terminated, packed).
    std::vector<u8> blob;
    std::vector<u32> offsets;
    for (const auto& e : entries) {
        offsets.push_back(static_cast<u32>(blob.size()));
        blob.insert(blob.end(), e.text.begin(), e.text.end());
        blob.push_back(0);
    }
    for (u32 o : offsets) PushU32(b, o);

    // Names: 80-byte NUL-padded records.
    for (const auto& e : entries) {
        u8 field[80] = {0};
        std::memcpy(field, e.name.data(), e.name.size() < 80 ? e.name.size() : 80);
        b.insert(b.end(), field, field + 80);
    }
    // Tags.
    for (const auto& e : entries) b.push_back(e.tag);

    PushU32(b, static_cast<u32>(blob.size()));
    b.insert(b.end(), blob.begin(), blob.end());
    return b;
}

} // namespace

// ===== form_asset path build =========================================================

TEST(GuiFormAsset, GfxPathBuild) {
    // VIBE_Gui_LoadGfxFile: "%sgfx\\%s" (prefix, name).
    CHECK(Gfx_BuildPath("", "gilde.gfx") == std::string("gfx\\gilde.gfx"));
    CHECK(Gfx_BuildPath("data\\", "gilde.gfx") == std::string("data\\gfx\\gilde.gfx"));
}

// ===== text_load path build ==========================================================

TEST(GuiTextLoad, ResPathBuild) {
    // VIBE_Text_LoadTextFile: "textbin_%s\\%s.res" (lang, name).
    CHECK(Text_BuildPath("german", "main") == std::string("textbin_german\\main.res"));
    CHECK(Text_BuildPath(nullptr, "x") == std::string("textbin_german\\x.res"));
}

// ===== text_load BuildTextArray: the recovered .res format ===========================

TEST(GuiTextLoad, ParsesResIntoTextDbBaseZero) {
    std::vector<ResEntry> entries = {
        { "Yes",    "OK_LABEL",     0xFF },
        { "No",     "CANCEL_LABEL", 0xFF },
        { "Loading","STATUS_LOAD",  0x09 }, // a {r1}-style tag
    };
    auto buf = MakeResFile(/*baseIndex=*/0, entries);

    TextDb db;
    auto res = BuildTextArray(buf.data(), buf.size(), db);
    CHECK(res.ok);
    CHECK_EQ(res.entryCount, 3);
    CHECK_EQ(static_cast<int>(res.baseIndex), 0);
    CHECK_EQ(static_cast<int>(res.lastIndex), 2);
    CHECK_EQ(db.Count(), 3);

    // Lookup by id.
    CHECK(std::strcmp(db.Text(0), "Yes") == 0);
    CHECK(std::strcmp(db.Text(1), "No") == 0);
    CHECK(std::strcmp(db.Text(2), "Loading") == 0);
    // Names + tags.
    CHECK(std::strcmp(db.Name(0), "OK_LABEL") == 0);
    CHECK_EQ(static_cast<int>(db.Tag(2)), 0x09);

    // Lookup by name (case-insensitive FindIndex).
    CHECK_EQ(db.FindIndex("ok_label"), 0);
    CHECK_EQ(db.FindIndex("CANCEL_LABEL"), 1);
    CHECK_EQ(db.FindIndex("STATUS_LOAD"), 2);
    CHECK_EQ(db.FindIndex("does_not_exist"), -1);
}

TEST(GuiTextLoad, HonorsBaseIndexPlacement) {
    // A file whose strings start at global index 5: indices 0..4 become empty
    // placeholders, the real entries land at 5,6 (dword_8C36B0[baseIndex+i]).
    std::vector<ResEntry> entries = {
        { "Alpha", "FIRST",  0xFF },
        { "Beta",  "SECOND", 0xFF },
    };
    auto buf = MakeResFile(/*baseIndex=*/5, entries);

    TextDb db;
    auto res = BuildTextArray(buf.data(), buf.size(), db);
    CHECK(res.ok);
    CHECK_EQ(static_cast<int>(res.baseIndex), 5);
    CHECK_EQ(static_cast<int>(res.lastIndex), 6);
    CHECK_EQ(db.Count(), 7);
    CHECK(std::strcmp(db.Text(5), "Alpha") == 0);
    CHECK(std::strcmp(db.Text(6), "Beta") == 0);
    // The gap entries are empty.
    CHECK(std::strcmp(db.Text(0), "") == 0);
    CHECK_EQ(db.FindIndex("SECOND"), 6);
}

TEST(GuiTextLoad, AppendsAfterAnExistingFile) {
    // Two .res files loaded into one DB end-to-end: file A at base 0, file B at
    // base 2 (== A's entry count) -> contiguous global array.
    auto a = MakeResFile(0, { { "One", "A1", 0xFF }, { "Two", "A2", 0xFF } });
    auto b = MakeResFile(2, { { "Three", "B1", 0xFF } });

    TextDb db;
    CHECK(BuildTextArray(a.data(), a.size(), db).ok);
    CHECK(BuildTextArray(b.data(), b.size(), db).ok);
    CHECK_EQ(db.Count(), 3);
    CHECK(std::strcmp(db.Text(2), "Three") == 0);
    CHECK_EQ(db.FindIndex("B1"), 2);
}

TEST(GuiTextLoad, RejectsShortBuffer) {
    TextDb db;
    u8 tiny[8] = {0};
    CHECK(!BuildTextArray(tiny, sizeof(tiny), db).ok);
    CHECK_EQ(db.Count(), 0);

    // Header claims 3 entries but the body is missing.
    std::vector<u8> hdr;
    PushU32(hdr, 3); PushU32(hdr, 0); PushU32(hdr, 2);
    CHECK(!BuildTextArray(hdr.data(), hdr.size(), db).ok);
    CHECK_EQ(db.Count(), 0);
}

TEST(GuiTextLoad, EmptyFileParsesToZeroEntries) {
    auto buf = MakeResFile(0, {});
    TextDb db;
    auto res = BuildTextArray(buf.data(), buf.size(), db);
    CHECK(res.ok);
    CHECK_EQ(res.entryCount, 0);
    CHECK_EQ(db.Count(), 0);
}

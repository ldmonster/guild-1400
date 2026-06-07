// Unit: drive the text-DB driver's parse + resolve logic over SYNTHETIC in-format
// ".res" bytes (no real assets). Builds a compiled .res blob by hand in the
// recovered layout, feeds it to the reconstructed BuildTextArray, then exercises
// the driver's ResolveStringKeys read-side helper with deterministic golden
// assertions.
#include "tests/framework/test.h"

#include "app/real_text_driver.h"
#include "gui/text/textdb.h"
#include "gui/text_load.h"

#include "guild/common/types.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using guild::gui::text::TextDb;
using guild::gui::text::kResNameStride;

namespace {

void PutU32(std::vector<u8>& b, u32 v) {
    b.push_back(static_cast<u8>(v & 0xFF));
    b.push_back(static_cast<u8>((v >> 8) & 0xFF));
    b.push_back(static_cast<u8>((v >> 16) & 0xFF));
    b.push_back(static_cast<u8>((v >> 24) & 0xFF));
}

struct SynthEntry {
    std::string name;
    std::string text;
    u8 tag;
};

// Build a compiled ".res" buffer matching the format BuildTextArray reads:
//   u32 entryCount, u32 baseIndex, u32 lastIndex,
//   u32 offset[entryCount], u8 name[entryCount][80], u8 tag[entryCount],
//   u32 blobSize, u8 blob[blobSize]  (NUL-terminated strings packed back to back)
std::vector<u8> BuildRes(u32 baseIndex, const std::vector<SynthEntry>& es) {
    std::vector<u8> b;
    u32 ec = static_cast<u32>(es.size());
    PutU32(b, ec);
    PutU32(b, baseIndex);
    PutU32(b, baseIndex + ec - 1); // lastIndex

    // Pack the blob first so we know per-entry offsets.
    std::vector<u8> blob;
    std::vector<u32> offs;
    for (const auto& e : es) {
        offs.push_back(static_cast<u32>(blob.size()));
        blob.insert(blob.end(), e.text.begin(), e.text.end());
        blob.push_back(0); // NUL terminator
    }

    for (u32 o : offs)
        PutU32(b, o);
    for (const auto& e : es) {
        u8 field[kResNameStride] = {0};
        std::size_t n = e.name.size() < (kResNameStride - 1u)
                            ? e.name.size()
                            : (kResNameStride - 1u);
        std::memcpy(field, e.name.data(), n);
        b.insert(b.end(), field, field + kResNameStride);
    }
    for (const auto& e : es)
        b.push_back(e.tag);

    PutU32(b, static_cast<u32>(blob.size()));
    b.insert(b.end(), blob.begin(), blob.end());
    return b;
}

} // namespace

TEST(RealTextDriverUnit, BuildTextArrayParsesSyntheticRes) {
    std::vector<SynthEntry> es = {
        {"_NEV_DORTHIN+0", "Dorthin", 0xFF},
        {"_NEV_DANKE+0",   "Danke",   0xFF},
        {"_NEV_WEITER+0",  "Weiter",  0xFF},
    };
    std::vector<u8> res = BuildRes(/*baseIndex=*/100, es);

    TextDb db;
    auto r = guild::gui::text::BuildTextArray(res.data(), res.size(), db);
    CHECK(r.ok);
    CHECK_EQ(r.entryCount, 3);
    CHECK_EQ(r.baseIndex, 100u);
    CHECK_EQ(r.lastIndex, 102u);

    // Entries land at [baseIndex, lastIndex]; gap before baseIndex is placeholders.
    CHECK_EQ(db.Count(), 103);
    CHECK_EQ(db.FindIndex("_NEV_DORTHIN+0"), 100);
    if (db.Text(100)) CHECK(std::strcmp(db.Text(100), "Dorthin") == 0);
    CHECK_EQ(db.Tag(100), 0xFF);
    // Case-folded lookup (mirrors VIBE_Text_FindTextArrayIndex).
    CHECK_EQ(db.FindIndex("_nev_danke+0"), 101);
}

TEST(RealTextDriverUnit, ResolveStringKeysGoldenResults) {
    std::vector<SynthEntry> es = {
        {"K_HELLO", "Hallo", 0xFF},
        {"K_BYE",   "Tschuess", 0xFF},
        {"K_EMPTY", "", 0xFF},          // present key, empty text
    };
    std::vector<u8> res = BuildRes(/*baseIndex=*/0, es);

    TextDb db;
    CHECK(guild::gui::text::BuildTextArray(res.data(), res.size(), db).ok);

    std::vector<std::string> keys = {"K_HELLO", "k_bye", "K_MISSING", "K_EMPTY"};
    auto resolved = guild::app::ResolveStringKeys(db, keys);
    CHECK_EQ(resolved.size(), 4u);

    // K_HELLO -> found, "Hallo"
    CHECK(resolved[0].found);
    CHECK_EQ(resolved[0].index, 0);
    CHECK(resolved[0].text == "Hallo");
    CHECK_EQ(resolved[0].tag, 0xFF);

    // k_bye -> found via case-folded lookup
    CHECK(resolved[1].found);
    CHECK_EQ(resolved[1].index, 1);
    CHECK(resolved[1].text == "Tschuess");

    // K_MISSING -> not found
    CHECK(!resolved[2].found);
    CHECK_EQ(resolved[2].index, -1);
    CHECK(resolved[2].text.empty());

    // K_EMPTY -> found but empty text (still resolvable by name)
    CHECK(resolved[3].found);
    CHECK_EQ(resolved[3].index, 2);
    CHECK(resolved[3].text.empty());
}

TEST(RealTextDriverUnit, ShortBufferFailsCleanly) {
    // A truncated buffer must leave the db untouched and report ok=false.
    std::vector<u8> bogus = {1, 0, 0, 0, 0, 0}; // < 12 bytes
    TextDb db;
    auto r = guild::gui::text::BuildTextArray(bogus.data(), bogus.size(), db);
    CHECK(!r.ok);
    CHECK_EQ(db.Count(), 0);

    // Empty key set resolves to empty result.
    auto resolved = guild::app::ResolveStringKeys(db, {});
    CHECK(resolved.empty());
}

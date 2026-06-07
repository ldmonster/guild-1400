// Real-asset e2e: load the REAL shipped `Resources/textbin_deutsch.BIN` (a PKZIP
// archive of ~101 compiled `.res` localized-text files), extract a `.res` member with
// the reconstructed ZipArchive, and feed it to the recovered text parser
// (VIBE_Text_BuildTextArray @0x44bb5c, gui/text_load) — verifying the TextDb populates
// with sensible names/strings.
//
// GUARDED: if the asset folder isn't present the test passes trivially.
#include "tests/framework/test.h"

#include "gui/text_load.h"
#include "gui/text/textdb.h"

#include "io/zip_archive.h"
#include "shim_impl/disk_filesystem.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using guild::gui::text::TextDb;
using guild::gui::text::TextResFile;

static const char* kRoot =
    "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";

static bool assetsPresent() {
    guild::shim::DiskFileSystem fs(kRoot);
    return fs.exists("Resources/forms.BIN");
}

TEST(RealAssetsText, BuildTextArrayFromRealResMember) {
    if (!assetsPresent()) { CHECK(true); return; } // skipped: no assets

    guild::shim::DiskFileSystem fs(kRoot);

    io::ZipArchive z;
    CHECK(z.Open(&fs, "Resources/textbin_deutsch.BIN"));
    // 108 members total, 101 of them .res (the rest are CVS bookkeeping dirs).
    CHECK_EQ(z.numberEntry(), 108u);

    // Extract one compiled .res member: Text_N_Nachrichten.res — 182 entries with a
    // declared base index of 6062 (the global text-array slot of its first string).
    std::vector<u8> res;
    CHECK(z.ExtractByName("Text_N_Nachrichten.res", res, false));
    CHECK(res.size() > 30000u); // the real packed .res blob

    // The recovered .res header: u32 entryCount @+0, u32 baseIndex @+4, u32 lastIndex @+8.
    u32 entryCount = 0, baseIndex = 0, lastIndex = 0;
    std::memcpy(&entryCount, res.data() + 0, 4);
    std::memcpy(&baseIndex,  res.data() + 4, 4);
    std::memcpy(&lastIndex,  res.data() + 8, 4);
    CHECK_EQ(entryCount, 182u);
    CHECK_EQ(baseIndex, 6062u);
    CHECK_EQ(lastIndex, 6243u);              // baseIndex + entryCount - 1

    // ---- Drive the reconstructed parser on the REAL bytes. ----
    TextDb db;
    TextResFile r = guild::gui::text::BuildTextArray(res.data(), res.size(), db);
    CHECK(r.ok);
    CHECK_EQ(r.entryCount, 182);
    CHECK_EQ(r.baseIndex, 6062u);
    CHECK_EQ(r.lastIndex, 6243u);

    // BuildTextArray fills [0, baseIndex) with empty placeholders, then the 182 real
    // entries at [baseIndex, lastIndex], so the DB count is lastIndex + 1.
    CHECK_EQ(db.Count(), 6244);

    // ---- Spot-check the populated entries (names are clean ASCII keys). ----
    // The first entries are the "Nachrichten" (message) keys, looked up by name.
    int iDorthin = db.FindIndex("_NEV_DORTHIN+0");
    CHECK_EQ(iDorthin, 6062);               // first entry lands at baseIndex
    CHECK(std::strcmp(db.Name(6062), "_NEV_DORTHIN+0") == 0);
    CHECK(std::strcmp(db.Name(6063), "_NEV_DANKE+0") == 0);
    CHECK(std::strcmp(db.Name(6064), "_NEV_WEITER+0") == 0);

    // Case-insensitive name lookup works (FindTextArrayIndex is case-folded).
    CHECK_EQ(db.FindIndex("_nev_danke+0"), 6063);

    // The strings are non-empty and the tag bytes are the plain-string marker (0xFF)
    // for these entries.
    CHECK(db.Text(6062) != nullptr);
    CHECK(std::strlen(db.Text(6062)) > 0);
    CHECK_EQ(db.Tag(6062), 0xFF);
    CHECK_EQ(db.Tag(6063), 0xFF);

    // The placeholder gap before baseIndex is empty (its index slot exists but holds
    // an empty string / empty name).
    CHECK_EQ(std::strlen(db.Name(0)), 0u);
    CHECK_EQ(std::strlen(db.Text(0)), 0u);
}

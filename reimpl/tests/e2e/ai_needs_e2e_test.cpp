// tests/e2e/ai_needs_e2e_test.cpp — GUARDED real-asset e2e for the AI needs/score
// catalog (gilde.exe 0x4764e8 VIBE_AiNeeds_BuildScoreTable + the
// 0x468a40 VIBE_AiMethod_LoadDataFile read path).
//
// Loads the REAL shipped Resources/gamedata/ai/AI_DATA.DFN (a gzip stream of
// 61 x 73-byte records) through the reconstructed gzip framing, overlays it onto a
// freshly-built catalog, and asserts the records match the binary: the method names
// line up with BuildScoreTable's fixed-field definition table, the attribute
// indices are valid (-1 or 0..13), and the well-known "Bildung" desire weights
// match the on-disk values.
//
// GUARDED: the real game dir is not in the repo. Absent -> ZERO checks (clean skip).
// Override with GUILD_GAME_DIR.
#include "tests/framework/test.h"

#include "sim/ai_needs.h"
#include "compress/gzip.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {
std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/guild-1400/reimpl/europe_guild_1400_original";
}
const char* kDfnPath = "Resources/gamedata/ai/AI_DATA.DFN";
} // namespace

TEST(AiNeedsE2E, OverlayRealDfnMatchesBinaryTable) {
    const std::string dir = GameDir();
    shim::DiskFileSystem fs(dir);
    if (!fs.exists(kDfnPath)) {
        std::printf("  [skip] AiNeedsE2E.OverlayRealDfnMatchesBinaryTable: "
                    "AI_DATA.DFN absent under %s\n", dir.c_str());
        return; // clean skip
    }

    // Read + gunzip the shipped catalog.
    shim::IFile* f = fs.open(kDfnPath, "rb");
    CHECK(f != nullptr);
    if (!f) return;
    std::vector<u8> gz((std::size_t)f->size());
    f->read(gz.data(), gz.size());
    fs.close(f);

    std::vector<u8> dfn;
    CHECK(compress::Gunzip(gz.data(), gz.size(), dfn));      // reconstructed gzip
    CHECK_EQ((int)dfn.size(), kAiNeedsDfnTotalBytes);        // 61 * 73 == 4453

    // Build the fixed-field catalog (no-INI shipped path), then overlay the DFN.
    AiNeedsCatalog cat;
    CHECK_EQ(AiNeeds_BuildScoreTable(cat), 1);
    int n = AiNeeds_OverlayFromDfn(dfn.data(), dfn.size(), cat);
    CHECK_EQ(n, kAiNeedsRecordCount);                        // 61 records overlaid

    // Cross-check: every defined method's on-disk name (at slot id) equals the
    // builder's definition-table name. (The DFN was written by the same builder, so
    // names + ids must agree — this proves loader<->builder layout fidelity.)
    for (const auto& d : kAiNeedsMethodDefs) {
        const AiNeedsCatalogEntry& e = cat[d.id];
        CHECK_EQ((int)e.id, (int)d.id);
        CHECK(std::strcmp(e.name, d.name) == 0);
        // Fixed fn-ptr fields preserved through the overlay (DFN holds no fn ptrs).
        CHECK_EQ(e.scorer, d.scorer);
        // Desire-slot attribute indices are all valid catalog ordinals or "unused".
        for (int s = 0; s < kAiMethodDesireSlots; ++s) {
            i8 sa = e.shortSlots[s].attrIndex;
            i8 la = e.longSlots[s].attrIndex;
            CHECK(sa == -1 || (sa >= 0 && sa <= 13));
            CHECK(la == -1 || (la >= 0 && la <= 13));
            // prev vector mirrors short.
            CHECK_EQ((int)e.prevSlots[s].attrIndex, (int)e.shortSlots[s].attrIndex);
            CHECK_EQ(e.prevSlots[s].change, e.shortSlots[s].change);
        }
    }

    // Golden values from the shipped AI_DATA.DFN (verified via Python gunzip):
    //   id 1 "DummyUnversehrtheit": short[0]={UNVERSEHRTHEIT(1),+10}, short[1]={GELD(3),-10}
    const AiNeedsCatalogEntry& du = cat[1];
    CHECK(std::strcmp(du.name, "DummyUnversehrtheit") == 0);
    CHECK_EQ((int)du.shortSlots[0].attrIndex, 1);
    CHECK_EQ(du.shortSlots[0].change, 10.0f);
    CHECK_EQ((int)du.shortSlots[1].attrIndex, 3);
    CHECK_EQ(du.shortSlots[1].change, -10.0f);

    //   id 4 "Bildung": short = {BERUF(4,+20),APS(0,-10),BILDUNG(8,+40),SICHERHEIT(11,+15)}
    const AiNeedsCatalogEntry& bi = cat[4];
    CHECK(std::strcmp(bi.name, "Bildung") == 0);
    CHECK_EQ((int)bi.shortSlots[0].attrIndex, 4);
    CHECK_EQ(bi.shortSlots[0].change, 20.0f);
    CHECK_EQ((int)bi.shortSlots[1].attrIndex, 0);
    CHECK_EQ(bi.shortSlots[1].change, -10.0f);
    CHECK_EQ((int)bi.shortSlots[2].attrIndex, 8);
    CHECK_EQ(bi.shortSlots[2].change, 40.0f);
    CHECK_EQ((int)bi.shortSlots[3].attrIndex, 11);
    CHECK_EQ(bi.shortSlots[3].change, 15.0f);
}

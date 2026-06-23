#include "test.h"

// Unit tests — render/texture_set_table (.TXS texture-set sidecar parser, the
// VIBE_Object_SelectTextureSet @0x5b3f54 texture-swap-leaf data) + the
// 0x506388 foliage gate + the 0x58339c season->set rule + the 24-bit material
// stand-in switch (render/texture.h, named gap @0x5da34c).
//
// The golden vector mirrors the shipped layout byte-for-byte (e.g.
// Vegetation/BUCHE/pfl_R_BUCHE_KRONE_01.TXS: magic 0x23F209AE, setCount=4,
// namesPerSet=3, set-major NUL-terminated names, sets ordered _F/_S/_H/_W).
#include "render/texture.h"
#include "render/texture_set_table.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild;

namespace {

void PutU32(std::vector<u8>& v, u32 x) {
    v.push_back((u8)(x & 0xFF));
    v.push_back((u8)((x >> 8) & 0xFF));
    v.push_back((u8)((x >> 16) & 0xFF));
    v.push_back((u8)((x >> 24) & 0xFF));
}
void PutStr(std::vector<u8>& v, const char* s) {
    while (*s) v.push_back((u8)*s++);
    v.push_back(0);
}

// A 4-set / 2-material table shaped exactly like the shipped seasonal tables.
std::vector<u8> GoldenTxs() {
    std::vector<u8> v;
    PutU32(v, render::kTextureSetMagic);
    PutU32(v, 4);   // setCount
    PutU32(v, 2);   // namesPerSet (== bgf materialCount)
    PutStr(v, "vg_nm_Laub_X_s01_F");   // set 0 (Frühling)
    PutStr(v, "vg_nm_Laub_X_t01_F");
    PutStr(v, "VG_NM_LAUB_X_S01_S");   // set 1 (Sommer)
    PutStr(v, "VG_NM_LAUB_X_T01_S");
    PutStr(v, "VG_NM_LAUB_X_S01_H");   // set 2 (Herbst)
    PutStr(v, "");                     // empty row = "no swap" (8647 shipped rows)
    PutStr(v, "VG_NM_LAUB_X_S01_W");   // set 3 (Winter)
    PutStr(v, "SNOW_256_NS");          // cross-base remap (winter snow rows)
    return v;
}

} // namespace

TEST(TextureSetTable, ParsesGoldenSetMajorLayout) {
    std::vector<u8> raw = GoldenTxs();
    render::TextureSetTable t;
    CHECK(render::ParseTextureSetTable(raw.data(), raw.size(), t));
    CHECK(t.ok);
    CHECK_EQ(t.setCount, 4);
    CHECK_EQ(t.namesPerSet, 2);
    CHECK_EQ((int)t.names.size(), 8);

    // Set-major addressing: NameFor(set, material).
    CHECK(t.NameFor(0, 0) && *t.NameFor(0, 0) == "vg_nm_Laub_X_s01_F");
    CHECK(t.NameFor(0, 1) && *t.NameFor(0, 1) == "vg_nm_Laub_X_t01_F");
    CHECK(t.NameFor(1, 0) && *t.NameFor(1, 0) == "VG_NM_LAUB_X_S01_S");
    CHECK(t.NameFor(2, 0) && *t.NameFor(2, 0) == "VG_NM_LAUB_X_S01_H");
    CHECK(t.NameFor(3, 0) && *t.NameFor(3, 0) == "VG_NM_LAUB_X_S01_W");
    CHECK(t.NameFor(3, 1) && *t.NameFor(3, 1) == "SNOW_256_NS");

    // Empty row -> null ("keep the current texture", the 0x5b3f54 not-found arm).
    CHECK(t.NameFor(2, 1) == nullptr);

    // Out-of-range set/material -> null (the 0x5b403b set range gate).
    CHECK(t.NameFor(4, 0) == nullptr);
    CHECK(t.NameFor(-1, 0) == nullptr);
    CHECK(t.NameFor(0, 2) == nullptr);
    CHECK(t.NameFor(0, -1) == nullptr);
}

TEST(TextureSetTable, RejectsMalformedBuffers) {
    render::TextureSetTable t;

    // Too short / null.
    CHECK(!render::ParseTextureSetTable(nullptr, 0, t));
    std::vector<u8> tiny(8, 0);
    CHECK(!render::ParseTextureSetTable(tiny.data(), tiny.size(), t));

    // Wrong magic.
    std::vector<u8> raw = GoldenTxs();
    raw[0] ^= 0xFF;
    CHECK(!render::ParseTextureSetTable(raw.data(), raw.size(), t));
    CHECK(!t.ok);

    // Truncated mid-name list.
    raw = GoldenTxs();
    raw.resize(raw.size() - 6);
    CHECK(!render::ParseTextureSetTable(raw.data(), raw.size(), t));

    // Implausible header (sets*per beyond the byte budget).
    std::vector<u8> huge;
    PutU32(huge, render::kTextureSetMagic);
    PutU32(huge, 0x10000);
    PutU32(huge, 0x10000);
    CHECK(!render::ParseTextureSetTable(huge.data(), huge.size(), t));

    // Zero counts.
    std::vector<u8> zero;
    PutU32(zero, render::kTextureSetMagic);
    PutU32(zero, 0);
    PutU32(zero, 3);
    CHECK(!render::ParseTextureSetTable(zero.data(), zero.size(), t));
}

// ---------------------------------------------------------------------------
// W10-TEX hardening — .TXS parse bounds: a header-only buffer (0 names), a
// buffer truncated at the exact 12-byte header end, a single-set/single-name
// table, names that consume the buffer to the LAST byte, and a NameFor() sweep
// over the whole index + out-of-range corners (ASAN bounds the names[] access).
// ---------------------------------------------------------------------------
TEST(TextureSetTable, ParseBoundsHardening) {
    render::TextureSetTable t;

    // Exactly 12 bytes (header, no name bytes) with sets*per == 1: the first
    // name read hits i>=size immediately -> truncated -> false, no OOB.
    {
        std::vector<u8> v;
        PutU32(v, render::kTextureSetMagic);
        PutU32(v, 1);   // setCount
        PutU32(v, 1);   // namesPerSet  (sets*per == 1 == size budget edge)
        CHECK_EQ((int)v.size(), 12);
        CHECK(!render::ParseTextureSetTable(v.data(), v.size(), t));
    }

    // size == 11 (one byte short of the header) -> rejected by the size<12 gate.
    {
        std::vector<u8> v(11, 0);
        CHECK(!render::ParseTextureSetTable(v.data(), v.size(), t));
    }

    // 1 set / 1 name, the name consumes the buffer to its last byte (the NUL is
    // the final byte). Must parse and leave 0 trailing bytes.
    {
        std::vector<u8> v;
        PutU32(v, render::kTextureSetMagic);
        PutU32(v, 1);
        PutU32(v, 1);
        PutStr(v, "AB");                 // 'A','B','\0' -> ends at size
        CHECK(render::ParseTextureSetTable(v.data(), v.size(), t));
        CHECK_EQ(t.setCount, 1);
        CHECK_EQ(t.namesPerSet, 1);
        CHECK_EQ((int)t.names.size(), 1);
        CHECK(t.NameFor(0, 0) && *t.NameFor(0, 0) == "AB");
        CHECK(t.NameFor(0, 1) == nullptr);   // material out of range
        CHECK(t.NameFor(1, 0) == nullptr);   // set out of range
    }

    // Header claims more names than the byte budget admits (sets*per > size):
    // the plausibility gate rejects it before any name read.
    {
        std::vector<u8> v;
        PutU32(v, render::kTextureSetMagic);
        PutU32(v, 9);     // 9*32 = 288 names, min 288 bytes, buffer only 12
        PutU32(v, 32);
        CHECK(!render::ParseTextureSetTable(v.data(), v.size(), t));
    }

    // A fully-empty-name table (every row "") at the size budget: each name is a
    // lone NUL, total == sets*per bytes. Parses; every NameFor is null.
    {
        std::vector<u8> v;
        PutU32(v, render::kTextureSetMagic);
        PutU32(v, 2);
        PutU32(v, 3);
        for (int k = 0; k < 6; ++k) PutStr(v, "");   // 6 NUL bytes
        CHECK(render::ParseTextureSetTable(v.data(), v.size(), t));
        CHECK_EQ((int)t.names.size(), 6);
        // Sweep the whole valid index range + the immediate out-of-range corners.
        for (int s = -1; s <= t.setCount; ++s)
            for (int m = -1; m <= t.namesPerSet; ++m)
                CHECK(t.NameFor(s, m) == nullptr);
    }

    // A table whose LAST name is unterminated (file ends mid-name) -> truncated.
    {
        std::vector<u8> v = GoldenTxs();
        v.pop_back();                    // drop the final NUL of the last name
        CHECK(!render::ParseTextureSetTable(v.data(), v.size(), t));
    }
}

TEST(TextureSetTable, FoliageGateMatches0x506388) {
    // The three case-SENSITIVE strncmp prefixes of HideFoliageDecor.
    CHECK(render::IsFoliageMeshName("pfl_R_BUCHE_KRONE_01.bgf"));
    CHECK(render::IsFoliageMeshName("vg_LBAUM01_BREIT_X12KRONE_LAUBKRONE01.bgf"));
    CHECK(render::IsFoliageMeshName("!vg_special"));
    // The original (0x506388 / StrncmpN @0x5e9ee0) compares from BYTE 0 — NO
    // basename split. A path-prefixed name therefore does NOT match: the gate is
    // applied to the bare archive member name in the live engine.
    CHECK(!render::IsFoliageMeshName("Vegetation/BUCHE/pfl_R_BUCHE_KRONE_01.bgf"));
    CHECK(!render::IsFoliageMeshName("Vegetation\\Buesche\\vg_BUSCH_A_1X1X3M_LAUBKRONE.bgf"));
    // Non-foliage / case mismatches (strncmp is case-sensitive in the original).
    CHECK(!render::IsFoliageMeshName("gb_HUETTE_A_0.bgf"));
    CHECK(!render::IsFoliageMeshName("PFL_R_BUCHE_01.bgf"));
    CHECK(!render::IsFoliageMeshName("VG_TANNE.bgf"));
    CHECK(!render::IsFoliageMeshName("Vegetation/BUCHE/"));
    CHECK(!render::IsFoliageMeshName(""));
    CHECK(!render::IsFoliageMeshName(nullptr));
}

TEST(TextureSetTable, SeasonToSetRule) {
    // 0x58339c: season = day % 4; season index == texture-set index
    // (0=_F spring, 1=_S summer, 2=_H autumn, 3=_W winter per 0x505df4).
    CHECK_EQ(render::SeasonTextureSetFromDay(0), 0);   // new game day 0 -> _F
    CHECK_EQ(render::SeasonTextureSetFromDay(1), 1);
    CHECK_EQ(render::SeasonTextureSetFromDay(2), 2);
    CHECK_EQ(render::SeasonTextureSetFromDay(3), 3);
    CHECK_EQ(render::SeasonTextureSetFromDay(4), 0);
    CHECK_EQ(render::SeasonTextureSetFromDay(365), 1);
}

TEST(TextureSetTable, Rgb24StandInSwitchDefaultsOff) {
    // The 1:1 posture: under the unreconstructed palettizer @0x5da34c a 24-bit
    // material renders the level-shaded white default — the stand-in is OFF
    // unless explicitly opted into.
    CHECK(!render::Rgb24MaterialStandInEnabled());
    render::SetRgb24MaterialStandIn(true);
    CHECK(render::Rgb24MaterialStandInEnabled());
    render::SetRgb24MaterialStandIn(false);
    CHECK(!render::Rgb24MaterialStandInEnabled());
}

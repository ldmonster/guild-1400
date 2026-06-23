// Golden-vector unit tests for the sky-colour band reconstruction.
// Vectors derived directly from the gilde.exe decompile (reference of record).
#include "tests/framework/test.h"
#include "render/skycolor_recon.h"

#include <cmath>
#include <cstring>

using namespace guild;
using namespace guild::render;

namespace {

bool close(float a, float b) { return std::fabs(a - b) < 1e-5f; }

// Build a band record with f[i] = base + i (deterministic, distinct values).
SkyBandRecord MakeBand(float base) {
    SkyBandRecord r{};
    for (int i = 0; i < 14; ++i) r.f[i] = base + static_cast<float>(i);
    return r;
}

} // namespace

// --- InterpolateBand: rejection paths ---------------------------------------
TEST(SkyColorReconInterp, RejectsNullAndRange) {
    SkyColorHooks h;
    SkyBandRecord bands[7];
    SkyObject o{};
    o.bands = bands; o.kind = 6;

    CHECK_EQ(SkyColor_InterpolateBand(nullptr, 0, 0.5f, h), (char)0);
    CHECK_EQ(SkyColor_InterpolateBand(&o, 7, 0.5f, h), (char)0);   // band >= 7
    CHECK_EQ(SkyColor_InterpolateBand(&o, 0, -0.1f, h), (char)0);  // frac < 0
    CHECK_EQ(SkyColor_InterpolateBand(&o, 0, 1.5f, h), (char)0);   // frac > 1
    o.kind = 4;
    CHECK_EQ(SkyColor_InterpolateBand(&o, 0, 0.5f, h), (char)0);   // kind < 5
    o.kind = 6; o.bands = nullptr;
    CHECK_EQ(SkyColor_InterpolateBand(&o, 0, 0.5f, h), (char)0);   // null band table
}

// --- InterpolateBand: full interpolation between two bands ------------------
TEST(SkyColorReconInterp, LinearBlendAllPresent) {
    SkyColorHooks h;            // band phase 0
    SkyBandRecord bands[7];
    for (int i = 0; i < 7; ++i) bands[i] = MakeBand(static_cast<float>(i * 100));
    SkyObject o{};
    o.bands = bands; o.kind = 6;

    // band 1, frac 0.25  -> cur=band1, next=band2.  All light fields f[9] (=109,
    // 209) are non-zero, prev band0 f[9]=9 non-zero -> v20,v9,v10 all true.
    const unsigned band = 1; const float frac = 0.25f;
    CHECK_EQ(SkyColor_InterpolateBand(&o, band, frac, h), (char)1);

    float* cur = bands[1].f; float* next = bands[2].f;
    CHECK(close(o.out_148, (next[9] - cur[9]) * frac + cur[9]));   // 109 + 0.25*100
    CHECK(close(o.out_144, (next[10] - cur[10]) * frac + cur[10]));
    CHECK(close(o.out_152, frac * (next[11] - cur[11]) + cur[11]));
    // orientation triple -> out_pos (via SetPosition)
    for (int i = 0; i < 3; ++i)
        CHECK(close(o.out_pos[i], (next[i] - cur[i]) * frac + cur[i]));
    // out_92[i] from f[6+i] (v11/v12 advance each iteration alongside the
    // orientation triple, so iteration i reads band field 6+i).
    for (int i = 0; i < 3; ++i)
        CHECK(close(o.out_92[i], (next[6 + i] - cur[6 + i]) * frac + cur[6 + i]));
    // translation triple -> out_trans (from f[3..5])
    for (int i = 0; i < 3; ++i)
        CHECK(close(o.out_trans[i], (next[3 + i] - cur[3 + i]) * frac + cur[3 + i]));
}

// out_148 selection branches when not all three bands carry light.
TEST(SkyColorReconInterp, Out148Branches) {
    SkyColorHooks h;
    SkyBandRecord bands[7];
    for (int i = 0; i < 7; ++i) { bands[i] = MakeBand(0.0f); bands[i].f[9] = 0.0f; }
    SkyObject o{};
    o.bands = bands; o.kind = 6;

    // Only current band (1) has light, frac < 0.5 -> out_148 = cur[9].
    bands[1].f[9] = 5.0f;
    CHECK_EQ(SkyColor_InterpolateBand(&o, 1, 0.25f, h), (char)1);
    CHECK(close(o.out_148, 5.0f));

    // Only next band (2) has light, frac >= 0.5 -> out_148 = next[9].
    bands[1].f[9] = 0.0f; bands[2].f[9] = 9.0f;
    CHECK_EQ(SkyColor_InterpolateBand(&o, 1, 0.75f, h), (char)1);
    CHECK(close(o.out_148, 9.0f));

    // No band has light -> out_148 = 0.
    bands[2].f[9] = 0.0f;
    CHECK_EQ(SkyColor_InterpolateBand(&o, 1, 0.75f, h), (char)1);
    CHECK(close(o.out_148, 0.0f));
}

// Band phase shifting the effective band index (phase pushes v21 > 1).
TEST(SkyColorReconInterp, PhaseAdvancesBand) {
    SkyColorHooks h; h.band_phase = 0.6f;   // frac 0.5 + 0.6 = 1.1 > 1.0
    SkyBandRecord bands[7];
    for (int i = 0; i < 7; ++i) bands[i] = MakeBand(static_cast<float>(i * 100));
    SkyObject o{};
    o.bands = bands; o.kind = 6;

    // band 6, phase pushes to band (6+1)%7 = 0, v21 = 1.1 - 1.0 = 0.1.
    CHECK_EQ(SkyColor_InterpolateBand(&o, 6, 0.5f, h), (char)1);
    float* cur = bands[0].f; float* next = bands[1].f;   // (0+1)%7 = 1
    const float v21 = 0.1f;
    CHECK(close(o.out_144, (next[10] - cur[10]) * v21 + cur[10]));
}

// --- SetTimeOfDay: integer/fraction split (truncate toward zero) ------------
TEST(SkyColorReconTime, SplitsIntegerAndFraction) {
    SkyColorHooks h;
    SkyBandRecord bands[7];
    for (int i = 0; i < 7; ++i) bands[i] = MakeBand(static_cast<float>(i * 10));
    SkyObject o{};
    o.bands = bands; o.kind = 6;

    float tod = 3.25f;   // -> band 3, frac 0.25
    CHECK_EQ(SkyColor_SetTimeOfDay(&o, &tod, h), (char)1);
    float* cur = bands[3].f; float* next = bands[4].f;
    CHECK(close(o.out_144, (next[10] - cur[10]) * 0.25f + cur[10]));
}

// --- StoreBandColors: exact field mapping -----------------------------------
TEST(SkyColorReconStore, MapsFieldsToBandRecord) {
    SkyBandRecord bands[7]{};
    SkyStoreSource src{};
    src.bands = bands;
    for (int i = 0; i < 39; ++i) src.d[i] = static_cast<u32>(0x1000 + i);

    CHECK_EQ(SkyColor_StoreBandColors(&src, 2), (char)1);
    const float* r = bands[2].f;
    auto at = [&](int byte_off) -> u32 {
        u32 v; std::memcpy(&v, reinterpret_cast<const char*>(r) + byte_off, 4); return v;
    };
    CHECK_EQ(at(40), src.d[36]);
    CHECK_EQ(at(36), src.d[37]);
    CHECK_EQ(at(44), src.d[38]);
    CHECK_EQ(at(0),  src.d[19]);
    CHECK_EQ(at(4),  src.d[20]);
    CHECK_EQ(at(8),  src.d[21]);
    CHECK_EQ(at(24), src.d[23]);
    CHECK_EQ(at(28), src.d[24]);
    CHECK_EQ(at(32), src.d[25]);
    CHECK_EQ(at(12), src.d[33]);
    CHECK_EQ(at(16), src.d[34]);
    CHECK_EQ(at(20), src.d[35]);
}

TEST(SkyColorReconStore, RejectsOutOfRangeOrNull) {
    SkyBandRecord bands[7]{};
    SkyStoreSource src{};
    src.bands = bands;
    CHECK_EQ(SkyColor_StoreBandColors(&src, 7), (char)1);   // band > 6 -> no-op, ret 1
    src.bands = nullptr;
    CHECK_EQ(SkyColor_StoreBandColors(&src, 0), (char)1);   // null table -> ret 1
}

// --- BandHasLight: kind 5/6, 7, default -------------------------------------
TEST(SkyColorReconLight, Kind6Present) {
    SkyBandRecord bands[7]{};
    SkyObject o{}; o.bands = bands; o.kind = 6;
    // a: f[6]>0; b: f[9]>0  -> has light, returns 0, flag set.
    bands[1].f[6] = 1.0f; bands[1].f[9] = 1.0f;
    bool flag = false;
    CHECK_EQ(SkyColor_BandHasLight(&o, 1, &flag), (char)0);
    CHECK(flag);
}

TEST(SkyColorReconLight, Kind6Absent) {
    SkyBandRecord bands[7]{};
    SkyObject o{}; o.bands = bands; o.kind = 6;
    // a true (f[8] magnitude) but b false (f[9],f[10] == 0) -> returns 1.
    bands[0].f[8] = 2.0f;
    bool flag = false;
    CHECK_EQ(SkyColor_BandHasLight(&o, 0, &flag), (char)1);
    CHECK(!flag);
}

TEST(SkyColorReconLight, Kind7) {
    SkyBandRecord bands[7]{};
    SkyObject o{}; o.bands = bands; o.kind = 7;
    // no_dir false (f[6]>0) and f[9]>0 -> has light -> returns 0.
    bands[3].f[6] = 1.0f; bands[3].f[9] = 1.0f;
    bool flag = false;
    CHECK_EQ(SkyColor_BandHasLight(&o, 3, &flag), (char)0);
    CHECK(flag);
    // f[9] <= 0 -> no light -> returns 1.
    bands[3].f[9] = 0.0f; flag = false;
    CHECK_EQ(SkyColor_BandHasLight(&o, 3, &flag), (char)1);
    CHECK(!flag);
}

TEST(SkyColorReconLight, DefaultKind) {
    SkyBandRecord bands[7]{};
    SkyObject o{}; o.bands = bands; o.kind = 5; // 5 uses the 5/6 path
    bool flag = false;
    // all zero -> a false -> returns 1
    CHECK_EQ(SkyColor_BandHasLight(&o, 0, &flag), (char)1);
    o.kind = 9; // default branch -> always 1
    CHECK_EQ(SkyColor_BandHasLight(&o, 0, &flag), (char)1);
}

// --- Gradient table: SetBandGradient + CopyGradientEntry --------------------
TEST(SkyColorReconGradient, StagedColorsStored) {
    SkyGradient g{};
    for (int i = 0; i < 6; ++i) g.staged[i] = static_cast<float>(i + 1);
    unsigned r = SkyColor_SetBandGradient(&g, 3, /*finalize=*/false);
    CHECK_EQ(r, 96u * 3u);
    for (int i = 0; i < 6; ++i) CHECK(close(g.entry[3][i], static_cast<float>(i + 1)));
}

TEST(SkyColorReconGradient, OutOfRangeBandIgnored) {
    SkyGradient g{};
    unsigned r = SkyColor_SetBandGradient(&g, 9, false);
    CHECK_EQ(r, 9u);   // band >= 7 -> returns band unchanged
}

TEST(SkyColorReconGradient, CopyFlushesScratchAndCopies) {
    SkyGradient g{};
    // 6 scratch triples with distinct values.
    for (int i = 0; i < 6; ++i) {
        g.scratch[i].packed = 0x100 + i;
        g.scratch[i].a = static_cast<float>(i) + 0.5f;
        g.scratch[i].b = static_cast<float>(i) + 0.25f;
    }
    // Pending flush into scratch[2].
    g.pending_index = 2;
    g.pending_a = 7.0f; g.pending_b = 8.0f; g.pending_packed = 0x999;

    float* res = SkyColor_CopyGradientEntry(&g, 4);
    CHECK_EQ(res, g.entry[4]);
    // pending applied:
    CHECK(close(g.scratch[2].a, 7.0f));
    CHECK(close(g.scratch[2].b, 8.0f));
    CHECK_EQ(g.scratch[2].packed, 0x999);
    // 72 bytes (6 triples) copied into entry[4]+6.
    CHECK_EQ(std::memcmp(g.entry[4] + 6, g.scratch, 72), 0);
}

TEST(SkyColorReconGradient, NoPendingWhenIndexNegative) {
    SkyGradient g{};
    g.pending_index = -1;
    g.scratch[0].packed = 0x55;
    g.pending_packed = 0x77;
    SkyColor_CopyGradientEntry(&g, 0);
    CHECK_EQ(g.scratch[0].packed, 0x55);  // untouched
}

// --- ApplyDefaultLighting wrapper -------------------------------------------
TEST(SkyColorReconDefault, ForwardsFullStrengthBlend) {
    DefaultLightingHook hook;
    unsigned band = 4;
    CHECK_EQ(SkyColor_ApplyDefaultLighting(&band, hook), 1);
    CHECK_EQ(hook.last_band, 4u);
    CHECK(close(hook.last_alpha, 1.0f));
    CHECK(close(hook.last_scale, 1.0f));   // 0x3F800000 == 1.0f
    CHECK_EQ(hook.last_flag, 0);
}

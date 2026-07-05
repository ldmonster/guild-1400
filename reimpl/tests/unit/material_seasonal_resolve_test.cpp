#include "test.h"

// Unit tests — the texture-set binding at the resolve layer
// (play::RealTextureSource::BuildTableFor + render::TextureSetTable),
// VERIFIED against the binary (wave-4 verification pass):
//
//   * the sidecar is adopted for EVERY mesh iff setCount > 0 and
//     namesPerSet == materialCount (VIBE_Model_LoadFastChunk @0x5f8a0e);
//   * an ADOPTED mesh binds the SET-0 rows at load (0x5f8c14) — NOT the .bgf
//     material names (349 shipped materials differ);
//   * the SEASON (day % 4 @0x58339c; byte_634484 @0x506df4) applies as the
//     set index to FOLIAGE members only (pfl_/vg_/!vg_ @0x506388) through
//     VIBE_Object_SelectTextureSet @0x5b3f54: an EMPTY season row keeps the
//     current (set-0) binding (0x5b4099); a row that fails to load keeps the
//     old texture (0x5b420b); season >= setCount keeps the set-0 state
//     (0x5b403b);
//   * without an adopted sidecar the .bgf material name binds, the script
//     record's name2 preferred over name0 (the 0x5d31b1 arms; the fast
//     chunk's second string == script name2, the 0x5f8b88 preferred load).
//
// Archive-less: BMPs are seeded through TextureBin::DecodeBuffer and the
// .TXS bytes served by the SetTxsFetch hook.
#include "play/real_texture_source.h"
#include "render/bgf_loader.h"
#include "render/bmp.h"
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

// 4x4 24-bit BMP filled with one colour (decode fills DecodedBmp::rgba; the
// resolve layer palettizes it — render/texture_palettize.h — so sampling
// returns the quantizer's 5-bit bucket centre of the colour).
std::vector<u8> SolidBmp(u8 r, u8 g, u8 b) {
    const int w = 4, h = 4;
    std::vector<u8> rgb((std::size_t)w * h * 3);
    for (int i = 0; i < w * h; ++i) {
        rgb[(std::size_t)i * 3 + 0] = r;
        rgb[(std::size_t)i * 3 + 1] = g;
        rgb[(std::size_t)i * 3 + 2] = b;
    }
    std::vector<u8> file = render::BmpSave24Bit(w, h, rgb.data());
    // Engine Save24 (gilde.exe 0x5f18f4) writes pixel data at 58; loaders seek
    // the standard 0x36 (0x5f0ce4) like real tool-authored assets. Convert the
    // synthetic fixture to standard layout.
    file.erase(file.begin() + 54, file.begin() + 58);
    file[10] = 54;
    for (int k = 0; k < 4; ++k) file[2 + k] = (u8)(file.size() >> (8 * k));
    return file;
}

// One-material foliage model whose name0 is the suffix-less base (the shipped
// vg_nm_* authoring style: the base resolves to NOTHING; only the per-set
// names exist).
render::BgfModel FoliageModel() {
    render::BgfModel m;
    m.materialCount = 1;
    m.materials.resize(1);
    m.materials[0].name0 = "vg_nm_TEST_Laub";
    m.vertexCount = 3;
    m.vertices.resize(3);
    m.polyCount = 1;
    m.polygons.resize(1);
    m.polygons[0].matIndex = 0;
    m.polygons[0].vtx[0] = 0; m.polygons[0].vtx[1] = 1; m.polygons[0].vtx[2] = 2;
    return m;
}

// The 4-set sidecar: set 0 (_F) resolves, set 1 (_S) is EMPTY (keep current),
// set 2 (_H) names a MISSING texture (the 0x5b420b "new not found keeps old"
// arm), set 3 (_W) resolves.
std::vector<u8> FoliageTxs() {
    std::vector<u8> v;
    PutU32(v, render::kTextureSetMagic);
    PutU32(v, 4);
    PutU32(v, 1);
    PutStr(v, "vg_nm_TEST_Laub_F");
    PutStr(v, "");
    PutStr(v, "vg_nm_TEST_Laub_H");   // not seeded -> unresolvable
    PutStr(v, "vg_nm_TEST_Laub_W");
    return v;
}

struct Fixture {
    play::RealTextureSource src;
    render::BgfModel model = FoliageModel();

    Fixture() {
        std::vector<u8> f = SolidBmp(0, 200, 0);     // _F: green
        std::vector<u8> w = SolidBmp(240, 240, 240); // _W: snow white
        src.bin().DecodeBuffer("vg_nm_TEST_Laub_F.BMP", f);
        src.bin().DecodeBuffer("vg_nm_TEST_Laub_W.BMP", w);
        src.SetTxsFetch([](const char* member, std::vector<u8>& out) {
            // BuildTableFor must ask for the sibling "<stem>.TXS" — for EVERY
            // member (the engine loads the sidecar unconditionally @0x5d32d4).
            if (std::strcmp(member, "Vegetation/X/vg_TESTBAUM_KRONE.TXS") != 0 &&
                std::strcmp(member, "Gebaeude/X/gb_TESTHAUS.TXS") != 0)
                return false;
            out = FoliageTxs();
            return true;
        });
    }
};

const char* kMember = "Vegetation/X/vg_TESTBAUM_KRONE.bgf";
const char* kBuilding = "Gebaeude/X/gb_TESTHAUS.bgf";

// The bound PALETTIZED colour of the table's first entry (what the original
// software renderer displays): the quantizer maps a solid colour to its 5-bit
// bucket centre ((c&0xF8)+4) — green (0,200,0) -> (4,204,4); snow
// (240,240,240) -> (244,244,244); the DecodedBmp's rgba keeps the source
// truth for the legacy RGB stand-in.
bool BoundColorIs(const play::MaterialTextureTable* t, u8 r, u8 g, u8 b) {
    if (!t || t->textures.empty() || !t->textures[0].bmp) return false;
    const render::DecodedBmp* bmp = t->textures[0].bmp;
    if (bmp->indices.empty() || bmp->palette.size() < 768) return false;
    u8 idx = bmp->indices[0];
    return bmp->palette[(std::size_t)idx * 3 + 0] == r &&
           bmp->palette[(std::size_t)idx * 3 + 1] == g &&
           bmp->palette[(std::size_t)idx * 3 + 2] == b;
}
bool IsGreen(const play::MaterialTextureTable* t) { return BoundColorIs(t, 4, 204, 4); }
bool IsSnow(const play::MaterialTextureTable* t) { return BoundColorIs(t, 244, 244, 244); }

} // namespace

TEST(MaterialSeasonalResolve, Set0BindsSpringRow) {
    Fixture fx;
    fx.src.SetActiveTextureSet(0);   // day 0 -> season 0 -> _F
    const play::MaterialTextureTable* tbl = fx.src.BuildTableFor(kMember, fx.model);
    CHECK(tbl != nullptr);
    if (!tbl) return;
    CHECK_EQ(tbl->appliedTextureSet, 0);
    // Set 0 IS the load state — no SelectTextureSet replacement happened.
    CHECK_EQ(tbl->swappedMaterials, 0);
    CHECK_EQ(tbl->resolvedMaterials, 1);
    CHECK_EQ(tbl->texturedPolys, 1);
    CHECK(tbl->textures.size() == 1);
    if (tbl->textures.size() == 1) {
        CHECK(tbl->textures[0].setName == "vg_nm_TEST_Laub_F");
        CHECK(tbl->textures[0].materialName == "vg_nm_TEST_Laub");
        // The bound pixels ARE the _F texture (palettized green).
        CHECK(IsGreen(tbl));
    }
}

TEST(MaterialSeasonalResolve, Set3BindsWinterAndCachesPerSet) {
    Fixture fx;
    fx.src.SetActiveTextureSet(3);   // winter -> _W
    const play::MaterialTextureTable* w = fx.src.BuildTableFor(kMember, fx.model);
    CHECK(w && w->appliedTextureSet == 3 && w->swappedMaterials == 1);
    if (w && !w->textures.empty()) {
        CHECK(w->textures[0].setName == "vg_nm_TEST_Laub_W");
        CHECK(IsSnow(w));
    }
    // Switching seasons builds a DIFFERENT cached table (the cache identity
    // carries the set index — a season change must rebuild, not reuse).
    fx.src.SetActiveTextureSet(0);
    const play::MaterialTextureTable* f = fx.src.BuildTableFor(kMember, fx.model);
    CHECK(f != nullptr);
    CHECK(f != w);
    if (f) CHECK_EQ(f->appliedTextureSet, 0);
}

TEST(MaterialSeasonalResolve, EmptyAndUnresolvableRowsKeepSet0Binding) {
    Fixture fx;
    // Set 1 row is EMPTY -> the 0x5b4099 skip KEEPS the current binding: the
    // set-0 load state (_F). (The old name0-fallback reading was REFUTED by
    // the binary — name0 is never rebound.)
    fx.src.SetActiveTextureSet(1);
    const play::MaterialTextureTable* t1 = fx.src.BuildTableFor(kMember, fx.model);
    CHECK(t1 != nullptr);
    if (t1) {
        CHECK_EQ(t1->appliedTextureSet, 1);
        CHECK_EQ(t1->swappedMaterials, 0);
        CHECK_EQ(t1->resolvedMaterials, 1);
        CHECK_EQ(t1->texturedPolys, 1);
        CHECK(IsGreen(t1));
    }
    // Set 2 row names a MISSING texture -> the 0x5b420b "new not found" arm
    // keeps the old binding — again the set-0 _F texture.
    fx.src.SetActiveTextureSet(2);
    const play::MaterialTextureTable* t2 = fx.src.BuildTableFor(kMember, fx.model);
    CHECK(t2 != nullptr);
    if (t2) {
        CHECK_EQ(t2->swappedMaterials, 0);
        CHECK_EQ(t2->resolvedMaterials, 1);
        CHECK(IsGreen(t2));
    }
}

TEST(MaterialSeasonalResolve, SeasonBeyondSetCountKeepsSet0State) {
    // set >= *(mesh+484) -> SelectTextureSet returns 0 WITHOUT swapping
    // (0x5b403b): the set-0 load state stays bound.
    Fixture fx;
    fx.src.SetActiveTextureSet(7);
    const play::MaterialTextureTable* t = fx.src.BuildTableFor(kMember, fx.model);
    CHECK(t != nullptr);
    if (t) {
        CHECK_EQ(t->appliedTextureSet, 0);   // the effective (load) set
        CHECK_EQ(t->swappedMaterials, 0);
        CHECK_EQ(t->resolvedMaterials, 1);
        CHECK(IsGreen(t));
    }
}

TEST(MaterialSeasonalResolve, NonFoliageBindsSet0NotSeason) {
    // The engine adopts the sidecar for EVERY mesh and binds the SET-0 rows
    // at load; only FOLIAGE nodes get the season applied (0x506388). A
    // building member therefore binds _F (set 0) even in winter.
    Fixture fx;
    fx.src.SetActiveTextureSet(3);
    const play::MaterialTextureTable* gb = fx.src.BuildTableFor(kBuilding, fx.model);
    CHECK(gb != nullptr);
    if (gb) {
        CHECK_EQ(gb->appliedTextureSet, 0);
        CHECK_EQ(gb->swappedMaterials, 0);
        CHECK_EQ(gb->resolvedMaterials, 1);
        CHECK(!gb->textures.empty() &&
              gb->textures[0].setName == "vg_nm_TEST_Laub_F");
        CHECK(IsGreen(gb));
    }
}

TEST(MaterialSeasonalResolve, Set0RowBeatsMaterialName) {
    // The 349-shipped-materials truth: when the adopted set-0 row differs
    // from the .bgf material name, the ROW is what renders — even though the
    // material name itself resolves.
    Fixture fx;
    std::vector<u8> base = SolidBmp(200, 0, 0);      // name0: red — resolvable
    fx.src.bin().DecodeBuffer("vg_nm_TEST_Laub.BMP", base);
    fx.src.SetActiveTextureSet(0);
    const play::MaterialTextureTable* t = fx.src.BuildTableFor(kMember, fx.model);
    CHECK(t != nullptr);
    if (t && !t->textures.empty()) {
        CHECK(t->textures[0].setName == "vg_nm_TEST_Laub_F");
        CHECK(IsGreen(t));   // not red
    }
}

TEST(MaterialSeasonalResolve, MinusOneIsReimplDebugDisable) {
    Fixture fx;
    // -1: REIMPL-ONLY debug state — sidecars ignored, raw material names
    // bind (the engine has no such state; its load state is set 0).
    fx.src.SetActiveTextureSet(-1);
    const play::MaterialTextureTable* off = fx.src.BuildTableFor(kMember, fx.model);
    CHECK(off != nullptr);
    if (off) {
        CHECK_EQ(off->appliedTextureSet, -1);
        CHECK_EQ(off->resolvedMaterials, 0);   // base name resolves to nothing
    }
}

TEST(MaterialSeasonalResolve, Name2PreferredOverName0) {
    // The engine prefers the script material's NAME2 (record +128 — the
    // 0x5d31b1 arm; baked as the fast chunk's second string, the 0x5f8b88
    // preferred load with the |1 flag). The shipped archive carries 42 such
    // materials, e.g. the reflection-map props with an EMPTY name0
    // (ob_VITRINE: '', '', 'EF_GLAS_01a_1t_Spiegel').
    play::RealTextureSource src;
    std::vector<u8> blue = SolidBmp(0, 0, 200);
    src.bin().DecodeBuffer("REFLEKT_TEST.BMP", blue);
    render::BgfModel m = FoliageModel();
    m.materials[0].name0 = "";
    m.materials[0].name2 = "REFLEKT_TEST";
    src.SetActiveTextureSet(0);                  // no sidecar -> names bind
    const play::MaterialTextureTable* t =
        src.BuildTableFor("Accessoires/X/ob_TEST.bgf", m);
    CHECK(t != nullptr);
    if (t) {
        CHECK_EQ(t->appliedTextureSet, -1);      // nothing adopted
        CHECK_EQ(t->resolvedMaterials, 1);
        CHECK(BoundColorIs(t, 4, 4, 204));       // palettized blue
    }
}

TEST(MaterialSeasonalResolve, CountGateRejectsMismatchedSidecar) {
    // namesPerSet != materialCount -> NOT adopted (the 0x5f8a0e gate; the one
    // shipped outlier gb_PALAZZO_B_0 lands here): the engine synthesizes a
    // 1-set table from the material names — observable as a plain name bind.
    Fixture fx;
    fx.src.SetTxsFetch([](const char*, std::vector<u8>& out) {
        std::vector<u8> v;
        PutU32(v, render::kTextureSetMagic);
        PutU32(v, 4);
        PutU32(v, 2);   // model has 1 material -> mismatch
        for (int i = 0; i < 8; ++i) PutStr(v, "vg_nm_TEST_Laub_F");
        out = v;
        return true;
    });
    fx.src.SetActiveTextureSet(0);
    const play::MaterialTextureTable* t = fx.src.BuildTableFor(kMember, fx.model);
    CHECK(t != nullptr);
    if (t) {
        CHECK_EQ(t->appliedTextureSet, -1);
        CHECK_EQ(t->swappedMaterials, 0);
        CHECK_EQ(t->resolvedMaterials, 0);   // name0 resolves to nothing
    }
}

// W10-TEX hardening: drive the texture-set swap against count mismatches and an
// out-of-range season so ASAN bounds the NameFor() index math in BuildTableFor.
TEST(MaterialSeasonalResolve, CountMismatchAndOutOfRangeSeasonNoOOB) {
    // namesPerSet GREATER than materialCount (1): not adopted; the per-set rows
    // beyond material 0 are never addressed. Season set to the last claimed set.
    {
        Fixture fx;
        fx.src.SetTxsFetch([](const char*, std::vector<u8>& out) {
            std::vector<u8> v;
            PutU32(v, render::kTextureSetMagic);
            PutU32(v, 3);     // setCount
            PutU32(v, 5);     // namesPerSet (model has 1 material -> mismatch)
            for (int i = 0; i < 15; ++i) PutStr(v, "vg_nm_TEST_Laub_F");
            out = v;
            return true;
        });
        fx.src.SetActiveTextureSet(2);   // last valid set index
        const play::MaterialTextureTable* t = fx.src.BuildTableFor(kMember, fx.model);
        CHECK(t != nullptr);
        if (t) CHECK_EQ(t->appliedTextureSet, -1);   // not adopted
    }
    // Adopted sidecar, but the active set is a huge / negative value: the
    // season>=setCount and the NameFor range gates must keep it in bounds.
    {
        Fixture fx;
        fx.src.SetActiveTextureSet(1 << 28);   // absurd season
        const play::MaterialTextureTable* t = fx.src.BuildTableFor(kMember, fx.model);
        CHECK(t != nullptr);
        if (t) CHECK_EQ(t->appliedTextureSet, 0);   // beyond setCount -> set-0 state
        fx.src.SetActiveTextureSet(-5);            // negative season
        const play::MaterialTextureTable* t2 = fx.src.BuildTableFor(kMember, fx.model);
        CHECK(t2 != nullptr);                       // no OOB on a negative set
    }
}

TEST(MaterialSeasonalResolve, ProcessDefaultSeedsNewInstances) {
    // The process default (0 = the day-0 season) seeds construction; restore it.
    const int orig = play::RealTextureSource::DefaultActiveTextureSet();
    CHECK_EQ(orig, 0);   // new-game day 0 -> season 0 (_F)
    play::RealTextureSource::SetDefaultActiveTextureSet(2);
    play::RealTextureSource s2;
    CHECK_EQ(s2.activeTextureSet(), 2);
    play::RealTextureSource::SetDefaultActiveTextureSet(orig);
    play::RealTextureSource s0;
    CHECK_EQ(s0.activeTextureSet(), 0);
}

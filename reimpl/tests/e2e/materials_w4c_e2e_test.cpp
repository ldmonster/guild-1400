#include "test.h"

// GUARDED real-asset e2e — wave-4 W4-C material/texture fidelity, pins
// RECALIBRATED by the wave-4 VERIFICATION pass (engine-truth set-0 binding +
// the reconstructed 24-bit palettizer):
//
//   (1) TEXTURE SETS over the real archives: the .TXS sidecar format
//       (render/texture_set_table.h; engine loader @0x5d2240) surveyed across
//       ALL of Objects.BIN; the set-major layout, the namesPerSet ==
//       materialCount adoption rule (@0x5f8a0e) and the foliage _F/_S/_H/_W
//       set order pinned on shipped bytes.
//   (2) The AUGSBURG city frame: white-default pixel counts at set -1 (the
//       reimpl debug names-only state) vs set 0 (the ENGINE load state:
//       adopted sidecars bind their SET-0 rows; the day-0 season
//       byte_634484 @0x506df4 adds nothing on top of set 0), plus
//       per-material accounting of the wave-3 unbound slots.
//   (3) A real 24-bit city BMP through the RECONSTRUCTED software palettizer
//       (VIBE_Quant_BuildPalette @0x6029f0) with golden output pins; the
//       legacy RGB stand-in switch stays default-OFF (compat only).
#include "app/wiring.h"
#include "io/archive_mount.h"
#include "io/save_world_load.h"
#include "play/city_view3d.h"
#include "play/object_mesh_render.h"
#include "play/real_texture_source.h"
#include "render/surface.h"
#include "render/texture.h"
#include "render/texture_bin.h"
#include "render/texture_set_table.h"
#include "shim_impl/disk_filesystem.h"
#include "sim/entity.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace guild;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/guild-1400/reimpl/europe_guild_1400_original";
}

bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Gilde.INI") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty") &&
           fs.exists("Resources/Objects.BIN") &&
           fs.exists("Resources/Textures.BIN");
}

// The white-default family tally: the unbound-material fallback draws the
// LEVEL-shaded white default — linear gray PackColor(L,L,L), which reads back
// through 565 as r==b, g within +7 of r (meshlist.cpp RasterTri). The sky
// clear (0,0,64) has r != b and never counts.
struct GrayTally {
    int gray = 0;
    int total = 0;
};
GrayTally TallyGray(render::Surface* s, int w, int h) {
    GrayTally t;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            u8 px[3];
            render::SurfaceGetPixelRgb(s, x, y, px);
            ++t.total;
            if (px[0] == px[2] && (int)px[1] >= (int)px[0] &&
                (int)px[1] - (int)px[0] <= 7)
                ++t.gray;
        }
    return t;
}

void DumpPpm(const char* path, render::Surface* s, int w, int h) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            u8 px[3];
            render::SurfaceGetPixelRgb(s, x, y, px);
            std::fwrite(px, 1, 3, f);
        }
    std::fclose(f);
}

// Per-material accounting over the AUGSBURG rendered members through a
// RealTextureSource with the given active set.
struct MatAccount {
    int mats = 0;
    int resolved = 0;        // bound 8-bit
    int viaSet = 0;          // bound through a .TXS row (subset of resolved)
    int noIndices24 = 0;     // resolved name but 24-bit (unbindable record)
    int unresolvedName = 0;  // no BMP at all
    std::map<std::string, int> unresolvedNames;
};
MatAccount Account(play::RealTextureSource& tex, play::RealMeshSource& mesh,
                   const std::set<std::string>& members) {
    MatAccount a;
    for (const auto& m : members) {
        if (!mesh.Resolve(m.c_str())) continue;
        const render::BgfModel* model = mesh.ModelFor(m.c_str());
        if (!model) continue;
        const play::MaterialTextureTable* tbl = tex.BuildTableFor(m.c_str(), *model);
        if (!tbl) continue;
        for (std::size_t mi = 0; mi < tbl->matToTex.size(); ++mi) {
            ++a.mats;
            int id = tbl->matToTex[mi];
            const render::DecodedBmp* bmp = tbl->TextureFor(id);
            if (!bmp || !bmp->ok) {
                ++a.unresolvedName;
                if (mi < model->materials.size())
                    a.unresolvedNames[model->materials[mi].name0]++;
            } else if (bmp->indices.empty()) {
                ++a.noIndices24;
            } else {
                ++a.resolved;
                if (id >= 0 && (std::size_t)id < tbl->textures.size() &&
                    !tbl->textures[(std::size_t)id].setName.empty())
                    ++a.viaSet;
            }
        }
    }
    return a;
}

} // namespace

// ---------------------------------------------------------------------------
// 1. The .TXS sidecar over the WHOLE shipped archive: format + correspondence
//    + seasonal set order, byte-pinned.
// ---------------------------------------------------------------------------
TEST(MaterialsW4C, TxsArchiveSurvey) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] MaterialsW4C.TxsArchiveSurvey: real game dir absent\n");
        CHECK(true);
        return;
    }
    shim::DiskFileSystem fs(GameDir());
    io::ArchiveMount obj;
    CHECK(obj.Mount(&fs, "Resources/Objects.BIN", true));

    int txsTotal = 0, parsed = 0, exactConsume = 0;
    int suffixF = 0, suffixS = 0, suffixH = 0, suffixW = 0, suffixWrongSlot = 0;
    for (const auto& mem : obj.members()) {
        const std::string& n = mem.name;
        if (n.size() < 4) continue;
        std::string ext = n.substr(n.size() - 4);
        if (ext != ".TXS" && ext != ".txs") continue;
        ++txsTotal;
        std::vector<u8> raw;
        if (!obj.OpenMember(n.c_str(), raw)) continue;
        render::TextureSetTable t;
        if (!render::ParseTextureSetTable(raw.data(), raw.size(), t)) continue;
        ++parsed;
        // Re-derive consumed size: 12 + every name + its NUL.
        std::size_t consumed = 12;
        for (const auto& s : t.names) consumed += s.size() + 1;
        if (consumed == raw.size()) ++exactConsume;
        // Seasonal-order survey: rows that are "<something>_<letter>".
        for (i32 set = 0; set < t.setCount; ++set)
            for (i32 m = 0; m < t.namesPerSet; ++m) {
                const std::string* nm = t.NameFor(set, m);
                if (!nm || nm->size() < 3) continue;
                char c = (char)std::toupper((unsigned char)(*nm)[nm->size() - 1]);
                if ((*nm)[nm->size() - 2] != '_') continue;
                if (c != 'F' && c != 'S' && c != 'H' && c != 'W') continue;
                if (t.setCount != 4) continue;   // only 4-set tables are seasonal
                const char order[4] = {'F', 'S', 'H', 'W'};
                if (c == order[set]) {
                    if (c == 'F') ++suffixF;
                    else if (c == 'S') ++suffixS;
                    else if (c == 'H') ++suffixH;
                    else ++suffixW;
                } else {
                    ++suffixWrongSlot;
                }
            }
    }
    std::printf("[w4c] TXS survey: total=%d parsed=%d exactConsume=%d  "
                "seasonal rows F=%d S=%d H=%d W=%d wrongSlot=%d\n",
                txsTotal, parsed, exactConsume,
                suffixF, suffixS, suffixH, suffixW, suffixWrongSlot);
    // Shipped archive: 590 sidecars, all parse and consume exactly.
    CHECK_EQ(txsTotal, 590);
    CHECK_EQ(parsed, 590);
    CHECK_EQ(exactConsume, 590);
    // The seasonal SET ORDER (_F/_S/_H/_W = sets 0..3) holds with zero
    // contradictions — the byte basis for season index == set index.
    CHECK(suffixF >= 20 && suffixS >= 14 && suffixH >= 20 && suffixW >= 50);
    CHECK_EQ(suffixWrongSlot, 0);

    // Pin one full table: Vegetation/BUCHE/pfl_R_BUCHE_KRONE_01.TXS
    // (setCount 4, namesPerSet 3 == the .bgf fast-chunk materialCount).
    std::vector<u8> raw;
    CHECK(obj.OpenMember("Vegetation/BUCHE/pfl_R_BUCHE_KRONE_01.TXS", raw));
    render::TextureSetTable t;
    CHECK(render::ParseTextureSetTable(raw.data(), raw.size(), t));
    CHECK_EQ(t.setCount, 4);
    CHECK_EQ(t.namesPerSet, 3);
    CHECK(t.NameFor(0, 0) && *t.NameFor(0, 0) == "vg_nm_Laub_Buche_s01_F");
    CHECK(t.NameFor(0, 1) && *t.NameFor(0, 1) == "vg_nm_Laub_Buche_t01_F");
    CHECK(t.NameFor(0, 2) && *t.NameFor(0, 2) == "vg_nm_Laub_Buche_s02_F");
    CHECK(t.NameFor(1, 0) && *t.NameFor(1, 0) == "VG_NM_LAUB_BUCHE_S01_S");
    CHECK(t.NameFor(2, 0) && *t.NameFor(2, 0) == "VG_NM_LAUB_BUCHE_S01_H");
    CHECK(t.NameFor(3, 0) && *t.NameFor(3, 0) == "VG_NM_LAUB_BUCHE_S01_W");
    CHECK(t.NameFor(3, 2) && *t.NameFor(3, 2) == "VG_NM_LAUB_BUCHE_S02_W");
}

// ---------------------------------------------------------------------------
// 2. AUGSBURG: per-material accounting + the white-default frame delta.
// ---------------------------------------------------------------------------
TEST(MaterialsW4C, AugsburgSeasonalResolveAndFrameDelta) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] MaterialsW4C.Augsburg: real game dir absent\n");
        CHECK(true);
        return;
    }
    shim::DiskFileSystem fs(GameDir());
    app::RealGameAssets assets = app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI");
    CHECK(assets.vfsBound);

    // ---- the BEFORE frame: set -1 (reimpl debug: raw material names, no
    // sidecars — the engine has no such state; kept as the delta baseline) ---
    play::RealTextureSource::SetDefaultActiveTextureSet(-1);
    GrayTally before{};
    std::set<std::string> members;
    int beforeBound = 0;
    {
        sim::ResetEntityArrays();
        io::WorldState world{};
        std::vector<u8> sceneBlob;
        CHECK(io::LoadWorldEx("Resources/gamedata/Cities/AUGSBURG.cty", world,
                              &sceneBlob));
        play::CityView3D view;
        CHECK(view.Init(&fs));
        CHECK(view.LoadCityFromWorld(sceneBlob));
        view.BindWorldObjects();
        play::CityView3D::Options opt;
        opt.fbW = 320; opt.fbH = 240;
        play::CityView3D::Result r = view.RenderFrame(view.OverviewCamera(), opt);
        before = TallyGray(view.surface(), opt.fbW, opt.fbH);
        beforeBound = r.boundMaterials;
        for (const auto& inst : view.instances())
            if (!inst.member.empty()) members.insert(inst.member);
        sim::ResetEntityArrays();
    }

    // ---- the AFTER frame: set 0 — the ENGINE load state (set-0 rows for
    // every adopted sidecar; day-0 season 0 on foliage == the same rows) -----
    play::RealTextureSource::SetDefaultActiveTextureSet(0);
    GrayTally after{};
    int afterBound = 0;
    {
        sim::ResetEntityArrays();
        io::WorldState world{};
        std::vector<u8> sceneBlob;
        CHECK(io::LoadWorldEx("Resources/gamedata/Cities/AUGSBURG.cty", world,
                              &sceneBlob));
        play::CityView3D view;
        CHECK(view.Init(&fs));
        CHECK(view.LoadCityFromWorld(sceneBlob));
        view.BindWorldObjects();
        play::CityView3D::Options opt;
        opt.fbW = 320; opt.fbH = 240;
        play::CityView3D::Result r = view.RenderFrame(view.OverviewCamera(), opt);
        after = TallyGray(view.surface(), opt.fbW, opt.fbH);
        afterBound = r.boundMaterials;
        DumpPpm("/tmp/guild_w4c_materials.ppm", view.surface(), opt.fbW, opt.fbH);
        sim::ResetEntityArrays();
    }
    std::printf("[w4c] frame gray(white-default family): before=%d after=%d "
                "(of %d)  boundMats before=%d after=%d\n",
                before.gray, after.gray, after.total, beforeBound, afterBound);
    std::printf("[w4c] frame dump: /tmp/guild_w4c_materials.ppm\n");

    // ---- per-material accounting over the rendered members -----------------
    play::RealMeshSource mesh;
    CHECK(mesh.MountArchive(&fs, "Resources/Objects.BIN", true));

    play::RealTextureSource texOff;
    texOff.SetActiveTextureSet(-1);
    CHECK(texOff.Mount(&fs));
    MatAccount off = Account(texOff, mesh, members);

    play::RealTextureSource texOn;
    texOn.SetActiveTextureSet(0);
    CHECK(texOn.Mount(&fs));
    MatAccount on = Account(texOn, mesh, members);

    std::printf("[w4c] materials (%d slots over %zu members):\n", off.mats,
                members.size());
    std::printf("[w4c]   set -1: bound=%d 24bpp=%d unresolved=%d\n",
                off.resolved, off.noIndices24, off.unresolvedName);
    std::printf("[w4c]   set  0: bound=%d (viaTXS=%d) 24bpp=%d unresolved=%d\n",
                on.resolved, on.viaSet, on.noIndices24, on.unresolvedName);
    int shown = 0;
    for (const auto& kv : on.unresolvedNames) {
        std::printf("[w4c]   still unresolved: %-48s x%d\n", kv.first.c_str(),
                    kv.second);
        if (++shown >= 40) break;
    }

    // PINS — recalibrated by the wave-4 VERIFICATION pass (engine-truth
    // semantics + the reconstructed @0x5da34c/@0x6029f0 palettizer):
    //
    //   * 24-bit categories are GONE (noIndices24 == 0): every 24-bit BMP now
    //     palettizes through VIBE_Quant_BuildPalette (render/
    //     texture_palettize.h) and binds like an 8-bit source — the former 23
    //     unbindable slots are bound in BOTH columns (894+23=917 at set -1).
    //   * set -1 (reimpl debug: raw names, no sidecars): 917 bound / 34
    //     unresolved of 951.
    //   * set 0 (the ENGINE load state: adopted sidecars bind their SET-0
    //     rows, foliage gets season 0 on top): 937 bound / 14 unresolved.
    //     viaTXS == 143: EVERY adopted material binds via a row (the 0x5f8c14
    //     load over the name table) — not only the foliage swaps; 318 shipped
    //     non-foliage materials differ from their material names archive-wide.
    //   * the 14 left are the genuinely-absent BMPs (dc_dchz_05a_1n_
    //     rautenziegel, sf_mrwr_*, ... — absent from Textures.BIN: the
    //     original's load fails too, white IS the engine state).
    CHECK_EQ(off.mats, on.mats);
    CHECK_EQ(off.mats, 951);
    CHECK_EQ(off.resolved, 917);
    CHECK_EQ(off.noIndices24, 0);
    CHECK_EQ(off.unresolvedName, 34);

    CHECK(on.resolved > off.resolved);
    CHECK(on.viaSet >= on.resolved - off.resolved);
    CHECK_EQ(on.noIndices24, 0);
    CHECK_EQ(on.resolved - off.resolved, off.unresolvedName - on.unresolvedName);

    CHECK_EQ(on.resolved, 937);
    CHECK_EQ(on.unresolvedName, 14);
    CHECK_EQ(on.viaSet, 143);

    // The frame: vegetation + the palettized 24-bit slots leave the
    // white-default family, and the live binds rise (3142/3636 -> 3177/3671).
    // Pinned 320x240 overview counts, re-pinned after the wave-4 raster
    // verification (progress/raster-verify-wave4.md): the textured leaf now
    // applies the evidence-exact 16.16 UV scale (vu = u_texels * 65536,
    // 0x5f6c30 v53) and the white default shades by the AVG +66 light byte
    // (dword_13FC5E0, 0x5f70bd) instead of the max — 11043 -> 10814 (set -1)
    // and 8201 -> 8392 (set 0).
    CHECK(after.gray < before.gray);
    CHECK(afterBound > beforeBound);
    CHECK_EQ(before.gray, 10814);
    CHECK_EQ(after.gray, 8392);
    CHECK_EQ(beforeBound, 3177);
    CHECK_EQ(afterBound, 3671);
}

// ---------------------------------------------------------------------------
// 3. A REAL 24-bit city BMP through the RECONSTRUCTED palettizer
//    (VIBE_Texture_LoadSoftPalettize @0x5da34c -> VIBE_Quant_BuildPalette
//    @0x6029f0, render/texture_palettize.h) — golden output pins.
// ---------------------------------------------------------------------------
TEST(MaterialsW4C, Real24BitBmpPalettizedGolden) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] MaterialsW4C.24bit: real game dir absent\n");
        CHECK(true);
        return;
    }
    // The legacy RGB stand-in switch stays OFF (compat only — the palettized
    // path is the default and makes it unreachable for resolve-layer binds).
    CHECK(!render::Rgb24MaterialStandInEnabled());

    shim::DiskFileSystem fs(GameDir());
    // The DECODE layer alone leaves a 24-bit source index-less...
    render::TextureBin bin;
    CHECK(bin.Mount(&fs));
    const render::DecodedBmp* raw = bin.Decode("hz_Dachleisten_Dunkel_AA");
    CHECK(raw != nullptr);
    if (!raw) return;
    CHECK_EQ(raw->bpp, 24);
    CHECK(raw->indices.empty());
    CHECK(!raw->rgba.empty());

    // ...and the RESOLVE layer palettizes it on first decode (one of the 23
    // formerly-unbindable AUGSBURG materials, wave-3 survey).
    play::RealTextureSource tex;
    CHECK(tex.Mount(&fs));
    const render::DecodedBmp* bmp = tex.ResolveMaterial("hz_Dachleisten_Dunkel_AA");
    CHECK(bmp != nullptr);
    if (!bmp) return;
    CHECK_EQ(bmp->bpp, 24);
    CHECK_EQ((int)bmp->indices.size(), 128 * 128);
    CHECK_EQ((int)bmp->palette.size(), 768);
    CHECK(!bmp->rgba.empty());               // source truth kept (stand-in/compat)

    // GOLDEN PINS (deterministic: the quantizer is pure over the image bytes;
    // dither cache/state reset per call). 143 colours survive the octree
    // (maxIdx 142); the first dithered indices and the first palette entries
    // are pinned byte-for-byte.
    int maxIdx = 0;
    for (u8 i : bmp->indices)
        if (i > maxIdx) maxIdx = i;
    unsigned long h = 5381;
    for (u8 i : bmp->indices) h = (h * 33 + i) & 0xFFFFFFFFul;
    std::printf("[w4c] palettized %s: maxIdx=%d hash=%lu pal0=(%d,%d,%d)\n",
                bmp->member.c_str(), maxIdx, h, bmp->palette[0],
                bmp->palette[1], bmp->palette[2]);
    CHECK_EQ(maxIdx, 142);
    static const u8 kIdx0[8] = {129, 135, 131, 138, 131, 106, 118, 122};
    for (int i = 0; i < 8; ++i)
        CHECK_EQ((int)bmp->indices[(std::size_t)i], (int)kIdx0[i]);
    CHECK_EQ((unsigned long)h, 2456635725ul);
    CHECK_EQ((int)bmp->palette[0], 132);     // pal[0] = (132,124,84)
    CHECK_EQ((int)bmp->palette[1], 124);
    CHECK_EQ((int)bmp->palette[2], 84);
    CHECK_EQ((int)bmp->palette[3], 132);     // pal[1] = (132,116,84)
    CHECK_EQ((int)bmp->palette[4], 116);
    CHECK_EQ((int)bmp->palette[5], 84);

    // The legacy affine RGB kernel still renders the kept rgba (compat path).
    render::Surface* fb = render::SurfaceCreate(64, 64, 16);
    render::SurfaceColorFill(fb, 0, 0, 64);
    render::Vertex v[3];
    std::memset(v, 0, sizeof(v));
    v[0].screenX = 2;  v[0].screenY = 2;  v[0].u = 0.0f; v[0].v = 0.0f;
    v[1].screenX = 60; v[1].screenY = 4;  v[1].u = 1.0f; v[1].v = 0.0f;
    v[2].screenX = 6;  v[2].screenY = 60; v[2].u = 0.0f; v[2].v = 1.0f;
    int written = play::RasterTexturedTriangleAffine(fb, &v[0], &v[1], &v[2], *bmp);
    CHECK(written > 500);
    render::SurfaceDestroy(fb);
}

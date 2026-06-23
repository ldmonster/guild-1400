#include "play/real_texture_source.h"

#include "render/texture_palettize.h"

#include <cctype>
#include <cmath>

namespace guild::play {

namespace {

std::string upper(const std::string& s) {
    std::string r = s;
    for (char& c : r) c = (char)std::toupper((unsigned char)c);
    return r;
}

RealTextureSource* g_activeTextureSource = nullptr;

} // namespace

// gilde.exe 0x5F0CE4 raster addressing (render::TexelAt). UVs in the mesh repeat;
// wrap into [0,w). For texelSpace=false the UV is normalized -> multiply by width.
//
// When the bmp carries palette INDICES (8-bit sources, and 24-bit sources after
// the resolve-layer palettization — render/texture_palettize.h), the sample
// reads index -> palette: that is the engine's software surface content (for
// an 8-bit source it is byte-identical to the expanded rgba; for a palettized
// 24-bit source it is the original's <=256-colour dithered approximation, NOT
// the true-colour rgba kept for the legacy stand-in).
TexSample SampleTexel(const render::DecodedBmp& bmp, float u, float v,
                      bool texelSpace) {
    TexSample s;
    if (!bmp.ok || bmp.width <= 0 || bmp.height <= 0 || bmp.rgba.empty())
        return s;
    float fu = texelSpace ? u : u * (float)bmp.width;
    float fv = texelSpace ? v : v * (float)bmp.height;
    // Floor to integer texel, then wrap (the engine masks with (w*w-1) for square
    // power-of-two textures; we use modulo so non-power-of-two also behaves).
    long iu = (long)std::floor(fu);
    long iv = (long)std::floor(fv);
    iu %= bmp.width;  if (iu < 0) iu += bmp.width;
    iv %= bmp.height; if (iv < 0) iv += bmp.height;
    std::size_t texel = (std::size_t)iv * bmp.width + (std::size_t)iu;
    std::size_t addr = texel * 4;
    if (addr + 3 >= bmp.rgba.size()) return s;
    if (texel < bmp.indices.size() && bmp.palette.size() >= 768) {
        std::size_t p = (std::size_t)bmp.indices[texel] * 3;
        s.r = bmp.palette[p + 0];
        s.g = bmp.palette[p + 1];
        s.b = bmp.palette[p + 2];
    } else {
        s.r = bmp.rgba[addr + 0];
        s.g = bmp.rgba[addr + 1];
        s.b = bmp.rgba[addr + 2];
    }
    s.a = bmp.rgba[addr + 3];
    s.ok = true;
    return s;
}

bool RealTextureSource::Mount(shim::IFileSystem* fs, const char* archivePath) {
    fs_ = fs;   // kept for the lazy .TXS sidecar mount (Resources/Objects.BIN)
    return bin_.Mount(fs, archivePath);
}

// ---------------------------------------------------------------------------
// Process default for the active texture set (see header): 0 = the day-0 /
// season-0 (_F Frühling) set the engine applies on scene activation
// (byte_634484 = GetSeasonFromDay() @0x506df4; day 0 at new game).
// ---------------------------------------------------------------------------
namespace {
int g_defaultActiveTextureSet = 0;
} // namespace

void RealTextureSource::SetDefaultActiveTextureSet(int set) {
    g_defaultActiveTextureSet = set;
}
int RealTextureSource::DefaultActiveTextureSet() { return g_defaultActiveTextureSet; }

// ---------------------------------------------------------------------------
// Fetch + parse (and cache) the ".TXS" texture-set sidecar of a .bgf member.
// The sidecar lives next to the mesh in Resources/Objects.BIN
// ("<member-stem>.TXS"; 590 shipped — render/texture_set_table.h). Bytes come
// from the installed fetch hook, else a lazy self-mount of Objects.BIN.
// ---------------------------------------------------------------------------
const render::TextureSetTable* RealTextureSource::TxsFor(const std::string& memberKey) {
    std::string sidecar = memberKey;
    std::size_t dot = sidecar.find_last_of('.');
    std::size_t slash = sidecar.find_last_of("/\\");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        sidecar.erase(dot);
    sidecar += ".TXS";

    const std::string ck = upper(sidecar);
    auto it = txsCache_.find(ck);
    if (it != txsCache_.end())
        return it->second.ok ? &it->second : nullptr;

    std::vector<u8> raw;
    bool got = false;
    if (txsFetch_) {
        got = txsFetch_(sidecar.c_str(), raw);
    } else if (fs_) {
        if (!txsMountTried_) {
            txsMountTried_ = true;
            auto m = std::make_unique<io::ArchiveMount>();
            // caseInsensitive=true — the same mode the mesh-source mount uses
            // for Resources/Objects.BIN (VFS texture/mesh trees resolve
            // case-insensitively).
            if (m->Mount(fs_, "Resources/Objects.BIN", true))
                txsMount_ = std::move(m);
        }
        if (txsMount_)
            got = txsMount_->OpenMember(sidecar.c_str(), raw);
    }

    render::TextureSetTable& slot = txsCache_[ck];   // default: ok=false
    if (got)
        render::ParseTextureSetTable(raw.data(), raw.size(), slot);
    return slot.ok ? &slot : nullptr;
}

const render::DecodedBmp* RealTextureSource::ResolveMaterial(const char* name0) {
    return DecodeAndPalettize(name0);
}

// Decode a lookup name and — for a 24-bit source — run the reconstructed
// software palettizer over it so the decoded record carries 8-bit indices +
// a 256-colour palette exactly like an 8-bit source. This is the
// VIBE_Texture_UploadToSurface @0x5db234 software arm:
//   record -> VIBE_Texture_LoadSoftPalettize @0x5da34c
//          -> VIBE_Bmp_LoadBuffer @0x5f0ce4 (flags=7)
//          -> VIBE_Quant_BuildPalette @0x6029f0 (256 colours, dithered)
// (render/texture_palettize.h). The DecodedBmp is cached by the TextureBin,
// so each texture palettizes once — as the original palettizes per record.
const render::DecodedBmp* RealTextureSource::DecodeAndPalettize(const char* name) {
    if (!name || !*name)
        return nullptr;
    const render::DecodedBmp* bmp = bin_.Decode(name);
    if (bmp && bmp->ok && bmp->bpp > 8 && bmp->indices.empty())
        render::PalettizeDecodedBmp(*const_cast<render::DecodedBmp*>(bmp));
    return bmp;
}

// gilde.exe 0x5D2348 / 0x5F87B8 — the mesh-load material loop: each material
// resolves through VIBE_Texture_LoadByName and the slot is stamped onto every
// poly that references the material (poly +36 texId). We reproduce that
// mapping with the ENGINE'S name selection:
//
//   * The .TXS sidecar is loaded for EVERY mesh (VIBE_Mesh_LoadAndRegister
//     @0x5d32d4 -> VIBE_Mesh_LoadTextureSet @0x5d2240) and ADOPTED iff
//     setCount > 0 and namesPerSet == materialCount (the 0x5f8a0e gate; the
//     one shipped outlier gb_PALAZZO_B_0, 18 vs 17, is rejected and falls
//     back to its material names via the synthesized 1-set table).
//   * ADOPTED: the LOAD-TIME texture of material m is the sidecar's SET-0 row
//     (the 0x5f8c14 LoadByName over the name-table row, NOT the .bgf material
//     string — 349 shipped materials differ!). The .bgf strings only gate
//     whether a load happens at all: name1 preferred, else name0, else no
//     load (the 0x5f8b88/0x5f8db7 branches).
//   * SEASONS: scene activation (byte_634484 = day%4 @0x506df4) applies the
//     season as the set index to FOLIAGE nodes (pfl_/vg_/!vg_ @0x506388) via
//     VIBE_Object_SelectTextureSet @0x5b3f54: a season row that is EMPTY or
//     fails to load KEEPS the current (set-0) binding (the 0x5b4099 skip and
//     the "new not found" arm); season >= setCount keeps the set-0 state (the
//     0x5b403b gate). Building/state set applications by other callers
//     (VIBE_Object_HideFoliageByState @0x506430 upgrade-level loop,
//     HideUpgradeScaffold @0x5063f0) need per-node state and remain at their
//     owners; the set-0 baseline here IS their level-0 state.
//   * NOT adopted / no sidecar: the .bgf material name binds (name1 pref).
//
// activeSet_ == -1 is a REIMPL-ONLY debug state (bind raw material names,
// ignore sidecars); the engine has no such state — its load state is set 0.
const MaterialTextureTable* RealTextureSource::BuildTableFor(
    const char* key, const render::BgfModel& model) {
    std::string k = key ? upper(std::string(key)) : std::string("<model>");

    // Resolve the sidecar FIRST (its adoption + the effective set are part of
    // the cache identity: a season change must rebuild foliage tables).
    const render::TextureSetTable* txs = nullptr;
    int effSet = -1;                       // -1 = names only (not adopted)
    if (activeSet_ >= 0 && key) {
        const render::TextureSetTable* t = TxsFor(key);
        if (t && t->setCount > 0 &&
            t->namesPerSet == (int)model.materials.size()) {
            txs = t;
            effSet = 0;                    // the load-time set-0 state
            // VIBE_Object_HideFoliageDecor @0x506388 applies the foliage gate
            // (VIBE_Util_StrncmpN @0x5e9ee0 from BYTE 0) to the scene NODE name
            // pointer — a BARE mesh name (no path), not an archive path. Our
            // `key` is the full archive member path ("Vegetation/X/vg_*.bgf"),
            // so reproduce the binary's byte-0 prefix test against the BASENAME.
            const char* leaf = key;
            for (const char* p = key; *p; ++p)
                if (*p == '/' || *p == '\\') leaf = p + 1;
            if (render::IsFoliageMeshName(leaf) && activeSet_ < t->setCount)
                effSet = activeSet_;       // the 0x506df4/0x506388 season
        }
    }
    if (txs)
        k += "#TS" + std::to_string(effSet);

    auto it = tables_.find(k);
    if (it != tables_.end()) return it->second.get();

    auto tbl = std::make_unique<MaterialTextureTable>();
    tbl->matToTex.assign(model.materials.size(), -1);
    if (txs)
        tbl->appliedTextureSet = effSet;

    for (std::size_t mi = 0; mi < model.materials.size(); ++mi) {
        const render::BgfMaterial& mat = model.materials[mi];
        // The engine's name selector over the SCRIPT material record (the
        // in-tree models parse the .bgf script): name2 wins, else name0 — the
        // 0x5d31b1 arms of VIBE_Mesh_LoadBgfFile prefer record+128 (name2,
        // the |1-flag arm) over +0 (name0) over +64. The FAST chunk bakes the
        // same priority: its second-read string (the 0x5f8b88 preferred one)
        // IS the script's name2 (verified over the shipped pairs, e.g.
        // ob_VITRINE '', '', 'EF_GLAS_01a_1t_Spiegel' -> fast chunk
        // ('', 'EF_GLAS_01a_1t_Spiegel', '')), and the fast path never loads
        // the script's name1 (its third-read string is copied but not
        // LoadByName'd — 0x5f8e5c).
        const std::string& base = !mat.name2.empty() ? mat.name2 : mat.name0;
        if (base.empty()) continue;

        const render::DecodedBmp* bmp = nullptr;
        std::string boundRow;              // the .TXS row that bound this entry
        if (txs) {
            // Load-time state: the SET-0 row (verbatim — an empty row loads
            // nothing; no shipped row 0 is empty, 0 of 14718).
            const std::string* row0 = txs->NameFor(0, (i32)mi);
            if (row0) {
                bmp = DecodeAndPalettize(row0->c_str());
                if (bmp) boundRow = *row0;
            }
            // Season application on top (foliage only; effSet 0 == load state).
            if (effSet > 0) {
                const std::string* rowS = txs->NameFor(effSet, (i32)mi);
                if (rowS) {                // empty row -> keep current (0x5b4099)
                    const render::DecodedBmp* swap = DecodeAndPalettize(rowS->c_str());
                    if (swap) {            // not found -> keep current (0x5b420b)
                        bmp = swap;
                        boundRow = *rowS;
                        ++tbl->swappedMaterials;
                    }
                }
            }
        } else {
            bmp = DecodeAndPalettize(base.c_str());
        }
        if (bmp) {
            int texId = (int)tbl->textures.size();
            MaterialTextureTable::Entry e;
            e.materialName = mat.name0;
            e.member = bmp->member;
            e.bmp = bmp;
            e.setName = boundRow;
            tbl->textures.push_back(e);
            tbl->matToTex[mi] = texId;
            ++tbl->resolvedMaterials;
        }
    }

    tbl->polyTexId.assign(model.polygons.size(), -1);
    for (std::size_t pi = 0; pi < model.polygons.size(); ++pi) {
        int mat = model.polygons[pi].matIndex;
        int texId = -1;
        if (mat >= 0 && (std::size_t)mat < tbl->matToTex.size())
            texId = tbl->matToTex[(std::size_t)mat];
        tbl->polyTexId[pi] = texId;
        if (texId >= 0) ++tbl->texturedPolys;
        else            ++tbl->untexturedPolys;
    }

    MaterialTextureTable* ret = tbl.get();
    tables_.emplace(k, std::move(tbl));
    return ret;
}

const MaterialTextureTable* RealTextureSource::TableFor(const char* key) const {
    if (!key) return nullptr;
    auto it = tables_.find(upper(std::string(key)));
    return it == tables_.end() ? nullptr : it->second.get();
}

TexSample RealTextureSource::SamplePoly(const MaterialTextureTable& tbl,
                                        std::size_t poly, float u, float v) const {
    int texId = tbl.PolyTexId(poly);
    const render::DecodedBmp* bmp = tbl.TextureFor(texId);
    if (!bmp) return TexSample{};   // untextured
    return SampleTexel(*bmp, u, v, /*texelSpace=*/false);
}

void InstallRealTextureSource(RealTextureSource* src) { g_activeTextureSource = src; }
RealTextureSource* ActiveRealTextureSource() { return g_activeTextureSource; }

} // namespace guild::play

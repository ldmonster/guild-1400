#pragma once
// =============================================================================
// guild::play — REAL TEXTURE SOURCE (PLAYABLE_PLAN P2/B3 visual fidelity).
//
// Wave 27/28 render AUGSBURG meshes as untextured geometry. This module closes
// the gap: it resolves a render::BgfModel's per-material name0 to a decoded BMP
// from Resources/Textures.BIN (render::TextureBin), builds a per-POLYGON texture
// id table (the engine's poly +36 texId, set by VIBE_Texture_LoadByName called
// from VIBE_Mesh_LoadBgfFile @0x5D2348), and exposes a TEXTURED-SAMPLE path the
// rasterizer can use: given a poly index + a barycentric/UV coordinate it returns
// the texel RGBA (UV * texture, with the engine's (V*w+U)&mask wrap addressing,
// render/texture.h TexelAt).
//
// GROUNDING
// ---------------------------------------------------------------------------
//   * VIBE_Mesh_LoadBgfFile @0x5D2348 walks each material and calls
//     VIBE_Texture_LoadByName(material.name0) -> a texture record; the resolved
//     record's slot index is stored on every poly that uses that material
//     (poly +36). Polys whose material has no resolvable BMP keep texId == -1
//     (untextured -> the flat/shaded path).
//   * The per-corner UVs live on the poly (uv0[k] = U, uv1[k] = V for corner k,
//     render/bgf_loader.h), already carried onto each Vertex.u/v by BuildGeometry.
//   * The sampler is render::TexelAt: addr = ((V<<widthShift)+U) & texelMask.
//
// This module is ADDITIVE and wires NOTHING by itself. It does NOT edit
// object_mesh_render.* / real_mesh_source.* / agf_loader.*; it consumes their
// public BgfModel + the public render::TextureBin. An installable process-global
// hook (with an inert default that returns null / "untextured") lets a renderer
// or test consult the active source; the default leaves meshes untextured.
// =============================================================================
#include "guild/common/types.h"
#include "io/archive_mount.h"          // io::ArchiveMount (.TXS sidecar lookup)
#include "render/agf_loader.h"         // render::BgfModel
#include "render/bgf_loader.h"         // render::BgfModel / BgfMaterial / BgfPolygon
#include "render/texture_bin.h"        // render::TextureBin / DecodedBmp
#include "render/texture_set_table.h"  // render::TextureSetTable (.TXS)
#include "shim/IFileSystem.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace guild::play {

// A single texel sample result.
struct TexSample {
    bool ok = false;     // false -> untextured (no texture bound for that poly)
    u8 r = 0, g = 0, b = 0, a = 0;
};

// ---------------------------------------------------------------------------
// MaterialTextureTable — for one BgfModel, the resolved texture per material and
// the per-polygon texId (index into `textures`, or -1 when untextured).
// ---------------------------------------------------------------------------
struct MaterialTextureTable {
    // One resolved texture (a decoded BMP) per UNIQUE material slot used.
    struct Entry {
        std::string materialName;          // BgfMaterial.name0
        std::string member;                // resolved Textures.BIN member ("" if none)
        const render::DecodedBmp* bmp = nullptr;  // decoded pixels (null if unresolved)
        std::string setName;               // the .TXS row that bound this entry
                                           // (set-0 load row or the season row;
                                           //  "" = bound by the material name —
                                           //  no sidecar adopted)
    };
    std::vector<Entry> textures;           // texId -> texture entry
    std::vector<int>   matToTex;           // material index -> texId (or -1)
    std::vector<int>   polyTexId;          // polygon index -> texId (or -1)

    int texturedPolys = 0;                 // polys with a resolved texture
    int untexturedPolys = 0;               // polys with no texture (texId == -1)
    int resolvedMaterials = 0;             // materials that resolved to a BMP

    // Texture-set state (render/texture_set_table.h): the EFFECTIVE set this
    // table binds (-1 = sidecar not adopted / debug-disabled; 0 = the engine
    // load state — set-0 rows; >0 = a foliage season set over that baseline)
    // and how many materials a season row actually swapped (the 0x5b3f54
    // replacements beyond the set-0 baseline).
    int appliedTextureSet = -1;
    int swappedMaterials = 0;

    int PolyTexId(std::size_t poly) const {
        return poly < polyTexId.size() ? polyTexId[poly] : -1;
    }
    const render::DecodedBmp* TextureFor(int texId) const {
        return (texId >= 0 && (std::size_t)texId < textures.size())
                   ? textures[(std::size_t)texId].bmp : nullptr;
    }
};

// ---------------------------------------------------------------------------
// RealTextureSource — owns a mounted TextureBin and per-model texture tables.
// ---------------------------------------------------------------------------
class RealTextureSource {
public:
    RealTextureSource() = default;

    // Mount Resources/Textures.BIN through `fs`. Returns true on success.
    bool Mount(shim::IFileSystem* fs,
               const char* archivePath = "Resources/Textures.BIN");

    bool mounted() const { return bin_.mounted(); }
    render::TextureBin& bin() { return bin_; }

    // Resolve a bare material name to a decoded texture (cached in the
    // TextureBin). A 24-bit source is palettized in place on first decode —
    // the VIBE_Texture_LoadSoftPalettize @0x5da34c software arm through the
    // reconstructed VIBE_Quant_BuildPalette @0x6029f0 (render/
    // texture_palettize.h) — so it carries 8-bit indices + a 256-colour
    // palette exactly like an 8-bit source. Returns null if the name has no
    // matching BMP.
    const render::DecodedBmp* ResolveMaterial(const char* name0);

    // Build (and cache by `key`) the per-poly texId table for `model`: walk each
    // material, resolve name0 -> texture, then map every polygon's matIndex to a
    // texId. Returns the table (owned by the source).
    const MaterialTextureTable* BuildTableFor(const char* key,
                                              const render::BgfModel& model);

    // Look up a previously-built table.
    const MaterialTextureTable* TableFor(const char* key) const;

    // Sample the texture bound to polygon `poly` of the table at normalized UV
    // (u,v in texel-or-normalized space; see SampleTexel). Returns ok=false when
    // the poly is untextured.
    TexSample SamplePoly(const MaterialTextureTable& tbl, std::size_t poly,
                         float u, float v) const;

    // -----------------------------------------------------------------------
    // TEXTURE SETS (.TXS) — the engine binds through the sidecar's name table
    // (verified against the binary; see render/texture_set_table.h):
    //
    //   * VIBE_Mesh_LoadAndRegister @0x5d32d4 loads "<member>.TXS" via
    //     VIBE_Mesh_LoadTextureSet @0x5d2240 for EVERY mesh; the table is
    //     ADOPTED iff setCount > 0 and namesPerSet == materialCount
    //     (VIBE_Model_LoadFastChunk @0x5f8a0e; reject -> a synthesized 1-set
    //     table from the material names).
    //   * ADOPTED: the LOAD-TIME texture of material m is the SET-0 row
    //     (0x5f8c14) — NOT the .bgf material string (349 shipped materials
    //     differ). The .bgf strings only select the load name when no table
    //     is adopted (script name2 preferred, else name0 — 0x5d31b1; the
    //     fast chunk's preferred second string IS the script's name2).
    //   * Scene activation applies the SEASON (byte_634484 = day%4 @0x506df4)
    //     as the set index to FOLIAGE nodes (pfl_/vg_/!vg_ @0x506388) via
    //     VIBE_Object_SelectTextureSet @0x5b3f54: empty/unloadable season row
    //     keeps the current (set-0) binding; season >= setCount keeps the
    //     set-0 state (the 0x5b403b gate).
    //
    // The sidecar bytes come from the installed fetch hook, or — when none is
    // installed — from a lazy self-mount of "Resources/Objects.BIN" through
    // the IFileSystem given to Mount() (the archive the .bgf members live in).
    // -----------------------------------------------------------------------
    using TxsFetch = std::function<bool(const char* member, std::vector<u8>& out)>;
    void SetTxsFetch(TxsFetch fn) { txsFetch_ = std::move(fn); }

    // Active SEASON set for table builds (byte_634484). Foliage members bind
    // their season rows over the set-0 baseline; every other adopted member
    // binds its set-0 rows (the engine load state). -1 is a REIMPL-ONLY debug
    // state (ignore sidecars, bind raw material names — the engine has no
    // such state). The instance default is the PROCESS default below.
    void SetActiveTextureSet(int set) { activeSet_ = set; }
    int  activeTextureSet() const { return activeSet_; }

    // Process-wide default for newly constructed sources. Initialised to 0 —
    // the new game starts at day 0 -> season 0 -> set 0 (_F, Frühling), so a
    // fresh session's city binds carry the engine's day-0 steady state. A
    // session advancing days re-applies via SetActiveTextureSet(day % 4)
    // (render::SeasonTextureSetFromDay) + a rebind.
    static void SetDefaultActiveTextureSet(int set);
    static int  DefaultActiveTextureSet();

private:
    // Fetch + parse (and cache) the ".TXS" sidecar for a .bgf member key.
    // Returns null when the member has no parseable sidecar.
    const render::TextureSetTable* TxsFor(const std::string& memberKey);

    // Decode + (for a 24-bit source) palettize in place — see ResolveMaterial.
    const render::DecodedBmp* DecodeAndPalettize(const char* name);

    render::TextureBin bin_;
    std::map<std::string, std::unique_ptr<MaterialTextureTable>> tables_;

    shim::IFileSystem* fs_ = nullptr;             // from Mount(); sidecar reads
    TxsFetch txsFetch_;                           // optional sidecar provider
    std::unique_ptr<io::ArchiveMount> txsMount_;  // lazy Objects.BIN self-mount
    bool txsMountTried_ = false;
    std::map<std::string, render::TextureSetTable> txsCache_;  // by UPPER key
    int activeSet_ = DefaultActiveTextureSet();
};

// ---------------------------------------------------------------------------
// SampleTexel — the engine's affine texel fetch over a DecodedBmp. The mesh UVs
// are stored as REPEATING coordinates (values can exceed 1.0, e.g. 1.402); the
// engine wraps them with (V*w+U) & ((w-1)|(w*w-1)) (render::TexelAt). We mirror
// that: take the integer texel coords modulo w (square power-of-two textures),
// then fetch from the decoded RGBA. `u`/`v` are in TEXEL units when texelSpace is
// true (the on-disk UVs are already scaled by the texture width in the engine? no
// — they are normalized-ish repeats; we multiply by width here). For a normalized
// UV in [0,1) pass texelSpace=false (default) and it is multiplied by width.
// ---------------------------------------------------------------------------
TexSample SampleTexel(const render::DecodedBmp& bmp, float u, float v,
                      bool texelSpace = false);

// ---------------------------------------------------------------------------
// Installable process-global hook (inert default: null). A renderer/test installs
// the active source so a textured draw path can consult it; the default leaves
// everything untextured (Wave 27/28 behavior preserved).
// ---------------------------------------------------------------------------
void InstallRealTextureSource(RealTextureSource* src);
RealTextureSource* ActiveRealTextureSource();

} // namespace guild::play

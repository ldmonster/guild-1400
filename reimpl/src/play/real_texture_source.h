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
#include "render/agf_loader.h"      // render::BgfModel
#include "render/bgf_loader.h"      // render::BgfModel / BgfMaterial / BgfPolygon
#include "render/texture_bin.h"     // render::TextureBin / DecodedBmp
#include "shim/IFileSystem.h"

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
    };
    std::vector<Entry> textures;           // texId -> texture entry
    std::vector<int>   matToTex;           // material index -> texId (or -1)
    std::vector<int>   polyTexId;          // polygon index -> texId (or -1)

    int texturedPolys = 0;                 // polys with a resolved texture
    int untexturedPolys = 0;               // polys with no texture (texId == -1)
    int resolvedMaterials = 0;             // materials whose name0 resolved to a BMP

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

    // Resolve a bare material name0 to a decoded texture (cached in the TextureBin).
    // Returns null if the name has no matching BMP.
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

private:
    render::TextureBin bin_;
    std::map<std::string, std::unique_ptr<MaterialTextureTable>> tables_;
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

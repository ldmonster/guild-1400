#include "play/real_texture_source.h"

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
    std::size_t addr = ((std::size_t)iv * bmp.width + (std::size_t)iu) * 4;
    if (addr + 3 >= bmp.rgba.size()) return s;
    s.r = bmp.rgba[addr + 0];
    s.g = bmp.rgba[addr + 1];
    s.b = bmp.rgba[addr + 2];
    s.a = bmp.rgba[addr + 3];
    s.ok = true;
    return s;
}

bool RealTextureSource::Mount(shim::IFileSystem* fs, const char* archivePath) {
    return bin_.Mount(fs, archivePath);
}

const render::DecodedBmp* RealTextureSource::ResolveMaterial(const char* name0) {
    if (!name0 || !*name0) return nullptr;
    return bin_.Decode(name0);
}

// gilde.exe 0x5D2348 — VIBE_Mesh_LoadBgfFile material loop: each material's name0
// goes through VIBE_Texture_LoadByName; the resolved slot is stamped onto every
// poly that references that material (poly +36 texId). We reproduce that mapping.
const MaterialTextureTable* RealTextureSource::BuildTableFor(
    const char* key, const render::BgfModel& model) {
    std::string k = key ? upper(std::string(key)) : std::string("<model>");
    auto it = tables_.find(k);
    if (it != tables_.end()) return it->second.get();

    auto tbl = std::make_unique<MaterialTextureTable>();
    tbl->matToTex.assign(model.materials.size(), -1);

    for (std::size_t mi = 0; mi < model.materials.size(); ++mi) {
        const std::string& name0 = model.materials[mi].name0;
        if (name0.empty()) continue;
        const render::DecodedBmp* bmp = bin_.Decode(name0.c_str());
        // Allocate a texId for this material (even if unresolved, so multiple
        // polys of an unresolved material share one entry — but mark texId -1 in
        // polyTexId so the renderer treats it as untextured).
        if (bmp) {
            int texId = (int)tbl->textures.size();
            MaterialTextureTable::Entry e;
            e.materialName = name0;
            e.member = bmp->member;
            e.bmp = bmp;
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

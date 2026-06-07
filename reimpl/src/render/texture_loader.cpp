#include "render/texture_loader.h"
#include "render/texture_asset.h"

#include <string>

namespace guild::render {

std::vector<TexLoadRequest> g_texLoadRequests;
int g_texNextSlot = 100;
int g_texFixed = -1;

namespace {
// The active VFS-backed cache (null until a caller mounts a texture archive).
TextureAssetCache* g_texCache = nullptr;
}

void TextureLoaderSetCache(TextureAssetCache* cache) { g_texCache = cache; }

// gilde.exe 0x5da714 — VIBE_Texture_LoadByName (name->slot front-end).
// Records the request, then either:
//   * g_texFixed >= 0 : returns that constant slot (test override), or
//   * a cache is installed : builds "*"+name+".BMP" (VIBE_Texture_BuildBmpPath),
//     opens it through the VFS, decodes the BMP into a record, returns the slot, or
//   * no cache : returns the legacy auto-incrementing slot id.
int TextureLoadByName(const char* name, u32 f0, u32 f1, u32 f2) {
    const std::string n = name ? name : "";
    g_texLoadRequests.push_back({n, f0, f1, f2});

    if (g_texFixed >= 0)
        return g_texFixed;

    if (g_texCache) {
        // VIBE_Texture_BuildBmpPath: "*" + name + ".BMP" (asc_62959C + name + aBmp).
        std::string path = "*" + n + ".BMP";
        int slot = g_texCache->LoadByName(path.c_str(), n);
        if (slot >= 0)
            return slot;
        // Fall through to the legacy slot id when the BMP is absent (the mesh can
        // still load with an unresolved texture, matching the engine's -1/0 path).
    }

    return g_texNextSlot++;
}

} // namespace guild::render

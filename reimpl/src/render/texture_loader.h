#pragma once
#include "guild/common/types.h"
#include <string>
#include <vector>

// Texture-name -> slot-id bridge used by the mesh loader (mesh_load.cpp) and the
// terrain/material code. The original VIBE_Texture_LoadByName @0x5da714 builds the
// "*"+name+".BMP" path, opens it through the VFS, decodes the BMP, and stores the
// 8-bit texels in a texture-record slot. That real VFS+decode path now lives in
// texture_asset.{h,cpp}; this header is the thin name->slot front-end the mesh
// loader calls. It still records every request (so callers/tests can observe what
// was asked for) and honours the g_texFixed test override.
namespace guild::render {

class TextureAssetCache;  // texture_asset.h — the real VFS+decode slot manager.

struct TexLoadRequest {
    std::string name;
    u32 f0, f1, f2;
};

extern std::vector<TexLoadRequest> g_texLoadRequests; // recorded load requests
extern int g_texNextSlot;                             // next auto-assigned slot
extern int g_texFixed;                                // >=0 forces a constant slot

// Install the active VFS-backed cache the bridge loads through. When set (and
// g_texFixed < 0), TextureLoadByName builds "*"+name+".BMP" and drives the real
// decode via this cache, returning the resulting slot index. When null, the bridge
// falls back to the legacy auto-incrementing slot id (so the geometry-only mesh
// tests that don't mount a texture VFS keep working).
void TextureLoaderSetCache(TextureAssetCache* cache);

// Resolve a texture name to a slot id. Records the request. With a cache installed
// this is the real VFS+BMP-decode path; g_texFixed >= 0 still forces a constant.
int TextureLoadByName(const char* name, u32 f0, u32 f1, u32 f2);

} // namespace guild::render

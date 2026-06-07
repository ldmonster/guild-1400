#include "render/texture_asset.h"

#include "render/bmp.h"
#include "render/mesh_asset.h"  // VfsSlurp (the shared VFS open+read+close helper)

#include <cstring>

// =============================================================================
// guild::render — VIBE_Texture_LoadByName software body: VFS open + BMP decode
// into the 8-bit texel buffer, plus the by-name slot cache. See texture_asset.h
// for the original-function map. The Texture record + TextureSet are REUSED from
// texture.h/.cpp; BmpLoadBuffer is REUSED from bmp.cpp.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// gilde.exe 0x5F0C10 — VIBE_Bmp_ReadHeaderInfo (square check). The loader rejects
// a texture whose width != height (v77 == v78 test in LoadByName).
// ---------------------------------------------------------------------------
bool TextureReadSquareInfo(const std::vector<u8>& bmp, int& side) {
    side = 0;
    BmpInfo info = BmpReadHeaderInfo(bmp);
    if (!info.ok)
        return false;
    if (info.width != info.height || info.width <= 0)
        return false;
    side = info.width;
    return true;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5DB724 (record geometry) + 0x5DA34C (VIBE_Texture_LoadSoftPalettize).
// Decode the BMP into `rec.texels` as w*w 8-bit palette indices and capture the
// source palette. The original then re-quantizes the used colours into a HiColTab
// (a2[18] = +72) and rewrites the indices to the compacted set; here we keep the
// raw 8-bit indices + the full source palette (the rasterizer addresses texels
// the same way — TexelAt() — and the palette is available for RGB reconciliation).
// ---------------------------------------------------------------------------
TextureDecode DecodeBmpIntoTexture(const std::vector<u8>& bmp, Texture& rec) {
    TextureDecode dec;

    int side = 0;
    if (!TextureReadSquareInfo(bmp, side))
        return dec;

    // Record geometry: width at +116/+120, texelMask at +88, w*w texel buffer +68.
    TextureSetSize(rec, side);

    // VIBE_Bmp_LoadBuffer: decode 8-bit indices (wantBpp==8) + the source palette.
    dec.palette.assign(256 * 3, 0);
    int w = 0, h = 0;
    std::vector<u8> idx = BmpLoadBuffer(bmp, 8, w, h, dec.palette.data());
    if (idx.empty() || w != side || h != side)
        return dec;

    // Store the indices into the record's texel buffer (+68). TextureSetSize
    // already sized texels to w*w; copy verbatim (top-down, as BmpLoadBuffer
    // returns — the rasterizer's V<<shift+U addressing is top-down).
    if (idx.size() != rec.texels.size())
        return dec;
    std::memcpy(rec.texels.data(), idx.data(), idx.size());

    // +72 palette source pointer (HiColTab in the original). We keep the decoded
    // palette in `dec` and point the record at it for the duration of the load.
    rec.palette = dec.palette.data();

    dec.ok = true;
    dec.width = side;
    return dec;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5DA714 (software branch) — load-or-find by name.
// ---------------------------------------------------------------------------
int TextureAssetCache::LoadByName(const char* path, const std::string& name) {
    // FindActive(name): a hit bumps the refcount and returns the existing slot
    // (mirrors the IncrementRefCount return-early in LoadByName).
    int hit = set_.FindActive(name);
    if (hit >= 0) {
        set_.IncrementRef(hit);
        return hit;
    }

    // Open + slurp the BMP through the VFS, decode into a freshly claimed slot.
    std::vector<u8> bmp;
    if (!path || !VfsSlurp(path, bmp))
        return -1;

    int side = 0;
    if (!TextureReadSquareInfo(bmp, side))
        return -1;

    int slot = set_.CreateRecord(name, side);  // claims slot, refCount=1, sets size
    if (slot < 0)
        return -1;

    Texture& rec = set_.records[(std::size_t)slot];
    TextureDecode dec = DecodeBmpIntoTexture(bmp, rec);
    if (!dec.ok) {
        set_.ReleaseEntry(slot);  // undo the claim on a decode failure
        return -1;
    }
    // The decoded palette is owned by `dec` (a local); persist it on the record so
    // it outlives this call. Stash it in the record's own storage by re-pointing to
    // a record-owned copy.
    rec.paletteStore = dec.palette;          // record-owned copy
    rec.palette = rec.paletteStore.data();
    return slot;
}

} // namespace guild::render

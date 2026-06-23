#include "render/texture_asset.h"

#include "render/bmp.h"
#include "render/mesh_asset.h"       // VfsSlurp (the shared VFS open+read+close helper)
#include "render/texture_bin.h"      // DecodeBmpBuffer (8-bit + 24-bit decode)
#include "render/texture_palettize.h"// PalettizeDecodedBmp (the @0x5da34c 24-bit arm)

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

    // 24-bit source arm (VIBE_Texture_LoadSoftPalettize @0x5da34c): an 8-bit
    // decode of a 24-bit BMP yields nothing (BmpLoadBuffer(8) does not quantize),
    // so run the SOFTWARE PALETTIZER the engine runs — VIBE_Quant_BuildPalette
    // @0x6029f0 (256 colours, dithered) — over the decoded RGB, producing the same
    // 8-bit indices + 256-colour palette an 8-bit source carries. The floor slot
    // BMPs (_DYNAMIC/Boden/*.bmp) are 24-bit, so this is the path they take.
    if (idx.empty()) {
        DecodedBmp d = DecodeBmpBuffer(bmp);
        if (d.ok && d.width == side && d.height == side && d.bpp > 8 &&
            PalettizeDecodedBmp(d) && (int)d.indices.size() == side * side &&
            d.palette.size() >= 256 * 3) {
            idx = std::move(d.indices);
            std::memcpy(dec.palette.data(), d.palette.data(), 256 * 3);
            w = side;
            h = side;
        }
    }
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

    // Open + slurp the BMP. When a BmpFetch override is installed (the archive
    // resolve the engine's VFS "*"+name+".BMP" wildcard performs over the mounted
    // .BIN containers), use it by the bare slot name; otherwise the loose-file VFS
    // slurp of `path`. Either way the same bytes feed the BMP decode below.
    std::vector<u8> bmp;
    bool got = false;
    if (bmpFetch_)
        got = bmpFetch_(name, bmp);
    if (!got)
        got = (path && VfsSlurp(path, bmp));
    if (!got)
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

#pragma once
#include "guild/common/types.h"
#include "render/texture.h"

#include <functional>
#include <string>
#include <vector>

// =============================================================================
// guild::render — the REAL VFS + BMP-decode body behind VIBE_Texture_LoadByName
// (@0x5DA714) and its software texel path.
//
// VIBE_Texture_LoadByName's software branch does:
//   0x5D97E8  VIBE_Texture_BuildBmpPath      — "*" + name + ".BMP" -> resolve.
//   0x5F0C10  VIBE_Bmp_ReadHeaderInfo        — VfsOpenFile(path,"rb") + seek 0xE +
//                                              read 40-byte BITMAPINFOHEADER ->
//                                              width/height/bpp. Requires w == h
//                                              (square) for the texture to load.
//   0x5DB724  VIBE_Texture_CreateRecord      — claim a slot, name (StrNCopyPad 63),
//                                              +116/+120 = w (mip/base width),
//                                              +88 = (w-1)|(w*w-1) texelMask,
//                                              +68 = AllocDebug(w*w) texel buffer.
//   0x5DA34C  VIBE_Texture_LoadSoftPalettize — VIBE_Bmp_LoadBuffer decodes the BMP
//                                              into w*w 8-bit indices (+68 texels)
//                                              and builds the HiColTab palette
//                                              (+72) from the colours actually used.
//
// This module supplies that decode (DecodeBmpIntoTexture) and the by-name cache
// (a TextureSet slot array; FindActive reuse + IncrementRef on a hit, exactly as
// LoadByName does). The texture record layout (Texture, 128-byte stride) and the
// slot manager (TextureSet) are REUSED from texture.h — this file does NOT
// redefine them (ODR); it only adds the VFS-open + decode + cache front-end.
//
// The thin name->slot bridge VIBE_Texture_LoadByName the mesh loader calls lives
// in texture_loader.cpp (signature + g_texFixed test override kept); its stub body
// is replaced to call into DecodeBmpIntoTexture via the active TextureSet here.
// =============================================================================
namespace guild::render {

// Result of decoding a BMP into a Texture record's software texel buffer.
struct TextureDecode {
    bool ok = false;
    int  width = 0;            // square texture side (w == h)
    std::vector<u8> palette;   // 256*3 RGB triples (the source palette, +72 source)
    // texels live in the Texture record (record.texels); palette is exposed here so
    // a caller/test can reconcile an index back to its RGB.
};

// gilde.exe 0x5F0C10 — read just width/height/bpp from a BMP byte buffer that was
// slurped from the VFS. (BmpReadHeaderInfo in bmp.h takes the same buffer; this is
// a convenience returning a square-check too.) Returns false on a non-square or
// unsupported BMP.
bool TextureReadSquareInfo(const std::vector<u8>& bmp, int& side);

// gilde.exe 0x5DB724 + 0x5DA34C — initialise `rec` from a BMP byte buffer:
//   * validate the BMP is square (w == h),
//   * TextureSetSize(rec, w) (allocates w*w texels, sets mask/shift/widths),
//   * VIBE_Bmp_LoadBuffer the BMP into rec.texels (8-bit indices),
//   * record the source palette into `dec.palette`.
// Returns a TextureDecode with ok=true on success.
TextureDecode DecodeBmpIntoTexture(const std::vector<u8>& bmp, Texture& rec);

// ---------------------------------------------------------------------------
// VFS-backed by-name texture cache. Models the slot scan + reuse of
// VIBE_Texture_LoadByName: FindActive(name) -> IncrementRef on a hit; otherwise
// open "<path>" through the VFS, decode it into a freshly claimed record, and
// return the slot index. Re-loading the same name returns the SAME slot.
// ---------------------------------------------------------------------------
class TextureAssetCache {
public:
    explicit TextureAssetCache(int capacity) : set_(capacity) {}

    // gilde.exe 0x5DA714 (software branch) — load-or-find by name. `path` is the
    // VFS path to the BMP (the original builds "*"+name+".BMP"); `name` is what is
    // stored/compared in the record. Returns the slot index, or -1 on failure.
    int LoadByName(const char* path, const std::string& name);

    // BMP-bytes source override (ADDITIVE; default unset -> the VFS slurp below).
    // The engine's VIBE_Vfs_ResolveAndBuildPath @0x4500a0 search resolves the
    // "*"+name+".BMP" wildcard against EVERY mounted resource container, including
    // the .BIN archives (Textures.BIN holds the floor slot BMPs as members, e.g.
    // "_DYNAMIC/Boden/Wiese.bmp"). A loose-file VfsSlurp of "*WIESE.BMP" can't see
    // an archive member, so this hook lets a caller (CityView3D) supply the member
    // bytes by the bare slot name — exactly the bytes the engine's VFS resolve
    // would have handed VIBE_Bmp_LoadBuffer. When set, LoadByName uses it instead
    // of VfsSlurp; the rest of the decode path (square check + DecodeBmpIntoTexture)
    // is unchanged. Unset -> byte-identical to before.
    using BmpFetch = std::function<bool(const std::string& name, std::vector<u8>& out)>;
    void SetBmpFetch(BmpFetch fn) { bmpFetch_ = std::move(fn); }

    // Direct access for tests / the mesh loader bridge.
    TextureSet& set() { return set_; }
    const TextureSet& set() const { return set_; }
    const Texture* record(int slot) const {
        return (slot >= 0 && (std::size_t)slot < set_.records.size())
                   ? &set_.records[(std::size_t)slot] : nullptr;
    }

private:
    TextureSet set_;
    BmpFetch   bmpFetch_;
};

} // namespace guild::render

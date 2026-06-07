#pragma once
#include "guild/common/types.h"
#include <string>
#include <vector>

// =============================================================================
// guild::render — software texture record + the texel buffer the rasterizer
// addresses. Faithful 1:1 reconstruction of the gilde.exe ts_texture.c record
// (a 128-byte stride record: VIBE_Texture_UploadToSurface iterates `v14 += 128`
// over `dword_1406A80` records based at `dword_1406A84`).
//
// Reconstructed from:
//   0x5db724  VIBE_Texture_CreateRecord  (record init / field layout / mask)
//   0x5db234  VIBE_Texture_UploadToSurface (128-byte stride, +68/+72/+116/+124)
//   0x5da244  VIBE_Texture_FindActiveRecord (slot scan by name)
//   0x5da2e4  VIBE_Texture_IncrementRefCount
//   raster.cpp FillSpanTextured: texel addr = ((V<<widthShift)+U) & texelMask
//
// THE TEXEL BUFFER (reconcile with raster.cpp)
// ---------------------------------------------------------------------------
// The software rasterizer fetches one 8-bit palette index per texel as:
//     addr = ((V_int << widthShift) + U_int) & texelMask
//     idx  = texBase[addr]
//     pixel = palBase[idx]
// where texBase is this record's `texels` (+68 in the original), widthShift =
// log2(width) (folded row stride), texelMask = `(w-1) | (w*w-1)` exactly as
// VIBE_Texture_CreateRecord computes it at +88 ((*((_DWORD*)v21-7))). For a
// power-of-two square texture of side w, w*w-1 has all low 2*log2(w) bits set,
// so the mask is `w*w - 1` — wrapping (V*w+U) into the square. TexelAt()
// performs that identical addressing so a test can tie a UV fetch to the
// rasterizer's inner loop.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Texture record — the 128-byte ts_texture.c record (offsets are ORIGINAL
// 32-bit byte offsets). Only the fields the software path reads are modelled;
// the DDraw surface pointers (+96/+100) are the present layer and omitted.
// ---------------------------------------------------------------------------
struct Texture {
    std::string name;       // +0    63-char name (first byte 42 '*' = special)
    i32  refCount = 0;      // +64   >0 == active/valid slot
    std::vector<u8> texels; // +68   w*w 8-bit palette-index buffer (texBase)
    void* palette = nullptr;// +72   HiColTab/palette block (palBase source)
    i32  slot = 0;          // +76   group/clone slot index
    i32  paletteId = -1;    // +80   palette id (-1 = unset)
    u32  texelMask = 0;     // +88   (w-1) | (w*w-1)  — raster wrap mask
    i32  refField = 0;      // +92   secondary ref/use counter
    u8   flags = 0;         // +104  bit0 mip, bit1 clone/tile, bit2 stretch,
                            //       bit3 no-transparency, bit6 colour-key-off
    i32  mipLevels = 0;     // +112  number of mip levels
    u8   isMip = 0;         // +113  this record is a generated mip
    i32  mipWidth = 0;      // +116  current width (= baseWidth >> shift)
    i32  baseWidth = 0;     // +120  full-resolution width
    u8   shift = 0;         // +124  byte_64A350 mip shift applied
    u8   loadShift = 0;     // +125  shift used at load time

    // Reconstruction helper: log2(mipWidth) — the raster widthShift. Derived in
    // SetWidth(); the original folded it into the patched span immediate
    // (byte_1407A91). Kept here so TexelAt() and the rasterizer agree.
    u8   widthShift = 0;

    // Reconstruction helper: record-owned backing for the +72 palette. The
    // original +72 pointed at a HiColTab block allocated elsewhere; here the
    // by-name BMP-decode path (texture_asset.cpp) stores the decoded source
    // palette here and points `palette` at it so it outlives the load call.
    std::vector<u8> paletteStore;  // 256*3 RGB triples when set by DecodeBmpIntoTexture
};

// gilde.exe 0x5db724 — texel index mask: (w-1) | (w*w-1). For power-of-two w
// this equals w*w-1.
inline u32 TexelMask(i32 width) {
    return (u32)((width - 1) | (width * width - 1));
}

// log2 of a power-of-two width (the raster widthShift). 0 for width<=1.
inline u8 WidthShift(i32 width) {
    u8 s = 0;
    while ((1 << (s + 1)) <= width) ++s;
    return s;
}

// gilde.exe 0x5db724 (partial) — initialise a record's geometry from a width:
// sets texelMask, baseWidth/mipWidth, widthShift, allocates w*w texels.
void TextureSetSize(Texture& t, i32 width);

// Affine texel fetch — the exact addressing the rasterizer's inner span loop
// uses. uInt/vInt are the integer parts of the 16.16 U/V accumulators.
//   addr = ((vInt << widthShift) + uInt) & texelMask ; return texels[addr]
inline u8 TexelAt(const Texture& t, i32 uInt, i32 vInt) {
    u32 addr = (((u32)vInt) << t.widthShift) + (u32)uInt;
    addr &= t.texelMask;
    return t.texels[addr];
}

// Convenience: fetch at normalised UV in [0,1) (multiplies by width, truncates).
inline u8 TexelAtUV(const Texture& t, float u, float v) {
    i32 uInt = (i32)(u * (float)t.mipWidth);
    i32 vInt = (i32)(v * (float)t.mipWidth);
    return TexelAt(t, uInt, vInt);
}

// ---------------------------------------------------------------------------
// Texture slot manager — the global record array (dword_1406A84 / count
// dword_1406A80). Models VIBE_Texture_FindActiveRecord (find by name) /
// CreateRecord (claim a free slot) / IncrementRefCount / ReleaseEntry.
// ---------------------------------------------------------------------------
struct TextureSet {
    std::vector<Texture> records;   // dword_1406A84 base, 128-byte stride

    explicit TextureSet(int capacity) : records((size_t)capacity) {}

    // gilde.exe 0x5da244 — find the active record matching `name` (refCount>0).
    // Returns index or -1.
    int FindActive(const std::string& name) const;

    // First free slot (refCount==0) or -1.
    int FindFree() const;

    // gilde.exe 0x5db724 — claim a free slot, init name/size, refCount=1.
    // Returns the new record index or -1 if full.
    int CreateRecord(const std::string& name, i32 width);

    // gilde.exe 0x5da2e4 — VIBE_Texture_IncrementRefCount. Bumps refCount.
    void IncrementRef(int idx);

    // gilde.exe 0x5d9a0c — VIBE_Texture_ReleaseEntry. Decrement; free at 0.
    void ReleaseEntry(int idx);
};

} // namespace guild::render

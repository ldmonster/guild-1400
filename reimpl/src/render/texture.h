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
    u8   flags = 0;         // +104  bit assignments CONFIRMED line-for-line from
                            //   the FRESH-LOAD writer VIBE_Texture_LoadByName
                            //   @0x5da714 and the consumer
                            //   VIBE_Texture_UploadToSurface @0x5db234:
                            //   bit0 (1):  a4&1 selector (call arg low bit;
                            //              0x5da799 v91 = a4 & 1)
                            //   bit1 (2):  alias/clone record — resolve via base
                            //              + (+76 << 7) (0x5db258/0x5db265). Set
                            //              to 1 on a fresh load (0x5daa01).
                            //   bit2 (4):  TRANSPARENCY / COLOUR-KEY flag. Set on
                            //              a fresh BMP load at 0x5dad52 when
                            //              `!dword_140809C && v79(bmp bpp) > 8`
                            //              (a >8bpp/24-bit source, global gate
                            //              off): `*v59 = (4 * v61) | v62`.
                            //              UploadToSurface @0x5db319 tests
                            //              `(*(rec+104) & 4)` to pick the
                            //              TRANSPARENT surface descriptor
                            //              &unk_14080C4 over the opaque
                            //              &unk_14080A0 for LoadAndStretchTexture
                            //              @0x5dea50 — a DDraw colour-key on
                            //              palette entry 0 (index-0 transparent).
                            //              THIS is the predicate the masked span
                            //              (FillSpanTexturedMasked @0x5F721A,
                            //              index 0 transparent) is the faithful
                            //              software equivalent of. See
                            //              TextureIsColourKeyed() below.
                            //   bit3 (8):  name-contains-"_NM" flag — set at
                            //              0x5dad75 from
                            //              loc_5CB930(rec+104, "_NM"). In
                            //              UploadToSurface @0x5db2f4 it ONLY
                            //              selects v9 = (bit3)?0:dword_64A1FC, the
                            //              MIP-BIAS arg 5 of LoadAndStretchTexture
                            //              @0x5dea50 — NOT a colour key. (The
                            //              wave-3/4 code wrongly used this bit as
                            //              the colour-key trigger; corrected in
                            //              wave-5, see progress/colourkey-wave5.md.)
                            //   bit5 (20): 8-bit-indexed source indicator (cleared
                            //              then re-set from v85; 0x5dac7b/0x5dac92).
                            //   bit6 (40): "don't downscale" — forces byte_64A350
                            //              (global mip shift) to 0 for this upload
                            //              (0x5db300). Set if rec[110]&2 or a name
                            //              probe (0x5dad82/0x5dae30).
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

// ---------------------------------------------------------------------------
// Texture record +104 flag bits (see the `flags` field doc above for the
// per-bit provenance). Named constants for the bits the software render path
// consults.
// ---------------------------------------------------------------------------
enum TextureFlagBits : u8 {
    kTexFlagArg0Low    = 0x01, // bit0 — a4&1 (0x5da799)
    kTexFlagAlias      = 0x02, // bit1 — clone/alias record (0x5db258)
    kTexFlagColourKey  = 0x04, // bit2 — TRANSPARENCY / colour-key (0x5dad52/0x5db319)
    kTexFlagNameNM     = 0x08, // bit3 — name contains "_NM"; mip-bias select only
    kTexFlagIndexed8   = 0x20, // bit5 — 8-bit indexed source
    kTexFlagNoDownscale= 0x40, // bit6 — don't downscale (0x5db300)
};

// gilde.exe 0x5da714 (writer, 0x5dad52) / 0x5db234 (consumer, 0x5db319) —
// a texture record is colour-keyed iff record +104 BIT 2 (0x04) is set: a
// >8bpp/24-bit source loaded with the global gate dword_140809C off. The
// original DDraw path then colour-keys on palette entry 0 (index-0 transparent);
// the faithful software equivalent is to route such a texture through the masked
// span FillSpanTexturedMasked @0x5F721A (which treats source index 0 as
// transparent). Bit 3 ("_NM") is NOT a colour key — it only feeds the mip-bias
// arg of LoadAndStretchTexture @0x5dea50 (0x5db2f4); the wave-3/4 code used it
// in error (see progress/colourkey-wave5.md).
inline bool TextureIsColourKeyed(const Texture& t) {
    return (t.flags & kTexFlagColourKey) != 0;
}

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

// ---------------------------------------------------------------------------
// The "WHITE" DEFAULT binding — a HOST-DEFINED STAND-IN (flagged, rule 8).
//
// WAVE-4 EVIDENCE (captured decompile of VIBE_Texture_BindActive @0x5db564):
// when the resolved record's +76 field is ZERO the original sets
//     dword_1406A88 = 0   (texel mask)         unk_1406A8C = 0  (texel base!)
//     dword_1406A78 = 0   (palette/HiColTab)   dword_1406A7C = 1 (width)
//     byte_1407A91  = 1   (width shift — note: 1, not 0)
// i.e. a NULL binding: a span fetched through it would read texel byte
// [0 & 0] = absolute address 0 and a palette at address 0 — not representable
// in a faithful host reconstruction. VIBE_Texture_ResetBinding @0x5db5f0
// stores the same nulls (plus dword_64A1F8 = 0). There is NO white texture in
// the binary; "1x1 white" is this tree's substitute so untextured polys render
// through the same 16bpp textured leaf instead of dereferencing null. The only
// captured live caller (RasterizeMirrorTriangle @0x5f6c30) binds the poly's
// real texture record, so the null arm is not known to be rasterized through.
// Behavioural sign-off for the white substitution is tracked in
// progress/raster-verify-wave4.md.
// The palette packs white in RGB565 (0xFFFF — all channels full).
// ---------------------------------------------------------------------------
const Texture& WhiteDefaultTexture();   // 1x1, texel index 0
const u16*     WhiteDefaultPalette();   // 256 entries, all 0xFFFF

// ---------------------------------------------------------------------------
// 24-BIT BMP MATERIAL POLICY — GAP CLOSED (wave-4 verification pass).
//
// The original's software path palettizes a 24-bit source through
// VIBE_Texture_LoadSoftPalettize @0x5da34c -> VIBE_Bmp_LoadBuffer @0x5f0ce4
// (flags=7) -> VIBE_Quant_BuildPalette @0x6029f0 (256 colours, serpentine
// Floyd–Steinberg dither). That quantizer is now RECONSTRUCTED 1:1 in
// render/texture_palettize.{h,cpp}; the resolve layer
// (play::RealTextureSource) palettizes every 24-bit DecodedBmp on first
// decode, so such materials bind 8-bit indices + a 256-colour palette and
// render through the SAME palettized path as 8-bit sources — the original
// software renderer's behaviour, and the DEFAULT.
//
// The legacy stand-in switch below is KEPT FOR API COMPATIBILITY only: it
// used to opt 24-bit materials into the in-tree affine RGB kernel while the
// palettizer was missing. On the default path it is now unreachable — a
// resolve-layer 24-bit bind always carries palettized indices, so the bind
// sites' "no indices" branch (which consulted this switch) no longer fires
// for resolve-layer textures. Still default false.
// ---------------------------------------------------------------------------
bool Rgb24MaterialStandInEnabled();          // default false; legacy/compat
void SetRgb24MaterialStandIn(bool on);

// ---------------------------------------------------------------------------
// COLOUR-KEY master switch (wave-5 W5-CKEY). Default TRUE — the faithful engine
// behaviour: a 24-bit colour-keyed texture (kTexFlagColourKey, record +104 bit
// 2) renders its black backdrop transparent (the DDraw KEYSRC-on-pal[0]==black
// path, VIBE_Render_LoadAndStretchTexture @0x5dea50). DIAGNOSTIC ONLY: set FALSE
// to render the same frame with the key suppressed (== the prior keyed=false
// posture) for a before/after black-pixel measurement. The engine itself has no
// such toggle. See progress/colourkey-integration-wave5.md.
bool ColourKeyEnabled();
void SetColourKeyEnabled(bool on);

} // namespace guild::render

// floorgfx_recon — 1:1 reconstructions of gilde.exe gfx surface-blit leaves.
// See floorgfx_recon.h for provenance and the rule-3 boundary notes.
//
// DEFERRED (platform boundary / not pure math — reported, not faked, per rule 8):
//   0x5ba508 VIBE_Floor_FreeBuffers        — memory-manager (VIBE_Memory_FreeDebug)
//                                            + texture-cache teardown over the opaque
//                                            7288-byte floor struct. Pure plumbing.
//   0x5bd010 VIBE_Floor_LoadTexture        — BMP loader (VIBE_Bmp_LoadBuffer) + VFS
//                                            path build + memory alloc; I/O boundary.
//                                            WAVE-5: its PURE cores are now
//                                            reconstructed (FloorTextureResolveSlot,
//                                            FloorTextureMipName) and the floor's
//                                            slot-name -> loaded-texture path is
//                                            wired end-to-end through the REAL
//                                            by-name loader (VIBE_Texture_LoadByName
//                                            @0x5da714) by FloorTextureResolver.
//                                            The remaining BMP-decode/alloc inside
//                                            @0x5bd010 IS that loader (texture_asset).
//   0x5bd2d8 VIBE_Floor_ReloadTextures     — same I/O boundary (re-reads bmp files).
//   0x5bd44c VIBE_Floor_LoadFromHeightmap  — heightmap/water/texture BMP loading +
//                                            memory alloc; I/O boundary.
//   0x5bdcb4 VIBE_Floor_CreateGrid         — allocates the floor struct via the
//                                            memory manager; alloc boundary.
//   0x5c2a9c VIBE_Floor_RenderMinimap      — DDraw surface lock/unlock, mipmap build,
//                                            texture precache (rule-3). Only its pure
//                                            tile-step / span math is reconstructed
//                                            here (VIBE_Floor_MinimapTileStep / Span).
//   0x41e674 VIBE_Gfx_CrossFadeBegin       — widget-slot alloc + back-buffer copy +
//                                            VIBE_Widget_* orchestration (widget bnd).
//   0x41e814 VIBE_Gfx_CrossFadeStep        — drives SetFadeParams then frees via the
//                                            memory manager + destroys the widget.
// These wrap the pure leaves below; reconstructing them faithfully requires the
// memory-manager, BMP loader, VFS, widget system and the DDraw surface shim, none
// of which are this cluster's responsibility. The math leaves are complete & exact.

#include "render/floorgfx_recon.h"

#include "render/texture.h"          // Texture record
#include "render/texture_asset.h"    // TextureAssetCache (VFS+BMP by-name loader)
#include "render/texture_loader.h"   // TextureLoadByName / TextureLoaderSetCache

#include <cstring>

namespace guild::render {

FloorGfxReconState& FloorGfxState() {
    static FloorGfxReconState s;
    return s;
}

// gilde.exe 0x5fc4ec — VIBE_Gfx_DarkenSurface  (__usercall)
void VIBE_Gfx_DarkenSurface(u16* a1, i32 a2, i32 a3, const u16* a4) {
    FloorGfxReconState& st = FloorGfxState();
    // dword_64AACC = a2  (width); preserved across the row loop exactly like the
    // original, which keeps it in a global to recompute the row-advance.
    const u32 width = static_cast<u32>(a2);
    do {
        u32 v5 = width;
        if (v5 & 1u) {
            // Odd leading pixel handled 16 bits at a time: (*src >> 1) & 0x3DEF.
            *a1++ = static_cast<u16>((*a4++ >> 1) & 0x3DEF);
        }
        u32 v6 = v5 >> 1;
        // 32-bit fast path: process two pixels per iteration with mask 0x3DEFBDEF.
        // (DarkenSurface darkens IN PLACE on a1 here — the original reads/writes a1
        // for the 32-bit body and only consults a4 for the odd-pixel preamble.)
        do {
            u32 two;
            std::memcpy(&two, a1, sizeof(u32));
            two = (two >> 1) & 0x3DEFBDEFu;
            std::memcpy(a1, &two, sizeof(u32));
            a1 += 2;
            --v6;
        } while (v6);
        // a1 += (2*stride - 2*width) bytes  ->  (stride - width) words.
        a1 = a1 + (st.darkenRemapStridePx - static_cast<i32>(width));
        --a3;
    } while (a3);
}

// gilde.exe 0x5fc554 — VIBE_Gfx_RemapSurfacePalette  (__usercall)
void VIBE_Gfx_RemapSurfacePalette(u16* a1, i32 a2, i32 a3) {
    FloorGfxReconState& st = FloorGfxState();
    // dword_64AAD4 = a2 (width); dword_64A1C4 = remap table base (v5).
    const i32 width = a2;
    const u16* v5 = st.remapTable;
    do {
        i32 v6 = width;
        do {
            // v4 = *a1 (16-bit, high word zero); *a1++ = table[v4].
            u16 idx = *a1;
            *a1++ = v5[idx];
            --v6;
        } while (v6);
        a1 = a1 + (st.darkenRemapStridePx - width);
        --a3;
    } while (a3);
}

// gilde.exe 0x5fc5a4 — VIBE_Gfx_FadeBlend565  (__usercall)
i32 VIBE_Gfx_FadeBlend565(u16* a1, const u16* a2, const u16* a3) {
    FloorGfxReconState& st = FloorGfxState();

    // byte_64AB61 = dword_1406934 (height); byte_64AB5C = 0xFF - alpha.
    i32 height = st.fadeHeight;
    const u8 invAlpha = static_cast<u8>(0xFF - st.fadeAlpha);
    const u8 alpha    = st.fadeAlpha;

    // 64-entry channel tables. AADC weights the background (a2) by (255-alpha)+1,
    // AB1C weights the foreground (a3) by alpha. Built for indices 0..63.
    u8 byte_64AADC[64];
    u8 byte_64AB1C[64];
    for (int i = 63; i >= 0; --i) {
        byte_64AADC[i] = static_cast<u8>(
            ((static_cast<u16>(invAlpha) * static_cast<u8>(i)) >> 8) + 1);
        byte_64AB1C[i] = static_cast<u8>(
            (static_cast<u16>(alpha) * static_cast<u8>(i)) >> 8);
    }

    i32 result = 0;
    do {
        i32 width = st.fadeWidth;   // dword_64AB65 = dword_140693C
        do {
            const u16 sb = *a2;     // background pixel (RGB565)
            const u16 sc = *a3;     // foreground pixel (RGB565)
            // al=blue, bl=green(6b), bh=red.  AADC[bg] + AB1C[fg] per channel.
            u8 blue  = static_cast<u8>(byte_64AADC[sb & 0x1F]        + byte_64AB1C[sc & 0x1F]);
            u8 green = static_cast<u8>(byte_64AADC[(sb >> 5) & 0x3F] + byte_64AB1C[(sc >> 5) & 0x3F]);
            u8 red   = static_cast<u8>(byte_64AADC[sb >> 11]         + byte_64AB1C[(sc >> 11) & 0x1F]);
            u16 out = static_cast<u16>(
                  blue
                | ((static_cast<u16>(green) & 0x3F) << 5)
                | ((static_cast<u16>(red) << 11) & 0xF800));
            *a1++ = out;
            ++a2;
            ++a3;
            --width;
        } while (width);
        result = 2 * (st.fadeStridePx - st.fadeWidth);
        a1 = reinterpret_cast<u16*>(reinterpret_cast<char*>(a1) + result);
        --height;
    } while (height);
    return result;
}

// gilde.exe 0x5fc6a8 — VIBE_Gfx_FadeBlend555  (__usercall)
i32 VIBE_Gfx_FadeBlend555(u16* a1, const u16* a2, const u16* a3) {
    FloorGfxReconState& st = FloorGfxState();

    i32 height = st.fadeHeight;
    const u8 invAlpha = static_cast<u8>(0xFF - st.fadeAlpha);
    const u8 alpha    = st.fadeAlpha;

    u8 byte_64AB6C[64];
    u8 byte_64ABAC[64];
    for (int i = 63; i >= 0; --i) {
        byte_64AB6C[i] = static_cast<u8>(
            ((static_cast<u16>(invAlpha) * static_cast<u8>(i)) >> 8) + 1);
        byte_64ABAC[i] = static_cast<u8>(
            (static_cast<u16>(alpha) * static_cast<u8>(i)) >> 8);
    }

    i32 result = 0;
    do {
        i32 width = st.fadeWidth;
        do {
            const u16 sb = *a2;     // background (RGB555)
            const u16 sc = *a3;     // foreground (RGB555)
            u8 blue  = static_cast<u8>(byte_64AB6C[sb & 0x1F]         + byte_64ABAC[sc & 0x1F]);
            u8 green = static_cast<u8>(byte_64AB6C[(sb >> 5) & 0x1F]  + byte_64ABAC[(sc >> 5) & 0x1F]);
            u8 red   = static_cast<u8>(byte_64AB6C[(sb >> 10) & 0x1F] + byte_64ABAC[(sc >> 10) & 0x1F]);
            u16 out = static_cast<u16>(
                  blue
                | ((static_cast<u16>(green) & 0x1F) << 5)
                | ((static_cast<u16>(red) << 10) & 0xFC00));
            *a1++ = out;
            ++a2;
            ++a3;
            --width;
        } while (width);
        result = 2 * (st.fadeStridePx - st.fadeWidth);
        a1 = reinterpret_cast<u16*>(reinterpret_cast<char*>(a1) + result);
        --height;
    } while (height);
    return result;
}

// gilde.exe 0x5d9078 — VIBE_Gfx_SetFadeParams  (__userpurge)
i32 VIBE_Gfx_SetFadeParams(u16* dst, const u16* b, const u16* c,
                           i32 width, i32 height, u8 alpha) {
    FloorGfxReconState& st = FloorGfxState();
    st.fadeWidth  = width;   // dword_140693C = a5 (width)... see note below
    st.fadeHeight = height;  // dword_1406934 = a5? -- exact mapping reproduced below
    st.fadeAlpha  = alpha;   // BYTE2(dword_1406947) = a6

    // The original stores:
    //   dword_1406934 = a5;   dword_140693C = a1;   BYTE2(dword_1406947) = a6;
    // where in the call from CrossFadeStep a1=width(cx), a5=height. We expose the
    // operands as (width,height,alpha) and map them to the same two globals the
    // blenders read: fadeWidth (dword_140693C, inner count) and fadeHeight
    // (dword_1406934, outer count). The assignments above already place them so.

    if (st.pixelFormat565 == 11)   // byte_76271E == 11  => RGB565
        return VIBE_Gfx_FadeBlend565(dst, b, c);
    else
        return VIBE_Gfx_FadeBlend555(dst, b, c);
}

// gilde.exe 0x5c2a9c — VIBE_Floor_RenderMinimap : pure tile LOD-step math.
i32 VIBE_Floor_MinimapTileStep(u8 tileOverrideByte, u8 floorFlagsByte) {
    if (tileOverrideByte) {
        // v8 = *(u8*)(tile+94)
        return static_cast<u8>(tileOverrideByte);
    }
    // v7 = *(u8*)(a1+7280); if (v7 & 0x1C) v8 = (u8)(8*v7) >> 5; else v8 = 1;
    const u8 v7 = floorFlagsByte;
    if (v7 & 0x1C)
        return static_cast<u8>(static_cast<u8>(8 * v7) >> 5);
    return 1;
}

// gilde.exe 0x5c2a9c — minimap inner sub-tile span math.
void VIBE_Floor_MinimapTileSpan(i32 cellsPerTile, i32 step,
                                bool isLastRow, bool isLastCol,
                                i32* outRowBound, i32* outColBound) {
    // v9 = v26/step + 1   (rows base)
    // v10 = (isLastCol) ? v9 - 4/step : v26/step + 1   (cols base)
    // if (isLastRow) v9 -= 4/step
    // v32 = v9 - 1  (row loop bound); v36 = v10 - 1 (col loop bound)
    const i32 base = cellsPerTile / step + 1;
    i32 v9  = base;
    i32 v10 = isLastCol ? (v9 - 4 / step) : base;
    if (isLastRow)
        v9 -= 4 / step;
    if (outRowBound) *outRowBound = v9 - 1;
    if (outColBound) *outColBound = v10 - 1;
}

// ===========================================================================
// TILE TEXTURES (wave-5)
// ===========================================================================

// gilde.exe 0x5bd010 — VIBE_Floor_LoadTexture : slot resolution (pure).
//   if (a3 < 0) { count consecutive non-empty 64-byte name slots @Floor+6756 }
//   else slot = a3;  if (slot >= 8) return -1;
int FloorTextureResolveSlot(const char slotNames[8][64], int requested) {
    int slot;
    if (requested < 0) {
        // v37 = 0; if (*(Floor+6756)) do { ... ; ++v37; } while (*(slot name byte))
        // i.e. advance over non-empty 64-byte name slots; stop at the first empty.
        slot = 0;
        if (slotNames[0][0]) {                 // if (v6) — first slot non-empty
            do {
                ++slot;                        // ++v37
                if (slot >= 8) break;          // (guarded below by the >= 8 return)
            } while (slotNames[slot][0]);      // while (*(slot+6820)) — next name[0]
        }
    } else {
        slot = requested;                      // v37 = a3
    }
    if (slot >= 8)                              // if (v37 >= 8) return -1
        return -1;
    return slot;
}

// gilde.exe 0x5B8C90 — the 3 mip-layer name suffixes (11-byte stride in the binary;
// layer 0 is the 11 zero bytes at 0x5B8C90 == "", then "_high_1", "_high_2").
const char kFloorMipSuffix[3][8] = { "", "_high_1", "_high_2" };

// gilde.exe 0x5bd010 — the per-mip texture name (texName + suffix).
std::string FloorTextureMipName(const std::string& texName, int layer) {
    if (layer < 0 || layer >= 3)
        return texName;
    return texName + kFloorMipSuffix[layer];
}

// gilde.exe 0x5ba1e8 — VIBE_TextureCache_GetOrBuildTile : mask-wrapped sub-block
// sample (pure). Copies an (span+1)x(span+1) block from the toroidal source grid.
void TileCacheSampleSubBlock(const u8* src, int srcStride, int x0, int y0,
                             int span, u8* dst, int dstStride) {
    // v9 = v27 - 1 == N - 1 (mask); the two nested do/while loops in the decompile
    // run row v30 in [v25 .. v25+a5], col v11 in [v28 .. v28+a5] (inclusive).
    const int mask = srcStride - 1;            // v9 = v27 - 1
    for (int row = 0; row <= span; ++row) {    // v30 = v25 .. v26 (== a5 + v25)
        const int srcRow = srcStride * (mask & (y0 + row));   // v10 = v27*(v9 & v30)
        u8* d = dst + (long)row * dstStride;                  // v29 += 5 per row
        for (int col = 0; col <= span; ++col) {               // v11 = v28 .. v28+a5
            // *(v31 + v10 + (v9 & v11)) — the wrapped source byte.
            d[col] = src[srcRow + (mask & (x0 + col))];
        }
    }
}

// gilde.exe 0x5db928 (+88) / 0x5ba1e8 width clamp.
u32 TileRecordTexelMask(int width) {
    // *((_DWORD*)v7 + 19) = (w - 1) | (w*w - 1) — identical to texture.h TexelMask.
    return (u32)((width - 1) | (width * width - 1));
}

int TileCacheWidthClamp(int span, u32 globalCap, u8 mipShift, u8 capShift) {
    // v16 = dword_64A038 << byte_64A045;
    // if (a5*(64 >> byte_64A044) < v16) v16 = a5*(64 >> byte_64A044);
    int cap = (int)(globalCap << capShift);
    int proj = span * (64 >> mipShift);
    if (proj < cap)
        cap = proj;
    return cap;
}

// ---------------------------------------------------------------------------
// FloorTextureResolver
// ---------------------------------------------------------------------------
void FloorTextureResolver::Bind(const char slotNames[8][64],
                                TextureAssetCache* cache) {
    slotNames_ = slotNames;
    cache_ = cache;
    for (int i = 0; i < 8; ++i) { slotRecord_[i] = -2; triedSlot_[i] = false; }
}

const Texture* FloorTextureResolver::LoadSlot(int slot) {
    if (slot < 0 || slot >= 8 || !slotNames_ || !cache_)
        return nullptr;
    if (triedSlot_[slot]) {
        int rec = slotRecord_[slot];
        return (rec >= 0) ? cache_->record(rec) : nullptr;
    }
    triedSlot_[slot] = true;
    slotRecord_[slot] = -1;

    // The 64-byte slot name (Floor+0x1A64 + 0x40*slot). Empty => no texture
    // (@0x5bd010 still runs but BuildBmpPath of an empty name resolves nothing).
    char name[64];
    std::memcpy(name, slotNames_[slot], 64);
    name[63] = '\0';
    if (!name[0])
        return nullptr;

    // VIBE_Floor_LoadTexture @0x5bd010 -> VIBE_Texture_BuildBmpPath @0x5d97e8:
    // "*" + name + ".BMP" (asc_62959C + name + aBmp). We load the BASE mip layer
    // (suffix "") — the engine's _high_1/_high_2 mips are an LOD detail the software
    // resolver does not need for the single bound record (the mip suffix builder is
    // exposed separately above for completeness/tests).
    const std::string base = FloorTextureMipName(name, 0);
    const std::string path = "*" + base + ".BMP";
    int rec = cache_->LoadByName(path.c_str(), base);
    if (rec < 0)
        return nullptr;
    slotRecord_[slot] = rec;
    return cache_->record(rec);
}

const Texture* FloorTextureResolver::Resolve(u8 typeByte) {
    // ComputeTileIllumination hole gate: high bit (0x80) => unlit/hole => untextured.
    if (typeByte & 0x80u)
        return nullptr;
    return LoadSlot(typeByte & 7);             // low 3 bits index the 8 slots
}

// Process-active resolver + the hook trampoline (the terrain walk installs one).
namespace {
FloorTextureResolver* g_activeFloorTexResolver = nullptr;
}
void SetActiveFloorTextureResolver(FloorTextureResolver* r) {
    g_activeFloorTexResolver = r;
}
FloorTextureResolver* ActiveFloorTextureResolver() {
    return g_activeFloorTexResolver;
}

const void* FloorTextureResolver::GetTileTextureHook(u8 typeByte) {
    if (!g_activeFloorTexResolver)
        return nullptr;
    return static_cast<const void*>(g_activeFloorTexResolver->Resolve(typeByte));
}

} // namespace guild::render

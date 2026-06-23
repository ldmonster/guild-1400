#pragma once
// floorgfx_recon — 1:1 reconstructions of gilde.exe gfx surface-blit leaves and
// the faithful pure-math helpers extracted from the Floor minimap renderer.
//
// Provenance (gilde.exe, imagebase 0x400000):
//   0x5fc4ec  VIBE_Gfx_DarkenSurface
//   0x5fc554  VIBE_Gfx_RemapSurfacePalette
//   0x5fc5a4  VIBE_Gfx_FadeBlend565
//   0x5fc6a8  VIBE_Gfx_FadeBlend555
//   0x5d9078  VIBE_Gfx_SetFadeParams
//   0x5c2a9c  VIBE_Floor_RenderMinimap  (pure tile-step math extracted as helper)
//
// These are the GPU-independent (rule-3 boundary respected) pixel-math routines:
// they operate on raw 16-bit framebuffer words already locked by the surface
// context. The surface stride / fade parameters that the original kept in process
// globals are reproduced here as module-scope state with the SAME semantics, so the
// math is bit-identical. The DDraw/widget/memory-manager orchestration that wraps
// these leaves (VIBE_Render_WithSurfaceContext, VIBE_Gfx_CrossFade*) is the
// platform boundary and is intentionally NOT reconstructed here (see deferral notes
// in the .cpp).

#include "guild/common/types.h"

#include <string>

namespace guild::render {

// ===========================================================================
// TILE TEXTURES (wave-5) — the floor's per-cell texture-id -> slot-name ->
// loaded-texture path, plus the recovered pure cores of:
//
//   0x5bd010  VIBE_Floor_LoadTexture            (slot-name -> texture, BMP+alloc)
//   0x5ba1e8  VIBE_TextureCache_GetOrBuildTile  (LRU tile cache + sub-block sample)
//   0x5db928  VIBE_Texture_CreateTileRecord     (tile record texelMask build)
//
// These two are I/O / memory-manager / texture-cache boundary functions; their
// device-coupled bodies (VIBE_Bmp_LoadBuffer @0x5f0ce4 BMP decode, AllocDebug,
// the 17-int LRU slot array dword_64A040, VIBE_Texture_CreateTileRecord's record
// alloc) are NOT reconstructed here per rule 8. What IS reconstructed 1:1 is every
// piece of GPU/IO-INDEPENDENT engine math inside them: the slot resolution + mip
// path build of @0x5bd010, the mask-wrapped sub-block sample + texelMask of
// @0x5ba1e8, and a clean resolver that turns a per-cell texture id into the loaded
// texture through the REAL by-name load path (VIBE_Texture_LoadByName @0x5da714,
// reconstructed in render/texture_loader + render/texture_asset).
// ===========================================================================

// ---------------------------------------------------------------------------
// 0x5bd010 — VIBE_Floor_LoadTexture : SLOT RESOLUTION (pure).
// The original resolves the slot index from arg `a3` (ebx):
//   if (a3 < 0)  scan the 8 64-byte name slots at Floor+6756, slot = count of
//                consecutive non-empty slots (the first EMPTY slot's index);
//   else         slot = a3.
//   if (slot >= 8) return -1.
// `slotNames` is the 8-slot 64-byte-stride name block (Floor+0x1A64 == +6756);
// `requested` is the original a3. Returns the resolved slot index, or -1 (>= 8).
// ---------------------------------------------------------------------------
int FloorTextureResolveSlot(const char slotNames[8][64], int requested);

// ---------------------------------------------------------------------------
// 0x5bd010 — VIBE_Floor_LoadTexture : the per-mip TEXTURE NAME (pure).
// For each of 3 mip layers (the @0x5bd127..0x5bd1d0 loop, base+0..+96 step 32) the
// engine appends the layer suffix from dword_5B8C90 (11-byte stride: layer 0 = ""
// (the 11 zero bytes at 0x5B8C90), layer 1 = "_high_1", layer 2 = "_high_2") to the
// texture name, then VIBE_Texture_BuildBmpPath builds "*" + that + ".BMP". Returns
// the layer name (texName + suffix); `layer` in [0,3). (BuildBmpPath itself is the
// VFS resolve boundary — TextureLoadByName handles the "*"+name+".BMP" build.)
// ---------------------------------------------------------------------------
extern const char kFloorMipSuffix[3][8];   // "", "_high_1", "_high_2"
std::string FloorTextureMipName(const std::string& texName, int layer);

// ---------------------------------------------------------------------------
// 0x5ba1e8 — VIBE_TextureCache_GetOrBuildTile : SUB-BLOCK SAMPLE (pure).
// On a cache miss the engine copies an (a5+1) x (a5+1) sub-block out of the source
// type/texel grid `src` (size N == `srcStride`, wrapped through mask = N-1) into a
// row-strided scratch, starting at source cell (a3, a4) (== (x0,y0)):
//   mask = N - 1
//   for (row = 0; row <= a5; ++row)
//     for (col = 0; col <= a5; ++col)
//       dst[row*dstStride + col] = src[ N*(mask & (y0+row)) + (mask & (x0+col)) ]
// `dstStride` is the scratch row stride (5 in the LRU slot, but exposed for tests).
// This is the byte-exact mask-wrapped sample the LRU key (CRC32) is taken over.
// ---------------------------------------------------------------------------
void TileCacheSampleSubBlock(const u8* src, int srcStride, int x0, int y0,
                             int span, u8* dst, int dstStride);

// ---------------------------------------------------------------------------
// 0x5db928 — VIBE_Texture_CreateTileRecord / 0x5ba1e8 width pick : the tile's
// texelMask (+88 == (w-1)|(w*w-1)) and the GetOrBuildTile width clamp
//   width = min(a5*(64 >> byte_64A044), dword_64A038 << byte_64A045)
// (byte_64A044/045 = global mip shifts, dword_64A038 = global cap; all 0 in the
// shipped binary, so the clamp degenerates — exposed for completeness/tests).
// ---------------------------------------------------------------------------
u32 TileRecordTexelMask(int width);
int TileCacheWidthClamp(int span, u32 globalCap, u8 mipShift, u8 capShift);

// ---------------------------------------------------------------------------
// Module-scope surface/fade state. These mirror the original process globals and
// carry the exact same meaning. The wrapping surface-context code installs these
// before invoking a blit leaf (just as the original did).
// ---------------------------------------------------------------------------
struct FloorGfxReconState {
    // dword_64A1C8 — destination surface stride in PIXELS (words). Used by Darken
    // and Remap to advance to the next row after writing `width` pixels.
    i32 darkenRemapStridePx = 0;
    // dword_64A1C4 — palette remap table (256 u16 entries) for RemapSurfacePalette.
    const u16* remapTable = nullptr;
    // dword_7626F8 — current locked surface stride in PIXELS. Used by the fade
    // blenders to step to the next row.
    i32 fadeStridePx = 0;

    // Fade parameters set by VIBE_Gfx_SetFadeParams:
    i32 fadeWidth  = 0;   // dword_140693C — columns per row (inner loop count)
    i32 fadeHeight = 0;   // dword_1406934 — number of rows
    u8  fadeAlpha  = 0;   // BYTE2(dword_1406947) — 0..255 blend weight of `b3`

    // byte_76271E — surface pixel format selector: 11 => RGB565, else RGB555.
    u8  pixelFormat565 = 0;
};

// The single module state instance (mirrors the original's flat globals).
FloorGfxReconState& FloorGfxState();

// ---------------------------------------------------------------------------
// 0x5fc4ec — VIBE_Gfx_DarkenSurface   (__usercall)
// Halves every channel of a 16-bit RGB block (>>1 with mask 0x3DEF per pixel,
// 0x3DEFBDEF for the 32-bit fast path) copying from `src` into `dst`.
//   dst        : destination 16-bit surface words (eax)
//   width      : pixels per row (edx)  [stored to dword_64AACC]
//   height     : number of rows (ebx)
//   src        : source 16-bit words (esi)
// Row advance uses FloorGfxState().darkenRemapStridePx (dword_64A1C8).
// ---------------------------------------------------------------------------
void VIBE_Gfx_DarkenSurface(u16* dst, i32 width, i32 height, const u16* src);

// ---------------------------------------------------------------------------
// 0x5fc554 — VIBE_Gfx_RemapSurfacePalette   (__usercall)
// In-place remap: each 16-bit word is replaced by remapTable[word].
//   buf        : surface words, modified in place (eax)
//   width      : pixels per row (edx)  [stored to dword_64AAD4]
//   height     : number of rows (ebx)
// Uses FloorGfxState().remapTable (dword_64A1C4) and .darkenRemapStridePx.
// ---------------------------------------------------------------------------
void VIBE_Gfx_RemapSurfacePalette(u16* buf, i32 width, i32 height);

// ---------------------------------------------------------------------------
// 0x5fc5a4 — VIBE_Gfx_FadeBlend565   (__usercall)
// Per-pixel cross-fade of two RGB565 sources into a destination, weighting `b`
// by (255-alpha) and `c` by alpha via 64-entry precomputed channel tables.
//   dst : destination words (eax)
//   b   : background source (edx)   weighted by (255-alpha)
//   c   : foreground source (ebx)   weighted by alpha
// Reads fadeWidth/fadeHeight/fadeAlpha/fadeStridePx from FloorGfxState().
// Returns the per-row byte advance (2*(stride-width)) like the original.
// ---------------------------------------------------------------------------
i32 VIBE_Gfx_FadeBlend565(u16* dst, const u16* b, const u16* c);

// ---------------------------------------------------------------------------
// 0x5fc6a8 — VIBE_Gfx_FadeBlend555   (__usercall)   RGB555 variant of the above.
// ---------------------------------------------------------------------------
i32 VIBE_Gfx_FadeBlend555(u16* dst, const u16* b, const u16* c);

// ---------------------------------------------------------------------------
// 0x5d9078 — VIBE_Gfx_SetFadeParams   (__userpurge)
// Stores fade width/height/alpha into the module state then dispatches to the
// 565 or 555 blender based on pixelFormat565 (byte_76271E == 11).
//   alpha (cx), b (edx), dst (eax), c (ebx), width (a5), heightAlphaByte (a6)
// NOTE on the original prototype: a1=alpha@cx, a2=b@edx (background source),
// a3=dst@eax, a4=c@ebx (foreground source), a5=width@(esp), a6=alphaByte@(esp).
// We keep the call shape explicit and documented to remain 1:1.
//   dst      : destination words
//   b        : background source
//   c        : foreground source
//   width    : columns per row     -> dword_140693C
//   height   : rows                -> dword_1406934
//   alpha    : blend weight 0..255 -> BYTE2(dword_1406947)
// Returns the value returned by the chosen blender.
// ---------------------------------------------------------------------------
i32 VIBE_Gfx_SetFadeParams(u16* dst, const u16* b, const u16* c,
                           i32 width, i32 height, u8 alpha);

// ---------------------------------------------------------------------------
// 0x5c2a9c — VIBE_Floor_RenderMinimap : pure tile-step math (extracted helper)
//
// The minimap renderer is otherwise pure DDraw/texture-cache orchestration
// (surface lock/unlock, mipmap build, precache) — the rule-3 boundary — so the
// full function is deferred. The one piece of GPU-independent engine math inside
// it is how a tile's LOD "step" is derived from the tile's own override byte or,
// when absent, from the floor flags byte (a1+7280). This helper reproduces that
// computation 1:1 so it can be golden-tested and reused by the eventual minimap
// reconstruction.
//
// Original (per tile):
//   if (*(tile+94))              step = *(u8*)(tile+94);
//   else { v7 = *(u8*)(a1+7280);
//          step = (v7 & 0x1C) ? ((u8)(8*v7) >> 5) : 1; }
//
//   tileOverrideByte : the tile's +94 byte (0 => derive from flags)
//   floorFlagsByte   : the floor descriptor's +7280 byte
// Returns the LOD step (0 is possible and the caller treats step==0 as "skip").
// ---------------------------------------------------------------------------
i32 VIBE_Floor_MinimapTileStep(u8 tileOverrideByte, u8 floorFlagsByte);

// Sub-tile span math used by the minimap's inner loop (also pure):
//   Given a tile spanning `cellsPerTile` cells, the per-tile loop count is
//   span = cells/step + 1, with the last tile in each axis (index==7) reduced by
//   (4/step). The two output loop bounds (rows v32 and cols v36) are then each
//   decremented by 1. This reproduces that exactly.
//     cellsPerTile : dword (a1+4) — cells along one tile edge (v26)
//     step         : the LOD step from VIBE_Floor_MinimapTileStep (must be != 0)
//     isLastRow    : tile row index == 7   (v21 == 7)
//     isLastCol    : tile col index == 7   (v27 == 7)
//   Writes the decremented inner row/col loop bounds.
// ---------------------------------------------------------------------------
void VIBE_Floor_MinimapTileSpan(i32 cellsPerTile, i32 step,
                                bool isLastRow, bool isLastCol,
                                i32* outRowBound, i32* outColBound);

// ===========================================================================
// FLOOR TILE-TEXTURE RESOLVER (wave-5) — the clean, present-coupled path the
// terrain walk installs as TerrainRenderHooks::getTileTexture.
//
// The floor carries (scene_floor.h, parsed live per wave-4):
//   * 8 texture-slot NAMES (Floor+0x1A64, the typeNames the @0x5bd44c loader
//     copied there from LoadFloorRegions @0x5e78a8 ctx+164),
//   * a per-cell min-normalized TEXTURE-ID grid (Floor+20: cell -> 0..7 slot).
//
// VIBE_Floor_LoadTexture @0x5bd010 turns ONE slot name into a loaded texture
// record by building "*" + name + ".BMP" (VIBE_Texture_BuildBmpPath @0x5d97e8)
// and decoding it (the I/O boundary). The terrain walk's per-cell texture fetch
// (VIBE_TextureCache_GetOrBuildTile @0x5ba1e8) then resolves a tile's dominant
// type byte to that loaded record.
//
// FloorTextureResolver wires this end-to-end through the REAL by-name load path
// (TextureLoadByName -> VIBE_Texture_LoadByName @0x5da714 -> the VFS+BMP decode
// in texture_asset.cpp): Resolve(typeByte) maps the per-cell texture id to its
// slot name, loads it once (cached by slot), and returns the texture record. It
// is the resolver the terrain walk installs as its getTileTexture hook.
// ===========================================================================
class TextureAssetCache;   // render/texture_asset.h — the VFS+BMP slot manager.
struct Texture;            // render/texture.h

class FloorTextureResolver {
public:
    // Bind the 8 floor slot names + the by-name texture cache the loads run through.
    // `slotNames` is the Floor+0x1A64 64-byte-stride name block (8 slots); `cache`
    // is the VFS-backed TextureAssetCache (may be null -> Resolve returns nullptr,
    // the engine's untextured fallback). The flags mirror @0x5bd010's call into the
    // by-name loader (the floor textures load with the default 0 flags here; the
    // water-region per-texture flag derivation is a separate, deferred path).
    void Bind(const char slotNames[8][64], TextureAssetCache* cache);
    bool bound() const { return cache_ != nullptr; }

    // VIBE_Floor_LoadTexture @0x5bd010 (resolved through the real loader): load (or
    // reuse) slot `slot` (0..7) and return its texture record, or nullptr when the
    // slot is empty / the BMP is absent / no cache is bound. Caches the resolved
    // slot index so repeated calls (per tile) don't re-scan the cache.
    const Texture* LoadSlot(int slot);

    // The getTileTexture hook body: a tile's dominant type byte (the per-cell
    // texture id, 0..7) -> the loaded slot texture. typeByte's high bit (0x80,
    // "hole/unlit") yields nullptr (untextured), matching ComputeTileIllumination's
    // hole gate; the low 7 bits index the 8 slots (& 7). Returns an opaque pointer
    // for the hook (const Texture*).
    const Texture* Resolve(u8 typeByte);

    // The reusable install entry-point: a free function with the exact
    // TerrainRenderHooks::getTileTexture signature ((u8)->const void*) bound to the
    // process-active resolver (SetActiveFloorTextureResolver). See the .cpp.
    static const void* GetTileTextureHook(u8 typeByte);

private:
    const char (*slotNames_)[64] = nullptr;   // Floor+0x1A64 (8 x 64 bytes)
    TextureAssetCache* cache_ = nullptr;
    int   slotRecord_[8];                       // resolved slot index cache (-2 = unknown)
    bool  triedSlot_[8] = {};                    // slot was attempted (avoid re-load)
};

// Process-active resolver the GetTileTextureHook trampoline reads (the terrain
// walk installs one resolver, then sets it active and hands the hook to
// SetTerrainRenderHooks). Null => the hook returns nullptr (untextured fallback).
void SetActiveFloorTextureResolver(FloorTextureResolver* r);
FloorTextureResolver* ActiveFloorTextureResolver();

} // namespace guild::render

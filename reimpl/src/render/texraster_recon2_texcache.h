#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — texraster_recon2: texture-CACHE teardown cluster of gilde.exe.
// These reset / free the two cache arrays the texture subsystem owns:
//
//   * the TILE cache  : dword_64A034 slots of 68 bytes at dword_64A040
//                       (the per-tile entries the LRU tile cache hands out;
//                        the build/lookup side lives in src/render/texture_cache)
//   * the TEXTURE cache: dword_1406A80 records of 128 bytes at dword_1406A84
//                        plus dword_1406A60 mip blocks of 776 bytes at
//                        dword_1406A68
//
// Reconstructed 1:1 from the Hex-Rays decompile of:
//   0x5b9444  VIBE_TextureCache_Free        (release '*' tile entries, zero, free)
//   0x5ba37c  VIBE_TextureCache_Setup       (build channel LUT, init, identity mtx)
//   0x5ba428  VIBE_TextureCache_Shutdown    (Free + clear stamp)
//   0x5d9b78  VIBE_TextureCache_DisposeAll  (release texture+mip entries, ZERO them)
//   0x5d9c98  VIBE_TextureCache_Shutdown_d9c98 (release + FREE the arrays)
//
// PLATFORM BOUNDARY (Rule 3/8): VIBE_Memory_FreeDebug (0x43923c) and the channel
// LUT / matrix / filter-weight builders (0x435a3c, 0x5b9e74, 0x5b94cc) and
// VIBE_Texture_ReleaseEntry (0x5d9a0c) live elsewhere — routed through hooks. The
// pure logic reconstructed here is: WHICH entries get released, the iteration
// strides (68 / 128 / 776), the memset-to-zero of the arrays, and the exact set
// of globals reset and to what values (incl. the 1.0f identity texture matrix).
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// State block mirroring the cache globals (each field carries its address).
// ---------------------------------------------------------------------------
struct TexCacheState {
    // --- tile cache (VIBE_TextureCache_Free) ---
    u32   tileCount = 0;        // dword_64A034  number of 68-byte tile slots
    u8*   tileBase  = nullptr;  // dword_64A040  base of the tile-slot array
    u32   tileStamp = 0;        // dword_64A038  LRU stamp cleared on shutdown

    // --- texture record cache ---
    u32   texRecCount = 0;      // dword_1406A80 number of 128-byte records
    u8*   texRecBase  = nullptr;// dword_1406A84 base of the record array
    u32   texRecCap   = 0;      // (capacity; not separately tracked here)

    // --- mip block cache ---
    u32   mipCount = 0;         // dword_1406A60 number of 776-byte mip blocks
    u8*   mipBase  = nullptr;   // dword_1406A68 base of the mip-block array
    u32   mipCap   = 0;         // dword_1406A64 mip-block capacity (alloc count)

    // --- misc globals reset on dispose/shutdown ---
    u32   uvScrollHi  = 0;      // dword_14069DC
    u32   uvScrollLo  = 0;      // dword_14069D8
    u32   uvScrollV   = 0;      // dword_14069D4
    float uvScrollF   = 0.0f;   // flt_14069D0
    u32   nextAnimId  = 0;      // dword_1406A58
    u32   frameStamp  = 0;      // dword_1406A6C / dword_1406A74 (= dword_62EB38)
    u32   basePathSet = 0;      // dword_1406A54
    u8    captureFlag = 0;      // dword_64A1FC (default-mip flag, also reset here)
    u32   defaultMip  = 0;      // dword_64A1F8

    // --- texture matrix (set by Setup), 12 floats; identity by default ---
    float texMatrix[12] = {0};  // dword_13FD4C0..13FD4EC

    // --- filter / channel-LUT inputs captured by Setup ---
    u8    filterBlur  = 0;      // byte_64A02C (a4)
    u8    filterMode  = 0;      // byte_64A02D (a3)
};

// Hooks for the boundary callees.
struct TexCacheHooks {
    // VIBE_Texture_ReleaseEntry(slot) (0x5d9a0c): decrement/free one '*' entry.
    // Must DECREMENT the slot's ref count at +64 (dword [16]) so the while-loops
    // terminate. Default: a built-in that just zeroes the ref count (one-shot).
    void (*releaseEntry)(u8* slot) = nullptr;
    // VIBE_Memory_FreeDebug(ptr) (0x43923c). Default: no-op.
    void (*freeDebug)(void* p) = nullptr;
    // VIBE_SurfaceCache_FreeAll(0) (0x5d9430). Default: no-op.
    void (*surfaceCacheFreeAll)() = nullptr;
    // VIBE_TextureCache_Init(a1) (0x5b9368). Default: no-op.
    void (*cacheInit)() = nullptr;
    // Render LUT / filter builders (0x435a3c, 0x5b9e74, 0x5b94cc). Default: no-op.
    void (*buildChannelLut)() = nullptr;
    void (*setMipFilterLevel)(u32 level) = nullptr;
    void (*computeFilterWeights)() = nullptr;
};

void SetTexCacheHooks(const TexCacheHooks& h);
const TexCacheHooks& GetTexCacheHooks();
TexCacheState& TexCache();

// 0x5b9444 — VIBE_TextureCache_Free: for each of tileCount 68-byte slots, if the
// slot's data ptr is set and its tag byte == 42, drain its sub-entries via
// releaseEntry (while ref>0); then null the slot ptr and clear its +64 flag.
// Finally tileCount=0, FreeDebug(tileBase), tileBase=0.
void TextureCache_Free();

// 0x5ba428 — VIBE_TextureCache_Shutdown: Free() then clear tileStamp.
void TextureCache_Shutdown();

// 0x5ba37c — VIBE_TextureCache_Setup(level@edx, mode@cl, blur@bl): build channel
// LUT, init cache, set mip filter level, load the IDENTITY texture matrix
// (1.0f on the diagonal-ish slots), capture filterMode/filterBlur, compute
// filter weights. (The float layout is reproduced exactly.)
void TextureCache_Setup(u32 mipFilterLevel, u8 mode, u8 blur);

// 0x5d9b78 — VIBE_TextureCache_DisposeAll: drain every texture record (128B
// stride) and mip block (776B stride) via releaseEntry / freeDebug, then ZERO
// both arrays in place (the memsets) and reset the scroll/stamp/anim globals.
// Arrays are NOT freed (capacities preserved). Returns the frame stamp.
u32 TextureCache_DisposeAll();

// 0x5d9c98 — VIBE_TextureCache_Shutdown_d9c98: drain records + mips, then FREE
// the record array and the mip array (FreeDebug), reset globals, and call
// SurfaceCache_FreeAll(0).
void TextureCache_ShutdownFull();

// Wiring/test helper (not in the original): seed the global frame stamp
// (dword_62EB38) that DisposeAll snapshots into the cache stamps.
void TexCache_SetFrameStamp62EB38(u32 v);

} // namespace guild::render

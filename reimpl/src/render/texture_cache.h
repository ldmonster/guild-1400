#pragma once
#include "guild/common/types.h"
#include <vector>

// =============================================================================
// guild::render — LRU texture-TILE cache. Faithful 1:1 reconstruction of the
// gilde.exe terrain-tile cache (d3_fl:TextureCache):
//
//   0x5b9368  VIBE_TextureCache_Init        (alloc N * 68-byte slots, zero them)
//   0x5ba03c  VIBE_TextureCache_FindLruSlot (free slot or evict lowest LRU stamp)
//   0x5ba0b0  VIBE_TextureCache_LookupTile  (build 25-byte signature, CRC, match)
//   0x5ba1e8  VIBE_TextureCache_GetOrBuildTile (lookup-or-claim a slot)
//
// CACHE SLOT — 68-byte record (17 dwords), the original indexes `base + 68*i`:
//   [+0]  -> built tile data (0 = free/empty)
//   [+4]  LRU timestamp (== global frame counter dword_649D58)
//   [+8]  tile size key   (a5)
//   [+12] CRC signature key
//   [+16] source U origin, [+20] source V origin, ... (geometry)
//   [+36..+60] 25-byte downsample signature scratch
//
// THE SIGNATURE (reconcile with the source texel buffer)
// ---------------------------------------------------------------------------
// LookupTile reads a small NxN block of source palette indices (N derived from
// the tile size), wrapping coordinates with (width-1), packs them into a 25-byte
// buffer and CRCs it (+ dword_649D60 scene salt). Two source regions with the
// same downsampled fingerprint share a cached tile. BuildSignature() reproduces
// that sampling so a test can verify a known source region hashes to the same
// key the cache lookup uses.
// =============================================================================
namespace guild::render {

// One 68-byte cache slot (17 dwords). `data` non-zero == occupied.
struct TileSlot {
    u32 data   = 0;   // [+0]  built tile id/ptr (0 = free)
    u32 stamp  = 0;   // [+4]  LRU timestamp
    i32 size   = 0;   // [+8]  tile-size key (a5)
    u32 key    = 0;   // [+12] CRC signature
    i32 srcU   = 0;   // [+16] source U origin
    i32 srcV   = 0;   // [+20] source V origin
    i32 width  = 0;   // [+24] source width used
    u8  sig[28] = {}; // [+36..] 25-byte signature scratch (pad to dword)
};

// Build the 25-byte downsample signature of a source region. `src` is the
// source 8-bit palette-index buffer of side `width` (power-of-two). `u0/v0` are
// the tile's source origin, `n` the sampled block side (the original derives n
// from the tile size; total samples are capped at 25). Wraps with (width-1).
// Returns the number of signature bytes written (<=25).
int BuildSignature(u8 outSig[25], const u8* src, int width,
                   int u0, int v0, int n);

// ---------------------------------------------------------------------------
// LRU tile cache.
// ---------------------------------------------------------------------------
struct TileCache {
    std::vector<TileSlot> slots;  // dword_64A040 base, 68-byte stride
    u32 frame = 0;                // dword_649D58 global frame counter (LRU)
    u32 salt  = 0;                // dword_649D60 scene/world signature salt

    // gilde.exe 0x5b9368 — Init: allocate `capacity` zeroed slots.
    explicit TileCache(int capacity) : slots((size_t)capacity) {}

    // Advance the LRU clock (the engine bumps dword_649D58 once per frame).
    void NextFrame() { ++frame; }

    // gilde.exe 0x5b9f54 — VIBE_TextureCache_Reset. Clears every cache slot back
    // to free: data(+0)=0, the +64 byte=0, stamp(+4)=0, size(+8)=0, key(+12)=0,
    // and the 25-byte signature scratch(+36..). The original also walks each '*'
    // group record at the slot's +0 (releasing ref'd entries while +64-record-ref
    // > 0) and invalidates the active floor + all 64 world-floor tile sets; those
    // are foreign subsystems (the texture-record bank + the floor tile grid), so
    // they are surfaced through `releaseGroup` (called once per occupied slot with
    // its data id) and `invalidateFloors` (called once at the end). Pass nullptr
    // for either to skip it (pure cache reset). Does NOT touch `frame`/`salt`.
    void Reset(void (*releaseGroup)(u32 dataId, void* ctx) = nullptr,
               void (*invalidateFloors)(void* ctx) = nullptr,
               void* ctx = nullptr);

    // gilde.exe 0x5ba03c — VIBE_TextureCache_FindLruSlot. Returns the index of a
    // free slot, or the index of the least-recently-used occupied slot (evicting
    // it). The evicted slot's stamp is refreshed to `frame`. Returns -1 only if
    // the cache is empty (capacity 0).
    int FindLruSlot();

    // gilde.exe 0x5ba0b0 — VIBE_TextureCache_LookupTile. Computes the signature
    // CRC for the region and searches for an occupied slot with matching `size`
    // and `key`. On hit, refreshes its stamp and returns its index (also writes
    // the computed key to *outKey). On miss returns -1.
    int LookupTile(const u8* src, int width, int u0, int v0, int n, int size,
                   u32* outKey);

    // gilde.exe 0x5ba1e8 — VIBE_TextureCache_GetOrBuildTile. Lookup; on miss
    // claim an LRU slot, stamp it with the region's key/size/origin and mark it
    // occupied (data = `tileId`). Returns the slot index (hit or freshly built).
    int GetOrBuildTile(const u8* src, int width, int u0, int v0, int n, int size,
                       u32 tileId);
};

} // namespace guild::render

#include "render/texture_cache.h"
#include "compress/crc.h"

#include <cstring>

// =============================================================================
// guild::render LRU tile cache — implementation. The signature CRC reuses the
// engine's standard reflected CRC-32 (VIBE_Util_Crc32 @0x5dc6e0 is byte-for-byte
// the same algorithm as compress::CrcCompute: init ~0, poly 0xEDB88320, final ~;
// CrcCompute(0,d,n) == VIBE_Util_Crc32(d,n)).
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// gilde.exe 0x5ba0b0 (signature build excerpt). The original walks an NxN block
// of source palette indices, wrapping coordinates with mask = width-1:
//   for (row = 0; row < n; ++row)
//     base = width * (mask & (v0 + row));            ; v13 = v25*(v8 & v27)
//     for (col = 0; col < n; ++col)
//       sig[k++] = src[base + (mask & (u0 + col))];  ; v8 & v14++
// Samples are written into a 25-byte buffer; the engine caps the block so the
// total never exceeds 25. We honour that cap explicitly.
// ---------------------------------------------------------------------------
int BuildSignature(u8 outSig[25], const u8* src, int width,
                   int u0, int v0, int n) {
    std::memset(outSig, 0, 25);
    const int mask = width - 1;
    int k = 0;
    for (int row = 0; row < n && k < 25; ++row) {
        int base = width * (mask & (v0 + row));
        for (int col = 0; col < n && k < 25; ++col) {
            outSig[k++] = src[base + (mask & (u0 + col))];
        }
    }
    return k;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5b9f54 — VIBE_TextureCache_Reset.
//   for i in [0, dword_64A034):                      ; slot count
//     slot = base + 68*i
//     if (slot.data /*+0*/) {
//        if (*(_BYTE*)slot.data == 42 /* '*' */)      ; a group-record back-link
//           while ((int)record[16] /*ref*/ > 0) VIBE_Texture_ReleaseEntry(record);
//        slot.data = 0;                               ; *(+0) = 0
//     }
//     slot.+64 = 0 ; slot.stamp(+4) = 0 ; slot.size(+8) = 0 ; slot.key(+12) = 0
//     memset(slot+36, 0, 25);                         ; the signature scratch
//   if (dword_64A028) Floor_InvalidateTiles(dword_64A028);   ; active floor
//   for each world floor in dword_13ECF74[...]: InvalidateTiles(floor)
// The group release + floor invalidation are foreign subsystems surfaced through
// the two callbacks; the cache-slot clear is reproduced exactly.
// ---------------------------------------------------------------------------
void TileCache::Reset(void (*releaseGroup)(u32 dataId, void* ctx),
                      void (*invalidateFloors)(void* ctx), void* ctx) {
    for (size_t i = 0; i < slots.size(); ++i) {
        TileSlot& s = slots[i];
        if (s.data != 0) {                  // *(+0) != 0 : occupied
            if (releaseGroup)
                releaseGroup(s.data, ctx);  // the '*' group-record release walk
            s.data = 0;                     // *(+0) = 0
        }
        // slot.+64 = 0 — the sig buffer's trailing pad byte (modelled in sig[]).
        s.stamp = 0;                        // *(+4) = 0
        s.size  = 0;                        // *(+8) = 0
        s.key   = 0;                        // *(+12) = 0
        std::memset(s.sig, 0, sizeof(s.sig)); // memset(slot+36, 0, 25) + pad
    }
    if (invalidateFloors)
        invalidateFloors(ctx);              // Floor_InvalidateTiles sweep
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5ba03c — VIBE_TextureCache_FindLruSlot.
//   walk slots: if a slot is free (data==0) use it (stamp it `frame`);
//   else track the lowest stamp; if no free slot, evict that LRU slot, clear
//   its old tile back-pointer and stamp it `frame`.
// ---------------------------------------------------------------------------
int TileCache::FindLruSlot() {
    if (slots.empty())
        return -1;
    u32 bestStamp = frame;      // dword_649D58 (v1)
    int lruIdx = -1;            // v4
    for (size_t i = 0; i < slots.size(); ++i) {
        if (slots[i].data == 0) {
            slots[i].stamp = frame;     // result[1] = dword_649D58
            return (int)i;
        }
        if (bestStamp > slots[i].stamp) {
            lruIdx = (int)i;
            bestStamp = slots[i].stamp;
        }
    }
    // No free slot: evict the LRU one (clear it, refresh stamp).
    if (lruIdx >= 0) {
        slots[(size_t)lruIdx].data  = 0; // *(v5+88)=0 — drop the old tile link
        slots[(size_t)lruIdx].stamp = frame;
    }
    return lruIdx;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5ba0b0 — VIBE_TextureCache_LookupTile.
//   key = CRC32(signature, 25) + salt
//   search occupied slots for (size == slot.size && key == slot.key)
//   on hit: refresh slot.stamp = frame, return index.
// ---------------------------------------------------------------------------
int TileCache::LookupTile(const u8* src, int width, int u0, int v0, int n,
                          int size, u32* outKey) {
    u8 sig[25];
    BuildSignature(sig, src, width, u0, v0, n);
    u32 key = compress::CrcCompute(0, sig, 25) + salt;  // VIBE_Util_Crc32 + dword_649D60
    if (outKey)
        *outKey = key;
    for (size_t i = 0; i < slots.size(); ++i) {
        TileSlot& s = slots[i];
        if (s.data != 0 && s.size == size && s.key == key) {
            s.stamp = frame;                   // *(result+4) = dword_649D58
            return (int)i;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5ba1e8 — VIBE_TextureCache_GetOrBuildTile.
//   if LookupTile hits, return that slot.
//   else claim FindLruSlot, fill key/size/origin, mark occupied (data=tileId).
// ---------------------------------------------------------------------------
int TileCache::GetOrBuildTile(const u8* src, int width, int u0, int v0, int n,
                              int size, u32 tileId) {
    u32 key = 0;
    int hit = LookupTile(src, width, u0, v0, n, size, &key);
    if (hit >= 0)
        return hit;
    int idx = FindLruSlot();
    if (idx < 0)
        return -1;
    TileSlot& s = slots[(size_t)idx];
    s.key   = key;     // v8[3] = v24
    s.size  = size;    // v8[8] = a5
    s.srcU  = u0;      // v8[5] = v28
    s.srcV  = v0;      // v8[6] = v25
    s.width = width;
    s.data  = tileId ? tileId : 1; // *v8 = built tile (non-zero == occupied)
    s.stamp = frame;
    return idx;
}

} // namespace guild::render

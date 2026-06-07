#pragma once
// Market price cache lookup — the fast path the trade/market panels use to read a
// good's current price without recomputing the supply/demand model every frame.
// Translated from gilde.exe:
//
//   VIBE_Building_LookupCachedMarketPrice  0x58f6b8
//     (__usercall, st0 = (goodId@ax, currencyId@dl))
//
// The original keeps a per-currency cache of up to 62 entries (128-byte stride,
// 7952-byte per-currency block) at dword_13C3B5C; each entry stores a key dword
// whose HIGH word is the good id (entry+2, read as `dword >> 16`) and a price
// float 60 bytes later (entry+60, the parallel flt_13C3B98 array). A linear scan
// returns the cached float on a key match; a miss falls back to the full price
// model VIBE_Building_ComputeMarketPrice(goodId, 100).
#include <cstddef>

#include "guild/common/types.h"

namespace guild::world {

// Cache geometry recovered from the scan bound and array deltas:
//   per-currency block stride = 7952 bytes (dword_13C3B5C indexed by 7952*currency)
//   entry stride              = 128 bytes
//   entries scanned           = 62          (v3: 0,128,..,7808; stops at >= 7936)
//   key dword                 = entry + 2   (good id == key >> 16)
//   price float               = entry + 60  (flt_13C3B98 - dword_13C3B5C == 60)
constexpr int kMarketCacheEntries     = 62;
constexpr int kMarketCacheEntryStride = 128;
constexpr int kMarketCacheScanBound   = 7936;   // kMarketCacheEntries*128
constexpr int kMarketCacheBlockStride = 7952;   // per-currency block

// One cache entry, modeled at the two fields the lookup reads.
struct MarketCacheEntry {
    i32   key   = 0;   // entry+2 dword; good id == key >> 16 (high word)
    float price = 0.0f; // entry+60 float (parallel flt_13C3B98)
};

// A per-currency cache view: `entries[currency*entriesPerBlock + slot]`. The
// caller owns the flat storage; this view supplies the geometry.
struct MarketPriceCache {
    const MarketCacheEntry* entries = nullptr;
    int entriesPerBlock = kMarketCacheEntries;  // 62 in the original
    int blockCount      = 0;                     // number of currency blocks
};

// Fallback the original calls on a cache miss:
//   VIBE_Building_ComputeMarketPrice(goodId, 100). The full model lives in the
// building/economy layer; callers inject it. (gilde.exe 0x58f3d0.)
using MarketPriceComputeFn = float (*)(i16 goodId, void* ctx);

// gilde.exe 0x58f6b8 — VIBE_Building_LookupCachedMarketPrice.
// Scans the `currencyId` block for the entry whose key high-word equals `goodId`
// and returns its cached price. On a miss (or no cache) invokes `fallback`
// (passing `ctx`); if `fallback` is null the miss returns 0.0f.
// NOTE (faithful quirk): a zeroed cache entry has key 0, so `goodId == 0` matches
// the first empty slot and returns its (zero) price — exactly as the original.
// Real good ids are 1-based; id 0 means "none".
float MarketLookupCachedPrice(const MarketPriceCache& cache, i16 goodId,
                              u8 currencyId, MarketPriceComputeFn fallback,
                              void* ctx);

} // namespace guild::world

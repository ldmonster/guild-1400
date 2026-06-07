#include "world/market_price.h"

// Cached market-price lookup (gilde.exe 0x58f6b8). The original walks a flat byte
// cache by raw offset; here the same scan is expressed over a typed entry view,
// with the per-currency block selected by index.

namespace guild::world {

// gilde.exe 0x58f6b8 — VIBE_Building_LookupCachedMarketPrice.
//   v3 = 0;
//   while (1) {
//     v4 = v3 + 7952 * currency;
//     if ( *(int*)(cache + v4 + 2) >> 16 == goodId ) break;   // key high word
//     v3 += 128;
//     if ( v3 >= 7936 ) return ComputeMarketPrice(goodId, 100);
//   }
//   return *(float*)(priceArray + v4);                        // entry+60 float
float MarketLookupCachedPrice(const MarketPriceCache& cache, i16 goodId,
                              u8 currencyId, MarketPriceComputeFn fallback,
                              void* ctx) {
    if (cache.entries && currencyId < cache.blockCount) {
        const MarketCacheEntry* block =
            cache.entries + static_cast<size_t>(currencyId) * cache.entriesPerBlock;
        const int n = cache.entriesPerBlock < kMarketCacheEntries
                          ? cache.entriesPerBlock
                          : kMarketCacheEntries;   // v3 bound: < 7936 -> 62 slots
        for (int slot = 0; slot < n; ++slot) {
            // Match the original's signed-arithmetic-shift key compare: the good
            // id is the high 16 bits of the key dword.
            if ((block[slot].key >> 16) == goodId)
                return block[slot].price;
        }
    }
    // Miss (or no cache): fall back to the full price model.
    if (fallback)
        return fallback(goodId, ctx);
    return 0.0f;
}

} // namespace guild::world

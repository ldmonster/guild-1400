#pragma once
// Full market price model — the supply/component price computation behind the
// cache (world/market_price.h). Translated from gilde.exe:
//
//   VIBE_Building_ComputeMarketPrice  0x58f3d0
//     (__usercall, st0 = (goodId@ax, currencyId@dl))
//
// The original prices a good from a 65-byte "good/building" record
// (dword_13CE27C + 65*goodId) plus a 589-byte type descriptor, recursing into the
// good's component recipe (up to four sub-goods) and writing the un-currency-
// scaled base back into the record cache (record+56). Once cached, a good is
// repriced by the cheap branch: base * 32 * currency * 0.01 (* 1.5 for flag-3
// goods).
//
// CONSTANTS (recovered byte-for-byte):
//   flt_626948 = 0.01          (1/100, currency scale)
//   flt_62694C = 1.5           (flag-3 multiplier on cached goods)
//   flt_626950 = 28.0  flt_626954 = 32.0
//   dbl_62695C = 1/12  dbl_626964 = 1/60   (type-need accumulation)
//   dbl_62696C = 0.5           (base half-weight)
//   flt_626974 = 2.2           (category-23 multiplier)        (== kPriceGain)
//   flt_626978 = 0.03125       (1/32, cache-write scale)
//
// RECORD FIELDS read (offsets into the 65-byte good record):
//   +0   byte  category   (23 == a "raw"/special good: skip the recipe)
//   +33  byte  flag       (3 -> the cached repricing gets the 1.5 multiplier)
//   +34  dword value      (numerator of the base ratio)
//   +54  word  divisor    (denominator; 0 is treated as 1)
//   +56  dword cachedBase (0 == not yet computed; written on first compute)
//   +46  word[4]          component good ids (0 ends, 0xFFFF == "named, no price")
//   +38  word             per-component quantity (read off the component slot)
//   +44  dword            component good id source (good == field >> 16)
// TYPE descriptor: byte_13CE862[goodId] selects a 589-stride type record; the
//   model sums (i16)typeRec[+563] over two entries with the 28*32/12/60 weight.
#include "guild/common/types.h"

namespace guild::world {

namespace mp {
constexpr float  kCurrencyScale  = 0.009999999776482582f; // flt_626948 (1/100)
constexpr float  kFlag3Mult      = 1.5f;                  // flt_62694C
constexpr float  kTypeWeightA    = 28.0f;                 // flt_626950
constexpr float  kTypeWeightB    = 32.0f;                 // flt_626954
constexpr double kTwelfth        = 1.0 / 12.0;            // dbl_62695C
constexpr double kSixtieth       = 1.0 / 60.0;            // dbl_626964
constexpr double kHalf           = 0.5;                   // dbl_62696C
constexpr float  kPriceGain      = 2.200000047683716f;    // flt_626974 (2.2)
constexpr float  kCacheScale     = 0.03125f;              // flt_626978 (1/32)
constexpr float  kNoTypeNeed     = 4.0f;                  // v27 when type index 0
constexpr int    kMaxComponents  = 4;
} // namespace mp

// A good record (the 65-byte good/building record fields the model reads).
struct GoodRecord {
    u8  category   = 0;     // +0
    u8  flag       = 0;     // +33
    u32 value      = 0;     // +34
    u16 divisor    = 0;     // +54
    i32 cachedBase = 0;     // +56  (0 == not cached)
    // Component recipe: up to 4 (goodId, quantity) slots. id 0 ends the list;
    // id 0xFFFF means "named ingredient, no price contribution".
    struct Comp { u16 good = 0; u16 qty = 0; };
    Comp components[mp::kMaxComponents];
    // Type-need contribution: the original sums two bytes off the 589-type record
    // (typeRec+563 and +564). When typeIndex == 0 the model substitutes 4.0.
    u8  typeIndex  = 0;     // byte_13CE862[goodId]
    u8  typeNeed0  = 0;     // typeRec[+563]
    u8  typeNeed1  = 0;     // typeRec[+564]
};

// The good table the model resolves component ids against. `records[goodId]`.
struct GoodTable {
    GoodRecord* records = nullptr;  // mutable: the cache field is written back
    int count = 0;
};

// gilde.exe 0x58f3d0 — VIBE_Building_ComputeMarketPrice.
// Computes the current price of `goodId` in `currencyId`, recursing into the
// recipe and writing the un-scaled base into records[goodId].cachedBase on first
// compute. Faithful to the original's float arithmetic and constants.
float BuildingComputeMarketPrice(GoodTable& table, i16 goodId, u8 currencyId);

} // namespace guild::world

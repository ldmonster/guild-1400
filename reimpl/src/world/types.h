#pragma once
// World / economy record substrate for the Guild simulation (gilde.exe).
//
// This header recovers two layouts driven by the economy module:
//   1. The 756-byte City record (gilde.exe byte_13CD6A0, stride 756), loaded
//      from gamedata/cities/<name>.ini by VIBE_City_LoadDefinitionIni 0x507144.
//   2. The 28-entry interleaved good/profession economy table (gilde.exe
//      0x1234750, stride 16 bytes/entry) plus the per-category tuning that
//      VIBE_City_InitParameterTable 0x577a9c seeds it with.
//
// Field offsets come from the loader (City) and from
// VIBE_Economy_Compute* / InitParameterTable (good table).
#include <cstddef>

#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// City record  (gilde.exe byte_13CD6A0 @0x13CD6A0, stride 756 bytes/city)
// ===========================================================================
// Loaded recursively (up to 8 NachbarStadt neighbours) from
// gamedata/cities/<name>.ini. All offsets are byte offsets into the 756-byte
// record exactly as VIBE_City_LoadDefinitionIni writes them.
//
// The original is a flat byte blob addressed by raw offsets; we mirror it with
// a packed struct of the same size. Some INI keys are written as 4-byte ints
// even where one byte would do (e.g. Glaube), so the struct types match the
// store widths the loader actually uses (byte vs dword vs word).
constexpr int kCityStride   = 756;
constexpr int kCityMaxCount = 9;   // self + up to 8 NachbarStadt neighbours

// Population / splendour growth pair (Einwohner_%i / Prunk_%i): two ints.
GUILD_PACKED_BEGIN
struct GrowthPair {
    i32 year;   // +0x00  threshold year
    i32 value;  // +0x04  population / splendour level at/after that year
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(GrowthPair) == 8, "GrowthPair must be 8 bytes");

// One Umland (surrounding region) entry: 18 bytes. The loader writes a person
// count (+2 -> off 324 in region 0), a parse-int at +3 of the region base
// (off 325), and active-object words for the rest. Only the first three byte
// slots are populated by the loader; the remaining bytes are reserved.
GUILD_PACKED_BEGIN
struct UmlandRegion {
    i16 objectCount;  // +0x00  (region+322) active-object id / count word
    u8  personCount;  // +0x02  (region+324) active person-slot count byte
    u8  parsedValue;  // +0x03  (region+325) ParseInt of token[1]
    u8  pad4[14];     // +0x04..+0x11  reserved (region+326..339)
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(UmlandRegion) == 18, "UmlandRegion must be 18 bytes");

GUILD_PACKED_BEGIN
struct CityRecord {
    u16 name[32];            // +0x000 (+0)   Stadtname, UTF-16, 64 bytes
    i32 mapPos[2];           // +0x040 (+64)  KartenPosition (2 ints)
    u8  maxPlayer;           // +0x048 (+72)  MaxPlayer
    u8  faith;               // +0x049 (+73)  Glaube (dominant faith)
    u8  pad74[2];            // +0x04A (+74)  alignment to the dword fields
    i32 historyStart;        // +0x04C (+76)  HistorieStart (default 1400)
    i32 historyEnd;          // +0x050 (+80)  HistorieEnde
    u16 currency;            // +0x054 (+84)  Waehrung (currency object id, word@+42)
    u8  pad86[10];           // +0x056 (+86)  gap to +96
    u8  land;                // +0x060 (+96)  Land
    u8  language;            // +0x061 (+97)  Sprache
    u8  pad98[2];            // +0x062 (+98)  alignment to the growth tables
    GrowthPair einwohner[10];// +0x064 (+100) population growth pairs (10x8)
    GrowthPair prunk[10];    // +0x0B4 (+180) splendour growth pairs   (10x8)
    u8  pad260[4];           // +0x104 (+260) gap to +264
    i32 rainProb[4];         // +0x108 (+264) Regenwahrscheinlichkeit
    i32 snowProb[4];         // +0x118 (+280) Schneewahrscheinlichkeit
    i32 freezeProb;          // +0x128 (+296) Zufrierenwahrscheinlichkeit
    u8  privileges[11];      // +0x12C (+300) Privilegien[11] (low byte of each)
    u8  pad311[11];          // +0x137 (+311) gap to +322
    UmlandRegion umland[8];  // +0x142 (+322) 8 surrounding regions (8x18 = 144)
    u8  pad466[10];          // +0x1D2 (+466) gap to +476
    u8  verfassung[12];      // +0x1DC (+476) Verfassungsgesetze (constitution law ids)
    u8  pad488[36];          // +0x1E8 (+488) gap to +524
    u8  finanz[12];          // +0x20C (+524) Finanzgesetze
    u8  pad536[20];          // +0x218 (+536) gap to +556
    u8  straf[12];           // +0x22C (+556) Strafgesetze
    u8  pad568[36];          // +0x238 (+568) gap to +604
    u8  gilde[12];           // +0x25C (+604) Gildengesetze
    u8  pad616[12];          // +0x268 (+616) gap to +628
    u8  kirche[12];          // +0x274 (+628) Kirchengesetze
    u8  pad640[16];          // +0x280 (+640) gap to +656
    u8  diebeRaeuber[12];    // +0x290 (+656) DiebeRaeubergesetze
    u8  pad668[14];          // +0x29C (+668) gap to +682
    u16 importGoods[16];     // +0x2AA (+682) KONTOR Import (16 object ids)
    u16 exportGoods[16];     // +0x2CA (+714) KONTOR Export (16 object ids)
    u8  pad746[2];           // +0x2EA (+746) gap to +748
    i32 mapOffset[2];        // +0x2EC (+748) KartenOffset (2 ints)
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(CityRecord) == kCityStride, "CityRecord must be 756 bytes");

// Compile-time offset checks for the fields the economy/tax code reads.
static_assert(offsetof(CityRecord, mapPos)       == 64,  "mapPos @+64");
static_assert(offsetof(CityRecord, maxPlayer)    == 72,  "maxPlayer @+72");
static_assert(offsetof(CityRecord, faith)        == 73,  "faith @+73");
static_assert(offsetof(CityRecord, historyStart) == 76,  "historyStart @+76");
static_assert(offsetof(CityRecord, historyEnd)   == 80,  "historyEnd @+80");
static_assert(offsetof(CityRecord, currency)     == 84,  "currency @+84");
static_assert(offsetof(CityRecord, land)         == 96,  "land @+96");
static_assert(offsetof(CityRecord, language)     == 97,  "language @+97");
static_assert(offsetof(CityRecord, einwohner)    == 100, "einwohner @+100");
static_assert(offsetof(CityRecord, prunk)        == 180, "prunk @+180");
static_assert(offsetof(CityRecord, rainProb)     == 264, "rainProb @+264");
static_assert(offsetof(CityRecord, snowProb)     == 280, "snowProb @+280");
static_assert(offsetof(CityRecord, freezeProb)   == 296, "freezeProb @+296");
static_assert(offsetof(CityRecord, privileges)   == 300, "privileges @+300");
static_assert(offsetof(CityRecord, umland)       == 322, "umland @+322");
static_assert(offsetof(CityRecord, verfassung)   == 476, "verfassung @+476");
static_assert(offsetof(CityRecord, finanz)       == 524, "finanz @+524");
static_assert(offsetof(CityRecord, straf)        == 556, "straf @+556");
static_assert(offsetof(CityRecord, gilde)        == 604, "gilde @+604");
static_assert(offsetof(CityRecord, kirche)       == 628, "kirche @+628");
static_assert(offsetof(CityRecord, diebeRaeuber) == 656, "diebeRaeuber @+656");
static_assert(offsetof(CityRecord, importGoods)  == 682, "importGoods @+682");
static_assert(offsetof(CityRecord, exportGoods)  == 714, "exportGoods @+714");
static_assert(offsetof(CityRecord, mapOffset)    == 748, "mapOffset @+748");

// ===========================================================================
// Economy good/profession table  (gilde.exe 0x1234750, stride 16, 28 entries)
// ===========================================================================
// The originals address four interleaved arrays at fixed strides of 16 bytes:
//   word_1234750[2*g]  -> +0  drift weight   (u16)
//   word_1234752[2*g]  -> +2  contribution weight (u16)
//   word_1234754[2*g]  -> +4  capacity / cap (u16)
//   flt_1234758[4*g]   -> +8  demand/supply accumulator (float)
//   flt_123475C[4*g]   -> +12 price-delta output (float)
// We model it as a flat array of GoodSlot. The two dwords BEFORE 0x1234750
// (dword_1234748 / dword_123474C) are zeroed alongside the table by the init &
// demand loops (i: 0..112 step 4 over a 28*4-dword span) — they are the first
// two dwords of good slot 0's accumulator span and need no separate field.
constexpr int kGoodCategoryCount = 28;

GUILD_PACKED_BEGIN
struct GoodSlot {
    u16   driftWeight;    // +0x00  word_1234750: price-drift multiplier
    u16   contribWeight;  // +0x02  word_1234752: city-total contribution weight
    u16   cap;            // +0x04  word_1234754: capacity / demand cap
    u16   pad6;           // +0x06  (high half of the +4 dword; unused)
    float accum;          // +0x08  flt_1234758: demand or supply accumulator
    float priceDelta;     // +0x0C  flt_123475C: price-delta output
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(GoodSlot) == 16, "GoodSlot stride must be 16 bytes");

} // namespace guild::world

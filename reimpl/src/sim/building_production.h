#pragma once
// Building PRODUCTION-SLOT table + market-price + per-tick production math for
// the Guild simulation (gilde.exe). MODULE: buildings (namespace guild::sim).
//
// This file recovers, byte-for-byte, the two big runtime tables the production
// engine drives, and ports the leaf slot accessors + market price + the full
// per-building production tick.
//
//  (1) The PRODUCTION-SLOT table  (gilde.exe dword_13C3B00 base).
//      A flat array of per-building records, stride 1988 DWORDS = 7952 bytes,
//      256 buildings.  Each record holds a 16-byte header followed by 62 slot
//      entries of 32 DWORDS (128 bytes) each.  The original reaches it through a
//      family of base globals (dword_13C3B00, dword_13C3B50, dword_13C3B5C,
//      flt_13C3B80, dword_13C3B90, byte_13C3BA0 ...) all offset from the same
//      record base; the recovered field offsets are documented on ProdSlot /
//      ProdBuilding below.
//
//  (2) The PRODUCTION-SCHEDULE curve table  (gilde.exe dword_13CD6A0 base).
//      Per-building schedule keyframes the tick interpolates against. Stride 756
//      bytes = 189 DWORDS (matches RecalcAllProduction's `v2 += 756`).  The two
//      curves (input @ dword_13CD704.., output @ dword_13CD754..) are arrays of
//      up-to-9 (timeKey, value) keyframes addressed as paired DWORD columns; see
//      ProdSchedule below.
//
// The scene TYPE-DEF table (gilde.exe dword_13CE27C, stride 65) is read by
// ComputeMarketPrice and the slot loop for the per-type kind byte; we model the
// fields the production engine touches as SceneTypeDef.
//
// Mutations / scene-graph / Person-iter dependencies are routed through the
// IProductionHooks sink (mocked in tests); the leaf interpolation + table math
// is exact and independently testable.
//
// Translated functions:
//   VIBE_GameTime_PackToRecord       0x583304
//   VIBE_Building_ComputeMarketPrice 0x58f3d0
//   VIBE_Building_ComputeSlotInput   0x584d34
//   VIBE_Building_ComputeSlotOutput  0x584de8
//   VIBE_Building_ComputeSlotYield   0x584ec8
//   VIBE_Building_FindSlotByProt     0x5851fc
//   VIBE_Building_GetSlotYieldByProt 0x585198
//   VIBE_Building_ComputeSlotStats   0x583c74
//   VIBE_Building_RunProductionTick  0x5847a0
//   VIBE_Building_RecalcAllProduction 0x583c3c
#include <cstddef>

#include "guild/common/types.h"
#include "sim/building_types.h"

namespace guild::sim {

// ===========================================================================
// Scene TYPE-DEF table  (gilde.exe dword_13CE27C, stride 65)
// ===========================================================================
// Only the fields ComputeMarketPrice / the slot loop read are named. The slot
// loop reads the +0 kind byte; market price reads +0/+33/+34/+38/+44/+46/+54/+56.
constexpr int kSceneTypeStride = 65;

GUILD_PACKED_BEGIN
struct SceneTypeDef {
    u8  kind;            // +0x00  kind byte (2/6 = storage/room, 23/37 = special)
    u8  pad1[32];        // +0x01..+0x20
    u8  subtype;         // +0x21 (+33) subtype byte (3 => *1.5 market mult)
    i32 baseValue;       // +0x22 (+34) base value (unaligned dword)
    u16 priceField;      // +0x26 (+38) per-component price field
    u8  pad40[4];        // +0x28..+0x2B
    i32 compType;        // +0x2C (+44) component type packed; high word (+46) =
                         //   component prot id / 0xFFFF terminator (compProt).
    u8  pad48[6];        // +0x30..+0x35
    u16 divisor;         // +0x36 (+54) value divisor
    i32 cachedPrice;     // +0x38 (+56) cached computed market price (0 = recompute)
    u8  pad60[5];        // +0x3C..+0x40  (pad to 65)
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(offsetof(SceneTypeDef, subtype)     == 33, "subtype @+33");
static_assert(offsetof(SceneTypeDef, baseValue)   == 34, "baseValue @+34");
static_assert(offsetof(SceneTypeDef, priceField)  == 38, "priceField @+38");
static_assert(offsetof(SceneTypeDef, compType)    == 44, "compType @+44");
static_assert(offsetof(SceneTypeDef, divisor)     == 54, "divisor @+54");
static_assert(offsetof(SceneTypeDef, cachedPrice) == 56, "cachedPrice @+56");
static_assert(sizeof(SceneTypeDef) == kSceneTypeStride, "SceneTypeDef stride 65");

// Scene type-def table store (gilde.exe dword_13CE27C). g_sceneTypesLoaded mirrors
// the original's null-base bail.
constexpr int kSceneTypeCapacity = 1024;
extern SceneTypeDef g_sceneTypes[kSceneTypeCapacity];
extern bool g_sceneTypesLoaded;
SceneTypeDef* SceneTypeDefAt(int prot);   // &g_sceneTypes[prot] or nullptr

// gilde.exe byte_13CE862 — per-prot building-type remap used by market price.
constexpr int kSceneTypeRemap = 256;
extern u8 g_sceneTypeRemap[kSceneTypeRemap];   // byte_13CE862[prot] -> type code

// ===========================================================================
// PRODUCTION-SLOT table  (gilde.exe dword_13C3B00 base, stride 1988 dwords)
// ===========================================================================
constexpr int kProdSlotsPerBuilding = 62;
constexpr int kProdBuildingCapacity = 256;

// The originals address the slot table through a FAMILY of parallel column
// globals (dword_13C3B50, dword_13C3B54, dword_13C3B58, dword_13C3B5C,
// dword_13C3B60, dword_13C3B6C, dword_13C3B70/74/78/7C, flt_13C3B80,
// dword_13C3B90/94, flt_13C3B98, byte_13C3BA0 ...) that all share ONE 1988-DWORD
// per-building geometry and base 0x13C3B00.  A field at column-base `C` for slot
// `k` of building `b` is the DWORD at `1988*b + (C-0x13C3B00)/4 + 32*k` (the
// per-slot stride is 32 dwords; slots begin at dword index 4 of dword_13C3B50,
// i.e. column-relative offset +0x10 / record offset +0x60).  We therefore model
// the table as a flat DWORD store with typed column accessors, exactly mirroring
// that addressing (and the original's deliberate 16-byte spill on the last slot
// of the last building, absorbed by a small guard tail).
//
// Recovered slot-field column offsets (bytes, relative to the per-building record
// base 0x13C3B00) and their slot-relative offset (slot base = +0x60):
//   prot      dword_13C3B60 +0x60   (slot+0x00) prot id in high word (>>16)
//   fieldC    dword_13C3B6C +0x6C   (slot+0x0C)
//   cust0..3  dword_13C3B70 +0x70   (slot+0x10..+0x1C) customer counters
//   smoothIn  flt_13C3B80   +0x80   (slot+0x20) smoothed input
//   outBase   dword_13C3B90 +0x90   (slot+0x30)
//   outComp   dword_13C3B94 +0x94   (slot+0x34)
//   yield     flt_13C3B98   +0x98   (slot+0x38)
//   active    byte_13C3BA0  +0xA0   (slot+0x40) active flag (low byte)
// Tick header (per building, NOT per slot):
//   inValue   dword_13C3B50 +0x50,  inScale  dword_13C3B54 +0x54,
//   outValue  dword_13C3B58 +0x58,  outScale dword_13C3B5C +0x5C.
constexpr int kProdBuildingDwords = 1988;
constexpr int kProdSlotDwords     = 32;
constexpr int kProdSlotBaseDword  = 24;   // (0x60/4) slot[0] base dword
// flat store: 256 buildings * 1988 dwords + 16-dword spill guard.
constexpr int kProdStoreDwords = kProdBuildingDwords * kProdBuildingCapacity + 16;
extern i32 g_prodStore[kProdStoreDwords];

// Typed view of one production building (computes column dword indices on demand).
struct ProdBuilding {
    int b;   // building index
    i32& dword(int colByte) { return g_prodStore[kProdBuildingDwords * b + colByte / 4]; }
    // tick-header accessors
    i32& inValue()  { return dword(0x50); }
    i32& inScale()  { return dword(0x54); }
    i32& outValue() { return dword(0x58); }
    i32& outScale() { return dword(0x5C); }
    // slot column accessors (colByte is the record-base byte offset of the column)
    i32& slotCol(int colByte, int slot) {
        // slot base dword = 24 (0x60/4); column relative dword = (colByte-0x60)/4.
        return g_prodStore[kProdBuildingDwords * b + kProdSlotBaseDword
                           + (colByte - 0x60) / 4 + kProdSlotDwords * slot];
    }
    i32&   slotProtPacked(int s) { return slotCol(0x60, s); }
    i32&   slotFieldC(int s)     { return slotCol(0x6C, s); }
    i32&   slotCust0(int s)      { return slotCol(0x70, s); }
    i32&   slotCust1(int s)      { return slotCol(0x74, s); }
    i32&   slotCust2(int s)      { return slotCol(0x78, s); }
    i32&   slotCust3(int s)      { return slotCol(0x7C, s); }
    float& slotSmoothIn(int s)   { return reinterpret_cast<float&>(slotCol(0x80, s)); }
    i32&   slotOutBase(int s)    { return slotCol(0x90, s); }
    i32&   slotOutComp(int s)    { return slotCol(0x94, s); }
    float& slotYield(int s)      { return reinterpret_cast<float&>(slotCol(0x98, s)); }
    u8&    slotActive(int s)     { return *reinterpret_cast<u8*>(&slotCol(0xA0, s)); }
    // extra float bands ComputeSlotYield reads (slot+0x24 outBonus, slot+0x9 etc).
    float& slotF24(int s)        { return reinterpret_cast<float&>(slotCol(0x84, s)); }
};
ProdBuilding ProdBuildingAt(int idx);   // {idx}

// ===========================================================================
// PRODUCTION-SCHEDULE curve table  (gilde.exe dword_13CD6A0 base, stride 756)
// ===========================================================================
// Each building owns up-to-9 keyframes per curve. The tick reads paired columns:
//   input curve  : time @ dword_13CD704[2*k], value @ dword_13CD708[2*k],
//                   nextTime @ dword_13CD70C[2*k], nextValue @ dword_13CD710[2*k].
//   output curve : same shape at dword_13CD754 / 758 / 75C / 760.
// dword_13CD6A0[i] (byte) is the per-building "has-production" flag scanned by
// RecalcAllProduction.  We model the curve as an array of {time,value} keyframes.
constexpr int kSchedKeyframes = 9;
constexpr int kProdScheduleStrideBytes = 756;

struct ScheduleKey { i32 time; i32 value; };   // 8 bytes (naturally aligned)

struct ProdSchedule {
    u8  hasProduction;        // +0x00 (byte_13CD6A0) production-active flag
    u8  pad1[3];              // +0x01..+0x03
    // input curve keyframes: time @ +4 (dword_13CD704 == base+0x64 in orig; we
    // collapse the original's wide layout into contiguous (time,value) pairs —
    // the tick only ever reads them via the documented column accessors so the
    // observable interpolation is identical).
    ScheduleKey input[kSchedKeyframes];   // +0x04 .. +0x4B
    ScheduleKey output[kSchedKeyframes];  // +0x4C .. +0x93
    u8  pad148[kProdScheduleStrideBytes - 4 - 16 * kSchedKeyframes];
};
static_assert(sizeof(ProdSchedule) == kProdScheduleStrideBytes,
              "ProdSchedule stride 756 bytes");

extern ProdSchedule g_prodSchedules[kProdBuildingCapacity];
ProdSchedule* ProdScheduleAt(int idx);

// ===========================================================================
// Hooks for the cross-module dependencies (scene queries, Person iteration,
// command mutations). Mocked in tests; default inert.
// ===========================================================================
struct IProductionHooks {
    virtual ~IProductionHooks() = default;

    // gilde.exe VIBE_Building_ComputeSlotOutput's Person-iter sum: total output
    // contributed by workers assigned to slot `slot` of building `building`.
    virtual int SlotWorkerOutput(int building, int slot) {
        (void)building; (void)slot; return 0;
    }
    // gilde.exe VIBE_Building_ComputeSlotYield's stored-quantity lookup: the
    // quantity currently in the work slot (drives the supply ratio). -1 == none.
    virtual int SlotStoredQuantity(int building, int slot) {
        (void)building; (void)slot; return -1;
    }
    // gilde.exe VIBE_Building_SyncProductionState (net sync) — fire-and-forget.
    virtual void SyncProductionState(int building) { (void)building; }
    // gilde.exe VIBE_Building_RandomizeStockTransforms — fire-and-forget.
    virtual void RandomizeStockTransforms() {}
};
void SetProductionHooks(IProductionHooks* hooks);
IProductionHooks* ProductionHooks();

// byte_6477A1: the "player-controlled building index" the slot math special-cases
// (player buildings recompute live; AI buildings reuse the last value).
void SetPlayerBuildingIndex(int idx);
int  PlayerBuildingIndex();

// dword_764CE0 standalone/host flag (-1 == single-player; gates the post-tick sync).
void SetStandaloneFlag(int v);
int  StandaloneFlag();

void ResetProductionTables();   // test/setup helper

// ===========================================================================
// Translated functions.
// ===========================================================================

// gilde.exe 0x583304 — VIBE_GameTime_PackToRecord. Packs the live game-time
// record (qword_13CE852 shape) into the schedule comparison record: writes a
// season/day header and copies the hour/minute/second cursor. We expose it over
// explicit fields.
struct PackedTime {
    i32 dayBlock;   // out +0 (= in.day, but the original only reads +4/+6/+10)
    u16 yearTag;    // out +2 = in.minuteHi? (original writes in[0]+1400)
    u8  season;     // out +0 (= 1)
    u8  dayInSeason;// out +1 (= 3*(in.day%4)+1)
    u8  hour;       // out +4
    u8  minuteByte; // out +5 (= in[+6] low byte)
    i32 cursor;     // out +8 (= in[+10])
};

// gilde.exe 0x583304 — pack the live game-time fields into a PackedTime.
PackedTime GameTime_PackToRecord(i32 day, u16 hourWord, i32 minute, i32 second);

// gilde.exe 0x58f3d0 — VIBE_Building_ComputeMarketPrice (st0 ret, ax=prot, dl=qty).
// Type-table-driven price for `prot` at quantity `qty` (a2, usually 100). Uses
// the cached price (typeDef.cachedPrice) when set; otherwise recurses over the
// type's components and caches. Returns a price scaled by qty.
double Building_ComputeMarketPrice(i16 prot, u8 qty);

// gilde.exe 0x584d34 — VIBE_Building_ComputeSlotInput (eax=building, edx=slot).
int Building_ComputeSlotInput(int building, int slot);

// gilde.exe 0x584de8 — VIBE_Building_ComputeSlotOutput (eax=building, edx=slot).
int Building_ComputeSlotOutput(int building, int slot);

// gilde.exe 0x584ec8 — VIBE_Building_ComputeSlotYield (st0 ret, eax=building,
// edx=slot). The yield/price the slot is currently producing at.
double Building_ComputeSlotYield(int building, int slot);

// gilde.exe 0x5851fc — VIBE_Building_FindSlotByProt. &slot.inValue band (the
// original returns &dword_13C3B50[32*slot+4+1988*building]) for the slot whose
// prot matches, or nullptr.
i32* Building_FindSlotByProt(int building, i16 prot);

// gilde.exe 0x585198 — VIBE_Building_GetSlotYieldByProt. -1 if no matching slot.
double Building_GetSlotYieldByProt(int building, i16 prot);

// gilde.exe 0x5847a0 — VIBE_Building_RunProductionTick (ax ret, eax=building,
// edx=timeRecPtr). Interpolates the input/output schedule curves at the current
// time, scales by the per-mille factors, then refreshes every slot's smoothed
// input / output / yield. `nowDay`/`nowMinute` come from the packed time record.
void Building_RunProductionTick(int building, i32 nowDay, i32 nowMinute);

// gilde.exe 0x583c3c — VIBE_Building_RecalcAllProduction. Runs the tick for every
// active building (up to 4) flagged in the schedule table.
void Building_RecalcAllProduction(i32 nowDay, i32 nowMinute);

}  // namespace guild::sim

#pragma once
// Building-record + building-type-table layouts for the Guild simulation
// (gilde.exe). MODULE: buildings (namespace guild::sim).
//
// Two record families are recovered here:
//
//  1. The BUILDING object record. Buildings share the 169-byte Object record
//     (gilde.exe *(0x13CE298), stride 169, 256 slots — see sim/types.h
//     ObjectRec). The building-specific fields are NOT laid out as named struct
//     members in the binary; the originals reach them with raw byte offsets off
//     a `__int16*`/`char*` base. We therefore expose a thin BuildingRec view that
//     documents and accesses those offsets, recovered from the value/production
//     accessors:
//       +0   (word)  building TYPE index -> indexes the 589-byte type table
//       +2   (byte)  object kind/state (6,7 = sale/auction price modes)
//       +44  (dword) packed staff/equipment bitfield (read by EvalProductionRating)
//       +122 (dword) quality / value scalar  (*(int*)(base+122))
//       +128+i(byte) per-stat raw level, i in 0..4 (EvalProductionRating stat)
//       +154+i(byte) wear/condition state ... (presentation; not modelled)
//     Plus the production runtime fields on the *live* building, read by
//     ComputeCurrentOutput/ComputeMaxOutput:
//       +0   (word)  alive marker (0xFFFF == empty)
//       +8   (byte)  active flag (0 == inactive)
//       +10  (word)  current fill / stock level
//       +16  (float) base output at zero fill
//       +28  (float) output at full fill
//       +32  (float) fill capacity
//       +36  (int)   flat output bonus
//
//  2. The BUILDING-TYPE definition table (gilde.exe dword_13CE294, stride 589,
//     indexed by the building's +0 type word — the "AiPlayer/type-def parallel"
//     array from recon 04 §1). Type-record fields used by the rules core:
//       +0    (byte)  category/kind enum (1..0x1A)
//       +35   (word[]) up to 64 room/object type ids (terminated by 0)
//       +547  (byte)  output-product profession code
//       +553+i(byte)  input-good factor, i in 0..1
//       +559  (byte)  input-product profession code
//       +563+i(byte)  output-good / production factor, i in 0..1
//       +583  (byte)  security level
//       +585  (dword) room-worth base multiplier
//
// All offsets carry their byte position. Production runtime field offsets are
// expressed as small accessor helpers so the EvalProductionRating bit math stays
// readable while remaining byte-faithful.
#include <cstddef>

#include "guild/common/types.h"
#include "sim/types.h"

namespace guild::sim {

// ===========================================================================
// Building-type definition table  (gilde.exe dword_13CE294, stride 589)
// ===========================================================================
constexpr int kBuildingTypeStride = 589;   // matches recon "AiPlayer/type" stride

// The category/kind enum stored at type-record +0. Values observed in the
// classification accessors (VIBE_Building_MapTypeToCategory and friends).
namespace BuildingTypeKind {
constexpr u8 kStorage    = 10;   // IsStorageType: kind == 10
// Production kinds (IsProductionType): 11,12,13,16,28.
constexpr u8 kProduction1 = 11;
constexpr u8 kProduction2 = 12;
constexpr u8 kProduction3 = 13;
constexpr u8 kProduction4 = 16;
constexpr u8 kProduction5 = 28;
}  // namespace BuildingTypeKind

// In-record byte offsets into a type-definition record.
namespace BuildingTypeField {
constexpr int kKind          = 0;     // category/kind enum
constexpr int kRoomList      = 35;    // word[64], terminated by 0
constexpr int kOutputProf    = 547;   // output-product profession code (byte)
constexpr int kInputFactor   = 553;   // +553+i input-good factor (i in 0..1)
constexpr int kInputProf     = 559;   // input-product profession code (byte)
constexpr int kOutputFactor  = 563;   // +563+i output/production factor (i in 0..1)
constexpr int kSecurity      = 583;   // security level (byte)
constexpr int kMaxUpgradeLevel = 584; // max upgrade level for this type (byte)
                                      // ExGebUpgrade guard: +583 >= +584 -> top
constexpr int kRoomWorthMul  = 585;   // room-worth base multiplier (dword)
}  // namespace BuildingTypeField

// A recovered building-type definition record. Only the fields the rules core
// reads are named; the rest is byte padding so sizeof matches the stride.
GUILD_PACKED_BEGIN
struct BuildingTypeDef {
    u8  kind;              // +0x000  category/kind enum
    u8  pad1[34];          // +0x001..+0x022
    u16 roomList[64];      // +0x023 (+35) room/object type ids, 0-terminated
    u8  pad163[384];       // +0x0A3 (+163)..+0x222 (gap to +547)
    u8  outputProf;        // +0x223 (+547) output-product profession code
    u8  pad548[5];         // +0x224 (+548)..+0x228
    u8  inputFactor[2];    // +0x229 (+553) input-good factor [0..1]
    u8  pad555[4];         // +0x22B (+555)..+0x22E
    u8  inputProf;         // +0x22F (+559) input-product profession code
    u8  pad560[3];         // +0x230 (+560)..+0x232
    u8  outputFactor[2];   // +0x233 (+563) output/production factor [0..1]
    u8  pad565[18];        // +0x235 (+565)..+0x246
    u8  security;          // +0x247 (+583) security level (== current upgrade lvl)
    u8  maxUpgradeLevel;   // +0x248 (+584) highest upgrade level for this type
    i32 roomWorthMul;      // +0x249 (+585) room-worth base multiplier (unaligned)
    // total: 585 + 4 = 589
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(BuildingTypeDef) == kBuildingTypeStride,
              "BuildingTypeDef stride must be 589");
static_assert(offsetof(BuildingTypeDef, roomList)     == 35,  "roomList @+35");
static_assert(offsetof(BuildingTypeDef, outputProf)   == 547, "outputProf @+547");
static_assert(offsetof(BuildingTypeDef, inputFactor)  == 553, "inputFactor @+553");
static_assert(offsetof(BuildingTypeDef, inputProf)    == 559, "inputProf @+559");
static_assert(offsetof(BuildingTypeDef, outputFactor) == 563, "outputFactor @+563");
static_assert(offsetof(BuildingTypeDef, security)        == 583, "security @+583");
static_assert(offsetof(BuildingTypeDef, maxUpgradeLevel) == 584, "maxUpgradeLevel @+584");
static_assert(offsetof(BuildingTypeDef, roomWorthMul)    == 585, "roomWorthMul @+585");

// ===========================================================================
// Building object-record view (over the 169-byte ObjectRec / live building)
// ===========================================================================
// The originals address the building through a raw `__int16*`/`char*` with the
// offsets documented at the top of this file: the value/rating math reads the
// type at +0 (`589 * *a1`), staff bits at +44, stat levels at +128, and the live
// production fields at +8/+10/+16/+28/+32/+36. The id->record SCAN
// (VIBE_Building_FindById) instead keys the alive byte @+0 and id dword @+1 of
// the same 169-byte slot — i.e. the +0 type byte doubles as the non-zero "alive"
// marker, and the id occupies +1. The two views are reconciled like this:
//   * BuildingFindRecordById returns the raw ObjectRec (alive @+0, id @+1).
//   * BuildingRec below is the VALUE-MATH overlay (type @+0, production fields)
//     the game reaches through the object's linked building data; it is modeled
//     as a standalone record so the math is exact and independently testable.
GUILD_PACKED_BEGIN
struct BuildingRec {
    u16 typeIndex;     // +0x00  building type -> BuildingTypeDef table index
    u8  objectKind;    // +0x02  object kind/state (6,7 => price-mode adjust)
    u8  pad3[5];       // +0x03..+0x07
    u8  activeFlag;    // +0x08  0 == inactive (gates output)
    u8  pad9;          // +0x09
    u16 fillLevel;     // +0x0A  current fill / stock level
    u8  pad12[4];      // +0x0C..+0x0F
    float outZeroFill; // +0x10 (+16) output at zero fill
    u8  pad20[8];      // +0x14..+0x1B
    float outFullFill; // +0x1C (+28) output at full fill
    float fillCap;     // +0x20 (+32) fill capacity
    i32  outBonus;     // +0x24 (+36) flat output bonus
    u8   pad40[4];     // +0x28..+0x2B
    u32  staffBits;    // +0x2C (+44) packed staff/equipment bitfield
    u8   pad48[74];    // +0x30..+0x79
    i32  qualityScalar;// +0x7A (+122) quality / value scalar
    u8   pad126[2];    // +0x7E..+0x7F
    u8   statLevel[5]; // +0x80 (+128) per-stat raw level (stat 0..4)
    u8   pad133[36];   // +0x85..+0xA8  (pad out to 169 bytes)
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(offsetof(BuildingRec, objectKind)    == 2,   "objectKind @+2");
static_assert(offsetof(BuildingRec, activeFlag)    == 8,   "activeFlag @+8");
static_assert(offsetof(BuildingRec, fillLevel)     == 10,  "fillLevel @+10");
static_assert(offsetof(BuildingRec, outZeroFill)   == 16,  "outZeroFill @+16");
static_assert(offsetof(BuildingRec, outFullFill)   == 28,  "outFullFill @+28");
static_assert(offsetof(BuildingRec, fillCap)       == 32,  "fillCap @+32");
static_assert(offsetof(BuildingRec, outBonus)      == 36,  "outBonus @+36");
static_assert(offsetof(BuildingRec, staffBits)     == 44,  "staffBits @+44");
static_assert(offsetof(BuildingRec, qualityScalar) == 122, "qualityScalar @+122");
static_assert(offsetof(BuildingRec, statLevel)     == 128, "statLevel @+128");
static_assert(sizeof(BuildingRec) == kObjectStride, "BuildingRec stride must be 169");

}  // namespace guild::sim

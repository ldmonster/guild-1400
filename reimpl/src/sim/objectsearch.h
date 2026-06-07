#pragma once
// Spatial object-search queries over the Object/Building array, plus the round-
// robin object-ring iterator. Faithful 1:1 port from gilde.exe.
//
// The object array is dword_13CE298 (stride 169 / 0xA9, 256 slots). A slot is
// "alive" when its byte +0 is nonzero. Each search probes the 256 slots in a
// pseudo-random order (a random start + a random odd stride co-prime with 256,
// drawn from dword_478450), applies an entity filter (MatchEntityFilter), and
// optionally a favourability range gate.
//
// Object record fields used by the filter (offsets into the 169-byte record):
//   +0    u8   alive/type byte (0 == empty slot)
//   +1    i32  id
//   +39   u16  owner/faction word (-1 == none)
//   +90   u8   flags (bit0 used by status filter)
//   +97   i32  status dword
// Filter struct (the 16-byte `a2` block in MatchEntityFilter):
//   +8    u32  faction bitmask (membership test via AiPlayer table)
//   +11   i8   "require status" sign byte (<0 -> reject by +90/+97)
//   +12   u8   owner-match mode (0..7; see MatchEntityFilter)
//   +13   u8   flag byte (bit0 -> XOR-with-owner-list refinement)
//   +14   u16[4] owner-id list for the bit0 refinement
//
// Translated functions:
//   VIBE_ObjectSearch_MatchEntityFilter           0x559d98
//   VIBE_ObjectSearch_MatchEntityFilterWithStatus 0x559ff8
//   VIBE_ObjectSearch_FindNearestEntity           0x47b1d0
//   VIBE_ObjectSearch_FindEntitiesByCount         0x47b308
//   VIBE_ObjectRing_AdvanceIterator               0x4784cc
#include "guild/common/types.h"

namespace guild::sim {

constexpr int kSearchObjectStride   = 169;  // 0xA9
constexpr int kSearchObjectCapacity = 256;

// Object record field offsets (subset the search/filter touch).
constexpr int kObjAlive   = 0;
constexpr int kObjId      = 1;    // i32
constexpr int kObjOwner   = 39;   // u16
constexpr int kObjFlags90 = 90;   // u8
constexpr int kObjStatus97 = 97;  // i32

// The 16-byte entity filter (gilde.exe `a2` in MatchEntityFilter).
struct EntityFilter {
    u32 factionMask;     // +8  faction membership bitmask (0 == any)
    u8  _pad0c;          // +12 (overlapped by ownerMode below for clarity)
    // (We lay this out by explicit offsets in the .cpp via a byte view to match
    //  the original's mixed-width packed access; the struct here is the API.)
    i8  requireStatus;   // +11 sign byte: <0 rejects records lacking a status
    u8  ownerMode;       // +12 owner-match mode 0..7
    u8  refineFlag;      // +13 bit0 -> XOR refinement over ownerList
    u16 ownerList[4];    // +14 owner ids for refinement
};

// AiPlayer-table accessor: the filter's faction-membership test indexes
// aiPlayerTable[589 * record.type] (gilde.exe dword_13CE294). Supplied by the
// caller so the search stays self-contained for tests.
struct ObjectSearchContext {
    const u8* objectArray;    // dword_13CE298 base (256 * 169 bytes)
    const u8* aiPlayerTable;  // dword_13CE294 base (faction byte per type, 589 stride)
    u16       queryFaction;   // *a1 (the asking entity's faction, owner-mode key)
    u16       extraFaction;   // *dword_6498E4 (mode 7's extra allowed owner)
};

// gilde.exe 0x559d98 — VIBE_ObjectSearch_MatchEntityFilter.
// Returns true when `record` (a 169-byte object) passes `filter` under `ctx`.
bool ObjectSearchMatchEntityFilter(const ObjectSearchContext& ctx,
                                   const EntityFilter* filter, const u8* record);

// gilde.exe 0x559ff8 — VIBE_ObjectSearch_MatchEntityFilterWithStatus.
// Identical to MatchEntityFilter but adds two extra gates evaluated up front:
//   (1) if the record's faction bit is in the mask 0x0F82806F AND the filter's
//       +11 status byte has bit 0x40 set, the record is REJECTED outright;
//   (2) the original's "require status" sign check reads only the +90 flag bit0
//       (not the +97 dword that MatchEntityFilter also tests).
// All other logic (faction membership, owner-mode switch, owner-list XOR
// refinement) is identical. Returns true on a pass.
bool ObjectSearchMatchEntityFilterWithStatus(const ObjectSearchContext& ctx,
                                             const EntityFilter* filter,
                                             const u8* record);

// gilde.exe 0x47b1d0 — VIBE_ObjectSearch_FindNearestEntity.
// Probes the object array in pseudo-random order; returns the id of the first
// record passing the filter via *outId, true on success. `probeSeed` and
// `probeStart` replace the two RNG draws (so tests are deterministic); pass the
// values the live RNG would have produced. `strideIndex` picks the probe stride
// from kObjectProbeStrides.
bool ObjectSearchFindNearestEntity(const ObjectSearchContext& ctx,
                                   const EntityFilter* filter,
                                   int strideIndex, int probeStart, i32* outId);

// gilde.exe 0x47b308 — VIBE_ObjectSearch_FindEntitiesByCount.
// Collects up to `maxCount` matching record ids into `outIds`; returns the
// number collected. Same pseudo-random probe order as FindNearestEntity.
int ObjectSearchFindEntitiesByCount(const ObjectSearchContext& ctx,
                                    const EntityFilter* filter, int strideIndex,
                                    int probeStart, int maxCount, i32* outIds);

// Probe-stride table (gilde.exe dword_478450, 16 entries) — odd values co-prime
// with 256 so the probe visits every slot exactly once.
constexpr int kObjectProbeStrideCount = 16;
extern const int kObjectProbeStrides[kObjectProbeStrideCount];

// ---------------------------------------------------------------------------
// Object-ring iterator (gilde.exe 0x4784cc).
// ---------------------------------------------------------------------------
// A fixed-capacity ring of 12-byte records (dword_B596A0, 3 dwords each). The
// iterator returns the current record pointer and advances the cursor by one
// slot, wrapping modulo `count` (dword_62EB9C). Modeled over a caller buffer.
struct ObjectRing {
    u8* base;     // dword_B596A0 (record storage)
    int* cursor;  // off_4784C0[0]: current record pointer (as byte offset index)
    int  bias;    // off_4784C0[1]: rotation bias added each step
    int  count;   // dword_62EB9C: ring length (0 -> iterator yields base)
};

// Returns the current record's byte offset from `base` and advances the cursor.
// Mirrors VIBE_ObjectRing_AdvanceIterator: when count==0 it returns slot 0 and
// does not advance.
int ObjectRingAdvance(ObjectRing& ring);

} // namespace guild::sim

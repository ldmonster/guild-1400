#pragma once
// gilde.exe — Combat slot / target-selection LEAVES (namespace guild::sim).
// MODULE: combat (prefix VIBE_Combat_*), the deferred slot-table / target-busy /
// underfull / squad-slot-find leaves that the battle-loop module left untranslated.
//
// These operate on two flat fixed-stride tables (separate from the 32-slot
// combat-unit array word_B5A350 modelled by CombatField in combat.h):
//
//  * The PERSON / CHARACTER slot table  (gilde.exe word_12CE910 @0x12CE910):
//      stride 536 / 0x218, capacity 768. Marker word @+0 (-1 == free slot).
//      The squad-slot allocator (FindAvailableSquadSlot) and the underfull test
//      (IsTargetUnderfull) read a handful of typed views into this record; the
//      fields they touch are named in SlotRecord below, the rest is raw padding.
//
//  * The "active-target" ring  (gilde.exe dword_13CE298 base @0x13CE298):
//      169-byte stride, capacity 256, walked round-robin by PickActiveTargetEntry
//      from a persistent cursor (dword_642004). Entry byte+0 == a person-type
//      index into the type table dword_13CE294 (589-byte stride).
//
// The originals address static globals; this module exposes the same logic over
// caller-provided table pointers / struct views so the leaves stay testable in
// isolation, with no OS / render / animation coupling.
//
// CROSS-MODULE calls (forward-declared, test-stubbed): the building-type /
// relation helpers that FindAvailableSquadSlot consults, and the game-object
// query that ResolveTargetObjekt issues. See combat_slots.cpp.
#include "guild/common/types.h"
#include "sim/combat_types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Person / character slot record  (gilde.exe word_12CE910 @0x12CE910)
//   stride 536 / 0x218, 768 slots. Byte offsets recovered from the slot leaves
//   (FindAvailableSquadSlot @0x57e76c, IsTargetUnderfull @0x57e4c8,
//    AccumulateThreatStats @0x57eb64). Only the touched fields are named.
//     +0x000  marker word (-1 == free slot); low word also == slot/person id
//     +0x002  busy-rank byte (byte_12CE912; allocator gate "<= 1")
//     +0x004  type-group dword (dword_12CE914; byte +0x006 == group class)
//     +0x008  active byte (byte_12CE918; threat scan gate "!= 0")
//     +0x00A  fill-count word (high word of dword_12CE919; current squad size)
//     +0x020  required-capacity float (flt_12CE930; underfull threshold)
//     +0x161  desired-class dword (unk_12CEA71; byte +0x164 == desired class)
//     +0x162  owner-class dword (unk_12CEA72; byte +0x165 == owner class byte)
//     +0x164  owner-class byte  (byte_12CEA74)
//     +0x165  reserved-by byte  (byte_12CEA75; 0 == unreserved)
//     +0x16C  lock dword        (dword_12CEA7C; != 0 == locked out)
//     +0x027  relation-id word  (read at a1+39 in the searcher arg, not here)
//     +0x1C8  flags dword       (dword_12CEAD8; bit 0x2000 == in-use / claimed)
// ---------------------------------------------------------------------------
constexpr int kSlotStride   = 536;   // 0x218
constexpr int kSlotCapacity = 768;

GUILD_PACKED_BEGIN
struct SlotRecord {
    i16 marker;            // +0x000  (-1 == free)
    u8  busyRank;          // +0x002  byte_12CE912  (allocator gate "<= 1")
    u8  pad3;              // +0x003
    i32 typeGroup;         // +0x004  dword_12CE914 (byte +0x006 == group class)
    u8  active;            // +0x008  byte_12CE918  (threat scan gate)
    u8  groupClass;        // +0x009  dword_12CE919 byte 0; type-group class code
                           //         (compared to Building_IsTypeInGroup/ClassifyTypeFlag)
    u16 fillCount;         // +0x00A  current squad size (dword_12CE919 word @+1)
    u8  padC[20];          // +0x00C..+0x01F
    float requiredCap;     // +0x020  flt_12CE930  (underfull threshold)
    u8  pad24[320];        // +0x024..+0x163
    u8  ownerClassByte;    // +0x164  byte_12CEA74 (== unk_12CEA71 dword byte 3)
    u8  reservedBy;        // +0x165  byte_12CEA75 (== unk_12CEA72 dword byte 3;
                           //         0 == unreserved)
    u8  pad166[6];         // +0x166..+0x16B
    i32 lock;              // +0x16C  dword_12CEA7C (!= 0 == locked)
    u8  pad170[88];        // +0x170..+0x1C7
    i32 flags;             // +0x1C8  dword_12CEAD8 (bit 0x2000 == claimed)
    u8  pad1CC[76];        // +0x1CC..+0x217
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(SlotRecord) == kSlotStride, "SlotRecord stride must be 536");

// SlotRecord::flags bit (dword_12CEAD8 & 0x2000) — slot is claimed / in use.
constexpr i32 kSlotClaimed = 0x2000;

// ---------------------------------------------------------------------------
// Cross-module helpers consulted by FindAvailableSquadSlot. The originals call
// VIBE_Building_IsTypeInGroup @0x5898cc, VIBE_Building_ClassifyTypeFlag @0x589818
// and VIBE_Relation_LookupMatrixEntry @0x5942fc. They are forward-declared here
// as a small policy interface so the allocator can be tested in isolation.
//   * IsTypeInGroup(typeId)   — returns a group class (2 == wildcard "any").
//   * ClassifyTypeFlag(flag)  — returns a group class (2 == wildcard "any").
//   * RelationMatrix(rel, idx)— relation matrix entry; <= -30 == hostile/reject.
// ---------------------------------------------------------------------------
class ISlotPolicy {
public:
    virtual ~ISlotPolicy() = default;
    virtual int IsTypeInGroup(u8 typeId) = 0;       // VIBE_Building_IsTypeInGroup
    virtual int ClassifyTypeFlag(u8 flag) = 0;      // VIBE_Building_ClassifyTypeFlag
    virtual int RelationMatrix(u16 rel, int idx) = 0; // VIBE_Relation_LookupMatrixEntry
};

// gilde.exe 0x57e76c — VIBE_Combat_FindAvailableSquadSlot
//   (__usercall, eax = searcherRelationId@<eax>, dl = wantType, bl = wantFlag).
// Linear-scans all 768 slots of the person/character slot table for a free,
// unclaimed, class-matching slot, claims it (sets the kSlotClaimed flag bit) and
// returns it. Two modes:
//   * wantType != 0: requires the slot full enough (fillCount >= requiredCap),
//     owner-class byte == wantFlag, reservedBy in {0, wantType}, and the slot's
//     groupClass matching IsTypeInGroup(wantType) (or wildcard 2).
//   * wantType == 0 && wantFlag != 0: owner-class byte == wantFlag and groupClass
//     matching ClassifyTypeFlag(wantFlag) (or wildcard 2); no fill check.
//   * wantType == 0 && wantFlag == 0: returns nullptr.
// In both modes a non-0xFFFF searcherRelationId rejects slots whose relation
// matrix entry is <= -30.  Returns nullptr if no slot qualifies.
//
// `searcherRelationId` is the value the original reads from `*(u16*)(a1+39)`.
SlotRecord* FindAvailableSquadSlot(SlotRecord* table, u16 searcherRelationId,
                                   u8 wantType, u8 wantFlag, ISlotPolicy& policy);

// NOTE. The slot-table leaves IsTargetUnderfull (0x57e4c8), CountActiveSlots
// (0x4897e0) and AssignGuardTarget (0x57e714) — and GetSelectionFlag (0x486460),
// SelectBeatingTarget (0x57829c) — are ALREADY translated in combat_orders.cpp
// (extracted as plain-value RULES). This module reuses those rather than
// redefining them, and adds the remaining slot/target leaves below.

// ---------------------------------------------------------------------------
// Squad order-slot scan  (gilde.exe dword_11AB000 / dword_631208).
// VIBE_Combat_FindUnitByEntity @0x485ffc.
// ---------------------------------------------------------------------------

// The per-squad order block layout used by FindUnitByEntity: 215 dwords per
// squad, 16 order slots starting at dword +37, stride 11 dwords; the matched
// field is order-slot dword +0 (== the squad-record id at +37, +48, ...).
constexpr int kSquadBlockDwords = 215;
constexpr int kOrderSlotDwordStride = 11;
constexpr int kOrderSlotBase = 37;

// gilde.exe 0x485ffc — VIBE_Combat_FindUnitByEntity  (__usercall, eax = unit ptr).
// Scans `activeSquads` squad blocks (each kSquadBlockDwords dwords); within each,
// the 16 order slots (dword +kOrderSlotBase, stride kOrderSlotDwordStride). Returns
// a pointer to the matching slot's id field (block + 11*slot + 37) whose value
// equals `entityId`, else nullptr.
const i32* FindUnitByEntity(const i32* squadTable, int activeSquads, i32 entityId);

// ---------------------------------------------------------------------------
// Combat-unit target accessors / guard assignment
//   (these treat the in-scene unit record as a flat byte blob; the originals
//    address it via raw +N offsets, reproduced here over a byte view.)
// ---------------------------------------------------------------------------

// gilde.exe 0x487090 — VIBE_Combat_GetUnitTarget. Returns dword at unit+512.
i32 GetUnitTarget(const u8* unit);

// ---------------------------------------------------------------------------
// Active-target ring  (gilde.exe dword_13CE298 / cursor dword_642004).
//   PickActiveTargetEntry walks a 256-entry ring of 169-byte records from a
//   persistent cursor, returning the first entry whose type (via the 589-byte
//   type table dword_13CE294) is one of {3, 22, 15}; the cursor advances past it.
// ---------------------------------------------------------------------------
constexpr int kActiveRingStride = 169;
constexpr int kActiveRingCount  = 256;
constexpr int kTypeTableStride  = 589;

// Active-target type codes that PickActiveTargetEntry accepts.
constexpr u8 kTypeActiveA = 3;
constexpr u8 kTypeActiveB = 22;
constexpr u8 kTypeActiveC = 15;

// ---------------------------------------------------------------------------
// gilde.exe 0x57e92c — VIBE_Combat_ResolveTargetObjekt  (__usercall, eax = entry).
// Resolves the in-scene object a ring entry refers to. The entry's person index
// (byte +0) selects a person-type code from the 589-byte type table; if that
// code == 2 the lookup prefers object kind 19 (else 18), falling back to kind 288.
// All three queries pass the entry's entity handle (dword at entry +93).
//
// The scene query is the cross-module VIBE_GameObject_QueryFind @0x5857fc,
// forward-declared as IObjectQuery so the resolver stays testable.
//   if (typeCode(entry) == 2)
//       r = Query(handle, 1, 0, 19); if (r) return r; return Query(handle, 1, 0, 288);
//   else
//       r = Query(handle, 1, 0, 18); if (r) return r; return Query(handle, 1, 0, 288);
class IObjectQuery {
public:
    virtual ~IObjectQuery() = default;
    // VIBE_GameObject_QueryFind(handle, 1, prev, kind) — returns an object ptr
    // (opaque) or nullptr. `prev` is the iteration seed (0 == first).
    virtual const void* QueryFind(i32 handle, int prev, int kind) = 0;
};
// The entry layout the resolver reads:
//   +0   person-type index (byte; selects type code from typeTable)
//   +93  entity handle (dword)
constexpr int kEntryPersonIdxOff = 0;
constexpr int kEntryHandleOff    = 93;
const void* ResolveTargetObjekt(const u8* entry, const u8* typeTable,
                                IObjectQuery& query);

// gilde.exe 0x57e9a4 — VIBE_Combat_PickActiveTargetEntry  (__usercall,
//   eax = squad-state ptr, edx = &outTypeId, ebx = &outEntityRef).
// If the squad already has an explicit objective ptr (state+368 != 0): resolve it
// via ResolveTargetObjekt, write *outEntityRef and *outTypeId from it, return the
// objective ptr. Otherwise round-robin the active-target ring from the persistent
// cursor; on the first accepted entry advance the cursor, write *outEntityRef = -1
// and *outTypeId = entry dword+1, and return the entry ptr. Returns nullptr after
// scanning all 256 without a hit.
//
// Modelled with the ring + type table + cursor passed explicitly (the cursor is
// read-modify-write, so it is an in/out reference).
struct ActiveRing {
    const u8* base;        // dword_13CE298 (169-byte stride records)
    const u8* typeTable;   // dword_13CE294 (589-byte stride; byte+0 == type code)
    int*      cursor;      // dword_642004  (persistent round-robin cursor)
};
// The "objective" path is resolved through ResolveObjektFn (ResolveTargetObjekt).
// objectivePtr == nullptr selects the ring path.
struct ActiveTargetResult {
    const u8* entry = nullptr;   // returned ring/objective entry ptr
    i32 typeId = 0;              // *outTypeId
    i32 entityRef = 0;          // *outEntityRef
};
ActiveTargetResult PickActiveTargetEntry(const ActiveRing& ring);

} // namespace guild::sim

#pragma once
// Building LIFECYCLE (remove/cleanup, slot free) + nearest-of-type query for the
// Guild simulation (gilde.exe). MODULE: buildings (namespace guild::sim).
//
// In the original, a "building" is a record in the PERSON array (word_12CE910,
// stride 536) — VIBE_Building_RemoveAndCleanup indexes word_12CE910[268*a1] and
// frees that 536-byte slot. We recover the building-as-person fields the cleanup
// path touches and model the slot store + the free path byte-faithfully. The
// trade-route table clearing, the history "rival" notification, the scene
// child-list free, the character destroy, and the production type-table decrement
// are routed through ILifecycleHooks (mocked in tests).
//
// Translated functions:
//   VIBE_Building_RemoveAndCleanup  0x5894b0
//   VIBE_Building_FindNearestSameType 0x587908
#include <cstddef>

#include "guild/common/types.h"
#include "sim/types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Building-as-Person record fields the cleanup path reads/writes (byte offsets
// into the 536-byte Person record). Recovered from RemoveAndCleanup @0x5894b0.
//   +0   (word)  marker (-1 == free slot)
//   +2   (byte)  kind/state byte (15 == "removed/destroyed")
//   +4   (dword) building id
//   +8   (byte)  active flag (cleared on remove)
//   +20  (word)  removal timestamp word (= qword_13CE852 low word)
//   +92  (dword) production-type record ptr (type-table decrement)
//   +97  (dword) live character handle (destroyed on remove)
//   +93*4? the original uses *((_DWORD*)v2 + 97) == +388 (char handle) and
//          *((_DWORD*)v2 + 92) == +368 (type record). We name them explicitly.
// ---------------------------------------------------------------------------
GUILD_PACKED_BEGIN
struct BuildingPersonRec {
    i16 marker;        // +0x000  -1 == free slot
    u8  kind;          // +0x002  15 == destroyed
    u8  pad3[5];       // +0x003..+0x007
    u8  activeFlag;    // +0x008
    u8  pad9[7];       // +0x009..+0x00F
    u16 removalTs;     // +0x010 (word index 8 == +16) removal timestamp
    u8  pad18[350];    // +0x012..+0x16F
    i32 charHandle;    // +0x170 (dword index 92 == +368) live character handle
    u8  pad372[16];    // +0x174..+0x183
    i32 typeRecord;    // +0x184 (dword index 97 == +388) production type record
    u8  pad392[144];   // +0x188..+0x217
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(offsetof(BuildingPersonRec, activeFlag) == 8,    "activeFlag @+8");
static_assert(offsetof(BuildingPersonRec, removalTs)  == 16,   "removalTs @+16");
static_assert(offsetof(BuildingPersonRec, charHandle) == 368,  "charHandle @+368");
static_assert(offsetof(BuildingPersonRec, typeRecord) == 388,  "typeRecord @+388");
static_assert(sizeof(BuildingPersonRec) == kPersonStride, "stride 536");

// The building/person slot store (aliases the Person array word_12CE910).
constexpr int kBuildingSlots = kPersonCapacity;   // 768
extern BuildingPersonRec g_buildingPersons[kBuildingSlots];
BuildingPersonRec* BuildingPersonAt(int slot);
void ResetBuildingPersons();

struct ILifecycleHooks {
    virtual ~ILifecycleHooks() = default;

    // gilde.exe VIBE_Building_ReleaseOccupantHoldings — release the building's
    // occupants' holdings (commands). Fire-and-forget.
    virtual void ReleaseOccupantHoldings(int slot) { (void)slot; }
    // gilde.exe trade-route table clearing (dword_11BC760/dword_11C2160): remove
    // every trade route whose endpoint id == this building's id. Returns the
    // number of routes that pointed at a sibling owner (drives the rival notify).
    virtual int ClearTradeRoutes(i32 buildingId) { (void)buildingId; return 0; }
    // gilde.exe VIBE_History_NotifyRivalEvent — log a rival-affecting removal.
    virtual void NotifyRivalEvent(int siblingSlot, int slot) {
        (void)siblingSlot; (void)slot;
    }
    // gilde.exe VIBE_GameObject_FreeChildList — free the scene child list.
    virtual void FreeChildList(int slot) { (void)slot; }
    // gilde.exe VIBE_Character_Destroy — destroy the live character handle.
    virtual void DestroyCharacter(i32 charHandle) { (void)charHandle; }
    // gilde.exe production type-table decrement: if the building's production
    // type record has kind 1, decrement its +101 active-count byte.
    virtual void DecrementTypeActiveCount(i32 typeRecord) { (void)typeRecord; }
    // gilde.exe FindNearestSameType's distance probe: world distance^2 between
    // building `slot` and `otherSlot`. Default 0 (co-located).
    virtual float BuildingDistanceSq(int slot, int otherSlot) {
        (void)slot; (void)otherSlot; return 0.0f;
    }
};
void SetLifecycleHooks(ILifecycleHooks* hooks);
ILifecycleHooks* LifecycleHooks();

// Global build counter (dword_647724) — decremented when a live slot frees.
void SetBuildCounter(int v);
int  BuildCounter();

// ---------------------------------------------------------------------------
// gilde.exe 0x5894b0 — VIBE_Building_RemoveAndCleanup (al ret, eax=slot, edx=free)
//   Marks the building destroyed (kind=15), clears its active flag, releases
//   occupant holdings, clears trade routes (notifying a sibling owner if hit),
//   frees the scene child list + character, decrements the production type count,
//   and — when `freeSlot` — frees the Person slot (marker=-1) and the build
//   counter. Idempotent: a slot already at kind 15 skips the destroy phase.
// ---------------------------------------------------------------------------
void Building_RemoveAndCleanup(int slot, bool freeSlot);

// ---------------------------------------------------------------------------
// gilde.exe 0x587908 — VIBE_Building_FindNearestSameType (eax=slot, dl=typeCode)
//   Scans the building/person array for the nearest *other* building of the same
//   raw type whose category matches `typeCode`, excluding production + storage
//   (kind 10) buildings. Returns the matching slot index, or 0 on none.
//   `selfHasObject` mirrors the original's `*(a1+97)` gate (the building must
//   have a live scene object to compare distances).
// ---------------------------------------------------------------------------
int Building_FindNearestSameType(int slot, u8 typeCode);

}  // namespace guild::sim

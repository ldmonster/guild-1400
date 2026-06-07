#include "sim/building_lifecycle.h"

#include <cmath>
#include <cstring>

#include "sim/building.h"             // BuildingTypeDefAt, Building_IsProductionType
#include "sim/building_production.h"  // (type table)

namespace guild::sim {

BuildingPersonRec g_buildingPersons[kBuildingSlots];

BuildingPersonRec* BuildingPersonAt(int slot) {
    if (slot < 0 || slot >= kBuildingSlots)
        return nullptr;
    return &g_buildingPersons[slot];
}

static ILifecycleHooks  g_defaultLifecycleHooks;
static ILifecycleHooks* g_lifecycleHooks = &g_defaultLifecycleHooks;
void SetLifecycleHooks(ILifecycleHooks* hooks) {
    g_lifecycleHooks = hooks ? hooks : &g_defaultLifecycleHooks;
}
ILifecycleHooks* LifecycleHooks() { return g_lifecycleHooks; }

static int g_buildCounter = 0;   // dword_647724
void SetBuildCounter(int v) { g_buildCounter = v; }
int  BuildCounter() { return g_buildCounter; }

void ResetBuildingPersons() {
    std::memset(g_buildingPersons, 0, sizeof(g_buildingPersons));
    // free slots are marker == -1 in the original.
    for (auto& r : g_buildingPersons)
        r.marker = -1;
    g_buildCounter = 0;
}

// The building's raw type code is the low byte of the +0 marker word (the
// original's `*v2` / `*i` reads the byte at +0). Aliased for the type-table read.
static inline u8 RawType(const BuildingPersonRec& r) {
    return static_cast<u8>(r.marker & 0xFF);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5894b0 — VIBE_Building_RemoveAndCleanup
// ---------------------------------------------------------------------------
void Building_RemoveAndCleanup(int slot, bool freeSlot) {
    BuildingPersonRec* v2 = BuildingPersonAt(slot);
    if (!v2)
        return;

    if (v2->kind != 15) {
        // mark inactive + stamp removal time, release holdings, mark destroyed.
        v2->activeFlag = 0;
        v2->removalTs  = 0;   // = qword_13CE852 low word (caller stamps; 0 in cold)
        g_lifecycleHooks->ReleaseOccupantHoldings(slot);
        v2->kind = 15;

        // Clear trade routes pointing at this building; the hook returns the
        // count that hit a sibling owner (drives the rival notification).
        i32 buildingId;
        std::memcpy(&buildingId, reinterpret_cast<u8*>(v2) + 4, sizeof(i32));
        int siblingHits = g_lifecycleHooks->ClearTradeRoutes(buildingId);
        if (siblingHits)
            g_lifecycleHooks->NotifyRivalEvent(-1, slot);
    }

    g_lifecycleHooks->FreeChildList(slot);

    if (v2->charHandle) {
        g_lifecycleHooks->DestroyCharacter(v2->charHandle);
        v2->charHandle = 0;
    }

    if (v2->typeRecord) {
        g_lifecycleHooks->DecrementTypeActiveCount(v2->typeRecord);
        v2->typeRecord = 0;
    }

    if (freeSlot) {
        if (v2->marker != -1)
            --g_buildCounter;
        // free the slot: marker = -1.
        v2->marker = -1;
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x587908 — VIBE_Building_FindNearestSameType
//   for each building i (Person query kind 6):
//       td = typeDef[*i];
//       if !IsProductionType(i) && td.kind != 10 && i has object && i != self
//          && td.kind == typeCode:
//             d = distance(self, i); track nearest.
//   `selfHasObject` is the *(a1+97) gate; the slot must have a live object.
// ---------------------------------------------------------------------------
int Building_FindNearestSameType(int slot, u8 typeCode) {
    BuildingPersonRec* self = BuildingPersonAt(slot);
    if (!self)
        return 0;

    int best = 0;
    float bestDist = 100000.0f;  // *(float*)v17 = 1287568416 (~1.0e5)

    for (int i = 0; i < kBuildingSlots; ++i) {
        if (i == slot)
            continue;
        BuildingPersonRec& r = g_buildingPersons[i];
        if (r.marker == -1 || r.kind == 15)
            continue;

        u8 raw = RawType(r);
        const BuildingTypeDef* td = BuildingTypeDefAt(raw);
        if (!td)
            continue;

        // skip production + storage(10) buildings.
        if (Building_IsProductionKind(td->kind) || td->kind == 10)
            continue;

        if (td->kind != typeCode)
            continue;

        float d = std::sqrt(g_lifecycleHooks->BuildingDistanceSq(slot, i));
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

}  // namespace guild::sim

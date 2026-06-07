#pragma once
// CreateGebaeude (0x586fb8) RULE CORES for the Guild simulation (gilde.exe).
// MODULE: buildings (namespace guild::sim).
//
// The full VIBE_Building_CreateGebaeude is ~0x600 bytes: it allocates a slot
// (FindFreeSlot, already in building_create.cpp), stamps the fixed scalar field
// block (already in building_create.cpp's default backend), copies + de-dups the
// building name from the 8C4788/8C4790 name tables, attaches storage rooms via
// the scene graph (AddObjekt — a render/scene leaf), and finally runs a big
// `switch(prot)` of per-type field initialisations.
//
// The recoverable, deterministic RULE CORES translated here are:
//   * Building_PickUniqueName  — the name-collision de-dup + RNG pick (uses the
//     gilde RNG VIBE_Util_RandNext).  The original scans the 256-slot object
//     array for a name clash and, on conflict, collects all candidate names and
//     picks one with RandNext() % count.  We model the name tables / object-array
//     scan through inputs so the pick rule is exact and testable.
//   * Building_ApplyTypeDefaults — the `switch(prot)` per-type field init.  This
//     is the deterministic data core: it writes the type-specific scalar defaults
//     (worker counts, quality floats, plant-map, time stamps) onto the new record.
//     Scene/memory leaves (plant-map alloc, AddObjekt) route through a hook.
//
// The slot alloc + fixed-field stamp + name copy + storage-room attach stay in
// building_create.cpp / the deferred scene leaf; this file owns the two rules.
#include "guild/common/types.h"
#include "sim/building_production.h"   // PackedTime

namespace guild::sim {

// gilde.exe — the name pick rule from CreateGebaeude.  Given the count of
// distinct candidate names found (those not already present in the object array),
// returns the chosen index in [0, count) using VIBE_Util_RandNext() % count, or
// 0 when count == 0 (the original leaves the primary name in place).  This is the
//   `v32 = RandNext() % v31`  step, isolated for determinism testing.
int Building_PickUniqueName(int candidateCount);

// Memory/scene leaf hook for the per-type init (plant-map alloc).
struct ICreateHooks {
    virtual ~ICreateHooks() = default;
    // gilde.exe VIBE_Memory_AllocDebug(0x600, "f3_gm:PlantMap"): allocate the
    // 0x600-byte plant map for a farm (prot 30).  Returns a buffer the caller
    // initialises, or nullptr.  Default: nullptr (no map; tests install one).
    virtual u8* AllocPlantMap() { return nullptr; }
};
void SetCreateHooks(ICreateHooks* hooks);
ICreateHooks* CreateHooks();
void ResetCreateHooks();

// gilde.exe — the `switch(prot)` tail of CreateGebaeude.  Applies the per-type
// scalar field defaults onto the freshly-stamped record `rec` (>= 169 bytes for a
// building object; the farm/plant case writes a +113 pointer field).  Inputs:
//   rec          : the new record base (already has the fixed-field block).
//   prot         : the building prototype/type byte (v59).
//   ownerWord    : the type/owner word (v58 / a2) — used by the 4..7 path.
//   ownerPersonId: the resolved owner-person id (dword_12CE914[134*ownerWord]),
//                  written to +101 for the 4..7 path; pass -1 when ownerWord ==
//                  0xFFFF (the original's LABEL_89 fall-through).
//   now          : the packed game-time (qword_13CE852) written to +105/+113/+117
//                  for the 4..7 path.
//   isProduction : VIBE_Building_IsProductionType(rec) result (sets +48 = 5000).
// Returns true if the record was modified.
bool Building_ApplyTypeDefaults(u8* rec, u8 prot, u16 ownerWord,
                                i32 ownerPersonId, const PackedTime& now,
                                bool isProduction);

}  // namespace guild::sim

#pragma once
// Building record store + type-table access + the production/security leaf
// accessors for the Guild simulation (gilde.exe). MODULE: buildings
// (namespace guild::sim).
//
// This is the central buildings module. It provides:
//   * the building-type definition table (gilde.exe dword_13CE294, stride 589),
//     modeled as a real array with a settable "loaded" guard,
//   * the building id->record lookup over the shared 169-byte object array,
//   * the type-table-driven kind/security/upgrade accessors,
//   * the command-lockstep HOOK (forward-declared sink) that all state-mutating
//     building operations route through — mocked in tests.
//
// Translated functions:
//   VIBE_Building_FindById          0x587b20
//   VIBE_Building_GetSecurityLevel  0x5902ac
//   VIBE_Building_CheckSecurityThreshold 0x5902f4
//   VIBE_Building_GetUpgradeLevel   0x58fc84
//   VIBE_Building_GetCounterA       0x589768
//   VIBE_Building_GetCounterB       0x589770
//   VIBE_Building_IsStorageType     0x587f50  (record-level wrapper)
//   VIBE_Building_IsProductionType  0x587f80  (record-level wrapper)
//   VIBE_Building_MapTypeToCategory 0x5878b0  (record-level wrapper)
#include "guild/common/types.h"
#include "sim/types.h"
#include "sim/building_types.h"
#include "sim/building_type.h"

namespace guild::sim {

// ===========================================================================
// Building-type definition table (gilde.exe dword_13CE294, stride 589).
// ===========================================================================
// Capacity is the number of distinct building types; the live game sizes this
// per-faction but the type-defs themselves form a fixed catalog. We allocate a
// generous fixed array; the "loaded" flag mirrors the original's null-base bail.
constexpr int kBuildingTypeCapacity = 256;
extern BuildingTypeDef g_buildingTypes[kBuildingTypeCapacity];
extern bool g_buildingTypesLoaded;   // false => accessors treat table as absent

// Type-table accessor: returns &g_buildingTypes[typeIndex] (the original
// "dword_13CE294 + 589*typeIndex"), or nullptr when the table is unloaded.
const BuildingTypeDef* BuildingTypeDefAt(u8 typeIndex);

// Resets the type table + building array to empty (test/setup helper).
void ResetBuildings();

// ===========================================================================
// Building record store (shares sim::g_objects, the 169-byte object array).
// ===========================================================================
// gilde.exe 0x587b20 — VIBE_Building_FindById  (__usercall, eax=(id@eax)).
// Linear scan over the object/building array (sim::g_objects, stride 169, 256):
// skip dead slots (alive byte @+0 == 0) and id mismatches (id dword @+1); returns
// the matching ObjectRec* else nullptr.  This is the id->record chokepoint; the
// buildings module's value/rating math operates on the BuildingRec VIEW the
// caller derives from this record's linked building data (see building_types.h —
// BuildingRec is a separate overlay, NOT the +0/+1 alive/id columns).
ObjectRec* BuildingFindRecordById(i32 id);

// ===========================================================================
// Type-table-driven record accessors.
// ===========================================================================

// gilde.exe 0x5902ac — VIBE_Building_GetSecurityLevel
//   type table +583 for the building's type. 0 if the table is unloaded.
int Building_GetSecurityLevel(const BuildingRec* b);

// gilde.exe 0x5902f4 — VIBE_Building_CheckSecurityThreshold
//   Compares a requested guard level against the building's security level,
//   where the type's security-mode byte (+583, reused as a mode here per the
//   original) selects the threshold: mode 1 -> need>=1, mode 2 -> level<=2,
//   mode 3 -> need>=7, else the passed threshold. `reqLevel` is a2.
bool Building_CheckSecurityThreshold(const BuildingRec* b, int reqLevel);

// gilde.exe 0x58fc84 — VIBE_Building_GetUpgradeLevel
//   upgrade level packed in the high byte of the building's +89 dword.
//   `upgradePacked` is *(int*)(building+89); returns it >> 24 (signed).
int Building_GetUpgradeLevel(i32 upgradePacked);

// gilde.exe 0x589768 / 0x589770 — VIBE_Building_GetCounterA / _GetCounterB
//   Read the global build counters (dword_647724 / dword_64771C).
int Building_GetCounterA();
int Building_GetCounterB();
void SetBuildingCounters(int a, int b);   // seeds the globals for tests

// Record-level kind predicates (resolve the type table, then delegate to the
// pure kind predicates in building_type.h).
//   gilde.exe 0x587f50 / 0x587f80 / 0x5878b0.
bool Building_IsStorageType(const BuildingRec* b);
bool Building_IsProductionType(const BuildingRec* b);
u8   Building_MapTypeToCategory(const BuildingRec* b);

// ===========================================================================
// Command-lockstep hook (mocked in tests).
// ===========================================================================
// State mutations the originals route through the deterministic command channel
// (VIBE_Command_Queue* / VIBE_Building_EnqueueBuyBuilding / _RequestGebaeudeBauen
// / _QueueCommandForAll) are funnelled through this sink so the rules core can be
// exercised without the full network stack. The deferred mutating builders are
// LISTED in building.cpp.
struct IBuildingCommandSink {
    virtual ~IBuildingCommandSink() = default;
    // gilde.exe 0x588798 — VIBE_Building_EnqueueBuyBuilding (op-coded purchase).
    virtual void EnqueueBuyBuilding(i32 buildingId, i32 buyerPersonId) {
        (void)buildingId; (void)buyerPersonId;
    }
    // gilde.exe 0x5942bc — VIBE_Building_RequestGebaeudeBauen (place new building).
    virtual void RequestBuildBuilding(u8 typeCode, i32 plotId) {
        (void)typeCode; (void)plotId;
    }
    // gilde.exe 0x5949b4 — VIBE_Building_QueueCommandForAll (broadcast op).
    virtual void QueueCommandForAll(int opcode, i32 arg) { (void)opcode; (void)arg; }
};
void SetBuildingCommandSink(IBuildingCommandSink* sink);
IBuildingCommandSink* BuildingCommandSink();

// Price-mode global (gilde.exe dword_63C744) used by ComputeItemBaseValue.
void SetBuildingPriceMode(int mode);
int  BuildingPriceMode();

}  // namespace guild::sim

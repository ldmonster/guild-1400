#pragma once
// Building VALUE-AGGREGATION + storage-room model for the Guild simulation
// (gilde.exe). MODULE: buildings (namespace guild::sim).
//
// The four heavy value aggregators iterate the Person/scene-entity arrays and
// call into the He_*/Inventory_* subsystems plus VIBE_GameObject_QueryFind. We
// model the cross-module dependency through IStorageHooks (the scene-item walk
// and the owner-wealth lookup), so the aggregation arithmetic is byte-faithful
// and testable in isolation. The leaf math they rely on
// (ComputeMarketPrice / ComputeItemBaseValue) is reused from
// building_production / building_value.
//
// Translated functions:
//   VIBE_BuildingValue_SumStorageItemWorth   0x591658
//   VIBE_BuildingValue_ComputeStockValue     0x590360
//   VIBE_BuildingValue_ComputeRoomWorth      0x59116c
//   VIBE_BuildingValue_ComputeProductionWorth 0x58fe68 (storage/sale portion)
#include <vector>

#include "guild/common/types.h"
#include "sim/building_types.h"
#include "sim/types.h"

namespace guild::sim {

// A stored-item record returned by the scene-item walk: a prot id (used as a
// market-price key) and a raw quantity. Mirrors the (*v3, *(v3+18), *(v3+7))
// triple the originals read off the scene node.
struct StorageItem {
    i16 prot;      // *v3        market-price key
    u8  qtyByte;   // *(v3 + 18) per-item quantity byte (market-price arg)
    i32 count;     // *(v3 + 7)  stock count (ComputeStockValue / reserve goods)
};

struct IStorageHooks {
    virtual ~IStorageHooks() = default;

    // gilde.exe VIBE_GameObject_QueryFind(...) item walk for the building's main
    // storage room. Returns the list of stored items (prot/qtyByte/count).
    virtual std::vector<StorageItem> StorageItems(const BuildingRec* b) {
        (void)b; return {};
    }
    // gilde.exe per-room item walk for a specific room prot (ComputeRoomWorth).
    virtual std::vector<StorageItem> RoomItems(const BuildingRec* b, i16 roomProt) {
        (void)b; (void)roomProt; return {};
    }
    // gilde.exe VIBE_Person_ComputeTotalWealth(owner) — owner's liquid wealth,
    // used by ComputeStockValue's owner-share term. 0 if no owner.
    virtual i32 OwnerWealth(i32 ownerId) { (void)ownerId; return 0; }
};
void SetStorageHooks(IStorageHooks* hooks);
IStorageHooks* StorageHooks();

// gilde.exe 0x591658 — VIBE_BuildingValue_SumStorageItemWorth (eax=building@a1).
// Sum of market price over every item in the building's main storage room.
int BuildingValue_SumStorageItemWorth(const BuildingRec* b);

// Result frame for ComputeStockValue (the original writes two dwords through a2).
struct StockValue {
    i32 ownerShare;  // a2[0]  owner-wealth share, capped + value-mult
    i32 stockWorth;  // a2[1]  market value of the goods in storage
};

// gilde.exe 0x590360 — VIBE_BuildingValue_ComputeStockValue (eax=building, edx=out).
// ownerShare = clamp(ownerWealth, 2560000) * (kind==2 ? 0.09 : 0.04), floored 0.
// stockWorth = sum over storage items of marketPrice * max(1, count*0.3).
StockValue BuildingValue_ComputeStockValue(const BuildingRec* b, u16 ownerId,
                                           u8 buildingKind);

// gilde.exe 0x59116c — VIBE_BuildingValue_ComputeRoomWorth (eax=building, edx=mul).
// Walks the type's room list, summing item market price for storage rooms
// (kind 2/6), then scales by (mul * 0.01 * roomWorthMul-derived base).
// `roomWorthMul` = typeDef.roomWorthMul (drives the 3840*mul base).
int BuildingValue_ComputeRoomWorth(const BuildingRec* b, int mul);

}  // namespace guild::sim

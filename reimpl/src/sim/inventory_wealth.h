#pragma once
// ===========================================================================
// inventory_wealth.{h,cpp} — currency + total-wealth AGGREGATION (gilde.exe)
// ===========================================================================
// MODULE: the wealth side of the Inventory cluster (namespace guild::sim).
//
// These three functions aggregate a person's liquid currency and total net
// worth by scanning the scene-entity tree. The currency PROTOTYPE for a given
// player is read from the goods table: dword_13CD6F2[189 * player] >> 16 — i.e.
// each player/faction has its own currency good id (189-byte goods records). The
// table is runtime-seeded (zero in the static image), so the goods-id resolver
// is injected, exactly as world/economy injects its rate table.
//
//   SumCurrencyHeld   : sum the +14 amount of every currency child under the
//                       person's container (person+376), for the ACTIVE player's
//                       currency proto (byte_6477A1 selects the player row).
//   GetCurrencyAmount : the +14 amount of the single currency child of a given
//                       player's currency proto (or 0 if none).
//   ComputeTotalWealth: liquid currency + sum over the person's owned buildings
//                       of ComputeRoomWorth + SumStorageItemWorth.
//
// The currency child walk reuses the StockChild/ContainerView model from
// inventory_capacity.h; the per-building worth terms reuse building_storage's
// BuildingValue_ComputeRoomWorth / SumStorageItemWorth (forward-declared so this
// module stays decoupled — a test provides the worth via a hook).
//
// Translated functions:
//   VIBE_Person_SumCurrencyHeld     0x59152c
//   VIBE_Person_GetCurrencyAmount   0x5915b8
//   VIBE_Person_ComputeTotalWealth  0x591f7c
#include <vector>

#include "guild/common/types.h"
#include "sim/inventory_capacity.h"
#include "sim/types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Goods-table currency resolver. The original computes the per-player currency
// prototype as dword_13CD6F2[189*player] >> 16. We inject the table (one i32 per
// player record's high-word slot) so the resolution is byte-faithful but the
// runtime data is supplied by the host/test.
// ---------------------------------------------------------------------------
void WealthSetCurrencyProtoTable(const std::vector<i16>& protoByPlayer);
// gilde.exe (dword_13CD6F2[189*player] >> 16) — currency proto for `player`.
i16 WealthCurrencyProto(u8 player);

// The currently active player (gilde.exe byte_6477A1). SumCurrencyHeld keys off
// it. Default 0; the host/test sets it.
void WealthSetActivePlayer(u8 player);
u8 WealthActivePlayer();

// gilde.exe 0x5915b8 — VIBE_Person_GetCurrencyAmount (eax=person, dl=player).
//   v2 = QueryFind(*(person+376), 1, 0, currencyProto(player));
//   if (v2) return *(v2+14);  return <uninit>;   // (missing -> 0)
// `container` holds the person's currency stacks (person+376 child list).
int PersonGetCurrencyAmount(const ContainerView& container, u8 player);

// gilde.exe 0x59152c — VIBE_Person_SumCurrencyHeld (eax=person).
//   for (i = QueryFind(*(person+376), 1, 0, currencyProto(activePlayer)); i;
//        i = IterNext()) sum += *(i+14);
//   return sum;
// Sums the +14 amount of every child stack whose type == the active player's
// currency proto. (The original sums ALL matching siblings the iterator yields.)
int PersonSumCurrencyHeld(const ContainerView& container);

// ---------------------------------------------------------------------------
// One owned building's contribution to ComputeTotalWealth: the two worth terms
// the inner loop accumulates (ComputeRoomWorth(building, building.kind>>24) +
// SumStorageItemWorth(building)). Supplied per building so the module need not
// reach into building_storage's record model.
// ---------------------------------------------------------------------------
struct OwnedBuildingWorth {
    i32 roomWorth = 0;     // VIBE_BuildingValue_ComputeRoomWorth(building, mul)
    i32 storageWorth = 0;  // VIBE_BuildingValue_SumStorageItemWorth(building)
};

// gilde.exe 0x591f7c — VIBE_Person_ComputeTotalWealth (ax=personIndex, esi=a2).
//   if (personIndex >= 0x300) return -1;            // 768 cap
//   if (person.marker == -1)  return -1;            // free slot
//   v4 = SumCurrencyHeld(person);
//   for (each owned building b) v4 += roomWorth(b) + storageWorth(b);
//   return v4;
// `freeSlot` mirrors the marker==-1 guard; `ownedBuildings` is the QueryBegin/
// IterNext owned-building list joined with its two worth terms.
int PersonComputeTotalWealth(int personIndex, bool freeSlot,
                             const ContainerView& currency,
                             const std::vector<OwnedBuildingWorth>& ownedBuildings);

}  // namespace guild::sim

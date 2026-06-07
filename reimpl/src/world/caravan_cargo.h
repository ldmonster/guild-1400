#pragma once
// Caravan cargo slot-table model — the transport-panel cargo grids and the cargo
// valuation / load math that walk them (gilde.exe). This is the world-side
// caravan/trade-transport cargo core, distinct from the GUI layout in
// gui/trade_panel.cpp (which owns the on-screen grid coordinates) and from the
// already-translated rules core in world/tradetransport.cpp.
//
// Translated functions:
//   VIBE_TradePanel_InitSlotTables   0x50854c  (cargo-grid portion only)
//   VIBE_TradeTransport_ComputeCargoValue 0x53ff3c  (faithful two-array walk)
//   VIBE_TradeTransport_LoadFromStorage   0x53f6bc  (per-slot load math, core)
//
// The originals address the two cargo grids as a family of PARALLEL arrays
// indexed by a dword stride of 14 (k -> 14*k), one set for the 8-slot "sell"
// grid (dword_122E764.. base) and one for the 16-slot "buy" grid (dword_122E064..
// base). We model each grid as a small struct-of-arrays so the math is faithful
// and testable without the live object/command tables.
//
//   sell grid (8):  dword_122E78C[i]  good-id packed dword (good == bytes@+44 hi-word)
//                   dword_122E7A0[i]  object id (-1 == empty slot)
//                   dword_122E7C0[i]  storage slot index (>=0 commits the move)
//   buy  grid (16): dword_122E08C[j]  good-id packed dword
//                   dword_122E0A0[j]  object id
//                   dword_122E0C0[j]  storage slot index
//
// where i,j step by 14 (so slot k lives at array index 14*k).
#include <vector>

#include "guild/common/types.h"

namespace guild::world {

// One cargo grid slot. `goodIdPacked` is the dword the original reads as
//   HIWORD(*(unsigned int *)((char *)&array[idx] + 2))
// i.e. the good id is bits 16..31 of the dword stored 2 bytes into the slot's
// packed dword (recovered as `goodIdPacked >> 16`). `objectId == -1` marks an
// empty slot (skipped). `storageSlot < 0` means the slot has no committed
// destination (load skips it). `dataPtr` is the live object data pointer the
// original obtains via VIBE_Object_GetDataPtr(objectId); here it doubles as the
// on-hand quantity the loader moves (the original treats the data-ptr value as
// the available count, e.g. `*(int*)(v6+7)` style reads collapse to a stock int).
struct CaravanSlot {
    i32 goodIdPacked = 0;   // packed dword; goodId == goodIdPacked >> 16
    i32 objectId     = -1;  // -1 == empty
    i32 storageSlot  = -1;  // <0 == no destination
    i32 dataPtr      = 0;   // VIBE_Object_GetDataPtr result (0 == no object data)

    i32 goodId() const { return goodIdPacked >> 16; }
};

// The two cargo grids the transport panel maintains. The sell grid holds 8 slots,
// the buy grid 16; the originals iterate them in that order.
struct CaravanCargoTables {
    std::vector<CaravanSlot> sell;  // dword_122E764-family (i: 0..112 step 14 -> 8)
    std::vector<CaravanSlot> buy;   // dword_122E064-family (j: 0..224 step 14 -> 16)
};

// Recovered slot counts (the loop bounds 112/14 == 8 and 224/14 == 16).
constexpr int kCaravanSellSlots = 8;
constexpr int kCaravanBuySlots  = 16;

// gilde.exe 0x50854c — VIBE_TradePanel_InitSlotTables (cargo-grid portion).
// Resets both cargo grids to empty: good-id packed dword 0, object id -1, storage
// slot -1, data ptr 0. (The original also lays out screen coords and several
// auxiliary tables owned by gui/trade_panel.cpp; only the cargo-grid reset that
// the valuation/load functions depend on is reproduced here.)
void CaravanInitSlotTables(CaravanCargoTables& t);

// Price factor when the owning building is a market (ownerKind == 10):
//   dbl_623F30 == dbl_623F28 == 1.1 (0x3FF199999999999A).
constexpr double kCaravanMarketPriceFactor = 1.1;

// Default price-context byte when the source object kind is not 71:
//   byte_6477A1 == 0 in the shipped data.
constexpr u8 kCaravanDefaultPriceContext = 0;

// Market-price lookup hook (gilde.exe VIBE_Building_LookupCachedMarketPrice
// 0x58f6b8): price = lookup(goodId, contextByte). Tests inject a deterministic
// table; the real wiring forwards to world/market_price.cpp.
using CaravanPriceHook = double (*)(i32 goodId, u8 context);
void CaravanSetPriceHook(CaravanPriceHook hook);
double CaravanLookupPrice(i32 goodId, u8 context);

// gilde.exe 0x53ff3c — VIBE_TradeTransport_ComputeCargoValue (faithful).
//   v13 = 0
//   ctx = (sourceKind == 71) ? sourceContext101 : byte_6477A1
//   for each sell slot i (8), then each buy slot j (16):
//     if objectId != -1 and dataPtr(objectId) != 0:
//       unit = (ownerKind==10) ? price(good,ctx)*1.1 : price(good,ctx)*priceMul
//       if (mode==2 || mode==4) unit = price(good, sellContext101)
//       v13 += dataPtr * unit
//   result stored as -v13 (a cost); we return +v13.
// Parameters recovered from the call frame:
//   `ownerIsMarket` <- (*v4 == 10) on the owner record
//   `priceMul`      <- *(float *)(a1 + 73)
//   `sourceKind71`  <- (*a1 == 71) selects the priceCtx source
//   `sourceCtx101`  <- a1[101] (the context byte / sell context)
//   `mode`          <- a3 (2 or 4 == "sell at remote contor")
double CaravanComputeCargoValue(const CaravanCargoTables& t, bool ownerIsMarket,
                                float priceMul, bool sourceKind71, u8 sourceCtx101,
                                int mode);

// One resolved load line produced by the load core (per non-skipped slot).
struct CaravanLoadLine {
    i32 goodId   = 0;   // HIWORD(packed) of the slot
    i32 quantity = 0;   // free space / carry capacity moved (v58 / v57)
    i32 toSlot   = 0;   // destination storage slot (storageSlot)
    double unitPrice = 0.0; // resolved per-unit price (v59 / v56)
};

// gilde.exe 0x53f6bc — VIBE_TradeTransport_LoadFromStorage (deterministic core).
// For each cargo slot (sell grid then buy grid) the original computes the amount
// to move and the per-unit price, then emits a per-good command. We reproduce the
// deterministic decision/quantity/price math and return the resolved lines; the
// command queue, audio, and HUD side effects are out of scope (GUI-coupled).
//
//   free = priceContext ? ComputeCarryCapacity(carrier, good, data)   // a5 path
//                       : ComputeFreeSpaceForItem(dest, good, data)   // v46 path
//   skip slot if free <= 0, or storageSlot < 0, or the resource check fails.
//   if applyPricing (v51): unit = (ownerKind==10) ? price*1.1 : price*priceMul
//                          if (mode&... 2/4) unit = price(good, sellCtx)
//                   else: unit = 0
// `freeSpace[k]` supplies the capacity result the originals get from the inventory
// leaves (already translated in sim/inventory_capacity.cpp); injecting it keeps
// this module free of the live object tables while preserving the exact branch
// structure.
std::vector<CaravanLoadLine> CaravanLoadFromStorage(
    const CaravanCargoTables& t, const std::vector<i32>& sellFree,
    const std::vector<i32>& buyFree, bool applyPricing, bool ownerIsMarket,
    float priceMul, u8 priceCtx, u8 sellCtx, int mode);

} // namespace guild::world

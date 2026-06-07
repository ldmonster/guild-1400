#pragma once
// Building STOCK / VALUE / CUSTOMER-distribution rule cores for the Guild
// simulation (gilde.exe). MODULE: buildings (namespace guild::sim).
//
// This is the remaining deferred set of the building economy: the stock
// projection/efficiency math, the flagged-slot worth + sale price, the
// workstation aggregation scans, the customer goods-distribution pass, and the
// big production-worth aggregator.  The originals are __usercall and reach the
// records through raw byte offsets off two distinct record families:
//
//   (A) the LIVE STOCK record (the runtime production object): the stock fields
//       at +10 (word, current stock), +20/+28/+32 (float: rate/cap/limit) and
//       +36 (int, projected delta).  See BuildingStockRec below.
//
//   (B) the BUILDINGS-AS-PERSONS record (536-byte Person slot, word_12CE910):
//       buildings also live in the Person array.  The customer/supplier/worth
//       passes read +2 (kind byte), +4 (id), +8/+9 (active/state bytes), +104..+120
//       (customer-id dwords), +61 (quality scalar), +77/+85/+101/+105/+109
//       (worth columns), +92 (associated-building/customer id), +124 (price
//       float), +368 (owner-record ptr).  See BuildingSaleRec below.
//
// State mutations (the VIBE_Command_Queue* / delta-packet tails) and the
// cross-module scene/handler walks route through IStockHooks so the arithmetic
// is byte-faithful and testable in isolation.  Float constants are recovered
// byte-for-byte (see building_stock.cpp).
//
// Translated functions:
//   VIBE_Building_ComputeProjectedStock      0x57d1c8  (pure)
//   VIBE_Building_ComputeEfficiencyScore     0x57d3d0  (pure; reuses Max/Cur out)
//   VIBE_Building_SyncStockLevel             0x57d0f8  (stock math + cmd hook)
//   VIBE_Building_AdjustStockAndNotify       0x57d5b4  (stock math + cmd hook)
//   VIBE_Building_FindMatchingSupplier       0x57dcb4  (pure Person scan)
//   VIBE_Building_SumFlaggedSlotsWorth       0x5913e0  (reuses ComputeMarketPrice)
//   VIBE_Building_ComputeSalePrice           0x591480  (reuses SumFlagged + law)
//   VIBE_Building_SumWorkstationCount        0x5905dc  (pure type-table scan)
//   VIBE_Building_SumWorkstationByCategory   0x5904fc  (type scan + scene hook)
//   VIBE_Building_DistributeGoodsToCustomers 0x57d83c  (rule core + cmd hook)
//   VIBE_BuildingValue_ComputeProductionWorth 0x58fe68 (Person walk + hooks)
#include "guild/common/types.h"
#include "sim/building_types.h"
#include "sim/types.h"

namespace guild::sim {

// ===========================================================================
// (A) Live STOCK record view — the runtime production object the stock math
// addresses through a `float*`/`char*` base.  Field offsets recovered from
// ComputeProjectedStock (0x57d1c8) / SyncStockLevel (0x57d0f8) /
// AdjustStockAndNotify (0x57d5b4).
// ===========================================================================
GUILD_PACKED_BEGIN
struct BuildingStockRec {
    u16 marker;       // +0x00 alive marker (0xFFFF == empty; AdjustStock gate)
    u8  kind;         // +0x02 kind byte (== 10 storage -> AdjustStock returns -1)
    u8  pad3[5];      // +0x03..+0x07
    u8  active;       // +0x08 active flag (0 -> AdjustStock returns -1)
    u8  pad9;         // +0x09
    u16 stock;        // +0x0A (+10) current stock / fill (word)
    u8  pad12[4];     // +0x0C..+0x0F
    float decayRate;  // +0x10 (+16 / float[4]) per-step decay rate
    float zeroFill;   // +0x14 (+20 / float[5]) output at zero fill
    u8  pad24[4];     // +0x18..+0x1B
    float fullFill;   // +0x1C (+28 / float[7]) output at full fill
    float fillCap;    // +0x20 (+32 / float[8]) fill capacity
    i32  delta;       // +0x24 (+36 / int[9])   projected/added stock delta
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(offsetof(BuildingStockRec, stock)    == 10, "stock @+10");
static_assert(offsetof(BuildingStockRec, zeroFill) == 20, "zeroFill @+20");
static_assert(offsetof(BuildingStockRec, fullFill) == 28, "fullFill @+28");
static_assert(offsetof(BuildingStockRec, fillCap)  == 32, "fillCap @+32");
static_assert(offsetof(BuildingStockRec, delta)    == 36, "delta @+36");

// ===========================================================================
// (B) Buildings-as-persons record view — the 536-byte Person slot a building
// occupies.  Field offsets recovered from FindMatchingSupplier (0x57dcb4),
// DistributeGoodsToCustomers (0x57d83c) and ComputeProductionWorth (0x58fe68).
// Only the worth/customer columns are named; the rest is padding so sizeof
// matches the 536-byte Person stride.
// ===========================================================================
GUILD_PACKED_BEGIN
struct BuildingSaleRec {
    i16 marker;          // +0x00 alive marker (-1 == free; worth walk skips)
    u8  kind;            // +0x02 kind byte (6/7 = sale/auction; 5,15 customer)
    u8  pad3;            // +0x03
    i32 id;              // +0x04 building/person id
    u8  active;          // +0x08 active byte (gate)
    u8  state;           // +0x09 state byte (== 1 to distribute)
    u8  pad10[27];       // +0x0A..+0x24
    u16 ownerPlayer;     // +0x25 (+37) faction/owner-A word
    u16 ownerB;          // +0x27 (+39) owner/player word
    u8  pad41[20];       // +0x29..+0x3C
    i32 quality;         // +0x3D (+61) quality / value scalar
    u8  pad65[12];       // +0x41..+0x4C
    i32 worth77;         // +0x4D (+77) misc worth column (a2[18])
    u8  pad81[4];        // +0x51..+0x54
    i32 worth85;         // +0x55 (+85) sale worth column (kind 5 -> a2[10])
    u8  pad89[3];        // +0x59..+0x5B
    i32 sceneNodeId;     // +0x5C (+92) associated scene node / customer id
    u8  pad96[5];        // +0x60..+0x64
    // NB: the customer-id slot scan (FindMatchingSupplier / Distribute) reads the
    // dwords at +104,+108,+112,+116,+120 (the originals: base+12 then +92 offset).
    // A SALE/AUCTION building (kind 6/7) overlays those bytes as customer ids; a
    // PRODUCTION building (kind 7/22) overlays them as the worth columns below.
    // Same bytes, role-dependent — use BPCustomerId() for the customer view.
    i32 worth101;        // +0x65 (+101) worth column (kind 7 -> a2[13])
    i32 worth105;        // +0x69 (+105) worth column (kind 7 -> a2[14]) / cust slot
    i32 worth109;        // +0x6D (+109) worth column (kind 22 -> a2[19]) / cust slot
    u8  pad113[11];      // +0x71..+0x7B
    float price;         // +0x7C (+124) current price float
    u8  pad128[240];     // +0x80..+0x16F
    i32 ownerRecPtr;     // +0x170 (+368) owner-record ptr (worth distribute)
    u8  pad372[86];      // +0x174..+0x1C9
    u8  flag458;         // +0x214 (+458) distribute gate flag (bit 0x04)
    u8  pad459[77];      // +0x215..+0x217
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(offsetof(BuildingSaleRec, ownerPlayer) == 37,  "ownerPlayer @+37");
static_assert(offsetof(BuildingSaleRec, quality)     == 61,  "quality @+61");
static_assert(offsetof(BuildingSaleRec, sceneNodeId) == 92,  "sceneNodeId @+92");
static_assert(offsetof(BuildingSaleRec, price)       == 124, "price @+124");
static_assert(offsetof(BuildingSaleRec, ownerRecPtr) == 368, "ownerRecPtr @+368");
static_assert(offsetof(BuildingSaleRec, flag458)     == 458, "flag458 @+458");
static_assert(sizeof(BuildingSaleRec) == kPersonStride,
              "BuildingSaleRec stride must be 536");

// The customer-id slot scan accessor: dword at +104 + 4*k (k in 0..4).  Mirrors
// the originals' `*(DWORD*)((base+12) + 92 + 4*k)` customer column.
i32  BPCustomerId(const BuildingSaleRec* b, int k);
void BPSetCustomerId(BuildingSaleRec* b, int k, i32 id);

// ===========================================================================
// Cross-module hooks (command mutations, scene/handler walks, law records).
// All default inert; mocked in tests.
// ===========================================================================
struct IStockHooks {
    virtual ~IStockHooks() = default;

    // gilde.exe VIBE_Building_SumWorkstationByCategory's per-room scene check:
    // VIBE_GameObject_QueryFind(building.sceneNodeId, 2,6,0, roomProt) != 0.
    virtual bool RoomPresent(const BuildingSaleRec* b, i16 roomProt) {
        (void)b; (void)roomProt; return false;
    }
    // gilde.exe VIBE_Gesetz_GetRecord(6,...) — returns the tax-tier index (a2[+0],
    // the `4 - v7` term in ComputeSalePrice). Default 4 (== no tax adjustment).
    virtual int SaleTaxTier() { return 4; }

    // ----- DistributeGoodsToCustomers command/Amt tail -----
    // gilde.exe VIBE_Command_QueueRequestArgs26(buildingId, field, floatBits):
    // queue a delta on a single building field (124 = price, 28 = stock).  Returns
    // a packet handle; the originals poll it.  Default: pretend it succeeded.
    virtual int QueuePriceUpdate(i32 buildingId, int field, float value) {
        (void)buildingId; (void)field; (void)value; return 1;
    }
    // gilde.exe VIBE_Command_EnqueueObjectInteraction(8, srcId,0, dstId, prot,0,0,
    //   count): post a buy/sell interaction between two buildings. Returns a
    // packet status (2 == failed/applied-elsewhere). Default 0 (== succeeded).
    virtual int EnqueueGoodsTransfer(i32 srcId, i32 dstId, i32 prot, int count) {
        (void)srcId; (void)dstId; (void)prot; (void)count; return 0;
    }
    // gilde.exe VIBE_Command_QueueRequest39 (+ delta packet): finalise the
    // customer's record-7 update once the transfer succeeds. Fire-and-forget.
    virtual void FinaliseCustomerUpdate(i32 customerId) { (void)customerId; }

    // gilde.exe owner-record resolve for ComputeProductionWorth: the worth pass
    // resolves the owner via VIBE_Person_FindRecordById(building.sceneNodeId);
    // returns the resolved record's +368 owner-rec-ptr (0 if none).  We expose the
    // resolution as a hook (the Person array is owned by the entity module).
    virtual const BuildingSaleRec* ResolveOwnerRecord(i32 personId) {
        (void)personId; return nullptr;
    }
};
void SetStockHooks(IStockHooks* hooks);
IStockHooks* StockHooks();

// ===========================================================================
// Translated functions.
// ===========================================================================

// gilde.exe 0x57d1c8 — VIBE_Building_ComputeProjectedStock (eax=stockRec).
// Iterates the decay recurrence  s = s - rate + rate/s  from fullFill while
// s > 1.0, counting steps, then adds the current stock and the clamped
// (cap - stock) headroom; truncates toward zero.
int Building_ComputeProjectedStock(const BuildingStockRec* b);

// gilde.exe 0x57d3d0 — VIBE_Building_ComputeEfficiencyScore (st0 ret, ax=index).
// 0 for an inactive building (active <= 1); else
//   maxOut*0.001*0.2 + (curOut/maxOut)*0.8.
// Reuses Building_ComputeMaxOutput / _ComputeCurrentOutput (building_value).
float Building_ComputeEfficiencyScore(const BuildingRec* b);

// gilde.exe 0x57d0f8 — VIBE_Building_SyncStockLevel (eax=stockRec, edx=newStock,
//   ecx=scratch).  Projects the stock forward by (newStock - cap) decay steps
// (same recurrence as above) and queues the resulting delta on field 28.
// Returns the projected level.  The command tail routes through IStockHooks.
float Building_SyncStockLevel(BuildingStockRec* b, u16 newStock);

// gilde.exe 0x57d5b4 — VIBE_Building_AdjustStockAndNotify (st0 ret, ax=index,
//   edx=addAmount).  Recomputes the current output (zeroFill..fullFill lerp +
//   delta) plus addAmount; if > 0 queues the field-36 delta, else queues a
// slot-reset.  Returns the new level (or -1 for an empty/storage/inactive rec).
float Building_AdjustStockAndNotify(const BuildingStockRec* b, int addAmount);

// gilde.exe 0x57dcb4 — VIBE_Building_FindMatchingSupplier (eax=building).
// Scans the buildings-as-persons array for a sale/auction building (kind 6/7)
// that supplies this building's id.  Returns the matching record (raw ptr) or
// nullptr; 1 (truthy non-record) when nothing matches the original's fall-through.
// `persons` is the 768-slot array base; `self` is the querying building record.
const BuildingSaleRec* Building_FindMatchingSupplier(
    const BuildingSaleRec* persons, const BuildingSaleRec* self);

// gilde.exe 0x5913e0 — VIBE_Building_SumFlaggedSlotsWorth (eax=typeIndex).
// Walks the type's room list; for each flagged room (room+1 sign-byte < 0) adds
// ComputeMarketPrice(roomKind, 100).  Base term is 3840 * typeDef.roomWorthMul.
int Building_SumFlaggedSlotsWorth(u8 typeIndex);

// gilde.exe 0x591480 — VIBE_Building_ComputeSalePrice (fastcall: a1=index unused
//   here, a2=typeIndex).  worth = SumFlaggedSlotsWorth(typeIndex); price =
//   ((4 - taxTier)*0.1 + 0.8) * worth * (qualityIs14 ? 0.85 : 1.0).
// `qualityCode` is the building's +358 byte (14 => the 0.85 discount).
int Building_ComputeSalePrice(u8 typeIndex, u8 qualityCode);

// gilde.exe 0x5905dc — VIBE_Building_SumWorkstationCount (al=typeIndex, dl=cat).
// Sums the per-room worker count (+483) over rooms whose category (+419) matches
// `cat`.  Pure type-table scan (64 rooms).
int Building_SumWorkstationCount(u8 typeIndex, u8 category);

// gilde.exe 0x5904fc — VIBE_Building_SumWorkstationByCategory (eax=building,
//   dl=cat, ebx=scaleFlag).  Like the count but gated on the room being present
// in the scene (IStockHooks::RoomPresent); when scaleFlag set, scales by
//   building.quality92 * 0.01 * sum + 0.5 (truncated).  `quality92` is +92.
int Building_SumWorkstationByCategory(const BuildingSaleRec* b, u8 typeIndex,
                                      u8 category, bool scale);

// gilde.exe 0x57d83c — VIBE_Building_DistributeGoodsToCustomers (eax=building).
// The per-tick customer goods distribution.  Gates on kind/active/state/flag458,
// then either (price drift > 1.0 path) posts a price update + a goods-transfer
// interaction to a chosen customer, or (fallback) counts the building's real
// (non kind 5/6/7/15) customers and nudges the price up by a per-count factor
// from flt_641FEC.  Mutations route through IStockHooks.  Returns the record.
const BuildingSaleRec* Building_DistributeGoodsToCustomers(BuildingSaleRec* b);

// Result frame for ComputeProductionWorth (the original fills a 21-dword frame).
struct ProductionWorth {
    i32 col[21];   // a2[0..20]; the totals land in [16]/[17]/[20] and per-kind
                   // sub-totals in [3],[10],[12]..[15],[18],[19].
    bool nonZero;  // == the original's "return 1 if any worth" boolean.
};

// gilde.exe 0x58fe68 — VIBE_BuildingValue_ComputeProductionWorth (eax=building,
//   ebx=outFrame).  Walks the buildings-as-persons array summing per-worker
// item base value (ComputeItemBaseValue * quality * 0.01) into the production
// total, then adds per-kind worth columns (5/7/22).  `selfTypeIndex` is the
// building's +0 type byte (used for ComputeItemBaseValue); `workerProfOut[6]` /
// `workerProfIn[2]` are the matching per-slot profession codes the original
// reads off the type table and the worker records (passed in so the worth math
// stays leaf-pure).  Returns the filled frame.
ProductionWorth BuildingValue_ComputeProductionWorth(const BuildingSaleRec* b,
                                                      u8 selfTypeIndex);

void ResetStockHooks();   // test helper

}  // namespace guild::sim

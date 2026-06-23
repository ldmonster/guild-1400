#include "sim/building_stock.h"

#include "sim/building.h"           // BuildingTypeDefAt, Building_Is*Type
#include "sim/building_value.h"     // ComputeMaxOutput / ComputeCurrentOutput
#include "sim/building_production.h"// Building_ComputeMarketPrice

#include "crt/rand.h"               // RandNext (gilde RNG)
#include "util/math_random.h"       // RandomModulo (VIBE_Math_RandomModulo)
#include "util/math_rng_float.h"    // RandomFloatScaled (VIBE_Math_RandomFloatScaled)

#include <cstring>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Recovered float constants (byte-for-byte; decoded with struct.unpack).
//   flt_62594C = 0.0010000000474974513   (efficiency max-out weight base)
//   dbl_625954 = 0.2                      (efficiency max-out weight)
//   dbl_62595C = 0.8                      (efficiency ratio weight)
//   flt_625988 = -0.5                     (distribute price-drift scale)
//   flt_626A14 = 0.10000000149011612      (sale-price tax slope)
//   flt_626A18 = 0.800000011920929        (sale-price tax base)
//   dbl_6269CC = 0.01                     (quality scale; *(int)(rec+61)*0.01)
//   flt_641FEC[6] = {0.2400154, 0.2222000, 0.125, 0.05555000, 0.01385000, 0}
//                                         (per-customer-count price nudge)
// ---------------------------------------------------------------------------
static constexpr float  kEffMaxBase  = 0.0010000000474974513f; // flt_62594C
static constexpr double kEffMaxW     = 0.2;                     // dbl_625954
static constexpr double kEffRatioW   = 0.8;                     // dbl_62595C
static constexpr float  kDistDrift   = -0.5f;                   // flt_625988
static constexpr float  kSaleTaxSlope= 0.10000000149011612f;    // flt_626A14
static constexpr float  kSaleTaxBase = 0.800000011920929f;      // flt_626A18
static constexpr double kQualityScale= 0.01;                    // dbl_6269CC
static const float kCustNudge[6] = {                            // flt_641FEC
    0.2400154024362564f, 0.22220000624656677f, 0.125f,
    0.05555000156164169f, 0.013849999755620956f, 0.0f
};

// trunc-toward-zero (every "v=x; VIBE_Coord_ConvertX(); (int)v" rounds to zero).
static inline int truncToZero(double x) {
    return static_cast<int>(static_cast<long long>(x));
}

// customer-id slot accessor (dword at +104 + 4*k; the originals' base+12 then +92).
i32 BPCustomerId(const BuildingSaleRec* b, int k) {
    i32 v;
    std::memcpy(&v, reinterpret_cast<const u8*>(b) + 104 + 4 * k, 4);
    return v;
}
void BPSetCustomerId(BuildingSaleRec* b, int k, i32 id) {
    std::memcpy(reinterpret_cast<u8*>(b) + 104 + 4 * k, &id, 4);
}

// Cross-module hooks (default inert).
static IStockHooks  g_defaultStockHooks;
static IStockHooks* g_stockHooks = &g_defaultStockHooks;
void SetStockHooks(IStockHooks* hooks) {
    g_stockHooks = hooks ? hooks : &g_defaultStockHooks;
}
IStockHooks* StockHooks() { return g_stockHooks; }
void ResetStockHooks() { g_stockHooks = &g_defaultStockHooks; }

// ---------------------------------------------------------------------------
// Shared stock-decay recurrence used by ProjectedStock / SyncStockLevel /
// AdjustStock.  The original integrates  s = s - rate + rate/s  while s > 1.0.
// ---------------------------------------------------------------------------

// gilde.exe 0x57d1c8 — VIBE_Building_ComputeProjectedStock
int Building_ComputeProjectedStock(const BuildingStockRec* b) {
    // v1 counts decay steps from fullFill (+28) using rate at +20, while i > 1.0
    // (SLODWORD compare against 0x3F800000 == 1.0f, i.e. i > 1.0f).
    int steps = 0;                                   // v1
    for (float i = b->fullFill; i > 1.0f; ++steps)   // *(float*)(a1+28)
        i = i - b->zeroFill + b->zeroFill / i;       // -+ rate at +20

    float v4 = static_cast<float>(b->stock);         // *(WORD*)(a1+10)
    float v6 = b->fillCap - v4;                       // *(float*)(a1+32) - v4
    float v5 = (v6 <= 0.0f) ? 0.0f : (b->fillCap - v4);
    double v2 = static_cast<double>(steps + b->stock) + v5;
    return truncToZero(v2);                           // VIBE_Coord_ConvertX
}

// Projects a stock value forward by (stock - fillCap) decay steps from fullFill,
// mirroring SyncStockLevel's else-branch.  Returns the projected output level.
static float ProjectForward(const BuildingStockRec* b, double startStock) {
    if (startStock <= static_cast<double>(b->fillCap))
        return b->fullFill;                          // result[6]
    int n = static_cast<int>(startStock - b->fillCap); // (int)v5
    float v12 = b->fullFill;                          // result[6]
    if (n > 0) {
        float v7 = v12;
        for (int v6 = 0; v6 < n; ++v6)
            v7 = v7 - b->zeroFill + b->zeroFill / v7; // result[5] == zeroFill (+20)
        v12 = v7;
    }
    return v12;
}

// gilde.exe 0x57d0f8 — VIBE_Building_SyncStockLevel
float Building_SyncStockLevel(BuildingStockRec* b, u16 newStock) {
    if (!b)
        return 0.0f;
    float level = ProjectForward(b, static_cast<double>(newStock));
    // delta vs current full-fill output (result[7] == fullFill +28) → field 28.
    float delta = level - b->fullFill;
    g_stockHooks->QueuePriceUpdate(b->marker /*id at +1 in orig*/, 28, delta);
    return level;
}

// gilde.exe 0x57d3d0 — VIBE_Building_ComputeEfficiencyScore
float Building_ComputeEfficiencyScore(const BuildingRec* b) {
    // word_12CE910[268*a1] base; gate: active byte (+8) <= 1 → 0.
    if (!b || b->activeFlag <= 1)
        return 0.0f;
    float maxOut = Building_ComputeMaxOutput(b);
    float curOut = Building_ComputeCurrentOutput(b);
    return static_cast<float>(maxOut * kEffMaxBase * kEffMaxW
                              + (curOut / maxOut) * kEffRatioW);
}

// gilde.exe 0x57d5b4 — VIBE_Building_AdjustStockAndNotify
float Building_AdjustStockAndNotify(const BuildingStockRec* b, int addAmount) {
    // gate: marker==-1 (0xFFFF), kind==10 (storage), or !active → -1.
    if (!b || b->marker == 0xFFFF || b->kind == 10 || !b->active)
        return -1.0f;

    double stock = static_cast<double>(b->stock);    // (WORD)v23[5] == +10
    float v19;
    if (stock <= b->fillCap)                          // float[8] == +32
        v19 = (b->fullFill - b->zeroFill) * static_cast<float>(stock) / b->fillCap
              + b->zeroFill;                          // float[7]/[4] == +28/+20
    else
        v19 = b->fullFill;
    double v4 = static_cast<double>(b->delta) + v19;  // int[9] == +36
    double total = v4 + static_cast<double>(addAmount);
    float v16 = (total <= 0.0) ? 0.0f : static_cast<float>(total);

    if (v16 > 0.0f) {
        // BeginDeltaPacket + AppendRawField(4,1,&add,36) → field 36 update.
        g_stockHooks->QueuePriceUpdate(b->marker, 36, static_cast<float>(addAmount));
        return v16;
    }
    // else: clear active byte + queue a slot reset (114-byte cmd).
    g_stockHooks->QueuePriceUpdate(b->marker, /*active byte +8*/ 8, 0.0f);
    g_stockHooks->FinaliseCustomerUpdate(b->marker);
    return 0.0f;
}

// gilde.exe 0x57dcb4 — VIBE_Building_FindMatchingSupplier
const BuildingSaleRec* Building_FindMatchingSupplier(
    const BuildingSaleRec* persons, const BuildingSaleRec* self) {
    if (!persons || !self)
        return nullptr;
    if (self->kind == 6 || self->kind == 7)   // *(BYTE*)(a1+2)
        return nullptr;

    const i32 selfId = self->id;               // *(DWORD*)(a1+4)
    for (int idx = 0; idx < 768; ++idx) {
        const BuildingSaleRec* p = &persons[idx];
        // skip the building itself.
        if (p->id == selfId)                   // *((DWORD*)v11+1) == id
            continue;
        if (p->kind != 6 && p->kind != 7)      // *((BYTE*)v11+2)
            continue;
        // owner columns 23/24/25 (dword indices) == +92/+96/+100; the orig reads
        // *((DWORD*)v11+23..25): the first customer-id columns. A match means this
        // sale building already serves selfId → not a fresh supplier; skip.
        const u8* pb = reinterpret_cast<const u8*>(p);
        i32 c23, c24, c25;
        std::memcpy(&c23, pb + 92, 4);
        std::memcpy(&c24, pb + 96, 4);
        std::memcpy(&c25, pb + 100, 4);
        if (c23 == selfId || c24 == selfId || c25 == selfId)
            continue;
        // scan the customer slots: the orig walks v8=v11+6 .. v11+16 (dword)
        // reading +92 off each (== +104 + 4*k), counting filled (!= -1) and
        // flagging the selfId match; a free supplier has the match set and < 5
        // customers.
        int matched = 0, count = 0;
        for (int k = 0; k < 5; ++k) {
            i32 cust = BPCustomerId(p, k);     // +104 + 4*k
            if (cust == selfId) matched |= 1;
            else if (cust == -1) break;
            ++count;
        }
        if (matched && count < 5)
            return p;
    }
    return nullptr;   // original returns 1 (truthy non-record); nullptr here.
}

// gilde.exe 0x5913e0 — VIBE_Building_SumFlaggedSlotsWorth
int Building_SumFlaggedSlotsWorth(u8 typeIndex) {
    const BuildingTypeDef* td = BuildingTypeDefAt(typeIndex);
    if (!td)
        return 0;
    // base term: 3840 * roomWorthMul (dword_13C... +585).
    int worth = 3840 * td->roomWorthMul;       // v8
    int n = 0;
    // walk roomList; sign byte (room+1, == the upper byte of the room word's
    // high-bit flag) < 0 means "flagged".  We test bit 0x8000 of the room word.
    for (int i = 0; i < 64; ++i) {
        u16 room = td->roomList[i];
        if (room == 0) break;
        u16 kind = room & 0x7FFF;              // HIBYTE(v4) &= ~0x80
        bool flagged = (room & 0x8000) != 0;   // *(char*)(slot+1) < 0
        if (flagged) {
            double v7 = Building_ComputeMarketPrice(static_cast<i16>(kind), 100)
                        + static_cast<double>(worth);
            worth = truncToZero(v7);           // VIBE_Coord_ConvertX
        }
        ++n;
        if (n >= 64) break;
    }
    return worth;
}

// gilde.exe 0x591480 — VIBE_Building_ComputeSalePrice
int Building_ComputeSalePrice(u8 typeIndex, u8 qualityCode) {
    int worth = Building_SumFlaggedSlotsWorth(typeIndex);   // v2
    float qmul = (qualityCode == 14) ? 0.85000002f : 1.0f;  // v8
    int taxTier = g_stockHooks->SaleTaxTier();              // Gesetz_GetRecord(6) +0
    double price = (static_cast<double>(4 - taxTier) * kSaleTaxSlope + kSaleTaxBase)
                   * static_cast<double>(worth) * qmul;
    return truncToZero(price);                              // VIBE_Coord_ConvertX
}

// gilde.exe 0x5905dc — VIBE_Building_SumWorkstationCount
int Building_SumWorkstationCount(u8 typeIndex, u8 category) {
    const BuildingTypeDef* td = BuildingTypeDefAt(typeIndex);
    if (!td)
        return 0;
    const u8* base = reinterpret_cast<const u8*>(td);
    int sum = 0;                                            // v2
    // walk 64 rooms; the orig steps room word (+35) and a per-room byte block at
    // +419 (category) / +483 (worker count); both share the 64-entry geometry.
    for (int i = 0; i < 64; ++i) {
        u16 room;
        std::memcpy(&room, base + 35 + 2 * i, 2);
        room &= 0x7FFF;                                     // HIBYTE &= ~0x80
        if (room && base[419 + i] == category)             // *(BYTE)(v4+419)==cat
            sum += base[483 + i];                          // *(BYTE)(v4+483)
    }
    return sum;
}

// gilde.exe 0x5904fc — VIBE_Building_SumWorkstationByCategory
int Building_SumWorkstationByCategory(const BuildingSaleRec* b, u8 typeIndex,
                                      u8 category, bool scale) {
    const BuildingTypeDef* td = BuildingTypeDefAt(typeIndex);
    if (!td || !b)
        return 0;
    const u8* base = reinterpret_cast<const u8*>(td);
    int sum = 0;                                            // v10
    for (int i = 0; i < 64; ++i) {
        u16 room;
        std::memcpy(&room, base + 35 + 2 * i, 2);
        room &= 0x7FFF;
        if (room && base[419 + i] == category
            && g_stockHooks->RoomPresent(b, static_cast<i16>(room))) {
            sum += base[483 + i];
        }
    }
    if (!scale)
        return sum;
    // (double)a1[92] * 0.01 * sum + 0.5  (a1[92] == quality byte at +92, here the
    // building's price/quality scalar).  a1[92] is the +0x5C person byte; we reuse
    // the customer/quality column the orig reads (low byte of sceneNodeId word).
    u8 q92 = static_cast<u8>(b->sceneNodeId & 0xFF);
    double v9 = static_cast<double>(static_cast<signed char>(q92)) * 0.01
                * static_cast<double>(sum) + 0.5;
    return truncToZero(v9);
}

// gilde.exe 0x57d83c — VIBE_Building_DistributeGoodsToCustomers
const BuildingSaleRec* Building_DistributeGoodsToCustomers(BuildingSaleRec* b) {
    if (!b)
        return b;
    // gate: kind>=10 || !active(+8) || state(+9)!=1 || (flag458 & 4).
    if (b->kind >= 10 || !b->active || b->state != 1 || (b->flag458 & 4) != 0)
        return b;

    // resolve the associated customer record and read its price (+124).
    const BuildingSaleRec* cust =
        g_stockHooks->ResolveOwnerRecord(b->sceneNodeId);  // Person_FindRecordById(+92)
    float myPrice = b->price;                               // *(float*)(v2+124)

    // High path: my price float bit-pattern > 1.0f (SHIDWORD > 1065353216).
    if (reinterpret_cast<i32&>(myPrice) > 1065353216) {
        // drift price toward target: rand*(-0.5) - myPrice  → field 124.
        float drift = static_cast<float>(util::RandomFloatScaled()) * kDistDrift - myPrice;
        if (!g_stockHooks->QueuePriceUpdate(b->id, 124, drift))
            return b;
        // pick a transfer count: 2, or the owner's slot count when no customers.
        int count = 2;                                      // v44
        // count this building's filled customer slots (+92 family of cust).
        if (cust && (cust->kind == 6 || cust->kind == 7)) {
            int filled = 0;
            for (int k = 0; k < 4; ++k)
                if (BPCustomerId(cust, k) != -1) ++filled;
            if (!filled)
                count = cust->id >> 24;                     // *(int*)(v46+6)>>24 path
        }
        // pick the prot to transfer (owner-rec +368 → +1, else -1).
        i32 prot = (b->ownerRecPtr ? /*owner.id at +1*/ b->ownerRecPtr : -1);
        // schedule the future appointment time (GameTime_Advance +8h + rand 4),
        // modelled by the hook (Amt keepalive loop folded in).
        int status = g_stockHooks->EnqueueGoodsTransfer(
            cust ? cust->id : b->id, b->id, prot, count);
        if (status == 2)
            return b;
        if (cust)
            g_stockHooks->FinaliseCustomerUpdate(cust->id);
        return b;
    }

    // Fallback path: count real customers and nudge price up.
    if (cust && cust->active && /* *(WORD*)(v2+10) <= 0x26 */ true) {
        int real = 0;                                       // v4
        for (int k = 1; k < 5; ++k) {                       // v3 = 3..8 (slots 1..4)
            i32 cid = BPCustomerId(b, k);
            if (cid == -1) continue;
            const BuildingSaleRec* c = g_stockHooks->ResolveOwnerRecord(cid);
            if (!c) continue;
            u8 ck = c->kind;
            if (ck == 6 || ck == 7 || ck == 5)              // sale/auction/player
                return b;                                   // early-out (orig returns)
            if (ck != 15)
                ++real;
        }
        if (real != 4) {
            // price += rand*nudge[real] + nudge[real]  → field 124.
            float nf = kCustNudge[real & 7];
            float bump = static_cast<float>(util::RandomFloatScaled()) * nf + nf;
            g_stockHooks->QueuePriceUpdate(b->id, 124, bump);
        }
    }
    return b;
}

// gilde.exe 0x58fe68 — VIBE_BuildingValue_ComputeProductionWorth
ProductionWorth BuildingValue_ComputeProductionWorth(const BuildingSaleRec* b,
                                                     u8 selfTypeIndex) {
    ProductionWorth out;
    std::memset(&out, 0, sizeof(out));   // VIBE_Light_SetGrayColorThunk(0,84,a2)
    if (!b)
        return out;

    int v5 = 0;     // running production total (ebp)
    int v38 = 0;    // a2[16] (production sub-total)
    int v39 = 0;    // a2[17]-feeding running total

    // ----- per-worker production sum (the matching Person-walk + ComputeItemBase
    // value loop).  The originals join the worker records through the parallel
    // dword_12CEA7C column and the worker's +353 packed profession; that join is
    // owned by the entity module, so we consult it as a hook returning the already
    // accumulated value (the v5 += (int)(ItemBaseValue * quality*0.01) total). The
    // arithmetic shape (quality*0.01 scale, ComputeItemBaseValue) is reproduced in
    // the default hook for a single self-worker so the leaf stays testable. -----
    {
        const BuildingRec* asBld = reinterpret_cast<const BuildingRec*>(b);
        double qScale = static_cast<double>(b->quality) * kQualityScale; // +61
        // WAVE-16 1:1 FIX (worth loop @0x58fe68 vs ComputeItemBaseValue @0x58f328):
        //   output loop  v10 = 0..5: ComputeItemBaseValue(td, kind, -1, v10)
        //                -> a4 = v10 -> the 6-wide +553 array (inputFactor[v10]);
        //   input  loop  v14 = 0..1: ComputeItemBaseValue(td, kind, v14, -1)
        //                -> a3 = v14 -> the 2-wide +563 array (outputFactor[v14]);
        //   fallback (no slot matched): ComputeItemBaseValue(td, kind, 0, -1)
        //                -> a3 = 0   -> +563[0].
        // The original adds every matching slot UNCONDITIONALLY (v5 += (int)v12;
        // v9 = 1) — no contrib>0 gate. The match gate in the binary is the worker
        // profession join (v45[547+v10] != 0 && == worker prof); that join lives
        // in the entity/worker module (dword_12CEA7C, 768x536), consulted here as
        // a hook. For the single-self-worker default we treat every slot with a
        // non-zero factor as a match (factor 0 => no output good in that slot).
        // Column targets verified against disasm @0x58fe68:
        //   output sweep (a4 path) writes a2[6]  -> [edx+18h]  (col[6])
        //   input  sweep (a3 path) writes a2[4]  -> [edx+10h]  (col[4])
        //   fallback     (a3==0)   writes a2[4]  -> [edx+10h]  (col[4])
        bool any = false;
        for (int o = 0; o < 6; ++o) {                         // a4 = o -> +553[o]
            float iv = Building_ComputeItemBaseValue(asBld, selfTypeIndex, -1, o);
            if (iv != 0.0f) {
                int contrib = truncToZero(iv * qScale);
                v5 += contrib; out.col[6] += contrib; any = true;
            }
        }
        for (int in = 0; in < 2; ++in) {                      // a3 = in -> +563[in]
            float iv = Building_ComputeItemBaseValue(asBld, selfTypeIndex, in, -1);
            if (iv != 0.0f) {
                int contrib = truncToZero(iv * qScale);
                v5 += contrib; out.col[4] += contrib; any = true;
            }
        }
        if (!any) {                                           // a3 = 0 -> +563[0]
            float iv = Building_ComputeItemBaseValue(asBld, selfTypeIndex, 0, -1);
            int contrib = truncToZero(iv * qScale);
            v5 += contrib; out.col[4] += contrib;
        }
    }

    out.col[3] = v5;          // a2[3] = production total
    out.col[18] = b->worth77; // a2[18] = *(DWORD)(a1+77)
    v38 = v5;

    // per-kind worth columns.  The binary reads *v40 (the TYPE-DEF +0 kind byte,
    // v40 = 589*selfTypeIndex + dword_13CE294), NOT the building record's +2 object
    // kind.  Verified @0x5900d4/0x5900ef/0x5901de (cmp byte ptr [v40], 5/7/22).
    const BuildingTypeDef* selfTd = BuildingTypeDefAt(selfTypeIndex);
    u8 kind = selfTd ? selfTd->kind : 0;
    if (kind == 5) {
        out.col[10] = b->worth85;   // a2[10] = *(DWORD)(a1+85)
        v39 = b->worth85;
    }
    if (kind == 7) {
        // a2[12] += scene-room worth (QueryFind walk) — folded into worth columns.
        out.col[12] += 0;                       // scene walk → hook (0 default)
        v39 += out.col[12];
        out.col[15] += 0;                       // second scene walk
        v39 += out.col[15];
        out.col[13] += b->worth101;             // a2[13] += *(DWORD)(a1+101)
        out.col[14] += b->worth105;             // a2[14] += *(DWORD)(a1+105)
        v39 += out.col[13] + out.col[14];
    } else if (kind == 22) {
        out.col[19] = b->worth109;              // a2[19] = *(DWORD)(a1+109)
        v39 += b->worth109;
    }

    out.col[16] = v38;
    out.col[20] = v39 - v38;
    out.col[17] = v39;
    out.nonZero = (out.col[16] != 0 || v39 != 0);
    return out;
}

}  // namespace guild::sim

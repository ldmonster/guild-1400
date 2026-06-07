#include "sim/building_storage.h"

#include "sim/building.h"             // BuildingTypeDefAt
#include "sim/building_production.h"  // Building_ComputeMarketPrice, SceneTypeDefAt

namespace guild::sim {

static constexpr float  kSV_Cap    = 2560000.0f;  // flt_6269D4
static constexpr double kSV_M2     = 0.04;        // dbl_6269DC  (kind != 2)
static constexpr double kSV_M2b    = 0.09;        // dbl_6269E4  (kind == 2)
static constexpr double kSV_StockM = 0.3;         // dbl_6269EC
static constexpr float  kRW_Mul    = 0.009999999776482582f; // flt_626A10

static inline int truncToZero(double x) {
    return static_cast<int>(static_cast<long long>(x));
}

// Reserve goods whose effective stock is count-1 (matches sim::ItemType).
static inline bool IsReserveGood(i16 prot) {
    return prot == kItemReserveA || prot == kItemReserveB ||
           prot == kItemReserveC || prot == kItemReserveD;
}

static IStorageHooks  g_defaultStorageHooks;
static IStorageHooks* g_storageHooks = &g_defaultStorageHooks;
void SetStorageHooks(IStorageHooks* hooks) {
    g_storageHooks = hooks ? hooks : &g_defaultStorageHooks;
}
IStorageHooks* StorageHooks() { return g_storageHooks; }

// ---------------------------------------------------------------------------
// gilde.exe 0x591658 — VIBE_BuildingValue_SumStorageItemWorth
//   v5 = 0;
//   for each item in storage room:
//       v4 = ComputeMarketPrice(*v3, *(v3+18)) + v5;
//       v5 = (int)v4;
//   return v5;
// ---------------------------------------------------------------------------
int BuildingValue_SumStorageItemWorth(const BuildingRec* b) {
    int v5 = 0;
    for (const StorageItem& it : g_storageHooks->StorageItems(b)) {
        double v4 = Building_ComputeMarketPrice(it.prot, it.qtyByte) +
                    static_cast<double>(v5);
        v5 = truncToZero(v4);
    }
    return v5;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x590360 — VIBE_BuildingValue_ComputeStockValue
// ---------------------------------------------------------------------------
StockValue BuildingValue_ComputeStockValue(const BuildingRec* b, u16 ownerId,
                                           u8 buildingKind) {
    StockValue out{};

    float v18 = 0.0f;
    if (ownerId != 0xFFFF) {
        float wealth = static_cast<float>(g_storageHooks->OwnerWealth(ownerId));
        // clamp to flt_6269D4 (2,560,000).
        float v17 = (static_cast<double>(kSV_Cap) >= static_cast<double>(wealth))
                        ? wealth : kSV_Cap;
        v18 = v17;
    }

    double v4 = (buildingKind == 2)
                    ? static_cast<double>(v18) * kSV_M2b
                    : static_cast<double>(v18) * kSV_M2;
    float v16 = (v4 >= 0.0) ? static_cast<float>(v4) : 0.0f;
    out.ownerShare = truncToZero(static_cast<double>(v16));

    // stock worth.
    double v20 = 0.0;
    for (const StorageItem& it : g_storageHooks->StorageItems(b)) {
        int qty = IsReserveGood(it.prot) ? (it.count - 1) : it.count;
        int v14 = truncToZero(static_cast<double>(qty) * kSV_StockM);
        if (v14 < 1) v14 = 1;
        v20 = Building_ComputeMarketPrice(it.prot, 100) * static_cast<double>(v14) + v20;
    }
    out.stockWorth = truncToZero(v20);
    return out;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x59116c — VIBE_BuildingValue_ComputeRoomWorth
//   v19 = 3840 * typeDef.roomWorthMul;
//   walk the room list; for storage rooms (kind 2/6) add item market price;
//   return (int)((double)mul * 0.01 * v19).
// ---------------------------------------------------------------------------
int BuildingValue_ComputeRoomWorth(const BuildingRec* b, int mul) {
    const BuildingTypeDef* td = BuildingTypeDefAt(static_cast<u8>(b->typeIndex));
    if (!td)
        return 0;

    int v19 = 3840 * td->roomWorthMul;

    bool inStorageRun = false;   // v21
    for (int i = 0; i < 64; ++i) {
        u16 raw = td->roomList[i];
        if (raw == 0)
            break;
        i16 roomProt = static_cast<i16>(raw & 0x7FFF);   // HIBYTE(v6) &= ~0x80
        SceneTypeDef* rtd = SceneTypeDefAt(roomProt);
        u8 kind = rtd ? rtd->kind : 0;

        if (!inStorageRun) {
            if (kind == 2 || kind == 6) {
                for (const StorageItem& it : g_storageHooks->RoomItems(b, roomProt)) {
                    double v9 = Building_ComputeMarketPrice(it.prot, it.qtyByte) +
                                static_cast<double>(v19);
                    v19 = truncToZero(v9);
                }
                inStorageRun = true;
            }
        } else {
            for (const StorageItem& it : g_storageHooks->RoomItems(b, roomProt)) {
                double v17 = Building_ComputeMarketPrice(it.prot, it.qtyByte) +
                             static_cast<double>(v19);
                v19 = truncToZero(v17);
            }
            // next room kind 2/6 re-opens a fresh storage run.
            if (i + 1 < 64) {
                i16 nextProt = static_cast<i16>(td->roomList[i + 1] & 0x7FFF);
                SceneTypeDef* ntd = SceneTypeDefAt(nextProt);
                u8 nk = ntd ? ntd->kind : 0;
                if (nk == 2 || nk == 6)
                    inStorageRun = false;
            }
        }
    }

    double v12 = static_cast<double>(mul) * static_cast<double>(kRW_Mul) *
                 static_cast<double>(v19);
    return truncToZero(v12);
}

}  // namespace guild::sim

#include "ai/meister_storage.h"

#include <algorithm>
#include <cmath>

// MeisterAi storage / workstation ORCHESTRATION (gilde.exe 0x45f1e4 / 0x4599f0 /
// 0x45a62c). The sell-decision cores and the table-build sweeps are translated 1:1
// here; the per-item math (ComputeWorkstationOutput / SortWorkstationsByScore /
// GatherClampToBudget …) lives in meister_workstation.cpp and is reused. The cart
// unload / storage-reconcile / reload tail of TradeManageStorage and the 100+
// German cm_RequestSellObjekt log lines are DEFERRED (LISTed in the report) —
// pure scene-storage-tree + string plumbing.

namespace guild::ai {

namespace {
// the special-output item ids that AssignWorkstations duplicates as their own
// stations (the 449..454 block) and the storage-bit it stamps.
bool IsSpecialHigh(u16 id) { return id == 452 || id == 453 || id == 454; }
bool IsSpecialLow(u16 id)  { return id == 449 || id == 450 || id == 451; }
} // namespace

// ---------------------------------------------------------------------------
// Sell-decision cores
// ---------------------------------------------------------------------------

// gilde.exe 0x45f1e4 (item demand-bump step).
int StorageDemandBump(int itemBits, int stock, int slotCapacity, bool aiTypeIs22) {
    if ((itemBits & 6) != 0)   // already matched/reserved -> skip
        return 0;
    if ((itemBits & 8) == 0)   // not flagged for restock -> skip
        return 0;
    if (slotCapacity / 3 >= stock)
        return 0;
    return aiTypeIs22 ? (stock / 2) : stock;
}

// gilde.exe 0x45f1e4 / 0x4614d0 (profit-margin sell step).
int ProfitMarginSell(int workstationBits, int stock, float sellPrice,
                     float buyPrice, double rollThreshold, bool storageVariant,
                     int slotCapacityOrCartCap) {
    if ((workstationBits & 6) != 0)
        return 0;
    // the (bits & 8) gate: storage variant requires stock > cartCap(3); general
    // variant requires stock > slotCap/3. When bit 8 is clear, both proceed.
    if ((workstationBits & 8) != 0) {
        if (storageVariant) {
            if (stock <= slotCapacityOrCartCap)  // cartCap == 3
                return 0;
        } else {
            if (slotCapacityOrCartCap / 3 >= stock)
                return 0;
        }
    }
    if (buyPrice == 0.0f)
        return 0;
    double ratio = static_cast<double>(sellPrice) / static_cast<double>(buyPrice);
    // ratio < 1.0 (the 0x3F800000 compare) AND ratio <= rollThreshold.
    if (ratio < 1.0 && ratio <= rollThreshold)
        return stock; // dump the whole stock
    return 0;
}

// gilde.exe 0x45f1e4 (emergency sell step).
int EmergencySell(int workstationBits, int stock, int price, int* proceeds) {
    if ((workstationBits & 6) != 0)
        return 0;
    if (proceeds && *proceeds >= kEmergencyTarget)
        return 0;
    if (price <= 0)
        return 0;
    int qty = kEmergencyTarget / price; // 32000 / price
    if (qty < 1)
        qty = 1;
    int raw = qty;            // the un-clamped qty drives the proceeds running sum
    if (qty >= stock)
        qty = stock;
    if (proceeds)
        *proceeds += raw * price;
    return qty;
}

// gilde.exe 0x45f1e4 (overstock sell step).
int OverstockSell(int workstationBits, int stock, int slotCapacity) {
    if ((workstationBits & 6) != 0)
        return 0;
    if (3 * slotCapacity / 4 >= stock)
        return 0;
    int qty = stock / 4;
    if (qty < kOverstockMin)
        qty = kOverstockMin;
    if (qty >= stock)
        qty = stock;
    return qty;
}

// gilde.exe 0x45f1e4 (orchestration).
int RunStorageSellPass(StoragePlayer& player, std::vector<WsItem>& items,
                       std::vector<WsStation>& stations, bool storageVariant,
                       const std::function<double()>& roll,
                       float (*sell_price)(u16 typeId),
                       float (*buy_price)(u16 typeId),
                       std::vector<StorageCommand>& out) {
    int emitted = 0;

    // --- demand bump over the item table -----------------------------------
    bool aiTypeIs22 = (player.aiType == 22);
    for (WsItem& it : items) {
        int req = StorageDemandBump(it.bits, it.stock, player.slotCapacity, aiTypeIs22);
        if (req != 0) {
            it.required = req;
            out.push_back(StorageCommand{StorageCmd::DemandBump, it.id, req, player.account});
            ++emitted;
        }
    }

    // --- the odd-hour sell block (runs once per odd hour, guarded by +437&2) -
    if (player.oddHour) {
        if (!player.sellDoneFlag) {
            const double bias = storageVariant ? kProfitSellBias : 0.0;
            const int cartCap = 3;

            // 1. profit-margin sell.
            for (WsStation& s : stations) {
                double thr = roll() * static_cast<double>(kProfitSellSlope) + bias;
                float sp = sell_price(s.typeId); // ComputeMarketPrice(typeId,100)
                float bp = buy_price(s.typeId);  // LookupCachedMarketPrice(typeId)
                int qty = ProfitMarginSell(s.bits, s.stock, sp, bp, thr, storageVariant,
                                           storageVariant ? cartCap : player.slotCapacity);
                if (qty != 0) {
                    out.push_back(StorageCommand{StorageCmd::ProfitSell, s.typeId, qty,
                                                 player.account});
                    ++emitted;
                }
            }

            // 2. emergency sell — only when cash-strapped.
            bool strapped = (player.funds < kEmergencyFundsFloor) ||
                            (player.heldCurrency < kEmergencyHeldFloor);
            if (strapped) {
                // projected proceeds start at min(held, funds).
                int proceeds = std::min(player.heldCurrency, player.funds);
                if (proceeds < kEmergencyTarget) {
                    for (WsStation& s : stations) {
                        if (proceeds >= kEmergencyTarget)
                            break;
                        int price = static_cast<int>(buy_price(s.typeId));
                        int qty = EmergencySell(s.bits, s.stock, price, &proceeds);
                        if (qty != 0) {
                            out.push_back(StorageCommand{StorageCmd::EmergencySell,
                                                         s.typeId, qty, player.account});
                            ++emitted;
                        }
                    }
                }
            }

            // 3. overstock sell.
            for (WsStation& s : stations) {
                int qty = OverstockSell(s.bits, s.stock, player.slotCapacity);
                if (qty != 0) {
                    out.push_back(StorageCommand{StorageCmd::OverstockSell, s.typeId, qty,
                                                 player.account});
                    ++emitted;
                }
            }
        }
        player.sellDoneFlag = true; // +437 |= 2
    } else {
        player.sellDoneFlag = false; // +437 &= ~2
    }

    return emitted;
}

// ---------------------------------------------------------------------------
// AssignWorkstations table-build sweep
// ---------------------------------------------------------------------------

namespace {
// find an item by id in the table; returns index or -1.
int FindItem(const std::vector<WsItem>& items, u16 id) {
    for (std::size_t i = 0; i < items.size(); ++i)
        if (items[i].id == id)
            return static_cast<int>(i);
    return -1;
}
// find a station by typeId; returns index or -1.
int FindStation(const std::vector<WsStation>& st, u16 id) {
    for (std::size_t i = 0; i < st.size(); ++i)
        if (st[i].typeId == id)
            return static_cast<int>(i);
    return -1;
}
// append a freshly-initialized item record; returns its index.
int AddItem(std::vector<WsItem>& items, u16 id, float buyPrice) {
    WsItem it;
    it.id = id;
    it.backIndex = -1;
    it.unitPrice = buyPrice;
    it.flags50 = 129;
    it.bits = 0;
    items.push_back(it);
    return static_cast<int>(items.size()) - 1;
}
// append a freshly-initialized station record; returns its index.
int AddStation(std::vector<WsStation>& st, u16 id) {
    WsStation s;
    s.typeId = id;
    s.slots[0] = s.slots[1] = s.slots[2] = s.slots[3] = -1;
    s.reserveTarget = 33;
    s.outCache = kNotComputed;
    st.push_back(s);
    return static_cast<int>(st.size()) - 1;
}
} // namespace

// gilde.exe 0x4599f0 — workstation + item table build.
void AssignWorkstations(const std::vector<WsCandidate>& candidates,
                        const std::vector<WsDelivery>& deliveries,
                        bool aiTypeAllowsSpecial,
                        std::vector<WsStation>& stations,
                        std::vector<WsItem>& items,
                        const StorageEnv& env,
                        std::vector<StorageCommand>& out) {
    stations.clear();
    items.clear();

    // 1. collect workstations (count > 1).  (candidate index == station index)
    std::vector<const WsCandidate*> stationCand;
    for (const WsCandidate& c : candidates) {
        if (c.count <= 1)
            continue;
        if (stations.size() >= 32) // capacity (dword_B56464 holds 32)
            break;
        AddStation(stations, c.typeId);
        stationCand.push_back(&c);
    }

    // 2. expand each station's input slots into the item table (slot k -> item idx).
    //    A non-zero input id that is not yet in the item table gets a fresh record.
    for (std::size_t si = 0; si < stations.size(); ++si) {
        const WsCandidate& c = *stationCand[si];
        for (int k = 0; k < 4; ++k) {
            u16 inId = c.inputId[k];
            if (inId == 0)
                continue;
            int ii = FindItem(items, inId);
            if (ii < 0) {
                if (items.size() >= 128) // item table capacity
                    continue;
                float buy = env.ws.market_price ? env.ws.market_price(inId) : kNotComputed;
                ii = AddItem(items, inId, kNotComputed); // -1e10 recompute sentinel
                items[static_cast<std::size_t>(ii)].unitPrice = kNotComputed;
                (void)buy;
            }
            stations[si].slots[k] = ii;
        }
    }

    // 3. special-output duplication (ids 449..454) when the AI type allows it.
    if (aiTypeAllowsSpecial) {
        const std::size_t nItems = items.size();
        for (std::size_t i = 0; i < nItems; ++i) {
            u16 id = items[i].id;
            if (IsSpecialHigh(id)) {
                items[i].bits |= 0x24; // 0x24 storage/matched
                if (FindStation(stations, id) < 0 && stations.size() < 32) {
                    int sj = AddStation(stations, id);
                    stations[static_cast<std::size_t>(sj)].bits = 36; // 0x24
                }
            } else if (IsSpecialLow(id)) {
                items[i].bits |= 0x204;
                if (FindStation(stations, id) < 0 && stations.size() < 32) {
                    int sj = AddStation(stations, id);
                    stations[static_cast<std::size_t>(sj)].bits = 516; // 0x204
                }
            }
        }
    }

    // back-link each item to its producing station by id (dword_B54454). Done
    // BEFORE ComputeWorkstationOutput so the chain-discount (0.9*price) applies to
    // items that are themselves produced.
    for (WsItem& it : items)
        it.backIndex = FindStation(stations, it.id);

    // 4. snapshot stock + free capacity for each station and item, and Notify20 on
    //    zero item stock.
    for (WsStation& s : stations) {
        s.stock   = env.stock_of(s.typeId);
        s.freeCap = env.free_cap(s.typeId);
    }
    for (WsItem& it : items) {
        it.stock = env.stock_of(it.id);
        it.freeCap = env.free_cap(it.id);
        if (it.stock == 0) {
            out.push_back(StorageCommand{StorageCmd::Notify20, it.id, 0, 0});
        }
    }

    // 5. fold handler deliveries into station.incoming + item.reserved.
    for (const WsDelivery& d : deliveries) {
        int si = FindStation(stations, d.itemId);
        if (si >= 0)
            stations[static_cast<std::size_t>(si)].incoming += d.amount;
        int ii = FindItem(items, d.itemId);
        if (ii >= 0)
            items[static_cast<std::size_t>(ii)].reserved += d.amount;
    }

    // 6. compute output for each station, sort by score, stamp final indices.
    for (WsStation& s : stations)
        ComputeWorkstationOutput(s, items, env.ws);
    SortWorkstationsByScore(stations);

    // 7. mark matched items (post-sort, re-resolving by id since the sort permuted
    //    the station indices): an item with a producing station gets 0x40 on both,
    //    and its backIndex is refreshed to the producer's new index.
    for (WsItem& it : items) {
        if (it.backIndex == -1)
            continue;
        int sj = FindStation(stations, it.id);
        if (sj >= 0) {
            it.bits |= 0x40;
            stations[static_cast<std::size_t>(sj)].bits |= 0x40;
            it.backIndex = sj;
        }
    }
}

// gilde.exe 0x45a62c — item table build from import/export lists.
void CollectStorageItems(const std::vector<u16>& imports,
                         const std::vector<u16>& exports,
                         const std::vector<WsDelivery>& deliveries,
                         std::vector<WsItem>& items,
                         const StorageEnv& env) {
    auto addList = [&](const std::vector<u16>& list) {
        for (u16 id : list) {
            if (id == 0)
                break;                 // 0 terminator
            if (FindItem(items, id) >= 0)
                continue;              // already present
            float buy = env.ws.market_price ? env.ws.market_price(id) : 0.0f;
            int idx = AddItem(items, id, buy);
            WsItem& it = items[static_cast<std::size_t>(idx)];
            it.stock = env.stock_of(id);
            it.reserved = 0;
            it.freeCap = env.free_cap(id);
        }
    };
    addList(imports);
    addList(exports);

    // fold handler deliveries into item.reserved.
    for (const WsDelivery& d : deliveries) {
        int ii = FindItem(items, d.itemId);
        if (ii >= 0)
            items[static_cast<std::size_t>(ii)].reserved += d.amount;
    }
}

namespace {
bool InList(const std::vector<u16>& list, u16 id) {
    for (u16 v : list) {
        if (v == 0)
            break;
        if (v == id)
            return true;
    }
    return false;
}
} // namespace

// gilde.exe 0x45c10c — the full gather sweep.
int GatherRequiredItems(const std::vector<GatherNeed>& needs,
                        const std::vector<u16>& imports,
                        const std::vector<u16>& exports,
                        std::vector<WsItem>& items, int budget,
                        void* (*seller_for)(u16 itemId)) {
    // 1. bump item.required for each consumer need that is a tradeable good.
    for (const GatherNeed& nd : needs) {
        int ii = FindItem(items, nd.itemId);
        if (ii < 0)
            continue;
        WsItem& it = items[static_cast<std::size_t>(ii)];
        if (!InList(imports, it.id) && !InList(exports, it.id))
            continue;
        ++it.required;
        it.bits |= 0x2;
    }

    // 2. per item with required>0: net the shortfall and pick a seller.
    for (WsItem& it : items) {
        if (it.required == 0)
            continue;
        if (!InList(imports, it.id) && !InList(exports, it.id))
            continue;
        it.required = GatherNetShortfall(it.required, it.reserved, it.stock);
        if (it.required != 0) {
            void* seller = seller_for ? seller_for(it.id) : nullptr;
            it.sourceBuilding = seller;
        }
        if (it.sourceBuilding == nullptr)
            it.required = 0; // no seller -> drop the demand
    }

    // 3. clamp the purchase quantities to the budget (the recovered core).
    return GatherClampToBudget(items, budget);
}

// gilde.exe 0x45bd68 — recursive reservation stamp.
int ReserveWorkstationItems(std::vector<WsStation>& stations, std::size_t stationIdx,
                            std::vector<WsItem>& items, const MeisterWsEnv& env,
                            const StockNeedQuery& needs) {
    WsStation& s = stations[stationIdx];
    const WsTypeDef* td = env.type_def(s.typeId);

    for (int k = 0; k < 4; ++k) {
        int idx = s.slots[k];
        if (idx < 0)
            continue;
        WsItem& it = items[static_cast<std::size_t>(idx)];

        // clamp the item reserve target (field18) to the station's, set 0x2 bit.
        if (it.field18 >= s.reserveTarget)
            it.field18 = s.reserveTarget;
        it.bits |= 0x2;

        int need = td ? td->inputNeed[k] : 0;

        // special service ids: just stamp the 0x10 headroom flag when there is
        // capacity and the chained production is below 4x the slot need.
        if (it.id == 449 || it.id == 450 || it.id == 451 ||
            it.id == 452 || it.id == 453 || it.id == 454) {
            if (it.backIndex >= 0) {
                WsStation& prod = stations[static_cast<std::size_t>(it.backIndex)];
                if (4 * (prod.incoming + prod.stock) < prod.freeCap &&
                    it.stock + it.reserved < 4 * need) {
                    it.bits |= 0x10;
                }
            }
            continue;
        }

        if (it.backIndex == -1) {
            // raw material: only when not yet on hand (reserved+stock short).
            if (it.stock + it.reserved >= 4 * need)
                continue;
            std::vector<StockSeller> sellers = needs.sellers ? needs.sellers(it.id)
                                                             : std::vector<StockSeller>{};
            for (const StockSeller& sl : sellers) {
                if (sl.isSelf || sl.category == 2 || !sl.hasObject)
                    continue;
                if (need > sl.deficit)
                    continue;
                if (sl.sameOwner) {
                    // free transfer within the faction: reserve the full chain qty.
                    it.field18 = it.stock; // *((DWORD*)v5+5) = *((DWORD*)v5+11)
                } else {
                    // priced reservation: clamp to deficit (the recovered supply
                    // math is in CheckWorkstationCapacity::AffordableSupply).
                    int qty = sl.deficit;
                    if (qty >= need && qty > it.field18)
                        it.field18 = qty;
                }
                break; // first viable seller wins (the original's first match)
            }
        } else {
            // chained product: clamp the producer's reserve target, set its 0x2
            // bit, and recurse when it has capacity headroom.
            WsStation& prod = stations[static_cast<std::size_t>(it.backIndex)];
            if (prod.reserveTarget >= s.reserveTarget)
                prod.reserveTarget = s.reserveTarget;
            prod.bits |= 0x2;
            if (4 * (prod.incoming + prod.stock) < prod.freeCap) {
                ReserveWorkstationItems(stations, static_cast<std::size_t>(it.backIndex),
                                        items, env, needs);
            }
        }
    }
    return 1;
}

} // namespace guild::ai

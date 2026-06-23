// ===========================================================================
// MeisterAi request-command builders + AiAction handlers/dispatch — impl.
// 1:1 translations of the packet-construction and routing logic. See header for
// addresses, prototypes, and the coupled-leaf (inert hook) list.
// ===========================================================================
#include "aiaction_dispatch_ai_recon2.h"

namespace guild::sim {

// --- MeisterAi request-command packet builders ----------------------------

RequestPacket BuildRequestCmd58(int objectId) {
    // 0x4c9330: v3=58; v4=obj; v5=-1; v6=1; QueueRequestSlotReset28.
    RequestPacket pkt;
    pkt.cmdId    = 58;
    pkt.objectId = objectId;
    pkt.sentinel = -1;
    pkt.mode     = 1;
    pkt.hasTime  = false;
    return pkt;
}

RequestPacket BuildRequestCmd59(int objectId, const GameTimeStamp& now,
                                bool existingHandler, bool* freedExisting) {
    // 0x4c937c: if (FindFirstHandlerByFilter(1,0,59)) FreeHandlerEntry(...);
    //           v7=59; v8=obj; v9=-1; v13=1; v10=time; QueueRequestSlotReset28.
    if (freedExisting)
        *freedExisting = existingHandler;
    RequestPacket pkt;
    pkt.cmdId    = 59;
    pkt.objectId = objectId;
    pkt.sentinel = -1;
    pkt.mode     = 1;
    pkt.hasTime  = true;
    pkt.time     = now;
    return pkt;
}

RequestPacket BuildRequestCmd115(int objectId, const GameTimeStamp& now,
                                 bool existingHandler, bool* freedExisting) {
    // 0x4c93f4: if (FindFirstHandlerByFilter(1,0,115)) FreeHandlerEntry(...);
    //           v7=115; v8=*(dword_6498E4+4); v9=-1; v13=1; v10=time; emit.
    if (freedExisting)
        *freedExisting = existingHandler;
    RequestPacket pkt;
    pkt.cmdId    = 115;
    pkt.objectId = objectId;
    pkt.sentinel = -1;
    pkt.mode     = 1;
    pkt.hasTime  = true;
    pkt.time     = now;
    return pkt;
}

bool BuildRequestCmd109(int objectId, const GameTimeStamp& now,
                        bool existingHandler, RequestPacket* out) {
    // 0x4c7164: result = FindFirstHandlerByFilter(1,0,109);
    //           if (!result) { ... v4=109; v5=obj; v6=-1; v10=2; v7=time; emit; }
    //           return result;   // i.e. emit ONLY when no handler exists.
    if (existingHandler)
        return false;
    RequestPacket pkt;
    pkt.cmdId    = 109;
    pkt.objectId = objectId;
    pkt.sentinel = -1;
    pkt.mode     = 2;
    pkt.hasTime  = true;
    pkt.time     = now;
    if (out)
        *out = pkt;
    return true;
}

// --- AiAction handlers -----------------------------------------------------

int HandleGuildhallTrigger(const ObjPair& p, int flaggedWorth, int* upgradeAmt,
                           bool* didUpgrade) {
    if (didUpgrade)
        *didUpgrade = false;
    // 0x475c74:
    //   if (a2.type==1 && a3.type==18 && a3.has8) { resolve; EnqueueCmd15; return 57; }
    if (p.aType == 1 && p.bType == 18 && p.bHas8 != 0) {
        return 57;
    }
    //   if (a2.type != 4) return 0;
    if (p.aType != 4)
        return 0;
    //   if (a3.type != 1) return 0;
    if (p.bType != 1)
        return 0;
    // upgrade path: v24 = SumFlaggedSlotsWorth(...); amt = trunc(v24 * 0.3).
    int amt = static_cast<int>(static_cast<double>(flaggedWorth) *
                               static_cast<double>(kGuildhallWorthScale));
    if (upgradeAmt)
        *upgradeAmt = amt;
    if (didUpgrade)
        *didUpgrade = true;
    return 57;
}

int HandleObjectType23(u8 aType, u8 bType) {
    // 0x476140: if (*a2 != 7 || *a3 != 23) return 0; ... return 58;
    if (aType != 7 || bType != 23)
        return 0;
    return 58;
}

int HandleObjectType14(u8 aType, u8 bType, bool hasPersonRecord) {
    // 0x476440: if (*a1 != 7) return 0; if (*a2 != 14) return 0;
    //           if (!FindRecordById(0)) return 0; ... return 59;
    if (aType != 7)
        return 0;
    if (bType != 14)
        return 0;
    if (!hasPersonRecord)
        return 0;
    return 59;
}

// --- DispatchSecondarySearch routing ---------------------------------------

SearchFinder FinderForType(int typeCode) {
    switch (typeCode) {
        case 0: return SearchFinder::FactionPerson;
        case 3: return SearchFinder::AdjacentEntityLarge;
        case 4: return SearchFinder::EligibleNeighbor;
        case 7: return SearchFinder::NearbyBuilding;
        case 8: return SearchFinder::NearbyWealthyTarget;
        default: return SearchFinder::None;
    }
}

SearchRoute RouteSecondarySearch(const std::array<int, 3>& searchTypes,
                                 bool hasFollowHandler,
                                 const std::function<int(int, int)>& probe,
                                 const std::function<int()>& rollCoin,
                                 const std::function<int(int)>& rollIndex) {
    // 0x47cc68 (a3.type == 0 fresh-search branch):
    //   v24[0]=1; for j in 0..2: if (probe(searchTypes[j],1) & 2) push to list1.
    std::vector<int> list1;
    for (int j = 0; j < 3; ++j) {
        if (probe(searchTypes[static_cast<size_t>(j)], 1) & 2)
            list1.push_back(searchTypes[static_cast<size_t>(j)]);
    }
    //   if (RecordById/follow-handler) { v24[0]=2; for j: if(probe(.,2)&2) push list2. }
    std::vector<int> list2;
    if (hasFollowHandler) {
        for (int j = 0; j < 3; ++j) {
            if (probe(searchTypes[static_cast<size_t>(j)], 2) & 2)
                list2.push_back(searchTypes[static_cast<size_t>(j)]);
        }
    }
    SearchRoute route;
    auto pickFromList1 = [&]() {
        int idx = rollIndex(static_cast<int>(list1.size()));
        // Guard: rollIndex models RandomModulo(count) (always in [0,count)); clamp
        // defensively so a degenerate hook cannot index past the collected list.
        if (idx < 0 || idx >= static_cast<int>(list1.size()))
            return;
        int code = list1[static_cast<size_t>(idx)];
        // probe again at mode 1 to confirm (the binary re-probes; bit 1 = ok).
        if (probe(code, 1) & 2) {
            route.finder = FinderForType(code);
            route.typeCode = code;
            route.mode = 1;
        }
    };
    auto pickFromList2 = [&]() {
        int idx = rollIndex(static_cast<int>(list2.size()));
        // Guard: see pickFromList1 — clamp the modeled RandomModulo index.
        if (idx < 0 || idx >= static_cast<int>(list2.size()))
            return;
        int code = list2[static_cast<size_t>(idx)];
        if (probe(code, 2) & 2) {
            route.finder = FinderForType(code);
            route.typeCode = code;
            route.mode = 2;
        }
    };
    if (!list1.empty() && !list2.empty()) {
        // if (!RandomModulo(2)) pick list1 else pick list2.
        if (rollCoin() == 0)
            pickFromList1();
        else
            pickFromList2();
    } else if (!list1.empty()) {
        pickFromList1();
    } else if (!list2.empty()) {
        pickFromList2();
    }
    return route;
}

} // namespace guild::sim

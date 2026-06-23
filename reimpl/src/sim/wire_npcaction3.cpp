// See wire_npcaction3.h. Binds the three late-batch NPC bridges (NpcAction11Hooks,
// NpcAction12Hooks, NpcMarketHooks) to their real reconstructed cross-cluster
// leaves. Glue only — no module logic.
//
// All handler free / find leaves operate on the SAME shared real HandlerTable that
// real_hooks3 owns (RealHandlerTable()); all command emits stage onto the SAME
// shared real CommandQueue (RealCommandQueue()). he.h's HeRecord and
// handler_entry.h's HandlerRecord are raw POD blobs over the same record-base byte
// layout; the reinterpret_casts between them are byte-faithful, exactly as
// real_hooks3 / wire_charaction already cast HeRecord*<->HandlerRecord*.
#include "sim/wire_npcaction3.h"

#include "sim/npcaction11.h"   // NpcAction11Hooks / SetNpcAction11Hooks / Get...
#include "sim/npcaction12.h"   // NpcAction12Hooks / SetNpcAction12Hooks / Get...
#include "sim/npc_market.h"    // NpcMarketHooks  / SetNpcMarketHooks  / Get...

#include "sim/real_hooks.h"        // RealCommandQueue()
#include "sim/real_hooks3.h"       // RealHandlerTable()
#include "sim/handler_entry.h"     // HandlerTable / HandlerRecord
#include "sim/entity.h"            // PersonFindRecordById / GameObjectResolveEntityById
#include "sim/command_codec.h"     // QueueRequestArgs25 / QueueRequestCoord27 / QueueRequest17
#include "sim/command_builders.h"  // QueueRequestEntity29
#include "sim/command_builders2.h" // QueueRequestSingle58 / QueueRequestSingle59
#include "sim/command_builders3.h" // RequestBuildOp71 / RequestBuildOp72
#include "sim/command_apply7.h"    // RequestBuildOp77 / QueueRequestMixed44
#include "sim/command_inherit.h"   // EnqueueCmd15
#include "sim/building_stock.h"    // Building_SumFlaggedSlotsWorth
#include "sim/building_production.h"// Building_ComputeSlotYield
#include "sim/building_type.h"     // BuildingType_GroupFromCode / ...ComputeRankWithinGroup
#include "app/session_init.h"      // MoneyMultiplyByRate
#include "util/math_random.h"      // util::RandomModulo
#include "util/util_misc.h"        // util::InitAndShuffleDwordArray

namespace guild::sim {

namespace {

CommandQueue&  Q()   { return *RealCommandQueue(); }
HandlerTable&  HeT() { return *RealHandlerTable(); }

// =========================================================================
// He pool leaves -> the shared real HandlerTable.
// =========================================================================

// VIBE_He_FreeHandlerEntry(record). Hook field shape: i32 (*)(HeRecord*).
i32 WnFreeHandlerEntry(HeRecord* h) {
    return HeT().FreeHandlerEntry(reinterpret_cast<HandlerRecord*>(h));
}

// Raw record-byte readers for the conflict-scan +172/+180 compares. The records
// are 332-byte HandlerRecord blobs; the originals address +172 (43*4) and +180
// (45*4) by explicit byte offset off the record base.
i32 RawField(const void* rec, int byteOff) {
    i32 v;
    __builtin_memcpy(&v, static_cast<const u8*>(rec) + byteOff, sizeof(v));
    return v;
}

// NpcAction11.findConflictingHandler(self, filter, target180, target172):
// VIBE_He_FindFirstHandlerByFilter(1, 0, filter) — selector 0 (kind) == filter —
// then a FindNext loop returning the first OTHER record whose +180 matches
// target180 OR +172 matches target172. 1:1 with the CheckTargetBusyState probe.
void* WnFindConflictingHandler11(HeRecord* self, int filter,
                                 i32 target180, i32 target172) {
    for (HandlerRecord* r = HeT().FindFirstHandlerByFilter(1, /*sel*/0, filter);
         r; r = HeT().FindNextMatchingHandler()) {
        if (reinterpret_cast<HeRecord*>(r) == self) continue;
        if (RawField(r, 180) == target180 || RawField(r, 172) == target172)
            return r;
    }
    return nullptr;
}

// NpcAction12.findConflictingHandler(self, filter, target172): same scan, +172 only.
void* WnFindConflictingHandler12(HeRecord* self, int filter, i32 target172) {
    for (HandlerRecord* r = HeT().FindFirstHandlerByFilter(1, /*sel*/0, filter);
         r; r = HeT().FindNextMatchingHandler()) {
        if (reinterpret_cast<HeRecord*>(r) == self) continue;
        if (RawField(r, 172) == target172) return r;
    }
    return nullptr;
}

// NpcAction12.scanFilterHasForeignMatch(self, filter): true iff ANY filter-`filter`
// record other than self exists (the BeginScanType50 "self is the only match" gate).
bool WnScanFilterHasForeignMatch(HeRecord* self, int filter) {
    for (HandlerRecord* r = HeT().FindFirstHandlerByFilter(1, /*sel*/0, filter);
         r; r = HeT().FindNextMatchingHandler()) {
        if (reinterpret_cast<HeRecord*>(r) != self) return true;
    }
    return false;
}

// =========================================================================
// id -> record resolves (entity.h linear scans).
// =========================================================================

// VIBE_Person_FindRecordById(id) -> Person* (or null).
void* WnFindPersonById(i32 id) {
    return reinterpret_cast<void*>(PersonFindRecordById(id));
}

// VIBE_GameObject_ResolveEntityById(id, out): writes the resolved Object base
// (else scene node, else null) into *out — mirrors the search order the originals
// use (Object/Building then scene).
void WnResolveEntity(i32 id, void** out) {
    if (!out) return;
    ObjectRec* obj = nullptr;
    SceneNode* scene = nullptr;
    GameObjectResolveEntityById(&obj, &scene, id, /*outPerson=*/nullptr);
    if (obj)        *out = reinterpret_cast<void*>(obj);
    else if (scene) *out = reinterpret_cast<void*>(scene);
    else            *out = nullptr;
}

// =========================================================================
// misc reconstructed scalar leaves.
// =========================================================================

u16 WnRandomModulo(u16 n) { return static_cast<u16>(util::RandomModulo(n)); }

// VIBE_Money_MultiplyByRate(amount, ratePct).
i32 WnMoneyMultiplyByRate(i32 amount, u8 cur) {
    return app::MoneyMultiplyByRate(amount, cur);
}

i32 WnBuildingSumFlaggedSlotsWorth(int group) {
    return Building_SumFlaggedSlotsWorth(static_cast<u8>(group));
}

i32 WnSumFlaggedSlotsWorth12(int group) {
    return Building_SumFlaggedSlotsWorth(static_cast<u8>(group));
}

// VIBE_Building_ComputeSlotYield(market, slot) — double-return, hook wants float.
float WnComputeSlotYield(int slot) {
    return static_cast<float>(Building_ComputeSlotYield(/*building=*/0, slot));
}

u8 WnGroupFromCode(u8 code) { return BuildingType_GroupFromCode(code); }

i32 WnComputeRankWithinGroup(u8 code) {
    return BuildingType_ComputeRankWithinGroup(code);
}

// VIBE_Util_InitAndShuffleDwordArray(count, arr).
void WnShuffleDwords(int n, i32* dst) {
    util::InitAndShuffleDwordArray(static_cast<u8>(n), reinterpret_cast<u32*>(dst));
}

// =========================================================================
// command emits -> real builders on the shared queue.
// =========================================================================

// QueueRequestArgs25(id, a, b, c, d).
void WnRequestArgs25(i32 id, int a, int b, int c, int d) {
    QueueRequestArgs25(Q(), id, a, b, c, d);
}

// QueueRequestCoord27(a, b, delta) — coordX/coordY land at 0 for these emits.
void WnRequestCoord27(i32 a, i32 b, int delta) {
    QueueRequestCoord27(Q(), a, b, delta, 0, 0);
}

// QueueRequest17(idA, idB, count, kind, cur, f).
void WnRequest17_11(i32 idA, i32 idB, int c, int d, u8 cur, int f) {
    QueueRequest17(Q(), idA, idB, c, static_cast<i16>(d), cur, f);
}
void WnRequest17_12(i32 idA, i32 idB, int count, int kind, u8 cur, int f) {
    QueueRequest17(Q(), idA, idB, count, static_cast<i16>(kind), cur, f);
}

void WnRequestSingle58(i32 id) { QueueRequestSingle58(Q(), id); }
void WnRequestSingle59(i32 id) { QueueRequestSingle59(Q(), id); }

void WnRequestBuildOp71(i32 id, int a, int b, int c) {
    RequestBuildOp71(Q(), id, a, b, c);
}
void WnRequestBuildOp72(i32 id, int amount) {
    RequestBuildOp72(Q(), id, static_cast<i8>(amount));
}
void WnRequestBuildOp77(i32 id) { RequestBuildOp77(Q(), id); }

// QueueRequestEntity29(arg, record).
void WnEnqueueCmd15_11(i32 idA, i32 idB, i32 amount, u8 cur) {
    EnqueueCmd15(Q(), idA, idB, amount, cur);
}
void WnEnqueueCmd15_12(i32 idA, i32 idB, long long amount, u8 cur) {
    EnqueueCmd15(Q(), idA, idB, static_cast<i32>(amount), cur);
}

// QueueRequestMixed44(a, b, c, d, e, f). Hook returns the request handle (i32).
i32 WnQueueRequestMixed44(i32 a, u8 b, u16 c, u8 d, int e, i32 f) {
    return QueueRequestMixed44(Q(), a, static_cast<i8>(b), static_cast<i16>(c),
                               static_cast<i8>(d), static_cast<i8>(e), f);
}

// NpcMarket.queueRequest17(from, to, qty, itemType, market) — the stall stock
// transfer; market index lands in the cmd17 a6 currency/extra slot (a5=0 host).
void WnMarketRequest17(i32 from, i32 to, i32 qty, i16 itemType, u8 market) {
    QueueRequest17(Q(), from, to, qty, itemType, /*cur*/0, /*f*/market);
}

// --- process-lifetime wired hook tables (the global hook ptrs reference these) ---
NpcAction11Hooks g_h11{};
NpcAction12Hooks g_h12{};
NpcMarketHooks   g_hmkt{};

} // namespace

void InstallRealNpcAction3Wiring() {
    HeT();   // force the shared real He pool to exist (composes with real_hooks3)
    Q();     // force the shared real command queue to exist

    // --- NpcAction11Hooks (npcaction11.h) ------------------------------------
    // Seed from the module default (zero-initialised inert) and override only the
    // wireable fields; unbound fields keep their inert default (the bridge
    // null-checks every hook before calling it).
    g_h11 = GetNpcAction11Hooks();
    g_h11.freeHandlerEntry              = &WnFreeHandlerEntry;
    g_h11.findConflictingHandler        = &WnFindConflictingHandler11;
    g_h11.findPersonById                = &WnFindPersonById;
    g_h11.resolveEntity                 = &WnResolveEntity;
    g_h11.randomModulo                  = &WnRandomModulo;
    g_h11.moneyMultiplyByRate           = &WnMoneyMultiplyByRate;
    g_h11.buildingSumFlaggedSlotsWorth  = &WnBuildingSumFlaggedSlotsWorth;
    g_h11.requestArgs25                 = &WnRequestArgs25;
    g_h11.requestCoord27                = &WnRequestCoord27;
    g_h11.request17                     = &WnRequest17_11;
    g_h11.requestSingle58               = &WnRequestSingle58;
    g_h11.requestBuildOp71              = &WnRequestBuildOp71;
    g_h11.requestBuildOp72              = &WnRequestBuildOp72;
    g_h11.requestBuildOp77              = &WnRequestBuildOp77;
    g_h11.enqueueCmd15                  = &WnEnqueueCmd15_11;
    // INERT (no clean reconstructed target — see wire_npcaction3.h):
    //   personQueryBegin / buildingFindById / buildingFindOfficeStorage /
    //   gameObjectQueryFind / requestSlotReset28 / requestNamedObject53 /
    //   requestState22/23 / beginDelta / appendCopiedField / appendRawField /
    //   buildingActionStart/End / aiLoadBuildingGraphic / sendEntity / sendQuickjump
    //   / runOfficeOverviewWindow / relationLookup / adjustMood / evaluateViolation
    //   / selectBestRecursive / tryRangedAttack / fastMode / the record-field readers
    //   (objId/markerWord/kind/rank/equipFlags/setEquipFlags/field101/field364/
    //    familyDur/spouseId/personEquipState/field92) / the per-city grid arrays
    //   (cityId/cityMarker/cityKind/cityPersonRecord).
    SetNpcAction11Hooks(&g_h11);

    // --- NpcAction12Hooks (npcaction12.h) ------------------------------------
    g_h12 = GetNpcAction12Hooks();
    g_h12.freeHandlerEntry              = &WnFreeHandlerEntry;
    g_h12.findConflictingHandler        = &WnFindConflictingHandler12;
    g_h12.scanFilterHasForeignMatch     = &WnScanFilterHasForeignMatch;
    g_h12.findPersonById                = &WnFindPersonById;
    g_h12.resolveEntity                 = &WnResolveEntity;
    g_h12.randomModulo                  = &WnRandomModulo;
    g_h12.computeRankWithinGroup        = &WnComputeRankWithinGroup;
    g_h12.groupFromCode                 = &WnGroupFromCode;
    g_h12.sumFlaggedSlotsWorth          = &WnSumFlaggedSlotsWorth12;
    g_h12.shuffleDwords                 = &WnShuffleDwords;
    g_h12.requestCoord27                = &WnRequestCoord27;
    g_h12.request17                     = &WnRequest17_12;
    g_h12.requestSingle59               = &WnRequestSingle59;
    g_h12.requestArgs25                 = &WnRequestArgs25;
    g_h12.requestBuildOp77              = &WnRequestBuildOp77;
    g_h12.enqueueCmd15                  = &WnEnqueueCmd15_12;
    g_h12.queueRequestMixed44           = &WnQueueRequestMixed44;
    // INERT: personQueryBegin / buildingFindOfficeStorage / buildingFindWorkProduct
    //   / gameObjectQueryFind / gameObjectIterFirst / gameObjectIterNext /
    //   amtFindRecordByKey / relationLookup / computeOfficeRank / computeTotalWealth
    //   / lookupMarketPrice / requestNamedObject53 / requestBuildOp84 /
    //   requestSlotReset28 / buildingActionStart/End / combatPickActiveTarget /
    //   aiLoadBuildingGraphic / sendEntity / sendQuickjump / panelShowAlliance /
    //   eventPanelCreate/Destroy / playExamVoice / renderExamResult /
    //   dialogOpenBuilding / changePlayerAction / createSoundAction /
    //   animalBuildWanderPath / heightmapWorldToTile / charActionInsert /
    //   gateHairGesture / tryRangedAttack / selectBestRecursive / the record-field
    //   readers (objId/markerWord/kind/equipState/field) / the per-city grid arrays
    //   (cityMarker/cityKind/cityId/cityIdShifted/cityAllianceEligible/cityRecord).
    SetNpcAction12Hooks(&g_h12);

    // --- NpcMarketHooks (npc_market.h) ---------------------------------------
    g_hmkt = GetNpcMarketHooks();
    g_hmkt.freeHandlerEntry = &WnFreeHandlerEntry;
    g_hmkt.computeYield     = &WnComputeSlotYield;
    g_hmkt.queueRequest17   = &WnMarketRequest17;
    SetNpcMarketHooks(&g_hmkt);
    // INERT: marketEnabled (dword_6477A4) / marketIndex (byte_6477A1) / treasury
    //   (Person_GetCurrencyAmount) / enqueueTopUp / slotCount / slot / commitSlot /
    //   slotIsActiveWorkable / effectiveStock (Inventory_GetEffectiveStock not
    //   reconstructed) / queueTransform64 (PendingState staging) / worldSyncSuppressed
    //   (word_63C740): process-global market state with no standalone reconstructed
    //   leaf. The pricing RULE (MarketRecomputeSlot) has its own golden tests.
}

} // namespace guild::sim

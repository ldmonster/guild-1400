// See wire_npcaction2.h. Binds the three NpcAction leaf bridges (NpcAction8/9/10
// Hooks) onto their real reconstructed siblings. Glue only — no module logic.
//
// SEED-FROM-DEFAULTS: each table is reset to its module's COMPLETE inert default
// (SetNpcActionNHooks(nullptr)) and copied out (GetNpcActionNHooks()) BEFORE any
// override, so the many fields with no clean real target keep their faithful inert
// stubs and the evaluators/coroutines (which already null-check every hook) behave
// exactly as before for the unbound fields.
//
// All command-emit bindings stage onto the SAME shared real CommandQueue the
// CharAction / real_hooks waves use (RealCommandQueue()); all He-pool bindings
// operate on the ONE shared HandlerTable (RealHandlerTable()). HandlerRecord and
// HeRecord are raw POD blobs over the same record-base byte layout, so the
// reinterpret_casts between them are byte-faithful (exactly as real_hooks3 / the
// CharAction wiring already cast HeRecord*<->HandlerRecord*).
#include "sim/wire_npcaction2.h"

#include "sim/npcaction8.h"        // NpcAction8Hooks / Set/GetNpcAction8Hooks
#include "sim/npcaction9.h"        // NpcAction9Hooks / Set/GetNpcAction9Hooks
#include "sim/npcaction10.h"       // NpcAction10Hooks / Set/GetNpcAction10Hooks
#include "sim/he.h"                // HeRecord

#include "sim/real_hooks.h"        // RealCommandQueue()
#include "sim/real_hooks3.h"       // RealHandlerTable()
#include "sim/handler_entry.h"     // HandlerTable / HandlerRecord
#include "sim/command_builders.h"  // QueueRequestEntity29
#include "sim/command_builders3.h" // RequestBuildOp71
#include "sim/command_apply7.h"    // RequestBuildOp77 / RequestBuildOp90
#include "sim/command_codec.h"     // QueueRequest16 / QueueRequest17 / QueueRequestCoord27
#include "sim/command.h"           // CommandQueue::GetPacketStatusById
#include "sim/building_type.h"     // BuildingType_ComputeRankWithinGroup
#include "util/math_random.h"      // util::RandomModulo
#include "util/math_rng_float.h"   // util::RandomFloatScaled

namespace guild::sim {

namespace {

CommandQueue&  Q()   { return *RealCommandQueue(); }
HandlerTable&  HeT() { return *RealHandlerTable(); }

// =========================================================================
// RNG (all three bridges). The bridges name VIBE_Math_RandomModulo @0x58b89c and
// VIBE_Math_RandomFloatScaled @0x58b910 directly.
// =========================================================================

// u16 (*randomModulo)(u16 n) in npcaction8/9. util::RandomModulo returns int but
// the original's result is < n (a small count), so the narrowing is value-faithful.
u16 WnRandomModulo(u16 n) {
    return static_cast<u16>(util::RandomModulo(n));
}

// float (*randomFloatScaled)() in npcaction9. The real leaf returns a double in
// [0,1); the original stores it into a float register here (the .cpp reads it as a
// float), so the narrowing is exactly the original's fld/fstp.
float WnRandomFloatScaled() {
    return static_cast<float>(util::RandomFloatScaled());
}

// =========================================================================
// building-type table (npcaction8/9). int(*)(int typeCode) -> the u8 leaf.
// =========================================================================
int WnBuildingRankWithinGroup(int typeCode) {
    return BuildingType_ComputeRankWithinGroup(static_cast<u8>(typeCode));
}

// =========================================================================
// He handler pool (npcaction9/10) -> the shared real HandlerTable.
// =========================================================================

// npcaction9: const u8* (*)(int a,int b,int c,int d,int e) maps onto
// VIBE_He_FindFirstHandlerByFilter(count=a, sel0=b,val0=c, sel1=d,val1=e). The
// single call site passes (2,0,24,2,record[0]) — count 2, two (sel,val) pairs.
const u8* Wn9FindFirstByFilter(int a, int b, int c, int d, int e) {
    return reinterpret_cast<const u8*>(
        HeT().FindFirstHandlerByFilter(a, b, c, d, e));
}
const u8* Wn9FindNextMatching() {
    return reinterpret_cast<const u8*>(HeT().FindNextMatchingHandler());
}

// npcaction10: i32 (*)(HeRecord*) -> FreeHandlerEntry(HandlerRecord*).
i32 Wn10FreeHandlerEntry(HeRecord* h) {
    return HeT().FreeHandlerEntry(reinterpret_cast<HandlerRecord*>(h));
}

// =========================================================================
// command emits (npcaction8/10) -> real builders on the shared queue.
// =========================================================================

// npcaction8: void(*queueRequest17)(i32 idA, i32 idB, int count, int kind,
//   u8 currency, long long price). The real op-17 builder takes
//   (a1,a2,a3,a4(word),a5(byte),a6). The original packs count@+0x14 as a2-ish; we
//   forward exactly as the bridge documents: idA=a1, idB=a2, count=a3, kind=a4(word),
//   currency=a5(byte), price=a6.
void Wn8QueueRequest17(i32 idA, i32 idB, int count, int kind, u8 currency,
                       long long price) {
    QueueRequest17(Q(), idA, idB, count, static_cast<i16>(kind), currency,
                   static_cast<i32>(price));
}

// npcaction10 packet ACK gate. i32(*)(i32 handle) -> GetPacketStatusById(u32).
i32 Wn10PacketStatus(i32 handle) {
    return Q().GetPacketStatusById(static_cast<u32>(handle));
}

// npcaction10: i32(*queueEntity29)(int arg, HeRecord* h) -> opcode-29 builder.
i32 Wn10QueueEntity29(int arg, HeRecord* h) {
    return QueueRequestEntity29(Q(), static_cast<i8>(arg), h);
}

// npcaction10 command builders (the upstream id args come from inert record
// readers => 0 when absent; the BUILDER itself is the real reconstruction).
void Wn10Request16(i32 fromId, i32 toId, i32 amt) {
    QueueRequest16(Q(), fromId, toId, amt, 0);
}
void Wn10RequestCoord27(i32 a, i32 b, int delta) {
    QueueRequestCoord27(Q(), a, b, delta, 0, 0);
}
void Wn10RequestBuildOp71(i32 id) {
    RequestBuildOp71(Q(), id, 0, 0, 0);
}
void Wn10RequestBuildOp77(i32 id) {
    RequestBuildOp77(Q(), id);
}
void Wn10RequestBuildOp90(int a, i32 cityId) {
    RequestBuildOp90(Q(), a, cityId);
}
void Wn10RequestBuildOp91(i32 id, int kind) {
    RequestBuildOp91(Q(), id, kind, 0, 0);
}

// --- process-lifetime wired hook tables (the global hook ptrs reference these) ---
NpcAction8Hooks  g_h8{};
NpcAction9Hooks  g_h9{};
NpcAction10Hooks g_h10{};

} // namespace

void InstallRealNpcAction2Wiring() {
    HeT();   // force the shared real He pool to exist (composes with real_hooks3)
    Q();     // force the shared real command queue to exist

    // --- NpcAction8Hooks (npcaction8.h) --------------------------------------
    SetNpcAction8Hooks(nullptr);            // = complete inert default
    g_h8 = GetNpcAction8Hooks();
    g_h8.randomModulo            = &WnRandomModulo;
    g_h8.buildingRankWithinGroup = &WnBuildingRankWithinGroup;
    g_h8.queueRequest17          = &Wn8QueueRequest17;
    // scoreDistance/scoreWeighted/scoreOwn (ai env relation kernels) /
    // getLawRecord / dispatchPrimarySearch / dispatchSecondarySearch /
    // loadDemandSnapshot / buildingCategoryForObject / findRivalToConfront /
    // shopStateWord / gameObjectQueryFind / inventoryFindSlotByItemId /
    // itemUseObjectAction / lookupCachedMarketPrice / currencyByte: no clean
    // byte-faithful free-leaf target (env-hook planners / process-globals /
    // unreconstructed) -> inert (documented in wire_npcaction2.h).
    SetNpcAction8Hooks(&g_h8);

    // --- NpcAction9Hooks (npcaction9.h) --------------------------------------
    SetNpcAction9Hooks(nullptr);
    g_h9 = GetNpcAction9Hooks();
    g_h9.randomModulo              = &WnRandomModulo;
    g_h9.randomFloatScaled         = &WnRandomFloatScaled;
    g_h9.buildingRankWithinGroup   = &WnBuildingRankWithinGroup;
    g_h9.heFindFirstHandlerByFilter = &Wn9FindFirstByFilter;
    g_h9.heFindNextMatchingHandler  = &Wn9FindNextMatching;
    // gameObjectQueryFind / resolveEntityById / sumValuesAtLocation /
    // selectBestRecursive / evalMeisterTarget / tryGroupAttack / findRivalToConfront
    // / findOpponentBuilding / countInventoryMatch / findNearestEntity /
    // findMatchingColors / personFindRecordById / personGetFamilyRecord /
    // personQueryBegin / personIterNext / personQueryByGoodType / personCurrencyAmount
    // / personTotalWealth / computePersonFavorability / loadDemandSnapshot /
    // lookupCachedMarketPrice / buildingSumFlaggedSlotsWorth / buildingCategoryForObject
    // / buildingFindActiveWorkSlot / buildingFindOfficeStorage / buildingRatingCurveA /
    // inventoryEffectiveStock / straftatCountActiveByTarget / heSumPlayerHandlerValues
    // (needs unported He entity table) / amtCheckGuildRankLevel2 / currencyByte /
    // clockLow / clockWord2: ai env-hook planners, ambiguous polymorphic-record
    // readers, env-virtual wealth, or unreconstructed leaves -> inert (documented).
    SetNpcAction9Hooks(&g_h9);

    // --- NpcAction10Hooks (npcaction10.h) ------------------------------------
    SetNpcAction10Hooks(nullptr);
    g_h10 = GetNpcAction10Hooks();
    g_h10.packetStatus       = &Wn10PacketStatus;
    g_h10.queueEntity29      = &Wn10QueueEntity29;
    g_h10.freeHandlerEntry   = &Wn10FreeHandlerEntry;
    g_h10.queueRequest16     = &Wn10Request16;
    g_h10.requestCoord27     = &Wn10RequestCoord27;
    g_h10.requestBuildOp71   = &Wn10RequestBuildOp71;
    g_h10.requestBuildOp77   = &Wn10RequestBuildOp77;
    g_h10.requestBuildOp90   = &Wn10RequestBuildOp90;
    g_h10.requestBuildOp91   = &Wn10RequestBuildOp91;
    // countHandlers / anyHandlerMatchesEntity (filter-fold helpers over the pool,
    // no single-leaf form) / findPersonById / personQueryBegin / resolveEntity /
    // gameObjectQueryFind / personFindActive / familyRecord / objId / markerWord /
    // kind / recRank / hasCharacter / cityPersonRecord / cityId / cityAuxRecord
    // (polymorphic record readers: Person id@+4 vs Object id@+1 — no faithful
    // single offset, same ambiguity real_hooks3 leaves inert for NpcAction3/4) /
    // sumCurrencyHeld / currencyAmount / computeTotalWealth (env-virtual) /
    // distributeCredit / requestNamedObject53(name) / queueSlotReset28 (real op-28
    // builder takes a PendingState+blob, not this flat 4-int form) / setEntityFieldM1
    // / enqueueBuildOp84 / sendMessage / playSample / eventPanelCreate/Destroy /
    // renderRichString / panelWindow / formEventMatches / formEventCode /
    // historyWanderA/B/Pair / evalGroupComposition / mapActionToCategory /
    // computeVariantIndex / mapToProfessionCode / buildingSlotsWorth /
    // evalMeisterTarget / buildingRating / registerApEvent / withinTolerance /
    // relationEntry / pickCarryTarget / inventorySlotActive / evaluateViolation /
    // groupMemberCount / groupMemberRecord: ambiguous record readers, env/UI/audio/
    // history renders, ai planners, or unreconstructed leaves -> inert (documented).
    SetNpcAction10Hooks(&g_h10);
}

} // namespace guild::sim

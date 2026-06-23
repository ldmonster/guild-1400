// See wire_charaction.h. Binds the five CharAction step / NpcAction-recon bridges
// to their real reconstructed cross-cluster leaves. Glue only — no module logic.
//
// All free/find leaves operate on the SAME shared real He/HandlerEntry pool that
// real_hooks3 owns (RealHandlerTable()); all command emits stage onto the SAME
// shared real CommandQueue (RealCommandQueue()). he.h's HeRecord, handler_entry.h's
// HandlerRecord, and types.h's Person/ObjectRec are all raw POD blobs over the same
// record-base byte layout (kind@+0, id@+4, …); the reinterpret_casts between them
// are byte-faithful, exactly as real_hooks3 already casts HeRecord*<->HandlerRecord*.
#include "sim/wire_charaction.h"

#include "sim/charaction_steps2.h"            // CharActionStep2Hooks / SetCharActionStep2Hooks
#include "sim/charaction_steps3.h"            // CharActionStep3Hooks / ...
#include "sim/charaction_steps4.h"            // CharActionStep4Hooks / ...
#include "sim/charaction_npcaction_recon.h"   // CharActionReconHooks / ...
#include "sim/charaction_npcaction_recon2.h"  // CharActionRecon2Hooks / ...

#include "sim/real_hooks.h"        // RealCommandQueue()
#include "sim/real_hooks3.h"       // RealHandlerTable()
#include "sim/handler_entry.h"     // HandlerTable / HandlerRecord
#include "sim/entity.h"            // PersonFindRecordById / GameObjectResolveEntityById
#include "sim/building_type.h"     // Building_MapKindToCategory
#include "sim/command_builders.h"  // QueueRequestEntity29
#include "sim/command_builders2.h" // QueueRequestQuad60
#include "sim/command_codec.h"     // QueueRequestArgs25 / QueueRequestCoord27
#include "util/math_random.h"      // util::RandomModulo

namespace guild::sim {

namespace {

CommandQueue&  Q()   { return *RealCommandQueue(); }
HandlerTable&  HeT() { return *RealHandlerTable(); }

// =========================================================================
// He pool leaves -> the shared real HandlerTable.
// =========================================================================

// VIBE_He_FreeHandlerEntry(record). Hook field shape: i32 (*)(HeRecord*).
i32 WcFreeHandlerEntry(HeRecord* h) {
    return HeT().FreeHandlerEntry(reinterpret_cast<HandlerRecord*>(h));
}

// --- find-by-filter adapters (one per bridge signature) ------------------
// VIBE_He_FindFirstHandlerByFilter is varargs: (count, sel0,val0, sel1,val1, …)
// with selector 0 => kind@+0, 1 => id@+4, 2 => index@+8, 3 => field@+16. Each
// bridge folds its own filter shape; the adapters reproduce the exact filter the
// originals pass (documented in each bridge header / .cpp).

// charaction_steps2/3: findFirstByFilter(count, selectors[], values[]).
HeRecord* WcFindFirstByFilterArr(int count, const int* sel, const int* val) {
    // Faithfully replay up to 4 (selector,value) pairs (the originals never pass
    // more) into the varargs filter; missing trailing pairs are simply not emitted.
    HandlerRecord* r = nullptr;
    switch (count) {
        case 0: r = HeT().FindFirstHandlerByFilter(0); break;
        case 1: r = HeT().FindFirstHandlerByFilter(1, sel[0], val[0]); break;
        case 2: r = HeT().FindFirstHandlerByFilter(2, sel[0], val[0], sel[1], val[1]); break;
        case 3: r = HeT().FindFirstHandlerByFilter(3, sel[0], val[0], sel[1], val[1],
                                                   sel[2], val[2]); break;
        default: r = HeT().FindFirstHandlerByFilter(4, sel[0], val[0], sel[1], val[1],
                                                    sel[2], val[2], sel[3], val[3]); break;
    }
    return reinterpret_cast<HeRecord*>(r);
}

// charaction_steps4: findFirstByFilter(sel, val) — a single (selector,value) pair.
HeRecord* WcFindFirstByFilter1(int sel, int val) {
    return reinterpret_cast<HeRecord*>(HeT().FindFirstHandlerByFilter(1, sel, val));
}

// charaction_npcaction_recon: findFirstByFilter(a, b, filter) maps to the original
// VIBE_He_FindFirstHandlerByFilter(a, b, filter) — i.e. ONE pair (selector=b,
// value=filter); `a` is the original's leading filter count (== 1 for these scans).
HeRecord* WcFindFirstByFilterRecon(int /*a*/, int b, i32 filter) {
    return reinterpret_cast<HeRecord*>(HeT().FindFirstHandlerByFilter(1, b, filter));
}

// charaction_npcaction_recon2 (DrinkInit): findFirstByFilter(2, 2, row) maps to the
// original VIBE_He_FindFirstHandlerByFilter(2, 2, row, 0, 96) — two pairs:
// (index@+8 == row) AND (kind == 96). The hook only carries `row`; the constants
// 2/2/0/96 are fixed in the original call site (see recon2.cpp).
HeRecord* WcFindFirstByFilterRecon2(int /*a*/, int /*b*/, i32 row) {
    return reinterpret_cast<HeRecord*>(
        HeT().FindFirstHandlerByFilter(2, /*sel*/2, /*val*/static_cast<int>(row),
                                       /*sel*/0, /*val*/96));
}

HeRecord* WcFindNextMatching() {
    return reinterpret_cast<HeRecord*>(HeT().FindNextMatchingHandler());
}

// =========================================================================
// id -> record resolves (entity.h linear scans). Person/ObjectRec share the
// record base with HeRecord; the casts are byte-faithful.
// =========================================================================

// VIBE_Person_FindRecordById(id) -> Person* (or null).
HeRecord* WcFindPersonById(i32 id) {
    return reinterpret_cast<HeRecord*>(PersonFindRecordById(id));
}

// VIBE_GameObject_ResolveEntityById(...) returning the resolved record pointer.
// The originals these bridges model pass the OBJECT out-slot (search Object/Building
// then scene); we mirror that resolve order and surface the resolved record.
HeRecord* WcResolveEntityById(i32 id) {
    ObjectRec* obj = nullptr;
    SceneNode* scene = nullptr;
    GameObjectResolveEntityById(&obj, &scene, id, /*outPerson=*/nullptr);
    if (obj)   return reinterpret_cast<HeRecord*>(obj);
    if (scene) return reinterpret_cast<HeRecord*>(scene);
    return nullptr;
}

// charaction_npcaction_recon: resolveEntityById(out, id) — out-parameter form
// (the original writes the resolved Object base into *out, else null).
void WcResolveEntityByIdOut(HeRecord** out, i32 id) {
    if (out) *out = WcResolveEntityById(id);
}

// =========================================================================
// misc reconstructed leaves.
// =========================================================================

// VIBE_Math_RandomModulo(n) — uniform draw in [0, n).
int WcRandomModulo(int n) {
    return util::RandomModulo(static_cast<u16>(n));
}

// VIBE_Building_MapTypeToCategory(typeByte) — table-driven kind->category.
int WcBuildingCategory(u8 typeByte) {
    return Building_MapKindToCategory(typeByte);
}

// =========================================================================
// command emits -> real builders on the shared queue.
// =========================================================================

// VIBE_Command_QueueRequestEntity29(arg, record).
i32 WcQueueRequestEntity29(int arg, HeRecord* h) {
    return QueueRequestEntity29(Q(), static_cast<i8>(arg), h);
}

// charaction_npcaction_recon: queueRequestArgs25(id, off, val, sz, extra).
void WcReconQueueArgs25(i32 id, int off, int val, int sz, int extra) {
    QueueRequestArgs25(Q(), id, off, val, sz, extra);
}

// charaction_npcaction_recon: queueRequestCoord27(from, to, delta). The original
// passes coordX/coordY = 0 here (the move-toward delta lands in a3).
void WcReconQueueCoord27(i32 from, i32 to, int delta) {
    QueueRequestCoord27(Q(), from, to, delta, 0, 0);
}

// charaction_npcaction_recon: queueRequestQuad60(a, b, c, d) -> handle.
i32 WcReconQueueQuad60(i32 a, i32 b, int c, i32 d) {
    return QueueRequestQuad60(Q(), a, b, c, d);
}

// charaction_steps3: queueRequestArgs25(entityId, sel, mask, mode). The original's
// 5th arg (a5) is 0 for this cmd25 flavour.
void WcStep3QueueArgs25(i32 entityId, int sel, int mask, int mode) {
    QueueRequestArgs25(Q(), entityId, sel, mask, mode, 0);
}

// charaction_steps4: queueArgs25(entityId, sel, mask, mode, arg) — full 5-arg cmd25.
void WcStep4QueueArgs25(i32 entityId, int sel, int mask, int mode, int arg) {
    QueueRequestArgs25(Q(), entityId, sel, mask, mode, arg);
}

// charaction_steps4: queueCoord27(toId, fromId, value).
void WcStep4QueueCoord27(i32 toId, i32 fromId, int value) {
    QueueRequestCoord27(Q(), toId, fromId, value, 0, 0);
}

// --- process-lifetime wired hook tables (the global hook ptr references these) ---
CharActionStep2Hooks  g_step2{};
CharActionStep3Hooks  g_step3{};
CharActionStep4Hooks  g_step4{};
CharActionReconHooks  g_recon{};
CharActionRecon2Hooks g_recon2{};

} // namespace

void InstallRealCharActionWiring() {
    HeT();   // force the shared real He pool to exist (composes with real_hooks3)
    Q();     // force the shared real command queue to exist

    // --- CharActionStep2Hooks (charaction_steps2.h) --------------------------
    // Seed from the module's inert defaults (NON-null stubs) so the fields we don't
    // bind keep their safe stubs — several charaction step call sites invoke hooks
    // WITHOUT a null-check, so a zero-initialised table would crash.
    g_step2 = GetCharActionStep2Hooks();
    g_step2.findFirstByFilter = &WcFindFirstByFilterArr;
    g_step2.findNextMatching  = &WcFindNextMatching;
    g_step2.findPersonById    = &WcFindPersonById;
    // changePlayerAction / enqueueCmd15 (opcode 15) / registerApEvent /
    // resolveCityId: no clean reconstructed target -> inert (documented in .h).
    SetCharActionStep2Hooks(&g_step2);

    // --- CharActionStep3Hooks (charaction_steps3.h) --------------------------
    g_step3 = GetCharActionStep3Hooks();
    g_step3.resolveEntityById   = &WcResolveEntityById;
    g_step3.findPersonById      = &WcFindPersonById;
    g_step3.findFirstByFilter   = &WcFindFirstByFilterArr;
    g_step3.findNextMatching    = &WcFindNextMatching;
    g_step3.queueRequestArgs25  = &WcStep3QueueArgs25;
    // personQueryBegin (op/value pairing unknowable, rule 8) /
    // queueRequestGuardTarget61 / requestBuildOp73Sabotage / fastTimeEnabled
    // (dword_63C7B8) / targetActionMinutes / sendNotifyMessage (render): inert.
    SetCharActionStep3Hooks(&g_step3);

    // --- CharActionStep4Hooks (charaction_steps4.h) --------------------------
    g_step4 = GetCharActionStep4Hooks();
    g_step4.findPersonById    = &WcFindPersonById;
    g_step4.resolveEntityById = &WcResolveEntityById;
    g_step4.findFirstByFilter = &WcFindFirstByFilter1;
    g_step4.findNextMatching  = &WcFindNextMatching;
    g_step4.buildingCategory  = &WcBuildingCategory;
    g_step4.randomModulo      = &WcRandomModulo;
    g_step4.queueArgs25       = &WcStep4QueueArgs25;
    g_step4.queueCoord27      = &WcStep4QueueCoord27;
    // personQueryBegin/personIterNext (rule 8) / personWealth / cityWillingness /
    // cityCategory / cityPersonId (process-global tables) / adjustMood /
    // enqueueCmd15 / queueRequest16 / sendEntityMessage / sendQuickjumpMessage /
    // buildingQueueAll / highlightGuildMembers / queueRequest39 / queueSlotReset28 /
    // eventPanelCreate/Destroy / renderDialogLine / playVoice / buildPersonCard /
    // officeHolder / productionRating / dialogWindow / dialogResult: render/UI or
    // process-global leaves with no clean reconstructed target -> inert.
    SetCharActionStep4Hooks(&g_step4);

    // --- CharActionReconHooks (charaction_npcaction_recon.h) -----------------
    g_recon = GetCharActionReconHooks();
    g_recon.resolveEntityById    = &WcResolveEntityByIdOut;
    g_recon.findPersonById       = &WcFindPersonById;
    g_recon.findFirstByFilter    = &WcFindFirstByFilterRecon;
    g_recon.findNextMatching     = &WcFindNextMatching;
    g_recon.queueRequestEntity29 = &WcQueueRequestEntity29;
    g_recon.freeHandlerEntry     = &WcFreeHandlerEntry;
    g_recon.queueRequestQuad60   = &WcReconQueueQuad60;
    g_recon.queueRequestArgs25   = &WcReconQueueArgs25;
    g_recon.queueRequestCoord27  = &WcReconQueueCoord27;
    g_recon.randomModulo         = &WcRandomModulo;
    // objectQueryFind / personQueryBegin (rule 8) / packetStatus /
    // queueRequestMixed45 (opcode 45) / sendEntityMessage / sendQuickjumpMessage /
    // evaluateViolation (needs LawRecord state) / stampTimeAndRequest /
    // GuildJoin packet builders (beginDeltaPacket/appendRawField/queueRequestState22/
    // requestBuildOp72/buildingGroupFromCode/buildingGuildRankPair/
    // meisterRegisterApEvent) / RunOfficeCandidacy leaves (packetSeqById/
    // officeConfirmCandidacy/officeFindHighestVacantRank/officeRenderRequirementText):
    // no clean reconstructed target -> inert (documented).
    SetCharActionReconHooks(&g_recon);

    // --- CharActionRecon2Hooks (charaction_npcaction_recon2.h, DrinkInit) ----
    g_recon2 = GetCharActionRecon2Hooks();
    g_recon2.findFirstByFilter = &WcFindFirstByFilterRecon2;
    g_recon2.findNextMatching  = &WcFindNextMatching;
    g_recon2.freeHandlerEntry  = &WcFreeHandlerEntry;
    // statTableByte (byte_12CE990) / realTimeModeFlag (dword_63C7B8): process-global
    // tables not modeled as standalone callable leaves -> inert (default 0 / not
    // saturated / not real-time), exercising DrinkInit's faithful control flow.
    SetCharActionRecon2Hooks(&g_recon2);
}

} // namespace guild::sim

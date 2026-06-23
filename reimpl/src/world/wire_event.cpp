// See wire_event.h. Binds the four world-event He-action bridges (EventHooks /
// Event3Hooks / Event4Hooks / Event5Hooks) to their real reconstructed leaves.
// Glue only — no module logic.
//
// All He-pool leaves (free / find-by-filter / find-next) operate on the SAME
// shared real HandlerTable that sim::real_hooks3 owns (sim::RealHandlerTable());
// all command emits stage onto the SAME shared real CommandQueue
// (sim::RealCommandQueue()). he.h's HeRecord, handler_entry.h's HandlerRecord,
// entity.h's Person/ObjectRec are all raw POD blobs over the same record-base
// byte layout (id@+4, …); the reinterpret_casts between them are byte-faithful,
// exactly as sim::real_hooks3 already casts HeRecord*<->HandlerRecord* and as
// sim/wire_charaction.cpp casts Person*/ObjectRec*<->HeRecord*.
#include "world/wire_event.h"

#include "world/event2.h"   // EventHooks  / SetEventHooks / GetEventHooks
#include "world/event3.h"   // Event3Hooks / SetEvent3Hooks / GetEvent3Hooks
#include "world/event4.h"   // Event4Hooks / SetEvent4Hooks / GetEvent4Hooks
#include "world/event5.h"   // Event5Hooks / SetEvent5Hooks / GetEvent5Hooks

#include "sim/real_hooks.h"        // RealCommandQueue()
#include "sim/real_hooks3.h"       // RealHandlerTable() / InstallRealSimHooks3()
#include "sim/handler_entry.h"     // HandlerTable / HandlerRecord
#include "sim/entity.h"            // PersonFindRecordById / BuildingFindById / GameObjectResolveEntityById
#include "sim/command.h"           // CommandQueue
#include "sim/command_builders.h"  // QueueRequestEntity29 / QueueRequestSingle49 / QueueRequestPair33
#include "sim/command_codec.h"     // QueueRequest16/17 / QueueRequestArgs25 / QueueRequestCoord27 / EnqueueObjectInteraction
#include "util/math_random.h"      // util::RandomModulo (0x58b89c)

namespace guild::world {

namespace {

using guild::sim::HandlerRecord;
using guild::sim::HandlerTable;
using guild::sim::CommandQueue;
using guild::sim::ObjectRec;
using guild::sim::SceneNode;
using guild::sim::Person;

CommandQueue& Q()   { return *guild::sim::RealCommandQueue(); }
HandlerTable& HeT() { return *guild::sim::RealHandlerTable(); }

// ===========================================================================
// He pool leaves -> the shared real HandlerTable. HeRecord*<->HandlerRecord*
// is byte-faithful (same record base), exactly as real_hooks3 casts them.
// ===========================================================================

// VIBE_He_FreeHandlerEntry(record) @0x4c6144. Returns the original eax.
i32 WeFreeHandlerEntry(HeRecord* h) {
    return HeT().FreeHandlerEntry(reinterpret_cast<HandlerRecord*>(h));
}

// VIBE_He_FindNextMatchingHandler() @0x4c6278 — continue the active scan.
HeRecord* WeFindNextHandler() {
    return reinterpret_cast<HeRecord*>(HeT().FindNextMatchingHandler());
}

// event3 findFirstHandler(a, b, kind): the originals scan the live handler pool
// filtered by event kind. VIBE_He_FindFirstHandlerByFilter is varargs
// (count, sel0,val0, …) with selector 0 => kind@+0; the kind-scan call sites pass
// a single (selector=0, value=kind) pair (a/b are the original leading filter
// count operands, == 1 for these scans).
HeRecord* WeFindFirstHandler3(i32 /*a*/, i32 /*b*/, i32 kind) {
    return reinterpret_cast<HeRecord*>(
        HeT().FindFirstHandlerByFilter(1, /*sel*/0, /*val*/static_cast<int>(kind)));
}

// event5 findFirstHandler3(a, b, c): same single-(selector=0,value=c) kind scan.
HeRecord* WeFindFirstHandler5_3(i32 /*a*/, i32 /*b*/, i32 c) {
    return reinterpret_cast<HeRecord*>(
        HeT().FindFirstHandlerByFilter(1, /*sel*/0, /*val*/static_cast<int>(c)));
}

// event5 findFirstHandler5(a, b, c, d, e): the 5-operand form the bldg/slot
// coroutines pass; the actual filter is the single kind value the original folds
// into the varargs (selector 0 == kind@+0, value=c). a/b/d/e are the original's
// leading/trailing filter-count operands.
HeRecord* WeFindFirstHandler5_5(i32 /*a*/, i32 /*b*/, i32 c, i32 /*d*/, i32 /*e*/) {
    return reinterpret_cast<HeRecord*>(
        HeT().FindFirstHandlerByFilter(1, /*sel*/0, /*val*/static_cast<int>(c)));
}

// ===========================================================================
// id -> record resolves (entity.h linear scans). Person/ObjectRec share the
// record base; the casts to void* are the records' own pointers (identity).
// ===========================================================================

// VIBE_Person_FindRecordById(id) @0x58bc6c -> Person* (or null).
void* WeFindPersonById(i32 id) {
    return static_cast<void*>(guild::sim::PersonFindRecordById(id));
}

// VIBE_Building_FindById(id) @0x587b20 -> ObjectRec* (or null).
void* WeFindBuildingById(i32 id) {
    return static_cast<void*>(guild::sim::BuildingFindById(id));
}

// VIBE_GameObject_ResolveEntityById @0x583b44 — out-parameter resolver. The
// real function is GameObjectResolveEntityById(outObject, outScene, id,
// outPerson) returning {0 miss,1 object,2 scene,3 person}. The event-bridge
// signatures pass the OBJECT out-slot first (search Object/Building then scene),
// mirroring the originals' resolve order; we surface the resolved record into
// *outA and the resolve status / seq into the secondary out.

// event3 resolveEntityById(i32* outA, i32* outB, i32 id, i32 z): RequestSlotResultRun
// only needs the "resolved-ok" flag (-> *outB) and the resolved record (it keeps
// the id in *outA, matching the inert default which writes the raw ints). We
// resolve against the real Object/Building array and report the status.
void WeResolveEntity3(i32* outA, i32* outB, i32 id, i32 /*z*/) {
    ObjectRec* obj = nullptr;
    SceneNode* scene = nullptr;
    int r = guild::sim::GameObjectResolveEntityById(&obj, &scene, id, nullptr);
    if (outA) *outA = id;          // the original leaves the requested id in the slot
    if (outB) *outB = r;           // resolved status (0 miss / 1 object / 2 scene)
}

// event4 resolveEntityById(void** outA, i32* outB, id, z): writes the resolved
// record pointer to *outA and the status/seq to *outB.
void WeResolveEntity4(void** outA, i32* outB, i32 id, i32 /*z*/) {
    ObjectRec* obj = nullptr;
    SceneNode* scene = nullptr;
    int r = guild::sim::GameObjectResolveEntityById(&obj, &scene, id, nullptr);
    if (outA) *outA = obj ? static_cast<void*>(obj)
                          : static_cast<void*>(scene);
    if (outB) *outB = r;
}

// event5 resolveEntityById(void** outA, void** outB, id, void** outC) -> al (nonzero
// == resolved). outA receives the resolved record; outB/outC the alternate slots.
i32 WeResolveEntity5(void** outA, void** outB, i32 id, void** outC) {
    ObjectRec* obj = nullptr;
    SceneNode* scene = nullptr;
    int r = guild::sim::GameObjectResolveEntityById(&obj, &scene, id, nullptr);
    void* rec = obj ? static_cast<void*>(obj) : static_cast<void*>(scene);
    if (outA) *outA = rec;
    if (outB) *outB = rec;
    if (outC) *outC = rec;
    return r ? 1 : 0;
}

// ===========================================================================
// command emits -> real builders on the shared queue.
// ===========================================================================

// event3 enqueueObjectInteraction(a..h) — VIBE_Command_EnqueueObjectInteraction
// (opcode 11): a1(byte), a2, a3(word), a4, a5, a6/a7/a8(byte).
i32 WeEnqueueObjectInteraction3(i32 a, i32 b, i32 c, i32 d, i32 e, i32 f, i32 g, i32 h) {
    return guild::sim::EnqueueObjectInteraction(
        Q(), static_cast<u8>(a), b, static_cast<i16>(c), d, e,
        static_cast<u8>(f), static_cast<u8>(g), static_cast<u8>(h));
}

// event3 queueRequestPair33(a, b) — VIBE_Command_QueueRequestPair33 (opcode 33).
void WeQueuePair33_3(i32 a, i32 b) { guild::sim::QueueRequestPair33(Q(), a, b); }

// event3 queueRequest17(handle, a, qty, objId, flag, z) — opcode 17. The builder
// is QueueRequest17(q, a1, a2, a3, i16 a4, u8 a5, a6); the hook's 6 i32s map onto
// (a1=handle, a2=a, a3=qty, a4=objId(word), a5=flag(byte), a6=z).
i32 WeQueueReq17_3(i32 handle, i32 a, i32 qty, i32 objId, i32 flag, i32 z) {
    return guild::sim::QueueRequest17(Q(), handle, a, qty, static_cast<i16>(objId),
                                      static_cast<u8>(flag), z);
}

// event3 packetStatus / packetSeq — VIBE_Command_GetPacketStatusById/SeqById.
i32 WePacketStatus3(i32 handle) { return Q().GetPacketStatusById(static_cast<u32>(handle)); }
i32 WePacketSeq3(i32 handle)    { return Q().GetPacketSeqById(static_cast<u32>(handle)); }

// event4 queueRequest17(handle, a, qty, objId, flag, z) — opcode 17 (same map).
i32 WeQueueReq17_4(i32 handle, i32 a, i32 qty, i32 objId, i32 flag, i32 z) {
    return guild::sim::QueueRequest17(Q(), handle, a, qty, static_cast<i16>(objId),
                                      static_cast<u8>(flag), z);
}

// event4 queueRequestCoord27(handle, memberId, mode) — opcode 27. The original
// passes coordX/coordY = 0 (the move arg lands in a3==mode).
void WeQueueCoord27_4(i32 handle, i32 memberId, i32 mode) {
    guild::sim::QueueRequestCoord27(Q(), handle, memberId, mode, 0, 0);
}

// event4 queueRequestPair33(a, rel) — opcode 33.
void WeQueuePair33_4(i32 a, i32 rel) { guild::sim::QueueRequestPair33(Q(), a, rel); }

// event4 packetStatus / packetSeqValue — GetPacketStatusById / *(GetPacketSeqById).
i32 WePacketStatus4(i32 handle)   { return Q().GetPacketStatusById(static_cast<u32>(handle)); }
i32 WePacketSeqValue4(i32 handle) { return Q().GetPacketSeqById(static_cast<u32>(handle)); }

// event5 queueRequest17(a..f) — opcode 17 (a1,a2,a3,a4(word),a5(byte),a6).
i32 WeQueueReq17_5(i32 a, i32 b, i32 c, i32 d, i32 e, i32 f) {
    return guild::sim::QueueRequest17(Q(), a, b, c, static_cast<i16>(d),
                                      static_cast<u8>(e), f);
}

// event5 queueRequestEntity29(a, rec) — opcode 29; rec is the He record snapshot
// source. VIBE_Command_QueueRequestEntity29(q, i8 a1, HeRecord* h).
i32 WeQueueEntity29_5(i32 a, const void* rec) {
    return guild::sim::QueueRequestEntity29(
        Q(), static_cast<i8>(a),
        reinterpret_cast<HeRecord*>(const_cast<void*>(rec)));
}

// event5 queueRequestSingle49(a) / single59(a) — opcodes 49 / 59.
void WeQueueSingle49_5(i32 a) { guild::sim::QueueRequestSingle49(Q(), a); }

// event5 queueRequestCoord27(a, b, c) — opcode 27 (coordX/Y = 0).
void WeQueueCoord27_5(i32 a, i32 b, i32 c) {
    guild::sim::QueueRequestCoord27(Q(), a, b, c, 0, 0);
}

// event5 queueRequestArgs25(a..e) — opcode 25 (RequestPerm-class), full 5 args.
void WeQueueArgs25_5(i32 a, i32 b, i32 c, i32 d, i32 e) {
    guild::sim::QueueRequestArgs25(Q(), a, b, c, d, e);
}

// event5 queueRequest16(a, b, c, d) — opcode 16 (a4 byte).
void WeQueueReq16_5(i32 a, i32 b, i32 c, i32 d) {
    guild::sim::QueueRequest16(Q(), a, b, c, static_cast<u8>(d));
}

// event5 packetStatus — GetPacketStatusById.
i32 WePacketStatus5(i32 handle) { return Q().GetPacketStatusById(static_cast<u32>(handle)); }

// event5 personFindRecordById(id) — VIBE_Person_FindRecordById.
void* WePersonFind5(i32 id) { return static_cast<void*>(guild::sim::PersonFindRecordById(id)); }

// event5 buildingFindById(id) — VIBE_Building_FindById.
void* WeBuildingFind5(i32 id) { return static_cast<void*>(guild::sim::BuildingFindById(id)); }

// VIBE_Math_RandomModulo(n) @0x58b89c — shared by no event hook directly, but the
// queryBuildingHeMax/updateBuildingHeState event5 forwards reach event4. (kept
// for symmetry with the event4 walkers wired below.)

// ===========================================================================
// event5 -> event4 sibling forwards: RunGebaeudeBauen calls the two building-
// visibility walkers (VIBE_Event_UpdateBuildingHeState 0x4f52f8 /
// VIBE_Event_QueryBuildingHeMax 0x4f56f8) reconstructed in event4. The live
// wiring forwards them into the event4 implementations (see event5.h note).
// ===========================================================================
void WeUpdateBuildingHeState(HeRecord* h) { UpdateBuildingHeState(h); }
i32  WeQueryBuildingHeMax(HeRecord* h)    { return QueryBuildingHeMax(h); }

// --- process-lifetime wired hook tables (the global hook ptrs reference these) ---
EventHooks  g_event{};
Event3Hooks g_event3{};
Event4Hooks g_event4{};
Event5Hooks g_event5{};

} // namespace

void InstallRealEventWiring() {
    // Compose with the shared sim He pool + command queue. InstallRealSimHooks3
    // creates/Init's RealHandlerTable() and the sibling NpcLeafHooks pool that the
    // He-pool casts share; RealCommandQueue() is Init'd on first use.
    guild::sim::InstallRealSimHooks3();
    Q();    // force the shared real command queue to exist
    HeT();  // force the shared real He pool to exist

    // --- EventHooks (event2.h) ----------------------------------------------
    // SEED-FROM-DEFAULTS (the help/advice playback bodies call freeHandlerEntry
    // teardown without a null-check on the inert table).
    g_event = GetEventHooks();
    g_event.freeHandlerEntry = &WeFreeHandlerEntry;     // 0x4c6144 (He pool)
    // sendEntityMessage: VIBE_He_SendEntityMessage is a UI/script broadcast leaf
    // with no clean reconstructed target -> inert (kept as the no-op stub).
    SetEventHooks(&g_event);

    // --- Event3Hooks (event3.h) ---------------------------------------------
    g_event3 = GetEvent3Hooks();
    g_event3.freeHandlerEntry         = &WeFreeHandlerEntry;            // 0x4c6144
    g_event3.findPersonById           = &WeFindPersonById;             // 0x58bc6c
    g_event3.findFirstHandler         = &WeFindFirstHandler3;          // 0x4c63f8
    g_event3.findNextHandler          = &WeFindNextHandler;            // 0x4c6278
    g_event3.findBuildingById         = &WeFindBuildingById;           // 0x587b20
    g_event3.findObjectById           = &WeFindBuildingById;           // 0x587b20 (object array)
    g_event3.enqueueObjectInteraction = &WeEnqueueObjectInteraction3;  // opcode 11
    g_event3.queueRequestPair33       = &WeQueuePair33_3;              // opcode 33
    g_event3.queueRequest17           = &WeQueueReq17_3;               // opcode 17
    g_event3.packetStatus             = &WePacketStatus3;              // 0x4939d4
    g_event3.packetSeq                = &WePacketSeq3;                 // 0x4939fc
    g_event3.resolveEntityById        = &WeResolveEntity3;             // 0x583b44
    // INERT (no clean reconstructed target):
    //   personCharId / personScriptHandle (raw person-record offset readers —
    //     ambiguous offsets, same reason real_hooks leaves person readers null),
    //   stampTimeAndRequest (VIBE_NpcAction_StampTimeAndRequestEntity — cross),
    //   changePlayerAction (VIBE_Character_ChangePlayerAction leaf),
    //   queueGuardTarget61 (opcode 61 builder not reconstructed),
    //   buildingVariantIndex (VIBE_BuildingType_ComputeVariantIndex),
    //   buildingGuardState/Slot/Owner (dword_13CE294 building-table field probes),
    //   initAndShuffleDwordArray (VIBE_Util_InitAndShuffleDwordArray),
    //   setListener (VIBE_Sound3d_SetListenerFromVectors — audio/render),
    //   reportMessage (VIBE_ErrorLog_ReportMessage), eventPanelDestroySlot/
    //   eventPanelCreateSlot/formSelectWindow/textRenderRichString/
    //   activeWindowHandle/activeWindowMessage (UI dialog leaves),
    //   objectBuildModelName (VIBE_Object_BuildModelName — render),
    //   buildingOutputRatio (VIBE_Building_ComputeOutputRatio),
    //   buildingAdjustStock (VIBE_Building_AdjustStockAndNotify),
    //   sendArrivalMessage (Text_RenderFormattedMessage + SendQuickjumpMessage).
    SetEvent3Hooks(&g_event3);

    // --- Event4Hooks (event4.h) ---------------------------------------------
    g_event4 = GetEvent4Hooks();
    g_event4.freeHandlerEntry    = &WeFreeHandlerEntry;   // 0x4c6144
    g_event4.findPersonById      = &WeFindPersonById;     // 0x58bc6c
    g_event4.findBuildingById    = &WeFindBuildingById;   // 0x587b20
    g_event4.resolveEntityById   = &WeResolveEntity4;     // 0x583b44
    g_event4.queueRequest17      = &WeQueueReq17_4;       // opcode 17
    g_event4.queueRequestCoord27 = &WeQueueCoord27_4;     // opcode 27
    g_event4.queueRequestPair33  = &WeQueuePair33_4;      // opcode 33
    g_event4.packetStatus        = &WePacketStatus4;      // 0x4939d4
    g_event4.packetSeqValue      = &WePacketSeqValue4;    // 0x4939fc
    // INERT (no clean reconstructed target):
    //   activeActionHe / personActionCharId (dword_12CEA8C / word_12CE910 cancel-
    //     table — process-global arrays not modeled as callable leaves),
    //   changePlayerAction (VIBE_Character_ChangePlayerAction),
    //   queryFind / queryIterNext (scene query needs the live scene cursor),
    //   collectMatchingProts (pre-scan side effect, no reconstructed target),
    //   renderFormattedMessage / sendQuickjumpMessage (Text/messaging),
    //   sumWorkstationByCategory / computeOutputOverTime / averageFavorability /
    //   personCommandHandle (building/production/AI leaves not reconstructed),
    //   playQueuedSample / audioEnabled / announceEnabled (Voice + audio gates),
    //   scriptFinishByHandle / characterStandUp / insertCollapseAction /
    //   selectConversationTarget / broadcastFamilyNews / findEmploymentRelation /
    //   queueRequestPair... wait (pair33 wired), queueRequest39 / cutsceneFindSlotById
    //   (Script/Conversation/Cutscene leaves),
    //   universeSwitchSlot / collectHeNodes / node*/scene*/universe*/object* /
    //   findHeRootObject (Universe + SceneGraph building-He walkers — render).
    SetEvent4Hooks(&g_event4);

    // --- Event5Hooks (event5.h) ---------------------------------------------
    g_event5 = GetEvent5Hooks();
    g_event5.freeHandlerEntry    = &WeFreeHandlerEntry;       // 0x4c6144
    g_event5.findFirstHandler5   = &WeFindFirstHandler5_5;    // 0x4c63f8 (5-op form)
    g_event5.findFirstHandler3   = &WeFindFirstHandler5_3;    // 0x4c63f8 (3-op form)
    g_event5.findNextHandler     = &WeFindNextHandler;        // 0x4c6278
    g_event5.resolveEntityById   = &WeResolveEntity5;         // 0x583b44
    g_event5.queueRequest17      = &WeQueueReq17_5;           // opcode 17
    g_event5.queueRequestEntity29= &WeQueueEntity29_5;        // opcode 29
    g_event5.queueRequestSingle49= &WeQueueSingle49_5;        // opcode 49
    g_event5.queueRequestCoord27 = &WeQueueCoord27_5;         // opcode 27
    g_event5.queueRequestArgs25  = &WeQueueArgs25_5;          // opcode 25
    g_event5.queueRequest16      = &WeQueueReq16_5;           // opcode 16
    g_event5.packetStatus        = &WePacketStatus5;          // 0x4939d4
    g_event5.personFindRecordById= &WePersonFind5;            // 0x58bc6c
    g_event5.buildingFindById    = &WeBuildingFind5;          // 0x587b20
    // event5 RunGebaeudeBauen calls the two event4 building-He walkers as siblings.
    g_event5.updateBuildingHeState = &WeUpdateBuildingHeState; // -> event4 0x4f52f8
    g_event5.queryBuildingHeMax    = &WeQueryBuildingHeMax;    // -> event4 0x4f56f8
    // INERT (no clean reconstructed target): the dozens of specialized command
    // builders with no reconstructed opcode (queueRequest18 has a builder but the
    // hook arg shape is ambiguous; single59/namedObject53-with-string/flagBlob32/
    // flag55/pair36/quad43/state22/slotReset28/buildOp66/enqueueCmd15/
    // enqueueTargetedAction/beginDeltaPacket/appendRawField/appendDeltaField),
    // packetSeq (returns a record ptr — handle-vs-ptr LP64 mismatch),
    // sendQuickjump/sendEntityMessage/destroyIconGfx (UI/messaging),
    // queryFind/queryIterNext/findByHandle (scene cursor),
    // all Person/Family/Character leaves (personQueryBegin/personFindActiveByEntity/
    // personGetFamilyRecord/personGetCurrencyAmount/changePlayerAction/
    // characterCountByType/characterRefreshFlagAnim),
    // all Building/Production/Inventory/AI leaves (buildingFindStorableObject/
    // buildingFindWorkProductObject/buildingFindActiveWorkSlot/
    // buildingSumWorkstationByCategory/buildingComputeOutputRatio/
    // buildingLookupCachedMarketPrice/buildingComputeProductionPixels/
    // buildingGetBauplatzPos/buildingRequestGebaeudeBauen/
    // buildingValueComputeStockValue/buildingAdjustStockAndNotify/
    // buildingReserveBauplatz/buildingBuildPath/inventory*/aiAverageFavorability/
    // productionComputeOutputOverTime/productionComputeDailyHourOutput),
    // all Script/Scene/Object/Universe leaves (script*/universe*/object*/scene*/
    // mesh*), Text/Voice/Misc (renderFormattedMessage/audioStartVoiceSample/
    // gesetzEvaluateViolation/charAction*/combatAssignGuardTarget/moneyMultiplyByRate/
    // tradeTransportFindOwnerChain/npcSetTargetCityRef), and the global gates
    // (fastBuildMode/deferredBuildFlag/playerCitySentinel*/activeActionHe/
    // personActionCharId — process-global tables). All unreconstructed.
    SetEvent5Hooks(&g_event5);
}

} // namespace guild::world

#pragma once
// event5 — the FOUR GIANT deferred world-event "He"-action state machines of the
// Guild simulation (gilde.exe VIBE_Event_* family) that earlier slices left
// untranslated. Each is one of the big per-event "Run" coroutines the handler-pool
// scheduler ticks once an event fires; they are translated 1:1, preserving the
// coroutine PHASE semantics exactly:
//
//   * RunGebaeudeBauen  0x4f5b5c — the building-construction coroutine. A packet
//                                  gate (+132 handle) front-fronts a 10-phase
//                                  (counter+2 = 0..9) build sequence: spawn the
//                                  scaffold, drive the visible He construction
//                                  level over time, then materialise + announce
//                                  the finished building.
//   * RunProduktion     0x4f2bd0 — the workshop production coroutine. Phase machine
//                                  keyed off +112 (counter -2..2) draining the
//                                  pending work (+20/+24) into output over time,
//                                  with per-worker favorability and "out of stock"
//                                  / "warehouse full" message branches.
//   * DiscoveryRaidRun  0x4f0708 — the smuggler "Raubzug"/discovery-raid coroutine.
//                                  Phase (counter+2 = 0..7) machine moving a band of
//                                  raiders to a target, fining them on discovery,
//                                  and dividing the loot.
//   * SlotProcessRun    0x4f4348 — the multi-slot transport/process coroutine. Two
//                                  packet gates (+232/+236) front a member-slot scan
//                                  that takes inputs (+208 slots) and emits outputs
//                                  (+214 slots), queuing per-good transport requests
//                                  and crediting the wage.
//
// Companion files: event2.{h,cpp} / event3.{h,cpp} / event4.{h,cpp} (earlier
// He-action slices). This file picks the four large untranslated machines.
//
// Shared state reused (NOT redefined here — ODR):
//   * the global game clock qword_13CE852 — guild::sim::NpcClock().
//   * GameTime arithmetic — guild::sim::GameTimeAdvance / GameTimeCompare.
//   * the CRT LCG — guild::util::RandomModulo.
// Records are touched via guild::sim::HeBytes(h)+off (mirrors the decompiler's
// *(T*)(base+off)); see sim/he.h.
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::world {

using guild::sim::HeRecord;
using guild::sim::GameTime;

// ===========================================================================
// Recovered float / double constants (resolved from gilde.exe .rdata).
// ===========================================================================
constexpr double kProdWorkRateMul   = 0.01;                 // dbl_6202F8
constexpr double kProdClassMul      = 0.003968253968253968; // dbl_620300 (1/252)
constexpr double kProdFavorScale    = 0.25;                 // dbl_620308
constexpr double kProdSlotMul       = 0.5;                  // dbl_620310
constexpr double kProdFavorBias     = -0.5;                 // dbl_620318
constexpr double kProdCmdStateMul   = 0.1;                  // dbl_620320
constexpr float  kProdWorkAmtMul    = 1.2999999523162842f;  // flt_620328
constexpr double kSlotPriceMulIn    = 0.9;                  // dbl_6203E8 (take @ 90%)
constexpr double kSlotPriceMulRest  = 0.8;                  // dbl_6203F0 (rest @ 80%)
constexpr double kSlotPriceMulRaw   = 1.1;                  // dbl_6203E0 (raw @ 110%)
constexpr double kRaidLootValueMul  = 0.35;                 // dbl_61FFE0
constexpr double kRaidLootStockMul  = 0.1;                  // dbl_61FFE8
constexpr double kBuildScaffoldMul  = 1.8;                  // dbl_6204C0

// ===========================================================================
// Leaf hooks — side effects / queries that cross into clusters this file does
// not own (Command queue, GameObject resolve/query, Person/Family, Script,
// Building, Universe/SceneGraph, Text/UI, Inventory, Combat, Audio...). Passing
// nullptr to SetEvent5Hooks installs an inert default: every effect a no-op,
// every "find/resolve" returns null/0, every queue returns -1, every iterator
// ends immediately. Tests install scripted mocks.
//
// This struct is DISTINCT from event2's EventHooks / event3's Event3Hooks /
// event4's Event4Hooks (different callee set) to avoid any ODR clash. The shared
// clock is sim::NpcClock(); RandomModulo / GameTime* are reused via extern.
// ===========================================================================
struct Event5Hooks {
    // --- He handler-pool plumbing --------------------------------------------
    // VIBE_He_FreeHandlerEntry(record,...) — release this handler (teardown).
    i32  (*freeHandlerEntry)(HeRecord* h);
    // VIBE_He_FindFirstHandlerByFilter / FindNextMatchingHandler — handler scan.
    // We expose the 5-arg and 3-arg forms (the originals overload by call site).
    HeRecord* (*findFirstHandler5)(i32 a, i32 b, i32 c, i32 d, i32 e);
    HeRecord* (*findFirstHandler3)(i32 a, i32 b, i32 c);
    HeRecord* (*findNextHandler)();
    // VIBE_He_SendQuickjumpMessage / SendEntityMessage — UI broadcast (collapsed).
    void (*sendQuickjump)(i32 dest, i32 a, i32 b, const char* buf, i32 tag,
                          i32 e, i32 f, i32 g, const char* name);
    void (*sendEntityMessage)(i32 dest, i32 a, i32 b, const char* buf, i32 tag,
                              const char* name);
    void (*destroyIconGfx)(void* gfx);     // VIBE_He_DestroyIconGfx

    // --- GameObject resolve / query ------------------------------------------
    // ResolveEntityById(outA,outB,id,outC): resolves an entity. Returns the al
    // low byte (nonzero == resolved). outA/outB receive the record/seq; outC the
    // alternate record (the 4th-arg byref some call sites pass).
    i32   (*resolveEntityById)(void** outA, void** outB, i32 id, void** outC);
    void* (*queryFind)(i32 scene, i32 a, i32 b, i32 c, i32 d);  // VIBE_GameObject_QueryFind
    void* (*queryIterNext)();                                   // VIBE_GameObject_IterNext
    void* (*findByHandle)(i32 a, i32 b, const char* name, i32 d, i32 e); // VIBE_Object_FindByHandle

    // --- Command queue emits --------------------------------------------------
    i32   (*queueRequest17)(i32 a, i32 b, i32 c, i32 d, i32 e, i32 f);
    i32   (*queueRequest18)(i32 a, i32 b, i32 c, i32 d);
    i32   (*queueRequestEntity29)(i32 a, const void* rec);
    void  (*queueRequestSingle49)(i32 a);
    void  (*queueRequestSingle59)(i32 a);
    void  (*queueRequestNamedObject53)(i32 a, i32 b, i32 c, i32 d, i32 e, const char* name);
    void  (*queueRequestFlagBlob32)(i32 a, const void* blob);
    void  (*queueRequestFlag55)(i32 a, i32 b);
    void  (*queueRequestPair36)(i32 a, i32 b);
    void  (*queueRequestQuad43)(i32 a, i32 b, i32 c, i32 d);
    void  (*queueRequestCoord27)(i32 a, i32 b, i32 c);
    void  (*queueRequestArgs25)(i32 a, i32 b, i32 c, i32 d, i32 e);
    void  (*queueRequestState22)();
    i32   (*queueRequestSlotReset28)(const void* blob);  // VIBE_Command_QueueRequestSlotReset28
    void  (*queueRequest16)(i32 a, i32 b, i32 c, i32 d);
    void  (*requestBuildOp66)(i32 a);                    // VIBE_Command_RequestBuildOp66
    i32   (*enqueueCmd15)(i32 a, i32 b, i32 c, i32 d);   // VIBE_Command_EnqueueCmd15
    i32   (*enqueueTargetedAction)(i32 a, i32 b, i32 c, i32 d); // VIBE_Command_EnqueueTargetedAction
    void  (*beginDeltaPacket)(i32 a, i32 b);             // VIBE_Command_BeginDeltaPacket
    void  (*appendRawField)(i32 a, i32 b, const void* c, i32 d); // VIBE_Command_AppendRawField
    void  (*appendDeltaField)(i32 a, i32 b, i32 c, i32 d);       // VIBE_Command_AppendDeltaField
    i32   (*packetStatus)(i32 handle);                   // VIBE_Command_GetPacketStatusById
    void* (*packetSeq)(i32 handle);                      // VIBE_Command_GetPacketSeqById

    // --- Person / Family / Character -----------------------------------------
    void* (*personFindRecordById)(i32 id);               // VIBE_Person_FindRecordById
    void* (*personQueryBegin)(i32 a, i32 b, i32 c, i32 d); // VIBE_Person_QueryBegin
    void* (*personFindActiveByEntity)(void* rec);        // VIBE_Person_FindActiveByEntity
    void* (*personGetFamilyRecord)(void* rec);           // VIBE_Person_GetFamilyRecord
    i32   (*personGetCurrencyAmount)(void* rec, i32 currency); // VIBE_Person_GetCurrencyAmount
    void  (*changePlayerAction)(void* obj, void* b, void* c, u16 charId); // VIBE_Character_ChangePlayerAction
    void  (*characterCountByType)(void* rec);            // VIBE_Character_CountByType
    void  (*characterRefreshFlagAnim)(void* rec);        // VIBE_Character_RefreshFlagAnimation

    // --- Building / Production / Inventory / AI -------------------------------
    void* (*buildingFindById)(i32 id);                   // VIBE_Building_FindById
    void* (*buildingFindStorableObject)(void* rec);      // VIBE_Building_FindStorableObject
    void* (*buildingFindWorkProductObject)(void* rec);   // VIBE_Building_FindWorkProductObject
    void* (*buildingFindActiveWorkSlot)(i32 hiword);     // VIBE_Building_FindActiveWorkSlot
    i32   (*buildingSumWorkstationByCategory)(void* rec, i32 a, i32 b); // VIBE_Building_SumWorkstationByCategory
    double(*buildingComputeOutputRatio)(void* rec);      // VIBE_Building_ComputeOutputRatio
    double(*buildingLookupCachedMarketPrice)(i32 item, i32 currency); // VIBE_Building_LookupCachedMarketPrice
    i32   (*buildingComputeProductionPixels)(i32 a, void* rec); // VIBE_Building_ComputeProductionPixels
    i32   (*buildingGetBauplatzPos)(void* outPos, void* rec);   // VIBE_Building_GetGebaeudeBauplatzPos
    i32   (*buildingRequestGebaeudeBauen)(void* rec);    // VIBE_Building_RequestGebaeudeBauen
    void  (*buildingValueComputeStockValue)(void* rec, i32* outValue); // VIBE_BuildingValue_ComputeStockValue
    void  (*buildingAdjustStockAndNotify)(i32 a, i32 delta, i32 c); // VIBE_Building_AdjustStockAndNotify
    void  (*buildingReserveBauplatz)(void* a, void* obj); // VIBE_Building_ReserveBauplatzForActiveChar
    void  (*buildingBuildPath)(void* a, i32 type);       // VIBE_Building_BuildGebaeudePath
    i32   (*inventoryGetEffectiveStock)(void* inv, void* item); // VIBE_Inventory_GetEffectiveStock
    i32   (*inventoryComputeFreeCapacity)(void* inv, i32 type, i32 c, i32 d); // VIBE_Inventory_ComputeFreeCapacity
    i32   (*inventoryGetSlotCapacity)(void* item);       // VIBE_Inventory_GetSlotCapacity
    i32   (*inventoryIsProductionSlotMatch)(i32 a, i32 b); // VIBE_Inventory_IsProductionSlotMatch
    double(*aiAverageFavorability)(u16 cityIdx, i32 n, const i32* members); // VIBE_Ai_AverageObjectFavorability
    i32   (*productionComputeOutputOverTime)(const GameTime* a, const GameTime* b); // VIBE_Production_ComputeOutputOverTime
    i32   (*productionComputeDailyHourOutput)(const GameTime* a, const GameTime* b); // VIBE_Production_ComputeDailyHourOutput

    // --- Script / Scene / Object / Universe ----------------------------------
    void* (*scriptFindByHandle)(i32 handle);             // VIBE_Script_FindByHandle
    void  (*scriptFinish)(void* script);                 // VIBE_Script_Finish
    void* (*scriptLoadFromScriptDir)(const char* name);  // VIBE_Script_LoadFromScriptDir
    void  (*scriptRunWithArgs)(void* script, i32 argc, i32 arg); // VIBE_Script_RunWithArgs
    void  (*scriptStep)(void* script, HeRecord* h);      // VIBE_Script_Step
    i32   (*universeSwitchSlot)(i32 slot);               // VIBE_Universe_SwitchActiveSlot
    void  (*universeRestoreObjectStates)(void* node, i32 mode); // VIBE_Universe_RestoreObjectStates
    void  (*objectDetachAndRelease)(void* node);         // VIBE_Object_DetachAndRelease
    void  (*objectBuildModelName)(i32 a, void* rec, i32 mode); // VIBE_Object_BuildModelName
    void  (*objectHideUpgradeScaffold)(void* obj);       // VIBE_Object_HideUpgradeScaffold
    void  (*objectInflateGeometry)(void* obj);           // VIBE_Object_InflateGeometry
    i32   (*objectIsNearDoor)(void* rec);                // VIBE_Object_IsNearDoor
    int   (*sceneCollectHandles)(void** out, int cap);   // SceneGraph_WalkAndInvoke + AppendCollectedHandle
    void  (*sceneRemoveMeshFromTree)(void* node);        // VIBE_SceneGraph_RemoveMeshFromTree
    void  (*sceneRebuildRegionOctree)(void* obj);        // VIBE_SceneGraph_RebuildRegionOctree
    void  (*meshApplyTransformRecursive)(void* obj);     // VIBE_Mesh_ApplyTransformRecursive
    i32   (*meshComputeBoundingRadius)(void* obj, float* outRadius); // VIBE_Mesh_ComputeBoundingRadius

    // --- Text / Voice / Misc --------------------------------------------------
    void  (*renderFormattedMessage)(char* buf, i32 fmtId, i32 a, i32 b, i32 c); // VIBE_Text_RenderFormattedMessage
    void  (*audioStartVoiceSample)(i32 a, i32 b, i32 c, i32 d, i32 e); // VIBE_Audio_StartVoiceSample
    i32   (*gesetzEvaluateViolation)(i32 a, i32 b, i32 c, i32 d, i32 e); // VIBE_Gesetz_EvaluateViolation
    void* (*charActionFindGestureTarget)(void* ctx);     // VIBE_CharAction_FindGestureTarget
    i32   (*charActionIsAnimalTargetBusy)(void* rec);    // VIBE_CharAction_IsAnimalTargetBusy
    void  (*combatAssignGuardTarget)(void* a, void* obj); // VIBE_Combat_AssignGuardTarget
    i32   (*moneyMultiplyByRate)(i32 amount, i32 currency); // VIBE_Money_MultiplyByRate
    void  (*tradeTransportFindOwnerChain)(void* a, void* b); // VIBE_TradeTransport_FindOwnerChain
    void  (*npcSetTargetCityRef)(HeRecord* h, u16 city); // VIBE_NpcAction_SetTargetCityRef

    // --- Recovered global gates / tables -------------------------------------
    // dword_63C7B8 — global fast/debug build-speed gate (0 == normal).
    i32   (*fastBuildMode)();
    // dword_649D60 — the "deferred build queued" flag the bldg coroutine restores.
    i32   (*deferredBuildFlag)();
    // dword_6498E4[+4] — the "player city" sentinel entity id (RunGebaeudeBauen
    // case 2 compares +172 against it; SlotProcessRun seeds +12 with its word).
    i32   (*playerCitySentinel)();
    u16   (*playerCitySentinelWord)();
    // The cancel-my-actions table: walks all 768 person slots, and for each whose
    // active-action handler (dword_12CEA8C[p]) == THIS record, re-invokes
    // ChangePlayerAction(obj,0,0, word_12CE910[p]). Exposed as two queries.
    void* (*activeActionHe)(int person);   // dword_12CEA8C[person]
    u16   (*personActionCharId)(int person); // word_12CE910[person]
    // The two building-visibility walkers (VIBE_Event_UpdateBuildingHeState 0x4f52f8 /
    // VIBE_Event_QueryBuildingHeMax 0x4f56f8 — reconstructed in event4.{h,cpp}).
    // RunGebaeudeBauen calls them as siblings; the live wiring forwards these into
    // the event4 implementations. Inert defaults: no-op / 0.
    void  (*updateBuildingHeState)(HeRecord* h);
    i32   (*queryBuildingHeMax)(HeRecord* h);
};

void SetEvent5Hooks(const Event5Hooks* hooks);
const Event5Hooks& GetEvent5Hooks();

// ===========================================================================
// The four giant phase machines.
// ===========================================================================

// gilde.exe 0x4f5b5c — VIBE_Event_RunGebaeudeBauen (al = run(h@eax, edi, esi)).
//   The building-construction coroutine. A packet gate fronts a 10-phase
//   sequence keyed off (+112 state)+2 (so state -2/-1 map to phases 0/1):
//     gate: if the pending build packet (+132) is unresolved, do nothing.
//     0/1 -> spawn: resolve the build object (+184), finish any running script
//            (+188), set +200 = +196 + 1, push the He-state update, optionally
//            request the build-flag blob, free the handler.
//     2   -> if the appointment (+82) is due and flag&2 set: enqueue the build
//            command (player vs npc path), bump the family treasury.
//     3/5 -> poll the build packet (+204); on ready re-arm the entity-29 packet.
//     4   -> enqueue the targeted "build" action, re-arm the entity-29 packet.
//     6   -> resolve the object (+184); spawn the build effect, build the model,
//            seed the He level fields (+192/+196/+200) from QueryBuildingHeMax,
//            advance +82 +1 min.
//     7   -> find the nearest door object, walk the scene "dummy_tuer" nodes,
//            run the per-door specialevents script, stamp +96/+82, advance +10.
//     8   -> advance +82 +20 min; if no doors pending, detach the temporary door
//            meshes, re-stamp, advance +10.
//     9   -> drive production output until the target level (+196) is reached,
//            then materialise the finished building (inflate geometry, refresh
//            flags, reserve the plot, announce to neighbours), free.
//   All cross-cluster calls route through hooks; the level/packet decisions are
//   translated verbatim. Returns the original al.
i32 RunGebaeudeBauen(HeRecord* h);

// gilde.exe 0x4f2bd0 — VIBE_Event_RunProduktion (eax = run(h@eax, edi, esi)).
//   The workshop-production coroutine. If the city index (+8) is 0xFFFF this is a
//   teardown-only record (free immediately). Otherwise a phase machine off +112:
//     -2/-1 -> resolve the building (+188), request the build-op, run the
//              cancel-my-actions loop over all 768 person slots, free.
//      0    -> ++counter.
//      1    -> drain the pending work (+20/+24): poll the in-flight request (+196),
//              resolve the building, compute the work rate from the workstation
//              category count, per-worker favorability (when the tired byte +208 >=
//              60), accumulate production over time, then loop GameTimeCompare on
//              +82 emitting QueueRequest18 batches and advancing +82 +1 min until
//              the production batch completes; on "out of stock"/"warehouse full"
//              emit the localized message and idle +5 min.
//      2    -> if work re-appeared set counter:=1; else resolve + cancel + build-op,
//              free.
//   Returns the original eax. The string-copy message-prefix selection (Rohstoff
//   names by building class byte) and the production math are translated.
i32 RunProduktion(HeRecord* h);

// gilde.exe 0x4f0708 — VIBE_Event_DiscoveryRaidRun (eax-ptr = run(h@eax)).
//   The smuggler "Raubzug" discovery-raid coroutine. Phase machine off (+112)+2:
//     0/1 -> for each band member (+140 slot array) and the raid leader, queue the
//            "single"/"named-object" raid commands + change the player action, free.
//     2   -> begin the raid: change each member's action, stamp +82, advance +4
//            min, set counter:=1.
//     3   -> scan the 8 member slots; if any member is resolved AND not near a door
//            advance +82 +4 min; else advance +82 +1 hour, counter:=2.
//     4   -> find a gesture target near the leader; if found evaluate the law
//            violation (fine), record the packet (+180/+184/+188), counter:=3;
//            else change member actions, advance +15 min, counter:=5.
//     5   -> poll the fine packet (+180); if the matching handler still owns it,
//            issue the pair-36 follow-up, counter:=4, advance +10 min; else reset.
//     6   -> if the handler still owns +184, advance +6 min; else compute the
//            payout (RandomModulo(200)+550 via the money rate), notify both
//            parties, adjust each member's stock by -(100+RandomModulo(100)),
//            issue the "entdeckt" named-object commands, free.
//     7   -> divide the loot: compute the leader's haul, evaluate the violation,
//            and (when the leader is busy/absent) build the "loot share" message,
//            iterating the storehouse goods with a 50% RandomModulo gate to value
//            and transfer each, credit the family treasury, notify, free.
//   Returns the original eax pointer (as i32). All cross-cluster calls route
//   through hooks; the RNG draws use util::RandomModulo (the itest wires the real
//   one over crt::RandNext).
i32 DiscoveryRaidRun(HeRecord* h);

// gilde.exe 0x4f4348 — VIBE_Event_SlotProcessRun (eax = run(h@eax, edi)).
//   The multi-slot transport/process coroutine. Front gates: state -2 reseeds the
//   target-city ref; two in-flight packets (+232/+236) idle the handler +1 min
//   until resolved. Then resolve the three role entities (+172 dest, +176 source,
//   +180 wage); if either of the first two is missing, free.
//     If a sibling handler already owns the same dest/source, idle (or free).
//     Else find the source's process node, clamp the member count to 3, and:
//       input scan (+208 slots): for each filled input slot whose +184 matches the
//       source, query the good, value it (take @90% / rest @80% / raw @110% by the
//       building class byte 10), emit QueueRequest17 take + rest commands, credit
//       the wage accumulator (+440 dword of the wage record).
//       output scan (+214 slots): for each filled output slot whose +196 matches,
//       compute the sellable quantity bounded by the wage budget and stock, emit
//       QueueRequest17 sell commands + the localized "Sprintf" line, debit the
//       wage accumulator.
//     Tail: if +240 set and any slot stalled, idle (30 min, or 5 min once the
//       hour >= 20, decrementing +240); else pick the next pending slot's id into
//       +176, resolve a fallback, and either idle +30 (carry flag +241) or emit
//       the slot-reset packet (+236) and idle +5/+10.
//   Returns the original eax. All cross-cluster calls route through hooks.
i32 SlotProcessRun(HeRecord* h);

} // namespace guild::world

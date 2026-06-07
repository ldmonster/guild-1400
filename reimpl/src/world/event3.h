#pragma once
// event3 — a further slice of the world-event "He"-action bodies of the Guild
// simulation (gilde.exe VIBE_Event_* family). These are the per-event-type
// phase machines, schedule/trigger bodies and small predicates that the
// handler-pool scheduler invokes once an event fires. Each operates on one He /
// handler record (see sim/he.h) by the original raw byte offsets.
//
// Companion files: event2.{h,cpp} (the timestamp/anim action initializers and
// the help/advice playback bodies), event_effects.{h,cpp} (FireRaidComputeDuration,
// PriceStateMachine, BuildingProductionTrigger), event_fire.* (FireRaidRun),
// event_bindings.* (registration / name<->id / serialization), event.{h,cpp}
// (descriptor table). This file translates a fresh, previously-untranslated set.
//
// Translated functions (addresses absolute, imagebase 0x400000):
//   0x4ef500 VIBE_Event_StartActorAction          — stamp 4 time blocks, queue char action, advance
//   0x4ef5d4 VIBE_Event_MoveTowardTargetRun        — output-decay transport phase machine
//   0x4ef270 VIBE_Event_GatherTargetsInit          — collect candidate production buildings into He slots
//   0x4ef7e8 VIBE_Event_RequestGuardInteraction     — pick a guard interaction request
//   0x4efb64 VIBE_Event_SinkToGroundStateMachine    — 6-phase "sink to ground" death/faint sequence
//   0x4efcdc VIBE_Event_AllocKillPlayer             — locate the kill-player handler, reset its schedule
//   0x4f01d0 VIBE_Event_SetActorAnimById            — season-driven idle anim + clock stamp
//   0x4f1bc0 VIBE_Event_PrepareInfoAction           — schedule the info-advice wake + shuffle advice ids
//   0x4f24e0 VIBE_Event_CancelMatchingActors         — re-stamp every armed type-35 handler matching a person
//   0x4f4114 VIBE_Event_NewDepositMessageBoxRun      — "new deposit found" message-box phase machine
//   0x4f4fbc VIBE_Event_RequestSlotResultRun         — slot-request result poll phase machine
//   0x4f5270 VIBE_Event_UpdateListenerFromActor      — push the 3D-sound listener from an actor transform
//   0x596074 VIBE_Event_MatchPersonState15Cmd272     — predicate: person in state 15 + cmd 272
//   0x5960c0 VIBE_Event_MatchType26OrJump            — predicate: command kind 26 with a record
//
// The +82 / +96 / +176 fourteen-byte blocks the originals copy from the global
// clock (qword_13CE852 + the trailing dword/word at +90/+94) are GameTime images;
// we copy them as a whole GameTime through the sim::He_* accessors. The phase
// counter the state machines read is at He+112 (`*(int*)(a1+112)`); several
// originals compute `*(int*)(a1+112) + 2` and switch on it (so counter -2/-1 map
// to the teardown cases 0/1). We preserve that exact decode.
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::world {

using guild::sim::HeRecord;
using guild::sim::GameTime;

// ===========================================================================
// Leaf hooks — side effects / queries that cross into clusters this file does
// not own (Person, Building, Object, Character, Command, He-pool, sound, UI...).
// Passing nullptr to SetEvent3Hooks installs an inert default: every effect a
// no-op, every "find" returns nullptr/0, every queue returns -1, the handler
// scan finds nothing, the building-table reads return 0. Tests install a
// recording / scripted mock to exercise the bodies in isolation.
//
// This struct is distinct from event2's EventHooks (different callee set) to
// avoid an ODR clash; the shared clock is sim::NpcClock(), not duplicated here.
// ===========================================================================
struct Event3Hooks {
    // VIBE_He_FreeHandlerEntry(record) — release the handler entry (teardown).
    // Returns the original eax; the inert default returns the record base as int.
    i32 (*freeHandlerEntry)(HeRecord* h);

    // VIBE_Person_FindRecordById(id) — resolve a person record by id, or null.
    // The first word of the record (*p) is the person's "char id" the callers
    // pass to ChangePlayerAction; we model the record as an opaque void*.
    void* (*findPersonById)(i32 id);
    // First word (*(u16*)record) of a person record — the char/action id.
    u16 (*personCharId)(void* personRec);
    // Active script handle stored at person+388 (dword index 97), or -1 / 0.
    i32 (*personScriptHandle)(void* personRec);

    // VIBE_He_FindFirstHandlerByFilter(a,b,kind) / VIBE_He_FindNextMatchingHandler()
    // — iterate the live handler pool filtered by event kind. Return null at end.
    HeRecord* (*findFirstHandler)(i32 a, i32 b, i32 kind);
    HeRecord* (*findNextHandler)();

    // VIBE_NpcAction_StampTimeAndRequestEntity(record) — re-arm a handler (used
    // by CancelMatchingActors to bump every matching actor).
    void (*stampTimeAndRequest)(HeRecord* h);

    // VIBE_Building_FindById(id) — resolve a building record by id, or null.
    void* (*findBuildingById)(i32 id);
    // VIBE_Object_FindObjectById(id) — resolve a scene object by id, or null.
    void* (*findObjectById)(i32 id);
    // VIBE_Character_ChangePlayerAction(building, object, record, charId).
    void (*changePlayerAction)(void* building, void* object, void* record, u16 charId);

    // Command-queue emits (return the resulting packet handle):
    //   VIBE_Command_EnqueueObjectInteraction(...)
    i32 (*enqueueObjectInteraction)(i32 a, i32 b, i32 c, i32 d, i32 e, i32 f, i32 g, i32 h);
    //   VIBE_Command_QueueRequestGuardTarget61(building, a, kind, owner)
    i32 (*queueGuardTarget61)(void* building, i32 a, i32 kind, i32 owner);
    //   VIBE_Command_QueueRequestPair33(a, b)
    void (*queueRequestPair33)(i32 a, i32 b);
    //   VIBE_Command_QueueRequest17(handle, a, qty, objId, flag, z) -> packet handle
    i32 (*queueRequest17)(i32 handle, i32 a, i32 qty, i32 objId, i32 flag, i32 z);
    //   VIBE_Command_GetPacketStatusById(handle) -> 0 pending / 1 applied / 2 failed
    i32 (*packetStatus)(i32 handle);
    //   VIBE_Command_GetPacketSeqById(handle) -> resolved sequence/packet record ptr
    i32 (*packetSeq)(i32 handle);

    // VIBE_BuildingType_ComputeVariantIndex(typeIdx, mode) — guard-target variant.
    i32 (*buildingVariantIndex)(i32 typeIdx, i32 mode);

    // Building-table reads for RequestGuardInteraction. The original indexes the
    // global building array `dword_13CE294 + 589 * *building` and reads three
    // fields; we expose them as queries on the resolved building record:
    //   guardState : *(building_record)         (== 2 selects the "spawn guard" path)
    //   guardSlot[i] : byte at +547 of slot 0..5 (the first non-zero one)
    //   guardOwner : HIBYTE(dword at +557)
    i32 (*buildingGuardState)(void* building);
    i32 (*buildingGuardSlot)(void* building, int slot);   // slots 0..5
    i32 (*buildingGuardOwner)(void* building);

    // VIBE_Util_InitAndShuffleDwordArray(count, array) — Fisher-Yates a dword set.
    i32 (*initAndShuffleDwordArray)(u32 count, i32* arr);
    // VIBE_Sound3d_SetListenerFromVectors(actor, fwd, mode, up, n) -> result.
    i32 (*setListener)(const void* actor, const float* fwd, i32 mode,
                       const float* up, i32 n);

    // VIBE_ErrorLog_ReportMessage(text) — diagnostic; inert default no-op.
    void (*reportMessage)(const char* msg);

    // UI plumbing for NewDepositMessageBoxRun:
    void (*eventPanelDestroySlot)(HeRecord* h, i32 a);
    void (*eventPanelCreateSlot)(HeRecord* h, i32 a, i32 textId);
    void (*formSelectWindow)(i32 window, i32 a);
    i32  (*textRenderRichString)(const char* fmt, const char* arg);
    // The active-window dialog gate the phase-3 teardown checks (dword_75BF04 /
    // dword_75BF38). Default returns {0,0} so the teardown does not trigger.
    i32  (*activeWindowHandle)();
    i32  (*activeWindowMessage)();

    // VIBE_GameObject_ResolveEntityById(outA, outB, id, z) — resolve an entity;
    // RequestSlotResultRun only needs the "resolved-ok" flag and the resolved
    // record pointer (written to *out). Inert default writes 0 and returns.
    void (*resolveEntityById)(i32* outA, i32* outB, i32 id, i32 z);
    // VIBE_Object_BuildModelName(a, rec, mode) — finalize a model name; inert no-op.
    void (*objectBuildModelName)(i32 a, i32 rec, u32 mode);

    // MoveTowardTargetRun leaves:
    //   VIBE_Building_ComputeOutputRatio(actor) -> production ratio (double).
    double (*buildingOutputRatio)(void* actor);
    //   VIBE_Building_AdjustStockAndNotify(charId, delta, mode).
    void (*buildingAdjustStock)(u16 charId, i32 delta, i32 mode);
    //   VIBE_Text_RenderFormattedMessage + VIBE_He_SendQuickjumpMessage — the
    //   arrival notification; collapsed into one inert default no-op.
    void (*sendArrivalMessage)(HeRecord* h, void* building, u16 charId);
};

void SetEvent3Hooks(const Event3Hooks* hooks);
const Event3Hooks& GetEvent3Hooks();

// The season idle-anim base table flt_6476FC (one float per season 0..3):
//   spring 8, summer 7, autumn 8, winter 9.
extern const float kSeasonAnimBase[4];
// Recovered float constants for the transport-decay model (MoveTowardTargetRun):
constexpr float  kMoveRateFlt   = 0.1f;   // flt_61FD3C
constexpr float  kMoveScaleFlt  = 5.0f;   // flt_61FD40
constexpr double kMoveRatioMul  = 100.0;  // dbl_61FD48
constexpr float  kMoveStockCap  = 100.0f; // flt_61FD50
constexpr double kListenerZBias = 1.47;   // dbl_620418

// ===========================================================================
// Phase machines / scheduling bodies
// ===========================================================================

// gilde.exe 0x4ef500 — VIBE_Event_StartActorAction (h@eax).
//   Resolve the actor (He+172) and host building (He+16) and optional object
//   (He+196 if != -1), kick a character action, then stamp the global clock into
//   the deadline (+176/+180), saved (+68), appointment (+82) and scratch (+96)
//   blocks and seed +176 := 200 before advancing the appointment by
//   10 * (200 / 5) = 400 minutes. Returns the resulting hour-of-day.
i32 StartActorAction(HeRecord* h);

// gilde.exe 0x4ef5d4 — VIBE_Event_MoveTowardTargetRun (h@eax).
//   Counter -2/-1: stop the actor's action and free. Counter 0: model the output
//   transport — moved = trunc(diffMinutes(+96, clock) * 0.1 * 5); if the remaining
//   amount (+176) is non-zero and (ratio*100 + moved) < 100 it adjusts the
//   building stock by `moved`, decrements +176 by `moved` (trunc), re-stamps the
//   appointment and advances 10 minutes; else bumps the counter. Always refreshes
//   the +96 scratch clock. Counter 1: deliver the arrival message and free.
//   `outputRatio` is VIBE_Building_ComputeOutputRatio(actor) — supplied via hooks
//   The building output-ratio / stock-adjust / arrival message route through
//   hooks. Returns nothing (void original).
void MoveTowardTargetRun(HeRecord* h);

// gilde.exe 0x4ef270 — VIBE_Event_GatherTargetsInit (h@eax).
//   Scan the production/storage buildings, collect those with a live model (+388)
//   into a scratch list, reset the He+172.. slot ids (16 slots) to -1, then pick a
//   seed building (random if He+172==-1) and fill up to 15 follower slots with
//   buildings within the He+240 tolerance of the seed. Zeroes He+256/+260 and
//   stamps the appointment from the clock. Returns the record. The candidate
//   collection + tolerance test are routed through hooks; the slot bookkeeping
//   and RNG selection are translated here. Returns the record base.
//
// The extended overload takes the scene-derived candidate building ids and the
// per-candidate within-tolerance results the engine computes; the one-arg
// overload is the inert path (no candidates -> no followers).
HeRecord* GatherTargetsInit(HeRecord* h,
                            const i32* candidateIds, int candidateCount,
                            const bool* withinTolerance);
HeRecord* GatherTargetsInit(HeRecord* h);

// gilde.exe 0x4ef7e8 — VIBE_Event_RequestGuardInteraction (h@eax).
//   Resolve the building at He+180; if absent free the handler. If its guard-state
//   is 2 enqueue a random object interaction (RandomModulo(12)+1 -> variant index,
//   RandomModulo(5)+22 -> kind); else find the first set guard slot (0..5) and
//   queue a guard-target request. Store the resulting handle into He+172 and stamp
//   the appointment from the clock. Returns the handle (or the free result).
i32 RequestGuardInteraction(HeRecord* h);

// gilde.exe 0x4efb64 — VIBE_Event_SinkToGroundStateMachine (h@eax).
//   Six-phase faint/collapse coroutine keyed off He+112+2:
//     0/1 (counter -2/-1) -> free.
//     2 -> resolve person; finish any running script on it; play the collapse
//          char action; ++counter.
//     3 -> wait until the person's script handle clears, then ++counter.
//     4 -> clear the person's standing flag, play the action, stand the model up,
//          attach the "bewegung/zu_boden_sinken" action string, stamp the
//          appointment and advance 1 day; ++counter.
//     5 -> if the person exists and He+120&2, queue a pair-33 command then free;
//          else free.
//   Returns the person/record pointer (the original eax). The script/character
//   plumbing routes through hooks; the phase transitions are translated.
HeRecord* SinkToGroundStateMachine(HeRecord* h);

// gilde.exe 0x4efcdc — VIBE_Event_AllocKillPlayer (h@eax).
//   If the target person (He+172) is missing, log "player not found" and free.
//   Otherwise scan the handler pool (kind 114) for the entry whose +172 matches
//   this one's +172 (skipping self); when found, reset its schedule: clear the
//   deadline (+176/+180 = -1), copy +68 -> +82 (saved -> appointment), clear the
//   request handle (+132 = -1), and return that handler. If none is found, free.
HeRecord* AllocKillPlayer(HeRecord* h);

// gilde.exe 0x4f01d0 — VIBE_Event_SetActorAnimById (h@eax).
//   Stamp clock into +82 and +68, set He+172 := RandomModulo(5) - 5, look up the
//   season anim base flt_6476FC[day%4], zero He+88 and set He+86 := (int)base.
//   Returns the chosen anim (int)base.
i32 SetActorAnimById(HeRecord* h);

// gilde.exe 0x4f1bc0 — VIBE_Event_PrepareInfoAction (h@eax).
//   Stamp the clock into +82; if a kind-131 handler already exists schedule the
//   appointment day +4, else same day; set He+86 := 8, He+88 := 0; shuffle the
//   26-entry advice id array. `anotherInfoActive` mirrors the handler-exists test.
//   Returns the shuffle result. The 26-id array is supplied by the host/test.
i32 PrepareInfoAction(HeRecord* h, bool anotherInfoActive, i32* adviceIds);

// gilde.exe 0x4f24e0 — VIBE_Event_CancelMatchingActors (h@eax via ecx).
//   Walk every kind-35 handler whose +120 flags have bit 1 or 2 set and whose +176
//   (dword index 44) equals this record's +4 (person id), re-stamping each via
//   StampTimeAndRequestEntity. `self` is the record providing the +4 match key.
void CancelMatchingActors(HeRecord* self);

// gilde.exe 0x4f4114 — VIBE_Event_NewDepositMessageBoxRun (h@eax).
//   Message-box coroutine keyed off He+112(=dword index 28)+2:
//     0/1 -> destroy the event panel slot, free.
//     2 -> create the panel slot (text 1424), select its window, render the rich
//          "new deposit found" string with the deposit text id (2*He+172+2151),
//          ++counter.
//     3 -> when the active window is this slot's window and the active message is
//          1210 (OK), destroy the slot and free.
//   Returns the original eax (the rich-string result / free result).
i32 NewDepositMessageBoxRun(HeRecord* h);

// gilde.exe 0x4f4fbc — VIBE_Event_RequestSlotResultRun (h@eax).
//   Slot-request poll coroutine keyed off He+112(=index 28)+2:
//     0/1 -> free.
//     2 -> once the appointment (+82) is due, queue a request-17 for the stored
//          object (He+178 high word), store the handle (index 50), ++counter.
//     3 -> poll the packet: status 2 -> free; status nonzero -> resolve the seq,
//          seed the scan window (index 48=32, 46=seq obj, 49=0), advance 1 minute,
//          ++counter.
//     4 -> loop while the appointment is in the past: resolve the entity, advance
//          1 minute; when the scan cursor (49) passes the window (48), build the
//          model name and free.
//   Returns the last computed value. Entity/packet plumbing routes through hooks.
i32 RequestSlotResultRun(HeRecord* h);

// gilde.exe 0x4f5270 — VIBE_Event_UpdateListenerFromActor (actor@eax, h@edx).
//   Read the actor transform (floats at index 33/34/35), bias x by +1.47, set the
//   3D sound listener from the actor's forward vectors (index 19), then decrement
//   the He countdown byte at +208. `actor` points at the float transform; the
//   listener push routes through a hook. Returns the listener-set result.
i32 UpdateListenerFromActor(const float* actor, HeRecord* h);

// ===========================================================================
// Predicates (command-filter callbacks)
// ===========================================================================

// gilde.exe 0x596074 — VIBE_Event_MatchPersonState15Cmd272 (cmd@eax).
//   True iff the command kind (*cmd) is 25, its person ptr (cmd+4) is set, that
//   person's state byte (building-table[589*charId]) is 15, and cmd+12 == 272.
//   `personState` is the resolved state byte; supplied via the host (the original
//   reads dword_13CE294 + 589 * *personPtr). cmd is a raw command record.
bool MatchPersonState15Cmd272(const void* cmd, i32 personState);

// gilde.exe 0x5960c0 — VIBE_Event_MatchType26OrJump (cmd@eax).
//   Returns 1 iff the command kind (*cmd) is 26 AND cmd+4 (person ptr) is set;
//   otherwise the original tail-jumps to 0x595E70 (the generic predicate). We
//   return 0 for that fall-through (the caller treats nonzero as "matched").
bool MatchType26OrJump(const void* cmd);

} // namespace guild::world

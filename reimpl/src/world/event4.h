#pragma once
// event4 — a further, previously-untranslated slice of the world-event "He"-action
// bodies of the Guild simulation (gilde.exe VIBE_Event_* family). These are the
// larger per-event "Run" phase machines the handler-pool scheduler ticks once an
// event fires: the harvest wage/process bodies, the production "work" body, the
// night-watchman time announcer, the conversation/collapse coroutine, and the
// building-visibility ("He"-state) update + query walkers.
//
// Companion files: event2.{h,cpp} / event3.{h,cpp} (earlier He-action slices),
// event_effects.* / event_fire.* / event_bindings.* / event.* . This file picks a
// fresh set of UNTRANSLATED machines; see the per-function provenance below.
//
// Shared state reused (NOT redefined here — ODR):
//   * the global game clock qword_13CE852 — guild::sim::NpcClock().
//   * GameTime arithmetic — guild::sim::GameTimeAdvance / Compare (sim/gametime.h).
//   * the FPU trunc-toward-zero — guild::util::ConvertX (util/coord.h).
//   * the CRT LCG — guild::util::RandomModulo (util/math_random.h).
//   * StrChrLast (== VIBE_Util_StrChr @0x5d3ef0) — guild::util::StrChrLast.
//
// Records are touched by the exact original raw byte offsets (mirrors the
// decompiler's *(T*)(base+off)); see sim/he.h. The phase counter the "Run"
// machines read is the dword at He+112 (a1[28]); the originals compute
// `a1[28] + 2` and switch on the raw value, so counter -2/-1 map to the teardown
// cases (free the handler). We preserve that decode verbatim.
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::world {

using guild::sim::HeRecord;
using guild::sim::GameTime;

// ===========================================================================
// Leaf hooks — side effects / queries that cross into clusters this file does
// not own (Character action table, GameObject resolve/query, Command queue,
// Person/Script, Voice, Building, Universe/SceneGraph, Text/UI...). Passing
// nullptr to SetEvent4Hooks installs an inert default: every effect a no-op,
// every "find/resolve" returns null/0, every queue returns -1, every iterator
// ends immediately, every table read returns 0. Tests install scripted mocks.
//
// This struct is DISTINCT from event2's EventHooks and event3's Event3Hooks
// (different callee set) to avoid any ODR clash; the shared clock is
// sim::NpcClock(), not duplicated here.
// ===========================================================================
struct Event4Hooks {
    // VIBE_He_FreeHandlerEntry(record,...) — release the handler entry (teardown).
    // Returns the original eax low byte; inert default returns 0.
    i32 (*freeHandlerEntry)(HeRecord* h);

    // --- Character-action cancel table (dword_12CEA8C / word_12CE910) ----------
    // The "cancel my actions" loop walks all 768 person slots; for each whose
    // active-action handler (dword_12CEA8C[p]) equals THIS record, it re-invokes
    // VIBE_Character_ChangePlayerAction(obj,0,0, word_12CE910[p]). We expose the
    // table as two queries + the change-action emit.
    //   activeActionHe(person)  : dword_12CEA8C[person]  (the driving He record, as void*)
    //   personActionCharId(person) : word_12CE910[person] (the action/char id)
    void* (*activeActionHe)(int person);
    u16   (*personActionCharId)(int person);
    void  (*changePlayerAction)(void* obj, void* b, void* c, u16 charId);

    // --- GameObject resolve/iterate (VIBE_GameObject_*) -----------------------
    // ResolveEntityById(outA, outB, id, z): resolves an entity; writes the record
    // pointer to *outA (the "v17"/"v22" slot) and/or the seq/base to *outB.
    void  (*resolveEntityById)(void** outA, i32* outB, i32 id, i32 z);
    // QueryFind(scene, a, b, c) -> matching scene node (or null); IterNext() -> next.
    void* (*queryFind)(i32 scene, i32 a, i32 b, i32 c);
    void* (*queryIterNext)();
    // CollectMatchingProts(rec): pre-scan side effect; inert no-op.
    void  (*collectMatchingProts)(void* rec);

    // --- Command queue emits --------------------------------------------------
    //   VIBE_Command_QueueRequest17(handle, a, qty, objId, flag, z) -> packet handle.
    i32   (*queueRequest17)(i32 handle, i32 a, i32 qty, i32 objId, i32 flag, i32 z);
    //   VIBE_Command_QueueRequestCoord27(handle, memberId, mode).
    void  (*queueRequestCoord27)(i32 handle, i32 memberId, i32 mode);

    // --- Text / messaging -----------------------------------------------------
    //   VIBE_Text_RenderFormattedMessage(buf, fmtId, textId, rec, qty).
    void  (*renderFormattedMessage)(char* buf, i32 fmtId, i32 textId, void* rec, i32 qty);
    //   VIBE_He_SendQuickjumpMessage(...) — collapsed: inert no-op.
    void  (*sendQuickjumpMessage)(i32 dest, const char* buf, i32 a, const char* tag);

    // --- Building / production / AI -------------------------------------------
    //   VIBE_Building_SumWorkstationByCategory(rec, a, b) -> count.
    i32   (*sumWorkstationByCategory)(void* rec, i32 a, i32 b);
    //   VIBE_Production_ComputeOutputOverTime(timeBlock, clock) -> produced units.
    i32   (*computeOutputOverTime)(const GameTime* timeBlock, const GameTime* clock);
    //   VIBE_Ai_AverageObjectFavorability(cityIdx, n, members) -> favorability.
    double (*averageFavorability)(u16 cityIdx, i32 n, const i32* members);
    //   command-handle for a person/city index (dword_12CE914[134*cityIdx]).
    i32   (*personCommandHandle)(u16 cityIdx);
    //   VIBE_Building_FindById(id) -> building record (or null).
    void* (*findBuildingById)(i32 id);

    // --- Voice (night watchman) ------------------------------------------------
    //   VIBE_Voice_PlayQueuedSample(speaker, a, b, name, prio).
    void  (*playQueuedSample)(i32 speaker, i32 a, i32 b, const char* name, i32 prio);
    // The two global gates the watchman checks (byte_63CC40 audio-enabled,
    // byte_1233569 announce-enabled). Inert defaults: audio off -> no samples.
    i32   (*audioEnabled)();      // byte_63CC40
    i32   (*announceEnabled)();   // byte_1233569

    // --- Person / Script / Conversation (ConversationSinkRun) -----------------
    void* (*findPersonById)(i32 id);          // VIBE_Person_FindRecordById
    void  (*scriptFinishByHandle)(i32 handle);// FindByHandle + Finish (collapsed)
    void  (*characterStandUp)(void* personScript);
    // VIBE_CharAction_InsertActionVararg(personScript, 54) then copy the action
    // string "bewegung/zu_boden_sinken" into +240; we model the whole effect as one.
    void  (*insertCollapseAction)(void* personScript);
    void* (*selectConversationTarget)(void* person); // VIBE_AiMethod_SelectConversationTarget
    void  (*broadcastFamilyNews)(void* person, void* target, i32 ctx);
    i32   (*findEmploymentRelation)(void* person);
    void  (*queueRequestPair33)(i32 a, i32 rel);
    i32   (*packetStatus)(i32 handle);        // VIBE_Command_GetPacketStatusById
    i32   (*packetSeqValue)(i32 handle);      // *(VIBE_Command_GetPacketSeqById(h))
    i32   (*queueRequest39)(const void* req); // VIBE_Command_QueueRequest39
    void* (*cutsceneFindSlotById)(i32 id);    // VIBE_Cutscene_FindSlotById

    // --- Universe / SceneGraph (building He-state walkers) --------------------
    // Switch the active universe slot; returns the previous slot (used as save/restore).
    i32   (*universeSwitchSlot)(i32 slot);
    // Collect the scene nodes named "he_%i" for the building's He id; the engine
    // walks the tree (VIBE_SceneGraph_WalkAndInvoke + AppendCollectedHandle) into a
    // 64-entry list. We model it as: fill `out` with up to `cap` node pointers,
    // return the count. The node "name" + its level number are queried separately.
    int   (*collectHeNodes)(i32 heId, void** out, int cap);
    // Per-node queries the walkers use:
    //   nodeStateByte(node)  : *(u8*)(node+535)   (5 == "hidden/disabled" state)
    //   nodeFlags(node)      : *(u8*)(node+531)   (bit 2 == 0x04 detached)
    //   nodeLevelCount(node) : how many "_<level>" numeric segments the name has
    //   nodeLevel(node, k)   : the k-th parsed level number ([124+4k] in the buffer)
    i32   (*nodeStateByte)(void* node);
    i32   (*nodeFlags)(void* node);
    int   (*nodeLevelCount)(void* node);
    i32   (*nodeLevel)(void* node, int k);
    // Mutations the walkers apply to a node:
    void  (*nodeSetStateByte)(void* node, i32 v);        // *(u8*)(node+535) = v
    void  (*nodeClearDetachedFlag)(void* node);          // *(u8*)(node+531) &= ~4
    void  (*nodeSetLevelBase)(void* node, i32 v);        // *(i32*)(node+536) = v
    void  (*sceneRemoveMesh)(void* node);                // RemoveMeshFromTree (if root set)
    void  (*universeRestoreObjectStates)(void* node, i32 mode); // RestoreObjectStates
    void  (*objectDetachAndRelease)(void* node);         // VIBE_Object_DetachAndRelease
    // Resolve the building He-root object handle for "he_%i" (VIBE_Object_FindByHandle).
    void* (*findHeRootObject)(i32 heId);
};

void SetEvent4Hooks(const Event4Hooks* hooks);
const Event4Hooks& GetEvent4Hooks();

// Recovered float / double constants.
//   flt_6476FC — morning announce hours per season (== event3 kSeasonAnimBase): 8,7,8,9.
//   flt_64770C — evening announce hours per season: 20,21,20,19.
extern const float kMorningHour[4];   // flt_6476FC
extern const float kEveningHour[4];   // flt_64770C
constexpr double kWorkRateMul    = 0.01;  // dbl_620100
constexpr double kFavorBias      = -0.5;  // dbl_620108
constexpr double kFavorScale     = 0.25;  // dbl_620110

// Season index from a game day (VIBE_GameTime_GetSeasonFromDay 0x58339c == day % 4).
inline int SeasonFromDay(i32 day) { return static_cast<int>(day % 4); }

// ===========================================================================
// Phase machines / "Run" bodies
// ===========================================================================

// gilde.exe 0x4f3b34 — VIBE_Event_HarvestWageRun (h@eax).
//   Phase machine keyed off He+112 (a1[28]):
//     -2/-1 -> cancel my char actions (the 768-slot table loop), free.
//      0    -> ++counter.
//      1    -> if neither work-amount field (+20/+24) is set, free; else once the
//              appointment (+82) is due (Compare(+82, clock) < 0), resolve the
//              wage entity (+176 word>>16 names a building-type row), draw a wage
//              = (amt20+amt24) * (40*base + RandomModulo(40*base)) where base is the
//              row's word at +54, queue a request-17 crediting the wage, render +
//              send the "_NACHRICHTEN_HS_07" message, cancel my actions, free. If
//              the entity is missing, set counter := -1.
//   Returns the original eax (counter+2 / Advance / free result). The entity
//   resolve, queue, message, table read and cancel route through hooks.
i32 HarvestWageRun(HeRecord* h);

// gilde.exe 0x4f3d7c — VIBE_Event_HarvestProcessRun (h@eax).
//   Like HarvestWageRun but multi-good: resolve two entities (+172 wage rec,
//   +176 source), find the first "ready" (+7==1) good node, queue a take-1, scan
//   the city object table (word_13CE860 count) for matching prots (4-slot member
//   gate), queue request-3s, then draw the final wage (30*base each) and free.
//   `produced` paths route through hooks; the member-gate + counter loop are
//   translated. Returns nothing (void original).
void HarvestProcessRun(HeRecord* h);

// gilde.exe 0x4f2614 — VIBE_Event_WorkActionRun (h@eax).
//   The production "work" body. Phase machine keyed off He+112:
//     -2/-1 -> cancel actions, free.
//      0    -> ++counter.
//      1    -> if no work pending (+20/+24 both zero) set counter:=2; else resolve
//              the workstation (+184), compute a base rate = count*0.01 + 1, every
//              time the "tired" byte (+200) >= 60 emit per-member coord-27 commands
//              and recompute the favorability bonus (+196), accumulate production
//              over time, then drain the remaining work (+176) advancing +82 by 5
//              minutes per step until due; on depletion render the "done" message,
//              cancel actions, free.
//      2    -> if work re-appeared set counter:=1; else resolve, if present cancel
//              + free, else counter:=-1.
//   Returns the original eax. Production/favorability/queue route through hooks.
i32 WorkActionRun(HeRecord* h);

// gilde.exe 0x4f0250 — VIBE_Event_NightWatchmanAnnounceRun (h@eax).
//   Only runs while the saved day (+68) matches the clock day. Phase machine:
//     -2/-1 -> free.
//      0 (morning) -> if the clock hour is within [morningHour[season],+1): if
//              audio on, play the bell; if announce on and RandomModulo(10)>7,
//              play the announce + the per-hour morning sample (7/8/9, the
//              autumn variant when season==2) + maybe the "spruch". Stamp +82,
//              set +86 := (int)eveningHour[season], +88 := 0, ++counter. Else
//              advance +82 by +1 day (LABEL_23).
//      1 (evening) -> symmetric with eveningHour[season] and the evening samples
//              (19/20/21); on success +86 := 22, +88 := 30, ++counter.
//      2 (night) -> if hour>=22 and minute in [30,50): play the bell, and if
//              announce on and RandomModulo(10)>7 play the night-end sample, free;
//              else free.
//   void original. Voice + the two audio gates route through hooks; the clock
//   reads use NpcClock().
void NightWatchmanAnnounceRun(HeRecord* h);

// gilde.exe 0x4efd88 — VIBE_Event_ConversationSinkRun (h@eax).
//   The "sit / collapse / talk" conversation coroutine. Front gate: resolve the
//   person (+172); if absent free. If the person has no live script (rec+520==-1)
//   or counter==6, run the phase machine; otherwise just re-stamp +82 +5 min.
//   Phases:
//     -2/-1 -> free.
//      0 -> if the person has a script slot (rec+388), finish any running script,
//           play the sit action, clear rec+364, ++counter; else if flag&2 set
//           counter:=3, else free.
//      1 -> wait until the person's script (rec+388)'s +40 clears, then ++counter.
//      2 -> stand the person up, insert the "zu_boden_sinken" collapse action,
//           stamp +82 +30 min, ++counter; if flag&4 free.
//      3 -> if the person's class byte (rec+2) is 6 or 7, queue a request-39 into
//           +180, advance +82 +1 min, counter:=6; else advance +82 +2 min, counter:=4.
//      4 -> pick a conversation target; if found broadcast family news, queue a
//           pair-33, free; else counter:=5.
//      5 -> broadcast the default family news (dword_6498E4), queue pair-33, free.
//      6 -> poll the request-39 packet: status 2 -> counter:=4; nonzero -> store
//           the seq into +176, stamp +82, counter:=7.
//      7 -> if person+8 set free; else if the cutscene slot (+176) resolves advance
//           +82 +2 days; else clear +176/+180, stamp +82, counter:=4.
//   Returns the original al. All cross-cluster calls route through hooks.
i32 ConversationSinkRun(HeRecord* h);

// ===========================================================================
// Building visibility ("He"-state) walkers
// ===========================================================================

// gilde.exe 0x4f56f8 — VIBE_Event_QueryBuildingHeMax (h@eax).
//   Switch to slot 0, collect the "he_%i" scene nodes for the building's He id
//   (h+4), then for each node whose name parses to exactly... (the original counts
//   the numeric "_<n>" segments and, when at least one is present, takes the max of
//   the node's level field across all collected nodes). Returns that max level (0 if
//   none). Restores the previous slot. The scene collection + name parse route
//   through hooks (collectHeNodes / nodeLevelCount / nodeLevel).
i32 QueryBuildingHeMax(HeRecord* h);

// gilde.exe 0x4f52f8 — VIBE_Event_UpdateBuildingHeState (h@eax).
//   Switch to slot 0, resolve the building He-root object, then drive the visible
//   construction level of its "he_%i" nodes from the current/target level fields
//   (h[49] current, h[50] target):
//     target == -1 -> hide every node (state:=5, clear detached, drop mesh),
//                     restore object states, done.
//     target  < current -> for each node: ensure hidden; parse its level segments;
//                     if exactly one segment, show it iff level <= target; if two,
//                     show iff in [target's range]; restore each node accordingly.
//     target  > current -> reveal the next building's root (FindById(h[46])) and
//                     detach/release the current root.
//   void/al original. Universe + scene plumbing route through hooks; the
//   level-compare decisions are translated.
void UpdateBuildingHeState(HeRecord* h);

} // namespace guild::world

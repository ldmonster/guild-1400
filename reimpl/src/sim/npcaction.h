#pragma once
// NpcAction — the NPC behavior state-machine layer for the Guild simulation
// (gilde.exe, VIBE_NpcAction_* family, 157 functions). Each NpcAction function is
// a step of a per-NPC behavior coroutine that operates on a He/handler record
// (see he.h) and emits network commands via the cmd29/cmd-request channel. The
// dispatcher (VIBE_NpcAction_Dispatch @0x5766a0) selects the step by the action
// type stored at He+4-relative slot (+4 word) through a 69-entry jump table
// (funcs_5766CB @0x63d964).
//
// This module translates a representative, self-contained subset (timestamp/
// appointment helpers, the walk-begin family, the social greet/flirt/compliment
// trio, accident-state resets, idle-anim arming, the neighbor-scan economy step,
// and the relation-by-mood adjuster) plus the dispatch table + registration. The
// huge render/pathfinder/cutscene-coupled steps are forward-declared as deferred
// (see the DEFERRED list at the bottom of npcaction.cpp).
//
// Leaf calls into the command / inventory / law / text clusters are routed through
// NpcLeafHooks so the behaviors are exercisable in isolation (tests install a
// recording mock; the real game installs the cluster bridge). The state-machine
// control flow inside each function is translated faithfully; only the leaf
// side-effects are indirected.
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::sim {

// ===========================================================================
// Global game clock (gilde.exe qword_13CE852 @0x13CE852, plus the adjacent
// unk_13CE85A/unk_13CE85E dwords/word that complete the 14-byte time image).
// The step functions copy this whole 14-byte block into the record's +82/+176
// slots. We model it as a single module-global GameTime the host/test sets.
// ===========================================================================
GameTime& NpcClock();
void SetNpcClock(const GameTime& t);

// ===========================================================================
// Leaf hooks — side effects that cross into other clusters.
// ---------------------------------------------------------------------------
// The originals call VIBE_Command_QueueRequestEntity29, VIBE_He_FreeHandlerEntry,
// VIBE_Inventory_FindSlotByItemId, VIBE_Law_GetBaseTextId,
// VIBE_Util_InitAndShuffleDwordArray, VIBE_Npc_AdjustRelationByMood's command
// emit, etc. Tests install a recording mock; passing nullptr installs an inert
// default (every effect a no-op, queries return "absent").
// ===========================================================================
struct NpcLeafHooks {
    // VIBE_Command_QueueRequestEntity29(arg, record) — queue a cmd29 entity
    // request for `h`; returns the packet handle (stored into He+132). `arg` is
    // the leading byte argument the callers pass (-1/0/1/2/3).
    i32 (*queueRequestEntity29)(int arg, HeRecord* h);
    // VIBE_He_FreeHandlerEntry(record, edi, esi) — release the handler entry
    // (the NPC's appointment is done). Returns the original's result code.
    i32 (*freeHandlerEntry)(HeRecord* h);
    // VIBE_Command_GetPacketStatusById(handle) — nonzero once the packet has been
    // applied/acked (used to gate re-arming). 0 == still pending.
    i32 (*packetStatus)(i32 handle);
    // VIBE_Law_GetBaseTextId() — base text id seed for timed events.
    i32 (*lawBaseTextId)();
    // VIBE_Inventory_FindSlotByItemId(itemId, &count) — returns nonzero (found)
    // and writes the slot's count into *count; 0 == not present.
    int (*findInventorySlot)(int itemId, i32* count);
    // VIBE_Util_InitAndShuffleDwordArray(n, dst) — fill dst[0..n-1] with a shuffled
    // 0..n-1 permutation (Fisher-Yates over the CRT RNG in the original).
    void (*shuffleDwords)(int n, i32* dst);
    // VIBE_Npc_AdjustRelationByMood's emit leaf: VIBE_Command_RequestBuildOp93.
    // Records a relation-mood delta command (id, kind, id, amount).
    void (*requestBuildOp93)(i32 id, int kind, i32 id2, u8 amount);
};

void SetNpcLeafHooks(const NpcLeafHooks* hooks);
const NpcLeafHooks& GetNpcLeafHooks();

// Installs the city-index -> city-id resolver used by SetTargetCityRef (the
// original reads the Person id column dword_12CE914[134*index]; entity.cpp owns
// that array). When unset, SetTargetCityRef echoes the index.
void SetNpcCityIdResolver(i32 (*fn)(u16 index));

// ===========================================================================
// Dispatch + registration  (VIBE_NpcAction_Dispatch @0x5766a0,
// jump table funcs_5766CB @0x63d964 — 69 entries, indices 0..0x44).
// ===========================================================================

// Step function signature: takes the He record, returns the original's eax. The
// callers ignore the result except for the dispatcher's sentinel codes.
using NpcActionStepFn = i32 (*)(HeRecord* h);

// gilde.exe 0x5766a0 — VIBE_NpcAction_Dispatch.
//   if (gameClock.day < 8) return 0;          (debug guard: VIBE_DebugCmd_RetZero)
//   if (!h) return -1;
//   type = h[+4 word];  if (type >= 0x45) return -2;
//   return jumpTable[type](h, type);
// Returns the step's result, or the sentinel -1/-2/0 per the guards above.
i32 NpcAction_Dispatch(HeRecord* h);

// Number of entries in the dispatch jump table (funcs_5766CB).
constexpr int kNpcActionTableSize = 69;   // indices 0..0x44

// The recovered jump-table addresses (absolute, imagebase 0x400000) in order.
// Exposed so a test can assert the table is byte-faithful to funcs_5766CB.
extern const u32 kNpcActionTableAddrs[kNpcActionTableSize];

// Returns the registered step function for `type` (0..68), or nullptr if the
// type is out of range or maps to a not-yet-translated (deferred) entry.
NpcActionStepFn NpcAction_TableEntry(int type);

// ===========================================================================
// Translated step functions (representative, self-contained set).
// ===========================================================================

// --- appointment / timestamp helpers ---

// gilde.exe 0x4c9458 — VIBE_NpcAction_StampTimeAndRequestEntity.
//   Copies the clock into +82; if flag 0x02 set, queues a cmd29 entity request.
HeRecord* NpcAction_StampTimeAndRequestEntity(HeRecord* h);

// gilde.exe 0x4e5b10 — VIBE_NpcAction_CopyTargetCoord.
//   Copies the saved timestamp (+68) into the appointment slot (+82).
HeRecord* NpcAction_CopyTargetCoord(HeRecord* h);

// gilde.exe 0x4e5c00 — VIBE_NpcAction_ClearTargetCoord.
//   Copies the clock into the appointment slot (+82). (No advance.)
HeRecord* NpcAction_ClearTargetCoord(HeRecord* h);

// gilde.exe 0x4c9484 — VIBE_NpcAction_SetTargetCityRef.
//   Stores `cityIndex` at +8; resolves it to a city id at +12 (via the Person id
//   column), or -1 if cityIndex == 0xFFFF.
i32 NpcAction_SetTargetCityRef(HeRecord* h, u16 cityIndex);

// gilde.exe 0x4cc018 — VIBE_NpcAction_ResetToState0.
//   Stamps the clock into +82, advances +2 days, clears state (+112).
int NpcAction_ResetToState0(HeRecord* h);

// gilde.exe 0x4cdcc0 — VIBE_NpcAction_AddTimeToActionDuration.
//   Stamps the clock into +82, adds RandomModulo(21-hour) to the hour field
//   (+86), zeroes minute (+92), and sets +88 := RandomModulo(0x3B). Returns +88.
int NpcAction_AddTimeToActionDuration(HeRecord* h);

// --- walk-begin family (arm an appointment N units ahead) ---

// gilde.exe 0x4e6d00 — VIBE_NpcAction_BeginWalkAndFace.   (+82, advance +1 second)
int NpcAction_BeginWalkAndFace(HeRecord* h);
// gilde.exe 0x4e7394 — VIBE_NpcAction_BeginWalkPhase4.    (+82, advance +4 days)
int NpcAction_BeginWalkPhase4(HeRecord* h);
// gilde.exe 0x4ec6b0 — VIBE_NpcAction_BeginGenericWalkStep. (+82, advance +1 sec)
int NpcAction_BeginGenericWalkStep(HeRecord* h);
// gilde.exe 0x4ed910 — VIBE_NpcAction_BeginCombatWalkStep.  (+82, advance +1 sec)
int NpcAction_BeginCombatWalkStep(HeRecord* h);

// --- idle anim arming ---

// gilde.exe 0x4e7538 — VIBE_NpcAction_BeginIdleAnim.
// gilde.exe 0x4eaeb0 — VIBE_NpcAction_BeginIdleAnimAlt.  (byte-identical twin)
//   Stamps the clock into +82, advances +1 day and +(RandomModulo(30)-20) minutes.
//   NOTE: the original passes an *uninitialised* ecx as the addSeconds argument
//   (a latent bug); the reconstruction passes 0 there (documented; the minute
//   delta is the meaningful one).
int NpcAction_BeginIdleAnim(HeRecord* h);

// gilde.exe 0x4c9d58 — VIBE_NpcAction_BeginIdleWaitState.
//   If not already spawned (flag 0x04): stamps the clock into +82 (+1 day), sets
//   the +176 deadline (+24h) with the wait counter (+180) := RandomModulo(4)+19,
//   state (+112) := 1, and queues a cmd29 entity request (handle -> +132).
i32 NpcAction_BeginIdleWaitState(HeRecord* h);

// --- social greet/flirt/compliment trio ---

// gilde.exe 0x56850c / 0x568578 / 0x5685e4 — resolve the conversation target
// (an office for guild-rank-6 NPCs, else the Person record) and apply a mood
// delta (greet:+1, flirt:+3, compliment:+4). Returns 1 on success, 0 if no
// target. The mood delta is applied through NpcAdjustRelationByMood.
int NpcAction_ResolveTargetAndGreet(HeRecord* h, HeRecord* target);
int NpcAction_ResolveTargetAndFlirt(HeRecord* h, HeRecord* target);
int NpcAction_ResolveTargetAndCompliment(HeRecord* h, HeRecord* target);

// gilde.exe 0x56840c — VIBE_Npc_AdjustRelationByMood(person, kind).
//   Applies a randomized relation/mood increment to person[+128+kind] (a per-mood
//   "relationship" byte, capped at a ceiling), and if the resulting delta is
//   nonzero emits a RequestBuildOp93 command. Returns 0 if kind>=5 or the byte is
//   already at the ceiling, else 1. Uses the recovered float constants below.
int NpcAdjustRelationByMood(HeRecord* person, i8 kind);

} // namespace guild::sim

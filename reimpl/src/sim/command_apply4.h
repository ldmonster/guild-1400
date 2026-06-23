#pragma once
#include "guild/common/types.h"
#include "sim/command.h"
#include "sim/types.h"

// gilde.exe — Command APPLY handlers, FOURTH batch (namespace guild::sim).
//
// A fourth, NON-overlapping set of the 96-entry dispatch jump table
// (funcs_4941F4 @0x631298, invoked from VIBE_Command_ExecCommands @0x494088).
// The first three batches own:
//   batch1 (command_apply):  0x16 0x17 0x18 0x19 0x1A 0x24 0x25 0x41 0x56 0x5A
//                            0x5B 0x5D
//   batch2 (command_apply2): 0x0D 0x0E 0x0F 0x10 0x1D 0x2B 0x3F 0x44 0x45 0x46
//                            0x5C 0x5E
//   batch3 (command_apply3): 0x27 0x28 0x29 0x2A 0x2E 0x2F 0x30 0x31 0x32 0x33
//                            0x34 0x35 0x36 0x37 0x3D 0x3E 0x43 0x48 0x49 0x4A
//                            0x4B 0x50 0x51 0x52 0x55 0x59 0x5F
// This file owns a fourth disjoint set — the lightweight ack/flag/loadbuf
// handlers, the object move/transform/effect handlers, the Straftat (crime)
// table handlers, the cut-info slot handler, and the person/character lifecycle
// handlers — registering ONLY those, so all four registries compose without
// clobbering each other:
//
//   trivial / framing-acks
//   0x01 ExHandleAck            49 6474  stamp ack=1
//   0x02 ExHandleNoop           49 6484  no-op (returns 1, no ack)
//   0x04 ExSetGlobalFlag        49 648C  byte_63CC28 |= payload[+0]
//   0x08 ExLoadBufAlloc         49 64A4  alloc the cm_LoadBuf staging buffer
//   0x09 ExLoadBufAppend        49 64D8  append 0x80 bytes into cm_LoadBuf
//   0x1F ExAckStub              49 8A4C  Hud-mode probe + ack=1
//
//   object create / transform / effect (object-stock leaves)
//   0x15 ExSetObjectTransform   49 7B8C  AddObjekt + copy 0x1C transform bytes
//   0x2D ExSpawnEffectObject    49 9854  AddObjekt(prot 437) effect + transform
//   0x40 ExAdjustObjectTransform 49 BC50  Building slot: add 7 transform deltas
//
//   Straftat (crime) table (dword_11BC760, 45-byte stride, the crime records)
//   0x22 ExAddStraftat          49 8F44  find free slot, copy 0x2D crime record
//   0x23 ExDispatchStatusResult 49 91B0  Straftat_ResolveAndClear wrapper
//   0x3C ExQueryObjectStatus    49 B610  Straftat_UpdateMatchingRecords wrapper
//
//   cut-info slots (dword_11AB000, 215-dword stride, 16 player slots)
//   0x26 ExSendCutInfo          49 9288  find/alloc a player's cut-info slot
//
//   person / character lifecycle (Person record + character/script leaves)
//   0x21 ExRemovePersonAndScript 49 8EAC  stop player action, finish script, remove
//   0x38 ExSetObjectParent      49 AE60  Building_SetObjectParent(leaf)
//   0x39 ExMoveObjectToRoom     49 AF00  re-parent person to a room, fix counts
//   0x42 ExActivateObject       49 BCF0  spawn chimney smoke for a production geb
//   0x47 ExChangePlayerHead     49 BE14  Character_ChangePlayerAction(head)
//   0x4D ExStopCharacterScript  49 C580  finish a person's active script
//   0x4E ExEnqueueCharacterAction 49 C5CC  queue a char action (vararg)
//   0x57 ExDestroyCharacter     49 CEEC  destroy a person's live character
//   0x58 ExClearObjectOccupants 49 CF40  clear a cutscene slot's participants
//
// Common ABI (recovered from the originals, all __usercall): packet record base
// -> CommandPacket&, payload at +0x10; the 10-byte ACK/status entry -> AckEntry*
// (may be null). Handlers stamp it: status 2 = in-progress, 1 = applied; +1 =
// sub-tag, +6 = result ptr/id (we keep the exact sub-tags). Return 0 == applied,
// nonzero == target not found / rejected (the dispatcher ignores the value).
//
// The -2/-3/-4 "last-created object/scene/trade id" remap tokens
// (dword_631288/63128C/631290) are the SAME globals batch 1 exposes
// (g_lastObjectId / g_lastSceneId / g_lastTradeId in command_apply.h); we reuse
// them. ExSetObjectTransform / ExSpawnEffectObject write g_lastTradeId
// (dword_631290), mirroring the originals.
//
// Deeply-coupled leaves (render/scene/path/script/character actor, person create,
// city placement, He/Beweis evidence) are reached through mockable
// function-pointer hooks declared below with faithful default backends that
// model just the observable record mutation. The full leaf functions are listed
// as DEFERRED in the module report.

namespace guild::sim {

// ---------------------------------------------------------------------------
// Opcode -> handler constants for THIS batch (jump-table indices @0x631298).
// ---------------------------------------------------------------------------
enum ApplyOpcode4 : u8 {
    kOp4HandleAck            = 0x01, // 1
    kOp4HandleNoop           = 0x02, // 2
    kOp4SetGlobalFlag        = 0x04, // 4
    kOp4LoadBufAlloc         = 0x08, // 8
    kOp4LoadBufAppend        = 0x09, // 9
    kOp4SetObjectTransform   = 0x15, // 21
    kOp4AckStub              = 0x1F, // 31
    kOp4RemovePersonAndScript= 0x21, // 33
    kOp4AddStraftat          = 0x22, // 34
    kOp4DispatchStatusResult = 0x23, // 35
    kOp4SendCutInfo          = 0x26, // 38
    kOp4SpawnEffectObject    = 0x2D, // 45
    kOp4SetObjectParent      = 0x38, // 56
    kOp4MoveObjectToRoom     = 0x39, // 57
    kOp4QueryObjectStatus    = 0x3C, // 60
    kOp4AdjustObjectTransform= 0x40, // 64
    kOp4ActivateObject       = 0x42, // 66
    kOp4ChangePlayerHead     = 0x47, // 71
    kOp4StopCharacterScript  = 0x4D, // 77
    kOp4EnqueueCharacterAction = 0x4E, // 78
    kOp4DestroyCharacter     = 0x57, // 87
    kOp4ClearObjectOccupants = 0x58, // 88
};

// ===========================================================================
// Module-global engine state the originals mutate (the game's file globals).
// Held here as the fourth batch's owned state; tests reset via ResetApply4State.
// ===========================================================================

// byte_63CC28 @0x63CC28 — packed "global flags" byte. ExSetGlobalFlag ORs into
// it; ExSysMessage (deferred) clears/sets bits 3/4. Exposed for verification.
extern u8 g_globalFlags; // byte_63CC28

// cm_LoadBuf staging buffer (dword_11AA48C base, dword_11AA490 size,
// dword_11AA464 write cursor). ExLoadBufAlloc records the requested size and
// resets the cursor; ExLoadBufAppend copies 0x80 bytes and advances the cursor
// by 128. We model the buffer contents so an append round-trips.
constexpr int kLoadBufCapacity = 0x4000; // 16 KiB model bound
extern u8  g_loadBuf[kLoadBufCapacity]; // dword_11AA48C target memory
extern i32 g_loadBufSize;               // dword_11AA490 requested size
extern i32 g_loadBufCursor;             // dword_11AA464 write cursor

// Straftat (crime) record table (dword_11BC760, 45-byte stride). The originals
// keep an unbounded table; we model a fixed pool. Each record: +0 dword crime id
// (st_id), then 0x29 more payload bytes copied from the packet, and +22 (offset
// 0x16, dword_11BC776) the He filter key the evidence loop matches.
//
// Sized at 512 slots (23040 bytes) because the He entity-query family
// (he_entity_query.cpp — VIBE_He_CollectPlayerEntitiesByType @0x4c3de4 et al.)
// linearly scans this same table to its hard-coded 23040-byte bound (512 records,
// `off >= 23040`). The earlier 256-slot figure under-sized the shared global; the
// binary's BSS region is the full 23040 bytes. command_apply4's own loops bound by
// kStraftatSlots, so the larger pool is a safe superset.
constexpr int kStraftatStride = 45;
constexpr int kStraftatSlots  = 512;
extern u8  g_straftatTable[kStraftatSlots * kStraftatStride]; // dword_11BC760
extern i32 g_straftatCounter; // dword_632240 (st_id allocator, host-incremented)
// Test/inspection: base of slot `i` (or null if out of range).
u8* Apply4_StraftatSlot(int i);

// Cut-info per-player slot table (dword_11AB000, 215-dword stride, 16 slots).
// Layout used by ExSendCutInfo: slot[+0..+0x35B] is a 0x35C-byte info blob
// (qmemcpy'd from a staging area), then parallel columns:
//   dword_11AB004 (+ slot index) : ready flag (zeroed on (re)alloc)
//   dword_11AB008 : valid flag
//   dword_11AB00C : time (cleared on alloc)
//   dword_11AB010 : owning player id (== payload +16, the lookup key; -1 free)
// We model the 16 slots as a flat array with the four columns the handler reads.
constexpr int kCutInfoSlots = 16;
struct CutInfoSlot {
    i32 playerId;  // dword_11AB010  (-1 == free)
    i32 ready;     // dword_11AB004
    i32 valid;     // dword_11AB008
    i32 time;      // dword_11AB00C
};
CutInfoSlot* Apply4_CutInfoSlot(int i);

// dword_764CE0 == -1 standalone flag mirror (several handlers branch on it to
// decide whether to latch a "current command" global). Default: standalone(-1).
void Apply4_SetStandalone(bool v);
bool Apply4_Standalone();

// ===========================================================================
// Modeled leaf hooks (deferred render/scene/script/character/person leaves).
// Default backends model only the observable record mutation / return code.
// ===========================================================================

// VIBE_GameObject_AddObjekt @0x585af4 — create a new sub-object of `proto` under
// container `containerId` (with `amount`); returns a pointer to a flat object
// record, or null on failure. The transform handlers write the object's
// id (dword @+2 -> g_lastTradeId) and transform bytes. Default backend allocates
// from a small modeled object pool so the apply round-trips; tests can spy.
// The returned record is at least 60 bytes (transform region through +58).
using AddObjektFn = u8* (*)(i32 containerId, i32 proto, i32 amount);
void SetAddObjektHook(AddObjektFn fn);
// Find the object record the last AddObjekt produced for (container, proto), so
// a test can verify the written transform. Null if none.
u8* Apply4_FindAddedObject(i32 containerId, i32 proto);

// VIBE_Building_FindSlotByProt @0x5851fc — locate a building production/transform
// slot for (typeByte, proto); returns a flat int-array slot (>= 15 ints) or null.
// ExAdjustObjectTransform adds deltas to ints [4][5][6] and floats [8][9][11][14].
using SlotFindFn4 = i32* (*)(u8 typeByte, i16 proto);
void SetSlotFindHook4(SlotFindFn4 fn);
i32* Apply4_SeedTransformSlot(u8 typeByte, i16 proto);

// --- Straftat leaves --------------------------------------------------------
// VIBE_Straftat_FindFreeSlot @0x4c3390 — first free crime-record slot index, or
// -1 if the table is full. Default: linear scan of g_straftatTable (free ==
// st_id dword == 0). Tests may override.
using StraftatFreeFn = int (*)();
void SetStraftatFreeHook(StraftatFreeFn fn);
// VIBE_Straftat_ResolveAndClear @0x4c354c — resolve/close a crime by (id, kind);
// returns 0 == applied, 1/2/3 == specific reject codes (propagated by 0x23).
using StraftatResolveFn = int (*)(i32 id, i32 kind);
void SetStraftatResolveHook(StraftatResolveFn fn);
// VIBE_Straftat_UpdateMatchingRecords @0x4c39a4 — bulk-update matching crime
// records; returns the count updated (>0 => ack status 1). Used by 0x3C.
using StraftatUpdateFn = int (*)(i32 a, i32 b, i32 c, i32 d);
void SetStraftatUpdateHook(StraftatUpdateFn fn);

// --- Person / character lifecycle leaves ------------------------------------
// VIBE_Character_ChangePlayerAction @0x4b09c8 — stop/restart a person's action
// (used by remove, change-head). Default: records the call (see CharLifeLog).
using ChangePlayerActionFn = void (*)(i32 personId, int kind);
void SetChangePlayerActionHook(ChangePlayerActionFn fn);
// VIBE_Script_Finish (via FindByHandle) — finish a person's active script.
// Default: records the call. handle == the script handle read from the record.
using ScriptFinishFn = void (*)(i32 handle);
void SetScriptFinishHook(ScriptFinishFn fn);
// VIBE_Building_RemoveAndCleanup @0x5894b0 — remove a person's building.
using BuildingRemoveFn4 = void (*)(i32 personMarker, i32 arg);
void SetBuildingRemoveHook4(BuildingRemoveFn4 fn);
// VIBE_Building_SetObjectParent @0x58820c — set an object's parent person.
// The DEFAULT now performs the disasm-verified record/column slice (the
// dword_12CEA80 workBld-column populater + the bld +0x25/+0x27 owner words +
// the kind-6/7 staff clear; see the .cpp provenance block); the render-coupled
// tail (storage rooms / scene loop / texture set / flag nodes) stays the
// hook's named gap. Args: ownerId = the BUILDING's id (resolved via
// BuildingFindById), newParentMarker = a2 (new parent's marker word),
// childMarker = a3 (the new owner slot; 0xFFFF == none).
using SetObjectParentFn = void (*)(i32 ownerId, i32 newParentMarker, i32 childMarker);
void SetSetObjectParentHook(SetObjectParentFn fn);
// VIBE_Object_SpawnChimneySmoke @0x4b60a0 — production-building smoke effect.
using ChimneySmokeFn = void (*)(i32 personMarker);
void SetChimneySmokeHook(ChimneySmokeFn fn);
// VIBE_Building_IsProductionType @0x587f80 — nonzero if the geb is a production
// type (the smoke is only spawned for NON-production buildings). Default: 0.
using IsProductionFn = int (*)(i32 personMarker);
void SetIsProductionHook(IsProductionFn fn);
// VIBE_Character_Destroy @0x402120 — destroy a person's live character actor.
using CharDestroyFn = void (*)(i32 charPtr);
void SetCharDestroyHook(CharDestroyFn fn);
// VIBE_Character_FindByPredicate @0x402314 + VIBE_CharAction_InsertActionVararg
// @0x40c1e4 — find a live actor by name and queue an action. Default: records
// the call. Returns nonzero if an actor was found (queue happened).
using CharFindAndQueueFn = int (*)(i32 actorId, i32 actionTag, i32 arg0, i32 arg1);
void SetCharFindAndQueueHook(CharFindAndQueueFn fn);

// VIBE_Cutscene_FindSlotById @0x4ac750 + VIBE_Cutscene_RemoveById @0x4ac860 —
// locate a cutscene slot by id (for ExClearObjectOccupants) and remove it. The
// handler reads the slot id (dword @+0), the participant count (byte @+48), and
// up to N participant person ids (dword column @+52, index 13). We model the
// slot via a hook that returns the id, count, and participant id list; a second
// hook records the remove. Default: a small modeled slot table seeded by tests.
struct CutsceneClearSlot {
    i32 id;             // slot id (== record[0])
    int participants;   // record[48] byte
    const i32* ids;     // record participant ids (index 13 column), `participants` long
};
using CutsceneFindFn = const CutsceneClearSlot* (*)(i32 id);
void SetCutsceneFindHook(CutsceneFindFn fn);
using CutsceneRemoveFn = void (*)(i32 id);
void SetCutsceneRemoveHook(CutsceneRemoveFn fn);
// Seed a modeled cutscene slot (ids are copied). Returns false if the table is
// full. The participant person ids must already exist in g_persons for the
// cutscene-id field to be cleared.
bool Apply4_SeedCutsceneSlot(i32 id, const i32* participantIds, int count);

// Observable record of the lifecycle/character leaf calls (so tests can verify
// which command ran on which target). Reset by ResetApply4State.
struct CharLifeLog {
    int  changeActionCount;   // ExRemovePersonAndScript / ExChangePlayerHead
    i32  lastChangeActionId;  // person id of the last ChangePlayerAction
    int  scriptFinishCount;   // ExRemovePersonAndScript / ExStopCharacterScript
    i32  lastScriptHandle;
    int  buildingRemoveCount; // ExRemovePersonAndScript
    int  setParentCount;      // ExSetObjectParent
    int  chimneyCount;        // ExActivateObject
    int  charDestroyCount;    // ExDestroyCharacter
    int  queueActionCount;    // ExEnqueueCharacterAction
    i32  lastActionTag;       // 0x2D / 0x3A frame the queue used
    i32  lastActorId;
};
const CharLifeLog& Apply4_CharLog();

// Reset every modeled table + hook + standalone state to defaults (test helper).
void ResetApply4State();

// ===========================================================================
// Handlers. Each takes the received packet and the dispatcher ACK (may be null).
// ===========================================================================

// gilde.exe 0x496474 — opcode 0x01. Stamp ack=1; return 0.
int ExHandleAck(CommandPacket& pkt, AckEntry* ack);
// gilde.exe 0x496484 — opcode 0x02. No-op; returns 1 (the original's literal 1).
int ExHandleNoop(CommandPacket& pkt, AckEntry* ack);
// gilde.exe 0x49648C — opcode 0x04. g_globalFlags |= payload byte (+0x10); ack=1.
int ExSetGlobalFlag(CommandPacket& pkt, AckEntry* ack);
// gilde.exe 0x4964A4 — opcode 0x08. Alloc the load buffer of size payload(+0x10);
// reset the write cursor to 0; ack=1.
int ExLoadBufAlloc(CommandPacket& pkt, AckEntry* ack);
// gilde.exe 0x4964D8 — opcode 0x09. Copy 0x80 payload bytes (+0x10) into the load
// buffer at the cursor, advance the cursor by 128; ack=1.
int ExLoadBufAppend(CommandPacket& pkt, AckEntry* ack);
// gilde.exe 0x498A4C — opcode 0x1F. Hud-mode probe (no-op here); ack=1.
int ExAckStub(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x497B8C — opcode 0x15. AddObjekt(id +0x10 remapped, proto =
// (dword @+0x12)>>16, amount 1); copy 0x1C transform bytes from +0x16 into
// obj+28; copy word+byte at +0x32 into obj+56/+58; set g_lastTradeId = obj id.
// ack +0=1,+1=3,+6=obj. Returns 1 if AddObjekt fails.
int ExSetObjectTransform(CommandPacket& pkt, AckEntry* ack);
// gilde.exe 0x499854 — opcode 0x2D. AddObjekt(id +0x10 remapped, proto 437,
// amount 1) effect object; set obj+18=64; copy dword+word+byte from +0x18 into
// obj+28/+32/+34; clear obj+34; set g_lastTradeId. ack +0=1,+1=3,+6=obj.
int ExSpawnEffectObject(CommandPacket& pkt, AckEntry* ack);
// gilde.exe 0x49BC50 — opcode 0x40. FindSlotByProt(type +0x10, proto =
// (dword @+0x0F)>>16); add int deltas (+0x13/+0x17/+0x1B) to slot ints [4][5][6]
// and float deltas (+0x1F/+0x23/+0x27/+0x2B) to slot floats [8][9][11][14].
// ack +0=1. Returns 1 if the slot is missing.
int ExAdjustObjectTransform(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x498F44 — opcode 0x22. Allocate a free crime slot, copy 0x2D bytes
// of the crime record from payload +0x10, stamp the st_id (counter or payload
// +0x10) at slot+0, then run the He/Beweis evidence loop (hooked). ack +0=1,+1=6,
// +6=slot. Returns -1 if the table is full.
int ExAddStraftat(CommandPacket& pkt, AckEntry* ack);
// gilde.exe 0x4991B0 — opcode 0x23. Straftat_ResolveAndClear(+0x10, +0x14):
// returns 1/2/3 as a reject code (propagated), else ack=1 / return 0.
int ExDispatchStatusResult(CommandPacket& pkt, AckEntry* ack);
// gilde.exe 0x49B610 — opcode 0x3C. Straftat_UpdateMatchingRecords(int args at
// +0x10/+0x14/+0x1C/+0x18): ack status = (count>0)?1:2. Returns 0.
int ExQueryObjectStatus(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x499288 — opcode 0x26. Find the cut-info slot for player(+0x10); if
// found (or a free slot exists, scanning 16 slots), (re)alloc it: clear ready
// (+0x35C) ... actually clear the ready column. ack +0=1,+1=8,+6=slot. Returns 1
// if all 16 slots are taken by other players.
int ExSendCutInfo(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x498EAC — opcode 0x21. Find the person by id(+0x10); stop its
// player action, finish its active script (record+97 -> handle+40), and remove
// its building (RemoveAndCleanup). ack +0=2 then 1 on hit. Returns 0 always.
int ExRemovePersonAndScript(CommandPacket& pkt, AckEntry* ack);
// gilde.exe 0x49AE60 — opcode 0x38. Query owner person(+0x10); resolve child(+0x18)
// and new-parent(+0x14) persons; SetObjectParent(owner, newParent, child).
// ack +0=2,+1=0. Returns 1 (no owner / no child) / 2 (no new parent).
int ExSetObjectParent(CommandPacket& pkt, AckEntry* ack);
// gilde.exe 0x49AF00 — opcode 0x39. Query owner(+0x10); find child person(+0x14);
// move child to the owner room: bump owner occupancy (+101) if district-limited,
// decrement old parent's occupancy, set child's parent(+92*4)=owner. ack=1.
// Returns 1 on miss / capacity-full.
int ExMoveObjectToRoom(CommandPacket& pkt, AckEntry* ack);
// gilde.exe 0x49BCF0 — opcode 0x42. Query person(+0x10); if it has a building
// (+97) and is NOT a production type, spawn chimney smoke. ack=1. Returns 0.
int ExActivateObject(CommandPacket& pkt, AckEntry* ack);
// gilde.exe 0x49BE14 — opcode 0x47. Find person(+0x10); ChangePlayerAction with
// the head model at +0x14. ack=1. Returns 0.
int ExChangePlayerHead(CommandPacket& pkt, AckEntry* ack);
// gilde.exe 0x49C580 — opcode 0x4D. Find person(+0x10); finish its active script
// (record+97 -> handle+40). ack=1. Returns 0 (1 if the person is missing).
int ExStopCharacterScript(CommandPacket& pkt, AckEntry* ack);
// gilde.exe 0x49C5CC — opcode 0x4E. Find actor(+0x10); queue a char action
// (vararg, args +0x28/+0x2C). ack=1. Returns 1 if the actor is missing.
int ExEnqueueCharacterAction(CommandPacket& pkt, AckEntry* ack);
// gilde.exe 0x49CEEC — opcode 0x57. Find person(+0x10); if it has a live
// character (+97), destroy it and clear record+388. ack +0=2 then 1. Returns 1
// if the person/character is missing.
int ExDestroyCharacter(CommandPacket& pkt, AckEntry* ack);
// gilde.exe 0x49CF40 — opcode 0x58. Find the cutscene slot by id(+0x10) (hook);
// for each participant, clear that person's cutscene-id field (+520) if it
// matches the slot id; remove the slot. ack +0=1. Returns 1 if the slot missing.
int ExClearObjectOccupants(CommandPacket& pkt, AckEntry* ack);

// ---------------------------------------------------------------------------
// Registry. Adds ONLY this batch's opcodes to a CommandQueue dispatch table —
// call it IN ADDITION TO RegisterApplyHandlers / 2 / 3; the four sets are
// disjoint so order does not matter.
// ---------------------------------------------------------------------------
void RegisterApplyHandlers4(CommandQueue& q);

// Apply a single packet directly (bypassing the queue) for this batch's opcodes.
// Unknown/guarded opcodes return -1 without touching state.
int ApplyPacket4(CommandPacket& pkt, AckEntry* ack);

} // namespace guild::sim

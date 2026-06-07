#pragma once
// ===========================================================================
// debugcmd3.{h,cpp} — third wave of the VIBE_DebugCmd_* event-command family
// (gilde.exe, namespace guild::sim). See debugcmd.{h,cpp} for the family intro
// and the shared DebugCmdPerson adapter / DebugCmdHooks plumbing this file
// reuses.
//
// This wave covers the "handler-list" and "entity-search" handlers that the
// earlier waves DEFERRED because they iterate the engine's HE (handler/event)
// list or the object-search subsystem rather than a single person record:
//
//   BroadcastMsgToHandlersA..E  (0x573ebc/0x574388/0x57458c/0x574784/0x5749f8)
//   QueueStateRequestPerHandler  (0x573930)
//   QueueScaledRequestPerHandler (0x5740ec)
//   SpawnEntityFromHandlerList   (0x571a74)
//   SpawnEntityNearNearest       (0x572400)
//
// All ten share the same recovered skeleton:
//
//   rec = Person_FindRecordById(cmd->personId);  if (!rec) return 1;
//   <enumerate a set of candidate entities; collect up to N matches>
//   if (matchCount == 0) return 1024;             // 0x400, nothing eligible
//   pick = Math_RandomModulo(matchCount);         // choose one
//   <roll a wealth-scaled gold and/or build a command>
//   Text_RenderFormattedMessage(...);             // GUI leaf
//   He_SendEntityMessage(rec.id, rec.id, ...);    // GUI/voice leaf
//   QueueRequest16 / QueueRequest* (...);         // command leaf
//   return 0;
//
// The HE-list iteration, the per-handler entity resolution, the production-type
// test, and the nearest-entity / query-list searches are all engine leaves with
// no reconstructed target, so they are routed through DebugCmd3Hooks (installable
// struct, inert defaults defined in debugcmd3.cpp). The deterministic decision
// math — the candidate filter (incl. the per-variant building-code 71 gate), the
// RandomModulo pick, and the wealth-scaled gold — is reproduced exactly.
// ===========================================================================
#include "sim/debugcmd.h"      // DebugCmdPerson, DebugCmdHooks, helpers, codes
#include "guild/common/types.h"
#include "crt/rand.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// A candidate HE handler as seen by the collection loops. The original reads a
// 46-int handler record by offset; these are the three fields the loops touch:
//   ownerEntityId   handler[43]  — the person/office that owns the handler
//   buildingIdA     handler[44]  — "v12"/"v14": first building entity id
//   buildingIdB     handler[45]  — "v13"/"v15": second building entity id
// ---------------------------------------------------------------------------
struct DebugCmd3Handler {
    i32 ownerEntityId = 0;   // handler[43]
    i32 buildingIdA   = 0;   // handler[44]
    i32 buildingIdB   = 0;   // handler[45]
};

// A resolved entity (GameObject_ResolveEntityById / Object_FindObjectById out).
// `valid` is the original's "pointer != 0" test; `buildingCode` is *(_BYTE*)ent
// (the byte the `== 71` / `!= 71` gates compare); `kindWord` is *(_DWORD)(ent+10)
// (compared to person.kindWord); `production` is Building_IsProductionType(ent).
struct DebugCmd3Entity {
    bool valid        = false;
    u8   buildingCode = 0;   // *(_BYTE*)ent
    i32  kindWord     = 0;   // *(_DWORD*)(ent+10)
    bool production   = false;
};

// ---------------------------------------------------------------------------
// Leaf hooks specific to this wave (the HE-list iteration + searches). The GUI/
// command emits reuse the base DebugCmdHooks (sendEntityMessage / queueRequest16
// / market). Inert defaults: the iterators yield nothing, resolves are invalid.
// ---------------------------------------------------------------------------
struct DebugCmd3Hooks {
    // VIBE_He_FindFirstHandlerByFilter(1, person.kindWord, 15) — begin iteration.
    // Returns true and fills `out` with the first matching handler, or false.
    bool (*heFindFirst)(i32 kindWord, DebugCmd3Handler* out);
    // VIBE_He_FindNextMatchingHandler() — advance; false at end of list.
    bool (*heFindNext)(DebugCmd3Handler* out);
    // VIBE_GameObject_ResolveEntityById(entityId) / VIBE_Object_FindObjectById.
    DebugCmd3Entity (*resolveEntity)(i32 entityId);
    // VIBE_ObjectSearch_FindNearestEntity(rec, 6, 0.0, 100.0) — SpawnEntityNear.
    // Returns true and writes the found target entity id, or false (nothing).
    bool (*findNearestEntity)(const DebugCmdPerson* p, i32* outEntityId);
    // VIBE_Person_QueryBegin/IterNext list used by SpawnEntityFromHandlerList:
    // fills `out` with up to `cap` candidate entity ids, returns the count.
    int (*queryEntityList)(const DebugCmdPerson* p, i32* out, int cap);
};

void SetDebugCmd3Hooks(const DebugCmd3Hooks* hooks);
const DebugCmd3Hooks& GetDebugCmd3Hooks();

// ---------------------------------------------------------------------------
// Handlers (faithful; the deterministic core of each). Each takes the command's
// personId. Return codes: 0 handled, 1 no person, 1024 (0x400) nothing eligible.
// ---------------------------------------------------------------------------

// gilde.exe 0x573ebc — VIBE_DebugCmd_BroadcastMsgToHandlersA (table index 36).
//   collect gate: bA.code==71 || bB.code==71 ; msg textId 1418.
i32 DebugCmdBroadcastMsgToHandlersA(i32 personId);
// gilde.exe 0x574388 — VIBE_DebugCmd_BroadcastMsgToHandlersB (table index 38).
//   collect gate: (bA && bB && bA.code!=71) || bB.code!=71 ; extra roll%3+2.
i32 DebugCmdBroadcastMsgToHandlersB(i32 personId);
// gilde.exe 0x57458c — VIBE_DebugCmd_BroadcastMsgToHandlersC (table index 39).
//   collect gate: bA.code!=71 || bB.code!=71 ; extra roll%3+2.
i32 DebugCmdBroadcastMsgToHandlersC(i32 personId);
// gilde.exe 0x574784 — VIBE_DebugCmd_BroadcastMsgToHandlersD (table index 41).
//   collect gate: (bB && bA && bA.code==71) || bB.code==71.
i32 DebugCmdBroadcastMsgToHandlersD(i32 personId);
// gilde.exe 0x5749f8 — VIBE_DebugCmd_BroadcastMsgToHandlersE (table index 42).
//   collect gate: bA.code==71 || bB.code==71 ; extra roll%3+2.
i32 DebugCmdBroadcastMsgToHandlersE(i32 personId);

// gilde.exe 0x573930 — VIBE_DebugCmd_QueueStateRequestPerHandler (index 34).
//   collect gate: (bA && bB && bA.code==71) || bB.code==71 ; owner via
//   Object_FindObjectById; after pick rolls %0x23+60 and emits a state request.
i32 DebugCmdQueueStateRequestPerHandler(i32 personId);
// gilde.exe 0x5740ec — VIBE_DebugCmd_QueueScaledRequestPerHandler (index 37).
//   collect gate: (bA && bB && bA.code==71) || bB.code==71 ; after pick rolls a
//   wealth-scaled gold (roll%2, base 1.0, scale ~0.01) and QueueRequest16.
i32 DebugCmdQueueScaledRequestPerHandler(i32 personId);

// gilde.exe 0x571a74 — VIBE_DebugCmd_SpawnEntityFromHandlerList (index 7).
//   collect a query-list of entity ids (cap 12); pick one; roll%5, base 2.0,
//   scale 0.01; QueueRequest16(personId, ...) + message.
i32 DebugCmdSpawnEntityFromHandlerList(i32 personId);
// gilde.exe 0x572400 — VIBE_DebugCmd_SpawnEntityNearNearest (index 17).
//   find nearest entity (else 1024); resolve it (else 1024); roll%3, base 1.0,
//   scale 0.01; QueueRequest16(-1, personId, ...) + message.
i32 DebugCmdSpawnEntityNearNearest(i32 personId);

// funcs_5766CB (NpcAction dispatch table @0x63d964) index -> this wave's handler,
// or nullptr if the index is owned elsewhere / deferred.
i32 (*DebugCmd3NpcTableEntry(int npcActionIndex))(i32 personId);

} // namespace guild::sim

#pragma once
// charaction_steps3 — batch 3 of the CharAction step / state-machine LEAVES of the
// Guild simulation (gilde.exe, VIBE_CharAction_* family). These are the remaining
// self-contained per-tick step coroutines that drive a He handler record (see
// he.h): the cmd29 entity-request "arm" leaves (PatrolFindTarget, FindBeggarTarget,
// FindInteractionPartner), the guard-target request, the busy/handler-pool scan
// (IsAnimalTargetBusy), the group-gather member counter (GroupGatherInit), the
// transport-duration arm (InitTargetState), the storage-expand init
// (InitLagerErweitern), the sabotage init (InitSabotage), and the two intro-message
// emitters (DuelIntroMessage, NotifyMessageInit).
//
// SCOPE — every function here operates on the SAME He record layout (he.h) and the
// +82 appointment / +68 saved-pose / +112 state / +120 flags convention used by
// charaction_steps2 / charaction_brawl. The global clock (NpcClock(), gilde.exe
// qword_13CE852 @0x13CE852) is stamped into +82 (or +192/+196 sub-records), the
// appointment is advanced via GameTimeAdvance (gilde.exe 0x583150), terminal states
// free the handler entry, and the (flags & 2 / & 4) bits gate the re-arm. The
// state-machine control flow, the RNG draws, the time deltas and the exact field
// offsets are translated 1:1; the cross-cluster leaf side effects (entity resolve,
// person query, cmd25/cmd29/cmd61/buildop73 emits, the formatted-message renders)
// are routed through the shared NpcLeafHooks (npcaction.h) plus the
// CharActionStep3Hooks bridge below so each step stays exercisable in isolation.
//
// Addresses are absolute, imagebase 0x400000.
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::sim {

// ===========================================================================
// CharActionStep3 cross-cluster leaves. A null member installs an inert default
// (resolves report "absent / not found", emits are no-ops). The shared NpcLeafHooks
// (npcaction.h) still supplies freeHandlerEntry / queueRequestEntity29; this struct
// adds the entity/person resolves, the cmd25/cmd61/buildop73 emits, the
// handler-pool find-by-filter scan, and the intro-message emitters.
// ===========================================================================
struct CharActionStep3Hooks {
    // VIBE_GameObject_ResolveEntityById(outA, outB, id, outC) — resolve a world
    // object/entity id to a record pointer. The originals pass differing out
    // slots; we expose the single resolved record pointer (the value the callers
    // null-check) and ignore the auxiliary out args. Returns null == not found.
    HeRecord* (*resolveEntityById)(i32 id);

    // VIBE_Person_QueryBegin(buf, a, b, c) — begin a person query; returns the
    // first matching person record (or null). `a`/`b`/`c` are the literal query
    // selectors the callers pass; `personOrCity` is the value column.
    HeRecord* (*personQueryBegin)(int a, int b, int c);

    // VIBE_Person_FindRecordById(id) — resolve a person id to a record base; null
    // == absent. (Reused by NotifyMessageInit.)
    HeRecord* (*findPersonById)(i32 id);

    // VIBE_He_FindFirstHandlerByFilter — begin a filtered scan of the live handler
    // pool. `count` (selector,value) pairs as in charaction_steps2. Returns the
    // first matching record, or null.
    HeRecord* (*findFirstByFilter)(int count, const int* selectors, const int* values);
    // VIBE_He_FindNextMatchingHandler — continue the active scan; null at the end.
    HeRecord* (*findNextMatching)();

    // VIBE_Command_QueueRequestGuardTarget61(entity, 0, 0, rankByte) — guard-target
    // request. Returns the packet handle (stored into +188). The original derives
    // rankByte = HIBYTE(dword_13CE294[589 * (signed)entity[0] + 556]); that table is
    // owned by the entity cluster, so the hook reads it off `entity` internally.
    i32 (*queueRequestGuardTarget61)(HeRecord* entity);

    // VIBE_Command_QueueRequestArgs25(entityId, sel, mask, mode, 0) — generic cmd25.
    void (*queueRequestArgs25)(i32 entityId, int sel, int mask, int mode);

    // VIBE_Command_RequestBuildOp73Str(0, personId, 0, -1, 1, 372, "SABOTAGE") —
    // the sabotage build-op. Returns the packet handle (stored into +200).
    i32 (*requestBuildOp73Sabotage)(i32 personId);

    // dword_63C7B8 — global "fast time" / debug-skip flag. When nonzero InitTargetState
    // schedules the action +1 second ahead instead of its per-type duration.
    bool (*fastTimeEnabled)();
    // The per-type action duration in minutes (the +34 field of the type record at
    // dword_13CE27C[65*typeId]). `typeId` is ((dword@+170) >> 16).
    int (*targetActionMinutes)(i32 typeId);

    // VIBE_Text_RenderFormattedMessage(buf, textId, nameArg, timePtr, nameArg) +
    // VIBE_He_SendEntityMessage(targetEntityId, ...) collapsed into one notify emit.
    // The originals build a formatted string and deliver it to the target entity;
    // here the side effect is opaque (the message text is render-bridge).
    // `targetEntityId` is the recipient (the SendEntityMessage `to`), `textId` the
    // template, `nameArg` the substituted name word.
    void (*sendNotifyMessage)(i32 targetEntityId, int textId, u16 nameArg);
};
void SetCharActionStep3Hooks(const CharActionStep3Hooks* hooks);
const CharActionStep3Hooks& GetCharActionStep3Hooks();

// ===========================================================================
// Additional He field accessors used by this batch (byte-faithful offsets).
//   +16  (+0x10)  : entity slot id (dword; -1 == none) — InitSabotage gate.
//   +132 (+0x84)  : cmd29 entity-request packet handle (dword; -1 == none).
//   +172 (+0xAC)  : counter / target id (dword); +173 sub-byte; +174 sub-byte.
//   +176 (+0xB0)  : target object/entity id (dword).
//   +180 (+0xB4)  : computed extent / wait counter (dword).
//   +184 (+0xB8)  : multiplier / successor id (dword).
//   +188 (+0xBC)  : guard-target packet handle (dword).
//   +192 (+0xC0)  : secondary GameTime sub-record (LagerErweitern deadline; 14 bytes).
//   +196 (+0xC4)..: misc dwords used by the sabotage/group-gather inits.
//   +210 (+0xD2)  : group-gather byte flag.
//   +216 (+0xD8)  : group-gather member count byte.
// ===========================================================================
inline i32&      Cas3_Slot16(HeRecord* h)    { return *reinterpret_cast<i32*>(HeBytes(h) + 16); }
inline i32&      Cas3_Packet(HeRecord* h)    { return *reinterpret_cast<i32*>(HeBytes(h) + 132); }
inline i32&      Cas3_Counter(HeRecord* h)   { return *reinterpret_cast<i32*>(HeBytes(h) + 172); }
inline i32&      Cas3_TargetId(HeRecord* h)  { return *reinterpret_cast<i32*>(HeBytes(h) + 176); }
inline i32&      Cas3_Extent(HeRecord* h)    { return *reinterpret_cast<i32*>(HeBytes(h) + 180); }
inline i32&      Cas3_Mult(HeRecord* h)      { return *reinterpret_cast<i32*>(HeBytes(h) + 184); }
inline i32&      Cas3_GuardPacket(HeRecord* h){ return *reinterpret_cast<i32*>(HeBytes(h) + 188); }
inline GameTime& Cas3_SubTime192(HeRecord* h){ return *reinterpret_cast<GameTime*>(HeBytes(h) + 192); }
inline i32&      Cas3_Misc196(HeRecord* h)   { return *reinterpret_cast<i32*>(HeBytes(h) + 196); }

// ===========================================================================
// Translated step functions.
// ===========================================================================

// gilde.exe 0x4ce9dc — VIBE_CharAction_PatrolFindTarget.
//   if (flags & 2): begin a person query for the patrol target (+172). If the query
//   finds none, stamp the clock into +82 (+1 second), cmd29(-1) -> +132. Then
//   unconditionally stamp the clock into +82 (+1 second) and cmd29(0) -> +132.
//   Returns the last cmd29 handle (or the record when the flag is clear).
i32 PatrolFindTarget(HeRecord* h);

// gilde.exe 0x4d0724 — VIBE_CharAction_GuardRequestTarget.
//   Resolve the entity at +176; if not found, free the handler entry (the original
//   falls through). Queue a guard-target request (+188 := handle), stamp the clock
//   into +82 (+6 minutes). Returns GameTimeAdvance's result.
i32 GuardRequestTarget(HeRecord* h);

// gilde.exe 0x4dc590 — VIBE_CharAction_IsAnimalTargetBusy(a1@eax).
//   if (!a1) return 1. Scan kind-98 handlers: return 0 if some handler has
//   +176 == *(int*)(a1+1) AND +184 == 1 (the animal target is busy); else 1.
i32 IsAnimalTargetBusy(HeRecord* h);

// gilde.exe 0x4d1fb8 — VIBE_CharAction_FindInteractionPartner(a1@eax).
//   Scan handlers (kind 95, index@+8 == a1->cityIndex). Skip self; if another
//   matching handler exists, stamp the clock into +82, cmd29(-1) -> +132, return
//   that handler. If none (besides self), clear state (+112 := 0) and byte@+186,
//   return null.
HeRecord* FindInteractionPartner(HeRecord* h);

// gilde.exe 0x4d286c — VIBE_CharAction_FindBeggarTarget(a1@eax).
//   if (flags & 4) return the record. Scan kind-89 handlers, skip self. If another
//   matching handler exists, stamp the clock into +82, cmd29(-1) -> +132, return
//   that handle. Else (none): state (+112) := 5, restore saved pose (+68 -> +82),
//   +132 := -1, cmd29(5); returns 5.
i32 FindBeggarTarget(HeRecord* h);

// gilde.exe 0x4d30f4 — VIBE_CharAction_GroupGatherInit.
//   Count non(-1) member ids in the 6-slot array at +140 into byte@+216. If none,
//   free the handler entry. Else reset the 6 dwords at +172..+192 to -1, clear
//   byte@+210, stamp the clock into the +196 sub-record AND +82 (+2 minutes).
//   Returns GameTimeAdvance's result.
i32 GroupGatherInit(HeRecord* h);

// gilde.exe 0x4dd150 — VIBE_CharAction_InitTargetState.
//   Stamp the clock into +82; if dword_63C7B8 (a debug/fast-time global) is set,
//   advance +1 second, else advance by the per-target duration read off the
//   resolved type record (table dword_13CE27C, stride 65, field +34). Returns
//   GameTimeAdvance's result.
int InitTargetState(HeRecord* h);

// gilde.exe 0x4dfeb4 — VIBE_CharAction_InitLagerErweitern.
//   Copy saved pose (+68) into the +192 sub-record, advance it by
//   (target@+176 * mult@+184) minutes. Copy saved pose into +82, advance +1 second.
//   Resolve the target object (+172); if found and its byte+19 has bit 0x20, free.
//   Otherwise emit cmd25(obj->id@+2, 19, 32, 1) and stamp the clock into +96.
//   Compute result := target@+176 + (objByte+18), clamped to 100, store into +180.
int InitLagerErweitern(HeRecord* h);

// gilde.exe 0x4e20a0 — VIBE_CharAction_InitSabotage.
//   Stamp the clock into +82 (+1 second). If the entity slot (+16) == -1, run a
//   person query (selectors 1,5,22 then 1,5,15); on a hit store the person id into
//   +16. Set +196 := -1, emit the sabotage build-op for the (possibly resolved)
//   entity id, store the handle into +200. Returns the handle.
i32 InitSabotage(HeRecord* h);

// gilde.exe 0x4cf9c8 — VIBE_CharAction_DuelIntroMessage(a1@eax, a2@edx, a3@ebx).
//   Disarm both combatants (cmd25 456/1024/4 on each), send the duel intro message
//   to each whose record byte+2 is 6 or 7, then cmd29(2). `a2`/`a3` are the two
//   combatant person records; a1 is the handler record. Returns cmd29's handle.
i32 DuelIntroMessage(HeRecord* h, HeRecord* combatantA, HeRecord* combatantB);

// gilde.exe 0x4d0918 — VIBE_CharAction_NotifyMessageInit.
//   +132 := -1. If (flags & 4) return the record. Stamp the clock into +82
//   (+24 days). If (flags & 2) == 0: cmd29(0). Else resolve the two persons at +176
//   and +172; if both resolve and the +172 person's byte+2 is 6 or 7, send the
//   notify message; then cmd29(0). If either fails to resolve, restamp the clock
//   into +82 (no advance) and cmd29(-1). Returns the cmd29 handle.
i32 NotifyMessageInit(HeRecord* h);

} // namespace guild::sim

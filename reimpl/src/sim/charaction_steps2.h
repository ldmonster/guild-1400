#pragma once
// charaction_steps2 — the remaining self-contained CharAction step / state-machine
// LEAVES of the Guild simulation (gilde.exe, VIBE_CharAction_* family). These are
// the small per-tick coroutine steps that drive a He handler record (see he.h):
// timestamp/appointment "reset" leaves, saved-pose restore + finish leaves, the
// terminal-state finalizers, the cmd29 entity-request (re)arm leaves, the
// per-frame "repeat" emitters, the group-action retargeters, and the
// handler-pool find-by-filter scans.
//
// SCOPE — every function here operates on the SAME 14-byte GameTime blocks and
// +112 state convention used by charaction_brawl / charaction_misc and the
// NpcEvent step machines: the global clock (NpcClock(), gilde.exe qword_13CE852
// @0x13CE852) is stamped into +82 / +68, the appointment is advanced via
// GameTimeAdvance (gilde.exe 0x583150), terminal states (-1/-2) free the handler
// entry, and the busy/needs-spawn flags at +120 gate the re-arm. The state-machine
// control flow, the RNG draws, the time deltas and the exact field offsets are
// translated 1:1; the cross-cluster leaf side effects (free handler entry, queue
// cmd29 entity request, packet status, find-by-filter handler scan, person lookup,
// player-action change, ap-event / cmd15 emit) are routed through the shared
// NpcLeafHooks (npcaction.h) plus the CharActionStep2Hooks bridge below so each
// step stays exercisable in isolation.
//
// Addresses are absolute, imagebase 0x400000.
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::sim {

// ===========================================================================
// CharActionStep2 cross-cluster leaves. A null member installs an inert default
// (queries report "absent / not found", emits are no-ops). The shared
// NpcLeafHooks (npcaction.h) still supplies freeHandlerEntry / queueRequestEntity29
// / packetStatus; this struct adds the find-by-filter scan, person lookup, the
// player-action retarget, and the repeat-step emitters.
// ===========================================================================
struct CharActionStep2Hooks {
    // VIBE_He_FindFirstHandlerByFilter — begin a filtered scan of the live handler
    // pool. `selectors`/`values` are `count` (selector,value) pairs, selector:
    //   0 => kind byte (+0), 1 => id@+4, 2 => index@+8 (word), 3 => field@+16.
    // Returns an opaque record base (the He-shaped record), or null if no match.
    HeRecord* (*findFirstByFilter)(int count, const int* selectors, const int* values);
    // VIBE_He_FindNextMatchingHandler — continue the active scan; null at the end.
    HeRecord* (*findNextMatching)();
    // VIBE_Person_FindRecordById(id) — resolve a person id to a record base; the
    // ChangeGroupAction steps read its +0 marker word. Null == absent.
    HeRecord* (*findPersonById)(i32 id);
    // VIBE_Character_ChangePlayerAction(0, 0, recordBase, personMarker) — retarget
    // the resolved person's character to the group action.
    void (*changePlayerAction)(HeRecord* groupRec, u16 personMarker);
    // VIBE_Command_EnqueueCmd15(-1, cityId, value, byte_6477A1) — the RepeatCommand
    // emit. `cityId` is the resolved city id (Person id column for the record's +8
    // index); `value` is the +172 dword.
    void (*enqueueCmd15)(i32 cityId, i32 value);
    // VIBE_MeisterAi_RegisterApEvent(index@+8, 0, -value@+172) — the RepeatTalk emit.
    void (*registerApEvent)(u16 index, i32 negValue);
    // Resolve the record's +8 city index to a city id (the Person id column
    // dword_12CE914[134*index]); RepeatCommand passes this to enqueueCmd15.
    i32 (*resolveCityId)(u16 index);
};
void SetCharActionStep2Hooks(const CharActionStep2Hooks* hooks);
const CharActionStep2Hooks& GetCharActionStep2Hooks();

// ===========================================================================
// Field accessors (byte-faithful offsets into the He handler record). The +82
// and +68 blocks are 14-byte GameTime images (see he.h / types.h GameTime).
//   +68  savedTime  GameTime (restore source / "goal" pose)
//   +82  apptTime   GameTime (appointment / next-wake)
//   +86  apptTime.hour word (RestorePosAndBranch nudges it by rand%4+2)
//   +112 state dword (-2/-1 terminal-free, 0 = active, 1.. = phases)
//   +120 flag byte (bit1 / &2 needs-cmd29, bit2 / &4 busy/already-spawned)
//   +132 in-flight cmd29 entity-request packet handle (dword; -1 == none)
//   +172 counter / value dword (RepeatCommand/Talk payload; ChangeGroupAction count
//        byte); +176 deadline / remaining-iteration dword; +184 successor id dword.
//   +176 RepeatCommand/Talk remaining-iteration counter (dword).
// ===========================================================================
inline GameTime& Cas2_SavedTime(HeRecord* h) { return *reinterpret_cast<GameTime*>(HeBytes(h) + 68); }
inline GameTime& Cas2_ApptTime(HeRecord* h)  { return *reinterpret_cast<GameTime*>(HeBytes(h) + 82); }
inline u16&      Cas2_ApptHour(HeRecord* h)   { return *reinterpret_cast<u16*>(HeBytes(h) + 86); }
inline i32&      Cas2_State(HeRecord* h)      { return *reinterpret_cast<i32*>(HeBytes(h) + 112); }
inline u8&       Cas2_Flags(HeRecord* h)      { return *reinterpret_cast<u8*>(HeBytes(h) + 120); }
inline i32&      Cas2_Packet(HeRecord* h)     { return *reinterpret_cast<i32*>(HeBytes(h) + 132); }
inline i32&      Cas2_Counter(HeRecord* h)    { return *reinterpret_cast<i32*>(HeBytes(h) + 172); }
inline i32&      Cas2_RemIter(HeRecord* h)    { return *reinterpret_cast<i32*>(HeBytes(h) + 176); }
inline i32&      Cas2_SuccId(HeRecord* h)     { return *reinterpret_cast<i32*>(HeBytes(h) + 184); }

// ===========================================================================
// Timestamp / appointment "reset" leaves — stamp the global clock into the
// appointment slot (+82) and advance it by a fixed delta. (No state change.)
// All return GameTimeAdvance's result (the resulting hour-of-day).
// ===========================================================================

// gilde.exe 0x4d0a98 — VIBE_CharAction_StateReset24      (+82 = clock, +24 days)
// gilde.exe 0x4d113c — VIBE_CharAction_StateReset24Alt   (byte-identical twin)
int StateReset24(HeRecord* h);

// gilde.exe 0x4d0af0 — VIBE_CharAction_StateAdvancePos.
//   Stamps the clock into +82, sets +176 := +172 + 1 (RemIter := Counter+1; the
//   original writes (a1+82)+94 := (a1+82)+90 + 1, i.e. a1+176 := a1+172 + 1),
//   advances +24 days. Returns GameTimeAdvance's result.
int StateAdvancePos(HeRecord* h);

// gilde.exe 0x4d17b4 — VIBE_CharAction_StateReset96      (+82 = clock, +96 days)
int StateReset96(HeRecord* h);

// gilde.exe 0x4d1844 — VIBE_CharAction_StateReset0       (+82 = clock, +5 seconds)
// gilde.exe 0x4d1904 — VIBE_CharAction_StateReset0Alt    (byte-identical twin)
int StateReset0(HeRecord* h);

// gilde.exe 0x4d38ec — VIBE_CharAction_StateReset0Alt2   (+82 = clock, +2 seconds)
int StateReset0Alt2(HeRecord* h);

// gilde.exe 0x4d0f30 — VIBE_CharAction_StateCopyPos3.
//   Copies the saved pose (+68) into the appointment (+82), advances +3 days.
int StateCopyPos3(HeRecord* h);

// gilde.exe 0x4dd574 — VIBE_CharAction_CopyGoalToTarget.
// gilde.exe 0x4dda88 — VIBE_CharAction_CopyGoalToTargetDup  (byte-identical twin)
//   Copies +68 -> +82, advances +15 minutes.
int CopyGoalToTarget(HeRecord* h);

// gilde.exe 0x4e0a08 — VIBE_CharAction_CopyGoalToTargetState2.
// gilde.exe 0x4e0d54 — VIBE_CharAction_CopyGoalToTargetState2Dup (byte-identical)
//   Copies +68 -> +82, advances +2 days.
int CopyGoalToTargetState2(HeRecord* h);

// gilde.exe 0x4cf990 — VIBE_CharAction_ArrestReset.
//   Stamps the clock into BOTH the saved pose (+68) and the appointment (+82),
//   advances +2 days. Returns GameTimeAdvance's result.
int ArrestReset(HeRecord* h);

// ===========================================================================
// Saved-pose restore + branch / finish leaves.
// ===========================================================================

// gilde.exe 0x4d0b38 — VIBE_CharAction_RestorePosAndBranch.
//   Sets +176 := -1, restores the saved pose (+68 -> +82) twice (the original
//   emits the copy block twice — preserved), draws r := RandomModulo(4)+2, clears
//   state (+112 := 0), and nudges the appointment hour (+86) by +r. Returns r.
int RestorePosAndBranch(HeRecord* h);

// gilde.exe 0x4d1384 — VIBE_CharAction_RestorePosFinish.
// gilde.exe 0x4d1674 — VIBE_CharAction_RestorePosFinishAlt (byte-identical twin)
//   Restores the saved pose (+68 -> +82); with probability 1/2 (RandomModulo(2))
//   frees the handler entry. Returns the original's eax (RandomModulo result, or
//   the free result when nonzero).
int RestorePosFinish(HeRecord* h);

// gilde.exe 0x4d19ac — VIBE_CharAction_ClearStateAndTimer.
//   Clears the state dword (+112) and the +172 counter. Returns the record (eax).
HeRecord* ClearStateAndTimer(HeRecord* h);

// gilde.exe 0x4dc070 — VIBE_CharAction_RetZero. Returns 0.
int RetZero();

// ===========================================================================
// Terminal-state finalizers — inspect the +112 state, free on terminal codes,
// optionally re-arm the cmd29 entity request when the needs-cmd29 flag is set.
// ===========================================================================

// gilde.exe 0x4d0b20 — VIBE_CharAction_FinishIfTerminal.
//   if (state == -1 || state == -2) free the handler entry; else return the record.
HeRecord* FinishIfTerminal(HeRecord* h);

// gilde.exe 0x4d17e0 — VIBE_CharAction_FinalizeEntityStep.
//   state < -1: state==-2 -> free, else return state.
//   state <= -1 (i.e. == -1): free.
//   state == 0 && (flags & 2): stamp clock into +82 (+2 minutes), queue cmd29
//     entity request (-1), store the handle into +132.
//   Returns the state (or the free result).
i32 FinalizeEntityStep(HeRecord* h);

// gilde.exe 0x4db514 — VIBE_CharAction_RequestEntityFinish.
//   if ((flags & 4) == 0): stamp the clock into +82 (+24 days), queue a cmd29
//   entity request (arg 0), store the handle into +132. Returns the handle (or eax).
i32 RequestEntityFinish(HeRecord* h);

// gilde.exe 0x4db558 — VIBE_CharAction_RequestEntityIfValid.
//   if (state == -1 || state == -2) free the handler entry; else if ((flags & 4)
//   == 0): stamp the clock into +82 (no advance), queue a cmd29 entity request
//   (-1), store the handle into +132. Returns the handle / free result / eax.
i32 RequestEntityIfValid(HeRecord* h);

// gilde.exe 0x4d3bdc — VIBE_CharAction_ExtortInit.
//   Sets +132 := -1; if ((flags & 4) == 0): +184 := -1, stamp the clock into +82
//   (+2 minutes), state (+112) := 1, queue a cmd29 entity request (arg 1), store
//   the handle into +132. Returns the handle (or eax).
i32 ExtortInit(HeRecord* h);

// ===========================================================================
// Per-frame "repeat" emitters — emit one command per tick, decrement the +176
// remaining-iteration counter, re-arm (+24 days) while nonzero, else free.
// ===========================================================================

// gilde.exe 0x4d1870 — VIBE_CharAction_RepeatCommandStep.
//   Terminal (-1/-2) -> free. state 0: enqueue cmd15(cityId(+8), value(+172)),
//   if (--*+176) re-arm (+24 days) else free. Returns the original's eax.
i32 RepeatCommandStep(HeRecord* h);

// gilde.exe 0x4d1930 — VIBE_CharAction_RepeatTalkStep.
//   Terminal (-1/-2) -> free. state 0: register ap-event(index(+8), -value(+172)),
//   if (--*+176) re-arm (+24 days) else free. Returns the original's eax.
i32 RepeatTalkStep(HeRecord* h);

// ===========================================================================
// Group-action retargeters — iterate up to +172 (byte) member ids stored at
// +140 + 4*i, resolve each Person, and ChangePlayerAction them onto the group.
// ===========================================================================

// gilde.exe 0x4e0fa0 — VIBE_CharAction_ChangeGroupAction.
//   For i in [0, byte@+172): resolve Person at +140+4*i; if found, mark "any"
//   and ChangePlayerAction. If none found, free the handler entry. Otherwise stamp
//   the clock into +82 (+5 minutes), state (+112) := 1. Returns GameTimeAdvance's
//   result (or the free result).
i32 ChangeGroupAction(HeRecord* h);

// gilde.exe 0x4e1764 — VIBE_CharAction_ChangeGroupActionAndGoal.
//   For i in [0, byte@+172): resolve Person at +140+4*i; if found ChangePlayerAction
//   (no "any" gate, never frees). Then copies the saved pose (+68 -> +82) and
//   advances +1 minute. Returns GameTimeAdvance's result.
i32 ChangeGroupActionAndGoal(HeRecord* h);

// ===========================================================================
// Handler-pool find-by-filter scans (over the live He handler pool).
// ===========================================================================

// gilde.exe 0x4dc5dc — VIBE_CharAction_FindPairedEntityForward(a@eax, b@edx).
//   Scans live handlers of kind 46. Returns 0 if some handler has +172 == b->id
//   AND +176 == a->id (the forward-paired entity exists); 1 otherwise. `b` is read
//   only for b->id (+4); `a` only for a->id (+4).
i32 FindPairedEntityForward(HeRecord* a, HeRecord* b);

// gilde.exe 0x4dc678 — VIBE_CharAction_FindActionByActor(marker@eax, rec@edx,
//                                                        outCount@ebx).
//   Scans live handlers matching (index@+8 == *marker, kind == 53). Returns 0 if
//   some handler has +188 == rec->id (+4) (the action is already held by this
//   actor); else returns 1 and, if outCount != null, writes the number of scanned
//   non-matching handlers into *outCount.
i32 FindActionByActor(const u16* marker, HeRecord* rec, i32* outCount);

} // namespace guild::sim

#pragma once
// event2 — self-contained world-event "He"-action bodies of the Guild simulation
// (gilde.exe VIBE_Event_* family). These are the per-event-type action
// initializers, small record helpers, and phase-machine step bodies that the
// handler-pool scheduler invokes once an event fires. Each operates on one He /
// handler record (see sim/he.h) by the original raw byte offsets.
//
// The companion files event_bindings.{h,cpp} (registration / name<->id /
// serialization), event.{h,cpp} (descriptor table), event_fire.* and
// event_effects.* (the FireRaid / price-spike / production data-rule cores)
// cover the rest of the module; this file translates the action initializers
// and the time/RNG-driven scheduling bodies.
//
// Translated functions (addresses absolute, imagebase 0x400000):
//   0x4f06bc VIBE_Event_ResetActionState        — stamp clock @+82, +1 minute
//   0x4f168c VIBE_Event_ResetActionStateZero    — stamp clock @+82, +20 seconds
//   0x4f1d14 VIBE_Event_SetActionAnim7          — stamp clock @+82, set anim 7
//   0x4f3ae0 VIBE_Event_AllocActionAnim4        — copy time blocks, anim 4, rng wait
//   0x4f3d28 VIBE_Event_AllocActionAnim7        — copy time blocks, anim 7, rng wait
//   0x4f4f54 VIBE_Event_AllocActionResetFields  — copy time blocks, +1s, zero +188..
//   0x4f6cd4 VIBE_Event_AllocActionResetFlags   — copy time blocks, +1s, zero +172..
//   0x4f52d0 VIBE_Event_AppendCollectedHandle   — push handle into +4 array (+260 cnt)
//   0x4f24dc VIBE_Event_RetZero                  — return 0  (subsystem tick stub)
//   0x4f3adc VIBE_Event_NullSub7                 — no-op
//   0x4f3de0 VIBE_Event_NullSub8                 — no-op
//   0x4f1ac4 VIBE_Event_HelpTextPlaybackRun      — help-text playback phase machine
//   0x4f1c2c VIBE_Event_HelpAdviceLoopRun        — help-advice broadcast loop
//
// The "anim" word the initializers write lives at He+86 (the hour slot of the
// +82 appointment GameTime, reused as an animation id by these actions); the
// data dword at He+88 is the appointment minute slot reused as an anim param.
// We address the record by explicit offset to stay byte-faithful (matching the
// originals' `*(T*)(base+off)`), through the sim::HeRecord accessors where they
// exist and raw helpers below otherwise.
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::world {

using guild::sim::HeRecord;
using guild::sim::GameTime;

// ===========================================================================
// Leaf hooks — side effects that cross into clusters this file does not own.
// Passing nullptr installs an inert default (free returns the record base as an
// int, the entity-message send is a no-op, the handler scan finds nothing).
// ===========================================================================
struct EventHooks {
    // VIBE_He_FreeHandlerEntry(h) — release the handler entry; returns the
    // original eax (inert default returns the record base reinterpreted to int).
    i32 (*freeHandlerEntry)(HeRecord* h);
    // VIBE_He_SendEntityMessage(target, a, kind, text, msgId, flag) — broadcast a
    // scripted message to an entity. Return value is ignored by the callers here.
    void (*sendEntityMessage)(i32 target, i32 a, i32 kind, const void* text,
                              i32 msgId, i32 flag);
};

void SetEventHooks(const EventHooks* hooks);
const EventHooks& GetEventHooks();

// The advice/help script tables the playback/loop bodies read. The originals
// index into static engine arrays (dword_12292D8/_DC/_E0/_E4 — the help-event
// schedule: day/hour/minute/text-id per step, -1 == end) and the advice id list
// (dword_1229270, 27 entries). Tests install these so the bodies are exercisable
// in isolation; when unset the playback table is treated as a single empty step.
struct HelpStep {
    i32 day;        // dword_12292D8[4*i]  -> appointment day   (He+82)
    u16 hour;       // dword_12292DC[4*i]  -> appointment hour   (He+86)
    i32 minute;     // dword_12292E0[4*i]  -> appointment minute (He+88)
    i32 textId;     // dword_12292E4[4*i]  -> message text id (0 / -1 == skip send)
};
void SetHelpScheduleTable(const HelpStep* steps, int count);
void SetAdviceIdTable(const i32* ids, int count);

// ===========================================================================
// Action initializers.
// ===========================================================================

// gilde.exe 0x4f06bc — VIBE_Event_ResetActionState (h@eax).
//   Stamp the global clock into the +82 appointment GameTime, then advance it by
//   one minute (GameTime_Advance(rec, 0, 1, 0): edx=0 hours, ecx=1 second... — see
//   note). Returns the resulting hour-of-day.
i32 ResetActionState(HeRecord* h);

// gilde.exe 0x4f168c — VIBE_Event_ResetActionStateZero (h@eax).
//   As ResetActionState but advances by 20 seconds (Advance(rec, 0, 0, 20)).
i32 ResetActionStateZero(HeRecord* h);

// gilde.exe 0x4f1d14 — VIBE_Event_SetActionAnim7 (h@eax).
//   Stamp the clock into +82, set the anim word (+86) to 7 and the anim param
//   dword (+88) to 0. Returns the record base (the original returns eax==h).
HeRecord* SetActionAnim7(HeRecord* h);

// gilde.exe 0x4f3ae0 — VIBE_Event_AllocActionAnim4 (h@eax).
//   Copy the saved GameTime (+68) into the appointment (+82) and the scratch
//   block (+96), then advance the appointment by (4, 0, rng) where
//   rng = RandomModulo(60). Returns the resulting hour-of-day.
i32 AllocActionAnim4(HeRecord* h);

// gilde.exe 0x4f3d28 — VIBE_Event_AllocActionAnim7 (h@eax).
//   As AllocActionAnim4 but advances by (7, <uninit ecx>, rng+30),
//   rng = RandomModulo(30). The original passes an uninitialised ecx (the
//   seconds arg); we reproduce the documented register and pass 0 (the field is
//   immediately overwritten by the minute carry, see note). Returns the hour.
i32 AllocActionAnim7(HeRecord* h);

// gilde.exe 0x4f4f54 — VIBE_Event_AllocActionResetFields (h@eax).
//   Copy +68 -> +82 and +82 -> +96, advance the appointment by one minute, then
//   zero the three dwords at +188/+192/+196. Returns the advance result (hour).
i32 AllocActionResetFields(HeRecord* h);

// gilde.exe 0x4f6cd4 — VIBE_Event_AllocActionResetFlags (h@eax).
//   As AllocActionResetFields but zeroes +172/+176/+180 instead. Returns hour.
i32 AllocActionResetFlags(HeRecord* h);

// ===========================================================================
// Small record helpers.
// ===========================================================================

// gilde.exe 0x4f52d0 — VIBE_Event_AppendCollectedHandle (handle@eax, h@edx).
//   Append `handle` to the collected-handle array at He+4 (dword each), bump the
//   count dword at He+260, and return whether the count is still below 64.
//   (The original writes *(int*)(h + 4*oldCount + 4) = handle.)
bool AppendCollectedHandle(i32 handle, HeRecord* h);

// gilde.exe 0x4f24dc — VIBE_Event_RetZero. Subsystem-tick stub; returns 0.
i32 RetZero();

// gilde.exe 0x4f3adc — VIBE_Event_NullSub7.  No-op.
void NullSub7();
// gilde.exe 0x4f3de0 — VIBE_Event_NullSub8.  No-op.
void NullSub8();

// ===========================================================================
// Phase-machine step bodies.
// ===========================================================================
// Both decode the phase counter as (He+112 state) + 2 and switch:
//   0 / 1 (state -2 / -1)  -> free the handler entry (teardown)
//   2     (state 0)        -> the work body
//   else                  -> idle passthrough (return state+2)

// gilde.exe 0x4f1ac4 — VIBE_Event_HelpTextPlaybackRun (h@eax).
//   Walks the help schedule table indexed by the cursor at He+172: for each step
//   whose text id is set (and the help-enabled gate `helpEnabled` is true) it
//   sends an entity message, advances the cursor, and either schedules the next
//   step's appointment (copy clock into +82 then overwrite day/hour/minute from
//   the table) or, at the end of the table, sets state to 1 (teardown next tick).
//   `helpEnabled` mirrors byte_12335B9. Returns the original eax value.
i32 HelpTextPlaybackRun(HeRecord* h);

// gilde.exe 0x4f1c2c — VIBE_Event_HelpAdviceLoopRun (h@eax).
//   When the advice gate (`adviceEnabled`, byte_12335BB) is set, broadcasts the
//   advice message for the current cursor (He+172), increments the cursor, sets
//   state to 1 once it passes 26, then schedules the next wake: clock + (rng+8)
//   hours (rng = RandomModulo(4)). When the gate is clear it just reschedules a
//   day later (clock + 1 day). Returns the advance result / free result.
i32 HelpAdviceLoopRun(HeRecord* h, bool adviceEnabled);

// Variant of HelpTextPlaybackRun that takes the help gate as a parameter (the
// original reads byte_12335B9 directly). Exposed for tests; HelpTextPlaybackRun
// forwards through GetHelpEnabled().
i32 HelpTextPlaybackRun(HeRecord* h, bool helpEnabled);

// The help/advice enable gates (byte_12335B9 / byte_12335BB). Default false.
void SetHelpEnabled(bool on);
void SetAdviceEnabled(bool on);
bool GetHelpEnabled();
bool GetAdviceEnabled();

} // namespace guild::world

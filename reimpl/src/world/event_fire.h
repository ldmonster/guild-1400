#pragma once
// Scripted "Brand" (house-fire / raid) event — the concrete event handler
// VIBE_Event_FireRaidRun (gilde.exe 0x4ee960). The original is a He (handler-
// entry) update callback invoked once per tick while the fire event is active;
// it is a phased state machine driving the .esc cutscene, 3D sound, the universe
// scene slot and network commands. The GUI / script-VM / sound / universe
// plumbing is the engine's; the recoverable data-rules core — the phase machine,
// the trigger gate, the fire-damage computation and the per-tick effect
// application + chronicle hook — is reproduced here, with the mutating engine
// calls routed through a small command hook (mock in tests).
//
//   case -1/-2 : teardown   (free scripts/sound, release handler)
//   case  0    : ignition    (place fire FX, advance time 4 units, spawn fires)
//   case  1    : burn tick   (compute fire damage to building value, voice tier,
//                             advance time, loop back while subPhase < 3)
//
// Event-handler record fields used by the original (byte offsets into the He
// entry, accessed as *(T*)(a1+OFF)). We model the handful the logic core reads.
#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Fire-event handler state  (subset of the He entry VIBE_Event_FireRaidRun walks)
// ===========================================================================
// Offsets recovered from the decompile (a1 = entry base):
//   +0x04  eventId        *(int*)(a1+4)      script-name id ("brand_he_%i_%i")
//   +0x78  flags          *(u8*)(a1+120)     bit1 (0x02) = "is owner / authoritative"
//   +0x6C  step           *(int*)(a1+112)    phase selector (-1,-2,0,1)
//   +0x84  pendingCmd     *(int*)(a1+132)    in-flight command id (-1 = none)
//   +0xAC  owner          *(int*)(a1+172)    person/entity the event targets
//   +0xB0  value          *(int*)(a1+176)    remaining building value (money)
//   +0xB4  subPhase       *(u8*)(a1+180)     ignition sub-step (0,1,2) / burn count
//   +0xB8  spawnIds[16]   *(int*)(a1+184..)  spawned fire-object command ids (-1=free)
struct FireEvent {
    i32 eventId;       // +0x04
    u8  flags;         // +0x78  (bit 0x02 = authoritative owner)
    i32 step;          // +0x6C
    i32 pendingCmd;    // +0x84  (-1 == none)
    i32 owner;         // +0xAC
    i32 value;         // +0xB0  remaining building worth
    u8  subPhase;      // +0xB4
    i32 spawnIds[16];  // +0xB8  (-1 == empty slot)
};

constexpr u8  kFireFlagAuthoritative = 0x02; // *(u8*)(a1+120) & 2
constexpr i32 kFireNone              = -1;
constexpr int kFireMaxSpawns         = 16;    // v59 < 16 cap in case 0

// Initialises a fire-event entry for `owner` with the given starting building
// value. step=0 (ignition), subPhase=0, no pending command, all spawn slots free.
void FireEventInit(FireEvent* ev, i32 eventId, i32 owner, i32 value);

// ---------------------------------------------------------------------------
// Trigger gate  (the leading guard of VIBE_Event_FireRaidRun).
// ---------------------------------------------------------------------------
// gilde.exe 0x4ee97d — the handler is only allowed to run its effect body when it
// is authoritative (flags & 2) OR has no outstanding command. The original, at
// entry, when (flags & 2) && pendingCmd != -1, polls the command status and
// returns early (doing nothing this tick) until the command resolves, then clears
// pendingCmd. This predicate reports whether the body should run this tick, given
// whether the outstanding command (if any) has resolved.
//   commandResolved is the mock for VIBE_Command_GetPacketStatusById != 0.
bool FireEventShouldRunBody(const FireEvent& ev, bool commandResolved);

// ---------------------------------------------------------------------------
// Fire damage  (case 1 burn tick, gilde.exe 0x4eef4f..0x4eefe2).
// ---------------------------------------------------------------------------
// The per-tick fire damage subtracts a production-derived amount from the
// building's remaining value:
//     base   = dailyHourOutput
//     dmg    = base * flt_61FCFC * flt_61FD00            (0.16667.. * 2.0)
//     factor = hasCatalyst ? 1 + catalyst*dbl_61FD08*dbl_61FD10 : 1
//     value -= round(dmg) * factor                       (truncated to int)
// catalyst is *dword_11BC1C8 (a difficulty/wind scalar; 0 when absent). The two
// Coord_ConvertX calls only re-pack the float for display and do not change the
// integer result, so we fold them out. Returns the new (clamped-by-caller) value.
i32 FireEventComputeDamage(i32 remainingValue, i32 dailyHourOutput,
                           bool hasCatalyst, i32 catalyst);

// gilde.exe 0x4ef02d — burn-progress voice tier from how much value the last tick
// destroyed (delta = prevValueHiByte - dmg):
//   delta <= 0   -> tier 2 (total loss)
//   delta <= 10  -> tier 1 (heavy)
//   delta <= 25  -> tier 0 (moderate)
//   delta > 25   -> -1     (no voice line)
// Returns the tier, or -1 when no line should play.
int FireEventVoiceTier(i32 delta);

// ===========================================================================
// Command hook (mock).  All mutations the event performs on the world go through
// this interface so the logic core can be unit-tested without the engine. The
// real backend forwards to VIBE_Command_QueueRequest* / VIBE_GameTime_Advance /
// VIBE_Universe_SwitchActiveSlot.
// ===========================================================================
struct FireEventHooks {
    virtual ~FireEventHooks() = default;
    // VIBE_Command_RequestBuildOp71 + QueueRequestSingle49/NamedObject53: spawn a
    // fire object on building `buildingId`; returns the spawned command id.
    virtual i32 SpawnFire(i32 buildingId) = 0;
    // VIBE_Command_QueueRequestFlag55: set the fire's random orientation flag.
    virtual void SetFireFlag(i32 cmdId, int flag) = 0;
    // VIBE_GameTime_Advance(a1+82, 0,0,4): advance the event's clock by `hours`.
    virtual void AdvanceTime(int hours) = 0;
    // VIBE_History_Notify* hook: record a chronicle entry for the event.
    virtual void RecordChronicle(i32 owner, int textId) = 0;
    // crt::RandNext-backed RandomModulo(n); the real one calls VIBE_Math_RandomModulo.
    virtual int RandomModulo(int n) = 0;
};

// ---------------------------------------------------------------------------
// Phase steps (the recoverable effect-application bodies).
// ---------------------------------------------------------------------------
// gilde.exe case 0 (ignition, 0x4eea30..): on each authoritative ignition tick the
// event spawns one fire object per qualifying building (here modelled as a count
// of candidate buildings), filling spawnIds up to kFireMaxSpawns, then bumps
// subPhase (capped at 2). Each spawn issues SpawnFire + SetFireFlag(rand%2 + 1).
// Returns the number of fires spawned this tick.
int FireEventIgnite(FireEvent* ev, int candidateBuildings, FireEventHooks& hooks);

// gilde.exe case 1 (burn tick, 0x4eeede..): applies one round of fire damage to
// `ev->value`, advances the event clock by 4, plays the burn-progress voice tier,
// and — while subPhase < 3 — resets step to 0 to loop. Records a chronicle entry
// (textId) when the building is destroyed (new value <= 0). Returns the voice tier
// (-1 = none) so the caller/test can verify the emitted line.
int FireEventBurnTick(FireEvent* ev, i32 dailyHourOutput, bool hasCatalyst,
                      i32 catalyst, FireEventHooks& hooks, int chronicleTextId);

// gilde.exe case -1/-2 (teardown, 0x4ee990..): releases the handler. The script /
// sound teardown is the engine's; the recoverable rule is that every outstanding
// spawn command is cancelled (QueueRequestSingle49) and the entry is marked done.
// Returns the number of spawn slots that were active (and thus cancelled).
int FireEventTeardown(FireEvent* ev, FireEventHooks& hooks);

} // namespace guild::world

#include "world/event_fire.h"

// Faithful port of the recoverable data-rules core of VIBE_Event_FireRaidRun
// (gilde.exe 0x4ee960). The phase machine, trigger gate, fire-damage arithmetic
// and per-tick effect application are reproduced 1:1; the .esc script VM, 3D
// sound, universe-slot swap and the GUI are left to the engine (here, the
// FireEventHooks mock). The damage constants are the exact IEEE-754 doubles/
// floats recovered via get_bytes.

namespace guild::world {

namespace {
// flt_61FCFC = 0x3E2AAAAB  == 0.16666667f  (1/6 work-hour scalar)
constexpr float kFireRate1   = 0.16666667f;        // flt_61FCFC @0x61FCFC
// flt_61FD00 = 0x40000000  == 2.0f
constexpr float kFireRate2   = 2.0f;               // flt_61FD00 @0x61FD00
// dbl_61FD08 = 0x3F60624DD2F1A9FC == 0.002 (catalyst per-unit scalar)
constexpr double kCatScaleA  = 0.002;              // dbl_61FD08 @0x61FD08
// dbl_61FD10 = 0x3FE6666666666666 == 0.7 (catalyst weight)
constexpr double kCatScaleB  = 0.7;                // dbl_61FD10 @0x61FD10
} // namespace

void FireEventInit(FireEvent* ev, i32 eventId, i32 owner, i32 value) {
    ev->eventId    = eventId;
    ev->flags      = kFireFlagAuthoritative;  // single-player owner path
    ev->step       = 0;                       // start at ignition
    ev->pendingCmd = kFireNone;
    ev->owner      = owner;
    ev->value      = value;
    ev->subPhase   = 0;
    for (int i = 0; i < kFireMaxSpawns; ++i)
        ev->spawnIds[i] = kFireNone;
}

// gilde.exe 0x4ee97d — leading trigger gate.
bool FireEventShouldRunBody(const FireEvent& ev, bool commandResolved) {
    // if ( (flags & 2) != 0 && pendingCmd != -1 ) { if (!resolved) return; clear; }
    if ((ev.flags & kFireFlagAuthoritative) != 0 && ev.pendingCmd != kFireNone)
        return commandResolved;  // body runs only once the in-flight cmd resolves
    return true;
}

// gilde.exe 0x4eef4f..0x4eefe2 — per-tick fire damage.
i32 FireEventComputeDamage(i32 remainingValue, i32 dailyHourOutput,
                           bool hasCatalyst, i32 catalyst) {
    // v32 = (double)dailyHourOutput * flt_61FCFC * flt_61FD00;  v53 = (int)v32;
    double dmg  = static_cast<double>(dailyHourOutput) * kFireRate1 * kFireRate2;
    i32    base = static_cast<i32>(dmg);             // v53 (truncated)
    // v54 = hasCatalyst ? (double)catalyst*dbl_61FD08*dbl_61FD10 + 1.0 : 1.0;
    double factor = 1.0;
    if (hasCatalyst)
        factor = static_cast<double>(catalyst) * kCatScaleA * kCatScaleB + 1.0;
    // v34 = (double)remainingValue - (double)v53 * v54;  value = (int)v34;
    double newValue = static_cast<double>(remainingValue)
                    - static_cast<double>(base) * factor;
    return static_cast<i32>(newValue);
}

// gilde.exe 0x4ef02d — burn-progress voice tier.
int FireEventVoiceTier(i32 delta) {
    if (delta <= 0)
        return 2;       // total loss
    if (delta <= 10)
        return 1;       // heavy
    if (delta <= 25)
        return 0;       // moderate
    return -1;          // no line
}

// gilde.exe case 0 (ignition).
int FireEventIgnite(FireEvent* ev, int candidateBuildings, FireEventHooks& hooks) {
    int spawned = 0;
    // The original only spawns when authoritative and on the first ignition wave
    // (subPhase == 1 in the building-scan branch); we gate on authoritative and
    // fill spawn slots for each candidate up to the cap.
    if ((ev->flags & kFireFlagAuthoritative) != 0) {
        for (int b = 0; b < candidateBuildings && spawned < kFireMaxSpawns; ++b) {
            i32 cmd = hooks.SpawnFire(ev->owner);
            ev->spawnIds[spawned] = cmd;
            // v28 = VIBE_Math_RandomModulo(2u); QueueRequestFlag55(cmd, v28 + 1);
            int flag = hooks.RandomModulo(2) + 1;
            hooks.SetFireFlag(cmd, flag);
            ++spawned;
        }
    }
    // v29 = subPhase; if (v29 < 2u) subPhase = v29 + 1;   (cap at 2)
    if (ev->subPhase < 2)
        ev->subPhase = static_cast<u8>(ev->subPhase + 1);
    return spawned;
}

// gilde.exe case 1 (burn tick).
int FireEventBurnTick(FireEvent* ev, i32 dailyHourOutput, bool hasCatalyst,
                      i32 catalyst, FireEventHooks& hooks, int chronicleTextId) {
    i32 prev = ev->value;
    i32 base = static_cast<i32>(static_cast<double>(dailyHourOutput)
                                * kFireRate1 * kFireRate2);  // v53 for the delta
    ev->value = FireEventComputeDamage(prev, dailyHourOutput, hasCatalyst, catalyst);

    // gilde.exe 0x4ef02d: v37 = (*(int*)(person+89) >> 24) - v53, i.e. the
    // building's wealth byte minus the production base consumed this tick. With
    // the wealth tracked as ev->value, the recoverable delta is (prev - base).
    int tier = FireEventVoiceTier(prev - base);

    hooks.AdvanceTime(4);                          // VIBE_GameTime_Advance(...,4)

    // while subPhase < 3 reset step to 0 to loop; else the event ends.
    if (ev->subPhase < 3)
        ev->step = 0;

    // Destroyed -> record a chronicle entry (the History_Notify* hook).
    if (ev->value <= 0)
        hooks.RecordChronicle(ev->owner, chronicleTextId);

    return tier;
}

// gilde.exe case -1/-2 (teardown).
int FireEventTeardown(FireEvent* ev, FireEventHooks& hooks) {
    int cancelled = 0;
    for (int i = 0; i < kFireMaxSpawns; ++i) {
        if (ev->spawnIds[i] != kFireNone) {
            // VIBE_Command_QueueRequestSingle49(spawnId) — cancel the spawn.
            hooks.SetFireFlag(ev->spawnIds[i], 0);   // reuse hook as the cancel sink
            ev->spawnIds[i] = kFireNone;
            ++cancelled;
        }
    }
    ev->step = kFireNone;  // mark handler done
    return cancelled;
}

} // namespace guild::world

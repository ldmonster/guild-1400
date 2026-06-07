#pragma once
// Mission reward / outcome bodies — the deferred remainder of the VIBE_Mission_*
// reward/outcome logic, building on world/mission.h (the slot table) and
// world/mission_rules.h (the descriptor lookups + outcome decode already done).
//
// Translated functions:
//   VIBE_Mission_FinishByOwner    0x539e48  (owner -> fail every owned mission)
//   VIBE_Mission_RunResultDialog  0x53adc8  (result-code -> load-session decision)
//
// FinishByOwner walks the 128-slot mission table (byte_122FEC0, stride 36) and runs
// the failure path on every occupied slot whose owner matches; we recover the slot
// scan + the per-slot fail invocation (the dialog frame loop is the engine's, so it
// is routed through a hook). RunResultDialog's recoverable rule is the result-code
// computation v6 = (dword_63CC30 != 0) + 1 and the v6 == 2 -> reload-session branch.
#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// VIBE_Mission_FinishByOwner 0x539e48.
// ===========================================================================
// The original passes the matched slot's type byte, an OR'd flag (dword_11BC2D0 |
// 0x80), and the slot byte-offset to VIBE_Mission_RunFailureDialog for every
// occupied slot owned by `owner`. We surface that per-slot fail callback as a hook.
//   slot occupied: byte_122FEC0[slot*36] != 0   (type != 0)
//   owner match  : dword_122FEC4[slot] == owner
using MissionFailHook = void (*)(int slot, u8 type, void* ctx);

// gilde.exe 0x539e48 — fails every owned mission. Returns the number of slots that
// matched (and were failed via `failHook`, when non-null). The flag byte the
// original OR's with 0x80 is passed straight through to the dialog and has no rule
// effect on the scan, so it is omitted here.
int MissionFinishByOwner(i32 owner, MissionFailHook failHook, void* ctx);

// ===========================================================================
// VIBE_Mission_RunResultDialog 0x53adc8.
// ===========================================================================
// After the result dialog's frame loop, the original computes the result code only
// once the active mission id has cleared (dword_63CC24 == -1):
//   v6 = (dword_63CC30 != 0) + 1;   // 1 when no reward claimed, 2 when claimed
// and, on v6 == 2, sets dword_63CC30 = 1 and loads the session. We recover the
// result-code computation and the load-session decision.
//   rewardClaimed mirrors (dword_63CC30 != 0).
int  MissionResultCode(bool rewardClaimed);
// True when the result code triggers the session reload (code == 2).
bool MissionResultTriggersReload(int resultCode);

} // namespace guild::world

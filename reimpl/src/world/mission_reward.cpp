#include "world/mission_reward.h"

#include "world/mission.h"   // g_missionSlots, kMissionSlotCount

// Faithful port of VIBE_Mission_FinishByOwner (gilde.exe 0x539e48) and the
// result-code rule of VIBE_Mission_RunResultDialog (0x53adc8). The slot scan walks
// the same 128-slot table mission.cpp models; the failure-dialog frame loop is
// routed through MissionFailHook. RunResultDialog's recoverable core is the
// result-code computation and the reload decision.

namespace guild::world {

// gilde.exe 0x539e48 — scan 128 slots (stride 9 dwords == 36 bytes), fail owned.
//   do { if (byte_122FEC0[v2*4] && owner == dword_122FEC4[v2])
//          result = RunFailureDialog(type, v3); v2 += 9; } while (v2 != 1152);
//   return result;
// The binary RETURNS the last RunFailureDialog result (or the owner unchanged when
// nothing matched). RunFailureDialog is a GUI frame-loop boundary, routed here
// through failHook. We instead return the MATCHED COUNT — a faithful observable of
// the scan itself (loop bound + occupancy + owner compare are reproduced exactly);
// the binary's edx/eax return is the dialog boundary value, not recoverable here.
int MissionFinishByOwner(i32 owner, MissionFailHook failHook, void* ctx) {
    int matched = 0;
    for (int slot = 0; slot < kMissionSlotCount; ++slot) {
        if (g_missionSlots[slot].type == 0)            // byte_122FEC0[v2*4] (occupied)
            continue;
        if (g_missionSlots[slot].owner != owner)       // dword_122FEC4[v2] == v1
            continue;
        ++matched;
        if (failHook)
            failHook(slot, g_missionSlots[slot].type, ctx);  // RunFailureDialog
    }
    return matched;
}

// gilde.exe 0x53ae46 — v6 = (dword_63CC30 != 0) + 1.
int MissionResultCode(bool rewardClaimed) {
    return (rewardClaimed ? 1 : 0) + 1;
}

// gilde.exe 0x53ae6a — v6 == 2 -> InitOrLoadSession.
bool MissionResultTriggersReload(int resultCode) {
    return resultCode == 2;
}

} // namespace guild::world

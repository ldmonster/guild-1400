// See real_reaper_wiring.h. Binds the reconstructed full Reaper functions to the
// live NpcEventHooks bridge.
#include "sim/real_reaper_wiring.h"

#include "sim/npcevent_steps.h"        // NpcEventHooks / SetNpcEventHooks
#include "sim/npcevent_reaper_full.h"  // ReaperApproachTarget / ...MoveTowardTarget / ...

namespace guild::sim {

void InstallRealReaperWiring() {
    // Process-lifetime table the global hook pointer references. Only the four
    // reaper leaves are bound; every other field stays null (= inert default,
    // since npcevent_steps.cpp null-checks each hook before use).
    static NpcEventHooks hooks{};
    hooks.reaperApproach    = &ReaperApproachTarget;     // 0x4d8c34
    hooks.reaperMove        = &ReaperMoveTowardTarget;    // 0x4d8f74
    hooks.reaperCachePose   = &ReaperCacheTargetPose;     // 0x4d92a4
    hooks.reaperUpdateSound = &ReaperUpdateSoundPos;      // 0x4d9440
    SetNpcEventHooks(&hooks);
}

} // namespace guild::sim

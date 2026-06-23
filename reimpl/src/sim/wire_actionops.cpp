// See wire_actionops.h. Binds the one bindable field across the eight action-ops
// bridges this agent owns — MiscActionHooks::findNearby — to its real reconstructed
// leaf (VIBE_Character_FindNearbyInRadius @0x40507c, character_social.h). Glue only;
// no module logic. The other seven bridges (Kill/Reset/Run/Wait/Flow/Ops4/Op85) are
// ZERO-bindable (every field a platform/render/network/process-global boundary) and
// get no installer here, per the wiring rule.
#include "sim/wire_actionops.h"

#include "sim/charaction_misc.h"     // MiscActionHooks / Set/GetMiscActionHooks
#include "sim/character_social.h"    // FindNearbyInRadius (the real proximity scan)

namespace guild::sim {

namespace {

// VIBE_Character_FindNearbyInRadius @0x40507c — the proximity scan. The original
// (see VIBE_CharAction_RotateInterpolate @0x40b998) calls it as
//   VIBE_Character_FindNearbyInRadius(self, outBuf /*v19[32]*/, radius)
// and uses the nonzero return as a boolean, then reads outBuf[0] as the first hit.
// The MiscActionHooks `findNearby` field is the single-result form of that same
// leaf — we replay the array scan and surface the first hit (or null). The 16-slot
// buffer matches the original's "up to 16 hits" cap (the v6<64 byte cursor); the
// caller-supplied radius is forwarded unchanged (RotateInterpolate passes 30.0).
Character* WaFindNearby(Character* ch, float radius) {
    Character* buf[16] = {};
    int n = FindNearbyInRadius(ch, buf, 16, radius);
    return (n > 0) ? buf[0] : nullptr;
}

// Process-lifetime wired hook table (the global hook ptr references this).
MiscActionHooks g_misc{};

} // namespace

void InstallRealActionOpsWiring() {
    // Seed from the module's inert defaults (NON-null stubs) so the fields we do not
    // bind keep their safe stubs — several CharAction step call sites invoke these
    // hooks WITHOUT a null-check, so a zero-initialised table would crash.
    g_misc = GetMiscActionHooks();
    g_misc.findNearby = &WaFindNearby;
    // attachMovementAni / attachAni / stepMotionQueue / checkQueueReady / stopSample
    // / sceneSlotIndex / worldToTile: the reconstructed cousins operate on different
    // runtime structs (CharActor3*/ChActor*/MotionQueueNode*) than the Character*
    // the hook carries, so a faithful bind is not available -> inert (see header).
    SetMiscActionHooks(&g_misc);
}

} // namespace guild::sim

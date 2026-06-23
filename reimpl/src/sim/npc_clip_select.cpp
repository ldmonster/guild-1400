// =============================================================================
// npc_clip_select — per-NPC animation CLIP SELECTION (1:1 of the clip-decision
// core of VIBE_Character_Update @0x405148). See npc_clip_select.h for the full
// engine-source derivation. This is a PURE SELECTOR (state -> clip name); the
// attach/advance are owned by the render/factory/pose modules.
// =============================================================================
#include "sim/npc_clip_select.h"

#include "sim/charaction.h" // ActionTypeId, ActionType()

namespace guild::sim {

// ---------------------------------------------------------------------------
// Exact clip-name constants (get_bytes-sourced; addresses in the header).
// ---------------------------------------------------------------------------
const char kClipGait[]      = "bewegung/gehen";          // aBewegungGehen   0x610170
const char kClipGaitCart[]  = "bewegung/karren_ziehen";  // aBewegungKarren  0x61024c
const char kClipTurn90R[]   = "bewegung/dreh_90_rechts"; // aBewegungDreh90  0x61070c
const char kClipIdleStand[] = "stehen/stehen_newnoise";  // aStehenStehenNe  0x610158
const char kClipIdleSit[]   = "sitzend/sitz_newnoise";   // aSitzendSitzNew  0x6103bc

// ---------------------------------------------------------------------------
// Action-type -> catalog clip name. The original's action catalog
// (VIBE_CharAction_RegisterHandlers @0x40be30, DeclareAction @0x405558) gives a
// non-empty animName to exactly three clip-bearing types; every other type
// carries the empty name byte_610134 ("") and attaches its own clip inside its
// handler at runtime. We reproduce the catalog's clip-bearing entries 1:1 (the
// exact strings from get_bytes, NOT the repo's slightly-truncated DeclareAction
// transcription) so this selector is self-contained and faithful.
// ---------------------------------------------------------------------------
const char* ActionClipName(int actionType, bool cart) {
    switch (actionType) {
        case kActTurnAnim:      // 7   "bewegung/dreh_90_rechts" 0x61070c
            return kClipTurn90R;
        case kActWalk:          // 45  WalkUpdate          -> gait 0x610170
        case kActWalkOnPath:    // 58  Command_Dispatcher  -> gait 0x610170
            // The full walk's morph-init (charaction_motion.cpp:464 / ch_WalkOnPath
            // @0x40a4d4) picks the cart variant "bewegung/karren_ziehen" when the
            // avatar is pulling a cart, else "bewegung/gehen".
            return cart ? kClipGaitCart : kClipGait;
        default:
            // Empty catalog name (byte_610134): the handler attaches its own clip
            // at runtime (take/drop/sound/...); the selector invents nothing.
            return "";
    }
}

// ---------------------------------------------------------------------------
// THE CLEAN ENTRY — see header. Distilled from VIBE_Character_Update @0x405148:
//   action-head present  -> the action's catalog clip (walk -> gait)
//   no action, moving    -> gait (the session "walking person" == active walk)
//   no action, idle      -> sit/stand idle (+ playback-speed jitter)
// ---------------------------------------------------------------------------
ClipSelection SelectNpcClip(const NpcClipState& st) {
    ClipSelection sel;

    // Gate 1 — *(actor+296) != 0 : the actor has an active action. Its clip is
    // whatever its handler attaches; for the catalog clip-bearing types we know
    // it exactly. The walk types (45/58) are the gait.
    if (st.hasAction) {
        const char* name = ActionClipName(st.actionType, st.cart);
        if (st.actionType == kActWalk || st.actionType == kActWalkOnPath) {
            sel.kind = ClipKind::Gait;
            sel.clip = name; // gait (or cart variant)
        } else if (name[0] != '\0') {
            sel.kind = ClipKind::Action;
            sel.clip = name; // a named action clip from the catalog (e.g. turn)
        } else {
            // Action active but its clip is runtime-attached by the handler; the
            // selector does not choose one (rule 8 — no invented clip).
            sel.kind = ClipKind::None;
            sel.clip = "";
        }
        return sel;
    }

    // Gate 2 — no action: the session's movement state stands in for an active
    // walk action. A MOVING person resolves to the gait exactly as a live actor's
    // type-45/58 walk would (the engine never reaches the idle attach for a
    // walking actor because the walk action holds +296).
    if (st.moving) {
        sel.kind = ClipKind::Gait;
        sel.clip = kClipGait; // a session mover never pulls a cart in this bridge
        return sel;
    }

    // Gate 3 — the idle/social branch (the `else` of the +296 gate in
    // VIBE_Character_Update): attach sit or stand, then seed the playback-speed
    // jitter. `if (*(actor+140) & 0x10)` picks SIT.
    sel.kind = ClipKind::Idle;
    sel.idle = true;
    sel.sit  = st.sit;
    sel.clip = st.sit ? kClipIdleSit : kClipIdleStand;
    sel.idlePlaybackScale = kIdlePlaybackJitterScale; // dbl_6103FC == 0.01
    sel.idlePlaybackBase  = kIdlePlaybackBase;        // + 1.0
    return sel;
}

// ---------------------------------------------------------------------------
// SESSION BRIDGE HELPER — the per-person, per-frame selection keyed only on the
// live movement state (+0x74). Persons in session_persons3d have no attached
// live action queue, so `hasAction` is false and the gait-vs-idle decision is
// the `moving`/`sit` pair. This is the exact handoff for CityView3D's clip flip.
// ---------------------------------------------------------------------------
ClipSelection SelectPersonClipFromMovement(bool moving, bool sit) {
    NpcClipState st;
    st.hasAction = false;
    st.moving    = moving;
    st.sit       = sit;
    return SelectNpcClip(st);
}

} // namespace guild::sim

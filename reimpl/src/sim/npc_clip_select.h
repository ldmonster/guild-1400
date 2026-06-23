#pragma once
// =============================================================================
// npc_clip_select — the per-NPC animation CLIP SELECTION runtime (1:1).
//
// THE GAP THIS FILLS
// ---------------------------------------------------------------------------
// Persons in the 3D city view (play::CityView3D / session_persons3d) currently
// play only the factory PRELOAD set with the idle first ("stehen/stehen_newnoise",
// gait "bewegung/gehen") regardless of what the person is doing — the wave-3/4
// documented "clip selection" named gap. The pose driver
// (render::UpdateSkeletonPose @0x5cd1d8 — VIBE_Anim_UpdateSkeletonPose) only
// ADVANCES whatever clip is attached; it does not CHOOSE one. The choice — gait
// (walking) vs idle (standing/sitting) vs an action clip — is the per-frame
// character driver's job, factored here.
//
// THE ENGINE SOURCE (the reference of record)
// ---------------------------------------------------------------------------
// VIBE_Character_Update @0x405148 runs, once per frame, over every live actor
// (dword_66F0D0[0..511]). The clip a person plays this frame is decided by two
// gates inside that loop:
//
//   v6 = *(actor + 296);              // +0x128 action-queue HEAD (ActionNode*)
//   if (v6) {                          // HAS AN ACTIVE ACTION
//       VIBE_ActionQueue_DispatchCurrent(actor);   // the action attaches its clip
//   } else {                           // NO ACTION -> idle/social branch
//       ... FindNearbyInRadius -> maybe enqueue a type-45 walk-to-talk ...
//       // and, when no current anim (+112==0) and the idle-anim is pending
//       // (+141 & 0x10):
//       if (*(actor+140) & 0x10)       // +0x8C flagsA bit 0x10 == SIT gate
//           VIBE_Character_AttachAni(actor, "sitzend/sitz_newnoise", 0);  // 0x6103bc
//       else
//           VIBE_Character_AttachAni(actor, "stehen/stehen_newnoise", 0); // 0x610158
//       // then: anim->playbackSpeed (+96) = RandomFloatScaled()*0.01 + 1.0;  // dbl_6103FC
//       //       anim->stateFlags (+109) &= ~0x10; clear +141 0x10; set +141 0x20;
//   }
//
// The clip an ACTION attaches is the action-type catalog entry's animName
// (VIBE_CharAction_RegisterHandlers @0x40be30, the DeclareAction @0x405558
// tuples). The clip-bearing action types (the rest carry an empty catalog name
// "" @0x610134 and attach their own clip inside their handler):
//
//   type  7  TurnStepActionUpdate    @0x406a18  "bewegung/dreh_90_rechts" 0x61070c
//   type 45  CharAction_WalkUpdate   @0x40a0b8  "bewegung/gehen"          0x610170  <- GAIT
//   type 58  Command_Dispatcher      @0x40a4d4  "bewegung/gehen"          0x610170  <- GAIT (walk-on-path)
//
// The full walk-on-path (type 58, ch_WalkOnPath @0x40a4d4 / WalkUpdate @0x40a0b8,
// reconstructed in src/sim/charaction_motion.* / charaction_walk.*) attaches the
// gait at its morph-init phase: animName = cart ? "bewegung/karren_ziehen"
// (0x61024c) : "bewegung/gehen" (0x610170) — charaction_motion.cpp:464.
//
// So the gait-vs-idle decision IS keyed on whether the NPC is MOVING: a moving
// person has an active walk action (type 45/58) whose clip is the gait; an idle
// person has no action and gets the stand (or sit) clip. The session bridge
// keys this on the live movement state the integration keeps at record +0x74
// (play::wire_npc_movement.h kMoveStateOff == 1 == has an active path).
//
// 1:1 NOTES / NAMED GAPS (rule 8)
// ---------------------------------------------------------------------------
//  * This is a PURE SELECTOR: state -> clip name. The actual attach
//    (VIBE_Character_AttachAni @0x404038 -> the "character/%s/%s_%s.baf" stream
//    load) and the pose ADVANCE (UpdateSkeletonPose) are owned by the existing
//    render/factory modules; this module only decides WHICH clip handle they get.
//  * The idle playback-speed jitter (RandomFloatScaled()*0.01+1.0, dbl_6103FC
//    @0x6103FC) is reported alongside the idle clip so the caller can seed the
//    attached anim's +96 exactly as the engine does. The RNG itself
//    (VIBE_Math_RandomFloatScaled @0x58b910) is the engine's; this selector
//    returns the SCALE (0.01) and the +1.0 base, not a rolled value.
//  * The action-head -> clip lookup uses the SAME catalog the live engine builds
//    (sim::ActionType(type).animName, charaction.cpp RegisterHandlers). For the
//    two clip-bearing walk types we also expose the cart variant
//    ("bewegung/karren_ziehen") the morph-init picks, since the empty catalog
//    name for type 58's runtime attach is filled there, not in DeclareAction.
// =============================================================================
#include "guild/common/types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// The exact clip-name string constants (get_bytes-sourced; addresses noted).
// ---------------------------------------------------------------------------
extern const char kClipGait[];        // 0x610170 "bewegung/gehen"
extern const char kClipGaitCart[];    // 0x61024c "bewegung/karren_ziehen"
extern const char kClipTurn90R[];     // 0x61070c "bewegung/dreh_90_rechts"
extern const char kClipIdleStand[];   // 0x610158 "stehen/stehen_newnoise"
extern const char kClipIdleSit[];     // 0x6103bc "sitzend/sitz_newnoise"

// dbl_6103FC @0x6103FC — the idle-anim playback-speed jitter scale (== 0.01).
// The engine seeds the attached idle anim's playbackSpeed (+96) with
// RandomFloatScaled()*kIdlePlaybackJitterScale + kIdlePlaybackBase.
constexpr float kIdlePlaybackJitterScale = 0.01f; // dbl_6103FC
constexpr float kIdlePlaybackBase        = 1.0f;

// ---------------------------------------------------------------------------
// Character record offsets the selector reads (the +0x128/+0x8C gates from
// VIBE_Character_Update @0x405148; mirrors sim/character.h).
// ---------------------------------------------------------------------------
enum ClipSelectOffset : int {
    kActionHeadOff = 0x128, // +296 action-queue head (ActionNode*); 0 == no action
    kFlagsAOff     = 0x8C,  // +140 flagsA: bit 0x10 == SIT idle gate
    kFlagsBOff     = 0x8D,  // +141 flagsB: bit 0x10 idle-anim pending, 0x20 attached
};
constexpr u8 kFlagsASitBit       = 0x10; // *(actor+140) & 0x10 -> sit vs stand
constexpr u8 kFlagsBIdlePending  = 0x10; // *(actor+141) & 0x10 -> idle attach due
constexpr u8 kFlagsBIdleAttached = 0x20; // *(actor+141) & 0x20 -> idle attached

// ---------------------------------------------------------------------------
// What kind of clip the selector chose (for the caller / tests / digest).
// ---------------------------------------------------------------------------
enum class ClipKind {
    None,     // no clip to attach this frame (action attaches its own / nothing due)
    Gait,     // walking — "bewegung/gehen" (or cart "bewegung/karren_ziehen")
    Idle,     // standing/sitting idle — "stehen/.." / "sitzend/.."
    Action,   // a named action clip from the action catalog (e.g. turn)
};

struct ClipSelection {
    ClipKind    kind = ClipKind::None;
    const char* clip = "";    // the clip name to feed the attach/pose driver ("" == none)
    bool        idle = false; // true when this is the no-action idle branch
    bool        sit  = false; // idle sub-kind: true => sit, false => stand
    // For idle: the engine seeds playbackSpeed = RandomFloatScaled()*scale + base.
    float       idlePlaybackScale = kIdlePlaybackJitterScale;
    float       idlePlaybackBase  = kIdlePlaybackBase;
};

// ---------------------------------------------------------------------------
// THE NPC STATE the selector keys on. This is the deterministic view of the
// two engine gates (action-head presence + the sit flag), plus the movement
// state the SESSION integration keeps at record +0x74 (the bridge that drives
// persons without attaching a full live Character action queue).
// ---------------------------------------------------------------------------
struct NpcClipState {
    // The engine's primary gate: the action-queue head type.
    bool hasAction   = false; // *(actor+296) != 0
    int  actionType  = 0;     // ActionNode.type (+9) of the head action (when hasAction)
    bool cart        = false; // the walk's morph-init cart variant (avatar+? cart flag)

    // The idle sub-gate (no-action branch only): the SIT flag (+140 & 0x10).
    bool sit         = false;

    // The session bridge gate (persons without a live action queue): the
    // movement state at record +0x74 (wire_npc_movement: 1 == active path).
    // When `hasAction` is false this lets a MOVING person resolve to the gait
    // exactly as a live actor's walk action would (the type-45/58 clip).
    bool moving      = false;
};

// ---------------------------------------------------------------------------
// THE CLEAN ENTRY — NPC state -> clip handle for the pose driver.
//
// gilde.exe 0x405148 (the clip-decision core of VIBE_Character_Update),
// distilled to the SELECTION (no attach, no advance):
//
//   if (st.hasAction)            -> the action head's clip (catalog lookup);
//                                   walk types 45/58 => gait (cart variant honored)
//   else if (st.moving)          -> gait ("bewegung/gehen") — the session's
//                                   "person is walking" equivalent of an active
//                                   type-45/58 walk action
//   else                         -> idle: sit ? "sitzend/sitz_newnoise"
//                                              : "stehen/stehen_newnoise"
//                                   (+ the playback-speed jitter to seed +96)
//
// Returns ClipKind::None with clip "" when an action is active but its catalog
// clip is empty (the handler attaches its own clip at runtime — e.g. take/drop):
// the selector does not invent a clip for those (rule 8).
// ---------------------------------------------------------------------------
ClipSelection SelectNpcClip(const NpcClipState& st);

// Convenience: the action-type -> catalog clip name lookup the selector uses
// (sim::ActionType(type).animName, with the two walk types resolving to the gait
// and its cart variant). Returns "" for a type whose catalog name is empty.
const char* ActionClipName(int actionType, bool cart);

// ---------------------------------------------------------------------------
// SESSION BRIDGE HELPER — the per-person, per-frame selection the
// session_persons3d / UpdateSessionPersons3D path uses. Given only the live
// MOVEMENT state at record +0x74 (the bridge's motion model; persons there have
// no attached live action queue), pick gait while moving, stand while idle. This
// is the exact handoff the orchestrator wires into CityView3D::SetBoundPersonClip
// (the wave-4 §4 handoff). `sit` lets a seated person idle on the sit clip.
// ---------------------------------------------------------------------------
ClipSelection SelectPersonClipFromMovement(bool moving, bool sit = false);

} // namespace guild::sim

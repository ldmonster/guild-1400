# NPC animation CLIP SELECTION (wave 8, agent W8-NPCCLIP)

Status: **landed** (2026-06-14). The per-NPC animation **clip-selection runtime**
— which animation a person plays *right now* (gait vs idle vs an action clip) —
reconstructed 1:1 from the clip-decision core of `VIBE_Character_Update`
@0x405148. This is the wave-3/4 named "clip selection" gap: previously persons in
the 3D city (`play::CityView3D` / `session_persons3d`) played only the factory
PRELOAD set with the idle first, movement state notwithstanding. The pose driver
(`render::UpdateSkeletonPose` @0x5cd1d8 — `VIBE_Anim_UpdateSkeletonPose`) only
*advances* whatever clip is attached; it never *chooses* one. The choice is this
module.

Module (NEW, owned):
* `src/sim/npc_clip_select.{h,cpp}` — the pure state→clip selector.
* `tests/unit/npc_clip_select_test.cpp` — suite `NpcClipSelect`, **11 tests, 74
  checks, 0 failures** (asset-free; golden clip strings are get_bytes values).

Read (NOT edited), entries referenced: `src/sim/character.{h,cpp}`
(`VIBE_Character_Update`), `src/sim/charaction.{h,cpp}` (the action catalog +
`ActionTypeId`), `src/sim/charaction_motion.cpp` (the walk morph-init gait
attach), `src/play/wire_npc_movement.h` (the +0x74 movement state),
`src/play/session_persons3d.h` (the bind/handoff), `src/play/city_view3d.h`
(`MoveBoundPerson` / the clip-flip handoff).

## The engine source (the reference of record)

`VIBE_Character_Update` @0x405148 runs once per frame over every live actor
(`dword_66F0D0[0..511]`). The clip a person plays this frame is two gates:

```
v6 = *(actor + 296);                 // +0x128 action-queue HEAD (ActionNode*)
VIBE_Character_CheckAniMorph(actor);
if (v6) {                            // GATE 1 — HAS AN ACTIVE ACTION
    VIBE_ActionQueue_DispatchCurrent(actor);   // the action attaches its clip
} else {                            // GATE 2/3 — no action -> idle/social branch
    if (FindNearbyInRadius(actor, buf, 20.0))  // a neighbour in range:
        InsertActionVararg(actor, ..., 45, ...);   // enqueue a type-45 walk-to-talk
    ...
    // when no current anim (+112==0) AND idle-anim pending (+141 & 0x10):
    if (*(actor+140) & 0x10)         // +0x8C flagsA bit 0x10 == SIT gate
        VIBE_Character_AttachAni(actor, "sitzend/sitz_newnoise", 0);  // 0x6103bc
    else
        VIBE_Character_AttachAni(actor, "stehen/stehen_newnoise", 0); // 0x610158
    anim->playbackSpeed(+96) = VIBE_Math_RandomFloatScaled()*0.01 + 1.0; // dbl_6103FC
    anim->stateFlags(+109) &= ~0x10;  // clear loop-end
    *(actor+141) &= ~0x10;  *(actor+141) |= 0x20;   // pending -> attached
}
```

The clip an ACTION attaches is the action-type catalog entry's `animName`
(`VIBE_CharAction_RegisterHandlers` @0x40be30, the `DeclareAction` @0x405558
tuples). Exactly three types are clip-bearing; the rest carry the empty catalog
name `byte_610134` ("") and attach their own clip inside their handler:

| type | handler | addr | catalog clip (addr) |
|---|---|---|---|
| 7  | TurnStepActionUpdate  | 0x406a18 | `bewegung/dreh_90_rechts` (0x61070c) |
| 45 | CharAction_WalkUpdate | 0x40a0b8 | `bewegung/gehen` (0x610170) — **GAIT** |
| 58 | Command_Dispatcher    | 0x40a4d4 | `bewegung/gehen` (0x610170) — **GAIT** (walk-on-path) |

The full walk-on-path (type 58 / `WalkUpdate`) attaches the gait at its
morph-init phase: `cart ? "bewegung/karren_ziehen" (0x61024c) : "bewegung/gehen"`
(`charaction_motion.cpp:464` / `ch_WalkOnPath` @0x40a4d4).

So **gait-vs-idle is keyed on whether the NPC is MOVING**: a moving person holds
an active walk action (type 45/58) whose clip is the gait, so it never reaches
the idle attach; an idle person has no action and gets the stand (or sit) clip.

## Constants (get_bytes-sourced, verified)

| symbol | addr | value |
|---|---|---|
| `aBewegungGehen`  | 0x610170 | `"bewegung/gehen"` |
| `aBewegungKarren` | 0x61024c | `"bewegung/karren_ziehen"` |
| `aBewegungDreh90` | 0x61070c | `"bewegung/dreh_90_rechts"` |
| `aStehenStehenNe` | 0x610158 | `"stehen/stehen_newnoise"` |
| `aSitzendSitzNew` | 0x6103bc | `"sitzend/sitz_newnoise"` |
| `byte_610134`     | 0x610134 | `""` (empty: runtime-attached clip) |
| `dbl_6103FC`      | 0x6103FC | `0.01` (idle playback-speed jitter scale) |

Note: the repo's existing `charaction.cpp` RegisterHandlers transcribes type 7's
clip as `"bewegung/dreh"` (truncated) and does not register type 58. This module
uses the EXACT engine strings directly (not that catalog) so it is self-contained
and 1:1; the existing module is read-only here and was not changed.

## The clean entry (state → clip handle for the pose driver)

```cpp
struct NpcClipState {
    bool hasAction;   // *(actor+296) != 0
    int  actionType;  // ActionNode.type (+9) of the head action
    bool cart;        // walk morph-init cart variant
    bool sit;         // +140 & 0x10 (idle sub-gate)
    bool moving;      // session bridge: record +0x74 == 1 (active path)
};
struct ClipSelection {
    ClipKind kind;            // None | Gait | Idle | Action
    const char* clip;         // the clip name to attach ("" == none)
    bool idle, sit;
    float idlePlaybackScale, idlePlaybackBase;   // 0.01, 1.0 (seed +96)
};
ClipSelection SelectNpcClip(const NpcClipState&);
const char*   ActionClipName(int actionType, bool cart);
ClipSelection SelectPersonClipFromMovement(bool moving, bool sit = false);
```

Decision (1:1 with the gates above):
1. `hasAction` → the action's catalog clip. Walk (45/58) ⇒ `ClipKind::Gait`
   (gait, cart variant honored); turn (7) ⇒ `ClipKind::Action`
   (`bewegung/dreh_90_rechts`); any runtime-attach type (empty catalog name) ⇒
   `ClipKind::None`, clip "" (the handler owns it — **rule 8, no invented clip**).
2. else `moving` → `ClipKind::Gait` `bewegung/gehen` (the session "walking
   person" equivalent of an active type-45/58 walk action).
3. else → `ClipKind::Idle`: `sit ? "sitzend/sitz_newnoise" :
   "stehen/stehen_newnoise"`, plus the playback-speed jitter seed (×0.01 + 1.0).

The action gate is **exclusive** (checked first, like the engine): a take action
with `moving=true` still selects `None`, not the gait.

## THE EXACT HANDOFF (session_persons3d → pose driver)

Persons in `session_persons3d` are factory records driven by the tile-grain
movement bridge (`wire_npc_movement`), NOT full live `Character` action queues —
so for them `hasAction` is false and the gait-vs-idle decision is purely the
`+0x74` movement state. The per-person, per-frame handoff:

```
UpdateSessionPersons3D / UpdateSessionPersons3DPositions   (per frame, per person)
  GetEntityMovePos(id).active   // wire_npc_movement.h: record +0x74 == 1 == moving
    -> sim::SelectPersonClipFromMovement(moving, sit)
       moving  -> ClipKind::Gait  "bewegung/gehen"
       idle    -> ClipKind::Idle  "stehen/stehen_newnoise"  (sit -> "sitzend/sitz_newnoise")
    -> CityView3D::SetBoundPersonClip(id, sel.clip)   // the wave-4 §4 handoff:
       flips the person's pose to the named factory-preloaded clip; the pose
       driver (render::UpdateSkeletonPose @0x5cd1d8) then ADVANCES that clip
       (city_view3d.cpp:~1002, the same gait/stehen preload set CityView3D binds).
```

Both selected clips (`bewegung/gehen`, `stehen/stehen_newnoise`) are already in
the factory PRELOAD set (`character_factory.cpp` `kGaitIdle`), so the flip needs
no new stream load — it just selects which preloaded clip plays. This is the
**orchestrator's wire-up** (a one-line per-person call in the session update +
the `SetBoundPersonClip` already declared in `city_view3d.h`); this module does
NOT edit any bind-site/integration file (CLAUDE.md ownership / wave-8 brief).

For a fully live actor (when the session attaches a real `Character` action
queue — a future integration), `SelectNpcClip` consumes the action head type
(`sim::ActionType(type)` / the catalog) and the +140/+141 flags directly,
reproducing the engine's clip choice for ALL action types, not just movement.

## Named gaps (rule 8 — none faked)

* **Attach + advance are not this module.** This is a pure selector (state →
  clip name). `VIBE_Character_AttachAni` @0x404038 (the `character/%s/%s_%s.baf`
  stream load) and `UpdateSkeletonPose` @0x5cd1d8 (the per-layer advance) are
  owned by the render/factory/pose modules; this module only decides WHICH clip
  handle they get. The orchestrator wires the call into the session per the
  handoff above.
* **Idle playback-speed jitter** is *reported* (scale 0.01 = `dbl_6103FC`, base
  1.0) so the caller can seed the attached anim's `+96` exactly; the RNG
  (`VIBE_Math_RandomFloatScaled` @0x58b910) is the engine's — this module
  returns the scale/base, not a rolled value.
* **Cart gait variant** (`bewegung/karren_ziehen`) is exposed via the `cart`
  flag (the morph-init pick); the session bridge never pulls a cart, so a
  session mover always resolves to plain `bewegung/gehen`.
* **Runtime-attached action clips** (take/drop/sound/load-anim/etc., catalog
  name ""): the selector returns `None`/"" — those handlers attach their own clip
  at runtime, and the selector invents nothing.
* **Sit gate source** (`+140 & 0x10`): exposed as the `sit` field; nothing in
  the session bridge sets it yet (persons stand), so seated-NPC idle is wired
  but unfired until a host marks the sit flag.

## Tests

`tests/unit/npc_clip_select_test.cpp` — suite `NpcClipSelect`, **11 tests, 74
checks, 0 failures**:
* `GoldenClipStrings` — the five clip names + the 0.01/1.0 jitter constants are
  the exact get_bytes values.
* `ActionCatalogLookup` — types 7/45/58 clip-bearing (cart variant); all other
  types empty.
* `ActiveWalkActionSelectsGait` / `ActiveTurnActionSelectsTurnClip` /
  `ActiveRuntimeAttachActionSelectsNothing` — gate 1 (the action-head clip).
* `NoActionMovingSelectsGait` — gate 2 (the session mover → gait).
* `IdleStandWhenNotMoving` / `IdleSitWhenSitFlagSet` — gate 3 (idle stand/sit +
  jitter seed).
* `ActionOverridesMovementAndSit` — the action gate is exclusive (checked first).
* `SessionBridgeMovementToClip` — the `SelectPersonClipFromMovement` handoff
  (moving→gait, idle→stand, sit→sit, moving overrides sit).
* `RecordOffsetsAndFlags` — the +0x128/+0x8C/+0x8D offsets + 0x10/0x10/0x20
  flag bits.

Build: the `guild` library (all of `src/**`) links cleanly with the new module —
no ODR clash (grepped before defining). `npc_clip_select_test` passes via ctest.
(Concurrent wave-8 agents' edits to shared GUI files caused transient parallel
link races in unrelated `gui_*_e2e` targets; they rebuild cleanly in isolation —
not caused by this module, which only adds new files.)

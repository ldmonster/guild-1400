# 17 — Character Actions & AI

Every NPC and player avatar in *Die Gilde* is driven by a **per-actor action queue**: a
doubly-linked list of *action records*, each tagged with a one-byte **action type code**.
On every simulated frame the engine walks the live character table, pulls the *head* action
of each character's queue, and calls that action's registered **update function**. An update
function does a slice of work (advance a walk, play a sample, take an item), and when its
job is finished it **unlinks itself** from the queue — at which point the next record becomes
the new head and runs on the following frame. This is the engine's behaviour spine: pathing,
turning, item handling, gate use, universe transitions, sounds and the higher-level work-AI
all express themselves as actions pushed onto this queue.

This doc reconstructs that machine from the binary: the per-actor tick, the handler-table
registration (which enumerates *every* action type), each update function, and the queue
lifecycle (insert → dispatch → finish → advance).

Cross-links: [14 — Per-frame loop](14-per-frame-loop.md) ·
[16 — Characters](16-characters-persons.md) · [22 — Script engine](22-script-engine.md) ·
[27 — Animation](27-animation-skeleton.md) · [30 — Audio](30-audio.md).

> **Platform boundary.** Action updates *drive* but do not *implement* the swapped subsystems.
> Walk/turn/anim actions attach and prune skeletal animations (mesh = doc 26, animation =
> doc 27); sound/sample actions and footsteps emit 3D sound (doc 30). Those leaf calls cross
> the `shim/` boundary; the action machinery itself is pure game logic and is reconstructed
> 1:1.

---

## 1. The per-actor tick — `VIBE_Character_Update @0x405148`

Called once per frame from the frame loop (doc 14). It snapshots the frame clock and walks
the **character table** `dword_66F0D0` (capacity **512** slots; iterates `i = 0..511`).

```c
// gilde.exe 0x405148 — VIBE_Character_Update (__usercall eax = a1@<edi>)
dword_62D008 = dword_62EB38;          // latch "this frame" tick into the action clock
for (i = 0; i < 512; ++i) {
    v3 = dword_66F0D0[i];             // character record pointer
    if (!v3 || (*(u8*)(v3+140) & 4)) continue;   // empty slot / flagged-dead → skip
    v5 = *(u32*)(v3 + 296);           // +0x128 = HEAD of this actor's action queue
    VIBE_Character_CheckAniMorph(v3);
    if (v5)                           // queue non-empty → run the head action
        VIBE_ActionQueue_DispatchCurrent(dword_66F0D0[i]);
    else
        ... idle / auto-pose / proximity logic ...
    // then: transport-attach, low-poly mesh, idle-pose selection, universe sanity check
}
VIBE_Character_FadeOutSlots();
```

Key facts recovered:

- **`+0x128` (offset 296) is the action-queue head pointer** on the character record. The
  whole machine keys off this field. If it is non-NULL, the actor has work; the tick hands
  the record to the dispatcher. If it is NULL, the tick runs *idle* behaviour:
  - At the top it (re)builds the **collision grid** (`VIBE_Map_BuildCollisionGrid @0x404b90`)
    for the active scene `off_649D64` and its sub-universe whenever they change
    (`dword_62D0AC` / `dword_62D0B0` cache the last-stamped scene).
  - For an idle actor it may select a standing/sitting "newnoise" idle animation
    (`VIBE_Character_AttachAni` with `"stehen/stehen_newnoise"` @0x610158 or
    `"sitzend/sitz_newnoise"` @0x6103bc) and randomise its playback speed via
    `VIBE_Math_RandomFloatScaled() * dbl_6103FC + 1.0`.
  - It can **auto-queue a re-pathing** action (type **45**, walk) when a nearby crowd is
    detected: `VIBE_Character_FindNearbyInRadius(v14, …, 20.0)` →
    `VIBE_CharAction_InsertActionVararg(<actor> | (45<<32), tileX, tileY, …)`.
- After dispatch it updates transport attachment (`+0x124`), the low-poly LOD mesh
  (`+0x1EC`), and validates `c->universe` (`+0x88` must equal mesh's `+0x208`), logging
  `"ch_Update(): invalid c->universe!"` @0x6103d4 if not.
- The mesh/anim and sound this tick touches are platform-boundary leaves (docs 26/27/30).

---

## 2. The action-record layout

Action records are fixed-size slots carved from a pre-allocated pool
(`dword_62CEFC`, **0x7E400 bytes**, allocated in §3). Offsets recovered from the update and
queue code:

| Offset | Field | Meaning |
|---|---|---|
| `+0x00` | `update_fn` | function pointer to this action's update (copied from the handler table) |
| `+0x04` | `finish_fn`? | secondary "ready" callback used by the dispatcher's tail |
| `+0x06` (byte3) | **type code** | the action-type byte; read as `*(int*)(rec+6) >> 24` and `BYTE4(packed)` |
| `+0x08` | priority/order byte | compared in the dispatcher (`rec[8] > head->prio`) |
| `+0x09` | type byte (copy) | `*(u8*)(rec+9)` — the action type, also written here |
| `+0x0C` | **run counter** | `0` on first tick, incremented every dispatch; `-1` = "finished, free me" |
| `+0x10` | flags byte | per-action latch (`&1` = looped sample started, etc.) |
| `+0x14` | `owner` | back-pointer to the character record (`rec+20`) |
| `+0x18` | (visible flag for type 55) | |
| `+0x24` | prev link | doubly-linked-list predecessor |
| `+0x28` | next link | doubly-linked-list successor |
| `+0x2C..` | `Data[0..]` | varargs payload (`Data[0]` at `+0x2C`, `Data[1]` at `+0x30`, …) |
| `+0x30` (`+0x34`,`+0x38`) | type-specific data | e.g. universe id / target ids for type 51 |
| `+0xF0` | embedded string | animation / object / dummy name (e.g. `"Walk2RndDummy"`, `"dummy_EINGANG"`) |

The packed 64-bit arg passed to the insert functions is `(charptr) | (typecode << 32)` — the
low dword is the owning character, byte 4 is the action type. `VIBE_CharAction_InsertActionVararg`
copies `dword_66FD18[19*type]` payload dwords from the varargs into `Data[]`.

---

## 3. Handler registration — `VIBE_CharAction_RegisterHandlers @0x40be30`

Run once at startup. It allocates the action-record pool, allocates the character/queue
tables, then **declares every action type** by calling
`VIBE_Character_DeclareAction(type, update_fn, motionName, enabled, dataCount)` @0x405558.
`DeclareAction` writes into three parallel tables indexed by `19 * type`:

- `dword_66FCD0[19*type]` = the **update function pointer**
- `byte_66FCD4[19*type*4]` = the *enabled* flag
- `dword_66FD18[19*type]` = the **`Data[]` element count** for that type
- the trailing bytes hold the **motion-animation name** string (e.g. `"bewegung/gehen"`).

The complete enumerated table (the authoritative list of action types in the binary):

| Code | Update handler @addr | Motion name | DataN | Behaviour |
|---:|---|---|---:|---|
| **0** | `VIBE_Character_RunActionOrFree @0x406a00` | (none) | 0 | Generic "play attached motion, free when done" — steps the motion queue; on `-1` unlinks. |
| **7** | `VIBE_Character_TurnStepActionUpdate @0x406a18` | (none) | 2 | Single discrete in-place turn step (uses `dreh_90_links/rechts` anims). |
| **23** | `VIBE_Character_SoundActionUpdate @0x4055d4` | (none) | 0 | Play a movement/voice animation+sound; hold actor "busy" (`+0x8C |= 2`) until done. |
| **45** | `VIBE_CharAction_WalkUpdate @0x40a0b8` | `bewegung/gehen` | 3 | **Walk along a path** to (tileX, tileY, universe). The main locomotion action. |
| **46** | `VIBE_Character_SoundActionUpdate @0x4055d4` | (none) | 1 | Same handler as 23, one data arg (speed). |
| **47** | `VIBE_Character_PlaySampleActionUpdate @0x405740` | (none) | 0 | Play a one-shot sample animation; switches active universe slot to emit it; flags `+0x8C |= 0x10`. |
| **48** | `VIBE_Character_SampleLoopActionUpdate @0x4058f0` | (none) | 0 | Looping sample/animation; latches `rec+0x10 |=1` after first start, loops until motion ends. |
| **49** | `VIBE_Character_TakeObjectActionUpdate @0x405c88` | (none) | 0 | Pick up an object: optional pickup anim, then `AttachItemToBone` at frame `Data[0]`. |
| **50** | `VIBE_Character_DropObjectActionUpdate @0x405f28` | (none) | 0 | Put down the held object (anim then detach at frame `Data[0]`). |
| **51** | `VIBE_Character_Move2UniverseActionUpdate @0x4063c8` | (none) | 3 | **Teleport actor between universes** (scene↔building interior). `Data[0]`=universe, `Data[1]`=ids, `Data[2]`=slot. |
| **52** | `VIBE_Character_UseGateActionUpdate @0x4059f4` | (none) | 5 | Use a door/gate: locate the `dummy_*` gate object, then *enqueue* a walk + move2universe + visibility sequence. |
| **53** | `VIBE_Character_TurnToTargetActionUpdate @0x40618c` | (none) | 1 | Compute signed angle to a target point, then enqueue a **type-7** turn-step for that angle and unlink. |
| **54** | `VIBE_Character_LoadAnimActionUpdate @0x406250` | (none) | 0 | Attach a named animation, mark it busy (`+0x8C |= 2`), unlink when it ends. |
| **55** | `VIBE_ActionQueue_FinishSetVisible @0x40b974` | (none) | 1 | Set actor visibility (`Data[0]`) once `run-counter == 0`, then unlink. |
| **56** | `VIBE_CharAction_RotateInterpolate @0x40b998` | (none) | 1 | Fade/dissolve the actor in or out over time (transparency ramp); used on universe entry/exit. |
| **57** | `VIBE_CharAction_QueueWalk2RndDummy @0x40b760` | (none) | 1 | Find a random `Walk2RndDummy` wait-spot and enqueue a walk (type 45) to it. |
| **58** | `VIBE_Command_Dispatcher @0x40a4d4` | `bewegung/gehen` | 3 | Large high-level command/order dispatcher (the AI "do this job" entry — see doc 22). |
| **59** | `VIBE_ActionQueue_CheckDurationExpiry @0x40bdd8` | (none) | 1 | Timed wait: unlink once `Data[0] + startTick < now` (a duration timer). |

Notes:
- `DeclareAction` rejects `type > 63` and refuses to overwrite an already-declared slot
  (`"ch_DeclareAction:Function already declared!"` @0x610418), so **64** is the hard ceiling
  on action-type codes; the binary uses the 18 listed above plus the implicit "free" type 0.
- Codes **45 and 58** share the `"bewegung/gehen"` walk motion; **51** (move2universe) has
  the dedicated validation in the insert path (§5).

---

## 4. Dispatch — `VIBE_ActionQueue_DispatchCurrent @0x404768`

This is the call `VIBE_Character_Update` makes for any actor with a non-empty queue.

```c
// gilde.exe 0x404768
v1 = *(fn***)(a1 + 296);          // head action record
if (!v1) return 0;                 // empty
if (!v1[5] || !*v1) return 1;      // no update fn → nothing to do
(*v1)(a1);                          // ← CALL THE HEAD ACTION'S UPDATE FUNCTION
++*(u32*)(head + 12);              // bump the run-counter (so next tick sees rec+0xC != 0)
// optional: if a secondary finish_fn is armed and the attached motion's frame has
// advanced past head->prio, fire finish_fn too and remember the frame in rec+0x85.
return 1;
```

So the dispatcher runs **exactly the head action**, then increments its run-counter. Every
update function distinguishes its **first tick** (`rec+0xC == 0`, do setup: attach the
animation, compute the path) from **subsequent ticks** (`rec+0xC != 0`, advance) using that
counter. The action completes by calling `VIBE_ActionQueue_UnlinkEntry` on itself.

---

## 5. The queue lifecycle — insert / link / advance / finish

### 5.1 Inserting an action

Two public insert paths, both keyed by the packed `char | (type<<32)` value:

- **`VIBE_CharAction_InsertActionVararg @0x40c1e4`** (variadic) — used by callers that push a
  brand-new action with a list of `Data[]` args. It calls
  `VIBE_CharAction_QueueInsertEntry @0x40c15c` (which **appends** to the *tail* of the
  actor's list, walking `+0x28` next-links), copies the update-fn pointer from
  `dword_66FCD0[19*type]`, stamps the type byte at `+0x09`, copies `dword_66FD18[19*type]`
  varargs into `Data[]`, and runs special-case validation for type **51** (move2universe:
  `Data[0]==0` must imply `Data[1]==-1` and vice-versa, else `__debugbreak`/unlink).
- **`VIBE_ActionQueue_InsertAction @0x404470`** (cdecl, `(owner, packed, data)`) — used by
  *action handlers themselves* to enqueue follow-up actions (e.g. type-53 turn enqueues a
  type-7; type-52 gate enqueues walk+move2universe). It grabs a free record via
  `VIBE_ActionQueue_GetFreeEntry @0x40431c`, links it after `head->prev` (`+0x24`), sets the
  head pointer if the list was empty, and validates:
  - **type 45 (walk):** `Data[0..1]` (tile coords) must be `≤ 0x100`, `Data[2]` (universe)
    `≤ 0x40`, the target universe must not be flagged unwalkable (`byte_13ED29D`), and if the
    scene has a tile map (`+0xB0`) the tiles must be in `[0, dword_13ECF7C[246*univ]]`. On
    failure it unlinks the record and returns 0.
  - **type 51:** the same id/universe consistency check, logging
    `"ch_InsertAction(): move2universe: invalid combination…"` @0x610354.
  - On free-pool exhaustion: `"ch_InsertAction(): Could not ch_GetFreeQueueEntry()"` @0x610320.

Both paths finish with `VIBE_ActionQueue_ValidateLinks @0x40442c` and, for the generic type 0,
copy the motion id from `Data[]` into `+0x30`.

### 5.2 Advancing & finishing — `VIBE_ActionQueue_UnlinkEntry @0x404370`

The universal "this action is done" call. Every update function reaches it on completion.

```c
// gilde.exe 0x404370
if (!rec->next /*+0x28*/ && rec == owner->queueHead /*+0x128*/)
    owner->queueHead = 0;                 // it was the only/last entry
else {
    // splice out of the doubly-linked list via +0x24 (prev) / +0x28 (next)
    if (prev) prev->next = rec->next;
    if (next) next->prev = prev;
    if (!prev && owner) owner->queueHead = next;   // it was the head → next becomes head
}
VIBE_Light_SetGrayColorThunk(0, 404, rec);        // return the record to the free pool
return 1;
```

After an unlink, the *next* record (now the head at `+0x128`) is what
`VIBE_ActionQueue_DispatchCurrent` runs on the following frame — that is exactly how "one
action finishes and the next begins." There is no explicit scheduler: completion = unlink,
and the linked-list head *is* the program counter.

### 5.3 The motion-stepping helper — `VIBE_Character_StepMotionQueue @0x4041e8`

The generic actions (0, 47, 48) delegate to this. On first call it attaches the action's
motion (`VIBE_Character_AttachMotion`, storing the live anim at character `+0x70`). On
later calls it checks the attached anim's "finished" flag (`anim+0x6D & 0x20`); when set it
prunes the expired attachment (anim subsystem, doc 27) and **returns -1** to tell the caller
to unlink. `VIBE_Character_RunActionOrFree @0x406a00` is the thin wrapper that turns that
`-1` into an unlink.

---

## 6. Representative update functions

### 6.1 Walk — `VIBE_CharAction_WalkUpdate @0x40a0b8` (type 45)

The locomotion workhorse. On its course it:

1. Resolves the actor's mesh and reads its path buffer.
2. Builds the walk animation name `"bewegung/gehen_<terrain>"` (sprintf `"%s_%s"` @0x610218),
   slash-normalises it, and attaches it on first entry.
3. Calls `VIBE_Character_WalkPathActionUpdate @0x408c4c` to advance one path segment, then
   `VIBE_CharAction_MorphMovementInit @0x409200` / `VIBE_CharAction_WalkStep @0x4093b0` to
   step the actor forward, `VIBE_CharAction_WalkOnPathRotation @0x409b2c` to turn it along
   the path, and `VIBE_Character_PlayFootstepSound @0x40905c` (3D sound, doc 30) per step.
4. **Per-step playback-speed modulation by terrain.** `VIBE_Character_QueryTileAhead @0x4066bc`
   classifies the next tile; the animation's speed field (`anim+0x60`) is scaled by terrain
   constants — stairs/slope tiles (codes **6**, **11**) get a different multiplier than flat
   ground:

   | Constant | Value | Used for |
   |---|---|---|
   | `flt_6108F0` | **2.2** | flat, normal-collision walk |
   | `flt_6108F4` | **2.5** | stair/ramp (tile 6/11), normal collision |
   | `flt_6108F8` | **1.7** | flat, "fast" collision flag (`mesh+4 & 8`) |
   | `flt_6108FC` | **1.9** | stair/ramp, fast-collision |
   | `flt_6108EC` | **0.01** | per-frame ramp-up of the speed factor (`+0x1A4`, capped at 1.0) |
   | `0.69999999` | **0.7** | base path-speed multiplier `v19` |

5. When the path is exhausted (no more segments, no live anim) it sets the "collision dirty"
   flag (`byte_62D011` → `mesh+0x8C |= 8`), frees the path buffer (`+0xF4`), and unlinks.

`VIBE_Map_CheckPathWalkable @0x408740` is the path-validity probe: it scans the path's tile
list (`mesh+36` collision array, stride `mesh+32`) over the window
`[max(1,idx), min(idx+2, len-2)]` and **returns 0 (blocked) if any tile's collision code is
0 or 13**. Tile code **13** is the universal "impassable/wall" marker (it also blocks the
auto-pose proximity and the heightmap probes elsewhere).

### 6.2 Turn-to-target — `VIBE_Character_TurnToTargetActionUpdate @0x40618c` (type 53)

Pure planner: transforms the target point through the bone chain
(`VIBE_Transform_PointThroughBoneChain @0x5c8b38`), computes the signed yaw delta
(`VIBE_Math_AngleToTargetSigned @0x5b6d1c`), converts radians→engine-units via
`angle * flt_610560 (180.0) * flt_610564 (1/π = 0.31831)`, **enqueues a type-7 turn-step**
carrying that angle, and immediately unlinks itself. (Types 53 and 7 illustrate the common
"planner action enqueues an executor action then frees itself" idiom.)

### 6.3 Turn-step — `VIBE_Character_TurnStepActionUpdate @0x406a18` (type 7) and
`VIBE_Character_TurnByAngleAction @0x408a10`

Executes a discrete rotation. First tick: pick `"bewegung/dreh_90_rechts"` (@0x61070c) or
`"bewegung/dreh_90_links"` (@0x610724) by the sign of the remaining angle (thresholds
`dbl_6107C4 = 0.2`, `dbl_6107CC = -0.2`), attach it, and build a `VIBE_Anim_CreateObjectAnim`
keyframe that drives the actual yaw from the anim's last-frame bone translation. Later ticks
wait for that object-anim's done flag (`+0x2D & 0x20`), then `SetWorldTranslation`, free the
anim, and unlink. Turn-rate scale comes from `flt_61073C` / `flt_61074C` / `dbl_610744`.

### 6.4 Take / Drop object — `…TakeObjectActionUpdate @0x405c88` / `…DropObjectActionUpdate @0x405f28`
(types 49 / 50)

Both: on first tick optionally attach a pickup/putdown movement anim
(`VIBE_Character_AttachMovementAni @0x4034c4`); each subsequent tick watches the live anim's
current frame and fires `VIBE_Character_AttachItemToBone @0x4068c0` (attach for take, NULL
for drop) when the frame reaches the scripted `Data[0]` frame number (sentinel **99999999**
= "already done"); when the anim finishes (`VIBE_Character_CheckQueueReady @0x403474`) it
runs `CheckAniMorph` and unlinks. Diagnostic strings confirm the semantics
(`"ch_TakingObject(): object not taken yet…"`, `"ch_DroppingObject(): object not dropped yet…"`).

### 6.5 Move-to-universe — `VIBE_Character_Move2UniverseActionUpdate @0x4063c8` (type 51)

Teleports the actor between scenes/interiors. Validates the `Data[0]` universe (`<0` →
`"ch_Move2Universe(): Invalid universe (<0)!"` @0x610568) and the id/universe pairing, calls
`VIBE_Character_MoveToUniverse @0x402d3c` (failure → `"…Could not change universe…"`),
switches the active universe slot (`VIBE_Universe_SwitchActiveSlot @0x5b4a24`), re-selects the
texture set (mesh, doc 26), stops the current sample (doc 30), re-positions the actor at the
target `dummy_EINGANG` (@0x6106b8) gate object, rewrites the actor's universe/position fields,
and toggles visibility. Always unlinks at the end (single-shot).

### 6.6 Use-gate — `VIBE_Character_UseGateActionUpdate @0x4059f4` (type 52)

The doors. It finds the named gate object, computes its tile via the heightmap, and then
**composes a follow-up sequence on the queue**: a walk (type 45) to the gate, an optional
visibility toggle (type 56 enqueued as `0x38…`), a move-to-universe-and-walk back out, then
unlinks the original. A clean example of one action expanding into several.

### 6.7 Rotate / fade — `VIBE_CharAction_RotateInterpolate @0x40b998` (type 56)

A timed transparency dissolve used on universe entry/exit. First tick latches the start frame
(`+0x50 = now-1`) and the fade flag (`mesh+0x8D |= 0x40`). Each tick computes
`t = (now - start) * dbl_6109E4 (0.02 = 1/50 frames)`, clamps to `[0,1]`, optionally inverts
when fading out (`Data[0]==0`), and writes transparency `t * dbl_6109F4 (255.0)` via
`VIBE_Object_ChangeTransparency @0x5b2710` onto the body, head and low-poly meshes. While a
nearby actor is detected (`FindNearbyInRadius … 30.0`) and `t < dbl_6109EC (0.1)` it **restarts**
the fade (re-enqueues itself as `0x3B…`/type 59 for 75 ticks). Completes when `t` hits 0 or
1, sets final visibility, and unlinks.

### 6.8 Sound / sample — `VIBE_Character_SoundActionUpdate @0x4055d4` (types 23/46),
`…PlaySampleActionUpdate @0x405740` (47), `…SampleLoopActionUpdate @0x4058f0` (48)

These attach a movement/voice/sample animation and hold the actor "busy" (`mesh+0x8C |= 2`)
while it plays; sample actions switch the active universe slot so the 3D sound emits from the
right place (doc 30), set the anim flags (`anim+0x6C=1`, `anim+0x6D` bits for one-shot/loop),
and unlink when the motion ends (`StepMotionQueue` returns -1). The loop variant latches
`rec+0x10 |= 1` so it only re-arms while the loop flag is set.

### 6.9 Finish-set-visible / duration — `VIBE_ActionQueue_FinishSetVisible @0x40b974` (55),
`VIBE_ActionQueue_CheckDurationExpiry @0x40bdd8` (59)

Both are trivial terminal actions: type 55 sets the actor visible/hidden (`Data[0]`) on its
first tick and unlinks; type 59 is a timer — it unlinks once `Data[0](+0x30) + startTick(+0x34)`
falls behind `dword_62EB38` (the global tick), giving a "wait N ticks" primitive.

### 6.10 Wait-slot callback — `VIBE_Character_WaitSlotCallback @0x4062c0`

A helper (not a queued type) that scans the `dummy_WAIT` (@0x401020) markers in a scene
(stride 64, up to 960 bytes = 15 slots) and records up to 32 free wait positions into the
actor's wait list at `+0x80`. Used by the work-AI to find idle standing spots, feeding the
type-57 `Walk2RndDummy` action.

---

## 7. The work-AI driver — `VIBE_Character_UpdateWorkScripts @0x4b9a5c`

This is the **higher-level AI** that decides *what* a character should be doing and turns that
into queued actions + scripts. It is throttled (it kicks every ~500 ticks via
`dword_63167C + 500 < now`) and iterates the **building/workplace table** `dword_11BB6A0`
(32 slots). For each occupied workplace whose scene matches the active one, it:

- Reads the workplace's **job kind byte** at `+0x180` (values 3/4/8/9/21/22/108 etc.) and the
  game season/time-of-day (`VIBE_GameTime_GetSeasonFromDay @0x58339c`,
  daytime windows `flt_6476FC`/`flt_64770C`).
- Maps the job kind to a **script category string**: `"Abbau"` (mining/3), `"Produktion"`
  (production/4), `"Suchen"` (searching/8,9), `"Trainieren"`/`"Training_Allgemein"` (22),
  `"Heilen"` (108), plus role strings `"Meister"`, `"Pause"`, `"bewohner"`, `"besucher"`,
  `"Getier"`.
- Builds an `.esc` script path
  (`"x:\engine\gfx\scripts\locations\<scene>\<category><N>_<scene>.esc"` @0x61e0fc, falling
  back to the `…0_…` variant), resolves it via the VFS (`VIBE_Vfs_ResolvePath @0x44fa5c`),
  and if found **loads and runs that location script** (`VIBE_Script_LoadScript @0x4421f0`,
  `VIBE_Script_RunWithArgs @0x443a90`) — see doc 22.
- Running scripts are what call `…InsertAction…` to push concrete walk/turn/take/use-gate
  actions onto the per-actor queue, closing the loop: **work-AI → script → action queue →
  per-frame update → animation/sound**.

`VIBE_Script_Finish @0x443f38` / `VIBE_Script_FindByHandle @0x442174` tie a workplace's
active script handle (`workplace+0x28`) to the running script instance, so a workplace runs at
most one job script at a time.

---

## 8. Supporting coordinate / collision leaves

- **`VIBE_Coord_ProjectFramePoint @0x407488`** — `(tileX, tileY) → world → screen`: calls
  `VIBE_Heightmap_TileToWorld @0x5c65d4` then `VIBE_Coord_ProjectPoint @0x407428`; returns 0
  if the tile has no terrain. Used to place/clip actors and path markers.
- **`VIBE_Map_StampEntityCollision @0x404ef8`** — converts an actor's world position to a tile
  via `VIBE_Heightmap_WorldToTileWithHeight @0x5c6644`, then `VIBE_Map_StampCollisionArea
  @0x404d10` writes the actor's footprint into the scene collision grid so other actors path
  around it. Returns the stamped tile or -1.
- **`VIBE_Heightmap_WorldToTileWithHeight @0x5c6644`** ("d3sm_3D2SuperMap" in the original
  strings) is the recurring world→tile projection used by walk, gate, take/drop and the
  auto-repath logic.

---

## 9. Summary of the machine

1. **Build** actions: the work-AI (`UpdateWorkScripts`) or a script or input pushes records
   onto an actor's queue via `InsertAction*`, tagged with a type code.
2. **Run** the head: each frame `VIBE_Character_Update` calls
   `VIBE_ActionQueue_DispatchCurrent`, which invokes the head record's registered update
   function (table built by `RegisterHandlers`) and bumps its run-counter.
3. **Finish**: the update function detects completion (anim done / target reached / timer
   elapsed) and calls `VIBE_ActionQueue_UnlinkEntry`, returning the record to the pool and
   promoting the next record to head.
4. The linked-list head at character `+0x128` is, in effect, the actor's program counter; the
   action types are its instruction set; and the work-AI + scripts are its compiler.

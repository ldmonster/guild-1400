# Session characters — live PERSONS rendered in the 3D city view

Status: **landed** (2026-06-11). Live `sim::g_persons` rows resolve to their REAL
character models through the 1:1 `VIBE_Office_ResolveStaffModel` reconstruction,
load from `Resources/Objects.BIN` (`_DYNAMIC/Character/<model>.bgf`), pose through
the REAL anim chain, and rasterize in the SAME sorted draw pass as the city
objects — opt-in (`RealCityRenderer::Options::scanPersons`, default **false**, so
every pre-existing test/behaviour is untouched).

Modules: `src/play/person_render.{h,cpp}` (new), `src/play/real_city_render.{h,cpp}`
(person pass + roster), `src/play/object_mesh_render.{h,cpp}` (additive
`scanPersons`/`maxPersons` scan into the shared draw list),
`src/render/skeleton_pose_driver.cpp` (a 1:1 selector-byte correction, below).

## How the ORIGINAL places a person in the 3D city (recovered via IDA MCP)

```
VIBE_Character_SpawnAtBuildingEntrance @0x57c8f0
  person = VIBE_Person_FindRecordById(id)            // word_12CE910, 536-stride
  if (*((DWORD*)person + 97)) return;                // +388 = live char actor ptr
  pos = building dummy "dummy_EINGANG"/"dummy_TUER"  // Object_FindByHandle 0x5b7be4
        via VIBE_Transform_PointThroughBoneChain 0x5c8b38
  VIBE_Character_SpawnOfficeStaffActor @0x57c744:
     rec  = VIBE_Office_ResolveStaffModel(personRow) @0x57c1e8   // <-- model name
     char = VIBE_Character_CreateFromModel(rec->name) @0x402d10  // the factory
     person+388 = char;  sprintf(node name, "sp_%i", person->id)
     node+512 = person backref;  node+72 = kindWord | 0x3000000
     strcpy(person+496, rec->name)                   // +0x1F0 stored model name
     VIBE_Character_ApplyHeadVariant @0x57c548; ResolveHeadBone @0x57c5d4
     PreloadAniSet(char, 1, "bewegung/gehen") @0x403c34
```

So a person's 3D placement IS its character object node (position pushed by the
spawn from the building-entrance dummy); the person record itself carries the
MODEL inputs, not a world position.

### Person-record columns the model resolver reads (base word_12CE910)
| offset | column | meaning |
|---|---|---|
| +0x02 | byte_12CE912 | kind byte (17 == reaper/spy → random spion) |
| +0x09 | LOBYTE(dword_12CE919) | gender (0 male / 1 female) |
| +0x0A | word | adulthood word (vs the +0x20 float) |
| +0x20 | flt_12CE930 | adulthood threshold (`word >= float` == adult) |
| +0x164 | byte_12CEA74 | office byte (office-model table key; also the adult gate) |
| +0x165 | byte_12CEA75 | profession byte (profession-model table key) |
| +0x18C | dword_12CEA9C | texture-set dword (−1 == none) |
| +0x1F0 | byte_12CEB00 | stored model name (written back on first spawn) |
| +0x184 | dword (person+97*4) | live character pointer (`SpawnOfficeStaffActor` writes it) |

### VIBE_Office_ResolveStaffModel @0x57c1e8 — reconstructed 1:1
`play::ResolveStaffModel` over `play::PersonModelView`:
1. **child gate**: `!(office || word@+0x0A >= float@+0x20)` → child boy
   (`holzfaeller_SOLDAT` @0x63DA78) / child girl (`Magd_FRAU` @0x63DAA0).
2. **stored-name path**: `+0x1F0[0] && +0x18C != -1` → copy the stored name into
   one of the engine's 8 rotating 40-byte scratch records (`dword_641FE8` cursor,
   `unk_1234610` records) and patch all four texVariant bytes to
   `LOBYTE(texSet)+68`.
3. **kind 17** → `0x6405E8 + 40*RandomModulo(3)` (spion_MEDIUM / spion2_MEDIUM ×2).
4. **office** → gender table (male `0x63E338`, female `0x63EF18`), scan ≤76
   records matching the sign-extended office byte against the record code; an
   unmatched code returns the scan's stop record (the code-0 terminator:
   `bettler_KUTTE` / `minerin_FRAU`).
5. **profession** → gender table (male `0x63DAC8`, female `0x63DF00`), scan ≤27;
   unmatched → the code-0 stop record (`bettler3_MANN2` / `minerin_FRAU`); a full
   27-deep miss falls to (6).
6. **default** → the gender table BASE record (`bergmann3_MANN` /
   `handwerkerin_FRAU`).

All seven tables recovered **byte-exact** with `get_bytes` (40-byte records:
i32 code + char name[32] + u8 texVariants[4]) and transcribed into
`person_render.cpp`. Record layout asserted by `static_assert(sizeof == 40)`.

### Asset-name mapping (verified against the shipped archives)
* mesh: `Mesh_LoadOrFindByName @0x5d345c` resolves `"*<name>.bgf"`; in
  `Resources/Objects.BIN` every character mesh is
  `_DYNAMIC/Character/<model>.bgf` (case-insensitive VFS match — e.g. table name
  `bettler3_MANN2` vs member `Bettler3_MANN2.bgf`).
* anim: `VIBE_Character_PreloadAniSet @0x403c34` formats
  `"character/%s/%s_%s.baf"` with the factory name-decomposition base
  (`sim::DecomposeModelName @0x402a4c`: `dieb_MANN2` → `MANN2`); shipped members
  e.g. `Character/MANN2/STEHEN/stehen_newnoise_MANN2.baf`,
  `Character/MANN2/Bewegung/gehen_MANN2.baf` in `Resources/animations.BIN`.

## The render pass (RealCityRenderer)

`Options::scanPersons` (default false) + `maxPersons`, person grid options, and
`personAnimStep` (pose ticks folded per render). When on, `Render()`:
1. `PreparePersons()` scans live rows (`marker != -1 && kind < 10` — the record
   gate of `VIBE_Person_IsValidActiveRecord @0x4f8e60`), runs the REAL model
   resolver, finds the exact `.bgf` member case-insensitively, decodes it through
   `RealMeshSource` (the same AGF chain as objects), loads the factory-preloaded
   idle clip (`stehen/stehen_newnoise`, gait fallback `bewegung/gehen`) and binds
   the pose;
2. each person poses through the REAL chain:
   `render::LoadAnimation` (0x5e450c) → **`render::UpdateSkeletonPose` (0x5cd1d8)**
   over a one-layer `SkeletonPoseState` (forward loop, the per-tick phase folded
   via the documented host-rate seam) → `render::SamplePosedMeshSeg`
   (`ComputeMorphWeights @0x5c9394` + `VectorLerp @0x5ca2fc`) →
   per-clip `render::CalculateClipNormals` (**0x5d0020**) + per-frame
   `render::RelightPosedFrame` (0x5d0020→0x5c9054 glue, gated by the +77 lit
   byte exactly as the engine's walk);
3. `ObjectMeshRenderer` (additive `scanPersons` scan) places persons through the
   SAME node→world-seat→project pipeline and the SAME radix-sorted
   `RasterizeMeshList` flush as the objects (one draw list per frame, as the
   engine's). The textured path works for persons too (the person's `.bgf`
   member keys the `MaterialTextureTable`).

`Result` gains `livePersons / personMeshes / personPosed / personRestPose /
personQuads / animsMounted`.

### Roster + pick
`RealCityRenderer::personRoster()` → one `PersonRosterEntry` per rendered person:
`id`, `slot`, resolved `model` + `member`, world seat, screen-projected centre
(the same `MakeCityViewCamera` projection `Pick` uses), a pick `radius` from the
drawn geometry's projected extent, and `posed`. `PickPerson(opt, sx, sy)` hit-tests
the roster (per-person radius + padding) so the session's pick/selection can
include persons without touching `sdl_session`.

## 1:1 correction to the pose driver (disasm evidence)

`skeleton_pose_driver.cpp` selected the inner advance-ladder leg (and the two
loop-continuation guards) on the **+110 flags** byte; the disasm shows all three
read **+109 mode**: `0x5cd490 / 0x5cda44 / 0x5cda70: test byte ptr [esi+6Dh], 2`
(+0x6D == +109). With the old byte, the FORWARD leg was unreachable for any
serviced track (the service gate at 0x5cd30d requires `flags & 2`), so a forward
looping clip could never advance. Corrected to `mode & 2`; all 10 existing
`SkeletonPoseDriver` vectors still pass, and the person idle clips now advance
frame-by-frame through the real ladder (verified live: pose steps change the
rendered pixels). `progress/anim-skeleton-pose-driver.md` carries the corrected
KEY DISASM FINDING.

## Named gaps (rule 8 — substituted or deferred, never faked)

* **World position**: the live spawn position is the building-entrance dummy node
  (`dummy_EINGANG`/`dummy_TUER`) in the live universe scene; the portable reimpl
  has no live universe scene, so persons seat on a deterministic grid — the SAME
  documented substitution the object pass uses for the missing +460/+76 state.
* **Animation selection**: which clip a live person plays is the
  NpcAction/charaction runtime; this pass plays the factory-preloaded idle
  (`stehen/stehen_newnoise`) with the gait (`bewegung/gehen`) as fallback —
  both genuinely the `CreateMesh @0x4029c4` preload set, nothing invented.
* **`VIBE_Character_ApplyHeadVariant @0x57c548` / `ResolveHeadBone @0x57c5d4`**
  (per-person head morph/texture variant) — not reconstructed.
* **`VIBE_Object_SelectTextureSet @0x5b3f54`** (the 4 texVariant bytes → surface
  swap) — not reconstructed; the resolved record carries the variant bytes for it.
* **Pose-rate source** (track +96 × universe time): the documented pose-driver
  host seam; `personAnimStep` pre-loads the fractional phase.
* A clip whose morph vertex count differs from its mesh draws at the static rest
  pose (`Result::personRestPose` counts them; none among the shipped idle/gait
  clips exercised so far).
* The shipped `.cty` SEED carries only the city/scene 536-row; live persons are
  created at new-game through `VIBE_Person_CreateAndSpawn @0x58da70`
  (`sim::Person_CreateAndSpawn`, the modeled factory) — the e2e populates persons
  through that same path.

## Tests

* `tests/unit/person_model_resolve_test.cpp` — suite `PersonModelResolve`,
  **8 tests, 58 checks, 0 failures**: child gate (boy/girl + office-makes-adult),
  profession match (m/f), unmatched-profession terminator fallback (m/f), office
  match + office-beats-profession + office terminator, gender defaults (table
  bases), kind-17 random spion (injected rand), stored-name scratch rotation +
  texVariant `+68` patch + `texSet==-1` bypass, and the asset-name mapping
  (mesh member, PreloadAniSet sprintf, case-insensitive member lookup).
* `tests/e2e/session_persons_render_e2e_test.cpp` — suite
  `SessionPersonsRenderE2E`, **2 tests, 67 checks, 0 failures** (guarded on the
  real game dir, honors `GUILD_GAME_DIR`):
  - `RenderLivePersonsInAugsburg`: mount + `LoadWorld` AUGSBURG, create live
    persons through the real factory, render `scanPersons=false` (baseline) vs
    `true`: 6/6 persons resolved to real character meshes (e.g. `dieb_MANN2` →
    `_DYNAMIC/CHARACTER/DIEB_MANN2.BGF`), **all 6 posed** through the driver
    chain, rasterTris 1792 vs 240 baseline, pixel delta > 0, roster of 6 with
    on-screen projections, `PickPerson` at a roster centre returns that person,
    determinism re-render.
  - `PoseDriverAdvancesAnimation`: persons-only zoomed frame at
    `personAnimStep` 0 vs 64 — the REAL `UpdateSkeletonPose` ladder moves the
    cursor and the posed pixels differ (delta 71).
* Existing suites: all 10 `SkeletonPoseDriver` vectors green after the selector
  correction; full build-tree ctest **1387/1388** (the one failure is the
  concurrent `session_save_e2e_test` slice, unrelated — no reference to these
  modules).

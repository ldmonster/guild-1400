# Wave-8 — W8-INTEGRATE: the wave-8 world-entity reconstructions wired into the live frame + session

Owner: W8-INTEGRATE. Mission: take the 10 wave-8 world-entity reconstructions (each
left a precise bind-site handoff in its `progress/<slice>-wave8.md`) and wire them into
the live `play::CityView3D` universe frame + the SDL session, ADDITIVELY. Every new path
rides an additive `Options` / trace flag, DEFAULT OFF so every pinned frame stays
BYTE-IDENTICAL; the SESSION (`sdl_session` use3d preset) opts in. The wave-8 module
BODIES were NOT edited — only their exposed entries are CALLED (rule 13).

Owned files edited: `src/play/city_view3d.{h,cpp}`, `src/play/sdl_session.{h,cpp}`,
`apps/guild_run.cpp`. New test: `tests/e2e/frame_integration_wave8_e2e_test.cpp`
(7 tests / 28 checks). This doc.
Read-only / not edited: every wave-8 render/sim module body, `universe_render.cpp`,
`scene_view.cpp`, `session_persons3d.{h,cpp}` (the persons pass is driven through its
existing public entries).

---

## Headline result: objects are now SUN-LIT (the big visible win)

Before wave-8 every city object got a single FLAT ambient shade byte (the wave-6/7
"the simplified instance pipeline carries no per-vertex normals" named gap). Wave-8
closes that gap end to end:

* **W8-NORMALS** generates the STATIC per-vertex OBJECT-SPACE normals
  (`render::GenerateVertexNormals` @0x5D1A6C) once per `.bgf`, cached per member.
* **W8-SCENELIGHTS** collects the scene's lights into a `render::SceneLightSet` and
  culls them per object (`render::CullForObject` @0x5c8218).
* **wave-7** `render::LightMeshVertices` computes the per-vertex sun NdotL + scene
  point-light diffuse over the day/night ambient seed.

Verified on the REAL AUGSBURG city (untextured so the per-vertex `lightIdx` shade
drives the pixels): the flat-ambient frame has **2** distinct colours; the per-vertex
lit frame has **36** — a genuine shade gradient across every mesh, differing by 2565
channels. `sunLitVerts=30868`, `sceneLights=205` (204 parsed scene lights + the live
day-cycle sun). In the textured session frame the pixel count is unchanged (3561)
because the affine textured span ignores `lightIdx` for textured polys — exactly the
engine's structure; the per-vertex shade is nonetheless computed and carried.

---

## The 10 handoffs: status + call site + gate

| # | Handoff | Status | Call site / gate |
|---|---------|--------|------------------|
| 1 | **MESH NORMALS** (W8-NORMALS) | **WIRED** | `CityView3D::meshNormalsFor` (lazy per-member `GenerateVertexNormals`), fed to LightMeshVertices in `doSceneWalk`. Gate `Options::sceneLights`. |
| 2 | **SCENE LIGHTS** (W8-SCENELIGHTS) | **WIRED** | `CityView3D::computeSceneLights` builds `SceneLightSet`; `CullForObject` per object in `doSceneWalk` -> `LightMeshVertices`. Gate `Options::sceneLights`. |
| 3 | **PARTICLE EMITTERS** (W8-EMITTER) | **WIRED** | `doParticles` drives `render::LiveSystems().WalkAndRender` (the 0x5b3a86 walk). Gate `Options::particles`. (Per-slot billboard splat gated on the deferred integrators — see Gaps.) |
| 4 | **CHIMNEY SMOKE** (W8-SMOKE) | **WIRED** | `CityView3D::SpawnCityChimneySmoke` scans the scene for `dummy_RAUCH` nodes -> `render::SpawnEmitterAtPosition`; session calls it at city load. Gate `Options::particles`. |
| 5 | **NPC GAIT** (W8-NPCCLIP) | **WIRED** | session per-frame: `sim::SelectPersonClipFromMovement(GetEntityMovePos(id).active)` -> `CityView3D::SetBoundPersonClip(id, clip)` (new). |
| 6 | **ANIMALS** (W8-ANIMALS) | **LIFECYCLE WIRED** | session: `sim::Animal_AllocPool()`+`Animal_LoadModels()` at load; `Animal_Update(season)` per frame; `Animal_FreePool()`+`ResetModelHandles()` at exit. Render-routing = documented boundary (see Gaps). |
| 7 | **FLAGS** (W8-CLOTH) | DOCUMENTED GAP | `RefreshFlagAnimation` needs the per-person universe-node CHILD list the simplified CityView3D pipeline does not carry (see Gaps). |
| 8 | **VEGETATION RELIGHT** (W8-VEG) | DOCUMENTED GAP | the type-4 pose-driver veg gate; CityView3D draws static scenery WITHOUT the per-object pose driver (see Gaps). |
| 9 | **MAP VIEW** (W8-MAP) | **WIRED** | session: 'M' toggles `play::MapView_RenderOverview` (markers from `boundObjects()` world x/z + the player) into the framebuffer. |
| 10 | **REFLECTIVE** (W8-REFLECT) | DOCUMENTED GAP | AUGSBURG's reflector is water (separate floor path); the texture-bit path needs the per-material high-shift+palette flags the city bind does not track (see Gaps). |

---

## Wired in detail

### 1+2 — OBJECT LIGHTING — `Options::sceneLights`

* **Normals (once per .bgf):** `meshNormalsFor(member, geom)` builds a
  `SourceMeshVertex[]` from the decoded mesh positions + the poly index triples
  (poly v0/v1/v2 pointers), runs `render::GenerateVertexNormals` (the engine's
  unweighted face-normal average @0x5D1A6C), and caches the flattened 3-floats-per-
  vertex array keyed by member. Reused by every instance of that mesh.
* **Lights (once per frame):** `computeSceneLights()` (called from `doSceneWalk`)
  pushes a `render::SceneLightNode` per parsed `SceneObjectInst::hasLight` (type +533,
  composed world pos +472, colour +92, dir +132, range +144, intensity +148,
  rangeParam +152), then appends the live day-cycle SUN as the directional (type-7)
  light (dir = -sunDir, warm-white colour scaled to 0..255, intensity from the 0..600
  brightness ramp) — the shipped city `.ed3` carries no type-7 node, so the day/night
  sun is what lights the city.
* **Per object (in `doSceneWalk`, before the model->view transform):** for each drawn
  instance, build `MeshLightVertex[]` with WORLD position (l2w*objpos + worldpos) +
  WORLD normal (object normal rotated by l2w, renormalized), cull the lights
  (`CullForObject` -> `CollectedObjectLights`), call the WORLD-space
  `LightMeshVertices` (sun NdotL + point diffuse + the 1024-entry falloff LUT
  `BuildFalloffLUT`), and write each vertex's `lightIdx` = the luma of the finalized
  shade. Falls back to the flat ambient byte when off / no normals (byte-identical).
* **Result fields:** `sunLitVerts`, `sceneLightCount`. Session trace
  `view3dSunLitVerts` / `view3dSceneLights`.

### 3+4 — PARTICLES + CHIMNEY SMOKE — `Options::particles`

* **Smoke spawn (city load):** `SpawnCityChimneySmoke()` scans the parsed scene for
  `dummy_RAUCH`-named nodes and runs `render::SpawnEmitterAtPosition(kind=points, ...)`
  at each dummy's composed world position — the `effekte\Schornstein_dunkel.esc`
  CreateEmitter the smoke script body invokes (building-fx-wave8.md default). Each
  system links into `render::LiveSystems()`. Session calls it after the city loads
  (`view3dSmokeSystems`). AUGSBURG's CITY scene ships its smoke dummies inside the
  `gb_` building model subtrees (not the city scene), so the scene-node scan finds
  0 — the e2e spawns one explicitly to exercise the walk.
* **Render walk (per frame):** `doParticles` drives
  `render::LiveSystems().WalkAndRender` (the exact 0x5b3a86 head..sentinel traversal),
  counting the visited systems (`particleSystems`). The per-slot billboard splat
  (`render_system_to_surface`) consumes slot data the per-type INTEGRATORS produce,
  and those (UpdatePoints/Polys/Lens, x87 asm) remain the deferred wave-7/8 leaves —
  so the live systems carry no slots yet and nothing is splatted (rule 8: no faked
  particle geometry). The walk is wired + counted so the smoke draws the moment the
  integrators land, with no further wiring.

### 5 — NPC GAIT — session per-frame

* `CityView3D::SetBoundPersonClip(id, clipName)` (new) reloads the bound person's
  `PersonCharacterPose` over the named factory-preload `.baf`
  (`character/<base>/<clip>_<base>.baf`), shared with BindPersons via `LoadPersonClip`.
  Idempotent (no-op if the clip is already playing). `boundPersonClip(id)` reports it.
* Session per frame: for each bound person, `sim::SelectPersonClipFromMovement(
  GetEntityMovePos(id).active)` -> the gait `bewegung/gehen` (moving) or the idle
  `stehen/stehen_newnoise` (still), flipped via `SetBoundPersonClip` BEFORE the pose
  advance. Both clips are in the factory preload set (no new stream). Counted as
  `view3dGaitFlips` (genuine changes only). A walking NPC now plays the walk cycle.

### 6 — ANIMALS — session lifecycle

* City load: `sim::Animal_AllocPool()` (0x4835c0) + `sim::Animal_LoadModels()`
  (0x484468). Per frame: `sim::Animal_Update(season)` (0x48364c — the real
  round-robin spawn-decision + lifetime/despawn core), `season = day % 4`. Exit:
  `Animal_ResetModelHandles()` (0x484424) + `Animal_FreePool()` (0x4835ec). Trace
  `view3dAnimalsActive` / `view3dAnimalTicks` / `view3dAnimalCount`. Verified
  `animalTicks=80` over an 80-frame session.
* **Render-routing = documented boundary** (creature-wave8.md "the one genuine gap"):
  the live `IAnimalWorld` that routes `SpawnAnimal` -> `sim::CreateFromModel` + door/
  building placement + the character render path needs a host character-actor
  injection API CityView3D does not expose. With the inert default `IAnimalWorld` the
  Update core runs faithfully and spawns nothing (rule 8: no faked animals). The
  lifecycle + per-tick decision are wired; the visible spawn awaits the actor-inject
  API (the same person-pass machinery, generalized).

### 9 — MAP VIEW — session 'M' toggle

* 'M' (`kVkM`, edge-detected) toggles the overview. When open, the session builds
  `play::OverviewMarker`s from every `boundObjects()` building at its world (x,z) +
  the camera/player marker, and composites `play::MapView_RenderOverview` into the
  SAME framebuffer (before the HUD/present) — the engine's
  `VIBE_MapView_PanelDispatcher @0x5441d0` opened over the session. Trace
  `view3dMapOpened` / `view3dMapMarkers`.

---

## Documented rule-8 boundaries (NOT faked) — items 7, 8, 10 + the animal render arm

7. **FLAGS** (cloth-anim-wave8.md): `render::RefreshFlagAnimation` walks a person's
   universe-node CHILDREN (`FlagAnimHooks` bound to the live scene graph) to (re)attach
   the `sp_WIMPEL.baf` flag object on each `dummy_FAHNE`. The CityView3D instance
   pipeline carries no per-person universe-node child list (its persons are flat posed
   character meshes; the building instances carry no node subtree), and the `.baf` is
   waved by the already-wired `UpdateSkeletonPose` only on objects that go through the
   pose driver. The handoff itself says "No bind-site/integration file edited; the host
   supplies the node children from its live scene graph." That live scene graph is the
   boundary; the flag attach/refresh module is reconstructed + golden-tested standalone.

8. **VEGETATION RELIGHT** (vegetation-anim-wave8.md): the engine has NO wind sway; the
   only per-frame veg work is the type-4 light-cache relight gated INSIDE
   `VIBE_Anim_UpdateSkeletonPose` (the veg gate calls `BuildVegetationCache`).
   CityView3D draws its static vegetation scenery WITHOUT the per-object pose driver
   (the pose driver runs only for posed persons, which are characters, type != 4), so
   there is no live veg pose-driver invocation in this view to bind the
   `SkeletonPoseHooks::buildVegetationCache` hook to. The handoff itself states "No edit
   to city_view3d.* is required for a sway because the engine has none; the relight
   rides the existing pose-driver gate" — which only fires when an object is posed each
   frame. The relight math is reconstructed + golden-tested standalone.

10. **REFLECTIVE** (reflective-nodes-wave8.md): a surface is reflective iff its material
    has high-shift + a real palette index (+104 bit5), read by
    `PrepareReflectionNode`. CityView3D's material bind does not track those per-material
    flags, so `doMirrors`' gate `reflectionPrepared` stays false and the mirror pass is
    correctly idle — exactly the gate the handoff documents. AUGSBURG's only large
    reflector is the WATER, drawn by the separate floor/terrain path (wave-6 W6-WR), not
    the texture-record mirror path; AUGSBURG ships no reflective MESH prop, so the
    texture-bit path has nothing to fire on. The flag-origin predicate + plane formula
    are reconstructed + golden-tested standalone, ready for a scene that carries one.

These four are DATA-AVAILABILITY boundaries in the simplified CityView3D instance
pipeline, named here exactly as each handoff's own "Completeness/deferred" section
anticipates — never faked.

---

## Determinism pins — RECALIBRATED: **NONE**

Every pre-existing pinned frame stays BYTE-IDENTICAL with the new Options/trace flags
default-off: `Options::sceneLights` (the new per-vertex lighting) and
`Options::particles` (the live-list walk) gate every wave-8 render path; the chimney
smoke spawn, the NPC gait flip, the animal Update and the map overlay are session-only
(driven from `sdl_session.cpp`, never from a default `RenderFrame`). The wave-8 e2e's
`DefaultsByteIdentical` asserts `sunLitVerts==0` + `particleSystems==0` + a
byte-identical rerun with the flags off.

Full `ctest` (GUILD_GAME_DIR set) = **1462 / 1462 pass, 0 failures** (no expectation
changed; the wave-6/7 e2e pins stay green WITH the new session paths active).

---

## Tests (rule 11)

`tests/e2e/frame_integration_wave8_e2e_test.cpp` — 7 tests / 28 checks, GUARDED on
GUILD_GAME_DIR over real AUGSBURG (the selector test is asset-free), all GREEN:

* `SceneLightsPerVertexSunLight` — `Options::sceneLights` collects 205 lights, applies
  30868 per-vertex sun shades over REAL object-space normals; the lit frame differs
  from the flat-ambient frame (2565 channels); deterministic rerun.
* `PerVertexShadeGradient` — untextured flat-ambient = 2 colours, lit = 36 (a genuine
  per-vertex gradient, not a flat constant).
* `ChimneySmokeLiveSystemWalk` — `SpawnEmitterAtPosition` links a live system; the
  doParticles `WalkAndRender` visits it (`particleSystems>=1`).
* `NpcGaitClipSelectionAndFlip` — the selector (moving->gait, still->idle, golden
  clip strings) + the `SetBoundPersonClip` flip round-trip (gait<->idle, idempotent).
* `OverviewMapMarksRealBuildings` — the overview composites markers from the 53 real
  bound buildings into the framebuffer (backdrop painted).
* `DefaultsByteIdentical` — wave-8 flags off -> no lighting/particle path ran, frame
  byte-identical across reruns.

The wave-8 module slices keep their own golden suites (mesh_normals 109, scene_lights
30, particle_emitter_create 66, building_fx 51, npc_clip_select 74, cloth_anim 70,
vegetation_anim 35, mapview 34, reflective_nodes 40 checks) — all still green.

---

## Visual verify (vulkan-sdl preset)

`cmake --build build-vk --target guild_run` then
`SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy ./build-vk/guild_run --play
 --game-dir europe_guild_1400_original --frames 80`:

```
--play trace: mounted=1 loaded=1 liveObjects=55 persons=1 framesPresented=80
  daysAdvanced=0 view3d=1(796 inst, 786432 px) clock=1 fires=15 dump=1
--play wave8: sceneLights=205 sunLitVerts=30868 smokeSystems=0 gaitFlips=0
  animalTicks=80 animalsAtExit=0 mapOpened=0 mapMarkers=0
```

`sceneLights=205` + `sunLitVerts=30868` confirm objects are per-vertex sun-lit in the
live session (the headline). `animalTicks=80` confirms the per-tick animal Update
drives the real spawn/lifetime core. Artifacts `/tmp/guild_session_city3d.ppm` (1024×768)
+ `/tmp/guild_w8_city3d.png`. The session frame is textured (3561 colours, unchanged —
the affine textured span ignores `lightIdx`); the per-vertex lighting is proven visible
on the untextured e2e path (2 -> 36 colours).

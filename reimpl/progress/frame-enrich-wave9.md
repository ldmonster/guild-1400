# Wave-9 — W9-FRAME-ENRICH: the wave-8 reconstructions FIRE in the live frame

Owner: W9-FRAME-ENRICH. Mission: take the wave-8 world-entity reconstructions that
`progress/frame-integration-wave8.md` left "reconstructed + tested standalone but
INERT in the live frame" (because the simplified `play::CityView3D` instance pipeline
did not carry their per-object data) and thread that data through so they FIRE — each
behind an additive `Options` flag, DEFAULT OFF so every pinned frame stays
BYTE-IDENTICAL; the SESSION (`sdl_session` use3d preset) opts in. The wave-8 / wave-9
render/sim module BODIES were NOT edited — only their exposed entries are CALLED
(rule 13). INTEGRATION/WIRING ONLY (the IDA MCP was disconnected for this task; no
decompiling — every datum was taken from the already-reconstructed modules + handoffs).

Owned files edited: `src/play/city_view3d.{h,cpp}`, `src/play/sdl_session.{h,cpp}`,
`apps/guild_run.cpp`. New test: `tests/e2e/frame_enrich_wave9_e2e_test.cpp`
(5 tests / 25 checks). This doc. No `progress/INDEX.md` edit. No git commit.

---

## The five gaps + status

| Gap (wave-8 deferred) | Status | Where it fires |
|---|---|---|
| (7) **FLAGS** — RefreshFlagAnimation needs the per-building universe-node child list | **FIRED** | `CityView3D::RefreshObjectFlags` walks each bound building's scene-node CHILDREN (the universe-node child list) into `render::RefreshFlagAnimation` @0x4b5ef8. Gate `Options::flagAnim`. |
| (8) **VEG RELIGHT** — the type-4 pose-driver veg gate isn't reached | **FIRED** | `CityView3D::doVegRelight` runs `render::BuildVegetationCache` @0x5c8560 per frame on the vg_/pfl_ type-4 scenery. Gate `Options::vegRelight`. |
| (10) **REFLECTIVE** — the detector isn't consulted on scene meshes | **WIRED (idle on AUGSBURG, documented)** | `CityView3D::doReflectiveScan` consults `render::TextureFlagIsReflective` on the bound meshes; on a reflective surface it derives the plane (`DeriveReflectionPlane` @0x5f68c8) + primes the mirror gate. Gate `Options::reflective`. |
| **ANIMALS** render-arm — animals tick but aren't drawn | **FIRED** | `CityView3D::AddAnimalInstance` (the host character-actor injection API the wave-8 handoff named as the one genuine gap) seats a spawned animal as a character instance, drawn through the SAME person/character pass. Gate `Options::animals`. Session installs `CityAnimalWorld`. |
| **PARTICLES** WalkAndUpdate+Render | **FIRED** | `doParticles` now drives `render::LiveSystems().WalkAndUpdate(now)` (W9-PARTICLE-RUNTIME's integrator, which had LANDED by the time this wave wired it) BEFORE the existing `WalkAndRender`. Gate `Options::particles`. |

---

## Wired in detail

### FLAGS — `Options::flagAnim` + `RefreshObjectFlags`

The wave-8 handoff (cloth-anim-wave8.md) said `RefreshFlagAnimation` "walks a
person's universe-node CHILDREN" and "the host supplies the node children from its
live scene graph." That live scene graph IS the parsed `.ed3` node tree CityView3D
already loads + composes: a building node's universe-node children are the scene
nodes whose `parent` index is that node (`scene_[i].parent == buildingSceneIndex`),
already available. `RefreshObjectFlags`:

* For each bound BUILDING object, gathers its `dummy_FAHNE` child nodes into a
  `render::FlagSceneNode[]` (the universe-node child list), builds the
  `render::FlagPerson` (universeNode = the building node, heraldry + heraldryByte
  from the optional `heraldryOf` provider), and runs `render::RefreshFlagAnimation`
  (the engine's own gate: `universeNode != 0 && heraldry != 0xFFFF &&
  buildType ∈ {5,6,7}`). The attach hook (`AttachToUniverseNode` @0x5b3e30) returns
  a non-null handle so each produced flag object is counted.
* **The engine's own gate is honoured, not bypassed.** With no per-building heraldry
  table carried in this view (`heraldryOf` null), every object is treated as 0xFFFF
  and the gate shuts — so NO flag fires where the engine itself would not (rule 8).
  A host that carries the heraldry supplies the provider and the flags fire.
* **Verified on REAL AUGSBURG:** 44 `dummy_FAHNE` children across the 53 bound
  buildings. `RefreshObjectFlags()` (no provider) = 0 (gate shut, faithful);
  `RefreshObjectFlags(provider{heraldry=0, buildType=5})` = **44** (a flag object per
  child — proving the universe-node child list is threaded into RefreshFlagAnimation);
  `buildType=2` = 0, `heraldry=0xFFFF` = 0 (the gate behaves).
* **Remaining boundary (documented, not faked):** the flag's per-frame `.baf` WAVE
  rides the skeletal pose driver (`UpdateSkeletonPose` @0x5cd1d8), which in this view
  is wired only for the character/person pass — building scenery has no per-object
  pose driver here. The flag attach/refresh ARM (which was INERT) now FIRES; the
  visible wave lands the moment a building pose driver is present.
* Result `flagObjects` / `flagRefreshNodes`. Session trace `view3dFlagObjects`.

### VEG RELIGHT — `Options::vegRelight`

The wave-8 handoff (vegetation-anim-wave8.md): there is NO vegetation wind sway in
Die Gilde (7 functions read, zero time terms); the only per-frame veg work is the
type-4 light-cache relight gated INSIDE the pose driver's veg gate (0x5cebed, which
calls `BuildVegetationCache` @0x5c8560). CityView3D draws static scenery WITHOUT the
per-object pose driver, so that gate was never reached. `doVegRelight` reaches the
SAME observable result: for each drawn `vg_`/`!vg_`/`pfl_` scenery instance (the
foliage-decor name classes wave-4 `ObjectHideFoliageDecor` @0x506388 keys on), run
`render::BuildVegetationCache` over its vertices (the inert light hooks -> the
seed grayscale, exactly the engine's result for a veg mesh with no nearby dynamic
light). The genuine per-frame work is the relight itself (counted), not a sway.

* **Verified on REAL AUGSBURG:** 460 vg_/pfl_ scenery instances relit per frame
  (9676 vertices). Default off = 0 (not reached). Deterministic across frames.
* Result `vegRelitMeshes` / `vegRelitVerts`. Session trace `view3dVegRelit`.

### REFLECTIVE — `Options::reflective`

The wave-8 handoff (reflective-nodes-wave8.md): a surface is reflective iff its
texture record's +104 bit5 (0x20) is set (`render::TextureFlagIsReflective`,
0x5f67c4), which the texture loader stamps for materials with high-shift + a real
palette index. The detector + plane formula were reconstructed standalone but never
consulted on the live meshes, so `doMirrors`' `reflectionPrepared` gate was
hard-false. `doReflectiveScan` consults the detector on every bound material the view
loaded; on a reflective surface it derives the plane (`DeriveReflectionPlane`
@0x5f68c8) and primes the wave-6/7 `MirrorPassGate` so `ShouldRenderMirrorPass` flips
true and `AppendMirroredPolys` would emit the reflection.

* **AUGSBURG ships NO reflective mesh prop** (its only large reflector is the water,
  drawn by the separate floor/terrain path — wave-6 W6-WR — not the texture-record
  mirror path). The scan correctly reports **0** there and the mirror gate stays
  idle — exactly the "City scene reality" the handoff documents (rule 8: the engine's
  real behaviour, not a stub). The PATH is wired so a scene carrying a reflective
  interior prop activates the reflection with no further work.
* Result `reflectiveMeshes`. Session trace `view3dReflectiveMeshes`.

### ANIMALS — `Options::animals` + `AddAnimalInstance` + session `CityAnimalWorld`

The wave-8 handoff (creature-wave8.md "the one genuine gap"): the live `IAnimalWorld`
that routes `SpawnAnimal` -> the character render path "needs a host character-actor
injection API CityView3D does not expose." `AddAnimalInstance(model, place)` IS that
API: it resolves the species model through the SAME `RealMeshSource` persons use
(animals ARE characters, type byte 2 — no separate render path) and seats a
renderable instance the `doSceneWalk` character pass draws.

* The session installs `CityAnimalWorld` (`sdl_session.cpp`): `SpawnAnimal(kind)`
  maps the kind byte to the species model (the **real SpawnCat/SpawnDog model swap**
  is preserved — Cat=0 -> `hund_HUND`, Dog=1 -> `katze_KATZE`, Cow=3 -> `kuh_KUH`,
  Sheep=4 -> `schaf_SCHAF`, Pig=6 -> `schwein_SCHWEIN`, Horse=7 -> `pferd_PFERD`),
  picks a real bound building's world position (the host stand-in for the
  FindDoorTarget/PickSpawnBuilding door anchor), and calls `AddAnimalInstance`.
  `DestroyAnimal` -> `RemoveAnimalInstance`.
* **The spawn gate now opens.** The wave-8 `animalsAtExit=0` root cause was twofold:
  (a) the inert default `IAnimalWorld` spawned nothing, and (b) `g_gameTick` never
  advanced so the >=350-tick spawn throttle never opened AND `g_weatherState==0`
  kept the population cap at 0. The session now installs the world, advances
  `g_gameTick` per tick (the sim-step clock the throttle/lifetime read), and seeds a
  light weather state (`g_weatherState=1`, cap 16) — the engine's own gates, just
  driven. The animal AI/lifetime core is unchanged (called, not edited).
* **Verified live (guild_run --play --frames 80):** `animalsAtExit=5`,
  `animalsDrawn=4` (was 0 in wave-8). Verified e2e: a seated `hund_HUND` draws
  (`animalInstances=1` on, 0 off; removal drops the draw).
* Result `animalInstances`. Session trace `view3dAnimalsDrawn`.

### PARTICLES — `WalkAndUpdate` + `WalkAndRender`

W9-PARTICLE-RUNTIME landed `render::ParticleSystemList::WalkAndUpdate(now)` (the
per-frame integrator over each live node's raw emitter image + 84-byte slot array —
the original's +0x304 update fn run once per tick BEFORE rendering). `doParticles`
now drives `WalkAndUpdate(particleClock_)` (a monotonic ~30fps ms tick) BEFORE the
existing `WalkAndRender`, so spawned systems (chimney smoke) advance and the renderer
sees the freshly-integrated slots. Systems whose raw image was not built stay inert
(WalkAndUpdate skips `runtimeReady==false` — never faked).

---

## Determinism pins — RECALIBRATED: **NONE**

Every pre-existing pinned frame stays BYTE-IDENTICAL: the wave-9 paths gate on the
new Options flags (`flagAnim`/`vegRelight`/`reflective`/`animals`, all DEFAULT
FALSE), and the particle `WalkAndUpdate` only runs under `Options::particles`
(already default-off). `RefreshObjectFlags`, the `CityAnimalWorld` install, the
`g_gameTick`/weather seed, and the flag/animal counters are session-only (driven
from `sdl_session.cpp`, never from a default `RenderFrame`). The wave-9 e2e's
`DefaultsByteIdentical` asserts `vegRelitMeshes==0 && reflectiveMeshes==0 &&
animalInstances==0 && flagObjects==0` plus a byte-identical rerun with the flags off.

`Animal_Update`'s side effects (the pool / `g_gameTick` / `g_weatherState`) are
exercised ONLY by the session loop, not by any pinned-frame test path — the
`Animal_*` golden suites set their own state. The whole-world hash is unaffected
(the animal pool is not in HashFullWorld's fold set).

---

## Tests (rule 11)

`tests/e2e/frame_enrich_wave9_e2e_test.cpp` — 5 tests / 25 checks, GUARDED on
GUILD_GAME_DIR over real AUGSBURG, all GREEN:

* `FlagRefreshWalksUniverseNodeChildren` — no-provider gate shut (0); gate-open
  build-type 5 -> 44 flag objects (the dummy_FAHNE children); build-type 2 -> 0;
  no-heraldry -> 0 (the engine gate behaves through the threaded child list).
* `VegetationRelitPerFrame` — off=0; on -> 460 meshes / 9676 verts relit;
  deterministic.
* `ReflectiveScanConsultsSceneMeshes` — textured scan reports 0 reflective meshes +
  idle mirror gate on AUGSBURG (the documented "no reflective prop" outcome).
* `AmbientAnimalDrawsThroughCharacterPath` — `AddAnimalInstance(hund_HUND)` seats +
  draws (on=1, off=0), `RemoveAnimalInstance` drops it.
* `DefaultsByteIdentical` — every wave-9 flag off -> no path ran, byte-identical.

Full `ctest` (GUILD_GAME_DIR set) = **1463 / 1463 pass, 0 failures** (was 1462; +1
wave-9 e2e). All wave-8 e2e pins (`frame_integration_wave8_e2e_test` 28 checks),
`cloth_anim`, `vegetation_anim`, `reflective_nodes`, `particle_emitter_create`,
`city_view3d`, and the playable-flow / real-scene-driver e2es stay green with the new
session paths active.

---

## Visual verify (vulkan-sdl preset)

`cmake --build build-vk --target guild_run` then
`SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy ./build-vk/guild_run --play
 --game-dir europe_guild_1400_original --frames 80`:

```
--play trace: mounted=1 loaded=1 liveObjects=55 persons=1 framesPresented=80
  daysAdvanced=0 view3d=1(796 inst, 786432 px) clock=1 fires=15 dump=1
--play wave8: sceneLights=205 sunLitVerts=30868 smokeSystems=0 gaitFlips=0
  animalTicks=80 animalsAtExit=5 mapOpened=0 mapMarkers=0
--play wave9: flagObjects=0 vegRelit=460 reflectiveMeshes=0 animalsDrawn=4
```

The wave-8 counters HELD (`sceneLights=205`, `sunLitVerts=30868`, `animalTicks=80`),
and the wave-8 `animalsAtExit` MOVED `0 -> 5` (the spawn gate now opens). The wave-9
line shows the enrichment firing: `vegRelit=460` (per-frame veg relight, was inert),
`animalsDrawn=4` (animals drawn through the character path, was inert),
`flagObjects=0` (gate correctly shut — no heraldry table; documented),
`reflectiveMeshes=0` (AUGSBURG ships none; documented). `gaitFlips=0` is unchanged
(persons=1, not moving — pre-existing).

---

## Rule-8 boundaries (named, not faked)

* **Building-flag WAVE** rides the skeletal pose driver, wired only for the character
  pass here; the flag attach/refresh arm FIRES (was inert) and the wave lands with a
  building pose driver.
* **REFLECTIVE on AUGSBURG** is genuinely absent (no reflective mesh prop); the scan
  correctly idles — the path is wired for a scene that carries one.
* **Flag heraldry gate** stays shut with no heraldry table carried (the engine's own
  gate) — no flag fired where the engine would not.
* **Animal placement** uses a real bound building's position as the host stand-in for
  the FindDoorTarget/PickSpawnBuilding door anchor (the IAnimalSceneOps scene leaf).

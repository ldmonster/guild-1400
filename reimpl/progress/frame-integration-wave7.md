# Wave-7 — W7-INTEGRATE: the wave-7 render refinements wired into the live 3D frame

Owner: W7-INTEGRATE. Mission: take the 9 handoffs in `/tmp/guild_w7/integration_plan.md`
(the wave-7 refinements sitting in already-wired wave-6 render modules) and bind them
into the live `play::CityView3D` universe frame, ADDITIVELY, each riding its existing
wave-6 `Options` flag. DEFAULT behaviour stays BYTE-IDENTICAL (every pinned frame); the
SESSION (`sdl_session` use3d preset) already opts every flag ON, so `guild_run --play`
shows the refined scene. The wave-7 module BODIES were NOT edited — only their exposed
entries are called (rule 13).

Owned files edited: `src/play/city_view3d.{h,cpp}`, `src/play/terrain_render.{h,cpp}`.
New test: `tests/e2e/frame_integration_wave7_e2e_test.cpp` (3 tests). This doc.
Read-only / not edited: `apps/guild_run.cpp` (no new flag needed — the refinements ride
the wave-6 flags), `src/play/sdl_session.{h,cpp}` (already opts every flag ON; no change),
`src/play/universe_render.cpp`, `src/play/scene_view.cpp`, and EVERY wave-7 module body.

---

## What got wired (the data-available refinements) + what stayed a documented gap

The integration plan listed 9 handoffs. Three of them refine data the CityView3D
instance pipeline actually carries; those are WIRED with visible, gated, deterministic
effect. The other six need engine object/mesh/scene records the simplified CityView3D
pipeline does not carry faithfully — those are NAMED rule-8 boundaries (NOT faked),
exactly as each handoff's own "Completeness/deferred" section anticipates.

| # | Refinement | Status in CityView3D | Call site / gate |
|---|-----------|----------------------|------------------|
| 7 | **SKY BANDS** (W7-SKYBANDS) | **WIRED** | `loadSceneBuffer` loads `skyBands_`; `computeSunForFrame` uses `ComputeSkyFog`. Gate `Options::sky`. |
| 6 | **WATER EF_WASS** (W7-WATERTEX) | **WIRED** | `doRenderTerrain` passes `CV3D_LoadWaterTexture` to `BuildWater`; `GroundSpanTextured` samples EF_WASS for water polys. Gate `Options::water` + textures mounted. |
| 4 | **PER-PIXEL FOG** (W7-FOGPIX) | **WIRED** | `RenderFrame` configures `fogState_`; the textured spans seed `RgbzVertex::fogFactor` from view-space depth. Gate `Options::fog`. |
| 1 | SHADOWS node-driver + ground quad | DOCUMENTED GAP | needs engine ShadowNode caster tables + ground-context records (see below). Visible drop-shadow already from wave-6 `doShadows()`. |
| 2 | PARTICLE integrators | DOCUMENTED GAP | CityView3D models no live particle systems (the sim's spawn list). |
| 3 | MIRROR scene-graph | DOCUMENTED GAP | scene carries no reflective node (`child byte[104] & 0x20`). |
| 5 | BONE-MATRIX vertex lighting | DOCUMENTED GAP | the .bgf instance Vertex carries no object-space normals. |
| 8 | ENV-MAP reflection UVs | DOCUMENTED GAP | same: no per-vertex object-space normals + no reflective-material flag. |
| 9 | BILLBOARD tint | DOCUMENTED GAP | the city instances use the mesh arm, not the billboard arm (no nodeType>=5 nodes). |

---

## The three wired refinements (call site + gate + Options flag)

### 7 — SKY BANDS (W7-SKYBANDS) — `Options::sky`

* **Load (once, at scene open):** `CityView3D::loadSceneBuffer` already parses the scene
  header into a `render::SceneHeader`; the SAME parsed header now feeds
  `skyBands_ = render::LoadSkyBands(h)` (sky.h, the 0x5e7e38 light-rig adapter).
  `hasSkyBands_ = (skyBands_.band_count > 0)`. `LoadCityFromNodes` (synthetic scenes,
  no header) leaves the table zero -> the fallback path.
* **Per frame:** `CityView3D::computeSunForFrame` — when `hasSkyBands_` and the sun band
  is valid, the sky colour is the REAL `render::ComputeSkyFog(skyBands_, sun.band,
  sun.blend, fogA=0, fogB=0, frac=0)` chain (BuildFogScratch @0x5b85e4 ->
  BlendAmbientFog @0x5b8b04 -> the framebuffer clear) instead of the wave-6
  brightness-ramp fallback. The fallback is KEPT for scenes without a band table
  (byte-identical to wave-6).
* **Frame position:** unchanged — the sky colour fills the surface in `doClear` (the
  pre-terrain clear @0x5b3953), BEFORE terrain/objects.
* **Member:** `render::SkySceneTable skyBands_` + `bool hasSkyBands_` (plain data).
* **Verified live (real AUGSBURG):** noon resolves band 2 -> `0x4B8ECC` (light blue),
  dusk resolves band 5 -> `0x224E89` (darker blue) — distinct time-of-day colours from
  the loaded band table, deterministic across reruns.

### 6 — WATER EF_WASS (W7-WATERTEX) — `Options::water` (+ textures mounted)

* **(a) Load:** `CityView3D::doRenderTerrain` now passes a `render::WaterTextureLoadFn`
  to `GroundFrame::BuildWater` (when `Options::textured` + Textures.BIN mounted): the new
  static `CityView3D::CV3D_LoadWaterTexture` resolves `EF_WASS_06A_2T_W_AN0` through the
  SAME `groundTexCache_` the ground uses (the Textures.BIN BmpFetch installed by
  `SetupGroundTexCache`), returning the loaded `render::Texture*` record. `BuildWater`
  stores it into every `WaterMesh+0/+4` and `GroundFrame::waterTexture_`. Null (no
  EF_WASS member / no textures) -> the white-default branch (byte-identical to wave-6).
* **(b) Sample:** `terrain_render.cpp GroundSpanTextured` (the slot 3/4 textured span)
  now routes a WATER poly (the engine's water `+38 bit1` no-cull flag, `flags38 & 0x02`,
  set by `RenderWaterSurface @0x5be668`; `uvZ > 0`) through the loaded EF_WASS record
  carried in `GroundTexState::waterTex` instead of a ground slot. The per-frame texture
  scroll (`poly.uvX/uvY == mesh[82]/[83]`, the `AnimateWaterVertices` texAccum the
  render arm copies onto every water poly) is added to the canonical per-cell quad UVs
  so the EF_WASS ripple tiles + scrolls. `GroundFrame::Render` populates
  `g_groundTex.waterTex` from `waterTexture_` and enables the textured dispatch when
  EITHER ground textures OR a water texture is bound. Ground tiles never set
  `flags38 & 0x02`, so the water/ground routing is clean (no collision).
* **Frame position:** unchanged — water polys are appended in `GroundFrame::Render`'s
  Phase-2 water sub-pass (AFTER ground tiles, SAME draw list) and flushed through the
  SAME `RasterizeMeshList`.
* **Verified live (real AUGSBURG):** 1 water region, 400 water polys, 268 water verts
  drawn through the EF_WASS span; deterministic across reruns.

### 4 — PER-PIXEL FOG (W7-FOGPIX) — `Options::fog`

* **Configure:** `CityView3D::RenderFrame` — when `Options::fog`, in addition to the
  wave-6 `SpanFog()` enable/colour it now configures a per-frame `render::FogState
  fogState_` (`render::ConfigureFog(near = opt.nearZ, far = scene fog far else 20000,
  color = sky colour)`). `fogState_.enabled` mirrors `SpanFog().enabled`.
* **Seed per vertex:** the textured span trampolines —
  `CV3D_SpanTextured` (objects) and `GroundSpanTextured` (terrain + water) — now seed
  each `RgbzVertex::fogFactor` from the vertex's view-space depth via
  `render::ComputeFogFactor(fogState_, x²+y²+z²)` (the engine's @0x5ac9aa / @0x5beb0b
  per-vertex factor over the squared camera-space distance; the vertex keeps its
  view-space x/y/z because the reproject writes only screenX/Y). The wave-7
  `RasterizeTexturedTriangleRgbz` then interpolates that factor LINEARLY across the
  triangle and blends `BlendFog565` per pixel — a genuine per-pixel depth gradient, not
  the wave-6 per-triangle constant. Default `fogFactor = 255` (fog off) keeps every
  fog-off tri byte-identical.
* **Plumbing:** `fogState_` is exposed to the object span via
  `CityView3D::fogStateForSpan()` (read by `CV3D_SpanTextured` through `g_activeCV3D`),
  and to the terrain/water spans via `GroundViewParams::fog` -> `GroundTexState::fog`
  (set/cleared around the `RasterizeMeshList` flush in `GroundFrame::Render`).
* **Frame position:** unchanged — the per-pixel blend happens inside the textured span
  during the same object/terrain flush the wave-6 fog wiring already brackets.
* **Verified live (real AUGSBURG):** the fog-on frame differs from fog-off by
  56329 / 57600 channels (a substantial depth gradient); deterministic across reruns.

---

## The six documented rule-8 boundaries (data not carried by CityView3D — NOT faked)

Each of these wave-7 modules is reconstructed 1:1 and golden-tested in its own slice;
the reason it does not light up in the CityView3D frame is a DATA-AVAILABILITY boundary
in the simplified instance pipeline, named here with its handoff:

1. **SHADOWS node-driver + ground quad** (`shadow-node-wave7.md` / `shadow-ground-wave7.md`):
   the wave-7 drivers `Shadow_UpdateNodeShadows`/`Shadow_CastFromLight2` manage engine
   `ShadowNode`/`ShadowNodeSlot` records (the per-object 4-slot caster table at
   `obj+1780`, the per-object shadow-batch record `obj+492+244`, the ground-context
   record) and route to leaves (`RenderMeshShadow`, `BuildGroundShadow`,
   `AcquireCacheSlot`) that consume those records. CityView3D instances carry NONE of
   them (no caster table, no shadow-batch record, no ground-context). The `RenderMeshShadow`
   hook signature `(obj, mesh, ctx, viewNode, lightVec, originVec, light, slot)` needs the
   engine obj/mesh records to produce the silhouette tris — which CityView3D does not have.
   Forcing the drivers would require SYNTHESIZING fake ShadowNode records (rule-8 violation).
   The shadow-node-wave7.md handoff itself documents this: "coupled leaves through NAMED
   hooks (data/records unavailable standalone)" and "No bind-site edited". The VISIBLE
   drop-shadow is already produced at the exact frame seam by the wave-6 `doShadows()`
   direct `RenderObjectShadow` path (after terrain, under objects) — unchanged.
2. **PARTICLE integrators** (`particle-integrate-wave7.md`): the three integrators operate
   on the 0x310-byte particle SYSTEM block (`Emitter`/`Slot`). CityView3D models NO live
   particle systems — the chimney/fire/spray emitters are spawned by the sim/scene, not
   this view (the `doParticles` hook is a faithful no-op, the spawn list is the sim's,
   per particle-render-wave6.md). Nothing to integrate over.
3. **MIRROR scene-graph** (`mirror-scenegraph-wave7.md`): `PrepareReflectionNode` scans a
   mesh's child sub-objects for the reflective one (`child byte[104] & 0x20`). CityView3D
   scene nodes carry no reflective-child flag, so `reflectionPrepared` stays false and the
   5-term gate (`ShouldRenderMirrorPass`) correctly skips the pass — exactly the honoured
   gate wave-6 documented. (The hook scaffolding `SetMirrorAllocHook/FreeHook` /
   `SetMirrorFrustumTableHook` keep their faithful defaults.)
4./5./8. **BONE-MATRIX vertex lighting + ENV-MAP reflection UVs**
   (`vertex-lighting-normals-wave7.md` / `envmap-wave7.md`): both need PER-VERTEX
   OBJECT-SPACE NORMALS. The CityView3D `render::Vertex` (the shipped .bgf vertex) has NO
   normal field — the same boundary wave-6 named for the per-vertex NdotL diffuse term
   (frame-integration-wave6.md: "LightMeshVertices's per-vertex NdotL needs world NORMALS
   the simplified instance pipeline does not carry"). Env-map additionally needs the
   reflective-material flag (`texRec+104 & 1`), which the city material bind does not
   track. The dominant visible light term (the day/night AMBIENT scale) IS wired (wave-6
   `dynamicLight`). Synthesizing normals would be a rule-8 cheap analogue — not done.
9. **BILLBOARD tint** (`sprite-tint-wave7.md`, low priority): the node-level effect tint
   only runs on the billboard arm for `nodeType >= 5` nodes. CityView3D's city instances
   are mesh nodes (nodeType 3) drawn through the mesh project arm, so `BillboardApplyEffectTint`
   would be a no-op (it returns early for nodeType < 5). The handoff itself flags this as
   "no-op for nodeType<5 (document, low priority)". No billboard/sprite node is switched
   to the billboard arm in this view today.

---

## Determinism pins — RECALIBRATED: **NONE**

Every pre-existing pinned frame (city_view3d, universe, playable_flow, full_session,
terrain_ground, render_fidelity, frame_integration_wave6) stays BYTE-IDENTICAL with the
Options default-off: the new sky-bands path only runs under `Options::sky` AND a loaded
band table; the EF_WASS span only runs under `Options::water` + a bound water texture +
the water `+38 bit1` poly flag; the per-pixel fog only runs under `Options::fog` (the
`RgbzVertex::fogFactor` default 255 leaves fog-off tris untouched). The wave-6 e2e
(features ON over real AUGSBURG, including its byte-identical-rerun determinism check)
stays green WITH the new paths active. No test expectation changed.

Full `ctest` (GUILD_GAME_DIR set) = **1452 / 1452 pass, 0 failures**.

---

## Tests (rule 11)

`tests/e2e/frame_integration_wave7_e2e_test.cpp` — 3 tests, GUARDED on GUILD_GAME_DIR,
real AUGSBURG, all GREEN:

* `SkyBandsDriveTimeOfDayClear` — the loaded city's sky colour follows the per-scene
  band/blend ramp: noon (band 2) `0x4B8ECC` != dusk (band 5) `0x224E89`; the sky is
  drawn at both; the same time-of-day re-renders to the same colour (determinism).
* `WaterSamplesEfWassTexture` — `Options::water` + textures draws the water region
  (1 region / 400 polys / 268 verts) through the EF_WASS span; the textured-water frame
  is deterministic across reruns. (Skips cleanly when the scene has no water cells.)
* `PerPixelFogDepthGradient` — `Options::fog` differs the textured frame from the
  fog-off frame (56329 / 57600 channels) — a genuine depth gradient; deterministic
  across reruns; `fogApplied` reported.

The wave-6 e2e (`frame_integration_wave6_e2e_test`) and all 53 other render/terrain/
water/sky/fog/shadow suites stay green.

---

## Visual verify (vulkan-sdl preset)

`cmake --build build-vk --target guild_run` then
`SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy ./build-vk/guild_run --play
 --game-dir europe_guild_1400_original --frames 80`:

```
--play trace: mounted=1 loaded=1 liveObjects=55 persons=1 framesPresented=80
  daysAdvanced=0 view3d=1(796 inst, 786432 px) clock=1 fires=14 dump=1
```

Artifact `/tmp/guild_session_city3d.ppm` (1024×768): **3561 distinct colours** (up from
the wave-6 baseline 2299) — the refined scene with the band-driven sky backdrop,
EF_WASS-textured/scrolling water, per-pixel depth-fog gradient on the textured terrain
and objects, plus the wave-6 day/night object shades + drop shadows. PNG at
`/tmp/guild_w7_city3d.png`.

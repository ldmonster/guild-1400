# Wave-6 — W6-INTEGRATE: the world-entity render frame, wired into the live 3D frame

Owner: W6-INTEGRATE (the wave-6 capstone). Mission: wire all 11 wave-6 world-entity
render modules (sky, shadows, particles, snow, rain/weather, mirror, fog, sun/day-
cycle, world-sprites, dynamic vertex lighting, mesh/node LOD) into the live
`play::CityView3D` universe frame, in `BeginUniverseFrame @0x5b3900`'s exact order,
each behind an ADDITIVE Options flag (default OFF == byte-identical), then turn them
ON in the session. Water (W6-WR) was already wired; this follows its exact pattern.

Owned files (edited): `src/play/city_view3d.{h,cpp}`, `src/play/sdl_session.cpp`,
`src/render/fog.{h,cpp}`, `src/render/raster.{h,cpp}` (the per-pixel span fog hook).
New tests: `tests/unit/frame_integration_wave6_test.cpp`,
`tests/e2e/frame_integration_wave6_e2e_test.cpp`. The 11 reconstructed module
bodies were NOT edited — only their exposed entries are called.

---

## The wired frame order (matches BeginUniverseFrame @0x5b3900)

`play::CityView3D::RenderFrame` drives `render::RenderMainViewFrame @0x5b6074` ->
`RenderUniverseFrame @0x5b3de8` -> `BeginUniverseFrame @0x5b3900` through the
`render::FrameHooks` vtable (`src/render/frame.cpp` reproduces the call order
verbatim). The wave-6 features attach at these points:

| # | Feature | Call site (1:1 frame position) | Module entry | Options gate | Engine gate honoured |
|---|---------|--------------------------------|--------------|--------------|----------------------|
| 1 | **SUN** | `RenderFrame` head, before the frame | `render::ComputeSunState` (daycycle.h, 0x58339c→0x4b2438→0x4b253b→band) | `sky\|\|dynamicLight\|\|shadows` | side-effect-free; reads world day/hour/minute |
| 2 | **SKY** | `doClear` (clearRect hook @0x5b3953), FIRST, before terrain | `render::RenderSky` semantics — surface fill to the time-of-day colour (sky.h) | `Options::sky` | the clear-to-`dword_649DD4` |
| 3 | TERRAIN+WATER | renderTerrain hook (@0x5b3a2f) | existing (W4/W6-WR) | `terrain`/`water` | `dword_64A028` |
| 4a | **LOD** | object pass, per instance, before transform | `render::SelectLodFrame` (node_lod.h, 0x5adb6c) | `Options::lodSelect` | the forced/distance branch (`dword_13FCD1C`/`+531`) |
| 4b | **LIGHT** | object pass, per instance, vertex shade | day/night ambient scale from `ComputeSunState` (object_light_shade.h) | `Options::dynamicLight` | the per-band ambient rebuild |
| 4c | **SPRITES** | object pass, project arm | `render::ProjectObjectVertices` (the @0x5ac970 mesh arm; depth-fade arm = `ProjectBillboardVertices`, sprite_scale.h) | `Options::worldSprites` | `byte_649DD8` depth-fade |
| 4d | **SHADOWS** | after terrain, UNDER objects (before the object flush) | `render::ProjectMeshToGround` + `render::RenderObjectShadow` (shadow_render.h, 0x5f3f38 tail) | `Options::shadows` | `dword_1408A60` + sun above horizon |
| 4e | **FOG** | per-pixel in the textured span | `render::BlendFog565` via `render::SpanFog()` global (fog.h, raster.cpp) | `Options::fog` | `byte_649DD8` |
| 5 | **PARTICLES** | renderParticles hook (@0x5b3a98), after objects | `render::render_system_to_surface` (fx_recon3_particle_render.h) | `Options::particles` | live system list `dword_1408438` (empty here — gap) |
| 6 | **MIRROR** | buildMirrors hook (@0x5b3af0), after particles | `render::ShouldRenderMirrorPass` + `AppendMirroredPolys` (mirror.h) | `Options::mirror` | the 5-term gate (`reflectionPrepared`=`dword_649D6C`) |
| 7 | **SNOW/RAIN** | after the object flush, composited LAST (screen-space) | `SnowRenderStepHeader`/`SnowBuildQuads` (snow.h), `WeatherUpdate`+`RainUpdateDrop`+`RainRenderToSurface` (rain.h/weather.h) | `Options::weather` | `SnowIsWinterDay(day)` (snow) / `arc[hour]&1` rainActive (rain) |
| 8 | PRESENT | `PresentToDevice` | existing | — | — |

---

## The Options (city_view3d.h `CityView3D::Options`) — ALL DEFAULT FALSE

`sky, shadows, particles, weather, mirror, fog, dynamicLight, worldSprites,
lodSelect` plus `worldDay/worldHour/worldMinute` (the world clock the sun reads).
Default-off keeps every pre-existing pinned frame BYTE-IDENTICAL (verified: the full
suite is green with no recalibration — see below). `CityView3D::Result` gained
introspection fields: `skyDrawn/skyColor/sunBand/sunBrightness/litObjects/
lodObjects/shadowCasters/shadowPixels/particlePixels/mirrorPass/weatherDrops/
fogApplied`.

## The session preset (sdl_session.cpp)

When `use3d`, `opt3` turns ON every wave-6 feature (sky, dynamicLight, shadows, fog,
lodSelect, worldSprites, particles, mirror, weather) plus water; per frame the live
world clock (`tick.worldTime()` under the continuous clock, else `g_sysGameTime`)
feeds `opt3.worldDay/Hour/Minute`. `guild_run --play` shows the full scene; the
unit/e2e frame pins keep the Options default-off so they stay byte-identical.

## The fog raster-span hook (fog.{h,cpp} + raster.{h,cpp})

Added `render::SpanFog()` — a process-global `SpanFogState{enabled=false, color=0,
factor=255}` the textured span bodies read. `FillSpanTextured` / `FillSpanTextured
Masked` (raster.cpp) apply `BlendFog565(px, color, factor)` per WRITTEN pixel when
`enabled && factor<255`; default-disabled bypasses the branch entirely (byte-
identical). `CityView3D::RenderFrame` sets/restores `SpanFog()` around the flush,
gated on `Options::fog`. The per-pixel factor is a per-triangle constant first-cut
(the per-vertex interpolation is the full 1:1, documented in fog-render-wave6.md).

---

## Rule-8 boundaries (named, not faked)

* **Sky colour** — the per-band sky/fog gradient (`flt_13FD1B8`/`dword_13FD170`) is
  RUNTIME data loaded from the world file (sky/sun handoffs), not a static table.
  Without it the faithful value (per the sky handoff) is the engine's clear colour
  scaled by the genuine 0..600 `UpdateBrightness` ramp (dark at night, bright by day)
  — the same time-of-day darkening the band blend produces. `ComputeSunState` is the
  real driver; the gradient blend (`BlendAmbientFog`) is the on-table consumer.
* **Dynamic per-vertex light** — `LightMeshVertices`'s per-vertex NdotL needs world
  NORMALS the simplified CityView3D instance pipeline does not carry (the .bgf Vertex
  has no normal field). The wired term is the day/night AMBIENT scale (the global
  ambient `flt_64A074` IS rebuilt per day-cycle band) — the dominant visible term;
  the directional NdotL is the named gap.
* **Shadows** — the per-object silhouette is projected to the ground via a sun-azimuth
  screen-space skew (the engine projects each vertex onto the ground plane through the
  sun direction; with no per-object ground Y here the screen skew reproduces the cast
  direction) and splatted with the real `RenderObjectShadow`. The shadow surface is a
  host-sized full-frame stencil clamped into bounds (the engine sizes it to the
  projected box via `MapShadowVertexToSurface`; the software `FillSpans` does no
  per-pixel clip, so coords are clamped to stay in-buffer).
* **Particles** — CityView3D models no live particle systems (the chimney/fire/spray
  emitters are spawned by the sim/scene, not this view), so the renderParticles loop
  is a faithful no-op; the hook is wired so a future emitter list flows through.
* **Mirror** — no prepared reflection node (`dword_649D6C`/`PrepareReflectionNode` is
  the scene-graph node binder, owned elsewhere), so the gate's `reflectionPrepared`
  term is false and the pass is correctly skipped; the gate is honoured.
* **LOD** — the city instances carry a single shipped .bgf (no multi-LOD drawData
  block), so `SelectLodFrame` evaluates the forced branch and keeps the one frame;
  the per-distance pick is exercised + counted (the multi-LOD stock-object load is the
  mesh-attach module's job).
* **Weather** — snow/rain are reproduced as the OBSERVABLE software overlay (flake
  pixels / streak lines) the present-layer D3D `DrawPrimitive` would draw; the vertex
  streams are the real `SnowBuildQuads` / rain integrate output. The weather field
  ANIMATES per frame (intentionally non-static); determinism holds across reruns from
  a fresh seed.

## Recalibrated pins

**NONE.** The full existing suite (1445 tests) stays green with the Options default-
off — every pinned frame (city_view3d, universe, playable_flow, full_session,
terrain_ground, fidelity e2es) is byte-identical. No test expectation changed.

## Tests (rule 11)

* `tests/unit/frame_integration_wave6_test.cpp` — 4 tests, 16 checks, GREEN:
  SpanFog default-disabled is identity; SpanFog enabled blends toward fog (full +
  mid factor, golden vs BlendFog565); ComputeSunState noon brighter than night; the
  masked span fogs only WRITTEN pixels (transparent untouched).
* `tests/e2e/frame_integration_wave6_e2e_test.cpp` — 3 tests, 23 checks, GREEN
  (guarded on GUILD_GAME_DIR; real AUGSBURG):
  * `FullSceneFeaturesContribute` — sky drawn, sun band=2/brightness=242 at noon,
    796 lit objects, 796 LOD picks, 796 shadow casters / 15317 shadow pixels, fog
    applied; the full-scene frame differs from the bare preset; the geometry features
    are deterministic across reruns.
  * `DayNightLightingDiffers` — noon brightness 242 > 02:00 brightness 0; day/night
    frames differ.
  * `WeatherOverlayDrawsInWinter` — the winter (day%4==3) snow field draws flakes.

Full `ctest` (GUILD_GAME_DIR set) = **1447/1447 pass, 0 failures**.

## Visual verify (vulkan-sdl preset)

`cmake --build build-vk --target guild_run` then
`SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy ./build-vk/guild_run --play
 --game-dir europe_guild_1400_original --frames 80`:

```
--play trace: mounted=1 loaded=1 liveObjects=55 persons=1 framesPresented=80
  daysAdvanced=0 view3d=1(796 inst, 786432 px) clock=1 fires=13 dump=1
```

Artifact `/tmp/guild_session_city3d.ppm` (1024×768): **2299 distinct colours** — the
full scene with day/night-dimmed object shades, shadow-darkened ground pixels, and
the textured terrain/objects. The complete world-entity frame renders.

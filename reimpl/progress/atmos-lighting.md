# atmos-lighting — the brightness-consuming LIGHTING-TABLE REBUILD

Closes the "renderer consumption pending" gap of `progress/session-atmos.md`:
the original applies day/night brightness **pre-raster, by rebuilding the
per-object vertex lighting table** — there is no frame post-process. This slice
reconstructs that rebuild 1:1 and wires it so the day/night cycle changes
rendered pixels.

Files:
- `src/render/light_atmos.{h,cpp}` — the reconstruction (new)
- `src/play/wire_atmos_bridge.{h,cpp}` — `ApplyAtmosLightingFrame` (SessionAtmos → rebuild)
- `src/play/real_city_render.{h,cpp}` — `Options::atmosRelight` consumption gate
- `src/play/object_mesh_render.{h,cpp}` — gated `Options::cacheShade` (additive, default off)
- `tests/unit/light_atmos_test.cpp`, `tests/integration/atmos_lighting_itest.cpp`

## Reconstructed 1:1 (addr → symbol)

| addr | original | reconstruction |
|------|----------|----------------|
| 0x5c886c | VIBE_Light_RefreshAllObjects | `LightAtmosRefreshAllObjects` — ++serial dword_64A064 ALWAYS; (force\|\|byte_649D54): force≤1 → invalidate walk (RemoveCacheEntry per object, byte_64A068=1), force>1 → eager BuildObjectCache walk; floor bound (dword_64A028) → Floor_BuildTilePolys hook; result = walk/floor return else the incoming force |
| 0x5c8218 | VIBE_Light_BuildObjectCache | `LightAtmosBuildObjectCache` — null frame/hidden(530&2) → 0; ++dword_64A050; PrepareObjectCache hook; SEED every vertex accumulator (+48/52/56) = flt_64A074/78/7C; collect flag = bit5 of +528; accumulate hook; REDUCE (colour: chained max, scale 255/max when float-bits > 1132396544 (0x437EFFFF=254.999985f), trunc bytes +70/69/68 = R/G/B; luma: trunc(g·0.58999997 + r·0.30000001 + b·0.10999999), unsigned-saturate 255, byte +70); PUBLISH dword +64 ← dword +68 (**lightIdx(+66) = byte +70**); ApplyVertexShading; +528\|=4; +68=stamp; serial latch |
| 0x5c7f04 | VIBE_Light_ApplyVertexShading | `LightAtmosApplyVertexShading` — per poly w/ material: (530 bit0 clear) flags38 = (f&0xFB)\|(mat+104 bit4→bit2); word = (529 bit7 clear)? mat+108 : frame+376; word&0x10000 → per vertex trunc(min(255,(r·0.30+g·0.59+b·0.11+HIBYTE)·(1/256)·LOBYTE)) broadcast into dwords +64 AND +68; else LOBYTE≠0xFF → bytes +67/+71 = LOBYTE |
| 0x5c7da4 | VIBE_Light_RemoveCacheEntry | `LightAtmosRemoveCacheEntry` — key null → free whole list (head +408 model); else head-unlink / scan-unlink of the first key match; missing key no-op; returns 1 |
| 0x5add1c | ProcessSceneNode trigger arms | `LightAtmosEnsureNodeLit` — type +533==4 ∧ not floor; freshFrame arm: serial stale ∨ byte_64A068 (no budget); on-screen arm: serial stale ∧ dword_64A05C(512) > dword_64A060 → spent += vertexCount, rebuild; culled arm: serial stale ∧ dword_64A054(128) > dword_64A058 → spent += count, +528\|=4, rebuild |
| 0x5b85e4 | BlendBandLighting (store half) | `LightAtmosStoreAmbient` — flt_64A074/78/7C/70 = the band-lerped triple + luma (the lerp math itself is `render::BlendBandLighting`, sky.cpp; the 0x5b88cf tail call is `RefreshAllObjects`) |
| 0x5b3900 | BeginUniverseFrame (0x5b3982/8c only) | `LightAtmosBeginUniverseFrame` — dword_64A058 = dword_64A060 = 0 |
| 0x5b3bbc | DrawUniverseAndStats (0x5b3c19 only) | `LightAtmosEndUniverseFrame` — byte_64A068 = 0 |
| 0x5c8964 | VIBE_Light_ResetGlobalState | `LightAtmosResetGlobalState` — zeros 64A05C/60/58/64/54 |

Static-image globals verified via `get_bytes`: flt_64A070/74/78/7C = **200.0f**
(0x43480000), dword_64A054 = **0x80** (offscreen budget), dword_64A05C =
**0x200** (onscreen budget), byte_649D54 = byte_649D70 = 0, dword_64A028 = 0.

## The rebuild math (how brightness reaches pixels)

1. brightness (0..600) → band = trunc(b·0.01) % 7, blend = frac (SessionAtmos).
2. `BlendBandLighting(band, blend, 1.0, force)` lerps the 7-band rig row →
   ambient RGB, stored at flt_64A074/78/7C; tail-calls `RefreshAllObjects(force)`.
3. `RefreshAllObjects` bumps the serial (even at force 0 — the soft-window arm),
   force 1 additionally frees every object's affecting-light cache list and sets
   byte_64A068.
4. The draw walk relights stale nodes lazily under the 512/128 per-frame vertex
   budgets: `BuildObjectCache` re-seeds each vertex's accumulator triple from the
   ambient, (accumulates affecting lights — hook), reduces to the shade byte and
   publishes dword +64 ← +68, i.e. `Vertex::lightIdx = shade`.
5. The software rasterizer (raster.cpp `FillTexturedSpansShaded`, fed
   `RasterVertex::light = lightIdx`) interpolates exactly that byte into the
   framebuffer. **No palette/gamma/framebuffer modulation exists.**

## Wiring (rule 13)

- `play::ApplyAtmosLightingFrame(atmos, stampMs, appliedRebuildsCursor)` —
  fires the store + one `RefreshAllObjects(refreshForce)` per
  `SessionAtmos::lightRebuilds` increment (the new-day init rebuild happens
  *inside* `Frame`, so a cursor over the cumulative counter keeps the
  dword_64A064 serial in 1:1 step with the original's call count).
- `RealCityRenderer::Options::atmosRelight` (default off) — per resolved mesh
  frame a registered `LightAtmosObject` runs through the 0x5add1c on-screen arm
  (`RelightForPublic` adapter); `Result::relitMeshes` / `lightSerial` read-backs.
  Frame brackets mirror BeginUniverseFrame / DrawUniverseAndStats.
- `ObjectMeshRenderer::Options::cacheShade` (default off, additive) — a lit
  universe OBJECT's rasterized shade is the light-cache byte, not the Y-depth
  character term `ProjectVerticesToScreen` recomputes (the same engine truth
  `universe_render` applies by overwriting the projected lightIdx). real_city's
  atmosRelight pass sets it.

## Tests

- `tests/unit/light_atmos_test.cpp` — **23 tests / 461 checks**: static-image
  defaults; ResetGlobalState; StoreAmbient (via real BlendBandLighting); luma
  reduce goldens (10 vectors incl. the 200-exact seed, trunc cases 95/88/144/
  172/130, unsigned saturation incl. negative accumulators); band→shade sweep
  (t = 0/.25/.5/1 → 200/172/144/88); the packed-dword publish (+64←+68 byte
  ride-along, lightIdx=+70); colour-branch goldens incl. the 0x437EFFFF
  bit-pattern threshold edge (254.99998474121094f does NOT scale); collect flag
  = bit5; hidden/null gates; ApplyVertexShading recompute arm (199/163/82/255
  goldens, broadcast into both dwords), byte arm (+67/+71), 0xFF skip, null-
  material skip, flag38 bit edit + its 530-bit0 gate, frame-word fallback
  (529 bit7); RemoveCacheEntry list semantics (free-all/head/middle/missing/
  empty); invalidate-then-lazy-rebuild flow (serial/byte_64A068/budget spend);
  eager force-2; force-0 serial bump; relightAlways; budget gating + per-frame
  reset; freshFrame arm; type/floor gates; floor-hook result; registry hygiene.
- `tests/integration/atmos_lighting_itest.cpp` — **3 tests / 37 checks**:
  asset-free (synthetic stored-PKZIP `Resources/Objects.BIN` with a real-format
  AGF octahedron in a MemFileSystem). Noon vs midnight through the real
  `SessionAtmos` curve → `ApplyAtmosLightingFrame` → `RealCityRenderer`
  (`atmosRelight`) produce **different** frames; the same lighting state
  re-renders **byte-identical** with zero further rebuilds; a hysteresis-skip
  frame applies nothing; the OFF gate touches nothing.

All pass; regression suites stay green (real_city_render unit/itest/e2e,
object_mesh_render, session_atmos unit/e2e, wire_atmos_bridge, light_band,
object_light_shade, render_tile_lighting, texraster_recon2, universe_render
e2e, session_persons_render e2e).

## Named gaps (rule 8 — said so, not faked)

- `VIBE_Light_PrepareObjectCache @0x5c7e58` (sun position → bone-local +472,
  radius² → +484 via the frame vtable +504) — scene/bone coupled; hook
  `prepareObjectCache`, default absent.
- The affecting-light collect walk (`VIBE_Light_CollectAffectedObject @0x5c80a0`
  two-pass + the `d3_light:cache` alloc) and `VIBE_Light_ApplyToCachedVertices
  @0x5c6f90`'s scene-record reads — hook `accumulateLights`, default absent ==
  the engine's "no affecting lights" path (v25 == 0; ambient-only relight, which
  IS the day/night application). Its per-vertex point/sun math cores are already
  1:1 in `render/light.cpp` (`AccumulatePointLight` / `AccumulateDirectionalLight`)
  and `render/object_light_shade.cpp`.
- The poly material record (`*(poly+20)`, bytes +104/+108) — hook
  `polyMaterial`, default absent == the null-material skip.
- The BuildObjectCache child pass `WalkAndInvoke(obj+520, RecomputeForObject
  @0x5c7cf8, 4)` — attached children relight via their own registry entry.
- The scene-graph walk itself (`WalkAndInvoke`/`TraverseTree` over off_649D64) —
  stood in by the explicit `LightAtmosObject` registry (documented substitution;
  the walk leaves and every decision around them are 1:1).
- `flt_64A06C`: the original stores post-call ECX (zeroed before the
  PrepareObjectCache call) and explicitly 0.0 at the tail; modeled as the 0.0
  store both times (`rebuildScratch`).

## Wave-2 note — universe-chain hookup (universe_render owned elsewhere)

`src/play/universe_render.cpp` (DO NOT EDIT this slice) currently stamps a
**fixed** ambient shade at lines ~278–281:

```cpp
const u8 ambientShade = render::FinalizeVertexShadeLuma(
    render::kVertexLightAmbient, render::kVertexLightAmbient, render::kVertexLightAmbient);
for (int i = 0; i < g->vertexCount; ++i) g->vertices[i].lightIdx = ambientShade;
```

The exact hookup: replace the fixed `kVertexLightAmbient` (200) triple with the
live globals —
`render::FinalizeVertexShadeLuma(render::LightAtmos().ambientR,
render::LightAtmos().ambientG, render::LightAtmos().ambientB)` — or, fully,
register one `render::LightAtmosObject` per universe mesh and call
`render::LightAtmosEnsureNodeLit(obj, /*freshFrame=*/false, /*onScreen=*/true,
frameStamp)` in place of the manual overwrite, bracketing the frame with
`LightAtmosBeginUniverseFrame()` / `LightAtmosEndUniverseFrame()`; the session
loop then drives `play::ApplyAtmosLightingFrame` per game frame.

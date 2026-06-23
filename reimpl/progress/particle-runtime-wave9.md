# Particle RUNTIME loop — Wave-9 (W9-PARTICLE-RUNTIME)

**Owner:** W9-PARTICLE-RUNTIME · **Mission:** complete the per-frame particle
RUNTIME loop so a live system actually **produces + integrates + renders visible
particles** each frame. Before wave-9 the chimney-smoke / fountain / effect systems
were *spawned + walked but splat nothing* (frame-integration-wave8.md §3): every
piece existed but the integrator was never run, so the systems carried no live slots.

**Files owned / edited (rule: runtime glue only):**
- `src/render/particle_emitter_create.{h,cpp}` — the runtime glue (below).
- `tests/unit/particle_emitter_create_test.cpp` — +4 wave-9 runtime tests.
- this doc.

**CALLED, not edited (rule 13 — wire up, don't reinvent):**
`render/particle_integrate.{h,cpp}` (the reconstructed `pintegrate::UpdateSystem` +
the three integrators), `render/particle_spawn.{h,cpp}` (`CreateEmitter` /
`SpawnSystemByType` / `AllocSystem`), `render/fx_recon3_particle_render.{h,cpp}`
(`render_system_to_surface`), `render/emitter_setup.*`. No bind-site file
(`city_view3d` / `universe_render` / `sdl_session`) was edited — the handoff is
documented below.

---

## The missing edge (why it splatted nothing)

The pipeline had every leaf but no live RUNTIME tick. Two distinct obstacles:

1. **The integrator was never called.** `doParticles` (wave-8) walked
   `render::LiveSystems()` with a *counting no-op* callback; the reconstructed
   integrators (`pintegrate::UpdatePoints/Polys/Lens`) were never invoked, so no
   slot ever became active and the per-slot center/size/alpha the renderer reads
   stayed zeroed.

2. **Two incompatible views of the SAME 0x310 system block.** The original engine
   keeps ONE 0x310 block per system that serves BOTH spawn AND integrate. The
   reconstruction split it:
   - `particle_spawn.h::ParticleSystem` — a **compact** 88-byte typed struct
     (members at packed offsets: `count` at +20, `particles` at +8, …).
   - `particle_integrate.h::pintegrate::Emitter` — the **raw 0x310 block**, fields
     read at their TRUE byte offsets (spawn speed +0x60, spanA/B/C +0xBC/C0/C4,
     colour base +0xC8.., flags +0xCC/CD, slotCount +0xD0, lifeBase +0xE4, …).

   `sizeof(ParticleSystem)==88`, `sizeof(pintegrate::Emitter)==784` (verified) — so
   a `ParticleSystem*` **cannot** be reinterpret-cast to `pintegrate::Emitter*`.
   The integrator cannot read the spawn struct's fields.

## The fix — rebuild the genuine raw 0x310 emitter image at spawn

`SpawnEmitterAtPosition` already reuses the wave-7 `CreateEmitter` (which fills the
0xA4 default template in place). The original `SpawnSystemByType` does
`qmemcpy(system+44, template, 0xA4)` — i.e. the template lands at **system+44**, and
its filled fields land EXACTLY on the integrator's raw offsets:

| template off | value | → emitter raw off | integrator field |
|---|---|---|---|
| +0x34/38/3C | 10/50/30 | +0x60/64/68 | spawn speed `a1+96/100/104` |
| +0x80 | 1.0 | +0xAC | `life255` factor `a1+172` |
| +0x8C | 255.0 | +0xB8 | alpha bias `a1+184` |
| +0x90/94/98 | 20/160/180 | +0xBC/C0/C4 | spanA/spanB/spanC |
| +0x9C/9D/9E | 0xFF×3 | +0xC8/C9/CA | colour base (B/G/R) |
| +0xA0 | 0x20 | +0xCC | flagsLow (init bit5; bit7 clear → cap unscaled, gate open) |

So wave-9 reconstructs the real block: each linked `SystemNode` carries a
`pintegrate::Emitter emitter` whose `raw[44..44+0xA4)` = the CreateEmitter template,
plus the AllocSystem writes (`+0xD0` slotCount, `+0xE4` lifeBase, `+0xCC/CD` flag
bytes, `+0x24` lastTick re-armed, `+0x20` killedCount). This is byte-for-byte what
SpawnSystemByType+AllocSystem write into the engine's single block. The integrator's
**slot array IS the spawned system's 84-byte particle array** (`ParticleSystem::
particles`, the one AllocSystem primed) — `pintegrate::Slot` and the renderer's
`fxrecon3::ParticleSlot` are both 84-byte views of it. Therefore integrate writes
the +56/60/64 center + +72 size + +79 alpha into the SAME array the render walk
reads → particles become visible. The dispatch `SystemType` is the kind the
script/scene chose (0=Points / 1=Polys / 2=Lens), mirroring the `updateFn`
SpawnSystemByType installed at +0x304.

## API added (`particle_emitter_create.h`)

- `SystemNode` gains `pintegrate::Emitter emitter`, `pintegrate::SystemType type`,
  `bool runtimeReady` (true once the raw image is populated).
- `ParticleSystemList::WalkAndUpdate(u32 now)` — the per-frame UPDATE walk. Same
  head..sentinel order as `WalkAndRender` (the 0x5b3a86 traversal); for each node
  with `runtimeReady`, runs `pintegrate::UpdateSystem(type, emitter, slots, now)`.
  The integrator BOTH spawns dead slots (per the emitter gate/rate) AND advances
  live ones, writing the renderer-facing center/size/alpha. `next` captured before
  the call (self-unlink safe). Returns the count of systems updated. Inert nodes
  (runtimeReady==false, e.g. raw-`LinkAtTail` test nodes) are skipped — never faked.
- `WalkAndUpdate(now, after, user)` — variant with a per-system callback fired
  AFTER the integrator, for host frames that want integrate-then-render in one pass.
- `SpawnEmitterAtPosition(...)` now populates the raw emitter image + sets
  `type` / `runtimeReady` after the reused CreateEmitter/AllocSystem path.

## What now produces visible particles

With the default emitter template (chimney smoke = kind 0 / Points):
- frame 1: all slots dead → `UpdatePoints` SPAWN branch fires (gate open: flagsLow
  0x20 → `(i8)0x20 >= 0`), spawning up to `cap = slotCount`. Each spawned slot gets
  `center = accumulator = speed(10)*dir + jitter`, `size = 0*sin + lifeBase(180)`,
  `alpha` from the 3-span fade, `flags |= 1`.
- subsequent frames: live slots INTEGRATE (velocity advance, accumulator carry,
  center recompute, alpha fade up over spanA=20 then down past spanB=160, kill at
  spanC=180). `(flagsHi & 1)==0` → the system lives indefinitely (continuous smoke).
- the renderer (`render_system_to_surface`) reads those slots and rasterizes the
  billboard quads — **non-zero visible output** (proven by the test below).

The wave-7 **x87 turbulence-phase residual** (the `WrapPi` phase-wrap chain,
documented at particle_integrate.cpp PHASE-WRAP NOTE; addresses Points 0x5e23f8 /
Polys 0x5e2ed4 / Lens 0x5e36c8) stays as the wave-7 boundary — particles still
move / fade / cull correctly without it; it is NOT faked.

## The handoff (rule 13) — host frame: WalkAndUpdate BEFORE WalkAndRender

In the original, `VIBE_Render_BeginUniverseFrame`'s object-list walk drives each
system's update fn (+0x304) per tick and the renderer consumes the result in the
SAME frame. The reconstruction's `CityView3D::doParticles` (the 0x5b3a98 hook,
after the object pass, `src/play/city_view3d.cpp`) must therefore call, with the
frame tick `now` (CityView3D's frame clock):

```cpp
void CityView3D::doParticles(char /*a2*/) {
    if (!opt_.particles) return;
    // (1) UPDATE: integrate every live system (spawn + advance) BEFORE rendering.
    render::LiveSystems().WalkAndUpdate(/*now=*/frameTick_);
    // (2) RENDER: walk the same list and splat each system's slots into fb_.
    render::LiveSystems().WalkAndRender(
        [](render::ParticleSystem* sys, int /*view*/) {
            // build a fxrecon3::ParticleSystemView over sys->particles (+40),
            // sys->count (+208), the bound texture/palette; compute the bone
            // world matrix; then:
            //   fxrecon3::render_system_to_surface(fb_, view, world, projState_,
            //                                      blendModeFor(sys), colorReplaced);
        }, /*view=*/0);
}
```

i.e. the ONLY edit the bind-site needs is to add the `WalkAndUpdate(now)` line
before the existing walk, and to make the walk callback call
`render_system_to_surface` (the wave-6 leaf) with the framebuffer + ProjState +
bone-world matrix the view already owns (`projScalars_` / clip planes per
particle-render-wave6.md "EXACT CityView3D frame handoff"). No new Options flag —
particles are part of the base universe frame, already gated by `opt_.particles`.
The session (`sdl_session.cpp`) already spawns the smoke (`SpawnCityChimneySmoke`)
and opts into `Options::particles`; with the two lines above the smoke draws.

## Tests (rule 11) — `particle_emitter_create_test`, 83 checks, all pass

Pre-existing 8 suites (66 checks) unchanged + 4 new wave-9 runtime suites:
- `PEmit_Runtime.WalkAndUpdateSpawnsAndIntegrates` — spawn Points emitter (16
  slots, all dead), tick 8 frames via `WalkAndUpdate`; ends with active slots that
  carry a non-zero render center (moved off origin) + non-zero sprite size.
- `PEmit_Runtime.IntegratedSystemRendersNonZero` — tick 40 frames, confirm a live
  slot carries a non-zero output alpha, then `render_system_to_surface` over the
  SAME runtime-filled slot array draws >0 slots and writes >0 non-zero pixels.
- `PEmit_Runtime.DeterministicAcrossRuns` — seeded `crt::Srand(42)`, two runs yield
  identical active-count + summed center (the loop is deterministic).
- `PEmit_Runtime.WalkAndUpdateSkipsUnreadyNodes` — a `runtimeReady==false` node is
  not integrated.

Neighbours green: `particle_integrate_test` (37), `particle_render_wave6_test` (42),
`particle_spawn_test` (90), `fx_recon3_particle_render_test` (67).

## Build / verification note

My owned targets build clean and `particle_emitter_create_test` passes 83/83 when
compiled against the particle + render + crt sources directly. **The full CMake
`libguild.a` link is currently BROKEN by a CONCURRENT (non-particle) edit** in
`src/play/city_view3d.cpp` (`render::FlagHeraldry` does not exist — a wave-8/9 cloth
/ flags wiring in `RefreshObjectFlags`, ~line 2010, unrelated to this slice). That
blocks `ctest` for every target, not just mine. Once that bind-site compiles, my
test runs through CMake unchanged (it already compiles; only the lib link fails on
the unrelated symbol). This is NOT a particle-runtime regression.

## Determinism / byte-identical pins

`WalkAndUpdate` only touches `render::LiveSystems()`, which is empty unless a system
was spawned (a session-only path gated by `Options::particles`). The default
`RenderFrame` (no spawned systems, particles off) is untouched → all pinned frames
stay byte-identical. The new runtime tests seed the RNG so they are deterministic.

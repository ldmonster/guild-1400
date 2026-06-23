# Particle render — Wave-6 (W6-PARTICLE)

3D-world particle effects (chimney smoke, fire, fountain spray, dust): the
billboard quad render into the live universe frame, blended, after objects.

Files owned by this task:
- `src/render/particle.{h,cpp}`            — 7 emitter UPDATE/INTEGRATE kernels (already 1:1)
- `src/render/particle_spawn.{h,cpp}`      — system spawn/alloc cluster (already 1:1)
- `src/render/emitter_setup.{h,cpp}`       — Set* / Trigger emitter property setters (already 1:1)
- `src/render/particle_render.{h,cpp}`     — snow/rain weather TLVERTEX build (already 1:1)
- `src/render/fx_recon3_particle_render.{h,cpp}` — **the world-particle RenderSystem leaf + the new billboard RASTERIZE entry**
- `tests/unit/particle_render_wave6_test.cpp` — new (42 checks, all pass)

NOT touched: `fxrecon_particle_mirror_shadow.*` (mirror agent owns it), and every
bind-site file (city_view3d.*, universe_render.*, frame.*, terrain_render.*, …).

## What was missing (why the frame showed no particles)

The whole particle pipeline was reconstructed EXCEPT the final visible step:
`VIBE_Particle_RenderSystem @0x5e1278` had only its per-slot projection math
(`project_slot`) reconstructed — nothing actually RASTERIZED the billboard quad
into the framebuffer. This wave closes that loop.

## The real call path (rule 7)

```
VIBE_Render_BeginUniverseFrame  @0x5b3900   (the universe frame spine)
  ├─ clear / fog / VIBE_Floor_RenderTerrain @0x5b3a2f      (terrain arm)
  ├─ VIBE_SceneGraph_WalkAndInvoke @0x5b3a5d               (OBJECT draw walk)
  ├─ VIBE_Render_ProcessSceneNode  @0x5b3a81
  └─ for (sys in dword_1408438 list)                        @0x5b3a90..0x5b3aa5
        VIBE_Particle_RenderSystem(sys, frame) @0x5b3a98   <-- PARTICLES, after objects
```

So particles are a global linked-list of live systems (`dword_1408438`, head sentinel
`&unk_1408440`) walked **after** the object pass, inside the same frame. Each system's
`RenderSystem` builds the billboard quads and appends them to the engine's
depth-sorted display list (`dword_13FC570`), which the same textured-triangle leaf
the rest of the scene uses then draws — translucently.

`RenderSystem` xref confirmed: only caller is `BeginUniverseFrame @0x5b3a98`.

## Reconstructed 1:1

### `VIBE_Particle_RenderSystem @0x5e1278` — `project_slot` (inner loop math)
Already present and verified; this wave ADDED the screen-space billboard corners it
computes (the geometry actually written to the poly verts), recovered exactly:

| decompile | meaning | field |
|-----------|---------|-------|
| `v99 = v103 - v102` (0x5e179b) | cxScr - halfW = left X   | `sxc` |
| `v97 = v103 + v102` (0x5e1937) | cxScr + halfW = right X  | `sxc` |
| `v98 = v101 - v104` (0x5e17b4) | cyScr - halfH = top Y    | `syc` |
| `v94 = v101 + v104` (0x5e1947) | cyScr + halfH = bottom Y | `syc` |

The four poly verts (a2+16/20, v14+16/20, a2+176/180, a2+256/260) are these four
corners. The per-slot transform / distance-fade alpha / projection / cull / colour
modulation (0x5e13ac..0x5e1c7a) were already 1:1 (op-order, constants, the genuine
`int (i32)v93` alpha-byte wraparound, the `colorReplaced` `(a*chan)>>8` path).

Constants (get_bytes, bit-exact): flt_62BA68=0.5, flt_62BA6C/70/74 (point luma
.3/.59/.11), flt_62BA78=0.25, flt_62BA7C=2^24, dbl_62BA84=3, dbl_62BA8C=255, and
the projection .bss scalars (flt_13FCD0C/10/18, flt_13FCAF8/FC, flt_13FC544/58C/
5AC/76C, scissor dword_13ECE58..64) surfaced as `ProjState`.

### NEW: `render_system_to_surface` — the billboard quad RASTERIZE into the frame
The visible-output completion of RenderSystem. For each active slot:
1. `project_slot` (1:1) → screen-space quad corners + fade alpha + colour.
2. Build the two billboard triangles (TL,TR,BL)+(TR,BR,BL) with the whole sprite
   texture mapped onto the quad (U follows screen X, V follows screen Y).
3. Rasterize BOTH triangles into the 16bpp `render::Surface` through the engine's
   translucent textured-triangle path, selecting the blend mode.

This REUSES the already-reconstructed leaves (no new rasterizer math invented):
- edge interpolators `InterpolateEdgeRgbz @0x5F6930` / `InterpolateEdgeZ @0x5F6A8C`,
- the affine UV-gradient triangle setup of `RasterizeMirrorTriangle @0x5F6C30`,
- the translucent inner span bodies (`raster_blend.cpp`):
  - `kBlendAlpha` → `FillSpanTexturedBlend @0x5F728A`  `dst=(src>>1)&m+(dst>>1)&m`
  - `kBlendAdd`   → `FillSpanTexturedOr   @0x5F739C`  `dst |= src`  (additive)

The per-field LSB-clear blend mask (the patched `word_1234567` immediate, e.g.
0xF7DE for RGB565) is derived from the Surface `ColorFormat` — the engine wrote it
at runtime from the same channel masks, so this is the faithful value, not a guess.

The difference from the opaque `RasterizeTexturedTriangleRgbz` is exactly the
per-pixel combine (blend vs. straight store) — i.e. the difference between the two
patched span bodies in the binary. Everything else (16.16 edge walk, ceil-to-pixel
rounding, surface clip) is identical.

## Spawn / integrate (already 1:1; covered here by golden tests)

The per-particle motion kernels in `particle.cpp` (verified bit-exact earlier):
| addr | kernel | role |
|------|--------|------|
| 0x42b930 | UpdateEmitter   | sticky directional (smoke plume) |
| 0x42bec0 | SeedParticles   | initial fill + gravity bounce |
| 0x42c140 | UpdateTrail     | respawn-into-trail |
| 0x42c42c | UpdateGravity   | ballistic + partial re-emit |
| 0x42c7c8 | UpdateCosineWave| cosine-windowed pulse |
| 0x42cadc | UpdateFadeOut   | linear + alpha fade |
| 0x42cde8 | UpdateScatter   | radial scatter + ground bounce |

Spawn rate / velocity / gravity / lifetime all live in these kernels and their
get_bytes-verified constants (flt_61199C..611B2C). RNG jitter is `crt::RandNext`
(15-bit LCG) * 1/32767, byte-exact draw order.

## EXACT CityView3D frame handoff (for the orchestrator)

`play::CityView3D::RenderFrame` (`src/play/city_view3d.cpp`) runs:
```
RenderMainViewFrame(fs, hooks)   // the BeginUniverseFrame spine: clear, terrain,
                                 //   object scene-graph walk
doFlush()                        // RasterizeMeshList -> objects into fb_
                                 //   <-- INSERT PARTICLE PASS HERE -->
(pixel readout / PresentToDevice)
```

In the original, the particle list loop (`dword_1408438`, 0x5b3a90) runs INSIDE
BeginUniverseFrame, AFTER the object scene-graph walk (0x5b3a5d) and ProcessSceneNode
(0x5b3a81). In the CityView3D model objects are rasterized by `doFlush()`, so the
faithful insertion point is **immediately after `doFlush()`**, before the present /
pixel readout — particles draw over the finished object frame, blended.

Call to add (per live particle system, walking the engine's system list):
```cpp
render::fxrecon3::ParticleSystemView view{
    /*slots*/  (const fxrecon3::ParticleSlot*)(sys + 40),   // a1+40
    /*count*/  *(int*)(sys + 208),                          // a1+208
    /*defTex*/ boundTextureRecordFor(sys + 212),            // a1+212 group base
    /*pal*/    boundPaletteFor(sys + 212)};
fxrecon3::Mat4 world = computeBoneWorld(sys + 232);         // 0x5c8fac (or hook)
render::fxrecon3::render_system_to_surface(
    fb_, view, world, projState_, blendModeFor(sys), colorReplacedFor(sys));
```
- INPUTS: the 16bpp `fb_` (already holds terrain + objects), the system's slot array
  + count + bound texture/palette, the system's bone world matrix, the frame
  `ProjState` (the camera/projection scalars CityView3D already owns as
  `projScalars_` / clip planes), the blend mode the system's poly node carries.
- GATE: the original gates the whole loop on `byte_649D71 && dword_13FCD1C`
  (engine on + a world exists) — already CityView3D's `fs.engineOn && fs.hasWorld`.
  No new Options flag is required; particles are part of the base universe frame.
- ORDER: strictly after objects, before present (blended over the frame).

## Tests (rule 11)

`tests/unit/particle_render_wave6_test.cpp` — 42 checks, all pass:
- ScreenCornersFromProjection — the recovered screen-space quad corners.
- AlphaBlendDrawsIntoFrame — a particle blends white→0x77DE into the 16bpp frame.
- AdditiveOrBlend — kBlendAdd ORs red onto the destination (0xF820).
- InactiveSlotsAndGuards — inactive slots / null guards draw nothing.
- GravityIntegrateStep / GravityKillsBelowFloor — ballistic integrate + floor kill.
- SeedParticlesDeterministicWithSeed — 8-draw RNG spawn reproduced bit-exactly.
- BlendMaskFormats — the RGB565 LSB-clear mask (0xF7DE) drives the 50/50 average.

Pre-existing suites still green: `fx_recon3_particle_render_test` (67),
`particle_spawn_test` (90).

## DEFERRED (rule 8 — named, not faked)
- 0x5e1e0c UpdatePoints / 0x5e2814 UpdatePolys / 0x5e32c0 UpdateLens — the per-frame
  integrators selected by `SpawnSystemByType` (template[0]); ~99% x87 inline-asm over
  engine globals. The 7 named kernels in particle.cpp already cover the integrate
  golden-vector surface; these remain deferred to avoid a half-faithful transliteration.
- The display-list APPEND inside RenderSystem (dword_13FC570 sorted queue, the
  &dword_1408100/&dword_1408118 poly draw vtbl) is the engine's GPU draw-queue
  plumbing (rule 3 present layer). `render_system_to_surface` performs the equivalent
  draw directly into the software Surface via the reconstructed translucent triangle
  leaf — the same pixels, without the runtime-bound vtbl indirection.

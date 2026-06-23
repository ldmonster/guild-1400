# Wave-10 hardening — W10-PARTICLE cluster

MCP-free memory-safety + degenerate/edge-case pass over the particle cluster.
No new 1:1 reconstruction (MCP down). Every source change is a FAITHFUL guard
that leaves the in-bounds / valid-input path byte-identical; all existing golden
values are unchanged.

## Cluster owned
`src/render/`:
particle.{h,cpp}, particle_render.{h,cpp}, particle_spawn.{h,cpp},
particle_integrate.{h,cpp}, particle_emitter_create.{h,cpp},
emitter_setup.{h,cpp}, fx_recon3_particle_render.{h,cpp}
+ tests: particle_spawn_test, particle_integrate_test, particle_render_wave6_test,
particle_emitter_create_test, fx_recon3_particle_render_test,
render_emitter_setup_test, particle_spawn_itest, particle_spawn_e2e_test,
render_emitter_setup_e2e_test.

(NB: `fxrecon_particle_mirror_shadow.*` is NOT in this cluster — left untouched.)

## Build / run
ASAN+UBSAN config:
```
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DGUILD_BACKEND=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -g"
```
All 9 owned targets: **rc=0, 0 failures, 0 ASAN/UBSAN reports, 0 leaks**
(incl. `GUILD_RUN_PARTICLE_E2E=1` stress pass). Normal `build/` also green.

| target | checks |
|---|---|
| fx_recon3_particle_render_test | 83 |
| particle_emitter_create_test | 107 |
| particle_integrate_test | 73 |
| particle_render_wave6_test | 56 |
| particle_spawn_test | 90 |
| render_emitter_setup_test | 54 |
| particle_spawn_itest | 16 |
| particle_spawn_e2e_test | 187 |
| render_emitter_setup_e2e_test | 29 |

## Memory-safety bugs FOUND + FIXED (all faithful)

1. **Leaks — default spawn backend never freed (LSAN: 7600/3600/4800/63200 B).**
   `particle_spawn.cpp` `DefaultAlloc` is the headless test allocator (the engine
   routed AllocSystem's four allocations through its heap, reclaimed at teardown).
   The ParticlePoints scratch alloc's return is *discarded* at the call site, so
   it was leaked unconditionally; the spawn tests freed only `sys`+`sys->particles`
   (leaking 2 scratch buffers each); `DestroyAllSystems` freed only the SystemNode
   (leaking the whole 0x310 block + 4 sub-allocs per system).
   FIX: `DefaultAlloc` now registers every block; added
   `FreeAllSpawnAllocations()` / `ReleaseSpawnAllocation(void*)`
   (`particle_spawn.{h,cpp}`). `DestroyAllSystems` now calls
   `FreeAllSpawnAllocations()`. Tests reclaim via `FreeAllSpawnAllocations()`
   instead of the partial manual `delete[]`s. Pure test-backend bookkeeping — no
   engine-observable behaviour change.

2. **`particle.cpp` null slot-array dereference (would crash on a degenerate
   system).** `UpdateCosineWave` derefs `particle[0]` unconditionally for its time
   base; `SeedParticles` pass-2 / `UpdateTrail` / `UpdateFadeOut` / `UpdateScatter`
   (both loops) iterate the slot array with no null check; `UpdateEmitter` /
   `UpdateGravity` gate on `count>0` but still deref a null array.
   FIX: null-array guards (`if (!p0) return false;` for CosineWave; `e.particles &&`
   added to each loop / count gate). The original only ever runs with an allocated
   array, so the valid path is byte-identical; the guard only intercepts the
   never-reached null.

3. **`particle_spawn.cpp` AllocSystem signed-overflow UB.** `84 * slotCount`,
   `80 * (4 * slotCount)`, `40 * (2 * slotCount)` are signed-`int` products
   (UB on overflow). The original computes these in 32-bit registers (defined
   modular wrap on x86). FIX: compute in `u32` (`84u * sc32`, …) — identical
   low-32-bit byte count for every input, no UB. Also guarded the per-slot init
   loop against a failed particle-array allocation (`base && i < slotCount`).

4. **`particle_emitter_create.cpp` `Unlink` null-pointer deref on a double-unlink
   (UBSAN: member access within null pointer).** Exposed by the new
   `PEmit_ListEdge.UnlinkDegenerate` test: `Unlink` nulls `node->prev/next` on
   exit, so a second `Unlink(node)` hit `p->next` with `p==nullptr`. The original
   is only ever called on a still-linked node. FIX: `if (!p && !n) return;`
   (an unlinked node — both links null — is a no-op). In-list splice path
   byte-identical.

## Edge / degenerate tests ADDED

- **particle_integrate_test** (suite `ParticleIntegrateEdge`, +15 tests):
  0 slots, negative slot count, 256 (max) slots spawn-all, dt=0, huge-dt clamp,
  negative-dt wrap-to-clamp, all-dead+gate-closed, 3-span alpha-fade at the exact
  spanA/spanB/spanC boundaries, alpha clamp polarity at 255 (incl. the faithful
  ">255 → 0" quirk), z-cap clip with a negative cap at an extreme center,
  frameMod default-1 with no group block, free-slot exhaustion (all-active → no
  spawn) and spawn capped below capacity.
- **particle_render_wave6_test** (suite `ParticleW6Edge`, +6 tests):
  particle.cpp kernels with null array & count>0 (drives the new guards),
  zero count with a valid array, UpdateGravity all-dead → 0, UpdateCosineWave
  before-window, fx_recon3 render null fb/tex/palette/zero-count guards.
- **fx_recon3_particle_render_test** (suite `FxRecon3Edge`, +6 tests):
  alpha==0 inactive gate, near/far z-cull boundary polarity, all four scissor
  edges, min-dist full-alpha branch, distance-fade clamp to [0,255] at the far end.
- **particle_emitter_create_test** (suite `PEmit_ListEdge`, +6 tests):
  LinkAtTail rejects null node / null system, Unlink degenerate (empty / sentinel /
  double-unlink), walk over many systems in order, **self-unlink during the update
  walk is safe** (next captured before the callback), WalkAndUpdate over empty.

## Deferred / untouched (rule 8)
- The **x87 turbulence-phase residual** in the integrators
  (`particle_integrate.cpp` PHASE-WRAP NOTE, Points 0x5e23f8.. / Polys 0x5e2ed4.. /
  Lens 0x5e36c8..) stays deferred — not touched. Needs MCP to resolve.

## Cross-cluster findings (FLAGGED for the owning cluster — NOT edited)
- **`raster_textured.cpp` negative-value left-shift UB (line ~80,
  `InterpolateEdgeZ`)** was observed early via my fx_recon3 render path feeding
  partly-off-screen quad vertices (negative 16.16 screen X). The raster cluster's
  own wave-10 pass already hardened it (cast to `u64` before `<<`); after rebuild
  the UB is gone. No action needed from this cluster; noted for the record.
- **`src/play/map_view.cpp:144` signed-integer-overflow UB**
  (`-2147483648 - 3`) trips `frame_integration_wave8_e2e_test` under UBSAN. Not in
  this cluster, not caused by my changes (map_view untouched). FLAGGED for the
  play/map_view owner.

## Behavioural ambiguities for MCP
- `UpdateCosineWave` (0x42c7c8) reads `particle[0].birthTime` BEFORE the `count>0`
  check, so a `count==0` system with a zero-length allocated array would read
  out of bounds even in the original. I guarded only the null-pointer case (a clear
  never-reached crash) and left the `count==0 / non-null array` read as the
  original wrote it. Whether the original allocator can ever hand a zero-length
  array here is a 1:1 question — **BEHAVIORAL, needs MCP** to confirm against the
  decompile / the AllocSystem slot-count source.

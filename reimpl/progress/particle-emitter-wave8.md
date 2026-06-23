# Particle emitter CREATION + scene linkage (wave-8)

**Agent:** W8-EMITTER · **Module:** `src/render/particle_emitter_create.{h,cpp}` ·
**Tests:** `tests/unit/particle_emitter_create_test.cpp` (8 tests, 66 checks, all pass)

## Problem

The wave-6/7 cluster reconstructed the emitter TEMPLATE fill, system allocation,
per-type integrator dispatch (`render/particle_spawn.{h,cpp}`) and the per-system
RENDER walk (`fx_recon3_particle_render`, `particle_render`). But **nothing ever
linked a spawned system into the live list the per-frame render loop walks**, so
the city stayed empty of chimney smoke / fountains / effects. This module
reconstructs exactly that missing edge: the render scene-graph object list + the
scene→emitter spawn handoff.

## The live list (reconstructed 1:1)

Every live particle SYSTEM is a node of the render scene-graph object list,
cached in two module globals around a sentinel:

| global | addr | role |
|---|---|---|
| `dword_1408438` | 0x1408438 | HEAD — renderer walks from here |
| `dword_140874C` | 0x140874C | TAIL — AllocSystem inserts here |
| `unk_1408440`   | 0x1408440 | SENTINEL (one-past-the-end terminator) |

Node list pointers (fixed byte offsets in the 0x310 system block):

| offset | field |
|---|---|
| +0x2F0 (752) | owner — parent scene node (== `off_649D64` root for the walked systems) |
| +0x308 (776) | next — toward sentinel |
| +0x30C (780) | prev — toward head |

### Anchors

| addr | name | what was recovered |
|---|---|---|
| 0x5e0e00 | `VIBE_Render_InitObjectList` | head=&sentinel, tail=&sentinel (empty) |
| 0x5e11f5 | `VIBE_Particle_AllocSystem` (tail) | the exact insertion sequence (below) |
| 0x5e0e9c | `VIBE_Render_UnlinkObjectNode` | splice-out + head/tail recache |
| 0x5b3a86 | `VIBE_Render_BeginUniverseFrame` (loop) | the render walk (below) |

AllocSystem-tail insertion (disasm 0x5e11fb..0x5e1216), byte-exact:
```
mov  eax, dword_140874C             ; eax = old tail
mov  dword ptr [esi+308h], offset unk_1408440  ; node.next = sentinel
mov  [esi+30Ch], eax                ; node.prev = old tail
mov  [eax+308h], esi                ; oldTail.next = node
mov  dword_140874C, esi             ; tail = node
```

Render walk (disasm 0x5b3a86..0x5b3aa5):
```
mov eax, dword_1408438
cmp eax, offset unk_1408440 ; jz -> done
loop:
  mov edx, [eax+308h]               ; next captured BEFORE the call (self-unlink safe)
  call VIBE_Particle_RenderSystem   ; 0x5e1278
  mov eax, edx
  cmp edx, offset unk_1408440 ; jnz loop
```

`UnlinkObjectNode` confirmed `dword_1408438`/`dword_140874C` are mirror caches of
the scene-root object `off_649D64` child-head (+164) / child-tail (+168): the
particle systems are children of the scene root in the render object tree.

## Reconstructed API (`particle_emitter_create.h`)

- `ParticleSystemList` — `Init` (0x5e0e00), `LinkAtTail` (0x5e11f5), `Unlink`
  (0x5e0e9c), `WalkAndRender` (0x5b3a86), `Head/Tail/Sentinel/Count/Empty`.
- `LiveSystems()` — the process-wide live list (the `dword_1408438` analogue).
- `SpawnEmitterAtPosition(kind, worldPos, owner, texName, texSlot, amplitude,
  userType, trigger, slotCount, nowTick)` + defaults overload — the clean
  scene-facing entry: runs the wave-7 `CreateEmitter` default-fill, then
  `LinkAtTail`s the result into the live list. Returns null on AllocSystem
  failure (life<=0 / owner==0 / texture-load fail), nothing linked.
- `DestroyAllSystems()` — frees nodes + resets list (analogue of 0x5affec
  `VIBE_Render_DisposeAllObjects`).

REUSE (not redefined): `CreateEmitter` 0x43fd24, `SpawnSystemByType` 0x5e3ae0,
`AllocSystem` 0x5e1000 — all from `render/particle_spawn.{h,cpp}` (wave-7).

## The scene → emitter HANDOFF (rule 13)

Emitters are spawned by the script command **`CreateEmitter`**, registered at
0x440a6c by `VIBE_Script_RegisterObjectCommands` via `VIBE_Script_ImportCommand`
(0x445bc8). Registration recovered from the push sequence (cdecl, right-to-left):

```
ImportCommand("CreateEmitter", &VIBE_Particle_CreateEmitter,
              retType=1, argCount=7, argTypes=[1,1,1,1,1,6,1])
```

The 7 operands map to CreateEmitter's params:
`kind(eax), amplitude(ecx), texSlot(edx), owner(ebx), texName(string,type 6),
userType, trigger`. Building / chimney / fountain object scripts invoke it;
CreateEmitter fills the default template, dispatches the integrator by `kind`
(0=points 0x5e1e0c, 1=polys 0x5e2814, 2=lens 0x5e32c0), allocates, copies the
template, places, and AllocSystem links the system as a child of the owner node.
In the script path the placement is `&flt_5CA2E0 == {0,0,0}` (verified via
get_bytes) — the emitter sits at the owner's origin and inherits the owner's
WORLD transform (a chimney emitter rides its building).

**Bind-site (orchestrator wires this; not edited by me):** CityView3D /
scene-load should call `guild::render::SpawnEmitterAtPosition(kind, worldPos,
ownerNode, texName, texSlot, ...)` for each scene effect node / building chimney,
and the universe render frame (`city_view3d` / `universe_render`) should drive
`guild::render::LiveSystems().WalkAndRender(&renderFn, view)` where the original
calls `VIBE_Render_BeginUniverseFrame`'s 0x5b3a86 loop — i.e. replace the inert
particle render walk with a walk over `LiveSystems()`, calling the wave-6/7
`VIBE_Particle_RenderSystem` reconstruction (`particle_render` / fx_recon3) per
system. The render fn signature expected by `WalkAndRender` is
`void(ParticleSystem*, int view)`.

## Recovered constants (get_bytes / decompile)

- `flt_5CA2E0` = `{0.0, 0.0, 0.0}` — script-path placement (owner-local origin).
- Default template literals (already in `particle_spawn.h`, reused): life 180
  (dword_7653B8), scale 10/50/30, colour white (0xFF×3), alpha-time 255.0,
  initFill bit (byte_7653C0 |= 0x20).

## Tests

`tests/unit/particle_emitter_create_test.cpp` — 8 tests / 66 checks, all pass:
- `PEmit_List.InitEmpty` — empty-list invariant.
- `PEmit_List.LinkAtTailOrderAndFields` — insertion order + next/prev/owner writes.
- `PEmit_List.WalkOrder` — head→tail traversal order; empty-list no-op.
- `PEmit_List.UnlinkMiddleHeadTail` — splice-out + head/tail recache.
- `PEmit_Spawn.CreatesLinkedSystemAtWorldPos` — create → linked → walkable, placed
  at the world position, integrator dispatch + lifeBase preserved.
- `PEmit_Spawn.PerTypeIntegratorDispatch` — kind 0/1/2 → Points/Polys/Lens;
  out-of-range kind rejected, nothing linked.
- `PEmit_Spawn.FailsWithoutLifeAndDoesNotLink` — life<=0 → null, nothing linked.
- `PEmit_Spawn.DefaultOverloadSpawnsAtWorldPos` — defaults overload.

## Completeness / deferrals

- List + insertion + unlink + walk + scene-spawn entry: COMPLETE, 1:1.
- The per-frame integrators (`UpdatePoints` 0x5e1e0c / `UpdatePolys` 0x5e2814 /
  `UpdateLens` 0x5e32c0) remain deferred in wave-7 (x87 asm); their addresses are
  carried as the `updateFn` sentinels so the dispatch is verifiable and the wiring
  is ready when they land.
- `DestroyAllSystems` frees the SystemNodes it owns; the four wave-7 spawn-allocator
  blocks per system are tracked by `particle_spawn`'s default backend (headless
  build has no engine heap to return them to) — reset via that module's stats.

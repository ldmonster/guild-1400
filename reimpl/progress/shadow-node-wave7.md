# Wave-7 — Per-node shadow caster MANAGEMENT (the UpdateNodeShadows / CastFromLight drivers)

Agent **W7-SHADOWNODE**. Closes the rule-8 gap wave-6 deferred to "the fxrecon agent"
(see `shadow-render-wave6.md`): the per-node shadow-caster MANAGEMENT that drives the
(already-done) mesh-shadow splat each frame. Wave-6 reconstructed the projection +
rasterize halves (`ProjectMeshToGround` / `RenderObjectShadow` / `RasterizeTriangle`),
the light-list collector (`ResetLightList` / `PushToDrawList`), the caster-height
(`ComputeCasterHeight`) and the caster-slot bookkeeping (`AcquireCacheSlot` /
`ClearAllCasters` / ...). This wave reconstructs the two DRIVER functions that tie the
collector to the splat.

## What this wave reconstructed (addresses)

Owned module: `src/render/fxrecon_particle_mirror_shadow.{h,cpp}` (SHADOW portions only).

| addr       | name                              | status |
|------------|-----------------------------------|--------|
| **0x5f3f98** | `VIBE_Shadow_CastFromLight`     | 1:1 driver — `Shadow_CastFromLight2` |
| **0x5f4494** | `VIBE_Shadow_UpdateNodeShadows` | 1:1 driver — `Shadow_UpdateNodeShadows` |

These join the already-present `Shadow_CastFromAllLights` (0x5f444c) in the same file
(`CastFromAllLights` is the sibling driver `ProcessSceneNode` does NOT call — the live
per-node path is `ProcessSceneNode → UpdateNodeShadows → CastFromLight`).

### `VIBE_Shadow_CastFromLight` @0x5f3f98 — `Shadow_CastFromLight2`
Manages ONE caster slot for one (object, light) pair. Reconstructed verbatim:

1. **GATE (0x4025):** shadows enabled (`dword_1408A60`), object caster budget
   `(+533) < 5`, object is a caster `(+529 & 4)`, object has a caster table `(+492)`,
   light is a caster `(+529 & 4)`, light intensity `a2[37] != 0`. (The engine global
   capacity check `(2*dword_64A7EC-2) > *(*(dword_649D68+492)+256)` reads the active
   building's poly count, unavailable standalone — documented; the rest is exact.)
2. **SEARCH (0x402b..0x4065):** scan the 4 slots (stride 128, offsets 0/128/256/384,
   ≥512 stops) for one whose `entry+0` (surface) is set AND `entry+4` == this light.
3. **ALLOC+INIT (0x4065..0x4113, when no match / `v7==4`):** find the first slot whose
   `entry+0` is zero; init `+108=-1`, `+12=0`, `+4=light`, `+16=0`, `+20=0`,
   `+24/+28/+32=0`; `+124 = (16 * object+529) >> 7` (== object `+529` **bit3**). If
   `+124`: load the shared `"Schatten"` texture into `+0`; else compute the
   resolution-grow request (clamped to `dword_1408A54`) and `AcquireCacheSlot` into `+0`.
4. **REDRAW DECISION (LABEL_19 0x4113..0x42a4):** only when the slot has a surface.
   - origin = `PointThroughBoneChain(light, light+76)`; light **dir**:
     - directional (`light +529 bit4`): `RotateVectorByHierarchy(light, flt_5CA2B0)`,
       tol `v37 = 0.01`;
     - point: `dir = PointThroughBoneChain(node) − origin`, tol `v37 = 1.0`.
   - `v16 = !VectorWithinTolerance(dir, slot+24, tol) || *(mesh+380)` → `v17` redraw.
   - **coverage reuse path** (`a3 && !v16 && +108!=-1 && !+124`): max-scan the slot's
     cached bbox region of the ground-coverage grid (`+318`, stride 800/100); if
     `v17 || max != slot+20` → redraw.
   - `if (v17) RenderMeshShadow(...)` (+ refresh the cached dir); then
     `BuildGroundShadow(...)`.
   Returns 0 (the original always returns 0).

### `VIBE_Shadow_UpdateNodeShadows` @0x5f4494 — `Shadow_UpdateNodeShadows`
Per scene object, called by `VIBE_Render_ProcessSceneNode` (0x5add1c). Reconstructed:

1. Clear `+531 bit1` ("cast this frame").
2. If `+530 >= 0`: `TransformBoundingVolume(node, (+528>=0 ? dword_13FCD1C : 0), 0)`.
3. If `dword_1408A5C` (collected-light count): for each light — break if the object has
   no mesh (`a1[115]`); transform the 8 bbox corners through the light + view matrix
   (the wave-6 `Shadow_ProjectCorner*` / `Shadow_TransformProjectedPoint` math) and
   `ClassifyBoundingBoxPlanes`; if **visible** (classify bit6 `0x40` clear): set
   `+531 bit1` and `CastFromLight(node, light, ctx, node)`.
   Returns the last classify/cast byte (`return (char)a1`).

## Provenance / structs

Engine records modeled as documented-offset structs (`ShadowNode`, `ShadowNodeSlot`,
`ShadowLight`, `ShadowFVec3`) so the slot logic reads cleanly; the coupled leaves are
reached through inert-default hooks (rule 8, NAMED with address+reason in the header):
`LoadShadowTexture` (0x5da714), `AcquireCacheSlot` (0x5f1e7c — note `shadow.cpp`
already reconstructs the BODY of this; the hook is the management→cache seam),
`PointThroughBoneChain` (0x5c8b38), `RotateVectorByHierarchy` (0x5c8990),
`RenderMeshShadow` (0x5f363c — wave-6 `RenderObjectShadow` is its tail),
`BuildGroundShadow` (0x5f3048 — render_leaves7 owns the body),
`TransformBoundingVolume` (0x5ad438), `ClassifyBoundingBoxPlanes` (0x5ad1f4).

## Constants verified by get_bytes (this wave)

| symbol     | addr      | bytes (LE)              | value         |
|------------|-----------|-------------------------|---------------|
| flt_5F1D80 | 0x5F1D80  | 00 00 70 42 / 00 00 48 43 | {60.0, 200.0} (`kShadowSizeThresholds`) |
| flt_5CA2B0 | 0x5CA2B0  | 00..00 / 00 00 80 3F    | {0,0,1,0} up-dir for the directional rotate |

## Tests

New `tests/unit/shadow_node_update_test.cpp` — **11 tests / 41 checks**, ASan-clean:
- `CastGateRejectsEachClause` — every clause of the 0x4025 gate blocks the cast.
- `CastAllocatesFirstFreeCacheSlot` — fresh node → first free slot via the cache path,
  `+108=-1`, `+4=light`, `+124=0`.
- `CastAllocatesTexturePathWhenBit3Set` — object `+529 bit3` → `"Schatten"` texture, `+124=1`.
- `CastReusesMatchingLightSlot` — existing same-light slot found; no alloc; steady dir →
  no redraw, ground shadow still emitted.
- `CastRedrawOnDirectionChange` — changed dir → `RenderMeshShadow` + cached-dir refresh.
- `CastRedrawOnMeshDirty` — `*(mesh+380)` forces a redraw with a steady dir.
- `CastDirectionalUsesUpVectorRotate` — `+529 bit4` drives `RotateVectorByHierarchy`
  with `flt_5CA2B0 == {0,0,1}`.
- `UpdateClearsCastBitAndRefreshesBVol` — `+531 bit1` cleared first; bvol refreshed.
- `UpdateVisibleLightSetsBitAndCasts` — visible light → `+531 bit1` set + cast.
- `UpdateZeroLightsNoCast` — empty light list → no cast.
- `SizeThresholdConstants` — flt_5F1D80 golden bytes.

The existing `fxrecon_particle_mirror_shadow_test` (91 checks) still passes (this wave
only appends to the header + cpp).

**Build note:** the full `cmake --build` was transiently broken by *other* agents'
in-flight edits to `src/render/mirror_project.*` and `src/render/particle_integrate.*`
(unrelated to shadows — `MirrorPoly`/`Slot` static_assert size drift,
`speedHere_init` typo). Both my TUs compile cleanly in isolation
(`-Isrc -Iinclude -Ishim`, `-fsanitize=address`) and the linked test binary passes.

## EXACT CityView3D / ProcessSceneNode handoff (frame order, gate, inputs)

The live per-object shadow path is, in the original frame:

```
VIBE_Render_BeginUniverseFrame      0x5b3900
  ├─ (terrain arm 0x5b3a2f draws first)
  ├─ VIBE_Shadow_ResetLightList     0x5f4428   -> CollectedShadowLights() (<=4)  [wave-6, wired]
  └─ SceneGraph_WalkAndInvoke -> VIBE_Render_ProcessSceneNode  0x5add1c (per object)
        ├─ VIBE_Shadow_UpdateNodeShadows  0x5f4494   <-- THIS WAVE (per object)
        │     └─ VIBE_Shadow_CastFromLight 0x5f3f98   <-- THIS WAVE (per visible light)
        │           ├─ VIBE_Shadow_RenderMeshShadow 0x5f363c (RenderObjectShadow tail)  [wave-6]
        │           └─ VIBE_Shadow_BuildGroundShadow 0x5f3048                            [render_leaves7]
        └─ (object draw-list append; RasterizeMeshList 0x5aec88 flushes AFTER)
```

`UpdateNodeShadows` is called **per scene object, between the terrain draw and the
object mesh flush** (xref confirmed: only caller is `ProcessSceneNode` @0x5ae223).
So the shadow is splatted into its surface + emitted as a ground quad BEFORE the
object draws — shadows lie on the terrain, objects draw on top.

**The call the orchestrator adds to CityView3D's `ProcessSceneNode` object step**
(NOT edited here — handoff only; the bind site is `city_view3d.cpp` / the
`ProcessSceneNode` bridge, owned by the integration agent):

```cpp
// once per object in ProcessSceneNode, after terrain, before the object's draw append:
render::fxrecon::Shadow_UpdateNodeShadows(
    node,                                  // the object record (ShadowNode view)
    collectedLights, collectedCount,       // render::CollectedShadowLights() (<=4)
    viewMatBlock,                          // dword_13FCD1C float block
    castCtx,                               // dword_64A028 (Shadow_SetCastContext)
    /*forceCoverageScan=*/ ...);           // the original a3@ebx (coverage reuse)
```

Gate the orchestrator wires (already enforced inside the two drivers):
`Options::shadows` (→ `ShadowState().shadowLimitB`/`dword_1408A60`), object
`+529 & 0x4` (caster), object caster budget `+533 < 5`, a non-empty
`CollectedShadowLights()`, and per-light frustum visibility (classify bit6 clear).

The integration agent must install the real leaves via the hooks before the frame:
`SetPointThroughBoneChainHook` (0x5c8b38, reconstructed in `camera_recon`/`shadow_project`
callers), `SetRotateVectorByHierarchyHook` (0x5c8990), `SetClassifyBoundingBoxHook`
(0x5ad1f4), `SetTransformBoundingVolumeHook` (0x5ad438),
`SetRenderMeshShadowHook` (→ the wave-6 `render::RenderObjectShadow` driver),
`SetBuildGroundShadowHook` (→ render_leaves7's `BuildGroundShadow`),
`SetLoadShadowTextureHook` (0x5da714), `SetAcquireCacheSlotHook`
(→ the wave-6 `render::ShadowSurfaceCache::AcquireCacheSlot`, 0x5f1e7c).
Until installed, the drivers run with inert defaults (no crash, no shadow) — the
control flow, the gate, and the slot/redraw bookkeeping are exercised exactly.

## Completeness / deferred (rule 8)

- **Reconstructed 1:1, tested:** both drivers' control flow, the full gate, the
  slot SEARCH / ALLOC+INIT (texture vs cache path), the `+124` derivation, the redraw
  decision (dir-change tolerance branch directional/point, mesh-dirty force, coverage
  reuse structure), the `+531` bit management, the per-light visibility gate.
- **Coupled leaves through NAMED hooks** (data/records unavailable standalone): the
  bone-chain transforms, the frustum classifier, the bvol transform, the texture load,
  the cache acquire, the mesh-shadow render, the ground-shadow build — each with its
  address + reason in the header. None faked.
- **Documented-as-deferred data inside the drivers:** the gate's building-poly capacity
  term (`dword_64A7EC` vs active building `+256`), the coverage grid bytes scanned by
  the reuse path (the ground-ctx `+318` field), and the resolution-grow scan over
  `flt_5F1D80` against the mesh reference size (`mesh+472`) — the SCAN STRUCTURE is
  reconstructed; the per-mesh size source is the deferred ground-ctx data.
- **No bind-site edited** (ownership rule). The `ProcessSceneNode`/CityView3D handoff is
  documented above for the orchestrator.

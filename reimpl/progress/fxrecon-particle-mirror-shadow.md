# fxrecon — Particle / Mirror / Shadow cluster

Files:
- `src/render/fxrecon_particle_mirror_shadow.h`
- `src/render/fxrecon_particle_mirror_shadow.cpp`
- `tests/unit/fxrecon_particle_mirror_shadow_test.cpp` (91 golden checks, all pass)

Namespace `guild::render::fxrecon`. Self-contained; coupled leaves reached via
inert-default hooks (rule 8 honoured — no analogues, the hook is just the seam).

## Reconstructed 1:1 (full)
| addr | name | notes |
|------|------|-------|
| 0x42c104 | VIBE_Particle_ResetEmitter | rebind fields + clear bit0 of (slot+81), 84B stride, returns advanced base |
| 0x4d887c | VIBE_Particle_SpawnSparkleEffect | preset "punkte_nm_2"; pos={0,50.0f,0}; descriptor byte0=50,+1=0x501E; life 300; a8=50.0; a9=1.0f a10=2.0f a11=a12=0.5f bits -> SpawnEffect hook |
| 0x5e1238 | VIBE_Particle_SetOrientationFromAngle | enable-gate; BuildBasis->MatrixToEuler->SetWorldTranslation(obj+232) via hooks |
| 0x5f1d90 | VIBE_Shadow_AllocCache | records count/param, alloc 20*count |
| 0x5f1fc0 | VIBE_Shadow_InitBuffers | clamp limits to dword_1408078; poly 48*res^2; point 600*(res^2>>4); seed two 1.0f rows; flags; AllocCache; clear slotA0C; ret 16 |
| 0x5f20e8 | VIBE_Shadow_ShutdownBuffers | zero limits, free poly/point/cache/heightmap |
| 0x5f444c | VIBE_Shadow_CastFromAllLights | flag(+529 bit2)+count gate; loop dword_1408A10[] -> CastFromLight hook |

## Reconstructed kernel only (pure math extracted 1:1)
| addr | kernel | provenance |
|------|--------|------------|
| 0x5f4494 | Shadow_ProjectCornerDirectional / ProjectCornerPoint / TransformProjectedPoint | the 8-corner ground-projection + view-matrix transform inside VIBE_Shadow_UpdateNodeShadows |
| 0x5f5d08 | Mirror_BuildClipPlane (+ TriangleNormal 0x5cb824) | the plane eq n=normalize((v1-v0)x(v2-v0)), d=-(n.v0) inside VIBE_Mirror_CreateClippingPlanes |
| 0x5caa4c | VectorWithinTolerance | shared mirror-dedup / shadow dir-change test |

## DEFERRED / OMITTED (rule 8) — coupled leaves, not faithfully standalone
- **0x5f3f98 VIBE_Shadow_CastFromLight** — dereferences per-object caster table
  (obj+1780, 128B*4), light record fields, texture load/upload, cache-slot
  acquire, RenderMeshShadow/BuildGroundShadow. Driver guards reproduced via the
  CastFromLight hook in CastFromAllLights; the body is OMITTED (would require
  fabricating unverified node/mesh record layouts).
- **0x5f4494 VIBE_Shadow_UpdateNodeShadows (driver)** — node iteration +
  TransformBoundingVolume + ClassifyBoundingBoxPlanes + ComputeCasterHeight +
  a virtual call `(*(v8+500))()` for corner array. Only the pure projection
  kernel (above) is reconstructed.
- **0x5f5d08 VIBE_Mirror_CreateClippingPlanes (full)** — mesh-poly traversal
  (40B stride, vertex ptr arrays, flag bytes), AllocDebug/FreeDebug, CreateOutline,
  dword_13DB398 clip-plane prefix table. Only the plane-equation kernel is kept.
- **0x5f676c VIBE_Mirror_PrepareReflectionNode** — pure scene-graph draw-node
  mutation (node+4/+12/+16/+20 fields, ProjectReflectedVertices binding,
  RotateVectorWithFrame, dword_649D6C reentrancy guard). No standalone math;
  OMITTED. To wire: belongs in the existing `mirror*`/`scenegraph` module once
  the draw-node record is modelled there.

## Wiring
- **Done (in-file):** CastFromAllLights -> CastFromLight via `SetCastFromLightHook`
  + `Shadow_SetCastContext`. Particle spawn -> `SetSpawnEffectHook`. Orientation ->
  basis/euler/translation hooks. Allocator -> `SetAllocHook/SetFreeHook`.
- **Callers (xrefs) for future connection:**
  - CastFromLight is called by CastFromAllLights (done) and UpdateNodeShadows.
  - InitBuffers/Shutdown/AllocCache are the shadow-subsystem lifecycle pair;
    connect to the d3 engine init/teardown.
  - SpawnSparkleEffect xref: gameplay sparkle trigger (see SpawnEffect 0x42bd6c).
- **Pending:** real backends must install hooks (CastFromLight body, SpawnEffect,
  the three math helpers) when the scene-graph/mesh records are modelled.

## ODR
None of the 11 VIBE_ symbols pre-existed (grep clean). Existing `shadow.h` already
documents CastFromLight as DEFERRED and defines the same caster-table layout
(obj+1780, 128B*4) — consistent, not redefined here. All new symbols live under the
unique `guild::render::fxrecon` namespace.

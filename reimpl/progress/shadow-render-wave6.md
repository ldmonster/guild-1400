# Wave-6 — Object/Person Drop Shadows (1:1 render pipeline)

Agent **W6-SHADOW**. Mission: make object/person drop shadows render in the live 3D
city frame, 1:1 with `gilde.exe`. The shadow modules existed but no entry tied the
projection + rasterize halves together, so the frame produced no shadows. This wave
adds the clean per-object render entry and documents the exact CityView3D handoff.

## The original shadow pipeline (call tree, addresses)

```
VIBE_Render_BeginUniverseFrame        0x5b3900   per-frame frame spine
  └─ VIBE_Shadow_ResetLightList       0x5f4428   rebuild the 4-light shadow list   [DONE]
        └─ VIBE_Render_PushToDrawList 0x5f43f4   collector callback (+529&4 caster) [DONE]
VIBE_Render_ProcessSceneNode          0x5add1c   per scene object (terrain done first)
  └─ VIBE_Shadow_UpdateNodeShadows    0x5f4494   per-object: for each of ≤4 lights  [fxrecon agent]
        ├─ VIBE_Shadow_ComputeCasterHeight 0x5f34c0  ground height the shadow flattens onto [DONE]
        └─ VIBE_Shadow_CastFromLight  0x5f3f98   alloc/reuse caster slot, decide redraw [fxrecon hook seam]
              ├─ VIBE_Shadow_RenderMeshShadow 0x5f363c  PROJECT silhouette + RASTERIZE  [DONE — this wave]
              │     ├─ VIBE_Shadow_ComputeCasterHeight 0x5f34c0                          [DONE]
              │     └─ VIBE_Shadow_RasterizeTriangle   0x603ed4  (per shadow-mesh tri)   [DONE]
              │           ├─ VIBE_Raster_ComputeEdgeSlope  0x603d00                      [DONE]
              │           ├─ VIBE_Raster_InterpolateEdgeZ  0x5f6a8c                      [DONE]
              │           └─ VIBE_Raster_FillSpans         0x603da8                      [DONE]
              └─ VIBE_Shadow_BuildGroundShadow 0x5f3048  emit ground-shadow quad         [render_leaves7 agent]
                    ├─ VIBE_Shadow_ProjectGroundQuad     0x5f216c
                    └─ VIBE_Shadow_RasterizeHeightField  0x5f2a58
```

`RenderMeshShadow` (0x5f363c) is the heart: it projects the caster mesh's silhouette
onto the ground plane (point or directional light), accumulates the projected XZ
bounding box, derives the shadow-surface rect, maps each projected vertex onto that
surface, locks the surface, splats every triangle with `RasterizeTriangle`, then
unlocks/copies-out. This wave reconstructs the projection→map→rasterize spine and
exposes the clean per-object render entry.

## What this wave added (owned modules only)

All in modules W6-SHADOW owns. No bind-site / integration file was edited.

### `src/render/shadow_render.{h,cpp}` — per-object render entry
- **`RenderObjectShadow(tris, count, surf)`** — the geometry tail of
  `VIBE_Shadow_RenderMeshShadow` (loc `0x5f3f38`):
  ```
  v78 = 0;
  for ( j = mesh.tris; v78 < mesh.triCount; j += 40 )      // stride 40 bytes
      VIBE_Shadow_RasterizeTriangle(j, surf.bpp, surf.width, v78++);
  ```
  One `ShadowRasterState` (the `dword_13FCxxxx` accumulator globals) is shared across
  the whole loop, exactly as the original re-uses the process globals per triangle.
  `ShadowMeshTri` wraps the already-existing `ShadowTri` (three projected screen-space
  verts + the `+38` bit2 back-face flag the rasterizer's reverse-winding branch reads).
  This is the clean entry the live frame calls per shadow-casting object.

### `src/render/shadow_project.h` — projection → surface UV map + bounds gate
- **`MapShadowVertexToSurface(p, bounds, surfW)`** — the per-vertex surface map
  `RenderMeshShadow` applies after projection (`0x5f3bb6..0x5f3bef`):
  `u = (x-minX)*surfW/(maxX-minX)`, `v = (z-minZ)*surfW/(maxZ-minZ)`. The projected
  ground XZ box maps onto the `[0,surfW)` shadow texture; the rasterizer then reads
  vertex `+16/+20` as screen X/Y.
- **`ShadowBoundsAcceptable(bounds)`** — the bounds-acceptance guard at `0x5f3857`:
  every accumulated bound and span must be within ±16384.0 (`dbl_62C288`), else the
  whole rasterize tail is skipped.

These join the projection math already reconstructed in this module
(`ProjectVertexDirectional` 0x5f3721, `ProjectVertexPoint` 0x5f3ce3,
`ProjectMeshToGround` 0x5f363c body, `ComputeCasterHeight` 0x5f34c0).

## Constants verified by `get_bytes` (this wave)

| symbol      | addr       | bytes (LE)            | value     |
|-------------|------------|-----------------------|-----------|
| flt_62C278  | 0x62C278   | 00 00 00 3F           | 0.5       |
| flt_62C27C  | 0x62C27C   | 00 00 A0 40           | 5.0       |
| flt_62C280  | 0x62C280   | 00 00 80 C1           | -16.0     |
| flt_62C284  | 0x62C284   | 00 00 80 41           | 16.0      |
| dbl_62C288  | 0x62C288   | 00 00 00 00 00 00 D0 40 | 16384.0 |
| flt_62C6C0  | 0x62C6C0   | 00 00 80 47           | 65536.0   |
| dword_5AC540| 0x5AC540   | 01 00 00 00 02 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 | {1,2,2,0,0,1} → kEdgeNext {1,2,0}, kEdgePrev {2,0,1} |

All match the values already baked into the modules.

## Tests

New: `tests/unit/shadow_object_render_test.cpp` (5 tests, 18 checks, clean under ASan):
- `UvMappingCornersAndCenter` — projected box → [0,surfW) corner/center golden map.
- `BoundsAcceptanceGuard` — ±16384 bound + span gate (`0x5f3857`), incl. edge==limit.
- `ProjectMapRenderQuad` — end-to-end: project a quad's silhouette (straight-down sun)
  → map to surface → `RenderObjectShadow` → non-empty 8bpp stencil.
- `EmptyMeshNoOp` — 0 triangles → surface untouched, returns 0.
- `Render16bpp` — drives the 16bpp word-fill branch (fillValue 0xFFFF).

Existing shadow suites unchanged and still valid (this wave only *appends* to headers):
- `tests/unit/shadow_project_test.cpp`, `tests/unit/render_shadow_test.cpp`,
  `tests/unit/shadow_light_list_test.cpp`,
  `tests/integration/shadow_project_itest.cpp`,
  `tests/e2e/shadow_project_e2e_test.cpp`, `tests/e2e/render_shadow_e2e_test.cpp`.

Build note: the full `cmake --build` was transiently broken by *other* agents' in-flight
edits to `src/render/sprite_scale.h` and `src/render/water_render.*` (unrelated to
shadows). Every TU that includes a shadow header compiles cleanly in isolation
(`-Isrc -Iinclude -Ishim`), and the new test links + passes under
`-fsanitize=address`.

## EXACT CityView3D handoff (frame order, inputs, gate)

CityView3D drives `render::BeginUniverseFrame` (0x5b3900). Shadows attach at two points,
mirroring the original frame:

1. **Light-list rebuild (already wired).** `BeginUniverseFrame` calls
   `ShadowResetLightList` (0x5f4428) at frame start. This is bound today via
   `render::FrameHooks::resetLights = &render::ShadowResetLightListActive`
   (set in `src/play/wire_atmos_bridge.cpp`, the `resetLights` hook). The active
   universe root must be set once with `render::SetActiveShadowUniverse(root)` (a null
   root is a safe no-op). Result: `CollectedShadowLights()` holds ≤4 active
   `+529&0x4` caster lights, in walk order.

2. **Per-object shadow render — WHERE: AFTER terrain, UNDER objects.** In the original
   frame, `ProcessSceneNode` (0x5add1c) calls `UpdateNodeShadows` (0x5f4494) for each
   scene object; the terrain arm (`0x5b3a2f`) has already drawn, and the object draw
   list (`RasterizeMeshList` 0x5aec88) runs after. So the shadow is splatted into its
   surface and emitted as a ground quad **between terrain and objects** — shadows lie
   on the terrain, objects draw on top.

   The per-object call the orchestrator must add to CityView3D's object loop, for each
   shadow-casting object (gate: object `+529 & 0x4`, AND `CollectedShadowLights().count
   > 0`, AND the global enable `dword_1408A60`/`Options::shadows`):

   ```cpp
   // For each of the ≤4 collected shadow lights (fxrecon UpdateNodeShadows loop, 0x5f4494):
   //   1. project the object silhouette onto the ground for this light
   render::ShadowVec3 light = /* light pos (point) or dir (directional, +529&0x10) */;
   float groundY = render::ComputeCasterHeight(casterTable, casterFlag, heightQuery); // 0x5f34c0
   render::ShadowBounds b =
       render::ProjectMeshToGround(meshVerts, n, projVerts, light, directional, groundY); // 0x5f363c
   if (!render::ShadowBoundsAcceptable(b)) continue;            // 0x5f3857 gate
   //   2. map each projected vertex onto the object's shadow surface
   for (each shadow-mesh triangle t)
       for (k in 0..2)
           auto uv = render::MapShadowVertexToSurface(projVerts[tri[t][k]], b, surf.width);
           tris[t].v.x[k] = uv.u; tris[t].v.y[k] = uv.v;        // 0x5f3bea
   //   3. splat the silhouette into the shadow surface  (THE NEW CLEAN ENTRY)
   render::RenderObjectShadow(tris, triCount, surf);            // 0x5f3f38 tail
   //   4. emit the ground-shadow quad onto the terrain  (render_leaves7 agent owns this)
   //      VIBE_Shadow_BuildGroundShadow(...);                  // 0x5f3048
   ```

   The caster slot allocation / redraw decision (whether step 1–3 re-run vs reuse the
   cached surface) lives in `CastFromLight` (0x5f3f98), surfaced as the fxrecon
   `CastFromLightHook` seam (`render::fxrecon::SetCastFromLightHook`). `RenderObjectShadow`
   is the function that hook ultimately drives once a redraw is needed. Caster-slot
   bookkeeping (acquire/free/clear/remove-by-light) is reconstructed in
   `src/render/shadow.{h,cpp}` (0x5f1e7c / 0x5f4710 / 0x5f474c / 0x5f47dc).

   Gate summary the orchestrator wires: `Options::shadows` (→ `dword_1408A60`), object
   `+529 & 0x4`, and a non-empty `CollectedShadowLights()`.

## Completeness / deferred

- **Reconstructed 1:1, tested:** light-list collect (0x5f4428/0x5f43f4), caster-height
  (0x5f34c0), vertex projection point+directional (0x5f3ce3/0x5f3721) + bounds fold,
  surface UV map (0x5f3bb6..ef), bounds gate (0x5f3857), flat shadow-triangle rasterizer
  (0x603ed4 + 0x603d00/0x5f6a8c/0x603da8), per-object render entry (0x5f3f38 tail),
  caster-slot bookkeeping (0x5f1e7c/0x5f4710/0x5f474c/0x5f47dc).
- **Owned by other agents (do not duplicate):** `UpdateNodeShadows` loop + `CastFromLight`
  hook seam → `src/render/fxrecon_particle_mirror_shadow.*`; `BuildGroundShadow`
  clip-rect + `ProjectGroundQuad`/`RasterizeHeightField` dispatch → `src/render/render_leaves7.*`.
- **Platform tail (rule 3/4):** the surface lock/unlock/copy-out around the tri loop
  (`VIBE_Render_LockSurfaceRegion` 0x4346c8, `VIBE_Render_UnlockSurface` 0x4350ac,
  `VIBE_Render_CopySurfacePixels` 0x5de87c) are the DirectDraw surface ops; here the
  caller owns the `ShadowSurface` directly, so `RenderObjectShadow` is the pure splat.
```

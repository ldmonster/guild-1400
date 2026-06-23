# texraster_recon2 — texture surface/cache, span patchers, scene light

Cluster: VIBE_Texture / VIBE_TextureCache / VIBE_Light / VIBE_Raster helpers.
Strict 1:1 from Hex-Rays. NEW files only (no edits to existing files/CMake).

## Files
- `src/render/texraster_recon2_texsurf.{h,cpp}` — DDraw-surface layer of the texture record
- `src/render/texraster_recon2_texcache.{h,cpp}` — texture/tile cache teardown
- `src/render/texraster_recon2_spanpatch.{h,cpp}` — span-constant SMC patchers (boundary-modelled)
- `src/render/texraster_recon2_light.{h,cpp}` — scene light orchestration
- `tests/unit/texraster_recon2_test.cpp` — 20 golden-vector tests (889 checks, all pass)

## Translated (done)
| addr | name | notes |
|------|------|-------|
| 0x5d9660 | VIBE_Texture_RestoreSurface | mip/dynamic/plain CreateDynamicTexture select |
| 0x5d96c0 | VIBE_Texture_LoadFromCache | free-surface cache match predicate + claim |
| 0x5d995c | VIBE_Texture_SetBasePath | VFS normalize (hook) + basePathSet global |
| 0x5d9970 | VIBE_Texture_ReleaseSurfaces | evict-vs-release-each + texel free |
| 0x5db624 | VIBE_Texture_ReleaseSurface | guard, release both, re-upload gate |
| 0x5dbc38 | VIBE_Texture_CapturePaletteSurface | wrapMask recompute + 0xRRGGBB->RGB unpack |
| 0x5b9444 | VIBE_TextureCache_Free | drain '*' tile slots (68B), free array |
| 0x5ba37c | VIBE_TextureCache_Setup | identity tex matrix (0/1), filter capture |
| 0x5ba428 | VIBE_TextureCache_Shutdown | Free + clear stamp |
| 0x5d9b78 | VIBE_TextureCache_DisposeAll | drain+zero records(128B)/mips(776B), reset globals |
| 0x5d9c98 | VIBE_TextureCache_Shutdown_d9c98 | drain + FREE record/mip arrays + SurfaceCache_FreeAll |
| 0x5f753f | VIBE_Raster_PatchSpanConstantsMasked | SMC -> SpanPatchVariantParams (Masked) |
| 0x5f757e | VIBE_Raster_PatchSpanConstantsBlend | SMC -> params (Blend) |
| 0x5f75bd | VIBE_Raster_PatchSpanConstantsBlendMasked | SMC -> params (BlendMasked) |
| 0x5f75fc | VIBE_Raster_PatchSpanConstantsOr | SMC -> params (Or) |
| 0x5f763b | VIBE_Raster_PatchSpanConstantsOrMasked | SMC -> params (OrMasked) |
| 0x5f71ac | VIBE_Raster_NullStub17 | bare retn (inert) |
| 0x5f74ff | VIBE_Raster_NullStub18 | bare retn (inert) |
| 0x42dc7c | VIBE_Light_CreateSunRays | sun-rays obj attach + exact flag-byte edits |
| 0x5047c0 | VIBE_Light_ApplyTorchEffects | collect torches, reparent type>=5 children, detach |
| 0x504860 | VIBE_Light_RefreshTorchLighting | traverse + octree rebuild guard |
| 0x504a00 | VIBE_Light_EnableDaylight | free/rebuild octree (region 7,8) |
| 0x5c7d70 | VIBE_Light_RegisterUpdateCallbacks | two TraverseTree passes |

## Deferred / omitted
- **0x5da4b4 VIBE_Texture_LoadAnimatedSet (608B)** — OMITTED (Rule 8). Decompile
  has multiple uninitialized-variable artifacts used before definition (v19, v20,
  v25, v28) in the path-build / file-exists / cache-slot-mutation logic; cannot
  be faithfully translated from the pseudocode alone. Needs a disasm-level pass.

## Boundary (Rule 3 / inert hooks)
- DDraw surface release `(*(vtbl+8))`, palette readback `(*(vtbl+80))`, Blt
  `(*(vtbl+20))`; VIBE_Render_CreateDynamicTexture/CreateSurfacePalette/ReportDDrawError.
- VIBE_Memory_FreeDebug, VIBE_Vfs_NormalizeDirPath, channel-LUT/filter builders.
- Self-modifying span code (0x5F72xx..0x5F744x): patchers reconstructed as
  parameter-struct fills (same convention as raster_textured.cpp BuildSpanTexParams).
- All scene-graph/object/light callees for the Light cluster (separate clusters).

## Wiring
- Each function wires its translated callees through the hook tables
  (TexSurfHooks / TexCacheHooks / LightSceneHooks / SpanPatchSources). Within the
  cluster the calls are already wired (RestoreSurface<-LoadFromCache<-CapturePalette;
  Free<-Shutdown; DisposeAll/ShutdownFull reuse ReleaseEntry drain).
- PENDING external wiring (callers in already-reconstructed code, per xrefs_to):
  - TextureCache_Shutdown / ShutdownFull <- VIBE_Render_ShutdownEngine (0x5b0228)
  - DisposeAll <- VIBE_Render_DisposeAllObjects (0x5affec)
  - LoadFromCache <- VIBE_Texture_CreateRecord (0x5db724), Shadow_RenderMeshShadow (0x5f363c)
  - ReleaseSurface <- VIBE_Snow_UpdateScene (0x42a2cc), SetTransparencyFlag (0x5db694)
  - CreateSunRays <- VIBE_Weather_RenderAndThunder (0x4c05ac)
  - RegisterUpdateCallbacks <- VIBE_Render_BeginUniverseFrame (0x5b3900)
  These callers are in other modules; hooks must be bound at their integration
  points (not edited here per the new-files-only constraint).

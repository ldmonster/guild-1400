# texlight_recon — texture surface cache / hi-colour bank / log10 table

Cluster: VIBE_Texture / VIBE_Light / VIBE_TextureCache / VIBE_SurfaceCache / VIBE_HiColTab.

Files:
- `src/render/texlight_recon.h`
- `src/render/texlight_recon.cpp`
- `tests/unit/texlight_recon_test.cpp`

## Reconstructed 1:1 (pure record/cache/math)
| addr | name | where | notes |
|------|------|-------|-------|
| 0x5d93f4 | VIBE_SurfaceCache_Init | SurfaceCache::Init | 20-byte entries, capacity/liveCount |
| 0x5d9430 | VIBE_SurfaceCache_FreeAll | SurfaceCache::FreeAll | releases occupied (surf1 then surf0) via inert hook |
| 0x5d94c8 | VIBE_SurfaceCache_StoreEntry | SurfaceCache::StoreEntry | flag0=(32*f)>>7, flag1=(16*f)>>7, stamp=frame, kind==8 probe hook |
| 0x5d9580 | VIBE_SurfaceCache_EvictAndStore | SurfaceCache::EvictAndStore | full->lowest-stamp evict; else first free slot; else release tex surfs |
| 0x5da04c | VIBE_HiColTab_FindOrBuild | HiColTabBank::FindOrBuild | bank fit-scan, alloc 0x8200, seed black, map via AddEntry |
| 0x5d988c | VIBE_Texture_InitTables (log10 table + sizing) | BuildLog10ByteTable / HiColRecordArrayBytes / TextureRecordArrayBytes | byte[k]=round(log10(k-1)); 776*n and 128*n |

## Already present — SKIPPED (ODR)
| addr | name | existing |
|------|------|----------|
| 0x5d9db8 | VIBE_HiColTab_AddEntry | src/render/hicoltab.cpp (HiColTabAddEntry) — reused |
| 0x434f30 | VIBE_Result_Handler_Final | src/render/colorformat.cpp (PackColor) — reused |
| 0x5b9368.. | terrain TILE LRU cache | src/render/texture_cache.cpp (TileCache) — distinct cache |

## Deferred / OMITTED (rule 8 — coupled, no faithful pure core)
| addr | name | reason |
|------|------|--------|
| 0x5d9660 | VIBE_Texture_RestoreSurface | thin wrapper over VIBE_Render_CreateDynamicTexture (GPU) |
| 0x5d96c0 | VIBE_Texture_LoadFromCache | surface-cache match + VIBE_Render_CreateSurfacePalette (GPU) |
| 0x5d995c | VIBE_Texture_SetBasePath | VFS path normalize (file I/O subsystem) |
| 0x5d9970 | VIBE_Texture_ReleaseSurfaces | COM vtable Release + slot fixup; couples to ReleaseEntry tree |
| 0x5db624 | VIBE_Texture_ReleaseSurface | COM vtable Release + UploadToSurface |
| 0x5da4b4 | VIBE_Texture_LoadAnimatedSet | string parsing + VFS + LoadByName (file I/O / loader tree) |
| 0x5dbc38 | VIBE_Texture_CapturePaletteSurface | DirectDraw GetPalette/Blt COM calls |
| 0x5b9444 | VIBE_TextureCache_Free | terrain-tile cache teardown (ReleaseEntry tree) |
| 0x5ba37c | VIBE_TextureCache_Setup | channel-LUT/mip-filter device init (GPU) |
| 0x5ba428 | VIBE_TextureCache_Shutdown | thin wrapper over _Free |
| 0x5d9b78 | VIBE_TextureCache_DisposeAll | ReleaseEntry tree + bank free (couples records+banks) |
| 0x5d9c98 | VIBE_TextureCache_Shutdown_d9c98 | ReleaseEntry tree + bank/surface-cache free |
| 0x42dc7c | VIBE_Light_CreateSunRays | object factory / scene-graph attach |
| 0x5047c0 | VIBE_Light_ApplyTorchEffects | scene-graph walk + object reparent/detach |
| 0x504860 | VIBE_Light_RefreshTorchLighting | scene-graph traverse + octree rebuild |
| 0x504a00 | VIBE_Light_EnableDaylight | scene-graph traverse + octree rebuild |
| 0x5c7d70 | VIBE_Light_RegisterUpdateCallbacks | scene-graph traverse (two callbacks) |

The Light cluster is entirely scene-graph/object-factory driven (no isolated lighting
math kernel in this cluster); object/vertex lighting math already lives in
src/render/object_light_shade, vertex_lighting, tile_lighting, anim_relight.

## Wiring
- HiColTabBank::FindOrBuild calls the existing HiColTabAddEntry (0x5d9db8) directly.
- SurfaceCache GPU surface release/create + DDraw probe are routed through
  SurfaceCacheHooks (default no-op) so the headless build links with no backend.
- Live-tree callers (VIBE_Texture_CreateRecord 0x5db724, VIBE_Texture_LoadSoftPalettize
  0x5da34c for FindOrBuild; VIBE_Texture_InitTables 0x5d988c for the cache/table)
  are the deferred GPU/loader functions above; wiring into them is PENDING until
  those coupled functions are reconstructed.

## Tests (golden vectors)
tests/unit/texlight_recon_test.cpp — prefix TexLightRecon*:
- SurfaceCache: Init/Store flags+stamp/release-old/FreeAll/Evict-fill/Evict-LRU/Evict-none
- HiColTab: alloc+map+565 direct value; dedup into same bank
- Log10Table: golden round(log10(k-1)) values incl. inf and round boundaries
- Sizing: 776*n / 128*n formulas

# Recon 02 — Graphics / 3D Engine / Rendering Cluster

Binary: `gilde.exe` ("Die Gilde" / "The Guild"), 32-bit x86, imagebase 0x400000.
Source units referenced in strings: `d3_d3d.c`, `d3_engine.c`, `d3_interface.c`, `ts_texture.c`, `d3_sky.c`, `dd_vesa.c`, `gfx.c`.
All addresses below are absolute (imagebase 0x400000).

---

## 1. Overview — Software renderer with optional Direct3D path

**This engine is fundamentally a SOFTWARE rasterizer.** DirectDraw is used only for
display-mode setup and as one of several *presentation* (blit-to-screen) back-ends.
Direct3D (the legacy DirectDraw3D / IDirect3DDevice immediate-mode interface) is an
*optional* hardware-accelerated alternative path, gated by a config flag.

### Imports actually present (the whole DX/GDI surface)
- `ddraw.dll`: `DirectDrawCreate` (0x60E82C), `DirectDrawEnumerateExA` (0x60E830). **No Direct3D9/8 imports** — D3D is reached via DirectDraw's `QueryInterface`/vtables (DX6-era retained/immediate mode), so it appears only as raw vtable index calls (`*(vtbl+N)`), not named imports.
- `dinput.dll`: `DirectInputCreateA` (0x60E838) — input only, not graphics.
- `gdi32.dll`: `BitBlt` (0x60E4FC), `CreateCompatibleDC` (0x60E500), `CreateDIBSection` (0x60E504), `DeleteDC`, `DeleteObject`, `GetStockObject`, `SelectObject`, `SetBkMode`, `SetTextColor`, `TextOutA`. This is the GDI/DIB software-present path (the `dd_vesa.c` / `gfx.c` lineage).
- No `dsound` in this cluster.

### How a frame flows
```
VIBE_GameLogic_RunFrameLoop (0x4C09A0)
  -> VIBE_Render_RenderMainViewFrame (0x5B6074)   ; gate flags byte_649D71 / dword_64A050
       -> VIBE_Render_ClearViewport (0x5DD464)   or  VIBE_Render_ClearRect (0x434728)
       -> VIBE_Render_RenderUniverseFrame (0x5B3DE8)
            -> VIBE_Render_BeginUniverseFrame (0x5B3900)
                 - clear, set fog range (VIBE_Render_SetFogRange)
                 - VIBE_Floor_RenderTerrain (0x5BF22C)         ; terrain
                 - VIBE_SceneGraph_WalkAndInvoke (0x5AC738)    ; per-object: project + cull + push to draw list
                 - VIBE_Render_ProcessSceneNode (0x5ADD1C)
                 - VIBE_Particle_RenderSystem loop (0x5E1278)
                 - VIBE_Render_UpdateSkyFlares (0x5EF7CC)
                 - VIBE_Mirror_BuildMirroredGeometry (0x5F637C)
            -> VIBE_Render_DrawUniverseAndStats (0x5B3BBC)     ; animation update + fps stats
  -> VIBE_Render_PresentFrame (0x4349E4)          ; blit back buffer to window
```

The scene-graph walk *projects vertices to screen and appends triangles to a global
draw list* (`dword_13FC584` = "d3:PolyList1", `dword_13FC51C` = "d3:PolyList2", both
`8 * dword_13ECE80` bytes, allocated in `VIBE_Render_InitEngineDevice`). The draw list
is radix-sorted (`VIBE_Render_RadixSortDrawList` 0x5AEF34) then consumed by either:
- **Software**: `VIBE_Render_RasterizeMeshList` (0x5AEC88) -> `VIBE_Raster_RasterizeTexturedTriangle` (0x5F7D58) -> `VIBE_Raster_FillTexturedSpansShaded` (0x5F7960).
- **Hardware (D3D)**: `VIBE_Render_DrawTexturedTriangles` (0x5AE434) -> fills a D3D vertex buffer (`VIBE_Render_GetVertexBufferInfo` 0x5DD9C8) and submits via `VIBE_Render_DrawTriangleList` (0x5DD9DC), bracketed by `VIBE_Render_BeginScene`/`EndScene`.

### Hardware vs software selection (VIBE_Render_InitEngineDevice 0x5AF984)
- Config struct at `dword_13ECE68` (96 bytes, memcpy'd from caller). Field `byte_13ECE74` = requested device type (0 = software). If nonzero, `VIBE_Render_SelectDDrawDevice` + `VIBE_Render_EnumDevices` run; success sets `byte_649D7C` = **"D3D hardware enabled"**.
- Regardless of HW, the software pipeline is always initialized (PolyLists, span-filler function-pointer table `dword_13D8780..13D8794` set to `VIBE_Raster_NullStub13`, view transform, light falloff LUT).
- If `byte_649D7C`, also `VIBE_Render_CreateDeviceAndViewport` (0x5DD61C, 8-bit?), `ApplyRenderStates`, `SetZEnable`.
- Config strings: `FULLSCREEN`, `FULLSCREENSOFT` (0x629290), `d3_screen_res_x/y/depth`, `d3s_max_palettes` — registry/INI keys (`VIBE_Render_Load/SaveD3dRegistryConfig`, `Load/SaveD3DSettingsFromRegistry`).

### Present back-ends (VIBE_Render_PresentFrame 0x4349E4) — switch on `byte_762721`
- **case 0**: GDI path — `GetDC` -> `CreateCompatibleDC` -> `SelectObject(h)` -> `BitBlt(...,SRCCOPY=0xCC0020)` from a DIB section (`h` = HBITMAP from `CreateDIBSection`, `ppvBits` = its pixels). This is the windowed/DIB software present.
- **case 1**: DirectDraw `Blt` of primary surface (`dword_62D584`->vtbl+20) with clip rect.
- **case 2/4**: Lock the offscreen DD surface (`VIBE_Render_LockSurface`), `qmemcpy` the software framebuffer (`ppvBits`) into it, Unlock (vtbl+128), then Blt/Flip.
- **case 3**: DirectDraw `Flip` (`dword_62D584`->vtbl+44) — fullscreen page-flip, handles `DDERR_SURFACELOST` (-2005532222 = 0x887601C2) by `Restore` (vtbl+100).

So the software framebuffer is `ppvBits` (0x7626C0); it is either a GDI DIB section (windowed) or copied into a Lock'd DirectDraw back buffer (fullscreen).

---

## 2. Key functions (address — prototype — purpose)

### Display / surface / present (d3_d3d.c, dd_vesa.c)
| Addr | Name | Purpose |
|------|------|---------|
| 0x4337D4 | VIBE_Render_InitDisplayMode | Create DDraw primary+back surfaces, set cooperative level/mode (huge, 0xC0E) |
| 0x43343C | VIBE_Render_SelectDDrawDevice | Pick DDraw device GUID by type |
| 0x4327A0 | VIBE_Render_RegisterVideoMode | Enumerated mode table entry |
| 0x43371C | VIBE_Render_EnumDisplayModes / 0x4336AC callback | DDraw mode enum |
| 0x432260 | VIBE_Render_ConfigureSurfaceCaps | Fill DDSURFACEDESC/DDSCAPS |
| 0x4343E4 | VIBE_Render_LockSurface | `Lock` DD surface (vtbl+100); derives pitch (`>>3`), bpp (`pitch/width`) into 0x7626E8.. |
| 0x4345D4 | VIBE_Render_AcquireBackBuffer | Get back buffer ptr |
| 0x4349E4 | VIBE_Render_PresentFrame | **Present (5 modes)**, see above |
| 0x434728 | VIBE_Render_ClearRect | Clear viewport rect to color |
| 0x42E4EC | VIBE_Render_ReportDDrawError | Giant HRESULT->string table (0x3A2C bytes) |
| 0x435BA8 | VIBE_Render_QueryAvailableVidMem | GetAvailableVidMem |

### Software 2D blit / color / surface (gfx.c, ts_texture.c)
| Addr | Name | Purpose |
|------|------|---------|
| 0x435748 | VIBE_Render_PutPixel | bpp-aware pixel write |
| 0x4351D8 | VIBE_Render_DrawHLine | horizontal span |
| 0x435434 | VIBE_Render_DrawLineClipped | clipped Bresenham |
| 0x4350C4 | VIBE_Render_BlitSurface | surface->surface blit |
| 0x4358D8 | VIBE_Render_ComputeChannelShifts | derive R/G/B mask shifts from pixel format |
| 0x4359B0 | VIBE_Render_PackColorToPixel / 0x434F7C UnpackColor | (un)pack RGB<->native |
| 0x435E00..436228 | VIBE_Render_StretchAverage16/24/32 | box-filter downscale per bpp |
| 0x436504..436F30 | VIBE_Render_StretchInterpolate16/24/32 | bilinear upscale per bpp |
| 0x437530 | VIBE_Render_StretchSurfaceDispatch | pick stretch routine by bpp |
| 0x43768C | VIBE_Render_Convert8To16Indexed / 0x437814 Convert24To16 | format conversion |

### 3D pipeline core (d3_engine.c)
| Addr | Name | Purpose |
|------|------|---------|
| 0x5AF5F8 | VIBE_Render_SetupViewTransform | set view params (eye xy, near/far, fov a5/a6, persp a7), calls SetProjection + BuildViewMatrix |
| 0x5ACCD0 | VIBE_Render_BuildViewMatrix | build world->view matrix |
| 0x5DE3E4 | VIBE_Render_SetProjectionTransform | projection (D3D path) |
| 0x5C5120 | VIBE_Mesh_ProjectVerticesToScreen | per-vertex 1/z perspective divide, screen scale, backface cull (signed area), push polys to draw list (`dword_13FC570/13FC584`) |
| 0x5AD614 | VIBE_Render_ComputeVertexClipFlags | per-vertex frustum outcodes |
| 0x5AD7D8 | VIBE_Render_ClipPolygonToPlane | Sutherland-Hodgman against a plane (`dword_649D74` out count) |
| 0x5AD1F4 | VIBE_Render_ClassifyBoundingBoxPlanes / 0x5AD588 CullNodeAgainstFrustum | frustum cull |
| 0x5ADD1C | VIBE_Render_ProcessSceneNode | recursive node draw |
| 0x5AEF34 | VIBE_Render_RadixSortDrawList | sort triangles (by texture/depth key) |
| 0x5AEC88 | VIBE_Render_RasterizeMeshList | **software** draw-list consumer |
| 0x5AE434 | VIBE_Render_DrawTexturedTriangles | **hardware D3D** draw-list consumer (fills VB, clips, BeginScene/EndScene) |
| 0x5AE2A0 | VIBE_Render_SetFogRange / 0x5AE384 ConfigureFog | fog |
| 0x5E0358 | VIBE_Render_SetBlendMode | translucency/colorkey/additive state |

### Software triangle rasterizer (the heart — d3_engine.c)
| Addr | Name | Purpose |
|------|------|---------|
| 0x5F7D58 | VIBE_Raster_RasterizeTexturedTriangle | sort verts by Y, set up edges, affine tex |
| 0x5F7960 | VIBE_Raster_FillTexturedSpansShaded | scanline span loop, **16.16 fixed-point** u/v/z/light interpolation, writes pixels + a parallel 24-byte/pixel shading buffer (`dword_1408AA4`) |
| 0x5F7840 | VIBE_Raster_InterpolateEdgeZTex / 0x5F6A8C EdgeZ / 0x5F6930 EdgeRgbz | edge interpolators |
| 0x5F71AD..5F740A | VIBE_Raster_FillSpanTextured[/Masked/Blend/Or] | 6 inner span variants (opaque/colorkey/alpha/OR raster ops) |
| 0x5F7500..5F763B | VIBE_Raster_PatchSpanConstants* | **self-modifying code**: patch immediates into the span loops |
| 0x5F76E2 | VIBE_Raster_BilinearBlendBlockMmx | **MMX** bilinear filter block |
| 0x603D00 | VIBE_Raster_ComputeEdgeSlope / 0x603DA8 FillSpans / 0x603ED4 RasterizeTriangle | flat/shadow triangle raster |
| 0x5F6C30 | VIBE_Raster_RasterizeMirrorTriangle | mirror-clipped raster |

### Mesh / model / animation
| Addr | Name | Purpose |
|------|------|---------|
| 0x5D2348 | VIBE_Mesh_LoadBgfFile | **load .BGF mesh** (0xF51 bytes — main model format) |
| 0x5F87B8 / 0x5F9558 | VIBE_Model_LoadFastChunk / FastChunkIo | fast binary model chunk IO |
| 0x5C953C | VIBE_Mesh_InterpolateMorphVertices | morph-target vertex blend |
| 0x5C9054 | VIBE_Mesh_ComputeVertexLighting | per-vertex lighting |
| 0x5C9C58 | VIBE_Mesh_TransformVertexNormals / 0x5C9D04 TransformPackedVertices | vertex transform |
| 0x5CD1D8 | VIBE_Anim_UpdateSkeletonPose | **skeletal pose update** (0x1A25 bytes, biggest anim fn) |
| 0x5CC0D0 | VIBE_Anim_ComputeBoneMatrices | bone matrix palette |
| 0x5CBC10 | VIBE_Anim_InterpolateBoneFrame | keyframe lerp |
| 0x5E450C | VIBE_ModelIo_LoadBinaryAnimation | load anim file |
| 0x5C8990.. | VIBE_Transform_* | bone-chain point/vector transforms |

### Texture / surface cache
| Addr | Name | Purpose |
|------|------|---------|
| 0x5DA714 | VIBE_Texture_LoadByName | main texture loader |
| 0x5DB234 | VIBE_Texture_UploadToSurface | upload to DD surface |
| 0x5BA1E8 | VIBE_TextureCache_GetOrBuildTile / 0x5BA0B0 LookupTile / 0x5BA03C FindLruSlot | LRU tile cache |
| 0x5B903C | VIBE_TextureCache_BuildMipmap / 0x5B8D18 ScaleBlitMip | mipmaps |
| 0x5DEA50 | VIBE_Render_LoadAndStretchTexture | load+rescale (0x1185 bytes) |

### Terrain / floor / sky / weather / particles / shadows
| Addr | Name | Purpose |
|------|------|---------|
| 0x5BF22C | VIBE_Floor_RenderTerrain | terrain render (0x386D bytes — largest fn in cluster) |
| 0x5C5610 | VIBE_Heightmap_BuildTerrainMesh / 0x5C47DC BuildLitTileGeometry | heightmap meshing |
| 0x5EF980 | VIBE_Sky_BuildDomeMesh ; 0x5B85E4 SkyColor_BlendBandLighting | sky dome + gradient |
| 0x4C0040 | VIBE_Weather_UpdateSky ; 0x42A014 Snow_Create ; 0x429098 Rain_Create | weather |
| 0x5E1278 | VIBE_Particle_RenderSystem ; 0x42B930 Particle_UpdateEmitter | particles |
| 0x5F363C | VIBE_Shadow_RenderMeshShadow ; 0x5F3F98 CastFromLight ; 0x603ED4 Shadow_RasterizeTriangle | projected shadows |
| 0x5C6F90 | VIBE_Light_ApplyToCachedVertices ; 0x5C7804 IlluminateObject | vertex lighting cache |

### Shape/sprite 2D (HiColor sprite bank — ts_texture / d3_interface)
| Addr | Name | Purpose |
|------|------|---------|
| 0x5D6160/5D5908/5D4C20 | VIBE_Shape_GrabBit8/16/24 | grab sprite from surface at depth |
| 0x5D6A08 | VIBE_Shape_BlitRleScaled ; 0x5D6D74 BlitRleLightTable | RLE sprite blit |
| 0x5D80A8 | VIBE_ShapeBank_ConvertNew ; 0x5D8330 AddShape | sprite bank |
| 0x5D9DB8 | VIBE_HiColTab_AddEntry ; 0x5DA04C FindOrBuild | 16-bit color remap tables |
| 0x603180.. | VIBE_Quant_* | octree color quantization (8-bit palette gen) |

---

## 3. Data structures (inferred from offsets — no named UDTs in IDB)

The IDB has **no struct definitions**; everything is `*(type*)(base + off)`. Inferred layouts:

### Object / scene node (base = `dword_649D68` root; nodes are large, ~ stride 246*4 in tables)
- `+460` -> pointer to **mesh/geometry block**:
  - `[+4]` poly array base, `[+8]` poly count, `[+12]` poly capacity
- `+492` -> **draw-data block**: `+244` mesh sub-block, `+252`/`+256` per-frame poly counters, `+2316` flag
- `+529` flags byte (0x20 = ?), `+530` render flags (0x10 mirror/0x20/0x40 = cull/double-sided/force), `+532` type

### Polygon record (40 bytes; in mesh poly array, iterated `v19 += 40`)
- `+0` vtxptr0, `+4` vtxptr1, `+8` vtxptr2 (pointers to vertex records)
- `+24/+28/+32` float UV/offset, `+36` flags byte (bit7 = backface, 0x40/0xC0 = culled), `+38` flags (bit1, bit2, bit4)

### Vertex record (80-byte stride; iterated `j += 80` in ProjectVerticesToScreen)
- `+0/+4` model-space x,y ; `+8` z/depth ; `+16,+20` **projected screen x,y** (float) ; `+32,+36` UV
- `+64/+76` color/light components ; `+66` byte = **light/shade table index** (`768 * idx` light-table offset)
- pre-projection `+16` = `1/z` reciprocal stored

### Draw-list entry (8 bytes each in PolyList; `dword_13FC584`, `dword_13FC770` = count)
- `[+0]` sort key (`768 * maxLightIdx`), `[+4]` -> poly record pointer

### D3D vertex buffer layout (in DrawTexturedTriangles): **24 floats per triangle** = 8 floats/vertex
- per-vertex: x,y (screen), z*invdepth, rhw(1/z), diffuse-ish color words (`+64`,`+76`), u,v. Matches a DX6 TLVERTEX-style format.

### Key globals (framebuffer / pitch / format — at 0x7626xx, the "dd_vesa" present state)
- `ppvBits` (0x7626C0) = software framebuffer / DIB pixel pointer
- `h` (0x7626BC) = HBITMAP of DIB section ; `dword_7626D4` = HWND
- `dword_7626E0`=width, `cy`(0x7626DC)=height, `dword_7626E8`=width(px), `dword_7626B8`=bytes/row to copy
- `dword_7626EC/F4/F8/FC/762714` = surface desc fields (pitch, bpp, lpSurface) from Lock
- `byte_762721` = present-mode selector (0..4)
- `dword_62D584` = primary DDSurface, `dword_62D57C` = back DDSurface, `dword_62D578` = DDraw obj, `dword_64A320` = D3D device
- `byte_649D7C` = HW-D3D-enabled ; `byte_649D71` = engine-on ; `dword_64A050` = render-reentrancy guard

### Engine/view state block (0x13ECE68.. and 0x13FC4xx..0x13FCFxx)
- `dword_13ECE68/6C` = render width/height ; `dword_13ECE80` = max polys ; `byte_13ECE74` = device type
- `flt_13ECEAC/B0/B4/B8` = viewport x/y/w/h ; `flt_13ECEBC`+`qword_13ECEC0` = fov/persp params
- `dword_13ECE58/5C/60/64` = current viewport rect (int) ; `dword_13FCD1C` = active camera/world ptr
- Matrix/transform scalars: `flt_13FCD0C/D10/D18` (projection terms), `flt_13FCAF8/AFC`, `flt_13FC774`(1/fov), `dword_13FC5C0`
- **Fixed-point raster accumulators** (16.16): `dword_13FC5D8`(x_left), `13FC5BC`(x_right), `13FC5F4`(u/v), `13FC590`(grad), step deltas `13FC5E8/5C8/5C4`, `13FC5DC`(pixel ptr), `1408AA4`(24bpp shade-buffer ptr), `1408AA0`(blur radius)
- Light shade table: 768-byte stride per index (RGB per palette entry × 256)

---

## 4. External dependency map
| API | Used by |
|-----|---------|
| `DirectDrawCreate` | InitDisplayMode (0x4337D4), SelectDDrawDevice (0x43343C) |
| `DirectDrawEnumerateExA` | device/mode enumeration |
| DDraw surface vtables (Lock/Unlock/Blt/Flip/Restore/GetAvailableVidMem) | LockSurface (0x4343E4), PresentFrame (0x4349E4), QueryAvailableVidMem |
| D3D device vtables (BeginScene/EndScene/DrawPrimitive/SetRenderState/SetTexture) | DrawTexturedTriangles (0x5AE434), ApplyRenderStates (0x5DDA1C), CreateDeviceAndViewport (0x5DD61C), EnumDevices/EnumTextureFormats/EnumZBufferFormats |
| `BitBlt`/`CreateCompatibleDC`/`CreateDIBSection`/`SelectObject`/`DeleteDC` | PresentFrame case 0 (GDI present), CaptureScreenshot (0x4FF6D4) |
| `TextOutA`/`SetTextColor`/`SetBkMode`/`GetStockObject` | DrawDebugOverlay (0x5B6128) GDI text |
| `DirectInputCreateA` | input (out of cluster) |

---

## 5. Effort & risk per module (S/M/L)
| Module | Effort | Risk notes |
|--------|--------|-----------|
| Coord/Vector/Transform math | S | `VIBE_Coord_ConvertX/ConvertY` are FPU helpers used everywhere; small but pervasive. |
| Render (matrix/view/proj) | M | hand-rolled view/projection; FPU `st` stack ordering matters for bit-exact. |
| Surface / present (dd_vesa, gfx) | M | 5 present paths; DDraw vtable dispatch must be reproduced or abstracted. |
| Software rasterizer (Raster_*) | **L** | **Self-modifying span code** (PatchSpanConstants*), **MMX** bilinear block, **16.16 fixed-point**, `__ROR4__` texel addressing. Highest fidelity risk. |
| StretchAverage/Interpolate, Convert* | M | per-bpp scalar loops, qmemcpy-with-alignment idioms. |
| Mesh / Model IO (BGF, FastChunk) | L | large binary loaders (LoadBgfFile 0xF51, FastChunkIo 0x1011); exact file format must be reversed. |
| Anim / skeleton | L | UpdateSkeletonPose 0x1A25, ComputeBoneMatrices 0x780 — matrix/quat heavy FPU. |
| Texture cache | M | LRU + mipmap + palette; straightforward logic. |
| Floor/Terrain/Heightmap | L | RenderTerrain 0x386D is the single largest fn; lots of tile/LOD state. |
| Shadow | M | projection + dedicated triangle raster. |
| Particle/Snow/Rain/Sky/Weather | M | many small update/spawn fns, RNG-seeded (RandNext) — match RNG for determinism. |
| Shape/sprite bank + HiColTab + Quant | M | RLE codecs, octree quantizer; self-contained. |
| Light | M | vertex-light cache, falloff LUT. |
| Mirror | M | clip-plane reflection geometry. |
| Pick / Collision | S/M | ray vs AABB/poly. |

**Cross-cutting risks**: (1) FPU x87 stack semantics for bit-identical floats; (2) self-modifying rasterizer; (3) MMX; (4) heavy reliance on raw offset access (no structs) — must reconstruct ~6 core structs precisely; (5) DDraw/D3D6 vtable indices must be mapped to the real interfaces.

---

## 6. Proposed C++ file / namespace layout (`namespace guild::render`)
```
render/
  math/        Coord.cpp Vector.cpp Transform.cpp Matrix.cpp   (FixedPoint.h, Fpu.h)
  surface/     Surface.cpp Blit.cpp Stretch.cpp Convert.cpp ColorFormat.cpp
  present/     DisplayMode.cpp PresentGdi.cpp PresentDDraw.cpp DDrawDevice.cpp
  raster/      RasterTriangle.cpp SpanFill.cpp EdgeInterp.cpp RasterMmx.cpp ShadowRaster.cpp
  texture/     Texture.cpp TextureCache.cpp Mipmap.cpp HiColTab.cpp Quant.cpp
  mesh/        Mesh.cpp Bgf.cpp ModelIo.cpp MeshBounds.cpp MeshLighting.cpp
  anim/        Skeleton.cpp BoneMatrices.cpp Morph.cpp AnimIo.cpp AnimState.cpp
  scene/       Scene.cpp SceneGraph.cpp Octree.cpp Frustum.cpp Pick.cpp
  engine/      Engine.cpp ViewTransform.cpp DrawList.cpp DrawSoftware.cpp DrawD3D.cpp Fog.cpp BlendMode.cpp
  terrain/     Floor.cpp Heightmap.cpp FloorWater.cpp
  effects/     Particle.cpp Snow.cpp Rain.cpp Sky.cpp SkyColor.cpp Weather.cpp Shadow.cpp Light.cpp Mirror.cpp Fade.cpp DayCycle.cpp
  sprite/      Shape.cpp ShapeBank.cpp ShapeAnim.cpp Paintbox.cpp Picture.cpp Bmp.cpp Font.cpp
```
Shared structs in `render/Types.h`: `Vertex` (80B), `Polygon` (40B), `DrawListEntry` (8B), `MeshBlock`, `SceneObject`, `Surface`, `EngineState`, `ViewState`.

---

## 7. Suggested implementation order
1. **math/coord** — Vector/Coord/Transform + FPU/fixed-point helpers (`VIBE_Coord_ConvertX/Y`, `VIBE_Math_*`). Everything depends on these; verify bit-exactness early.
2. **surface + color format** — Surface alloc/blit/stretch/convert + channel-shift/pack-color. Standalone, testable against bitmaps.
3. **present layer** — DIB/GDI present first (case 0), DDraw later. Gets pixels on screen for visual diffing.
4. **raster core** — edge interpolators -> span fillers -> textured triangle -> flat/shadow triangle. Validate per-pixel vs reference. (Hardest; do span variants incrementally.)
5. **texture + cache + HiColTab/Quant** — feed the rasterizer real textures.
6. **mesh + BGF/ModelIo loaders** — reverse the file formats; load static geometry.
7. **engine view/projection + draw list + software draw path** — wire ProjectVerticesToScreen -> cull/clip -> sort -> RasterizeMeshList. First full 3D frame.
8. **scene graph + frustum/octree + pick** — object management.
9. **anim/skeleton/morph** — animated meshes.
10. **terrain (Floor/Heightmap)** — large but self-contained on top of raster.
11. **lighting + shadow + mirror** — vertex light cache, projected shadows.
12. **effects** — particles, snow/rain, sky, weather, day cycle, fade.
13. **sprite/2D** — Shape bank, Paintbox, Picture/Bmp, Font (UI layer).
14. **D3D hardware path (optional)** — DrawTexturedTriangles + device/render-state, last (can stub to software initially).
```
```

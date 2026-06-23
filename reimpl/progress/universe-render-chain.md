# Universe render chain — model-load → scene-node → per-frame render

The full chain that turns a model name into rendered geometry, reconstructed 1:1 and
verified live on real game assets:

```
CreateMenuDummyActor / CreateMesh (sim/character_factory, sim/menu_actor)
  -> Object_AttachToUniverseNode 0x5b3e30 (sim/object_lifecycle10)
       -> Object_Spawn 0x5b054c (sim/object_lifecycle4) + InitStruct 0x5b0e88
       -> Mesh_LoadOrFindByName 0x5d345c (render/mesh_lod_name + mesh_asset)
            -> BuildLodFileName 0x5d15fc / BuildTexturePath 0x5d1034
            -> LoadAndRegister 0x5d32d4 + FindStockObject 0x5d10d0 (render/mesh_stock_object)
                 -> Mesh_LoadByName 0x5d2348 (the real .bgf LoadFastChunk parser)
       -> AttachStockObjectLods 0x5d1824 (render/mesh_lod_name)
       -> AttachStockTextures 0x5d1114 (render/mesh_attach_textures) + AllocPolysAndPoints 0x5b0c10
       -> SetPosition 0x5af38c / SetWorldTranslation 0x5af50c / LinkIntoScene 0x5b0a20
  -> RenderUniverseFrame 0x5b3de8 (render/frame): BeginUniverseFrame 0x5b3900 +
       DrawUniverseAndStats 0x5b3bbc
       -> Floor_RenderTerrain, ResetLightList 0x5f4428, SceneGraph_WalkAndInvoke 0x5ac738
          (+ TestNodeFlag 0x5ac6d8) -> ProcessSceneNodeAppend 0x5add1c (draw-list build)
       -> RadixSortDrawList 0x5aef34 -> RasterizeMeshList 0x5aec88 / RasterizeDrawList
          (DrawTexturedTriangles 0x5ae434 is the D3D layer SWAPPED for Vulkan/CPU, rule 3)
       -> per-universe object walk -> UpdateSkeletonPose 0x5cd1d8 (render/skeleton_pose_driver)
```

Every gateway is a genuine 1:1 reconstruction; `object_attach_chain_e2e_test` drives the
real `Objects.BIN` member `BUCH` (316 verts / 604 polys / 12 mats) through the whole live
chain (spawn → mesh load → stock register → LOD attach → texture binding → place → link).

## 2026-06-09 — VIBE_Shadow_ResetLightList @0x5f4428 + VIBE_Render_PushToDrawList @0x5f43f4

The per-frame shadow-light collector `BeginUniverseFrame` runs (`render/frame.cpp` calls it
through the `resetLights` FrameHook, previously inert). `ResetLightList` zeroes the count and
walks the scene graph with mask 16 (`TestNodeFlag` 8→0x10, i.e. type-8 light nodes) invoking
`PushToDrawList`; that callback appends a node only when its `+529` flags byte has bit2 (the
"active shadow-caster" flag `CreateMesh @0x4029c4` sets), caps the list at **four**
(`dword_1408A0C`), and returns `count < 4` so the walk STOPS once four lights are gathered.

- `render/shadow_light_list.{h,cpp}` — `ShadowPushLight` (the collector), `ShadowResetLightList`
  (reuses the reconstructed `WalkAndInvoke`), and the `ShadowResetLightListActive()` void thunk
  bound to the `resetLights` FrameHook in `wire_atmos_bridge` (a null active universe is a safe
  no-op until `SetActiveShadowUniverse`). Added the `+529` flags byte to `SceneNode` (the
  `+0x211` byte; bit2 = active shadow-caster). Test: `shadow_light_list_test` (4 vectors, 22
  checks): the collector's filter/cap/early-stop, a real scene-walk collecting 4-of-5 casters
  in order, null-root empty, the void-thunk active binding. Both presets 1189/1189.

This completes the BeginUniverseFrame leaf set: the universe render chain is now reconstructed
end-to-end (the remaining hooked leaves — GPU `Texture_UploadToSurface` and the make-current
`InvalidateCurrent @0x5af2e4` sky-dome/current-cam draw-block reset — need the running
Vulkan/universe runtime, named per rule 8).

`SelectLodFrame @0x5adb6c` is now reconstructed AND wired (2026-06-10): the clean math lives in
`node_lod.cpp` (`render::SelectLodFrame`) and the new `render::SelectLodFrameForNode` raw-block
adapter installs it as `MeshAttachHooks::selectLodFrame` (the `AttachStockTextures` step-6 frame
pick). Only the live LOD-distance globals (current camera / fov scale / force-rebuild) are
runtime state — supplied via `SetLodSelectView`, defaulting to the no-camera forced-LOD branch
(the original's `dword_13FCD1C == 0` path). The step-6 scan fallback's frame-validity offsets
were also corrected to the 64-bit-relocated draw-block layout. See `mesh-lod-stock-object.md`.

## 2026-06-10 — LIVE LOOP STOOD UP: real .bgf through the engine frame spine

`src/play/universe_render.{h,cpp}` (`play::UniverseFrameDriver`) drives a REAL `Objects.BIN`
mesh through the genuine engine per-frame spine to pixels — the "stand up the live loop"
milestone. Until now `RealFrameScene` (app/wiring.cpp) drove `render::RenderMainViewFrame` but
over a SYNTHETIC quad/hex-fan; this drives the spine over actual decoded game geometry:

```
render::RenderMainViewFrame 0x5B6074            (gate + clear)
  -> render::RenderUniverseFrame 0x5B3DE8
     -> render::BeginUniverseFrame 0x5B3900
          clearRect hook  -> SurfaceColorFill (sky)
          sceneWalk hook  -> render::ProjectVerticesToScreen 0x5C5120 (verts->screen, backface)
                           + render::ProcessSceneNodeAppend 0x5ADD1C  (REAL per-node dispatch,
                             driven by play::WalkSceneTree + InstallRealSceneBridge)
                           + render::RadixSortDrawList 0x5AEF34
  -- then caller-driven (the original's separate present path, like
     RealSubsystems::renderMainViewFrame) --
render::RasterizeMeshList 0x5AEC88 -> software render::Surface
```

The geometry is the REAL shipped `.bgf` decoded by `play::RealMeshSource` (the same decode
`real_city_render` uses) — no synthetic stand-in (rule 8). The `render::FrameHooks` are plain C
function pointers (engine register callbacks), so the driver threads itself through a
process-global active pointer + free trampolines (the `RealFrameScene` pattern). Additive
module: no owned-file edits, only public sibling APIs.

E2E `tests/e2e/universe_render_e2e_test.cpp` (`UniverseRenderE2E.RealMeshThroughEngineFrameSpine`,
guarded on `Resources/Objects.BIN`, honors `GUILD_GAME_DIR`): mounts the real archive, scans
members for the first that decodes to real multi-tri geometry, drives it through the spine, and
asserts real geometry was projected + dispatched + rasterized + visible, then dumps a BMP.

### 2026-06-10 (cont.) — TEXTURED raster + ON-SCREEN present completed

`UniverseFrameDriver` now renders the real mesh **textured** through the genuine 1:1 textured
triangle leaf and presents it to a graphics device (the rule 3/4 present swap):

- **Textured raster (1:1).** `render::Polygon` gained a `matIndex` (carried from `BgfPolygon`'s
  +0x28 material index in `BuildGeometry`; default -1). The driver mounts `Resources/Textures.BIN`
  (`RealTextureSource`), builds the per-material `MaterialTextureTable` for the member's `BgfModel`,
  and for each resolved material builds a `render::Texture` (`TextureSetSize` + the `DecodedBmp`
  8-bit indices) + a 565 palette LUT (from the BMP's source palette). It installs a custom
  `render::SpanDispatch` whose textured slots (3 + 4) call the REAL affine textured-triangle leaf
  `render::RasterizeTexturedTriangleRgbz` (gilde.exe 0x5F6C30) — sampling `texels[(V<<shift)+U &
  texelMask] -> palette[idx]` exactly as the engine. Untextured polys fall back to the flat/shaded
  `render::RasterizeTexturedTriangle` (the +66 light index), the engine's pre-texture state. The
  affine textured path interpolates U/V only (no per-vertex shade) — 1:1; per-vertex SCENE lighting
  of untextured polys (the +66 fill beyond Y-depth) is the one remaining refinement, named.

- **On-screen present (rule 3/4).** `UniverseFrameDriver::PresentToDevice(IGraphicsDevice&)` blits
  the rendered `render::Surface` into the device backbuffer (16/24/32 bpp) and calls `present()`.
  Headless via `MemoryGraphicsDevice`; the IDENTICAL call drives a real SDL window via
  `VulkanGraphicsDevice` + `SdlVulkanPlatform` (the `apps/guild_run.cpp --play` swapchain path,
  already proven). The Vulkan present (`vkCmdBlitImage` -> `vkQueuePresentKHR`) was already complete.

Verified live (member `_DYNAMIC/Buch/Buch.bgf`, 368v/604p, 160x120): **216 tris rasterized, 5
materials bound, 58 polys sampled through `RasterizeTexturedTriangleRgbz`, 191 distinct texel
colours (vs flat green before), 11618 visible px**, then **presented to the device (19158 non-zero
backbuffer px)**. BMP `/tmp/guild_universe_frame.bmp`.

### 2026-06-10 (cont.) — real view transform (isometric 3D)

The engine's `ProcessSceneNode @0x5add1c` per-object pipeline is: `SelectLodFrame` (wired) → cull →
the VERTEX TRANSFORM `VIBE_Mesh_InterpolateMorphVertices @0x5c953c` (applies the object's +72
world/view matrix) → `ComputeVertexClipFlags @0x5ad614` → append. The engine projection itself
(`ProjectVerticesToScreen @0x5c5120`) is AFFINE/isometric (model x→screenX, z→screenY, y→light) —
Die Gilde's fixed top-down camera — so the perspective/orientation comes from that pre-projection
matrix, not the projection. The driver now applies that orientation through the REAL transform
leaf `render::MatrixFromEuler @0x5cb1bc` + `render::Apply` (scene_transform.cpp): each frame it
rotates the source mesh about its centre by `R = MatrixFromEuler(viewEuler)` into a private copy
(`view = R*(v-centre)`), then projects the copy — the cached source mesh stays pristine. The
default `viewEuler` is the game's isometric tilt, so a model renders in recognizable 3D instead of
flat top-down. Re-verified live: Buch now **257 tris, 96 textured polys, 261 distinct colours** in
an isometric view; presented (19169 bb px). The `viewEuler` is a camera PARAMETER (as the city
screen feeds the MegaCam euler) — the rotation MATH is the 1:1 reconstruction.

### 2026-06-10 (cont.) — object-light shade kernels (BuildObjectCache finalize)

`render/object_light_shade.{h,cpp}` reconstructs the self-contained per-vertex SHADE math of
`VIBE_Light_BuildObjectCache @0x5c8218` + `VIBE_Light_ApplyVertexShading @0x5c7f04`, 1:1 with the
exact constants recovered via `get_bytes`:
- `FinalizeVertexShadeSoftware` — the software branch: normalise so max(R,G,B) ≤ 255 (scale by
  255/max on overflow), truncate to B/G/R bytes (vertex +68/+69/+70).
- `FinalizeVertexShadeLuma` — the hardware branch: `luma = G*0.59 + R*0.30 + B*0.11`, clamp 255.
- `ApplyMaterialVertexShade` — `(R*0.30 + G*0.59 + B*0.11 + texHi) * (1/256) * texLo`, clamp 255.
Constants: ambient seed **200.0** (`flt_64A074/78/7C`), luma **0.30/0.59/0.11** (`flt_628C88/8C/90`
= `flt_628C64/68/6C`), cap **255.0** (`flt_628C94/74`), material scale **1/256** (`flt_628C70`).
The golden values are the exact float32 results (the verbatim kernel rounds `0.11f*200` UP to 22,
etc.). Test `object_light_shade_test` (4 vectors, 18 checks).

WIRED (rule 13): a UNIVERSE object's per-vertex shade comes from the light cache, NOT the character
Y-depth term `ProjectVerticesToScreen` writes; the driver now overwrites each projected vertex's
`+66` lightIdx with the BuildObjectCache ambient shade (`FinalizeVertexShadeLuma(200,200,200)` =
200), the engine's correct no-scene-light object shade. Untextured fallback polys now render at
ambient instead of the (wrong-path) Y-depth gradient.

Per-light DIFFUSE kernel: `render::PointLightDiffuse` reconstructs the per-vertex inner loop of
`VIBE_Light_IlluminateObject @0x5c7804` 1:1 — `d = vpos-lpos`, range gate (`dist2 >= range2`),
normalize, `NdotL = normal·d̂` backface gate (`NdotL >= 0`), ramp index `(int)(NdotL * -1023.0)`
(`flt_628C50`), `atten = 1/(falloff·dist2)·intensityScaled`, `rgb = lcolor · atten · ramp[idx]`.
Constants `flt_628C4C = 10.0` (intensity), `flt_628C50 = -1023.0` (ramp index). The shade-ramp LUT
is threaded as a parameter so the kernel is golden-tested with a synthetic ramp (range/backface/
NdotL gates + the zero-ramp → 0 case). Test `object_light_shade_test` now 5 vectors / 30 checks.

### 2026-06-10 (cont.) — static vertex-transform walk (InterpolateMorphVertices)

`render/mesh_transform_walk.{h,cpp}` (`TransformMeshVerticesByMatrix`) reconstructs the STATIC
(non-morph) object-vertex transform WALK of `VIBE_Mesh_InterpolateMorphVertices @0x5c953c` — the
per-object transform `ProcessSceneNode @0x5add1c` calls (a3 = the object's `drawData+72` 16-float
column-major world matrix). For each vertex it applies the already-reconstructed per-vertex kernel
`render::TransformPointByWorldMatrix` (`out.x = x*m[0]+y*m[4]+z*m[8]+m[12]`, …) in place, and on the
`a1+529 & 0x20` path accumulates the running near/far z bounds (`flt_13FD168[0]`/`flt_13FCF3C`).
The morph-active (`+380`) keyframe-blend walk and the `ComputeVertexLighting` tail are the named
boundary (rule 8). Golden `mesh_transform_walk_test` (4 vectors, 9 checks): identity, translation,
the column-major rotation map, depth bounds.

WIRED (rule 13): the universe driver's view transform now goes through this real walk — it builds
the object's column-major world matrix (`R = MatrixFromEuler(viewEuler)` about the mesh centre,
translation `-R*centre`) and runs `TransformMeshVerticesByMatrix`, replacing the ad-hoc rotation
loop with the exact engine transform `ProcessSceneNode` uses. Re-verified live: Buch 255 tris, 94
textured polys, presented — visually identical (the matrix is behaviour-equivalent), now faithful.

### 2026-06-10 (cont.) — env-map reflection-UV walk (ComputeVertexLighting tail)

`render/env_map_walk.{h,cpp}` (`ComputeEnvMapVertexUvs`) reconstructs the OBJECT WALK of
`VIBE_Mesh_ComputeVertexLighting @0x5c9054` — the env-map reflection-UV pass `InterpolateMorphVertices`
tail-calls for every drawn object. Per vertex (gated by the `+77` reflective byte) it sources the
normal — SKINNED from the active keyframe's `+184` skin-normal bytes via `render::UnpackSkinNormal`,
or NON-SKINNED from the per-vertex source normal — and writes
`render::ComputeEnvMapReflectionUv(pos, normal, m3x3)` to the vertex u/v (`+32/+36`). Both per-vertex
kernels were already reconstructed (`vertex_lighting.cpp`); this is the loop + the normal-source
selection + the early-outs. Golden `env_map_walk_test` (4 vectors, 14 checks): non-skinned vs the
kernel oracle, the `+77` gate, the skinned byte-unpack source, and the null-input early-outs.

WALK INPUTS / boundary (rule 8): the bone 3x3 (`ComputeBoneWorldMatrix @0x5c8fac`, reconstructed),
the active-layer skin keyframe (`FindHighestPriorityLayer @0x5d0e84`, reconstructed), the per-vertex
source normals, and the reflective-material gate (`texRec +104 & 1`) are the caller-supplied inputs —
the engine runs this walk ONLY for a reflective object, so it stays caller-gated (the universe driver's
Buch is non-reflective → not invoked, exactly as the engine would not). This + `mesh_transform_walk`
complete the static branches of the `InterpolateMorphVertices → ComputeVertexLighting` pair.

### 2026-06-10 (cont.) — morph-keyframe blend walk (animated characters)

`render/morph_blend_walk.{h,cpp}` (`AccumulateMorphBlend`) reconstructs the MORPH-ACTIVE branch of
`VIBE_Mesh_InterpolateMorphVertices @0x5c953c` (the `a2+380 != 0` path) — the per-frame vertex
deformation for animated character meshes. It walks up to three morph layers (records at a2+28,
116-stride); for each layer with a clip + non-zero weight it blends the two keyframes' quantized
point bytes (keyframe `clip+348 + 192*frame`; scale `+20/+24/+28`, bias `+8/+12/+16`, points `+180`)
via the reconstructed kernel `render::InterpolateMorphVertex` (run through identity world for the
blend-only result), scaling each keyframe's scale/bias by the LAYER weight; the FIRST active layer
SETS each vertex position, the rest ACCUMULATE. The caller then world-transforms
(`TransformMeshVerticesByMatrix`) and runs the env-map walk — the static tail of the same function.
The layer/keyframe RECORD parsing and `ComputeMorphWeights @0x5c9394` (reconstructed in
`agf_anim.cpp`) are the caller-supplied inputs (named boundary, rule 8) — the walk takes parsed
`MorphLayer`s so it is golden-testable. `morph_blend_walk_test` (4 vectors, 9 checks): single-layer
set, bias+layer-weight scaling, multi-layer accumulate, the clip/weight gate.

Both branches of `InterpolateMorphVertices` (static transform + morph blend) and its
`ComputeVertexLighting` tail (env-map) are now reconstructed over the existing per-vertex kernels.

### 2026-06-10 (cont.) — the universe-object PERSPECTIVE projection (0x5ac970)

`render/object_project.{h,cpp}` (`ProjectObjectVertices`) reconstructs `VIBE_Render_ProjectObjectVertices
@0x5ac970` (IDA-mislabeled "UpdateBillboards") — the per-object projection + backface cull
`ProcessSceneNode @0x5add1c` runs after the view transform + clip-flag pass. It is a TRUE PERSPECTIVE
divide: for each `+76 & 0x80` (visible) vertex, `invZ = 1/z` (view z); `screenX = xScale*x*invZ +
xOffset` (`flt_13FCD0C/18`); `screenY = yScale*y*invZ + yOffset` (`flt_13FCAF8/10`) → `+16/+20`. Then
per-poly backface cull by the PROJECTED signed area toggles `+36` bit7 (with the `+38&4` no-cull and
`+36&0x10` special cases). Golden `object_project_test` (3 vectors, 10 checks): the perspective
divide, the `+76` visible gate (+ projectAll), and the signed-area cull (front/back/no-cull).

FINDING: this is the GENUINE universe-object projection — a perspective divide — distinct from the
affine model→screen map of `VIBE_Mesh_ProjectVerticesToScreen @0x5c5120` that the `UniverseFrameDriver`
demo uses. The four view scalars (`flt_13FCD0C/18` `flt_13FCAF8/10`) are per-frame VIEWPORT/camera
state the engine derives from the active viewport; the driver has no live viewport (it synthesises a
fit), so — like the camera and the shade-ramp LUT — those runtime scalars are the named boundary
(rule 8). The projection + cull MATH is reconstructed and golden-tested, ready for the live viewport.
The fog/distance shade byte (`+79`, `byte_649DD8` over `flt_13FC544/5AC/58C`+`dbl_628074`) is the
other named sub-boundary.

WIRED (rule 13): the `UniverseFrameDriver` now has a `perspective` option that routes through the
REAL `ProjectObjectVertices` leaf instead of the affine stand-in — it pushes the centred mesh to
positive view-z (a synthesized camera distance), marks vertices visible (`+76|0x80`) + polys
front-candidate (`+36|0x80`), synthesizes the viewport scalars from the FB + the mesh fit, and runs
the perspective divide + backface cull; the rest of the chain (append → textured raster → present)
is unchanged. `universe_render_e2e` now renders the SAME real `Buch.bgf` through BOTH projections:
affine (parallelogram) AND perspective (`/tmp/guild_universe_persp.bmp`: 248 tris, 104 textured, 257
colours, 7748 px — visible 1/z foreshortening). The synthesized viewport (camDist + scalars) is the
demo's stand-in for the engine's per-frame viewport state (the same legitimacy as the demo camera
euler); the projection leaf itself is the verbatim 1:1 reconstruction. Default stays affine so the
existing assertions are undisturbed; the perspective pass adds 4 checks. Both presets green.

LUT FINDING (rule 8). I traced the shade-ramp LUT `flt_1405110`: its FIVE xref sites
(`ApplyToCachedVertices ×3`, `IlluminateObject`, `BuildTilePolys`) are all READS; there is no
writer among them, no mid-LUT-range xref, and `get_bytes` shows the whole table ZERO (an
uninitialized `.bss`). So in the analyzed image the per-vertex dynamic diffuse term evaluates to
**zero** and object shading is **ambient-dominant** — exactly what the universe driver renders
(ambient 200 + textures). The accumulation walk (`ApplyToCachedVertices`, the live light list) and
the ramp BUILDER (if one exists on a night/torch path) are the named boundary; the per-light
diffuse + finalize MATH is reconstructed and tested now, ready for the ramp once recovered.

### Live scene-graph splice trio — now REAL (src/render/scene_link.{h,cpp})

The object-table scene-list splice (previously a hooked GAP) is reconstructed 1:1:
`VIBE_Object_LinkIntoScene @0x5b0a20` (`render::LinkIntoScene`) + the LiveScene-threaded
`VIBE_Object_SetParent @0x5b0b9c` (`render::SetParent`), reusing the already-reconstructed
`render::LinkAsSibling @0x5b0b3c` / `render::SetParent` (scene_walk.cpp) for the sibling-chain
append. `LiveScene` threads the four engine globals (`dword_13FD140` scene head / `unk_13FCF4C`
sentinel / `dword_13FCD1C` current cam / `off_649D64` universe). The boot seed
(`InitEngineDevice @0x5af984`: `InitStruct(sentinel)` -> +528=0x65 bit0-terminator, +533=1;
`dword_13FD140 = dword_13FCF10 = sentinel`) is reproduced, so the scene head STARTS as the
sentinel and the walk descends from `sentinel->nextSibling`. The +528 terminator bit (bit0 =
root-level / chain terminator) and the type-3 make-current-camera leg (InvalidateCurrent +
SetWorldTranslation via inert hooks) are reconstructed verbatim.

Wired (rule 13) in `object_attach_wiring.cpp`: `ObjLife10Hooks.objSetParent`/`objLinkIntoScene`
now bind to the real splice. RECONCILIATION (SceneNode10 raw block <-> render::SceneNode): each
verbatim 0x21C SceneNode10 the live chain threads gets a SHADOW `render::SceneNode` carrying the
+496/+500/+504/+508 link fields (a 64-bit host cannot keep the original's 4-byte pointer spacing
inside one block — LP64); the +528/+533 bytes mirror the raw block and +528 is synced back after
each splice. The shadows form the tree `render::WalkAndInvoke` traverses. So
`ObjectAttachToUniverseNode` (parent=0 -> LinkIntoScene root; parent!=0 -> SetParent child) now
builds a real walkable tree.

Tests: `scene_link_test` (suite SceneLink, 52 checks, golden, no assets) — LinkAsSibling
append/inherit/terminator, SetParent firstChild+sibling-chain+parent, SetParent(null) prepend +
universe, type-3 make-current-once, type-0 not chained, and a built tree walked by WalkAndInvoke
in correct pre-order. `scene_link_chain_e2e_test` (suite SceneLinkChain, guarded) — attaches real
Objects.BIN members (BUCH 604 polys / OB_BUCH_LOW 172 / ABT_KUTTE 532) as roots + one child under
the first root, then WalkAndInvoke visits all 4 spliced shadow nodes (16 checks; clean skip with
no assets).

# 3D scene GPU pipeline (Vulkan shaders) + FPS

Goal (user, 2026-06-09): "add shaders 1:1" + "improve fps" for the 3D scene path.
The original is fixed-function DirectDraw/Direct3D (no programmable shaders); rule 3
lets us swap the *rasteriser's GPU API* to Vulkan while the transform/lighting/
projection MATH stays the reconstructed 1:1 engine math. Decision (user): a **3D-scene
GPU pipeline** with **GLSL compiled at build time**.

## Architecture — one draw list, two backends
- `render::Scene3DDrawList` (`src/render/scene_drawlist.{h,cpp}`) — backend-neutral:
  world-space verts + a 0..1 colour modulator (folds baked-light Gouraud OR flat
  shade) + UV, triangles grouped into texture-runs in draw order, plus the camera
  basis + `render::ProjectViewPoint` terms. The fragment result is always `texel *
  colour` (texel = white when untextured).
- `play::BuildSceneDrawList` (`src/play/scene_view.cpp`) runs the engine's exact T&L —
  the same parent-composed world transform, `VIBE_Light_BuildObjectCache` per-vertex
  baked lighting, camera basis and projection `RenderSceneObjects` uses — and emits the
  list. `UpdateSceneDrawListCamera` re-aims an existing list (camera-only, cheap).
- `render::RasterizeDrawList` — CPU reference rasteriser (the headless/portable path +
  the oracle). Validated **bit-exact** to `RenderSceneObjects` for flat shading and
  within 1 LSB for baked (float summation association); test `scene_drawlist_equiv_e2e`.
- `shim::IGraphicsDevice::renderScene3D(dl, target)` — GPU path. Default returns false
  (software/headless backends) so the caller falls back to `RasterizeDrawList`.

## Vulkan backend (`src/shim_impl/vulkan_backend.cpp`, `GUILD_HAVE_VULKAN`)
`VulkanGraphicsDevice::renderScene3D` builds (lazily) a render pass (colour=present
target B8G8R8A8 + D32 depth), pipeline layout (128-byte push constants, 1 combined-
image-sampler set), and the graphics pipeline from the embedded SPIR-V, then per call
uploads/binds the draw list and reads the rendered image back into the caller's CPU
surface (so the 2D overlay + present path are untouched).
- Shaders: `src/shim_impl/shaders/scene.{vert,frag}`, compiled to SPIR-V by **glslc at
  build time** (CMake, `-mfmt=c` -> embedded `uint32` headers; `GUILD_HAVE_SCENE_SHADERS`).
  * vert: reproduces the view basis + `ProjectViewPoint`; sets `gl_Position.w = vz` so
    `smooth` UV interpolates perspective-correct (1/vz, matching the CPU) while `vColor`
    / `vViewZ` are `noperspective` (screen-space, matching the CPU's flat Gouraud + the
    linear-z buffer). frag: `texel * vColor` (NEAREST+REPEAT == `SampleTexel`), and
    `gl_FragDepth = vViewZ/farZ` so the GPU depth order matches the CPU z-buffer.
  * Backface cull: CPU screen `area` == 2x Vulkan framebuffer signed area, so
    `backfaceCull==1` (drop area<0) == cull BACK with **CW** front (verified on lavapipe).
- Validation: `scene_gpu_vs_cpu_e2e_test` renders the real ChooseCity scene on the GPU
  (lavapipe) and on the CPU and asserts they match — ~95% of pixels within 4/255, mean
  error < 8; residual is triangle-edge fill-rule + NEAREST texel-boundary ULP, not a
  transform/shading divergence. Visually identical (the lit walled room + map table).

## FPS
- Per-frame cost was dominated by re-decoding 27 `.bgf` meshes + rebuilding 67 textures
  + baking lighting EVERY frame. `RunCityScreen3D` now builds the draw list **once** and
  per frame only calls `UpdateSceneDrawListCamera` (a handful of floats); it rebuilds
  only when the tower moves (on click), bumping `Scene3DDrawList::geometryId`.
- The Vulkan backend **caches** the uploaded vertex buffer + textures + descriptor sets
  keyed by `geometryId`; a camera-only frame re-records with new push constants and
  skips every upload. On real hardware the per-pixel raster + texturing now run on the
  GPU instead of the CPU software rasteriser — the structural FPS win.

## Status
Both presets green: portable **1151/1151**, vulkan-sdl-system **1151/1151** (lavapipe).
Tests: `scene_drawlist_equiv_e2e` (list == oracle), `scene_gpu_vs_cpu_e2e` (GPU == CPU),
plus the existing ChooseCity flow/montage/itest. Software rasteriser retained for the
portable/headless build (no GPU deps), per the platform-boundary rule.

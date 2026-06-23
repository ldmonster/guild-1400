# Per-pixel interpolated vertex fog — wave-7 (agent W7-FOGPIX)

Owner modules: `src/render/raster.{h,cpp}`, `src/render/raster_textured.{h,cpp}`,
`src/render/fog.{h,cpp}`. Extends `tests/unit/render_fog_test.cpp`.
Status: **per-pixel D3D vertex-fog interpolation reconstructed 1:1 + tested.** Closes
the wave-6 named gap (fog was wired but used a per-TRIANGLE constant factor first cut).

## The gap this closes

Wave-6 (fog-render-wave6.md / frame-integration-wave6.md) reconstructed the fog
state, the per-vertex `ComputeFogFactor`, and the per-pixel colour blend
`BlendFog565`, then wired fog into the textured span via a process-global
`SpanFog()`. But the per-pixel factor was a **per-triangle CONSTANT** (`SpanFog().factor`)
— a first cut. The engine's D3D fixed-function vertex fog interpolates the
per-vertex factor LINEARLY across the triangle and blends each pixel with its own
factor. Wave-7 makes that interpolation real: the per-vertex fog factor is carried
as a **THIRD 16.16 span channel alongside U and V**, exactly the way the original's
D3D fixed-function linear vertex fog interpolates.

## What the original does (decompiled, 1:1)

D3D fixed-function VERTEX fog. The engine never sets `D3DRENDERSTATE_FOGTABLEMODE`,
so D3D uses the **per-VERTEX fog factor** (the FVF specular byte), interpolated
linearly across the triangle in screen space (Gouraud-style, affine — matching this
path's affine U/V), then per pixel: `out = f*src + (1-f)*fog`, `f = factor/255`.

- `0x5e010c VIBE_Render_BeginScene` (re-verified): issues only
  `SetRenderState(28 FOGENABLE, a1)` and, when enabled, `SetRenderState(34 FOGCOLOR,
  a2)`. **No** FOGTABLEMODE -> table/pixel fog OFF -> vertex fog with linear factor
  interpolation. (vtable +88 = SetRenderState.)
- Per-vertex factor written to **vertex + 79** (= 0x4F; vertex stride 0x50 = 80):
  - billboards `0x5ac9aa VIBE_Particle_UpdateBillboards`: `mov [edx+4Fh], al`
    (factor truncated via `VIBE_Coord_ConvertX` @0x5c6b08; clamp `mov eax,406FE000h`
    = HIDWORD(255.0)). Verified disasm 0x5aca99/0x5acaa1.
  - terrain `0x5beb0b VIBE_Floor_TransformTileGeometry`: `mov [ecx+4Fh], al`
    (same `dbl_628B34` = 255.0 clamp, `mov ebx,406FE000h`). Verified disasm
    0x5beba6/0x5bebae.
  Both are byte-identical to the `ComputeFogFactor` reconstruction (fog.cpp), and
  both write the SAME +79 FVF specular byte we now interpolate.

## The per-pixel reconstruction (the third span channel)

Carried exactly like the affine U/V channels already in the RGBZ rasterizer.

### Data (raster_textured.h `RgbzRasterState`, `RgbzVertex`)
- `RgbzVertex::fogFactor` (int, default 255) — the per-vertex factor (vertex+79).
- `RgbzRasterState::vf[3]` — per-vertex factor promoted to 16.16 (`factor << 16`).
- `RgbzRasterState::fLeft / fLeftStep` — left-edge factor accumulator + d/dy.
- `RgbzRasterState::fGrad` — horizontal dF/dx (16.16).
- `RgbzRasterState::fogPerPixel` — set when `SpanFog().enabled`.

### Carrier (raster.h `RasterState`, used by the inner span body)
- `RasterState::fStart / fGrad / fPerPixel` — the per-pixel factor accumulator,
  horizontal step, and the enable flag the inner span fill reads.

### Edge walk (raster_textured.cpp `InterpolateEdgeRgbz`)
The factor channel uses the **same two-path fixed-point edge slope** as U/V
(`(vf[b]-vf[a])<<16 / dy`, or the `(recip*num)>>14` short-edge trick), and the same
sub-scanline prologue back-off `fLeft = vf[a] + (fLeftStep*sub)>>16`. `clampRows`
advances `fLeft += fLeftStep*skip` over surface-clipped top rows.

### Horizontal gradient (raster_textured.cpp `RasterizeTexturedTriangleRgbzWith`)
`dF/dx` is the SAME screen-space cross product as `dU/dx`, on the per-vertex factor
bytes (argument order, like the binary reads the projected vertex attributes),
scaled by the identical `v18 = 65536/area`. So `fGrad` is already 16.16 factor/pixel.
`fogPerPixel = SpanFog().enabled`.

### Span fill (raster_textured.cpp `FillSpanLoop`)
Per row, the factor start backs off from the left edge to the first covered pixel
exactly like U/V: `fStart = (fGrad*sub)>>16 + fLeft`, and `span.fPerPixel/fGrad/
fStart` are handed to the inner body.

### Per-pixel blend (raster.cpp `FillSpanTextured` / `FillSpanTexturedMasked`)
When `SpanFog().enabled && rs.fPerPixel`: each pixel computes
`factor = clamp(fStart >> 16, 0, 255)`, blends `BlendFog565(palBase[lightRow8|idx],
fog.color, factor)`, then `fStart += fGrad`. The masked span advances the factor
channel on EVERY covered pixel (D3D interpolates fog at every pixel; only the
write/blend is masked), so the gradient stays in phase across skipped texels.

## Byte-identical default-disabled path (critical)

* `SpanFog().enabled` defaults FALSE -> the whole fog branch is bypassed; the span
  is the exact original textured fetch. Verified: existing raster + raster_textured
  + colourkey + frame-integration pins are GREEN with no change.
* `RasterState::fPerPixel` / `RgbzRasterState::fogPerPixel` default FALSE.
* When `enabled` but `!fPerPixel` (direct `FillSpanTextured` callers that don't seed
  the channel, e.g. the wave-6 `frame_integration_wave6_test`), the span falls back
  to the per-triangle constant `SpanFog().factor` — the wave-6 behaviour, preserved.
* The per-pixel factor only changes output when fog is ENABLED and the channel is
  seeded by the RGBZ triangle pipeline.

## Bind-site note (no bind-site edit — handoff already wired)

The wave-6 handoff is already in place: `CityView3D::RenderFrame` sets/restores
`SpanFog()` (enable + colour) around the object/terrain flush (frame-integration-
wave6.md table row 4e). With wave-7, **`RasterizeTexturedTriangleRgbz` now reads the
per-vertex `RgbzVertex::fogFactor` (the vertex+79 byte set by the already-wired
`ComputeFogFactor`) and interpolates it per pixel automatically whenever
`SpanFog().enabled`** — the integrator only needs to populate `RgbzVertex::fogFactor`
from the vertex+79 byte produced during transform (billboard/terrain `ComputeFogFactor`
passes). No new frame step, no bind-site change in city_view3d.*/universe_render.cpp.
Default `fogFactor = 255` keeps untouched vertices fog-free. This note is the handoff
for the orchestrator (do NOT edit bind sites — W7 owns module bodies + tests only).

## Tests (rule 11) — render_fog_test.cpp, all GREEN

New suite `RenderFogPerPixel` (and existing `RenderFog` unchanged):
* `SpanGradientGolden` — 6-px white span, black fog, factor ramp 0..255 via the
  16.16 channel -> per-pixel gradient `0000 3186 632C 94B2 C658 FFFF` (engine
  ColorFormat 565 reference; monotone toward source).
* `SpanFactorClamped` — negative start -> factor clamps to 0 (full fog); >255 ->
  clamps to 255 (no fog).
* `FogOffByteIdentical` — channel seeded but `SpanFog()` disabled -> raw palette
  colours (the third channel is ignored).
* `FillSpanLoopRowGradient` — the RGBZ scanline driver reproduces the same per-pixel
  ramp through `FillSpanLoop` (seeded `fLeft/fGrad/fogPerPixel`).
* `TriangleVertexFactorGradient` — full `RasterizeTexturedTriangleRgbz` with top-left
  factor 0 / top-right factor 255: the top span is a monotone left->right blend
  (blue fog) from heavily-fogged to (near-)unfogged — a genuine per-pixel gradient.
* `PerPixelDiffersFromConstant` — the same triangle's top span carries >=3 distinct
  colours (a real ramp), which a per-triangle constant factor could never produce.

Existing pins kept GREEN (fog-off byte-identical), verified standalone:
`render_raster_test` (1790 checks), `render_raster_textured_test` (1082 checks),
`frame_integration_wave6_test` (16 checks, incl. the per-triangle constant path),
plus `render_fog_test` (130 checks total).

Standalone build (other agents' concurrent edits broke the shared lib at write time
in non-owned files — mirror_project.h, shadow_ground.cpp, particle_integrate.h;
these three owned files compile clean as `guild.dir` objects and standalone):
```
g++ -std=c++17 -Isrc -Iinclude -Ishim -Itests/framework \
  tests/unit/render_fog_test.cpp tests/framework/test_main.cpp \
  src/render/fog.cpp src/render/raster.cpp src/render/raster_textured.cpp \
  src/render/colorformat.cpp src/render/particle.cpp src/crt/rand.cpp \
  src/render/surface.cpp src/render/texture.cpp -o /tmp/fog_test && /tmp/fog_test
```

## Pin changes

NONE to existing pins (all fog-off frames byte-identical). New goldens added only in
the new `RenderFogPerPixel` suite.

## Addresses (reference of record)
| addr | name | role |
|------|------|------|
| 0x5e010c | VIBE_Render_BeginScene | FOGENABLE(28)/FOGCOLOR(34), no FOGTABLEMODE -> vertex fog |
| 0x5ac9aa | VIBE_Particle_UpdateBillboards | per-vertex factor -> vertex+79 (`mov [edx+4Fh],al`) |
| 0x5beb0b | VIBE_Floor_TransformTileGeometry | per-vertex factor -> vertex+79 (`mov [ecx+4Fh],al`) |
| 0x5c6b08 | VIBE_Coord_ConvertX | factor float->byte truncate (both passes) |
| dbl_628074 / dbl_628B34 | 255.0 | factor clamp ceiling (both passes; 406FE000h) |
| 0x5F6930 | VIBE_Raster_InterpolateEdgeRgbz | now also interpolates the fog factor edge |
| 0x5F6B34 | VIBE_Raster_FillSpanLoop | now walks the fog factor across the span |
| 0x5F6C30 | VIBE_Raster_RasterizeMirrorTriangle | now computes dF/dx + seeds the channel |
| 0x5F71AD / 0x5F721A | FillSpanTextured / ..Masked | per-pixel BlendFog565 with interp factor |

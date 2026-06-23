# Distance fog — render reconstruction (Wave 6, agent W6-FOG)

Owner module: `src/render/fog.{h,cpp}`, `src/render/falloff_lut.{h,cpp}`.
Status: **math complete + colour-blend reconstructed + tested.** One bind-site hook
remains for the orchestrator to wire into the raster span (documented precisely below;
this agent does NOT edit raster*/city_view3d*/terrain* per ownership rules).

## What the original does (decompiled, 1:1)

Distance fog in `gilde.exe` is **D3D fixed-function VERTEX fog**. There is no fog
math in the rasterizer itself — the engine computes a per-vertex fog factor and hands
it to Direct3D, which interpolates it across the triangle and blends each pixel toward
the fog colour. Rule 3 (D3D -> software raster) requires we reconstruct both halves.

### 1. Fog state / configuration (already reconstructed, verified)
- `0x5ae2a0 VIBE_Render_SetFogRange(near, far)` — stores near, `near^2`, far, and
  `densitySlope = 255/(far-near)` (numerator `flt_628088 = 0x437F0000 = 255.0`).
- `0x5ae384 VIBE_Render_ConfigureFog(near, far, color)` — gated on
  `byte_649D70 && (byte_140806D & 0x20)`; sets `enabled = (near<far)` (`byte_649DD8`),
  latches fog colour `dword_649DD4`, near/far `flt_13FC5FC/5F8`.
- `0x5b8b04 VIBE_SkyColor_ApplyAmbientBlend` — time-of-day cross-band lerp feeding
  ConfigureFog.
- Globals: `flt_13FC5AC` near, `flt_13FC568` far, `flt_13FC544` near^2,
  `flt_13FC58C` slope, `dword_649DD4` colour, `byte_649DD8` enable.

### 2. Per-vertex fog factor (`ComputeFogFactor`)
Computed in TWO passes with **byte-identical** math:
- `0x5ac970 VIBE_Particle_UpdateBillboards` @0x5ac9aa (billboards), and
- `0x5be668 VIBE_Floor_TransformTileGeometry` @0x5beb0b (terrain),
both gated on `byte_649DD8`, both storing the result at **vertex + 79** (the FVF
specular byte D3D uses as the fog channel):
```
if (d2 <= near^2)  f = 255.0;
else { e = (sqrt(d2) - near) * slope;
       e = (255.0 >= e) ? e : 255.0;    // clamp ceiling 255.0
       f = 255.0 - e; }
vertex[+79] = (int)f;                    // ConvertX truncate toward zero
```
`d2` = squared camera-space distance (`flt_13FC548`). Result is in **[0, 255]**:
255 = no fog (at/inside near), 0 = full fog (at/beyond far).

**1:1 CORRECTION (important).** The prior reconstruction (and the existing unit test)
claimed the billboard clamp ceiling was **256.0**, giving `255-256 = -1` (a 0xFF wrap)
beyond the far plane. That was a **Hex-Rays decompiler artifact**: the pseudocode
showed `v12 = 1081073664` (= `0x40700000` = HIDWORD(256.0)). The real instruction
(disasm `0x5aca75`) is `mov eax, 406FE000h` — the HIGH dword of a double with low
dword 0 = `0x406FE00000000000` = **255.0**. Confirmed by `get_bytes`:
`dbl_628074 = ..E0 6F 40` and the terrain clamp `dbl_628B34 = ..E0 6F 40`, both 255.0.
So **both passes clamp to 255.0**, the factor floors at 0, and there is **no wrap**.
`kFogFactorClampHi` is now 255.0 and `ComputeFogFactor(beyond-far) == 0`.

### 3. The fog-colour blend (D3D vertex fog -> software, NEW)
- `0x5ae434 VIBE_Render_DrawTexturedTriangles` calls
  `0x5e010c VIBE_Render_BeginScene(byte_649DD8, dword_649DD4, 1)`.
- `BeginScene` issues (device vtable `+88` = `SetRenderState`):
  - state `28` (0x1C `D3DRENDERSTATE_FOGENABLE`) = `byte_649DD8`,
  - state `34` (0x22 `D3DRENDERSTATE_FOGCOLOR`) = `dword_649DD4` (only if enabled).
- `D3DRENDERSTATE_FOGTABLEMODE` is **never set** -> table/pixel fog OFF -> D3D uses
  the per-VERTEX factor (FVF specular, vertex+79), interpolated linearly across the
  span, and blends per channel: `out = f*src + (1-f)*fog`, `f = factor/255`.

Reconstructed (8-bit integer-exact, round-to-nearest HW /255 blend):
```
out = (factor*src + (255-factor)*fog + 127) / 255      // per channel
```
New entries in `fog.{h,cpp}`:
- `u8  BlendFogChannel(u8 src, u8 fog, int factor)` — one channel.
- `u32 BlendFogRgb(u32 srcRgb, u32 fogColor, int factor)` — full 0x00RRGGBB.
- `u16 BlendFog565(u16 src565, u32 fogColor, int factor)` — **the raster hook**:
  unpacks the 565 surface pixel through the engine `ColorFormat` (the same shift-only
  `UnpackColor` @0x434f7c the rest of the raster uses), blends, repacks via
  `PackColor` @0x434f30. `factor == 255` fast-returns `src565` unchanged.

### 4. Falloff LUT (`falloff_lut.{h,cpp}`, already reconstructed, now tested)
- `0x5c88f8 VIBE_Light_InitFalloffTable` — `table[i] = 1 - asin(i/1024)*(2/pi)`,
  i in [0,1024). Constants `flt_628CB4 = 0x3A800000 = 1/1024`,
  `flt_628CB8 = 0x3F22F983 = 2/pi` (get_bytes verified).
- `0x5f0b9c VIBE_Math_AcosGuarded` computes `asin(x)` (despite the IDA name);
  `|x|==1` guard returns 0 / pi (never hit by the table, max x = 1023/1024 < 1).
- This is the **light** falloff curve (distance attenuation for the 7 light rigs),
  adjacent to the fog cluster; it is NOT part of the distance-fog blend. Kept in this
  agent's ownership and now covered by golden tests.

## ===== THE BIND-SITE HANDOFF (for the orchestrator) =====

The fog colour blend is the only piece needing a raster-layer hook. The math entry is
ready; the integration is one per-pixel call in the textured/shaded span tail. This
agent must NOT edit raster* — here is the precise hook.

**Where:** in the textured span fill, after a texel/shaded colour is fetched and just
before it is written to the 16bpp `Surface` — i.e. the per-pixel tail of:
- `render/raster_textured.cpp` `FillSpanTextured` / `FillSpanLoop` (0x5F6B34), and
- `render/raster.cpp` `FillTexturedSpansShaded` (the shaded-affine sibling).

**What to add (per pixel):**
```cpp
// fog is the live FogState; fogFactor is the per-pixel interpolated factor.
if (fog.enabled)                                   // byte_649DD8
    px565 = guild::render::BlendFog565(px565, (u32)fog.color, fogFactor);
*dst = px565;
```

**The per-pixel `fogFactor`:** D3D interpolated the per-vertex factor (vertex+79)
linearly across the triangle. The faithful path is to carry the fog factor as a THIRD
interpolated span channel (alongside U and V), 16.16 fixed-point, exactly like the
U/V edge-walk already in `RgbzRasterState`:
- add `fLeft / fLeftStep` (left-edge accumulators) and `fGrad` (dF/dx) to the span
  setup, seeded from the three `vertex[+79]` bytes,
- step it per pixel, `factor = fLeft >> 16` (clamp [0,255]),
- pass `factor` to `BlendFog565`.
If the orchestrator prefers a first cut, a per-TRIANGLE constant factor (average of
the 3 vertices' +79 bytes, matching `dword_13FC5E0`-style averaging the raster already
does for the light row) is acceptable as an inert-default until the third channel is
wired — but the per-vertex interpolation is the 1:1 behavior.

**Inputs available at the bind site:**
- `FogState` (fog near/far/colour/enable) — produced by the already-wired
  `GetUniverseRenderHooks().configureFog(...)` chain (`src/sim/universe.cpp`), which
  must populate a `render::FogState` the raster can read. Recommend a single
  process-global `render::FogState g_fog` set by ConfigureFog and read by the span.
- The per-vertex fog factor — `ComputeFogFactor(g_fog, d2)` called during vertex
  transform (the billboard / terrain transform passes), stored on the vertex; the
  object draw path (`VIBE_Render_DrawTexturedTriangles` @0x5ae434) already copies
  vertex bytes into the vertex buffer (the `v3[5] = *(float*)(v8+76)` copies pick up
  +76; +79 rides in the same dword) — when the SW transform writes +79, the SW span
  reads it.

**Gate:** `FogState::enabled` (== `byte_649DD8`). No new Options flag; fog enable is
driven by `ConfigureFog` (near < far) which the universe/atmos chain already calls.

**Frame order:** unchanged — fog is applied inside the existing span fill, so it
needs no new step in `CityView3D`'s frame; it rides along the object/terrain draw that
already happens after `BeginUniverseFrame` -> terrain arm -> `RasterizeMeshList`.

## Tests (all pass — 113 checks)
- `tests/unit/render_fog_test.cpp` — SetFogRange, ComputeFogFactor (corrected far/
  beyond-far = 0), ConfigureFog gating, ApplyAmbientBlend, **BlendFogChannel /
  BlendFogRgb / BlendFog565 golden vectors** (Python-reference, get_bytes-sourced
  constants).
- `tests/unit/render_falloff_lut_test.cpp` — AcosGuarded == asin, InitFalloffTable
  golden entries (0/1/256/512/768/1023), monotone-falloff range.
- `tests/integration/render_fog_blend_itest.cpp` — renders a fog-blended span into a
  real 16bpp `Surface` (near->far gradient: left = texel, right = fog colour, monotone
  between) + a disabled-fog passthrough. This is the render-to-Surface proof the brief
  requires and the exact per-pixel handoff the raster layer will perform.

Standalone build (other agents' concurrent edits broke the shared `guild` lib at the
time of writing; these files compile clean in isolation and as `guild.dir` objects):
```
g++ -std=c++17 -Isrc -Iinclude -Ishim -Itests/framework \
  tests/unit/render_fog_test.cpp tests/unit/render_falloff_lut_test.cpp \
  tests/integration/render_fog_blend_itest.cpp tests/framework/test_main.cpp \
  src/render/fog.cpp src/render/falloff_lut.cpp src/render/colorformat.cpp \
  src/render/particle.cpp src/crt/rand.cpp src/render/surface.cpp -o /tmp/fog_all
```

## Addresses touched (reference of record)
| addr | name | role |
|------|------|------|
| 0x5ae2a0 | VIBE_Render_SetFogRange | near/far -> slope, near^2 |
| 0x5ae384 | VIBE_Render_ConfigureFog | gated config + latches |
| 0x5b8b04 | VIBE_SkyColor_ApplyAmbientBlend | time-of-day cross-band lerp |
| 0x5ac970 @0x5ac9aa | VIBE_Particle_UpdateBillboards | per-vertex fog factor (billboards) |
| 0x5be668 @0x5beb0b | VIBE_Floor_TransformTileGeometry | per-vertex fog factor (terrain) |
| 0x5ae434 | VIBE_Render_DrawTexturedTriangles | calls BeginScene w/ fog enable+colour |
| 0x5e010c | VIBE_Render_BeginScene | D3D FOGENABLE(28)/FOGCOLOR(34) -> the blend |
| 0x5c88f8 | VIBE_Light_InitFalloffTable | light falloff LUT |
| 0x5f0b9c | VIBE_Math_AcosGuarded | asin helper |
| 0x434f30/0x434f7c | PackColor/UnpackColor | 565 (un)pack used by BlendFog565 |
| dbl_628074 / dbl_628B34 | 255.0 | fog factor clamp ceiling (both passes) |
| flt_628088 | 255.0 | fog slope numerator |
| dword_649DD4 / byte_649DD8 | fog colour / enable | blend inputs |

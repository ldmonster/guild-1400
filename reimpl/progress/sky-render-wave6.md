# Sky / horizon backdrop — wave-6 (W6-SKY)

Status: **DONE** (reconstructed + wired entry exposed + tested + documented).

## The core finding (what the "sky" actually is)

Die Gilde's 3D engine (DirectDraw / Direct3D fixed-function) has **no sky dome or
sky-gradient GEOMETRY**. The visible sky behind the terrain is the **framebuffer
clear colour**, global `dword_649DD4`. That colour is the engine's current
**time-of-day fog/sky colour**, recomputed each scene-state change. So "render the
sky 1:1" == compute that time-of-day colour and fill the universe surface with it
**before** drawing terrain — exactly the order the original frame uses.

This was verified end-to-end through the call graph (no guessing):

* `VIBE_Render_BeginUniverseFrame @0x5b3900` (the frame spine):
  - `@0x5b3953` if `byte_649D70` → `VIBE_Render_ClearViewport @0x5dd464`
    else → `VIBE_Render_ClearRect(&dword_13ECE58, …, dword_649DD4, …) @0x434728`
  - **then** `@0x5b3a2f VIBE_Floor_RenderTerrain`.
  So the **clear (with `dword_649DD4`) happens first, terrain second.**
* `VIBE_Render_ClearViewport @0x5dd464` issues the DDraw clear with the colour
  block `dword_13ECE58..64` / `dword_649DD4` (vtbl+80 Blt-colorfill).
* `VIBE_Render_ClearRect @0x434728` is the software clear of the back buffer rect
  (memset-style span fill) — what our software path mirrors.
* `dword_649DD4` is written **only** by `VIBE_Render_ConfigureFog @0x5ae384`
  (`@0x5ae419 dword_649DD4 = a3`).
* `VIBE_Render_ConfigureFog` is called by `VIBE_SkyColor_ApplyAmbientBlend
  @0x5b8b04` (`@0x5b8c7a`) — the time-of-day cross-band sky/fog colour blend.

The lens-flare / sun-sprite path (`VIBE_Render_UpdateSkyFlares @0x5ef7cc` →
`VIBE_Render_DrawSkyFlareSprite @0x5eef90`) draws *sprites* via the D3D device
DrawPrimitive (vtbl+116) — that is a GPU-API leaf (rule 3 / present layer), **not**
the backdrop, and is out of scope for the surface-fill backdrop. Noted for
completeness; not reconstructed here.

## Functions reconstructed this wave

| Address | Name | Status |
|---|---|---|
| 0x5b8b04 | VIBE_SkyColor_ApplyAmbientBlend | **NEW — reconstructed 1:1** → `BlendAmbientFog` |
| 0x5ae384 | VIBE_Render_ConfigureFog (sink) | analyzed; sets `dword_649DD4` (the sky colour) |
| 0x5dd464 / 0x434728 | ClearViewport / ClearRect | analyzed; modeled by `RenderSky` (surface fill) |
| 0x5b85e4 | VIBE_SkyColor_BlendBandLighting | already present (ambient triple + luma) |
| 0x43f460 | VIBE_SkyColor_ApplyScaledBlend | already present (brightness→alpha) |
| 0x5c6b08 | VIBE_Coord_ConvertX | chop-toward-zero truncate (used by the byte lerp) |

`sky.cpp` was a 55-line stub (only the two pre-existing colour helpers). It is now
completed with the time-of-day sky colour blend and the `RenderSky` fill entry.

### 0x5b8b04 — VIBE_SkyColor_ApplyAmbientBlend (the time-of-day sky colour)

`__userpurge al(a1@eax = bandA, a2@edx = bandB, a3 = frac)`. Disassembly-exact
(0x5b8b04..0x5b8c8d). Cross-fades band `a1` → band `a2` by `frac` across three
parallel triple-stride (3-dword) runtime arrays:

* `dword_13FD170[3*i]` — device-packed sky/clear colour (bytes B2,B1,B0)
* `flt_13FD174[3*i]`   — fog near distance
* `flt_13FD178[3*i]`   — fog far distance

Math (1:1):
* per colour byte B2/B1/B0: `trunc( (byteB - byteA)*frac + byteA )` via ConvertX
  (chop toward zero), reassembled `B2<<16 | B1<<8 | B0` into `v20`.
* `near = ((nearB-nearA)*frac + nearA) * flt_64A018`   (ConfigureFog arg1, v12)
* `far  = ((farB -farA )*frac + farA ) * flt_64A018`   (ConfigureFog arg2, v17)
* `VIBE_Render_ConfigureFog(near, far, v20)` then `dword_649F08 = a1`.
* Early-out `return 0` (nothing written) when `a1>=6 || a2>=6 || frac<0 || frac>1`.

Recovered constant (`get_bytes`): `flt_64A018 = 0x3F800000 = 1.0f` (fog-range
scale) → `kFogRangeScale`. Luma weights re-confirmed: `flt_628728=0.59`,
`flt_62872C=0.30`, `flt_628730=0.11`.

Note the byte arrays `dword_13FD170[…]` are the SAME scratch triples
`VIBE_SkyColor_CopyGradientEntry @0x5b84c4` fills from `dword_649DD4` (see
`skycolor_recon.cpp`: `pending_packed`). So the gradient cluster (already
reconstructed) feeds this blend, which feeds back the next clear colour — the loop
is closed and consistent with the existing module.

## Public entry points (src/render/sky.h)

* `SkyFog BlendAmbientFog(const SkyFogBand bands[6], unsigned a, unsigned b, float frac)`
  — 1:1 of 0x5b8b04. Returns `{color, near_, far_, applied}`.
* `u32 SkyFogColor(const SkyFogBand bands[6], unsigned a, unsigned b, float frac)`
  — convenience: just the device-packed sky colour (= `dword_649DD4`).
* `void RenderSky(Surface* surf, u32 packedColor)` — fills the universe Surface
  (16/24/32 bpp, clip-rect-respecting) with the sky colour. The software
  equivalent of the engine's pre-terrain clear.

## EXACT CityView3D frame handoff (for the orchestrator — I do NOT edit bind sites)

In `play::CityView3D`'s universe frame, **before** the terrain arm
(`render::BeginUniverseFrame @0x5b3900` → `@0x5b3a2f` terrain), add the sky fill:

```cpp
// 1) time-of-day sky/fog colour (0x5b8b04 -> ConfigureFog sets dword_649DD4):
//    a/b = integer day bands, frac = fractional time-of-day (same split the
//    SkyColor cluster uses: VIBE_SkyColor_SetTimeOfDay @0x5b83b0).
guild::render::SkyFog sky =
    guild::render::BlendAmbientFog(skyFogBands, bandA, bandB, frac);
// (sky.color is the value the original stores in dword_649DD4; sky.near_/far_
//  are the ConfigureFog/SetFogRange inputs the terrain arm already consumes.)

// 2) clear the universe surface to the sky colour, FIRST, before terrain:
guild::render::RenderSky(universeSurface, sky.color);
```

Frame order (must match 0x5b3900): **RenderSky (clear)** → terrain
(`BeginUniverseFrame`/`Floor_RenderTerrain`) → object draw list → present.

Gate: the original guards the clear on `byte_649D71 && dword_13FCD1C` (frame
active) and chooses ClearViewport vs ClearRect on `byte_649D70` (DDraw lock
state). In the software path there is one surface, so `RenderSky` is the
unconditional pre-terrain fill; the `byte_649D70` branch is a no-op distinction.

Inputs: `skyFogBands[6]` come from the SkyColor gradient cluster
(`skycolor_recon` `SkyGradient` scratch — `dword_13FD170/flt_13FD174/flt_13FD178`);
`bandA/bandB/frac` from the per-frame world time-of-day. If the orchestrator does
not yet surface the populated 6-band runtime table, `RenderSky(surf, lastClearColor)`
with the engine's current `dword_649DD4` is the faithful fallback (it IS the clear
colour the original would use that frame).

## Tests (tests/unit/sky_render_test.cpp) — 14 tests, 115 checks, 0 failures

* ScaledBlendAlpha clamps low/high + midpoint (0x43f460).
* BlendBandLighting rejection, lerp+luma, mod-7 wrap (0x5b85e4).
* BlendAmbientFog: out-of-range rejects; frac=0/frac=1 endpoints exact; midpoint
  per-channel chop-toward-zero truncation (golden bytes); SkyFogColor parity.
* RenderSky: 16bpp fill, 32bpp fill, clip-rect honoring, null-safety.

Build note: the full CMake build had transient errors in OTHER agents' concurrently
edited files (`render/sprite_scale.h`, `render/water_render.h`) — none of mine.
Verified my objects compile and the suite passes by building+linking
`sky.cpp + particle.cpp(TruncToward) + crt/rand.cpp + sky_render_test.cpp` directly.

## Files owned/changed
* `src/render/sky.h` — completed (added BlendAmbientFog/SkyFogColor/RenderSky + handoff doc).
* `src/render/sky.cpp` — completed from stub (added the 3 entries above).
* `src/render/skycolor_recon.{h,cpp}` — unchanged (already complete; cross-referenced).
* `tests/unit/sky_render_test.cpp` — new.
* `progress/sky-render-wave6.md` — this file.

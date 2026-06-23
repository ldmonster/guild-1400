# Snow weather render — wave-6 (W6-SNOW)

Owner: W6-SNOW. Scope: `src/render/snow.{h,cpp}`, `src/render/snow_recon.{h,cpp}`,
`tests/unit/snow_render_test.cpp`, `tests/unit/snow_recon_test.cpp`.

The "snow" entity is two distinct things in `gilde.exe`, both reconstructed 1:1:

1. **Falling-snow particle field** — a screen-space billboard particle overlay drawn
   on top of the city frame, gated on the winter season.
2. **Snow scene teardown / floor-texture restore** — the misleadingly-named
   `VIBE_Snow_UpdateScene`, run when the season *leaves* winter.

There is no separate "snow-on-ground tint": the ground/building snow look is produced
by the texture loader's winter flag `dword_140809C` (see below) which makes the engine
load the snow-variant floor textures and suppress the alpha-transparency flag on
opaque `DC_`-prefixed textures. `VIBE_Snow_UpdateScene` is the function that *reverts*
that when winter ends.

---

## Addresses reconstructed

| Address    | Original name                  | Reconstructed as                              | Status |
|------------|--------------------------------|-----------------------------------------------|--------|
| 0x42b5b0   | VIBE_Snow_Render               | `SnowRenderStepHeader` + `SnowBuildQuads`     | full (math) |
| 0x42a644   | VIBE_Snow_UpdateFlake          | `SnowUpdateFlake` (pre-existing, kept)         | full   |
| 0x42a014   | VIBE_Snow_Create               | `SnowSeedFlakes` (seed path, pre-existing)     | seed path |
| 0x42a2cc   | VIBE_Snow_UpdateScene          | `SnowResetSceneTexTransparency` + inert hooks | decision reconstructed; table/floor walk = inert hook |
| 0x58339c   | VIBE_GameTime_GetSeasonFromDay | `SnowSeasonFromDay` / `SnowIsWinterDay`        | full (`day % 4`) |
| 0x4b1e94   | VIBE_Sky_InitScene             | (READ-only; documents the gate)                | n/a (other owner) |

### Constants (get_bytes, bit-exact)

Falling-snow render (`0x611990` block):
- `flt_611990 = 0.1`   (0x3DCCCCCD) — per-frame `dt = (now - lastUpdate) * 0.1`
- `flt_611994 = 0.025` (0x3CCCCCCD) — flake depth `v = (1 - pz) * 0.025`
- `flt_611998 = 0.5`   (0x3F000000) — first-vertex x midpoint `(sx + sx2) * 0.5`

Vertex (D3D TLVERTEX) bit patterns emitted verbatim (`0x42b708..0x42b7be`):
- rhw = `0x3F800000` (1.0), diffuse = `0x50646464` (1348756580), specular = `0`.

Flake integration / seed constants (`0x6117AC..0x611814`) were already documented in
`snow.h`/`snow.cpp` (1/32767 RNG norm, dt-drift 0.0025, z-scale 2.0, tail 13.5,
projection bias 3.0, wrap bounds ±1/±2).

---

## VIBE_Snow_Render @0x42b5b0 — the falling-snow overlay

`char VIBE_Snow_Render(int* sys@eax, int@esi)` — runs once per frame for an active
snow system. Control flow:

1. **Header time-interpolation** (`0x42b5c9..0x42b648`), reconstructed in
   `SnowRenderStepHeader(SnowSystemHdr&, now)`:
   - **count ramp** `[0]` over window `[cBeg..cEnd]` between `[cFrom..cTo]`
     (two *separate* integer divides by `(cEnd - cBeg)`); snaps to `cTo` past `cEnd`.
   - **direction ramp** `(dirX,dirZ) [17]/[18]` over `[dBeg..dEnd]` from
     `(oldX,oldZ) [15]/[16]` toward `(tgtX,tgtZ) [19]/[20]`; snaps to target past `dEnd`.
   - **dt** = `(now - lastUpdate) * 0.1`, then `lastUpdate := now`. `now` is the engine
     ms clock `dword_62EB38`.
2. **Integrate** every flake: `VIBE_Snow_UpdateFlake(sys, dt)` (`SnowUpdateFlake`).
3. **GPU state**: `BeginScene`, `SetBlendMode(1,1,0,...)` (additive-style blend),
   set the snow texture (`Schneeflocke`, `sys[36]+100`). *(present-layer; not owned)*
4. **Emit quads** (`0x42b689..0x42b7c8`), reconstructed in `SnowBuildQuads`:
   for each flake whose projected screen segment is inside the viewport
   (`dword_13ECE58/5C/60/64`: `x0<=sx && x1>sx2 && y0<=sy && y1>sy2`), emit **3
   TLVERTEX** forming the flake triangle. Vertices A/B/C use screen coords
   `(sx+sx2)*0.5,sy` / `sx2,sy2` / `sx,sy2`, depth `(1-pz)*0.025`, uv
   `(0.5,0)/(1,1)/(0,1)`. The buffer flushes in batches of 192 verts via
   `DrawPrimitive(D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, ...)`.
5. `EndScene`.

The temp buffer steps 8 dwords (32 B) per vertex (`shl eax,5`); the original passes
`28` as the DrawPrimitive stride arg (an original quirk — TLVERTEX is 32 B). `SnowVertex`
reproduces the **32-byte** record bit-for-bit.

The DDraw/Direct3D state-management (`BeginScene`/`SetBlendMode`/`SetTexture`/
`DrawPrimitive`/`EndScene`) is the present layer and is **not** reconstructed here
(rules 3–4: it routes through `shim::IGraphicsDevice`). `SnowBuildQuads` produces the
exact vertex stream the present layer would draw.

---

## VIBE_Snow_UpdateScene @0x42a2cc — snow teardown / floor restore

Run by `VIBE_Sky_InitScene` when the season leaves winter (weather mode 0) while a
snow system still exists. Steps:
1. `VIBE_Texture_ReleaseEntry(sys[36])` — release the snow texture.
2. free the flake list (`sys+56`).
3. **Walk the global texture table** (`dword_1406A84`, stride 128, count
   `dword_1406A80`): for each `DC_`-prefixed (`byte_6117C8` = "DC_") opaque texture
   with `[+64] > 0` and `([+104] & 2) == 0`, set its transparency flag to
   `v6 = !dword_140809C && *(u8*)(v4+125) > 8` (reconstructed bit-for-bit in
   `SnowResetSceneTexTransparency(winterFlag, texLevel)`); if the set fails, release
   the surface; clear `[+88]`.
4. `VIBE_TextureCache_Reset()`, then reload floor textures for the 64 active universe
   slots (`VIBE_Floor_ReloadTextures`), then free the system block.

Steps 1, 3 (table walk), 4 touch the engine texture table + universe slots which are
owned by other modules; only the **pure per-texture decision** (step 3's rule) is
reconstructed here. The table iteration / floor reload remain documented inert hooks
(rule 8) rather than fabricated.

### `dword_140809C` — the winter texture flag (the "snow tint")

`dword_140809C` is set by the texture loader (`VIBE_Texture_LoadByName @0x5da714`,
`VIBE_Texture_CreateRecord @0x5db724`, `VIBE_Render_EnumTextureFormats @0x5dd0a0`).
It is the global winter/snow flag: while set, the engine loads the snow-variant
floor/building textures and `UpdateScene` keeps `DC_` textures opaque. When winter
ends, `UpdateScene` reads `!dword_140809C` (now true) and re-enables transparency on
high-mip (`level > 8`) `DC_` textures, restoring the normal look. This is the
interaction noted in the brief (`0x42a32f` reads `dword_140809C`).

---

## Seasonal gate (the day%4==3 logic)

`VIBE_GameTime_GetSeasonFromDay @0x58339c` = `*a1 % 4`. In `VIBE_Sky_InitScene
@0x4b1e94`:
```
season = GetSeasonFromDay(day);          // 0..3
if (season == 3) {                       // 3 == WINTER
    r = RandomModulo(100);
    weatherMode = (r >= intensity/2) ? 1(rain) : 2(snow);  // dword_11BC1C0
} else weatherMode = 0;                   // clear
...
if (weatherMode) {                        // winter rain or snow
    if (mode==2 && !snowSys) snowSys = VIBE_Snow_Create(maxFlakes);
    VIBE_Snow_GrowFlakeList(snowSys, 2/1, ...);
} else if (snowSys) {                      // season left winter
    VIBE_Snow_UpdateScene(snowSys);        // <-- teardown
}
```
So: **snow renders only when `day % 4 == 3` (winter) AND the per-season RNG picks
mode 2.** `SnowIsWinterDay(day)` exposes the `day % 4 == 3` test for the gate.

---

## CityView3D handoff (EXACT)

The orchestrator integrates this; **I do not edit** `city_view3d.*` /
`universe_render.*` / `frame.*` / the atmos bridge.

- **WHERE in the frame order:** the falling-snow overlay is the same FrameHooks slot
  the atmos bridge already routes for particles
  (`render::FrameHooks::renderParticles`, installed in
  `src/play/wire_atmos_bridge.cpp::InstallInertAtmosBridge`). It draws **after** the
  terrain + object draw list + water (it is a screen-space billboard overlay composited
  last, like the original's `VIBE_Snow_Render` which runs its own Begin/EndScene over
  the finished 3D frame).

- **CALL to add (faithful path):** once per frame, for the active snow system,
  ```cpp
  float dt = render::SnowRenderStepHeader(snowHdr, nowMs);      // 0x42b5c9..0x42b648
  render::SnowUpdateFlake(sys, dt, snowCam, snowVp);            // 0x42a644
  int nv = render::SnowBuildQuads(sys, snowVp, vertBuf, cap);   // 0x42b689..
  // present-layer: additive-blend the `nv` SnowVertex (textured triangles, FVF
  // TLVERTEX) using the Schneeflocke texture, via shim::IGraphicsDevice.
  ```
  `nowMs` = engine ms clock (`dword_62EB38`); `snowVp` = the active 3D viewport rect
  (`dword_13ECE58/5C/60/64`); `snowCam` = the active camera/world block
  (`dword_13FCD1C` fields).

  The current headless bridge uses the simplified `SnowUpdateFlake(sys, 1.0f, cam, vp)`
  + per-flake point-splat (`SurfaceSetPixelRgb`); the faithful overlay swaps that for
  `SnowRenderStepHeader` (real dt) + `SnowBuildQuads` (the real triangle stream).

- **GATE:** draw only when `render::SnowIsWinterDay(currentDay)` **and** the active
  weather mode is snow (`dword_11BC1C0 == 2`), i.e. a snow system exists
  (`dword_11BC1CC != 0`). On the season leaving winter, the engine instead calls the
  `VIBE_Snow_UpdateScene` teardown (one-shot, not per-frame).

---

## Tests

`tests/unit/snow_render_test.cpp` (this wave):
- `SnowRenderSeason.SeasonFromDayModulo` — `day % 4`, winter = 3.
- `SnowResetScene.TexTransparencyRule` — `!winter && level>8` (UpdateScene tail).
- `SnowRenderHeader.{DtAndLastUpdate, CountRampInterpolates, DirectionRampInterpolates}`
  — golden vectors for the time-interp header.
- `SnowRenderQuads.{EmitsThreeVertsPerVisibleFlake, RespectsOutputCap, EmptyWhenNoFlakes,
  VertexStride}` — exact vertex coords + TLVERTEX bit patterns + cull + cap.
- `SnowRenderSurface.FieldProjectsAndPaints` — seeds + integrates a flake field through
  the real projection, builds the real quads, rasterizes the triangles onto a software
  `Surface`, asserts non-zero pixels (render-to-Surface proof).

`tests/unit/snow_recon_test.cpp` (pre-existing): coverage/accumulator/batch arithmetic.

Result: **67 checks, 0 failures** (built standalone; the full CMake build also globs
these). Other agents' concurrent edits to non-owned files (sky.cpp, water_render,
sprite_scale) currently break the whole-tree build — unrelated to this module.

## Deferred / inert (rule 8)
- The `dword_1406A84` global texture-table walk and `VIBE_Floor_ReloadTextures` loop in
  `UpdateScene` (depend on the engine texture table + universe slot arrays owned
  elsewhere) — documented, the pure per-texture decision is reconstructed.
- The DDraw/D3D state + `DrawPrimitive` calls in `VIBE_Snow_Render` (present layer,
  rules 3–4) — `SnowBuildQuads` produces the exact vertex stream they'd consume.
- `VIBE_Snow_Create` allocation, camera snapshot, and `Schneeflocke` texture upload
  (the seed math is reconstructed in `SnowSeedFlakes`).

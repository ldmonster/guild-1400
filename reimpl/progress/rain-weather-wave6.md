# Wave-6 — RAIN render + weather-state driver

Status: **complete** (1:1). Rain now renders (software streak field onto the
`render::Surface`) and the weather state machine drives the rain gate.

## Functions reconstructed / completed (with addresses)

| gilde.exe addr | name | where | status |
|---|---|---|---|
| 0x429098 | VIBE_Rain_Create (seed path) | `rain.cpp::RainSeedDrops` | pre-existing, verified vs decompile |
| 0x4294d4 | VIBE_Rain_UpdateDrop (integrate+clamp+wrap+project) | `rain.cpp::RainUpdateDrop` | pre-existing, verified vs decompile |
| 0x429c38 | VIBE_Rain_Render (colour pack) | `rain.cpp::RainStreakDiffuse` | **new** |
| 0x429c38 | VIBE_Rain_Render (clip + draw streaks) | `rain.cpp::RainRenderToSurface` | **new** |
| 0x429c38 | VIBE_Rain_Render (vertex build) | `particle_render.cpp::BuildRainVertices` | pre-existing (not owned; D3D-stream form) |
| 0x4292b8 | VIBE_Rain_GrowDropList | `rain_grow_misc_recon.cpp::Rain_GrowDropList` | pre-existing |
| 0x4c0040 | VIBE_Weather_UpdateSky (intensity/grow/scroll/cloud-select) | `weather.cpp` | pre-existing |
| 0x4c0040 | VIBE_Weather_UpdateSky (per-frame state + **rain gate**) | `weather.cpp::WeatherUpdate` | **new** |
| 0x57f190 | VIBE_Weather_UpdateAmbientLoops (rain audio crossfade) | `weather_recon.cpp` | pre-existing |
| 0x505df4 | VIBE_Weather_ApplySeasonalMeshes (season->suffix) | `weather_recon.cpp` | pre-existing |

Note: 0x505df4 was referenced in wave-4 as "the weather dispatch", but it is
actually the **seasonal terrain re-texturing** pass (SNOW/HERBST/FRUEHLING per
`VIBE_GameTime_GetSeasonFromDay`). The real per-frame weather state machine that
**gates rain** is **0x4c0040 VIBE_Weather_UpdateSky**.

## The weather state machine (0x4c0040) — what gates rain

Per frame the engine:
1. `hour = HIWORD(qword_13CE852)` (game-time clock) -> `dword_11BC1C4`. /*0x4c0057*/
2. Indexes three 24-entry arrays by hour:
   - `arc[h]   = dword_11BC038[h]`  weather code (bit0 = "is rain", value = intensity)
   - `windX[h] = dword_11BC100[h]`
   - `windY[h] = dword_11BC160[h]`
3. **Rain gate:** `if (arc[hour] & 1)` -> spawn rain with count `arc[hour] / 5`
   (signed `idiv`), else 0. /*0x4c0085 test byte,1 ; 0x4c009a..a2*/
   - If a rain system exists but no snow system, the original passes the raw
     `arc[hour]` (no /5). /*0x4c0416*/  (captured as `rainSpawnNoSnow`)
4. **Intensity** = peak-of-3 = `max(arc[(h+23)%24], arc[h], arc[(h+1)%24])`. /*0x4c00f0..*/
5. **Grow amounts** (op==2 GrowList): `snowGrow = trunc(2.0 * -windX * I)`,
   `rainGrow = trunc(0.5 * -windX * I)` (`dbl_61E4B0=2.0`, `dbl_61E4B8=0.5`).
6. **Cloud scroll**: `scrollMag = sqrt(windX^2+windY^2) * (I+150) * 0.125`
   (`dbl_61E4C0=0.125`); front layer `*0.75` (`61E4C8`), back layer `*1.5`
   (`61E4D0`). Cloud texture pools selected by category (fair/medium/heavy =
   `Sky_Schoen`/`Sky_Mittel`/`sky_schwer`, via `RandomModulo` with name-reroll).

`render::WeatherUpdate(arc, windX, windY, hour)` returns all of the above in a
`WeatherFrame` (pure decision core; the subsystem dispatch — GrowList, sky
texture/fade/scroll — is the caller's, since it owns those handles).

## The rain render (0x429c38) — math recovered

Constants (`get_bytes`, bit-exact):
- `flt_611764 = 0.1`     dt scale: `dt = (now - lastTick) * 0.1`, fed to UpdateDrop.
- `flt_611768 = 0.0005000000237` density fade: `f = 1.0 - count * 0.0005`.
- `flt_61176C = 96.0`    streak R scale.
- `flt_611770 = 128.0`   streak G/B scale.
- `flt_611774 = 0.025`   z fade: `z = (1 - pz) * 0.025`.

**Streak diffuse colour** (`RainStreakDiffuse(count)`):
```
f = 1.0 - count*0.0005
a = round(f*96)    ; b = round(f*128)    (round-to-nearest, VIBE_Coord_ConvertX/frndint)
diffuse = 0x80000000 | (a<<16) | (b<<8) | b      ; ARGB: A=0x80, R=a, G=B=b
```
count=0 -> **0x80608080** (golden, asserted in tests).

**Per drop** (after UpdateDrop projected head `(sx,sy)` and tail `(sx2,sy2)`):
viewport clip = all four corners inside `[x0,x1) x [y0,y1)`
(`dword_13ECE58/5C/60/64`), then a LINELIST segment head->tail in the streak
colour. The original submits these as D3D `DrawPrimitiveUP(type=2 LINELIST,
FVF=452, stride=28)` in batches of 128 verts (`BuildRainVertices` produces that
exact stream). The software-surface equivalent reproduced here
(`RainRenderToSurface`) draws each clipped streak with `render::SurfaceDrawLine`
in the unpacked streak RGB — the observable result the original produced.

## CityView3D handoff (EXACT — for the orchestrator to wire)

Owner of the bind site: the agent owning `city_view3d.{h,cpp}` /
`universe_render.cpp`. Do NOT edit those here. The handoff:

1. **Per-day weather state** (once per game-day or when the hour changes):
   call `render::WeatherUpdate(arc, windX, windY, hour)` where
   `hour = HIWORD(gameClock)` and the three arrays are the engine's weather/wind
   arcs (`dword_11BC038` / `dword_11BC100` / `dword_11BC160`). Use the returned
   `WeatherFrame` to:
   - drive `Rain_GrowDropList` (op==1 with `wf.rainSpawn` when `wf.rainActive`;
     op==2 with `wf.rainGrow`),
   - drive the snow grow list (`wf.snowGrow`),
   - set the sky layer textures/scroll (`wf.category`, `wf.scrollMag/Fast/Back`).
   The **gate**: rain is only active when `wf.rainActive` (`arc[hour] & 1`).

2. **Per-frame rain integrate + render** (in the universe frame, as a
   world/screen overlay AFTER the terrain + object draw list, BEFORE present —
   the original brackets it BeginScene/SetBlendMode(additive)/EndScene):
   - `dt = (frameTick - rainSys.lastTick) * 0.1f`
   - `RainUpdateDrop(rainSys, dt, cam, vp)` — `cam` from CityCamera3D
     (`dword_13FCD1C` fields: eye +76/80/84, anchor +132/136/140, 3x3 rot
     +396..+436), `vp` = device viewport rect.
   - `u32 diffuse = RainStreakDiffuse(rainSys.count);`
   - `RainRenderToSurface(rainSys, vp, diffuse, frameSurface)` — draws the
     streaks onto `render::Surface` (the 16/32bpp frame presented via Vulkan).
   - **Gate:** only when the active weather is rain (`wf.rainActive`) AND a rain
     system exists (`dword_11BC1C8 != 0`). When not raining, skip the render.

   Snow uses the parallel path (`SnowUpdateFlake` + `BuildSnowVertices` /
   future snow surface render). Both are screen-space overlays (XYZRHW verts).

## Tests

`tests/unit/rain_weather_wave6_test.cpp` (10 tests, 61 checks, all pass):
- Weather: rain gate bit0, peak-of-3 intensity (incl. mod-24 wrap), grow/scroll
  golden (snowGrow=1200, rainGrow=300, scrollMag=218.75 for I=200, windX=-3,
  windY=4), category thresholds.
- Rain: streak diffuse golden (count 0 -> 0x80608080, R=96/G=128/B=128, dims with
  density, alpha bit always set); RNG-seeded drop field deterministic + ordered;
  integrate wraps positions into `[-1,1)` and produces finite screen points;
  >1000 overflow resets to 0; render-to-surface clips (1 of 3 drops drawn) and
  leaves non-black pixels.

Existing suites untouched and still valid: `weather_recon_test`,
`rain_grow_misc_recon_test`, `snow_recon_test`.

## Build note

The full `cmake --build` currently fails on OTHER agents' concurrent edits
(`sky.cpp` missing `std::size_t`, `sprite_scale.h` static_asserts,
`water_render.h` `DrawList`). My four modules compile cleanly in isolation and
the wave-6 test binary links + passes (built standalone from the owned sources +
their math/surface deps).
```
g++ -std=c++17 -I. -Isrc -Iinclude -Ishim -o /tmp/rw_test \
  tests/unit/rain_weather_wave6_test.cpp tests/framework/test_main.cpp \
  src/render/rain.cpp src/render/weather.cpp src/render/surface.cpp \
  src/render/colorformat.cpp src/crt/rand.cpp src/util/matrix.cpp \
  src/render/particle.cpp src/util/math_random.cpp src/util/math_trig.cpp \
  src/util/float_math.cpp src/util/math.cpp src/util/math_rng_float.cpp \
  src/util/fpu.cpp src/util/coord.cpp
```

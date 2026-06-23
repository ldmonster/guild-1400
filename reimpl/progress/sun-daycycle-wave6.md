# Wave-6 — Sun position + Day/Night cycle driver

Owner: W6-SUN. Files: `src/render/sun_state.{h,cpp}`, `src/render/daycycle.{h,cpp}`,
`tests/unit/sun_daycycle_test.cpp`.

The day/night driver maps the game clock (`tick.worldTime()`: day/hour/minute) to
the time-of-day outputs the sky, lighting, and shadow passes consume: the **sky
band + cross-band blend**, the **0..600 brightness step**, and the **sun-elevation
regime** (sunrise vs sunset → sun-direction sign for shadows).

## State of the reconstruction

The day-cycle math leaves were already reconstructed and wired before this wave;
this wave **completed the driver** by adding the consolidated, pure
`render::ComputeSunState(day,hour,minute)` query plus the band→sun-elevation
regime split, so the sky/light/shadow agents have ONE deterministic entry instead
of re-chaining the pieces. No engine behaviour was invented — every line maps to
the decompile.

### Reconstructed / verified functions

| addr | original | where | status |
|------|----------|-------|--------|
| `0x4b2438` | `VIBE_DayCycle_BuildTimeTable` | `daycycle.cpp BuildTimeTable` | done (pre-wave) |
| `0x4b2504` | `VIBE_DayCycle_UpdateBrightness` (brightness core) | `daycycle.cpp UpdateBrightness` | done (pre-wave) |
| `0x4b2671..0x4b274b` | brightness→band/blend | `daycycle.cpp BrightnessToBand` | done (pre-wave) |
| `0x4b28f0` | sunrise/sunset split (`v11%7 < 3`) | `daycycle.cpp SunRegimeForBand` | **NEW this wave** |
| `0x58339c -> 0x4b2438 -> 0x4b253b -> band` | composed time→sun-state | `daycycle.cpp ComputeSunState` | **NEW this wave** |
| `0x42dc40` | `VIBE_Light_SetSunDirection` | `sun_state.cpp SetSunDirection` | done (pre-wave) |
| `0x42dc5c` | `VIBE_Light_EnableSun` | `sun_state.cpp EnableSun` | done (pre-wave) |
| `0x5c8964` | `VIBE_Light_ResetGlobalState` | `sun_state.cpp ResetGlobalState` | done (pre-wave) |

### Tables / constants recovered by `get_bytes`

- `dword_4AD160` @0x4ad160 (96 B) — 4 seasons × 6 keyframe minutes-of-day
  (`kDayKeyframeMinutes`). Verified byte-for-byte.
- `dbl_61DD68` @0x61dd68 = `0.01` — brightness→band scale (`kBandScale`).
- Sun-height envelope (`VIBE_Light_SetSunHeight` @0x4b24b0), `get_bytes` @0x61dd38:
  - `dbl_61DD48` = `0.3`  (day base, `kSunDayBase`)
  - `dbl_61DD40` = `-0.3` (night base, `kSunNightBase` = abs)
  - `dbl_61DD38` = `0.6`  (random span, `kSunSpan`)
  - day branch  : `pitch = 0.3 + r*0.6`  ∈ [+0.3, +0.9)
  - night branch: `pitch = -0.3 - r*0.6` ∈ [-0.9, -0.3)
  - `r = VIBE_Math_RandomFloatScaled()` @0x58b910 = `RandNext()*flt_62675C`,
    `flt_62675C` @0x62675c = `0x38000100` = `1/32767`.

### Sun-elevation regime (the day/night switch the shadow pass reads)

`VIBE_DayCycle_UpdateBrightness`, when the band changes (`0x4b26bb`), walks the
lights and calls `VIBE_Light_SetSunHeight @0x4b24b0`:
- bands **0,1,2** → sunrise walk (`raise=0`) → positive elevation (sun above horizon)
- bands **3..6** → sunset walk (`raise=1`) → negative elevation (sun below horizon)

`SunRegimeForBand(band)` returns `{raise, elevLo, elevHi}` — the branch + the
deterministic elevation envelope. The actual per-light elevation float is RNG-
sampled live by `render::SetSunHeight @0x4b24b0` (owned by `render_leaves2`,
reused, NOT redefined here); the regime gives the sign/envelope the shadow agent
needs without re-deriving the band math.

## Handoff — how the live 3D frame consumes this

The original per-frame path is `VIBE_GameLogic_RunFrameLoop @0x4c0c50` →
`call VIBE_DayCycle_UpdateBrightness` with `eax = &qword_13CE852` (the world-time
record: `+4` = hour `u16`, `+6` = minute `dword`, `+0` = day). The full stateful
relight machine for that call already lives in `play::SessionAtmos::brightnessStep`
(`src/play/session_atmos.cpp`, 0x4b2504, 1:1) and is wired into the session
per-frame chain (`scene_recon2_orchestrator` / `wire_atmos_bridge`).

`render::ComputeSunState` is the **pure deterministic head** of that same chain,
exposed for consumers that want time→sun outputs without the stateful relight:

```cpp
// In CityView3D's per-frame body, from tick.worldTime():
const auto& wt = tick.worldTime();           // day @+0, hour @+4 (u16), minute @+6
render::SunState sun = render::ComputeSunState(wt.day, wt.hour, wt.minute);
// sun.band / sun.blend  -> SKY agent   (VIBE_SkyColor_BlendBandLighting @0x5b85e4)
// sun.brightness        -> LIGHT agent (ambient/diffuse band blend)
// sun.regime.raise      -> SHADOW agent (sun-direction sign: raise=0 day, 1 night)
// sun.regime.elevLo/Hi  -> SHADOW agent (elevation envelope; live pitch via
//                          render::SetSunHeight @0x4b24b0)
```

**Where in the frame order:** alongside the existing
`SessionAtmos::brightnessStep` call (the `0x4c0c68` arm of `RunFrameLoop`),
before `VIBE_Render_RenderMainViewFrame @0x5b6074`. **Gate:** the same arm gate
the original uses at `0x4c0c5b` (`(v54 & 1)==0 && (v54 & 4)!=0`). Shadow direction
gates additionally on the renderer's shadow option (sky/shadow agents own that
flag). `ComputeSunState` itself is side-effect-free and safe to call every frame.

The bind-site edit belongs to the CityView3D owner (not this agent); this doc is
the contract.

## Tests

`tests/unit/sun_daycycle_test.cpp` — 13 tests / 82 checks, all passing
(`build/sun_daycycle_test`):
- `BuildTimeTable` season-0 + season-wrap golden vectors (from `dword_4AD160`).
- `UpdateBrightness` full piecewise ramp incl. the noon plateau slope + u16 hour cast.
- `BrightnessToBand` trunc/frac golden vectors.
- `SunRegimeForBand` sunrise (bands 0-2, +) / sunset (bands 3-6, -) split + envelope
  constants (`0.3 / -0.3 / 0.6`).
- `ComputeSunState` composed outputs at noon / night / predawn + season-from-day wrap.

Existing related suites still cover the band/fog/light side (other agents):
`render_effects_test`, `render_fog_*`, `session_atmos_test`, `light_atmos_test`,
`render_leaves2_test` (`SetSunHeight`).

## Notes / deferrals (rule 8)

- The per-band **ambient/diffuse/sky colour curves** (`flt_13FD1B8..` stride 24,
  `dword_1408770`) are **per-scene runtime data loaded from the world file**, not
  static LUTs — they are populated by `VIBE_Scene_LoadFromStream @0x5e7e38` /
  `VIBE_SkyColor_SetBandGradient @0x5b8548` and consumed by
  `VIBE_SkyColor_BlendBandLighting @0x5b85e4` (sky/light agents' `skycolor_recon` /
  `sky.cpp`). They are intentionally NOT reconstructed as constants here; the driver
  supplies the **band index + blend** those functions index with. The static
  luminance weights (0.59/0.30/0.11 @0x628728) and fog scale (1.0 @0x64a018) live
  in the sky/light modules.
- `SetSunHeight`'s live RNG-sampled pitch is non-deterministic by design (matches
  the original); `ComputeSunState` reports the regime + envelope, not a sampled value.

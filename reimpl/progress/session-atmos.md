# Session atmosphere — day/night brightness + weather on the city frame

`play::SessionAtmos` (`src/play/session_atmos.{h,cpp}`) — the per-frame
day-cycle + weather state machine of the live session, reconstructed 1:1 and
driving the already-reconstructed `render::UpdateBrightness` / `render::Weather*`
cores. Owner: atmosphere slice; additive to `render/daycycle.*`, `render/weather.*`
(no changes were needed there — the cores were already faithful).

## Call-tree position (rule 7)

```
VIBE_GameLogic_RunFrameLoop          0x4c09a0   per frame:
  ├─ VIBE_Weather_UpdateSky          0x4c0040   → SessionAtmos::weatherStep
  ├─ VIBE_Weather_RenderAndThunder   0x4c05ac   → SessionAtmos::ThunderTick (decision core)
  └─ VIBE_DayCycle_UpdateBrightness  0x4b2504   → SessionAtmos::brightnessStep
VIBE_Command_ExSysMessage case 0     0x498ab4   new-day message:
  ├─ VIBE_Util_RandSeed              0x5cb8e0   srand(packet seed)  → crt::Srand(weatherSeedOrState)
  └─ VIBE_Sky_InitScene              0x4b1e94   weather-day regen   → SessionAtmos::initDay + BuildWeatherDay
VIBE_Render_BeginUniverseFrame       0x5b3900
  └─ VIBE_Render_UpdateSkyFlares     0x5ef7cc   layer fade/scroll step → SkyLayerStep
```

## THE RECOVERED APPLICATION MATH — how brightness reaches the pixels

`VIBE_DayCycle_UpdateBrightness @0x4b2504` (per frame, fed the world clock
`qword_13CE852`): brightness = the 0..600 piecewise curve
(`render::UpdateBrightness`, keyframes from `VIBE_DayCycle_BuildTimeTable
@0x4b2438`, called at session init `0x533edd` + day turn `0x52fa20`); then
`band = trunc(brightness * 0.01)` (dbl_61DD68), `blend = frac`, band mod 7
(latched in `dword_631DD0` / `flt_631DD4`). Application:

1. **`VIBE_SkyColor_BlendBandLighting @0x5b85e4 (band, blend, 1.0f, force)`**
   - sky-dome vertex colours: scene walk type 30 → `SkyColor_SetTimeOfDay
     0x5b83b0` → `InterpolateBand 0x5b80ac`;
   - **global ambient** `flt_64A074/78/7C` = lerp(row band → (band+1)%7) and
     luma `flt_64A070` (= `render::BlendBandLighting`, sky.cpp) — the base of
     the per-vertex light accumulation (`VIBE_Light_BuildObjectCache 0x5c8218`,
     light.h `LightVertex` floats [12..14]);
   - the **current 6-row fog table** `13FD170/174/178` = per-channel trunc-lerp
     of the per-band rows (`render::LerpChannelTrunc`);
   - **`VIBE_Light_RefreshAllObjects @0x5c886c (force)`** — scene-graph walk
     invalidating every object's vertex-light cache (`RemoveCacheEntry
     0x5c7da4`; force>1 → eager rebuild) + `Floor_BuildTilePolys 0x5bc45c`
     (terrain relight). The relit caches yield the 8-bit shade index the 16bpp
     rasterizer consumes through the 768-byte shade ramps — **this is the pixel
     application point**;
   - sun-flare sprite RGB from the per-band table `dword_1408770`.
2. **`VIBE_SkyColor_ApplyAmbientBlend @0x5b8b04 (0, 1, overcast)`** → fog rows
   0→1 lerp → `VIBE_Render_ConfigureFog @0x5ae384` (near·1.0, far·1.0, packed
   RGB; `flt_64A018 = 1.0`). `overcast = darkCloudLayer.fadeByte(+38) ·
   (1/255)` (`flt_61DD50`).
3. **Sun height** at band transitions: walk type 6 → `VIBE_Light_SetSunHeight
   @0x4b24b0`: day `0.3 + rand01·0.6`, sunset (band≥3, phase 1)
   `-0.3 - rand01·0.6` (dbl_61DD48/40/38) → light object +420.
4. **Lightning**: while `flashStart+flashDur > nowMs` (`dword_11BC0F8/FC` vs
   `dword_62EB38`) fog blends rows 0→**2** at t=1.0, restored via the
   `dword_631DD8` latch.
5. **Hysteresis** (state: `dword_11BC1E8` last brightness init −100,
   `flt_11BC1EC` transition, `dword_631D98` band latch, `byte_631D9C` sun
   phase): settled && |Δ|<10 → fog-only frame (dbl_61DD58 = 0.1); settled &&
   |Δ|<100 → force = `dword_62D4E8` (0) instead of 1 (dbl_61DD60 = 0.2).

**There is NO palette/gamma write and NO 16bpp framebuffer modulation** — the
day/night look is produced entirely by the vertex-light rebuild + fog + dome
colours **before** rasterisation. Therefore **no `Apply(fb16,…)` is shipped**
(a post-tint would be a cheap analogue, rule 8).

## Weather (recovered, all 1:1)

- **Day generation** `VIBE_Sky_InitScene @0x4b1e94` (`BuildWeatherDay`):
  seeded by the new-day sys-message seed (`VIBE_Util_RandSeed @0x5cb8e0` —
  the `weatherSeedOrState` argument). Mode: winter (season 3) roll
  `RandomModulo(100) >= rainProb/2 ? 1 : 2`, else 0. Hourly arc: gate
  `RandomModulo(100) < rainProb`, value `RandomModulo((u16)(10·rainProb))`,
  banded **parity-preserving** `(v&1)+999 / +499 / +149 / 0` at >700/>500/>250
  (disasm 0x4b21da..0x4b22cb). Thunder `RandomModulo(3)` only when mode 0 and
  arc≥999. Wind: angle walk `rand01·6.2831853` (dbl_61DD30, bit pattern
  0x401921FB53C8D4F1), drift ±0.175 (`RandNext() <= 0x3FFF`), per hour
  sin/cos, angle kept as float. `rainProb` = the per-city/season climate dword
  `dword_13CD7A8[189·city + season]` (runtime world data → config input).
- **Per frame** `VIBE_Weather_UpdateSky @0x4c0040` (`weatherStep`): hourly
  grows (snow `arc[h]`; rain `snow ? (arc[h]&1 ? arc[h]/5 : 0) : arc[h]`),
  intensity = peak-of-3 (`render::WeatherIntensity`), wind grows
  (`render::Snow/RainGrowAmount`), scroll magnitude
  (`render::CloudScrollMagnitude`), cloud texture swap on the MID layer fade
  byte (0 → new mid texture fade-in 255/1500ms; 0xFF → new FRONT texture, mid
  fades out 0/1500ms; `render::SelectCloudLayerIndex`), scroll speeds front
  ·0.75 / mid ·1.0 / dark ·1.5, **dark (overcast) layer fade target
  `(intensity<<6)/1000 + 96` over 1000ms, else 0 over 2500ms** (disasm
  0x4c057d / 0x4c03ab).
- **Layer fades** `VIBE_Sky_SetLayerFade @0x5efca8` + step from
  `VIBE_Render_UpdateSkyFlares @0x5ef7cc` (`SkyLayerSetFade` / `SkyLayerStep`):
  `progress += (1/duration)·dt` clamp 1; `fade = trunc(from +
  (target−from)·progress)` clamp 0xFF; `scrollPos = fmod(speed·dt + pos, 1)`;
  `SetLayerScrollSpeed @0x5efc78`: stored = `−speed · 1e-6` (flt_62C164).
- **Thunder** `VIBE_Weather_RenderAndThunder @0x4c05ac` (`ThunderTick`):
  thunder hour && `RandomModulo(300)==0` (the `==3 && RandomModulo(100)==0`
  arm kept 1:1 though dead) → `flashStart = nowMs`, `flashDur =
  RandomModulo(8)+8` ms. Sun-ray tail: hour 11..17, rain previous hour (the
  original reads `dword_11BC034[h]` — the dword *before* the arc = arc[h−1]),
  dry now, minute>30 → `RandomModulo(128) > 0x60 && !dword_62D564`.

## API

```cpp
play::BuildWeatherDay(WeatherDayState&, season, rainProb);   // 0x4b1e94 regen arm
play::SkyLayerSetFade / SkyLayerSetScrollSpeed / SkyLayerStep; // 0x5efca8/0x5efc78/0x5ef7cc

struct play::SessionAtmos {
  void Frame(const sim::GameTime&, u32 weatherSeedOrState, u32 nowMs); // + 2-arg overload
  bool ThunderTick(const sim::GameTime&, u32 nowMs);
  // config: climateRainProb[4], sunLightNodes, relightDisabled (dword_62D4E8),
  //         sunRayOption (dword_62D564), skyBands[7]/bandFog[7][6] (runtime rigs)
  // outputs: brightness, band, blend, lightingRebuilt/lightRebuilds/refreshForce,
  //          sunPhase/sunHeight/sunEvent, ambient (SkyAmbient),
  //          weatherIntensity/Category, peakIntensity, wind, grows, cloud scrolls,
  //          front/mid/dark SkyLayer, overcast, flash window, fogCurrent[6],
  //          fog (FogState), fogRowA/B + fogBlendT, sunRaysFired
};
```

## Wiring (rule 13)

`SessionAtmos` is the game-loop half. **The renderer-consumption link is now
CLOSED** (was: pending) — see `progress/atmos-lighting.md`. The brightness →
lighting-table rebuild is reconstructed 1:1 in `src/render/light_atmos.*`
(`VIBE_Light_RefreshAllObjects @0x5c886c`, `VIBE_Light_BuildObjectCache
@0x5c8218` core, `VIBE_Light_ApplyVertexShading @0x5c7f04`,
`VIBE_Light_RemoveCacheEntry @0x5c7da4`, the `0x5add1c` lazy-rebuild trigger
arms + budgets) and wired:

```
SessionAtmos::Frame  ──ambient/refreshForce/lightRebuilds──▶
  play::ApplyAtmosLightingFrame   (wire_atmos_bridge — one RefreshAllObjects
                                   per BlendBandLighting run, cursor-tracked)
    ├─ render::LightAtmosStoreAmbient        (flt_64A074/78/7C store half of 0x5b85e4)
    └─ render::LightAtmosRefreshAllObjects   (0x5c886c: serial bump + walks)
RealCityRenderer::Render(Options::atmosRelight)
    └─ render::LightAtmosEnsureNodeLit       (0x5add1c on-screen arm, 512-vert budget)
        └─ render::LightAtmosBuildObjectCache(0x5c8218: ambient → Vertex::lightIdx)
            └─ the flat/shaded raster interpolates the rebuilt byte → PIXELS
```

`tests/integration/atmos_lighting_itest.cpp` proves two brightness levels
(noon vs midnight through the real `SessionAtmos` curve) produce two DIFFERENT
deterministic frames through `real_city_render`. The fog state
(`fogCurrent`+`fog`) remains a read-out for the fog-consuming raster paths.

## Named gaps (rule 8 — no analogues shipped)

- ~~The per-object light cache walk~~ — **closed** by
  `src/render/light_atmos.*` (see `progress/atmos-lighting.md`). Still open
  here: the dome vertex colours (`InterpolateBand 0x5b80ac` over the runtime
  dome mesh) and `ConfigureFog`'s render-state application; SessionAtmos
  computes their exact inputs.
- Per-band gradient rows (`flt_13FD1B8`, 96 B/band), per-band fog rows and the
  sun-flare colour table `dword_1408770` are runtime tables populated at scene
  load (`SkyColor_StoreBandColors 0x5b83f0` from the .ed3 sky rig) —
  caller-supplied (`skyBands` / `bandFog`).
- `VIBE_Light_CreateSunRays @0x42dc7c` / `SetSunDirection @0x42dc40` visuals
  and the thunder voice sample (`Audio_StartVoiceSample`, volume
  `flt_634490·127.0`) — decisions 1:1, leaves elsewhere.
- Cloud texture records ("Sky_Schoen_01"… 76-byte entries; dims copied on
  swap) — identity tracked as (pool, variant), 1:1 with the name table.
- No `Apply(fb16,…)`: the original performs **no** framebuffer/palette/gamma
  modulation (see application math above).

## Tests

- `tests/unit/session_atmos_test.cpp` — 15 tests / 2216 checks: wind-circle
  constant bits; weather-day vs an independent CRT-LCG reference (draw order +
  banding + parity), winter mode roll, dry day; layer fade/scroll goldens;
  brightness 24h sweep goldens (season 0) vs `BrightnessToBand`; hysteresis
  (skip <10, rebuild ≥10, soft-window force 0); sunrise/sunset phase machine;
  overcast chain (159 target for intensity 999, exponential re-armed fade,
  fog t); weather cores cross-check; lightning flash + restore; thunder gate;
  sun-ray decision; ambient + fog-row application params; new-day reseed
  determinism.
- `tests/e2e/session_atmos_e2e_test.cpp` — 3 tests / 13415 checks: full
  1440-frame day sweep (curve/band/cores/fade-law per frame, sun events, day
  rollover incl. the band-6→0 sunrise quirk); 8-day season rotation (winter
  snow modes, arc septet invariant); thunderstorm flow (flash frames blend
  rows 0→2 at 1.0, rebuild suppressed, restore, overcast rises).

All pass (`build-agent-atmos`, GUILD_BACKEND=OFF).

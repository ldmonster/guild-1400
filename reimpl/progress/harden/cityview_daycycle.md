# Harden — cityview DAY-CYCLE / SUN / SKY (key: daycycle)

Scope: `src/render/daycycle.{h,cpp}`, `src/render/sky.{h,cpp}`,
`src/play/city_view3d.cpp::computeSunForFrame` (~L1344). All evidence is IDA
decompile/disasm/get_bytes of `gilde.exe`; addresses cited per claim.

## Per-function verdicts

### VIBE_DayCycle_BuildTimeTable @0x4b2438 — VERIFIED-1:1
- Decompile: 6 iterations over `dword_4AD160[6*season + i]`; `h = (char)(min/60)`
  @0x4b2462, `m = LOBYTE(min) - 60*h` stored/reloaded as a byte @0x4b2478/0x4b2484,
  result `3600*h + 60*m` @0x4b248f. Reimpl `BuildTimeTable` reproduces the byte
  split exactly (u8 casts, lossless for all table values).
- Season index comes from GetSeasonFromDay @0x58339c (`day % 4`) — matches
  `sim::SeasonFromDay` handoff.
- Layout note: the binary stores h/m/seconds per keyframe at stride 12
  (byte_631AB4 / byte_631AB5 / dword_631AB8, then +12 each -> 631AC4/631AD0/
  631ADC/631AE8/631AF4). The reimpl's contiguous `outSeconds[6]` is behaviorally
  identical (only the seconds dwords are consumed by 0x4b2504).

### Keyframe table dword_4AD160 — VERIFIED byte-for-byte (SEASONAL check done)
- `get_bytes @0x4ad160 size=96` decodes (little-endian dwords, minutes-of-day):
  - season 0: 390 510 630 1050 1170 1290
  - season 1: 300 420 540 1140 1260 1380
  - season 2: 420 540 660 1020 1140 1260
  - season 3: 480 600 690  960 1020 1140
- Exactly matches `kDayKeyframeMinutes[4][6]` in daycycle.cpp. The brief's
  "static values 14400/21600/43200/57600/64800/68400" are the season-1 row
  converted to seconds (300min=18000s? no — they are NOT this table; the
  recovered per-season minutes above are what 0x4b2438 actually indexes).
  All four seasons verified, not just one.

### VIBE_DayCycle_UpdateBrightness @0x4b2504 (brightness core) — VERIFIED-1:1
- `t = 3600*(u16)(a1+4) + 60*(dword)(a1+6)` @0x4b253b; piecewise:
  `<kf0 -> 0` @0x4b253f; `100*(t-kf0)/(kf1-kf0)` @0x4b256d;
  `+100` band @0x4b27a3; middle band `200*(t-kf2)/(kf3-kf2) + 200` @0x4b27d1
  (the "+200 with slope 200" band — confirmed); `+400` @0x4b2801;
  `+500` @0x4b2831; else 600 @0x4b283b. Reimpl `UpdateBrightness` is identical.

### Band select (writer of dword_631DD0 / flt_631DD4) — FIXED (1 divergence)
- Writer is UpdateBrightness itself, @0x4b2671..0x4b274b.
- kBandScale: `get_bytes @0x61dd68` = 0x3F847AE147AE147B = 0.01 (double). VERIFIED.
- `dword_631DD0 = v11 % 7` @0x4b274b (v11 = ConvertX-chopped `brightness*0.01`
  @0x4b267f). VERIFIED.
- **DIVERGENCE FOUND & FIXED** in `BrightnessToBand` (daycycle.cpp): the disasm
  does `fst dword [var_34]` @0x4b2677 — a 4-byte FLOAT store of the scaled
  brightness — and the blend is `fild whole; fsubr [var_34]; fstp flt_631DD4`
  @0x4b2727/0x4b272f/0x4b2733, i.e. the fraction is computed from the
  FLOAT-ROUNDED scaled value, not the 80-bit/double one. The reimpl computed
  `(float)(scaled - (double)whole)` from the unrounded double.
  - Old: `s.blend = (float)(scaled - (double)whole);`
  - New: `float v7 = (float)scaled; s.blend = (float)((double)v7 - (double)whole);`
  - Bit-level example: brightness=599 -> old 0x3F7D70A4 (0.99000001f),
    binary/new 0x3F7D70A0 (0.98999977f).
  - The truncation itself (`fistp` after ConvertX) consumes the unrounded x87
    register value — `TruncToward(scaled)` on the double is kept (correct).
  - Test pins unaffected (all blend pins use 1e-4 tolerance).

### VIBE_Light_SetSunHeight @0x4b24b0 + envelope constants — VERIFIED
- Decompile: `raise!=0 -> v3 = dbl_61DD40 - Rand()*dbl_61DD38` @0x4b24c5;
  `raise==0 -> v3 = Rand()*dbl_61DD38 + dbl_61DD48` @0x4b24ea; stored as float
  to lightObj+420 @0x4b24f5.
- `get_bytes`: dbl_61DD38 @0x61dd38 = 0x3FE3333333333333 = 0.6;
  dbl_61DD40 @0x61dd40 = 0xBFD3333333333333 = -0.3;
  dbl_61DD48 @0x61dd48 = 0x3FD3333333333333 = 0.3.
  Match `kSunSpan=0.6`, `kSunNightBase=0.3` (negated), `kSunDayBase=0.3`.
- The reimpl's deterministic envelope stand-in (regime + [lo,hi]) is as
  documented; the live pitch is RNG (VIBE_Math_RandomFloatScaled @0x58b910 =
  `(int)RandNext() * flt_62675C`, flt_62675C @0x62675c = 0x38000100 ≈ 1/32767).

### SunRegimeForBand (the 0x4b28f0 split) — VERIFIED as documented
- Binary: raise=1 walk @0x4b28f8 requires `v11 % 7 >= 3 && byte_631D9C == 1`;
  raise=0 walk @0x4b26ec fires when `v5 >= 0 && byte_631D9C == 0` (first band
  change), then `++byte_631D9C` @0x4b26f1. The band<3/band>=3 split at 0x4b28f0
  is real; the once-per-arm gating via byte_631D9C is stateful relight plumbing
  the reimpl's pure regime QUERY intentionally omits (documented in daycycle.h).
  No code change.

### VIBE_GameTime_GetSeasonFromDay @0x58339c — VERIFIED-1:1
- `return *a1 % 4;` — matches (day % 4).

### VIBE_SkyColor_BlendBandLighting @0x5b85e4 (ambient core) — VERIFIED-1:1
- Early-out `a1>=7 || t<0 || bits(t)>0x3F800000` @0x5b8604 — matches.
- Float-stored channel DIFFS (var stores @0x5b868b/0x5b86a5/0x5b86bd); R and B
  lerps float-stored (flt_64A074 @0x5b86ff, flt_64A07C @0x5b8735); G lerp NEVER
  stored before the scale (`(v10 + rowA.g) * a3` @0x5b8733) — the reimpl's
  documented G asymmetry is exactly what the binary does.
- Luma @0x5b876b..0x5b87b7: `v13*flt_62872C` (R*0.30) + `v12*flt_628728`
  (G*0.59) + `v15*flt_628730` (B*0.11), 80-bit scaled channels. Matches
  `gScaled*kSkyLumaG + rScaled*kSkyLumaR + bScaled*kSkyLumaB` (same association).
- Constants `get_bytes @0x628724 size=16`: flt_628724=0x3F000000 (0.5),
  flt_628728=0x3F170A3D (0.59f), flt_62872C=0x3E99999A (0.3f),
  flt_628730=0x3DE147AE (0.11f). All match sky.h.
- DOC FIX (comment only): sky.h's prose said "luma = b'*0.59 + g'*0.30 +
  r'*0.11" — channel labels were garbled; corrected to g'*0.59 + r'*0.30 +
  b'*0.11 per the disasm. Code and constants were already right.

### VIBE_SkyColor_ApplyScaledBlend @0x43f460 — VERIFIED-1:1
- `v = (double)brightness * flt_61752C` (get_bytes @0x61752c = 0x3C23D70A =
  0.01f); bits<1.0f && v<=0 -> 0 @0x43f486; bits>=1.0f -> 1.0 @0x43f49c; else v.
  `ScaledBlendAlpha` matches (signed-bits compare == value compare for the
  reachable cases, incl. negatives -> 0).

### VIBE_SkyColor_ApplyAmbientBlend @0x5b8b04 — VERIFIED-1:1
- Early-out `a1>=6 || a2>=6 || frac<0 || bits(frac)>0x3F800000` @0x5b8b27 — matches.
- Colour: per-byte `(double)(B-A)*frac + A` then ConvertX chop, B2 @0x5b8b9e,
  B1 @0x5b8bfb, B0 @0x5b8c50 — matches `lerpDiffTrunc` (the (B-A)*t+A form,
  NOT a*(1-t)+b*t; reimpl note is correct).
- Fog: diff kept in x87, lerp float-stored (near v18 @0x5b8bdf, far v16
  @0x5b8c3c), then * flt_64A018 (get_bytes @0x64a018 = 0x3F800000 = 1.0f).
  Matches `nearLerp/farLerp` float rounds + `kFogRangeScale`.
- ConfigureFog(near, far, packed) sets dword_649DD4 = packed clear colour;
  matches the RenderSky/skyColor model.

### city_view3d.cpp computeSunForFrame (~L1344) — VERIFIED (composition only)
- Composes ComputeSunState -> BlendBandLighting(scale=1.0, matching the
  1065353216 push @0x4b2751) -> ComputeSkyFog. No constants of its own carry
  addresses; the sun-direction azimuth sweep is the documented deterministic
  stand-in for the RNG pitch (unchanged). No edit.

## Files edited
- `src/render/daycycle.cpp` — BrightnessToBand blend float-round fix (0x4b2677/0x4b272f).
- `src/render/sky.h` — luma comment channel labels corrected (doc only).

## Tests built + run (targets only, no full-tree build) — ALL GREEN
| target | checks |
|---|---|
| sun_daycycle_test | 204 / 0 fail |
| sky_render_test | 372 / 0 fail |
| skycolor_recon_test | 73 / 0 fail |
| light_band_test | 2074 / 0 fail |
| light_atmos_test | 461 / 0 fail |
| frame_integration_wave6_test | 16 / 0 fail |
| frame_integration_wave6_e2e_test | 23 / 0 fail |
| frame_integration_wave7_e2e_test | 14 / 0 fail |
| session_atmos_test | 2216 / 0 fail |
| session_atmos_e2e_test | 13415 / 0 fail |
| atmos_lighting_itest | 37 / 0 fail |
| render_effects_test | 158 / 0 fail |
| render_effects_e2e_test | 288 / 0 fail |
| render_fog_itest | 19 / 0 fail |
| render_fog_e2e_test | 146 / 0 fail |
| wire_atmos_bridge_test | 47 / 0 fail |

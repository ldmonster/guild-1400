# Sky band table — wave-7 (W7-SKYBANDS)

Status: **DONE** — the rule-8 gap from wave-6 is closed. The 6/7-band per-scene
sky/fog gradient table that `BlendAmbientFog @0x5b8b04` and `BlendBandLighting
@0x5b85e4` index is now LOADED 1:1 from the world/scene file, so the real
time-of-day sky colours drive the clear instead of a fallback.

## The gap (wave-6, named rule-8)

`sky-render-wave6.md` / `sun-daycycle-wave6.md` reconstructed the band/fog BLEND
math but deferred the **runtime band table** itself: `flt_13FD1B8..` (band ambient)
and `dword_13FD1D0 / flt_13FD1D4 / flt_13FD1D8` (the per-band × 6 fog/shade
keyframes). Those are **per-scene data loaded from the world file**, not static
LUTs, and had not been surfaced — `city_view3d.cpp` (line ~1203) therefore used a
hand-rolled fallback colour with a `// gap` comment. This wave reconstructs the
LOAD + the band-table BUILD.

## Where the bands load from (the call tree, all 1:1, addresses verified)

```
VIBE_Scene_LoadFromStream @0x5e7e38          (the .ed3 / world-scene parser)
  reads the version-gated light-rig table @0x5e8011..0x5e8107 into the BSS globals:
    flt_13FD1B8/1BC/1C0[24*i] = ReadVec3      (band i AMBIENT RGB)         /*0x5e803d*/
    flt_13FD1C4/1C8/1CC[24*i] = ReadVec3      (band i SECONDARY RGB, >=0xB5)/*0x5e842f*/
    for kf v39 in [0,6):                       (6 fog/shade keyframes per band)
      dword_13FD1D0[24*i + 3*v39] = ReadDword  (packed colour: bytes B2/B1/B0)/*0x5e80b8*/
      flt_13FD1D4 [24*i + 3*v39] = ReadDword   (fog NEAR)                  /*0x5e80c5*/
      flt_13FD1D8 [24*i + 3*v39] = ReadDword   (fog FAR)                   /*0x5e80f4*/
  lightCount = (ver>=0x3A6C00BA)?7 : (ver>=0x3A6C00A5)?6 : 4               /*0x5e8005*/

VIBE_SkyColor_BlendBandLighting @0x5b85e4    (inner do-while @0x5b87c1..0x5b88c1)
  cross-fades band a1 -> band (a1+1)%7 by t over all 6 keyframes -> the scratch
    dword_13FD170 / flt_13FD174 / flt_13FD178 [3*i], i 0..5

VIBE_SkyColor_ApplyAmbientBlend @0x5b8b04    (already reconstructed wave-6)
  cross-fades two of those 6 scratch triples by the time-of-day fraction ->
    VIBE_Render_ConfigureFog @0x5ae384 sets dword_649DD4 = the framebuffer clear.
```

The BIO readers (`VIBE_Bio_ReadVec3 @0x5dc938`, `ReadDword @0x5dc894`,
`ReadDwordSwapArgs @0x5dc8b0`) are thin `VIBE_Vfs_ReadStream @0x4514ac` wrappers —
RAW little-endian, NO byte swap (the "SwapArgs" suffix is vestigial). Confirmed by
decompile. So a keyframe `id` dword is the packed colour as-stored; bytes B2/B1/B0
are the colour channels BlendBandLighting reads (`fild word` of each byte, 0..255).

`VIBE_Coord_ConvertX @0x5c6b08` = `fldcw` chop + `frndint` = truncate toward zero
(matches the existing `LerpChannelTrunc` / `particle::TruncToward`).

The fallback is `VIBE_Sky_InitDefaultColors @0x5efdc8` (it seeds `dword_1408770[0]=64`
and the `dword_1408774..84` sun-disc colour pointers) — that is the lens/sun-flare
disc colour cluster, NOT the band table; documented for completeness.

## Functions reconstructed / surfaced this wave

| Address | Original | Where | Status |
|---|---|---|---|
| 0x5e7e38 (light-rig portion) | VIBE_Scene_LoadFromStream | `sky.cpp LoadSkyBands` (adapter onto scene_load's parse) | **NEW** |
| 0x5b85e4 (inner do-while) | VIBE_SkyColor_BlendBandLighting (fog-scratch build) | `skycolor_recon.cpp SkyColor_BuildFogScratch` | **NEW** |
| 0x5b8b04 | VIBE_SkyColor_ApplyAmbientBlend | `sky.cpp BlendAmbientFog` (wave-6) — now fed real bands | reused |
| 0x5c6b08 | VIBE_Coord_ConvertX | trunc-toward-zero (verified again) | reused |

## The scene_load handoff (rule: I did NOT edit scene_load — it is owned elsewhere)

`src/render/scene_load.{h,cpp}` ALREADY parses the light-rig bytes 1:1 into
`render::SceneHeader::lights[]`:
* `light[i].pos`   = the band AMBIENT vec3 (flt_13FD1B8/1BC/1C0)
* `light[i].color` = the SECONDARY vec3   (flt_13FD1C4/1C8/1CC, `hasColor` when >=0xB5)
* `light[i].keyframes[k]` = `{id, a, b}` = the fog keyframe `{packed, near, far}`

My `LoadSkyBands(const SceneHeader&)` is the tiny accessor that maps that parsed
header onto the runtime `SkySceneTable` field-for-field, exactly as 0x5e7e38 fills
the engine globals. No change to scene_load; I only read its public `SceneHeader`.

## Public entry points

* `skycolor_recon.h`
  * `struct SkySceneTable` — the runtime band/keyframe table (13FD1B8.. / 13FD1D0..).
  * `struct SkyFogScratch` — the 6 output triples (13FD170/174/178).
  * `bool SkyColor_BuildFogScratch(table, band, t, out)` — the 0x5b85e4 inner loop:
    cross-fade band→(band+1)%7 over all 6 keyframes (per-channel chop-toward-zero).
* `sky.h`
  * `SkySceneTable LoadSkyBands(const SceneHeader&)` — scene_load → runtime table.
  * `SkyFog ComputeSkyFog(table, band, blend, fogA, fogB, frac)` — the full chain:
    BuildFogScratch (band/blend) → BlendAmbientFog (pick + cross-fade) → clear colour.

## EXACT CityView3D handoff (I do NOT edit bind sites — this is the contract)

`city_view3d.cpp` already (a) parses the scene header into a `render::SceneHeader`
at scene-open (line ~595) and (b) computes `render::SunState sun =
ComputeSunState(...)` per frame (line ~1182), and (c) has the `RenderSky` fill arm
(line ~1162). To close the gap, replace the fallback block (the `// gap` colour at
~1203-1218) with:

```cpp
// once, at scene open (alongside the existing ParseSceneHeader call ~595):
render::SceneHeader h;
if (render::ParseSceneHeader(r, h)) skyBands_ = render::LoadSkyBands(h);

// per frame, in the w6_ atmosphere update (replacing the fallback ~1203):
//   sun.band / sun.blend  come from ComputeSunState (already computed)
guild::render::SkyFog sky =
    guild::render::ComputeSkyFog(skyBands_, sun.band, sun.blend,
                                 /*fogA=*/0, /*fogB=*/0, /*frac=*/0.0f);
if (sky.applied) { w6_.skyColor = sky.color; w6_.skyDrawn = true; }
// (sky.near_/far_ are the ConfigureFog inputs the terrain/fog arm already consumes
//  via render::SpanFog — same near/far the original sends to VIBE_Render_ConfigureFog.)
```

`fogA/fogB/frac` select which of the 6 built scratch triples to cross-fade for the
clear. In the live engine the same `band/blend` split drives BlendBandLighting and
the clear comes from triple 0 of the result (the daytime fog/sky colour — verified:
band-3 keyframe-0 of every shipped city scene == the scene's header fogColor
0x0063A2E6 / near 4167 / far 6800). Using `fogA=fogB=0, frac=0` reproduces that.
`w6_.skyColor` is then converted to the surface format and filled by `RenderSky`
(unchanged wave-6 arm), BEFORE terrain — frame order matches 0x5b3900.

Storage note for the orchestrator: add a `render::SkySceneTable skyBands_;` member
to `CityView3D` (owned by that file), populated once at scene open. It is plain
data; no engine state.

## Tests (tests/unit/sky_render_test.cpp) — 18 tests, 147 checks, 0 failures

New wave-7 golden vectors sourced from the REAL shipped scene
`unpacked_resources/scenes/Staedte/stadt_MASTER.ed3` (tag 0x3A6C00BB, 7 bands;
band bytes extracted with the byte layout VIBE_Scene_LoadFromStream reads, then
cross-checked: band-3 keyframe-0 == the scene header fogColor/near/far):

* `LoadSkyBandsMapsHeader` — SceneHeader → SkySceneTable mapping (ambient + keyframe).
* `BuildFogScratchEndpoints` — blend=0 == band a's keyframes; blend=1 == band (a+1)'s.
* `BuildFogScratchMidpointTruncates` — band0→band1 kf0 cross-fade, golden bytes
  (B2 28.5→28, B1 61.5→61, B0 108) + near/far midpoints (chop-toward-zero).
* `BuildFogScratchRejectsOutOfRange` — band>=7 / t<0 / t>1 early-out (0x5b8604).
* `ComputeSkyFogTimeOfDayRamp` — full pipeline: daytime colour 0x0063A2E6 at
  band-3/blend-0, and a keyframe-0→2 cross-fade ramp (golden packed bytes + near/far).

Plus the 12 pre-existing wave-6 sky tests, all still green.

Build note (rule: verify MY targets): the full CMake `guild` lib had a transient
error in ANOTHER agent's concurrently-edited, untracked file
(`src/render/particle_integrate.cpp` — `PointEmitter` undeclared) — not in my
ownership. My owned files (`sky.cpp`, `skycolor_recon.cpp`) compile clean
(`-fsyntax-only` OK). The suite was built+run by linking my objects + their direct
deps (`scene_load.cpp`, `particle.cpp`, `crt/rand.cpp`) + the test — 18/18 pass.

## Files owned/changed
* `src/render/skycolor_recon.h` — added SkySceneTable / SkyFogKeyframe / SkyFogScratch
  + SkyColor_BuildFogScratch (the 0x5b85e4 inner fog-scratch build).
* `src/render/skycolor_recon.cpp` — implemented SkyColor_BuildFogScratch (1:1).
* `src/render/sky.h` — added LoadSkyBands + ComputeSkyFog + the handoff contract.
* `src/render/sky.cpp` — implemented LoadSkyBands (scene_load adapter) + ComputeSkyFog.
* `tests/unit/sky_render_test.cpp` — +5 wave-7 golden tests (real scene data).
* `progress/sky-bands-wave7.md` — this file.

## Not edited (read-only handoff, owned elsewhere)
* `src/render/scene_load.{h,cpp}` — parses the band bytes into SceneHeader; my
  LoadSkyBands consumes its public output (no edit).
* `src/render/sun_state.*`, `daycycle.*`, `scene_floor.*` — read for the handoff.
* `src/play/city_view3d.cpp` and other bind sites — handoff documented above, not edited.

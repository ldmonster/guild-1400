# Wave-23 — VIBE_Rain_UpdateDrop 1:1 reconstruction (W23-RAIN)

**Agent:** W23-RAIN · **Date:** 2026-06-16 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Owns: `src/render/rain.{h,cpp}` (extend; reuse the existing Rain_* + constants, no
ODR/redefine), `tests/unit/rain_updatedrop_wave23_test.cpp`, this doc.

Target (from the wave-22 coverage-audit over-count correction): **VIBE_Rain_UpdateDrop
@0x4294d4 (0x763 = 1891 bytes)** — the per-drop rain particle integrator. Wave-21
*claimed* it but the audit (wave-22) found the cited addresses `0x4294b6..c6` actually
belong to the sibling **Rain_GrowDropList @0x4292b8** (which ends exactly at 0x4294d4
where UpdateDrop starts). The body that *was* sitting in `rain.cpp` under the
`RainUpdateDrop` name was a guess copied from the snow sibling and was **NOT 1:1** —
this wave reconstructs it faithfully from the disassembly.

---

## What was wrong in the prior (wave-21) body, fixed this wave

Re-decompiled + paged the full disasm (`0x4294d4..0x429c34`, 499 instrs) and checked
every term against the bytes. The prior body had three substantive divergences:

| site | prior (wrong) | binary (0x4294d4) | fix |
|------|---------------|-------------------|-----|
| drift columns | `d·(m[1],m[4],m[7])` etc. | `drift0 = d·(m[0],m[3],m[6])·0.0025`, `drift1 = d·(m[1],m[4],m[7])·0.0025`, `drift2 = d·(m[2],m[5],m[8])·0.0025` (0x429602/0x42964e) | corrected column grouping |
| velocity X basis | `velX = cam.m[col]` (raw matrix column) | `wind = (windX,0,windZ)·camMatrix` (0x4296cb) — the per-drop wind vector transformed | now reads sys.windX/windZ |
| Z integration | `pz += dt·vZ + off2·0.5` | `pz += dt·vZ + drift2·2.0` (0x429a8f) — **off.z is a dead store**, Z uses drift·2.0 | corrected term + constant |
| drift source vectors | `cam.anchor - cam.eye` for both off & drift | off from `prevAnchor - cameraAnchor` (euler), drift from `prevEye - cameraEye`; both snapshots stored back into the rain system per frame (0x4295b2 / 0x429690) | added prevAnchor/prevEye state |

The projection numerator was already right (`num = (count·0.5 + maxHalf·⅓)·3.0`,
verified `fild [ecx]; fmul flt_611674` at 0x429b5d — `[ecx]` = drop count).

## Faithful structure (verified against disasm)

`__userpurge a1@<eax> = rain system, a2 = dt`. `ecx = a1`. The drop array is at
`[ecx+10h]`, 40-byte (10-float) stride. Rain-system fields used (from Create @0x429098,
0x60 bytes):

```
+0x00 count   +0x10 drops   +0x1C windX(1.0)  +0x20 windZ(0.0)
+0x40 prevAnchor[3] (init = cameraAnchor)   +0x50 prevEye[3] (init = cameraEye)
```

Per-system setup (runs before the count<=0 early-out; the prev snapshots happen
unconditionally):
- `euler = prevAnchor - cameraAnchor`; `MatrixFromEuler(euler) -> em`; `off_k = 2.5·em[8/9/10]`.
  (`off2` is computed then never read — dead store, kept for fidelity.)
- `prevAnchor := cameraAnchor`.
- `d = prevEye - cameraEye`; `drift_k = (d·camCol_k)·0.0025`.
- `prevEye := cameraEye`.
- `wind = (windX,0,windZ)·camMatrix`; `grav = (0,-0.75,0)·camMatrix`.
- viewport half-extents `(x1-x0)>>1`, `(y1-y0)>>1` (integer sar); `maxHalf = max`;
  `cx = x0 + halfW`, `cy = y0 + halfH`; `depthBias = maxHalf·(1/3)`.

Per drop (`vel = d1·grav + d0·wind`):
- `p_axis += dt·vel + drift + off` (X,Y), `pz += dt·vZ + drift2·2.0` (Z asymmetry).
- reset to 0 if `|p| > 1000` (`< -1000 || > 1000`, the original also Sprintf-logged
  "Drop %li: X is %f" — a debug side effect with no observable state change; dropped).
- wrap into `[-1,1)`: `while p < -1: p += 2`; `while p >= 1: p += -2`.
- head proj: `num = (count·0.5 + depthBias)·3`; `proj = num/(pz·2+3)`;
  `sx = px·proj + cx`, `sy = cy - py·proj`.
- tail proj (streak): `w = vel·size`; `proj2 = num/(3 + 2·(pz + wZ))`;
  `sx2 = cx + (px+wX)·proj2`, `sy2 = cy - proj2·(py+wY)`.

## Constants (get_bytes @0x611670.., bit-exact)

| sym | addr | bytes | value |
|-----|------|-------|-------|
| flt_611670 | 0x611670 | 0A D7 23 3B | 0.0025 (dt scale) |
| flt_611674 | 0x611674 | 00 00 00 3F | 0.5 (count weight) |
| flt_611678 | 0x611678 | 00 00 00 40 | 2.0 (z coeff / tail) |
| flt_61167C | 0x61167C | 00 00 40 40 | 3.0 (denom bias) |
| flt_611680 | 0x611680 | AB AA AA 3E | 0.33333334 (depthBias) |
| dbl_611684 | 0x611684 | …C08F4000 | -1000.0 |
| dbl_61168C | 0x61168C | …408F4000 | 1000.0 |
| dbl_611694/9C/A4 | | | -1.0 / 2.0 / -2.0 (unit-cube wrap) |
| gravity | imm 0xBF400000 @0x429717 | | -0.75 |
| anchor Z | imm 0x40200000 @0x4294f7 | | 2.5 |

## Float->int audit (ConvertX truncation concern)

There is **no float->int conversion in the integrator**. The only x87 fild/fistp are:
- `fild [ecx]` (the int drop count -> double, for the numerator), and
- `fild dword_13ECE58/5C` (int viewport extents -> float for cx/cy).

The half-extent `>>1` is the integer `sar eax, 1` (done before any int->float). So
**VIBE_Coord_ConvertX @0x5c6b08 (RC=11, truncate-toward-zero) is NOT reached from this
function** — it applies only to the render colour pack `RainStreakDiffuse` (0x429c38),
which already uses truncation (`ConvertXTrunc`) and is unchanged this wave. All position
accumulations are float stores of double intermediates (`fst dword ptr`), modelled as
`(float)(double-expr)` — matching the x87 double-rounding the binary performs.

## Reconstructed vs genuine leaves

| addr | name | status |
|------|------|--------|
| 0x4294d4 | VIBE_Rain_UpdateDrop | **reconstructed this wave** (rain.cpp `RainUpdateDrop`) |
| 0x5cb1bc | VIBE_Math_MatrixFromEuler | already reconstructed (`util::MatrixFromEuler`, util/matrix.cpp) — reused |
| 0x5cba00 | VIBE_Crt_Sprintf_0 | debug-log side effect inside the >1000 reset branch; no observable state change, dropped (rule-8-clean: documented, not faked) |

`VIBE_Math_MatrixFromEuler` is the only genuine callee with engine math; it was already
a faithful leaf. The Sprintf calls are pure debug logging (the position is set to 0.0
identically with or without them).

## Wiring (rule 13)

- **Caller:** `VIBE_Rain_Render @0x429c38` calls UpdateDrop @0x429cdb (the only xref).
  In `src/`, the live per-frame rain path is `CityView3D::drawWeatherOverlay`
  (`src/play/city_view3d.cpp:2618`) which calls `render::RainUpdateDrop(weather_->rain,
  dt, scam, vp)` then `RainStreakDiffuse` + `RainRenderToSurface`. **Signature unchanged**
  — the wind/prev state now lives on `RainSystem` (Create-default windX=1.0, windZ=0.0;
  prev fields snapshot per frame). Added a comment at the call site documenting the new
  carried state and that this overlay leaves wind at the Create defaults (the live engine
  feeds windX/windZ via Rain_Render's time-interp, not modelled in the headless overlay).
- **GPU emit boundary:** the original submits a D3D LINELIST of the per-drop head->tail
  segments; the present path rasterises the same segments onto the software Surface via
  `RainRenderToSurface` (rule-3 boundary, already in place, unchanged).

## Tests — `tests/unit/rain_updatedrop_wave23_test.cpp`

7 cases, **117 checks, all pass**. Includes an independent term-for-term oracle
(`RefUpdateDrop`) kept separate from production so the trajectory pin is a true
cross-check, plus targeted pins for each prior bug.

| suite.case | pins |
|------------|------|
| rain23_updatedrop.matches_reference_tilted_multiframe | production == independent oracle over 4 frames on a tilted/moving camera; prevAnchor/prevEye in lockstep |
| rain23_updatedrop.snapshots_camera_each_frame | prevAnchor==cameraAnchor, prevEye==cameraEye after a frame (0x4295b2/0x429690) |
| rain23_updatedrop.z_uses_drift2_times_two_not_off | Z = dt·vZ + drift2·2.0 + pz; explicitly rejects the old off2·0.5 path |
| rain23_updatedrop.velocity_uses_wind_transform | different windX/windZ -> divergent trajectory; matches the (windX,0,windZ)·matrix oracle |
| rain23_updatedrop.clamp_then_wrap | >1000/<-1000 reset to 0, all axes wrapped into [-1,1) |
| rain23_updatedrop.projection_numerator_count_half | head sx/sy via num=(count·0.5+bias)·3; rejects the 0.0025-weight value |
| rain23_updatedrop.deterministic_golden_trajectory | fixed seed + camera + 6 frames -> byte-identical drop field |

Pre-existing wave-6 (`rain_weather_wave6_test`, 61 checks) and wave-21
(`weather_wave21_test`, 91 checks) rain/snow/morph tests still pass after the rewrite.

## Build note

My objects (`src/render/rain.cpp.o`, the new test object) compile clean and the test
**links + runs green** (117/117) against `libguild.a`. The full `cmake` reconfigure
surfaces an **unrelated** sibling breakage: untracked wave-22 files
`src/world/privilege_panels_b.h` / `wire_privilege_panels_b.h` have a
`PrivilegePanelBHooks` forward-declaration-ordering error that fails `wiring.cpp`
(`?? src/world/privilege_panels_b.h`, not in this agent's ownership). Not caused by and
not fixable within this wave's scope; the rain work itself is green.

*All addresses are gilde.exe (imagebase 0x400000). Snapshot 2026-06-16.*

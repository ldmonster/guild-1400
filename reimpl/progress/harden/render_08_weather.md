# Hardening report — render_08 weather

Scope: `src/render/weather.cpp`, `src/render/weather_recon.cpp` (+ headers + the
three unit tests). Method: decompile + disasm each provenanced function, diff
line-for-line against the binary (DISASM wins). Constants confirmed byte-for-byte
with `get_bytes`. Every float->int and div/operand-order checked.

Result: **all functions VERIFIED-1:1. 0 FIXED, 0 BOUNDARY.** (The audio dispatch
and sky/particle subsystem calls inside the two parent functions are documented in
the headers as out-of-scope integration coupling — the *arithmetic* core is what is
reconstructed and verified here.)

---

## weather.cpp — gilde.exe 0x4c0040 VIBE_Weather_UpdateSky

### Constants (get_bytes @0x61E4B0, little-endian doubles)
| addr | bytes | value | source |
|------|-------|-------|--------|
| 0x61E4B0 | 00..00 40 | 2.0   | kWxSnowRate ✓ |
| 0x61E4B8 | 00..E0 3F | 0.5   | kWxRainRate ✓ |
| 0x61E4C0 | 00..C0 3F | 0.125 | kWxScroll ✓ |
| 0x61E4C8 | 00..E8 3F | 0.75  | kWxScrollFast ✓ |
| 0x61E4D0 | 00..F8 3F | 1.5   | kWxScrollBack ✓ |

### WeatherIntensity — VERIFIED-1:1
disasm 0x4c00f0..0x4c0127: `v0 = arc[(h+23)%24]`; `if (v0 <= arc[h]) v0 = arc[h]`;
`v1 = (h+1)%24`; `if (v0 <= arc[v1]) v2 = arc[v1] else v2 = v0`. Modulo via
`idiv ebx(=24)` with `sar edx,1Fh` (matches `% 24` on non-negative h). `<=`
(`jle`) comparisons confirmed. Peak-of-3. ✓

### SnowGrowAmount / RainGrowAmount — VERIFIED-1:1 (subtle: windX, not windY)
disasm 0x4c0135 (snow) / 0x4c0193 (rain): the chain first loads `var_28`
(=dword_11BC160 = **windY**), multiplies by rate*intensity, calls ConvertX, fistp
to var_1C — then that result is **overwritten**. At 0x4c015a/0x4c01b8 it loads
`var_20` (=dword_11BC100 = **windX**), `fchs`, `fmulp`, ConvertX, `fistp var_1C`,
and *that* is the value passed to GrowFlakeList/GrowDropList. So the live value is
`trunc(rate * -windX * intensity)`. Source uses windX. ✓
ConvertX (0x5c6b08): `fstcw; set RC=0x1F high byte -> round-toward-zero; frndint;
restore` = truncate-toward-zero in place. Matches `TruncToward`. ✓
Operand order `(rate * -windX) * intensity` (left-assoc) matches `fmul` order. ✓
Stores confirm var_20=dword_11BC100=windX (0x4c00c1), var_28=dword_11BC160=windY
(0x4c00d2). Both grow amounts use windX.

### CloudScrollMagnitude — VERIFIED-1:1
disasm 0x4c01f4: `windX^2 + windY^2`, `fsqrt`, `* (intensity+150)` (lea ecx+96h),
`* dbl_61E4C0(0.125)`, fstp var_24. Order `sqrt(...) * (I+150) * 0.125`. ✓

### CategoryFor — VERIFIED-1:1
disasm 0x4c022e `cmp ecx,32h; jge` (>=50) then `>=150` (0x4c0231). heavy>=150,
medium>=50, else fair. ✓

### CloudVariantCount / SelectCloudLayerIndex — VERIFIED-1:1
disasm: fair pool `RandomModulo(4)` @0x4c023e, medium `(3)` @0x4c043d, heavy `(2)`
@0x4c048e — read as `(u16)ax`. Reroll: `if (!StrCmpNoCase(current, pool[idx])) ++idx`
(StrCmpNoCase==0 on match -> `!0` -> ++). idx==current -> ++idx. Variant counts
4/3/2. Single RandomModulo draw per selection. ✓

### WeatherUpdate (driver) — VERIFIED-1:1
- hour = WORD2(qword_13CE852) -> dword_11BC1C4 (0x4c0057). ✓
- rain gate: `test byte ptr dword_11BC038[edx],1` (0x4c0085) = code&1. ✓
- rainSpawn: `mov ecx,5; sar edx,1Fh; idiv ecx` (0x4c009a..0x4c00a2) = **signed**
  code/5. ✓
- rainSpawnNoSnow = raw code (0x4c0416). ✓
- wind reads dword_11BC100[h] (windX), dword_11BC160[h] (windY). ✓
- scrollFast = mag * dbl_61E4C8(0.75) (0x4c0374 `v25 = v28 * dbl_61E4C8`). ✓
- scrollBack = mag * dbl_61E4D0(1.5) (0x4c03c7 `v26 = v28 * dbl_61E4D0`). ✓
- struct/global offsets dword_11BC038/100/160 confirmed in disasm. ✓

---

## weather_recon.cpp

### Constants (get_bytes, little-endian floats) — all VERIFIED
| addr | bytes | value | source |
|------|-------|-------|--------|
| 0x625A3C | 00 80 22 45 | 2600.0      | kF625A3C ✓ |
| 0x625A40 | CD CC B1 42 | 88.9000015  | kF625A40 ✓ |
| 0x625A44 | 00 29 BD 48 | 387400.0    | kF625A44 ✓ |
| 0x625A48 | 00 00 FE 42 | 127.0       | kF625A48 ✓ |
| 0x625A4C | 00 2B 5E 49 | 910000.0    | kF625A4C ✓ |
| 0x625A50 | 34 A6 C9 39 | 0.000384615 | kF625A50 ✓ |
| 0x625A54 | 00 B1 9E 49 | 1300000.0   | kF625A54 ✓ |
Inline 0.69999999 = 0.7f (0x3F333333). ✓

### 0x57f190 VIBE_Weather_UpdateAmbientLoops — all tiers VERIFIED-1:1

Setup (head): `v89 = flt_625A3C - v93` (2600 - height_delta); `v65 = v89 *
flt_625A40` (inv_height * 88.9). ✓

Heavy (>=999) @0x57f32a:
- main `v77 = v65 * flt_625A50`. ✓
- near `v82 = 0.7 * flt_625A48 * v93 * flt_625A50`. ✓
- `v63 = 2600 - v93`; far `v76 = 0.7 * flt_625A48 * v63 * flt_625A50`. ✓

Mid (499..999) @0x57f66e: `v20 = (double)(v7-499)` -> t.
- a `v86 = v20 * flt_625A40 * v89 / flt_625A54`. ✓ (disasm 0x57f66e-0x57f6a1:
  fild; fst t(var_34); fmul 88.9; fmul v89; fdiv 1300000; fstp — chain stays
  x87 80-bit, spills to float ONCE. Source single-expr matches.)
- denom `v79 = 500*flt_625A3C`. b `v62 = 0.7*127*t*v93`, `(int)(v62/v79)`. ✓
- `v59 = 500-t`, `v60 = 2600-v93`, `v68 = 500*2600`; c `v72 = 0.7*127*v60*v59`,
  c = v72/v68 (inv*remain order). ✓

Light (149..499) @0x57fa10: `v31 = (double)(v7-149)` -> t.
- a `v80 = v31 * flt_625A40 * v89 / flt_625A4C`. ✓
- denom `v87 = 350*flt_625A3C`. b `v83 = 0.7*127*v92(t)*v93`. ✓
- `v64 = 350-t`, `v55 = 2600-v93`, `v73 = 350*2600`; c `v84 = 0.7*127*v55*v64`,
  c = v84/v73 (inv*remain order). ✓

Drizzle (<149) @0x57fda4: `v90 = (float)intensity` -> rate.
- main `v53 = v65 * v90 / flt_625A44`. ✓
- denom `v70 = 149*flt_625A3C`. b `v88 = 0.7*127*v93*v90`, b = v88/v70. ✓
- `v58 = 2600-v93`, `v78 = flt_625A3C*149` (IEEE-754 mult commutative -> bit-equal
  to 149*2600); c `v69 = 0.7*127*v58*v90`, c = v69/v78. ✓

Note: each pan is computed entirely on the x87 stack (80-bit), spilled to a single
32-bit float, then ConvertX-truncated before handing to the audio mixer. The source
keeps the whole sub-expression in one float statement, matching the single spill
point. (Documented x87 vs SSE caveat: under FLT_EVAL_METHOD=0 each op rounds to
float; the final integer truncation absorbs the sub-ULP difference. Acceptable per
the existing header note.)

### 0x505df4 VIBE_Weather_ApplySeasonalMeshes — WeatherSeasonSuffix VERIFIED-1:1
switch on GetSeasonFromDay():
- 3 (winter) -> sprintf "%s_SNOW" (0x505ee7). ✓
- 2 (autumn) -> "%s_HERBST" (0x506023). ✓
- 1 (summer) -> uses base name v55 directly, no suffix => "" (0x506224 arm). ✓
- 0/default (spring) -> "%s_FRUEHLING" (0x506184). ✓

---

## Tests
Build: `cmake --build build --target weather_recon_test weather_wave21_test
rain_weather_wave6_test -j` -> all built.
Run: `GUILD_GAME_DIR=... ctest -R "weather|rain" --output-on-failure` ->
**100% passed (35/35)**. Owned targets: weather_recon_test (#707),
weather_wave21_test (#708), rain_weather_wave6_test (#449) — all PASS.

## Counts
VERIFIED-1:1: 13 functions (WeatherIntensity, SnowGrowAmount, RainGrowAmount,
CloudScrollMagnitude, CategoryFor, CloudVariantCount, SelectCloudLayerIndex,
WeatherUpdate, WeatherRainMixSetup, WeatherRainHeavyPans, WeatherRainMidPans,
WeatherRainLightPans, WeatherRainDrizzlePans) + WeatherSeasonSuffix = 14.
FIXED: 0.  BOUNDARY: 0.

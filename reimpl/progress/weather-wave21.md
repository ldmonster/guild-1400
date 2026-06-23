# Wave-21 — Weather particles + morph-anim builder (W21-WEATHER)

**Agent:** W21-WEATHER · **Date:** 2026-06-16 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Owns: `src/render/rain.cpp` (rain), new `src/render/snow_update.{h,cpp}`, new
`src/render/anim_morph.{h,cpp}`, `tests/unit/weather_wave21_test.cpp`, this doc.

Targets from the wave-20 coverage audit "NEW logic" list:

| addr | bytes | name | status |
|------|------:|------|--------|
| 0x4294d4 | 1891 | VIBE_Rain_UpdateDrop      | already reconstructed (rain.cpp) — **bug fixed** (projection numerator) |
| 0x42a644 | 1329 | VIBE_Snow_UpdateFlake     | already reconstructed (snow.cpp) — verified 1:1, re-exported + golden-pinned |
| 0x5cf150 | 3026 | VIBE_Anim_CreateMorphAnim | **NEW reconstruction** (anim_morph.cpp) |

---

## VIBE_Rain_UpdateDrop @0x4294d4 — verification + bug fix

The integrator was already faithfully reconstructed in `src/render/rain.cpp`
(`RainUpdateDrop`). Re-decompiled and re-verified the full body against the
get_bytes constants @0x611670..0x6116A4:

| sym | addr | value | role |
|-----|------|-------|------|
| flt_611670 | 0x611670 | 0.0025 | dt/drift scale |
| flt_611674 | 0x611674 | **0.5** | projection numerator `count` weight |
| flt_611678 | 0x611678 | 2.0 | pz depth coeff |
| flt_61167C | 0x61167C | 3.0 | denom bias / numerator ×3 |
| flt_611680 | 0x611680 | 0.333… | depthBias = maxHalf·⅓ |
| dbl_611684/8C | | ∓1000.0 | clamp / reset-to-0 |
| dbl_611694/9C/A4 | | -1.0 / 2.0 / -2.0 | unit-cube wrap |
| gravity | | (0,-0.75,0) | per-frame gravity |
| anchor | | (0,0,2.5) | billboard offset |

**Bug found + fixed (this wave):** the head-point projection numerator was
`((double)n * kDtScale + depthBias) * kThree` but the decompile
(`v25 = ((double)*(int*)v2 * flt_611674 + v63) * flt_61167C`, 0x429b83) uses
**flt_611674 = 0.5**, not 0.0025. Corrected to `((double)n * kHalf + depthBias) * kThree`.
The prior `rain_weather_wave6_test` only asserted finiteness, so the error was
unconstrained; the new `weather21_rain.projection_numerator_uses_half` test pins
the exact `sx/sy` against the recomputed numerator and explicitly rejects the old
buggy value. The existing wave-6 rain test still passes (61 checks).

Rest of the projection (tail point, velocity-scaled streak, `proj2 = num /
(3 + 2*(pz+wZ))`, sx2/sy2) verified equal to 0x429bf7..0x429c18.

## VIBE_Snow_UpdateFlake @0x42a644 — verified, re-exported

Re-decompiled and confirmed the existing `SnowUpdateFlake` in `src/render/snow.cpp`
is 1:1 (constants @0x6117F0..0x61181C: dt 0.0025, z-coeff 2.0, tail 13.5, denom 3.0,
gravity (0,-1,0), anchor (0,0,1.9); wrap -1.0/2.0/-2.0 and the raw-float-bit z upper
wrap `>= 1065353216` == ≥1.0f, increment -2.0f). Projection:
`proj = (maxHalf·3)/(pz·2+3)`, `sx=px·proj+cx`, `sy=cy-py·proj`,
`tail=((1-pz)·13.5+1)·size`, `sx2=sx+tail`, `sy2=sy+tail`.

To honor the ODR rule (snow.cpp already defines it), `snow_update.{h,cpp}` does
**not** redefine the integrator — it re-exports the existing symbol and adds the
deterministic `SnowGoldenStep` driver (seed + N integrate ticks) used by the
trajectory pin test.

## VIBE_Anim_CreateMorphAnim @0x5cf150 — NEW (anim_morph.cpp)

The morph-stream builder. Allocates a 364-byte (0x16C) record + a
192·frameStride frame buffer (frameStride fixed = 2, set before the alloc) and
fills it from: src anim node `a1` (point list +64, count +68, transform +80..+100),
dest `a2` (bone-name table +64, bone count +324), control float array `a4`
(`a4[45]`/`a4[47]` gate the WPoints/Points blobs; `a4[2..7]`→+8..+28,
`a4[8..13]`→+32..+52, per-bone `a4[15+6i..20+6i]`→+84+24i), and a morph endpoint
that is either the primary `a6` (≠0) or the alt `a3`/`a5` (when a6==0).

Branches reconstructed 1:1:
- **early reject** (0x5cf18b): `!a4 || !a1 || (!a6 && (!a3||!a5)) || a8<=1`.
- **bone match** (0x5cf52d / 0x5cfaf6): per dest bone, scan the endpoint's 4 bone
  slots (64-byte name stride); on `VIBE_Util_StrCmp==0` copy 6 matrix dwords →
  +276+24·destBone.
- **transform deltas** +224..+244: prim path = `(a6+80..)-(a1+80..)` with the
  deliberate +228=0 overwrite (0x5cf65f); alt path = verbatim `a5[8..13]`.
- **Points1** (a4[47]): per-point 3-float `(target-source)` delta → +380.
- **WPoints1** (a4[45]) — *the real motion/blend math* (0x5cf797..0x5cfa69):
  pass 1 computes per-point delta and tracks component min/max with the binary's
  compare-then-select idiom (NaN keeps the accumulator); bounds stored
  min→+200/+204/+208 and scale `range·(1/255)`→+212/+216/+220 (flt_628E60=1/255);
  pass 2 re-quantizes each delta to a byte `b = trunc((delta-min)·255/range)`
  (`MorphQuantizeByte`), truncated toward zero by VIBE_Coord_ConvertX @0x5c6b08
  (RC=11), into the 3·numPoints WPoints1 blob.

**ConvertX rounding:** verified ConvertX TRUNCATES (the three ConvertX call sites
at 0x5cf9e6/ed/fe and the explicit `(int)` casts all reduce to truncation toward
zero) — `MorphQuantizeByte` uses `(int)` truncation accordingly.

**Constants** (get_bytes): flt_628E60 = 0x3B808081 = 1/255; flt_628E64 = 0x437F0000
= 255.0; min/max init sentinels ∓1e10.

### Modelling boundary (rule 8)

The raw engine struct shapes (`a1`/`a2`/`a3`/`a5`/`a6`) and the 364-byte record
are modelled as structured views + byte buffers so the kernel is byte-faithful and
testable in isolation; every frame-buffer write keeps the binary's byte offset
(annotated with its decompile address). The two genuine out-of-tree effects —
`VIBE_Memory_AllocDebug`/`FreeDebug` (host heap) and the live anim-list insertion
into `dword_13FC8E4` (+352 next ptr, sentinel `&unk_13FC780`) — are documented; the
record is returned to the caller for list insertion.

---

## Wiring (rule 13)

- **Rain / Snow:** integrators are already wired through the weather render path
  (`weather.cpp` / the per-frame weather caller). The numerator fix and the snow
  re-export change no call sites.
- **CreateMorphAnim handoff:** the live caller is `VIBE_Character_CheckAniMorph`
  @0x403764 (reconstructed in `src/sim/character_recon5_morph.cpp`, line ~199) via
  the `MorphHooks.createMorphAnim` function-pointer hook (also reached from
  `VIBE_Character_DetachMorphAni` @0x4035d0). The hook's prototype is the raw
  engine-pointer form `(int a1, int v31, int a3, void* keyA, void* keyB, int a6,
  const char* name, int frames)`; binding it to the structured
  `render::CreateMorphAnim` requires the raw TChar/anim-node field map (a1+64/+68/
  +80, a2+324, a4[45/47], a6+64/+80..) which is owned by the sim/character module
  that defines `MorphHooks`. **One-line handoff:** the character module installs a
  thin adapter in its `MorphHooks` that reads those raw offsets into the
  `Morph{SrcNode,DestNode,Control,Endpoint}` views and calls
  `render::CreateMorphAnim`, then links the returned record into `dword_13FC8E4`.
  (Not done here — out of this agent's ownership; `anim_morph.{h,cpp}` provides the
  callable body.)

---

## Tests — `tests/unit/weather_wave21_test.cpp`

7 cases, 91 checks, all pass (standalone-linked; the normal `build/` shared lib is
currently red due to **unrelated** untracked files from sibling wave-21 agents —
`src/gui/recruit_office.cpp` and `src/sim/ai_meister_bank.cpp` — not in this
agent's ownership; my two objects `anim_morph.cpp.o` / `snow_update.cpp.o` compile
clean).

| suite.case | pins |
|------------|------|
| weather21_rain.projection_numerator_uses_half | exact sx/sy via num=0.5 weight; rejects old buggy 0.0025 |
| weather21_rain.deterministic_seed_repeats | same RNG seed → byte-identical drop field |
| weather21_snow.deterministic_trajectory | seed + 5 steps → byte-identical flake field |
| weather21_snow.projection_tail_relation | sx2==sx+tail, sy2==sy+tail; px,py wrapped [-1,1) |
| weather21_morph.quantize_byte_truncates | 0/127/255 boundary cases incl. negative min |
| weather21_morph.create_reject_paths | frames≤1, !prim&&!alt, !name → nullptr |
| weather21_morph.create_quantized_blob_and_layout | full layout: +4/+192/+196 counts, bounds +200..+220, hand-computed WPoints1 bytes (0/127/255/102…), Points1 floats, xform deltas +224.. with +228=0 |

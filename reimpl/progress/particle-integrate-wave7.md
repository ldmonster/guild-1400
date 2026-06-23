# Particle integrate — Wave-7 (W7-PARTINT)

Closes the rule-8 gap left by wave-6 (`progress/particle-render-wave6.md` §DEFERRED):
the per-frame INTEGRATORS for the three d3_par particle SYSTEM TYPES were named but
not reconstructed. This wave reconstructs all three 1:1.

## Reconstructed (rule 1, 7)

| addr | function | type | role |
|------|----------|------|------|
| 0x5e1e0c | `VIBE_Particle_UpdatePoints` | 0 | point / sprite particles |
| 0x5e2814 | `VIBE_Particle_UpdatePolys`  | 1 | poly / ribbon particles  |
| 0x5e32c0 | `VIBE_Particle_UpdateLens`   | 2 | lens-flare / glow        |

Dispatch confirmed at `VIBE_Particle_SpawnSystemByType @0x5e3ae0` (template byte
0/1/2), which installs the fn pointer into the 0x310 system block at +0x304 via
`VIBE_Particle_AllocSystem @0x5e1000`. Slot array base = sys+40, count = sys+208,
stride 84 (0x54) — the SAME 84-byte record `fx_recon3_particle_render` renders
(+56/60/64 center, +72 size, +76..79 rgba, +80 frame, +81 flags). **The integrate
computes that center + the +79 alpha byte each frame, so it directly feeds
`render_system_to_surface`.**

### Callees (rule 7, all already reconstructed elsewhere — reused via the helpers)
- `VIBE_Coord_ConvertX` 0x5c6b08 — fpu round-toward-zero → `TruncToward` = `(int)x`.
- `VIBE_Math_Fmod` 0x5d3fb2 — fmodl (x87 fprem loop) → `std::fmod` (bit-identical).
- `VIBE_Math_VectorNormalize` 0x5cb148 — normalize 3-vec / zero if |v|==0.
- `VIBE_Util_RandNext` 0x5cb8bc — `crt::RandNext` (15-bit LCG, [0,32767]).

### Constants (get_bytes, bit-exact)
`flt_62BA94/B4/D4 = 3.0518509e-05 (1/32767)`, `…98/B8/D8 = 2.0`, `…9C/BC/DC = pi`,
`dbl_62BAA4/AC4/AE4 = pi`, `…AC/CC/EC = 255.0`, Points bias `flt_62BAB0 = -0.5`,
Polys bias `flt_62BAD0 = -0.5`, Lens bias `flt_62BAF0 = -1.0`.

## Files owned
- `src/render/particle_integrate.{h,cpp}` — NEW. The three integrators + the shared
  verified skeleton (cap resolve, spawn gate, frame divisor, age step, 3-span alpha
  fade, z-cap clip, spawn colour) + `UpdateSystem(type,…)` dispatcher.
- `tests/unit/particle_integrate_test.cpp` — NEW (37 checks, all pass).
- this doc.

NOT touched: `particle.{h,cpp}` (wave-6's 7 emitter kernels — a DIFFERENT, smaller
emitter family; left intact), `particle_render.*`, `particle_spawn.*`,
`fx_recon3_particle_render.*` (wave-6), every bind-site.

A NEW module (not `particle.{h,cpp}`) was the right call: these three operate on the
LARGE 0x310 system block with field offsets that DIFFER per function and DIFFER from
wave-6's `Emitter`/`Particle`. They are modelled as a raw byte block (`Emitter`)
with explicit offset accessors so each kernel dereferences exactly the offsets its
own asm reads — no ODR clash with wave-6 (separate `pintegrate` namespace).

## What is byte-exact vs. the one documented residual (rule 8)

Byte-exact (verified by golden tests + asm/decompile line-by-line):
- short-circuit `now == a1+36`; the `cap = a1+208` (or `trunc(count*a1+172)` when the
  low-flags byte is negative); frame divisor from `group[+112]` else 1;
- spawn RNG draw ORDER (5 angle draws, 3 colour draws, dir/accumulator/velocity
  draws) — reproduced bit-exactly from a seeded `crt::Srand` in the tests;
- big-dt clamp (`(float)(now-lastTick) > 65536 → dt=0, birthTick += raw`);
- velocity(+0/4/8) advance, box bias (a1+120/124/128), accumulator(+0x20/24/28)
  carry, render center (a1+64/68/72 * sin(ang) + acc), size (a1+76*sin + a1+228),
  alphaSrc (sin(phase)*a1+176 + a1+184);
- z-cap clip against a1+168 (snap + scale by a1+156/160/164);
- 3-span triangular alpha fade (pre-ramp 1/spanA, steady clamp, down-ramp
  1/(spanC-spanB)) + kill past spanC + killedCount;
- the spawn variants (ring vs. direct; Lens dead-slot-first loop order).

Per-function field-offset divergences captured exactly: Points advances angles via
a1+0x50/54/58/5C and velocity via `phase * a1+0x90/94/98`; Polys/Lens advance angles
via a1+0x80/84/88/8C and velocity via `dt * a1+0x90/94/98`.

**Residual RESOLVED (wave-11, x87 trace).** The phase-wrap chain was traced
register-by-register from the disasm (Points 0x5e23e6.., Polys 0x5e2ec5..,
Lens 0x5e36bb..) and is now fully 1:1. Findings:

- `VIBE_Math_Fmod @0x5d3fb2` is `fprem` (loop) then `fstp st(1)`: `fprem` computes
  `st0 mod st1` with **dividend = st0, divisor = st1**, and the trailing `fstp st(1)`
  leaves that remainder. The Hex-Rays prototype lists args `(a1@st1, a2@st0)`, so its
  `Fmod(pi, x)` rendering means divisor=pi (st1), **dividend = x (st0)** → `x mod pi`.
  There is NO transient divide-by-zero: each wrap site pushes `dbl pi` and keeps a
  copy live deep on the FPU stack (`fld st(2)`/`fxch`) across all five calls; Hex-Rays
  mistracked which deep slot held pi vs. a prior wrap result.
- The five accumulators **do NOT seed off each other.** Each is independent:
  `ang_i = fmod(dt*rate_i + ang_i, pi)` for i=0..3, `phase = fmod(dt*phaseRate + phase, pi)`.
  `WrapPi(advanced) = fmod(advanced, pi)` was already exactly this — kept as-is.
- **Bugs FIXED in the same pass** (the prior decompile transliteration was wrong):
  - Points velocity advanced by `phase * a1[+0x90/94/98]`; the asm at 0x5e246f
    multiplies **dt** (the FPU top after the wrap), not phase → fixed to `dt * a1[..]`.
  - Polys/Lens angle rates were `a1[+0x80/84/88/8C]` and phase rate `a1[+0xB0]`; the
    asm uses the SAME rates as Points: angles `a1[+0x50/54/58/5C]` (0x5e2ecb/0x5e36bf
    + 0x5e2eeb.. / 0x5e36e1..) and phase rate `a1[+0xB4]` (0x5e2f36 / 0x5e373c) →
    fixed. alphaSrc amplitude stays `a1[+0xB0]` (0x5e3019 / 0x5e383e), correct.

Golden pins (suite `ParticleIntegratePhase`, +2 tests, seeded): `PointsWrapChainAndDtVelocity`
reproduces all five wrapped accumulators and the three dt-driven velocity components
bit-exactly; `PolysAndLensWrapChainSameRates` proves Polys/Lens use the 0x50.. /0xB4
rates (the 0x80.. fields deliberately set to garbage to prove they are unused).

## Tests (rule 11) — `particle_integrate_test`, 37 checks, all pass
- ShortCircuitNowEqualsLastTick — `now==lastTick` returns true, touches nothing.
- CapScaledByLife255WhenFlagsNegative — `trunc(count*life255)` budget.
- SpawnGateClosed — gate predicate (negative flags w/o flagsHi bit1).
- PolysSpawnAngleDrawsDeterministic — seeded RNG reproduces the 5 angle draws.
- BigDtClampZeroesAdvance — the 65536.0f dt clamp + lastTick stamp.
- AlphaFadeSpansAndKill / AlphaFadePreRamp — the 3-span envelope + kill + killedCount.
- LensSpawnAccumulatorSeed — Lens dead-first ordering, (rnd*2-1)*a1[96/100/104] seed.
- PointsIntegrateVelocityAndCenter — center == accumulator when amps are 0.
- ZCapClipPositive — z snap to cap + accumulator scale.
- FrameModFromGroupByte — `frame = age/(flags&0x1F) % group[+112]`.

Neighbours still green: `fx_recon3_particle_render_test` (67), `particle_spawn_test` (90).

## Handoff (for the orchestrator — NO bind-site edited)

The integrate must run per live system BEFORE that system is rendered (it produces
the +56/60/64 center + +79 alpha that `render_system_to_surface` consumes). In the
original this is the same per-system walk wave-6 documented (`dword_140874C` system
list; each system's installed update fn at +0x304 is called by the engine tick).

Live seam to call (per system, walking the engine list):
```cpp
namespace pi = guild::render::pintegrate;
pi::Emitter& e   = *reinterpret_cast<pi::Emitter*>(systemBlock);     // sys+0 (0x310)
pi::Slot*    sl  = reinterpret_cast<pi::Slot*>(systemBlock + 40);    // sys+40
e.setGroupPtr(boundTextureGroupFor(systemBlock + 212));             // host ptr at +0xD4
bool alive = pi::UpdateSystem(systemTypeOf(systemBlock), e, sl, nowTick);
// then: fxrecon3::render_system_to_surface(fb_, view, world, projState_, blend, …)
```
- `nowTick` = the frame tick the engine passes as the @<edx> arg (CityView3D's frame
  clock). `systemTypeOf` = the template byte SpawnSystemByType branched on (0/1/2).
- GATE/ORDER unchanged from wave-6: integrate during the post-object particle pass,
  immediately before `render_system_to_surface`, inside the same universe frame.
- HOST NOTE: the group pointer at +0xD4 is a 4-byte field in the 32-bit original; on
  the 64-bit host store the native pointer via `Emitter::setGroupPtr` (the 0x310
  block has room; +0xD4..+0xDB is free before +0xE4 startW).

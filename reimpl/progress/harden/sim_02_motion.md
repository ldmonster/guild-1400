# Harden sweep — sim_02_motion (src/sim/charaction_motion.cpp)

MCP live (gilde.exe). Every provenanced function decompiled + diffed line-for-line;
every float/double/int constant re-read via get_bytes; every float→int / float-store
site checked against disasm. Float/angle-heavy file — maximal scrutiny applied.

## Constants — ALL VERIFIED byte-for-byte (get_bytes, little-endian, round-tripped)

| sym | addr | bytes → value | header literal | ok |
|-----|------|---------------|----------------|----|
| kDotFloor | 0x6286F8 | BFF0000000000000 = -1.0 | -1.0 | ✓ |
| kHalfPiRot | 0x628700 | 3FF921FB54442EEA = pi/2 | 1.570796326795 | ✓ (round-trips) |
| kStairStepGate | 0x6106CC | C014000000000000 = -5.0 | -5.0 | ✓ |
| kIndoorLift | 0x6106D4 | 4008000000000000 = 3.0 | 3.0 | ✓ |
| kHeightLerp | 0x6106DC | 3E800000 = 0.25 | 0.25 | ✓ |
| kRampStep | 0x610938 | 3C23D70A = 0.0099999998 | matches | ✓ |
| kIndoorDurScale | 0x61093C | 3.0 | 3.0 | ✓ |
| kMissedThresh | 0x610944 | 3FF3333333333333 = 1.2 | 1.2 | ✓ |
| kTurnHi | 0x61094C | 0.04 | 0.04 | ✓ |
| kTurnLo | 0x610954 | -0.04 | -0.04 | ✓ |
| kTurnPerTick | 0x61095C | 3FED174571745D18 = 0.9090909090909092 | matches | ✓ |
| kCartTurn | 0x610964 | 3ECCCCCD = 0.4 | 0.4 | ✓ |
| kRunThresh | 0x61096C | 2pi/3 = 2.094395102393333 | matches | ✓ |
| kBucketHalfPi | 0x610974 | pi/2 | 1.570796326795 | ✓ |
| kBucketPi3 | 0x61097C | pi/3 = 1.047197551196667 | matches | ✓ |
| kBucketPi6 | 0x610984 | pi/6 = 0.5235987755983333 | matches | ✓ |
| kFac200 | 0x61098C | 2.0 | 2.0 | ✓ |
| kFac014 | 0x610994 | 0.14 | 0.14 | ✓ |
| kFac020 | 0x61099C | 0.2 | 0.2 | ✓ |
| kFac032 | 0x6109A4 | 0.32 | 0.32 | ✓ |
| kTwoPi | 0x6109AC | 2pi = 6.28318530718 | matches | ✓ |
| kSpeedMulFlat | 0x6109B4 | 400CCCCD = 2.2 | 2.2 | ✓ |
| kSpeedMulStairs | 0x6109B8 | 40200000 = 2.5 | 2.5 | ✓ |
| kSpeedMulCartFl | 0x6109BC | 3FD9999A = 1.7 | 1.7 | ✓ |
| kSpeedMulCartSt | 0x6109C0 | 3FF33333 = 1.9 | 1.9 | ✓ |
| kIndoorOne | 0x62D07C | 3F800000 = 1.0 | 1.0 | ✓ |
| flt_5CA2B0 (bone probe base) | 0x5CA2B0 | 0.0 | — | ✓ |

All 27 constants confirmed against the binary. No constant divergences.

## Per-function results

### VIBE_Object_SetWorldTranslationXYZ (0x5af5cc) — VERIFIED-1:1
Disasm 0x5af5cc: packs {arg0=x, arg1=yaw, arg2=z} and tail-calls SetWorldTranslation
(0x5af50c), which writes +132=x, +136=yaw, +140=z then rebuilds the euler matrix.
Crucially the yaw is passed/stored as the raw float BITS (the dispatcher hands it
`SLODWORD(v107)` = `push [var_24]` after `fstp [var_24]`) — NOT a float→int convert.
The C++ writes transX/transYaw/transZ (the +132/+136/+140 observable fields) and
models the scene-graph dirty walk + euler matrix as render leaves. Field writes 1:1.

### VIBE_Math_AngleToTargetSigned (0x5b6d1c) — VERIFIED-1:1 (core) / BOUNDARY (bone input)
Geometric core diffed exactly:
- facing.y (v14) and toTarget.y (v23) zeroed; normalize toTarget then facing.
- dot = v22*v13 + v23*v14 + v24*v15.
- clamp: `if (dot < -1.0 || SLODWORD(dot) < 1065353216) c = dot>=-1?dot:-1 else c=1`
  (1065353216 = 0x3F800000 = 1.0f) — C++ matches incl. the bit-compare.
- ang = AcosGuarded(c) (acos).
- sign: rotate facing +pi/2 in XZ (sin/cos of dbl_628700=pi/2): v19=co*fx+fz*s,
  v21=fz*co+s*(-fx); flip ang's sign BIT (`HIBYTE ^= 0x80`) when v19*tx+0*ty+v21*tz>0.
  C++ reproduces the sign-bit XOR (not a negate) exactly.
Result stored to float (`v11 = v6`) — C++ returns float. 1:1.
BOUNDARY (0x5b6d35/0x5b6d43): the engine derives `facing`/`pivot` via
PointThroughBoneChainPivot/PointThroughBoneChain (skeleton transform, render leaf
out of this module's tree). The C++ takes the already-resolved facing/pivot/target
world vectors and computes the determinism-relevant math 1:1; AvatarForward models
the planar facing from the heading. Documented in header.

### VIBE_Character_StepMotionQueue (0x4041e8) — VERIFIED-1:1
v3=avatar(node+20); handle=avatar+112. First-poll attach (no handle & no node+12) →
return SHIBYTE(node+13) (node+13>>24, signed). Reached (handle & anim+109&0x20) →
prune (gated on avatar+140&2==0) + clear handle + return -1. Abort (handle & node+400)
→ same. Else playing → return packed result. The ComputeBoneDelta/PruneExpired calls
are render leaves modeled by the prune hook; control flow + return codes (incl. the
signed >>24) match. Note Hex-Rays' AttachMotion shows a stale 3rd arg (`v3`) from a
collapsed __usercall — irrelevant to the modeled attach. 1:1.

### VIBE_Character_QueryTileAhead (0x4066bc) — VERIFIED-1:1 (math) / BOUNDARY (floor pick)
- world = {obj[19],obj[20],obj[21]} (avatar position +76/+80/+84).
- WorldToTileWithHeight (0x5c6644, render chunk) → tileX=v16[0], tileY=v14, h=v13.
- idx = mesh[8]*tileY + tileX (mesh[8]=+32 grid size); type = (i8)entries[24*idx].
- early-out `(type==0||type==13) && universe[+172]==0` returns (i8)entries[24*idx].
- floor present: PickTileAtPoint, then `if (type==0||type==13|| floorH-h >= -5.0) h=floorH`.
- `if (!IndexFromPointer(universe)) h += 3.0`.
- obj[80] = (h-obj[80])*0.25 + obj[80]; SetPosition.
Return = v9 (signed type byte). C++ signedness, offsets, side-effect order all match.
BOUNDARY: Floor_PickTileAtPoint (0x5c2ddc) and IndexFromPointer (0x426724) are
render/scene leaves; floor pick modeled as floorH==h (identity blend), IndexFromPointer
modeled via universeIndoorFloor==0. Documented in source.

### VIBE_Command_Dispatcher / WalkOnPathStep (0x40a4d4) — FIXED (float precision)
Full monolith diffed across all phases (morph cooldown, build, morph-init, abort,
per-tick advance, footstep throttle, ramp, rotation interp, gap-skip scan, waypoint
advance, speed update). Verified 1:1:
- Branch dispatch at LABEL_111 (disasm 0x40ad3b): `0.0 fcomp [a1+264]`, jbe → the
  `>=0` side; the `<=0` sub-branch is the aligned/missed case; `>0` the additive arm;
  `<0` the subtractive arm. The C++ three-way (turnAngle <0 / ==0 / >0) matches.
- Subtractive snap (0x40b1db): `jnb` skip when nv>=target → snap when nv<target.
- Additive snap (0x40b509): `jbe` skip → snap when nv>target. Both arms verified.
- target uses the ORIGINAL yaw (v54[34]/v67[34]) + recomputed angle, not the stepped nv.
- missed-threshold: stop bit (anim+110|=8) set unconditionally; visibility gated on
  final segment is a render leaf. seg-length sqrt vs segLength*1.2 compare matches.
- gap-skip scan / stall bits (anim+110 &= ~2 |= 8, anim+109 |= 0x20) match.
- speed update float multiply chain (baseSpeed*mul*tileFactor*ramp) + the indoor
  tileFactor `(float)(1.0+0.2)` all match; the >>24 packed result, etc.
- No float→int truncation anywhere in the heading path: every `fstp`/`fst` stores a
  FLOAT and the yaw is passed by raw bits (no ConvertX, no fistp, no (int) cast).

FIX — RotateStep float-precision divergence (0x40b177 subtractive / 0x40b49e additive):
  The binary carries the running heading (var_24/var_20), the per-tick step (v104/v105),
  the Fmod result, and the segment target (var_48/var_4C) as 32-bit FLOAT, with float
  stores (`fstp`/`fst` of dword operands) at each step; the snap compares are
  float-vs-float (`fld [dword]`). The reconstruction did the entire wrap/compare chain
  in DOUBLE, only casting to float at the final SetWorldTranslationXYZ.
  Evidence:
    0x40b190 `fst [esp+..var_24]`   (Fmod result rounded to float BEFORE the <0 wrap)
    0x40b1cd `fstp [esp+..var_48]`  (target = float(cur_yaw + delta))
    0x40b1d4/0x40b1db `fld [var_24]; fcomp [var_48]`  (float-vs-float snap compare)
    0x40b4be / 0x40b4fb / 0x40b502 — identical pattern in the additive arm.
    `v104`/`v105` declared `float` in the decompile; step is a float field.
  BEFORE: `double target=cur+delta; double step=elapsed*kTurnPerTick; ... double nv;
           nv=fmod(nv,2pi); if(nv<0)nv+=2pi; if(nv>/<target)nv=target; return nv;`
  AFTER:  step rounded to float (and `(float)(step*kCartTurn)` when cart); target =
           `(float)(cur+delta)`; nv = `(float)(cur +/- s)`; fmod result `(float)`;
           wrap `(float)(nv+2pi)`; snap compares done on the float nv vs float target.
  The transient bucket product `s` and the cur±s subtract stay extended (x87 register)
  until the modeled float store, matching the binary. This removes a rare boundary-case
  drift in the snap/overshoot decision and the emitted heading.

## Tests
tests/unit/sim_charaction_motion_test.cpp: angle goldens, SetWorldTranslationXYZ field
write, StepMotionQueue return codes, QueryTileAhead terrain/height-snap, universe
ladder — all use approximate `Near` and remain valid (RotateStep is internal/untested
directly; no golden encoded the wrong precision, so no golden change needed). File
compiles clean (`g++ -std=c++17 -Wall -Wextra` object build, exit 0, no warnings);
test file syntax-checks clean against the header.

## Counts
- Functions provenanced: 5
- VERIFIED-1:1: 4 (SetWorldTranslationXYZ, AngleToTargetSigned core, StepMotionQueue,
  QueryTileAhead math)
- FIXED: 1 (Command_Dispatcher/WalkOnPathStep — RotateStep float precision)
- BOUNDARY (render/skeleton leaves, documented): bone-chain transform (AngleToTarget),
  Floor_PickTileAtPoint + IndexFromPointer (QueryTileAhead), and the anim/mesh/sound
  hooks across the monolith.
- Constants verified: 27 (0 divergent)

## Handoff (NOT my chunk)
- src/gui/widget_layout.cpp:214/234/236 — PRE-EXISTING build break (uncommitted work
  in another chunk): `w.ld<i32>(...)` references a non-existent `Widget::ld` member.
  Blocks the full `guild` static lib (and therefore every test target) from linking.
  Not touched by this sweep; flagging for the gui chunk owner. My file builds in
  isolation.

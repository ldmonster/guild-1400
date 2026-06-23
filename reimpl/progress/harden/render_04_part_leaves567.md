# Harden Wave — render_leaves5 / render_leaves6 / render_leaves7

1:1 hardening pass over the three constant/table-heavy render leaf modules. Every
function carrying `// gilde.exe 0xADDR` provenance was re-decompiled + disassembled
and diffed line-for-line against the reimplementation. Every constant / LUT / magic
number confirmed bit-exact via `get_bytes`. Float→int and x87 80-bit accumulation
sites checked against disasm.

Files touched (only these, per scope):
- src/render/render_leaves5.cpp / .h
- src/render/render_leaves7.cpp / .h
- tests/unit/render_leaves5_test.cpp
- tests/integration/render_leaves5_itest.cpp

render_leaves6.cpp / .h: NO code changes needed (all VERIFIED-1:1).

Tests: 9/9 pass (478-480 unit, 968-970 integration, 1355-1357 e2e).

---

## render_leaves5.cpp — DIVERGENCES FIXED

### InitColors (0x42be58) — FIXED (golden + impl)
- **Loop count.** Disasm: `xor ecx,ecx` / body / `inc ecx` / `cmp ecx,count` /
  `jl` (0x42beb6). Entry guard `test esi,esi; jle` (count>0). It is a do-while
  that seeds **all `count` slots** (0..count-1), NOT count-1. The prior code used
  `while (i+1 < slotCount)` and the comment claimed "seeds count-1"; both wrong —
  there is no `+1` in the disasm.
- **count<=0 return.** `result = a4` (baseW2 low byte) @0x42be75 is the al residue
  when the loop is skipped. Restored.
- Golden recomputed via LCG oracle (Srand(12345)): 5 slots seed
  B=[218,232,219,227,220], G=[104,131,126,144,131], R=[87,95,76,77,69], ret=69.
  (Prior golden stopped at 4 slots with ret=77 — wrong.) Integration-test colour
  oracle loop also corrected from `i+1<slots` to `i<slots`.

### SpawnExplosion (0x42c8e0) — FIXED (3 divergences; signature changed)
- **sqrt term.** `fld dword ptr [esi]` @0x5f... (0x42ca5a) loads `(float)sys[0]`
  == `hdr0` REINTERPRETED as float — the ring radius for the sqrt is `hdr0f`, NOT
  `radius`. Prior code used `radius*radius`. Real caller @0x486822 passes
  hdr0=119.0f bits, radius=80.0f. Fixed: `sqrt((double)hdr0f*hdr0f - yprof*yprof)`.
- **angle 80-bit + order.** `fild;fmul flt_611AD0(2pi);fmul flt_611AC0(norm)` —
  angle is 80-bit (never stored to float), order `i*2pi*norm`, fed straight to
  fsin/fcos. Prior code rounded to float and used order `i*norm*2pi`. Now `double`.
- **ring 80-bit.** sqrt result kept on the x87 stack and multiplied by sin/cos at
  80-bit before the float store (fmul st,st(1); fstp). Now `ring` is `double`.
- **owner/userTag.** AllocSystem(owner=a2(ebx), …, userTag=a1(eax)). Prior code
  passed ownerA1 for both. Added `ownerA2` param (eax/ebx pair). Test + header
  updated.
- Constants 0x611AC0 table all bit-exact (norm=1/32767, 0.1, 0.5, 0.05, 2pi,
  pi/2, 0.4, 0.2).

### SpawnBlood (0x42c728) — FIXED
- AllocSystem **owner is hardcoded 200** (eax), a1 is the userTag. Prior code used
  ownerA1 for both — diverges on the null-owner gate and stored sys->owner/userTag.

### SpawnDebris (0x42d188) — FIXED
- Texture name is **"Erdbrocken_an0"** (aErdbrockenAn0 @0x611b34, confirmed via
  get_string), not "Erdbrocken anim".

### SpawnRefLens (0x42c3ac) — FIXED
- Stored integrator is **VIBE_Particle_SeedParticles (0x42bec0)**, not
  UpdateScatter. Forward-declared SeedParticles and pass its address as updateFn.
  (InitColors RNG-consumption now seeds all slots — integration oracle updated.)

### SpawnBloodEffect (0x43fa1c) — FIXED
- Texture name is **"blut"** (lowercase, aBlut @0x6175e0), not "Blut".
- (Reimpl keeps a defensive null-check on AllocSystem the original lacks — the
  original assumes success; success path is byte-identical. Noted, not a math
  divergence.)

### UpdateRainStep (0x43f858) — FIXED
- **dt is 80-bit.** `fild;fmul dbl_617590` keeps dt on the x87 stack and reuses it
  at full precision for both `dt*2.0` (0x43f8bc) and `slot+4 - dt` (0x43f8d0,
  fsubr). Prior code rounded dt to float first. Now `double dt`. Unit-test oracle
  updated to match.
- All other arithmetic (unsigned dt subtract, slot56/60/64 sums, respawn box
  100.0f/0, randomized vel, fade clamp+ConvertX) VERIFIED-1:1. Rain constants
  0x617590 table all bit-exact.

## render_leaves5.cpp — VERIFIED-1:1 (no change)
- SpawnEffect (0x42bd6c): tag dword 0x7F|(((trig&1)|2)<<16), header writes,
  turnRate*0.75 (flt_6119C4=0.75). meshTurnRate is the external mesh-chain value
  (parameterized — boundary). ✓
- SpawnSmoke (0x42cbc4): drift bias (flt_611AEC=0.5), per-slot fade reciprocal
  pair, colour seeding `(HIBYTE(col)&rand)+BYTEn(col)`, birth-frame `frame+(rf&7)`
  written to slot+48, frameStep advance. Smoke constants 0x611AE8 bit-exact. ✓
- SpawnSmokeEffect (0x4d880c): v3 vel {0,200.0f,0}, v4 {LOBYTE=100, word=-24321},
  literal SpawnEffect args. ✓

---

## render_leaves6.cpp — ALL VERIFIED-1:1 (no code change)

- DrawLineClipped (0x435434): slopes v14=(y1-x0)/(x1-y0), v13 inverse; the full
  trivial-reject guard chain (4 guards) and the 4 single-edge clip blocks
  (each interpolating the other coord, `> bound` = clip-hi else clip-lo) match
  the decompile branch-for-branch incl. the `result` running value on each reject.
  `(int)vN` truncation == VIBE_Coord_ConvertX x87 chop. ✓
- MeshDrawBoundingBox (0x426a30): class!=4 -> 1; first-qualifying submesh seeds
  min==max==corner; refinement uses `>=` for min and `<=` for max (verbatim); the
  4 derived corners (min)/(max0,min1,min2)/(max0,min1,max2)/(min0,min1,max2) all
  use min1 for Y. ✓ (assignMeshData / transformPivot are engine boundary hooks.)
- SurfaceColorFillRect (0x423c70): leftClampX clamp, x1=min(x+w,width)/
  width-1<x+w, y1 likewise, in-bounds origin test — rect math 1:1. NOTE: the
  original takes NO color arg (surface=a5); the vendor fill color is a constant
  grey descriptor (SetGrayColorThunk value 100) and the linear path uses
  *(surface+28). The reimpl's `color` param is a boundary-modeling convenience
  passed to the surfaceFill hook (the GPU/DDraw boundary, Rule 8); the
  deterministic rect derivation is exact. Documented, not a math divergence.
- ComputeLabelEntries (0x428a84): visible = +529&1; a[axis]=posA(+92..)-worldPos
  (+76..); b[axis]=posB(+144..)-trans(+132..). ✓ (FindByHandle / CreateObjectAnim
  / reentrancy guard are the anim boundary, excluded from the kernel.)
- ComputeMarkerEntry (0x428d30): a=worldPosA - cam[19..21](+76..);
  b=worldPosB - cam[33..35](+132..). ✓

---

## render_leaves7.cpp — DIVERGENCES FIXED + VERIFIED-1:1

### ComputeShadowClipRect (0x5f3048) — FIXED (precision)
- **uStep.** `fld1; fdiv [float v40]` @0x5f316e — `1.0/v40` is 80-bit then rounded
  to float. Prior code used float division `1.0f/v40`. Now
  `(float)(1.0/(double)v40)` matching the vStep model already present.
- All early-out rejections (dim<=minX, maxX<0, dim<=minY, maxY<0, maxX-minX<=0,
  maxY-minY<=0) and the rect clamp + uOff/vOff derivations VERIFIED-1:1.

### ProjectGroundVertex (0x5f216c) — FIXED (precision)
- **Y double-rounding.** `v15 = height*yScale` stays 80-bit (fmul @0x5f2547),
  then `fadd v37` (precomputed float groundY+0.5) before the single `fstp`
  @0x5f2565. Prior code rounded height*yScale to float first. Now the product is
  kept as `double` and rounded once. X/Z math already 1:1. flt_62C26C=0.5 exact.

## render_leaves7.cpp — VERIFIED-1:1 (no change)
- HeightFieldBias (0x5f2b09): byte_64A351 ? 1.5 : 1.0. ✓
- RasterizeHeightVertexY (0x5f2bfb): ((double)h+bias)*yStep+(baseY+bias). ✓
- HeightEdgeDiscontinuous (0x5f2dd5): |hA-hB|>25 || |hA-hC|>25 (dbl_62C270=25.0). ✓
- PointLightAttenuation (0x5c74c0): d2<range2 ? 1/(denom*d2)*intensity : 0;
  80-bit modeled with double. ✓
- DirectionalFalloffScale (0x5c7241 spot / 0x5c76d6 area): NdotL<0 ?
  intensity*lut[(int)(NdotL*-1023)] : 0 (flt_628C28=-1023.0; lut=flt_1405110). ✓
- FlareRingT (0x5ef40b): ring*0.2 (flt_62C13C). FlareSegT (0x5ef4a5): seg*(1/7)
  (flt_62C140). ✓
- FlareSunAngle (0x5ef222/22f): VectorAngleBetween(up=(0,0,1), viewDir) then
  fmod(., 2pi=dbl_62C134). ✓
- FlareVertexAlpha (0x5ef77e): band<0||255>=band ? max(0,raw) : 255
  (flt_62C144=255.0). ✓
- ComputeFlareGrid (0x5ef3bf..5c8): 6 rings x 8 segs; rowA=lerp(c30,c32,r*0.2),
  rowB=lerp(c31,c33,r*0.2), out=normalize(lerp(rowA,rowB,s/7)). ✓

### Constants — ALL bit-exact (get_bytes)
flt_62C26C=0.5, flt_62C278=0.5, dbl_62C270=25.0, dbl_62C134=6.283185307179586,
flt_62C13C=0.2, flt_62C140=0.142857149, flt_62C144=255.0, flt_628C20=10.0,
flt_628C24=20.0, flt_628C28=-1023.0, flt_628C2C=0.001, flt_628C30=0.01.

---

## Function status summary

| module | function | addr | status |
|--------|----------|------|--------|
| leaves5 | InitColors | 0x42be58 | FIXED (loop count, count<=0 ret, golden) |
| leaves5 | SpawnEffect | 0x42bd6c | VERIFIED-1:1 |
| leaves5 | SpawnBlood | 0x42c728 | FIXED (owner=200) |
| leaves5 | SpawnExplosion | 0x42c8e0 | FIXED (sqrt hdr0f, 80-bit angle/ring, owner/userTag) |
| leaves5 | SpawnSmoke | 0x42cbc4 | VERIFIED-1:1 |
| leaves5 | SpawnDebris | 0x42d188 | FIXED (string "Erdbrocken_an0") |
| leaves5 | SpawnRefLens | 0x42c3ac | FIXED (integrator SeedParticles) |
| leaves5 | SpawnSmokeEffect | 0x4d880c | VERIFIED-1:1 |
| leaves5 | SpawnBloodEffect | 0x43fa1c | FIXED (string "blut") |
| leaves5 | UpdateRainStep | 0x43f858 | FIXED (dt 80-bit) |
| leaves6 | DrawLineClipped | 0x435434 | VERIFIED-1:1 |
| leaves6 | MeshDrawBoundingBox | 0x426a30 | VERIFIED-1:1 |
| leaves6 | SurfaceColorFillRect | 0x423c70 | VERIFIED-1:1 (color param = boundary) |
| leaves6 | ComputeLabelEntries | 0x428a84 | VERIFIED-1:1 |
| leaves6 | ComputeMarkerEntry | 0x428d30 | VERIFIED-1:1 |
| leaves7 | ComputeShadowClipRect | 0x5f3048 | FIXED (uStep 80-bit) |
| leaves7 | ProjectGroundVertex | 0x5f216c | FIXED (Y double-rounding) |
| leaves7 | HeightFieldBias | 0x5f2a58 | VERIFIED-1:1 |
| leaves7 | RasterizeHeightVertexY | 0x5f2a58 | VERIFIED-1:1 |
| leaves7 | HeightEdgeDiscontinuous | 0x5f2a58 | VERIFIED-1:1 |
| leaves7 | PointLightAttenuation | 0x5c6f90 | VERIFIED-1:1 |
| leaves7 | DirectionalFalloffScale | 0x5c6f90 | VERIFIED-1:1 |
| leaves7 | FlareRingT/SegT/SunAngle/VertexAlpha/Grid | 0x5ef19c | VERIFIED-1:1 |

Counts: 23 functions reviewed; 9 FIXED, 14 VERIFIED-1:1; 0 deferred.
Tests: 9 suites (unit/integration/e2e x 3), 100% pass.

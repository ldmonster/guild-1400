# Wave-H1 1:1 Hardening — render chunk G (particle integrate / render / spawn)

Files owned: `src/render/particle_integrate.cpp`, `particle_render.cpp`, `particle_spawn.cpp`
(+ headers + `tests/unit/particle_integrate_test.cpp`).

MCP IDA Pro driven. Disasm is reference of record where Hex-Rays mistracked the x87 stack.

## Summary counts
- VERIFIED-1:1: 6 functions (UpdateLens, BuildSnowVertices, BuildRainVertices,
  InterpolateFade, CreateEmitter, SetPosition/KillParticle/KillEmitter trivial wrappers)
- FIXED: 2 functions (UpdatePoints, UpdatePolys) — 3 distinct divergences total
- BOUNDARY: 2 (rain diffuse packing in un-reconstructed render head; D3D DrawPrimitive
  vendor call — rule-3)
- HANDOFF/finding: 1 (AllocSystem owner/slotCount single-value vs two-param modeling)

All constants re-verified by `get_bytes` (see below). All three integrate goldens +
spawn goldens pass after fixes (integrate: 97 checks 0 fail; spawn: 90 checks 0 fail).

---

## particle_integrate.cpp

### VIBE_Particle_UpdatePoints @0x5e1e0c — FIXED (2 divergences)

**Divergence 1 — phase accumulator multiplier (was dt, must be ang0-result).**
Disasm 0x5e2446..0x5e2463: after the four angle stores the FPU top holds `r0` (the
just-wrapped ang0 = `fmod(dt*rate0+ang0, pi)`); `fxch st(1); fmul [esi+0B4h]; fadd
[ecx+0Ch]; call Fmod` multiplies THAT value, not dt. The velocity advance that follows
(`fmul [esi+90h]`) is what consumes dt (the duplicated dt copy still on the stack).
- before: `p->phase = WrapPi(dt * e.f(0xB4) + p->phase);`
- after:  `p->phase = WrapPi(p->ang0 * e.f(0xB4) + p->phase);`
- Fmod semantics confirmed: 0x5d3fb2 = `fprem` (st0 mod st1) then `fstp st(1)` →
  returns fmod(dividend=st0, divisor=st1). Wrap = fmod(advanced, pi). (Angles already
  correct: 4 independent `fmod(dt*rate_i+ang_i, pi)`, verified full stack trace.)

**Divergence 2 — ring-spawn speed endpoints start/end were SWAPPED.**
Disasm 0x5e212b..0x5e216b:
- `start` (var_C): `fcomp a1+96,a1+100; jnb -> a1+100` ⇒ start = (a1+96 < a1+100)? a1+96 : a1+100  (min)
- `end`   (var_64): `fcomp a1+96,a1+100; jbe -> a1+100` ⇒ end   = (a1+96 <= a1+100)? a1+100 : a1+96 (max)
- before: `start = hiLo? a1+100 : a1+96; end = hiLo? a1+96 : a1+100;` (inverted)
- after:  `start = (a1+96<a1+100)? a1+96:a1+100; end = (a1+96<=a1+100)? a1+100:a1+96;`
speed = (end-start)*bt + start.

Everything else verified 1:1: spawn 5 angle draws (rnd*1/32767*2*pi), color draw order
R(+0x4E)/G(+0x4D)/B(+0x4C), 3 dir draws `rnd*1/32767 + (-0.5)` (NO *2) in order
[dir0,dir1,dir2] → normalize, turbulence 3 draws *a1+0x84/88/8C, render center
a1+0x40/44/48*sin(ang_i)+acc, size a1+0x4C*sin(ang3)+a1+0xE4, alphaSrc
sin(phase)*a1+0xB0+a1+0xB8, z-cap clip a1+0xA8/9C/A0/A4, big-dt clamp (65536.0f =
1125515264), frame=age/(flagsLow&0x1F)%frameMod, 3-span alpha fade (preRecip=1/spanA
a1+0x30=+48, cbRecip=1/(spanC-spanB) a1+0x34=+52), ConvertX truncate, kill, return.

### VIBE_Particle_UpdatePolys @0x5e2814 — FIXED (2 divergences)

**Divergence 1 — phase multiplier (same as Points).** Disasm 0x5e2f22..0x5e2f3f is
byte-identical to Points: phase = `fmod(ang0_result * a1+0xB4 + phase, pi)`, not dt.
- before: `p->phase = WrapPi(dt * e.f(0xB4) + p->phase);`
- after:  `p->phase = WrapPi(p->ang0 * e.f(0xB4) + p->phase);`

**Divergence 2 — spawn direction had a spurious `*2` and Y/Z swapped.**
Disasm 0x5e2a3f..0x5e2a7b: two draws each `fild; fmul flt_62BAB4(=1/32767); fadd
flt_62BAD0(=-0.5)` (NO *2) stored to var_80(dir0), var_7C(dir1); var_78(dir2)=0
(`xor eax,eax`). VectorNormalize(&var_80) reads [dir0,dir1,dir2]=[draw0,draw1,0].
- before: `dir[0]=RnN()*2-0.5; dir[2]=RnN()*2-0.5; dir[1]=0;`
- after:  `dir[0]=RnN()-0.5; dir[1]=RnN()-0.5; dir[2]=0;`

Other Polys details verified 1:1: angle rates a1+0x50/54/58/5C (NOT 0x80..), velocity
a1+0x90/94/98, box bias a1+0x78/7C/80, accumulator carry dt*box, ring/non-ring spawn
speed = sp*invMid (invMid=a1+100/a1+96 at scratch a1+0x3C) or rnd*a1+100, dir
recompute `dir*speed - acc` with dir2=a1+104-accZ, back-dated birthTick (a1+196 spanC),
jitter draws *a1+132/136/140, color/frame/spans/zcap/fade all match.

### VIBE_Particle_UpdateLens @0x5e32c0 — VERIFIED-1:1

Crucially, Lens differs from Points/Polys on phase: disasm 0x5e3738 `fld var_3C`(dt);
0x5e373c `fmul [ebx+0B4h]` → Lens phase IS dt-driven. The existing reconstruction
(`WrapPi(dt*e.f(0xB4)+p->phase)`) was already correct — left unchanged. Spawn draw
order (5 ang, frame, R/G/B color, accumulator a0/a1v/a2 with a2b=a2*2-1, optional
bit6 backdate, 3 velocity draws *a1+132/136/140), integrate, fade, zcap, and the
inverted dead-slot-first loop all match. Return `(flagsHi&1)==0 || cap>killed`. ✓

### Golden tests fixed (tests/unit/particle_integrate_test.cpp)
- Added `phaseStep(phaseAcc, ang0Result, rate)` helper.
- `ParticleIntegratePhase.PointsWrapChainAndDtVelocity`: phase now
  `phaseStep(3.0, r0, 1.5)` (r0 = wrapped ang0), was `wrapStep(3.0, dt, 1.5)`.
- `ParticleIntegratePhase.PolysAndLensWrapChainSameRates`: Polys phase →
  `phaseStep(2.9, r0, 0.8)`; Lens phase kept `wrapStep(2.9, dt, 0.8)` (dt-driven).
No golden encoded the wrong Polys spawn dir / Points ring start-end, so no other
golden changes were needed.

### Constants re-verified (get_bytes)
0x62BA94/B4/D4 = 0x38000100 = 1/32767; 0x62BA98/B8/D8 = 2.0f; 0x62BA9C/BC/DC =
0x40490FDB pi(f); 0x62BAA4/C4/E4 = pi(double); 0x62BAAC/CC/EC = 255.0f; 0x62BAB0/D0 =
-0.5f; 0x62BAF0 = -1.0f. All match header literals.

---

## particle_render.cpp — VERIFIED-1:1

### VIBE_Snow_Render @0x42b5b0 (BuildSnowVertices) — VERIFIED-1:1
flake stride 10 floats; sx=[6] sy=[7] sx2=[8] sy2=[9] pz=[2]. Clip x0<=sx && x1>sx2
&& y0<=sy && y1>sy2 (matches). z=(1-pz)*flt_611994(0.025). 3 verts: v0
x=(sx+sx2)*0.5 y=sy u=0.5 v=0; v1 x=sx2 y=sy2 u=1 v=1; v2 x=sx y=sy2 u=0 v=1; rhw=1,
diffuse=0x506E4FE4=1348756580, specular=0. All exact.

### VIBE_Rain_Render @0x429c38 (BuildRainVertices) — VERIFIED-1:1
drop stride 10; same field map. Clip sx>=x0&&sx<x1&&sx2>=x0&&sx2<x1 then
sy>=y0&&sy<y1&&sy2>=y0&&sy2<y1 (float→double promotion preserved). z=(1-pz)*0.025
(flt_611774). 2 verts head/tail, u=v=0, rhw=1, specular=0, diffuse=caller word. Exact.

### InterpolateFade (render head) — VERIFIED-1:1
`valA*(now-start)/(end-start) + (end-now)*valB/(end-start)` else valA; unsigned guard
`end>now`, signed division. Matches Snow 0x42b882 / Rain 0x429f76.

### Render constants (get_bytes) — all verified
flt_611990/611764 = 0.1; 611994/611774 = 0.025; 611998 = 0.5; 611768 =
0.0005000000237; 61176C = 96.0; 611770 = 128.0.

### BOUNDARY
- Rain `diffuse` word (v40) is packed in the un-reconstructed render head from
  `ConvertX((1-count*0.0005)*96/128)` — exposed as a parameter to the testable build
  fn (documented in header). The packing lives outside the vertex-build seam.
- D3D `DrawPrimitive` (dword_64A320 vtbl+112) + BeginScene/SetBlendMode/EndScene =
  rule-3 GPU vendor calls; raster MATH (the TLVERTEX stream) is reproduced 1:1.

---

## particle_spawn.cpp

### VIBE_Particle_AllocSystem @0x5e1000 — VERIFIED-1:1 (math) + 1 modeling finding
Reject life<=0 / owner==0 / texture==null; alloc 0x310 "d3_par:ParticleSystem";
particle array 84*count "Particles"; ParticlePoints scratch 80*(4*count);
ParticlePolys scratch 40*(2*count); per-slot init (+56/60/64=0, +0/4/8=0, +48=nowTick
[dword_62EB38], +72=lifeBase[+228], clear +81 bit0, +80=0, +76 dword=-1). All match.

**FINDING / HANDOFF (not fixed — would refactor the whole spawn API + tests):** in the
binary AllocSystem takes ONE eax value that is simultaneously the owner field (+208),
the `if(!a1) return 0` guard, the array size multiplier (84*a1), and the loop count.
The reconstruction split this into separate `owner` and `slotCount` parameters (the
test backend is an abstracted hooked layout, not byte-faithful). For callers where
owner != slotCount the two-param model diverges from the binary's single value. The
real call chain (CreateEmitter 0x43fe5f → SpawnSystemByType 0x5e3b14/92/a5 →
AllocSystem) passes `*ownerPtr` as that single eax value, so owner IS the count in the
binary. Recommend a follow-up to collapse owner/slotCount into one parameter across
AllocSystem/SpawnSystemByType/CreateEmitter + spawn goldens. Left as-is this pass to
avoid a cross-test churn outside the stated divergence classes.

### VIBE_Particle_SpawnSystemByType @0x5e3ae0 — VERIFIED-1:1 (dispatch/control flow)
template[0] 0/1/2 → Points/Polys/Lens; else return 0; alloc; SetPosition; copy 0xA4
template to system+44; BuildBasisFromAngle/MatrixToEuler/SetWorldTranslation place;
if (flagByte1 & 1) { spawnedCount=0; flagByte1 |= 2; }. Modelled via hooks (placeObject
folds the basis/translate). ✓

### VIBE_Particle_CreateEmitter @0x43fd24 — VERIFIED-1:1
Zero 0xA4 template; defaults verified by get_bytes: +0x34=10.0f(0x41200000),
+0x38=50.0f, +0x3C=30.0f, +0x40=flt_5CA2D0(0.0), +0x44=flt_5CA2D4(1.0),
+0x48=flt_5CA2D8(0.0), +0x80=1.0f, +0x8C=255.0f, +0x90=20, +0x94=160, +0x98=180,
+0x9C/9D/9E=0xFF, +0x9F=0; template[0]=*kind; pack v19 (LOBYTE=userType,
BYTE2=trigger|2); +0xA0 |= 0x20; life=(float)*amp; tail-call SpawnSystemByType. ✓

### SetPosition @0x5e1228, KillParticle @0x43fb88, KillEmitter @0x43fe68 — VERIFIED-1:1
Trivial guarded wrappers over place/isValid/freeNode/report hooks. ✓

---
Build: all three owned .cpp compile clean (g++ -std=c++17). Integrate + spawn unit
suites pass. (The full `cmake` target currently fails on an UNRELATED file outside this
chunk: src/sim/character_recon5_transport.cpp:29 — pre-existing, another owner.)

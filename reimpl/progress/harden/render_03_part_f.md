# Wave-H1 hardening — render chunk 03 (particle)

Files:
- `src/render/particle.cpp`
- `src/render/particle_emitter_create.cpp`

MCP IDA Pro live (gilde.exe @0x400000). Every provenance-carrying function
decompiled + diffed line-for-line; every float/double constant re-read with
get_bytes; every float->int site checked against disasm (ConvertX @0x5c6b08
TRUNCATES toward zero -> `(int)` cast is correct everywhere here); every dt /
fixed-point / RNG-order site verified.

## particle.cpp

### UpdateEmitter @0x42b930 — FIXED
- Constants flt_61199C..flt_6119C0 all re-verified bit-exact (2.0, 0.3333333432674408,
  255.0, 1/32767=0x38000100, 10.0, 2.0, 0.20000000298023224, -5.0).
- RNG order (3 pos-spread + 2 axis-spread per respawn) and all spawn fields VERIFIED.
- Follow-branch shade/fade logic, VectorNormalize snap, damping path VERIFIED.
- **FIX (x87 precision):** disasm 0x42bb1d shows dt (st0) is `fst`-stored to a FLOAT
  slot but KEPT on the x87 stack; px uses the un-rounded extended dt while py/pz reload
  the float-rounded fdt. Source rounded dt to float for all three.
  - before: `p->px = fdt * p->vx + p->px;`
  - after:  `p->px = (float)(dt * p->vx + p->px);`  (py/pz keep fdt — matches binary)

### SeedParticles @0x42bec0 — FIXED
- Pass-1 constants dbl_6119CC..flt_611A14 re-verified bit-exact (4.0,3.0,10.0,4.0,9.6
  spreads; -2.0,-1.5,-5.0,0.5,-4.8 biases; bounce 0.37/0.61/0.59). RNG order (8 draws/
  fresh slot: px,py,pz,vx,vy,vz, life=r&0x7F+127, phase) VERIFIED.
- Pass-2 gravity+bounce control flow & fields VERIFIED.
- **FIX (x87 precision):** disasm 0x42c054 — same pattern as UpdateEmitter: `fst` to a
  float slot keeps st0; px uses extended dt, py/pz reload float fdt.
  - before: `p->px = fdt * p->vx + p->px;`
  - after:  `p->px = (float)(dt * p->vx + p->px);`

### UpdateTrail @0x42c140 — FIXED
- Constants dbl_611A1C..flt_611A54 re-verified (0.2857142857, 2.0, 0.2222222222, 0.25,
  0.3333333432674408, -0.5, -2.0). Respawn field map (10 RNG draws) + colour/life/seed
  jitter VERIFIED byte-exact.
- **FIX (x87 precision):** disasm 0x42c17c — dt (st0) is held at extended precision and
  used for ALL FOUR updates (px,py,pz,life) WITHOUT any float round-trip (no `fst` to a
  dword slot, unlike SeedParticles). Source rounded dt to a 32-bit float first.
  - before: `float dt = (float)((double)(u32)(now-birth) * kTrDt); p->px = dt*p->vx+p->px; ...`
  - after:  `double dt = (double)(u32)(now-birth) * kTrDt;
             p->px = (float)(dt*p->vx+p->px); p->py = (float)(dt*p->vy+p->py); ...
             float nlife = (float)(p->life - dt*e.baseVy);`

### UpdateGravity @0x42c42c — FIXED (3 fixes)
- Constants flt_611A68..flt_611A9C re-verified (0.001,0.8,1.5,2.0,0.1,0.05,-10.0,50.0,
  -0.5,-55.0,-0.2,0.4). RNG order (5 draws/re-emit) + main-loop ballistic path + half-
  count re-emit gate (a1[8]>>1 unsigned) VERIFIED.
- **FIX A (re-emit t1 — WRONG FIELDS):** disasm 0x42c549 = `fld [edx+3Ch]`(py) `fadd
  flt_611A94`(-55) `fmul [ecx+8]`(baseVz). Hex-Rays read it as `a1*damping`; both wrong.
  - before: `float t1 = (p->a1 + kGrBias55) * e.damping;`
  - after:  `float t1 = (p->py + kGrBias55) * e.baseVz;`
- **FIX B (x87 precision):** disasm 0x42c685 — dt (st0) is kept extended for both the
  seed accumulate and the vy update (no float round-trip).
  - before: `p->seed = (float)(dt*kGrTime) + p->seed; p->vy = p->vy - (float)dt;`
  - after:  `p->seed = (float)(dt*kGrTime + p->seed); p->vy = (float)(p->vy - dt);`
- **FIX C (re-emit vy precision):** disasm 0x42c57c `fst [edx+4]` keeps the 80-bit vy
  for the t2 multiply (`fmul [ecx]`=baseVx). Source used the float-stored vy.
  - after: compute `double vyEx` once, store `p->vy=(float)vyEx`, feed `vyEx` into t2;
    `vz = (float)(rnd*norm*0.05 + t2 + t3)` (no intermediate float round between adds).

### UpdateCosineWave @0x42c7c8 — VERIFIED-1:1
- Constants flt_611AA8..flt_611AB0 re-verified (pi/2=1.5707963705, 0.4, 255.0).
- t=(now-birth)/dur rounded to float before cos (matches `(float)t`); px/py/pz/seed
  field map (life*w, a1*w+w*baseVz, a2*w, ampShade*lifeBase+lifeBase), shade via
  ConvertX truncate, and `return now < birth+dur` all VERIFIED. No RNG.

### UpdateFadeOut @0x42cadc — VERIFIED-1:1
- No constants. Window test (`now<birth || now>dur+birth`, unsigned), tnorm float
  round, px/py/pz = (+16/+20/+24)*tnorm, triangular alpha split on threshold +0x1C
  (rise = baseVx*tnorm*vx, fall = (1-(tnorm-thresh)*vy)*baseVx), ConvertX truncate,
  dead-count return all VERIFIED. No RNG.

### UpdateScatter @0x42cde8 — FIXED (2 fixes) + 1 documented boundary
- Constants dbl_611AF4..dbl_611B2C re-verified (4.0,1.5,2.0,0.4,0.25,0.3333333432674408,
  0.29,0.34,0.31,-2.0,0.2). Init-pass colour mask (byte3 AND, byte2/1/0 add), frame=i%mod,
  position/velocity spreads, rejection-sample scale, life=r&0x3F+192, seed jitter, and the
  full 11-draw RNG order VERIFIED. Pass-2 bounce/stop-threshold control flow VERIFIED.
- **FIX A (life decay — WRONG FIELD):** disasm 0x42ceb7 = `fmul [ebx+8]` = emitter+0x8 =
  baseVz, NOT damping (+0x18).
  - before: `float nlife = p->life - dt * e.damping;`
  - after:  `float nlife = p->life - dt * e.baseVz;`
- **FIX B (x87 precision):** disasm 0x42ce2e — same `fst`-keeps-st0 pattern; px uses the
  extended dt, py/pz reload the float-rounded dt.
  - after: `double dtEx=...; float dt=(float)dtEx; p->px=(float)(dtEx*p->vx+p->px); py/pz use dt`
- BOUNDARY (x87-80bit, no portable fix): the rejection-shell test's FIRST compare
  (`rad > maxSpeed`) uses the 80-bit sqrt result while the SECOND (`rad < maxSpeed*0.4`)
  and the `/rad` divide use the float-rounded rad (disasm 0x42d073-d07c). Reproducing the
  80-bit-vs-float straddle exactly is impossible in portable C++; kept rad as float for
  both (sub-ULP-boundary, extremely rare). Documented in code.

### Helpers — VERIFIED-1:1
- `TruncToward` = (int) toward zero (matches ConvertX frndint RC=11, confirmed by
  decompiling 0x5c6b08: HIBYTE(cw)=31). `VectorNormalize` matches 0x5cb148 (|v| low-31-bit
  zero test -> zero the vector). `kRandNorm` = 0x38000100 = flt_6119A8/C8/A2C/AF0/611A74,
  all the same bit-pattern, verified.

## particle_emitter_create.cpp

These are scene-graph LIST plumbing + wave-7/9/10 integration glue, modelled (and
documented as such) on the engine's dual-sentinel object list with a single-sentinel
abstraction.

### ParticleSystemList::Init @0x5e0e00 — VERIFIED (model)
- 0x5e0e00 sets dword_1408438=&unk_1408440 and dword_140874C=&unk_1408130 (two
  terminators of one logical list). The reimpl collapses both to one sentinel. Faithful
  to the empty-state semantics.

### LinkAtTail @0x5e11f5 — VERIFIED-1:1
- disasm 0x5e11f5 confirmed exactly: owner [+0x2F0], node.next=sentinel [+0x308],
  node.prev=oldTail [+0x30C], oldTail.next=node, tail=node. (The reimpl's null/`!system`
  guard and head-cache promotion are documented adaptations of the single-sentinel model;
  the pointer arithmetic is byte-identical.)

### Unlink @0x5e0e9c — VERIFIED-1:1 (model)
- disasm: a1[195]=prev(+0x30C), a1[194]=next(+0x308), a1[188]=owner(+0x2F0); prev==head-
  sentinel -> owner.head[+164]=next else prev.next[+776]=next; next!=tail-sentinel ->
  next.prev[+780]=prev else owner.tail[+168]=prev; then head/tail caches re-read from the
  scene-root (off_649D64). The reimpl models prev/next splice + head/tail recache against
  its single sentinel. The owner +164/+168 cache fields and the off_649D64 scene-root
  identity are out-of-tree engine globals -> BOUNDARY (documented).

### WalkAndRender @0x5b3a86 — VERIFIED-1:1
- Walk eax=dword_1408438 while !=&unk_1408440, edx=[eax+0x308] BEFORE the RenderSystem
  call (self-unlink safe), eax=edx. Matches.

### WalkAndUpdate / SpawnEmitterAtPosition / DestroyAllSystems / LiveSystems
- Wave-9/10 integration glue (rule-13 wiring), not 1:1 translations of single binary
  functions. They REUSE CreateEmitter (0x43fd24)->AllocSystem and the verified list ops.
  No provenance-function divergence; left as-is.

## Counts
- VERIFIED-1:1: 7  (UpdateCosineWave, UpdateFadeOut, helpers TruncToward/VectorNormalize,
  Init, LinkAtTail, Unlink, WalkAndRender)
- FIXED: 5 emitter kernels (UpdateEmitter, SeedParticles, UpdateTrail, UpdateGravity[3],
  UpdateScatter[2]) — 8 individual divergences fixed total:
    * 2 WRONG-FIELD bugs (Gravity re-emit t1 = py*baseVz not a1*damping;
      Scatter life decay = baseVz not damping)
    * 6 x87-extended-precision dt/seed/vy round-trip fixes
- BOUNDARY: 2  (Scatter rejection-shell first-compare 80-bit sqrt; Unlink owner-cache /
  scene-root globals)

## Handoffs
- `src/gui/widget_layout.cpp` (another agent, UNTRACKED) fails to compile: `Widget` has
  no member `ld` (used as `w.ld<i32>(28)`). This blocks the FULL library/test build (not my
  files). My two files pass `g++ -std=c++17 -fsyntax-only` cleanly.
- `particle_integrate.cpp` / `particle_render.cpp` / `particle_spawn.cpp` are owned by
  another agent. No shared-symbol changes were needed from my side (the kEmDt/kSdDt etc.
  constants and Particle/Emitter structs live in particle.h, untouched in layout). If that
  agent likewise reconstructs dt integration, the same x87 "px-extended, py/pz-float" /
  "Trail all-extended" pattern applies — recommend they cross-check their dt handling.

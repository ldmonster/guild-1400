# Harden pass — render rain/weather particles

Files audited:
- src/render/rain.cpp
- src/render/rain_grow_misc_recon.cpp
(+ matching .h, + tests/unit/rain_grow_misc_recon_test.cpp)

IDA module gilde.exe @ imagebase 0x400000. Every function with `// gilde.exe 0xADDR`
provenance was decompiled + disassembled and diffed line-for-line. RNG draw count
and order, all constants (get_bytes, bit-exact), float->int sites, clamp/wrap order,
and per-drop stride were verified against the disassembly.

## Per-function status

### RainSeedDrops  — gilde.exe 0x429098 (VIBE_Rain_Create seed loop)  — VERIFIED-1:1
- 6 RandNext draws per drop, order px,py,pz,size,d0,d1 — matches disasm 0x429167..0x429230.
- Constants flt_6115E0=1/32767, 6115E4=2.0, 6115E8=0.25, 6115EC=0.05, 6115F0=-0.5 — bit-exact.
- Formulas: pos = (rnd/32767 - 0.5)*2 ; size = rnd/32767*0.25 + 0.25 ; d0/d1 = rnd/32767*0.05 + 0.25. Match.
- Loop count n (guarded by n>0, do-while i+1<n) equivalent to for(i<n). Match.

### RainUpdateDrop — gilde.exe 0x4294d4 (VIBE_Rain_UpdateDrop)  — VERIFIED-1:1
- Anchor billboard off = 2.5 * eulerMatrix(prevAnchor-camAnchor)[col2]; off2 computed but dead. Match.
- Drift = (prevEye-camEye)*camMatrix*0.0025 (flt_611670). Match.
- Wind = (windX,0,windZ)*camMatrix ; Gravity = (0,-0.75,0)*camMatrix. Match (operand order d1*g + d0*wind confirmed @0x42984d).
- Per-axis integrate: p += dt*vX + drift + off + p (X/Y) ; Z uses drift2*2.0 (flt_611678), NOT off2 — deliberate asymmetry confirmed @0x429a8f. Match.
- Clamp [-1000,1000] (dbl_611684/61168C) compares the UNROUNDED extended value (fst var_64 before store); C++ tests the pre-cast double. Match.
- Wrap order: while p < -1.0 (dbl_611694) add +2.0 (dbl_61169C); then while p >= 1.0 add -2.0 (dbl_6116A4). Reloads float each iter. Match.
- Projection num = (n*0.5 + maxHalf*0.33333334)*3.0 ; head proj = num/(pz*2+3) ; tail proj2 = num/(3 + 2*(pz+wZ)). Match.
- No float->int in the integrator (only fild on int viewport extents / int count). Match.
- x87 80-bit accumulation modelled as double per project convention.

### RainStreakDiffuse — gilde.exe 0x429c38 head (colour pack)  — FIXED -> VERIFIED-1:1
- Constants flt_611768=0.0005, 61176C=96.0, 611770=128.0 — bit-exact.
- Bit layout CONFIRMED from disasm (not the misleading decompile): 0x429d23 `shl eax,10h` =>
  diffuse = 0x80000000 | (a<<16) | (b<<8) | b. C++ already correct.
- ConvertX @0x5c6b08 sets x87 RC high byte to 0x1F (round-toward-zero) and frndint =>
  TRUNCATE toward zero. C++ ConvertXTrunc = std::trunc. Match.
- **FIX**: the binary keeps f=1.0-count*0.0005 on the x87 stack and multiplies by 96/128
  WITHOUT rounding f to float first (`fld st; fmul flt_61176C`, 0x429d02..0x429d1d).
  The C++ had `(float)f * kStreakR` — an extra float round. Removed the cast; now
  `trunc(f * 96.0)` / `trunc(f * 128.0)` in double, matching x87. Edge-case-only
  divergence; golden vectors (count=0 -> 0x80608080) unchanged.

### RainRenderToSurface — gilde.exe 0x429c38 tail  — VERIFIED-1:1 (approved tech swap, rule 3)
- Original is a D3D LINELIST DrawPrimitiveUP; reconstructed as software SurfaceDrawLine per rule 3.
- All-four-corners viewport clip (sx,sx2 in [x0,x1); sy,sy2 in [y0,y1)) matches 0x429d65..0x429e2b
  (bounds fild int->float then fcomp). Diffuse byte extraction (R=a,G=b,B=b) consistent with the pack.

### Rain_GrowDropList — gilde.exe 0x4292b8  — FIXED -> VERIFIED-1:1
- Constants flt_6115F4=0.001, 6115F8=1/32767, 6115FC=2.0, 611600=0.25, 611604=0.05, 611608=-0.5 — bit-exact.
- Variadic cursor (ebp walk): op==0 consumes {op,count}; op==1 {op,count,extra}; op==2 {op,a,b}. Matches ebp `add 4` accounting. C++ nextArg() model correct.
- Grow path: newCap=count+100, alloc 40*newCap (tag "d3t:RainDropList"), copy 40*capacity bytes (movsd+movsb), free old, RNG-seed slots [oldCount,newCap) with 6 draws/slot in order — matches 0x42937B..0x4294af. Match.
- op==2 burst: f24=a*0.001, f28=b*0.001, f38=f2c, f14=f1c, f18=f20, f3c=f2c+b. Match.
- op==1 activate: f08=head, f0c=count, f30=f2c, f34=f2c+extra. Match.
- **FIX (head is i32, not float)**: Hex-Rays types a1 as float* and prints `*a1 = 0.0`,
  but the disasm only touches [ebx] with integer moves: op==0 `mov [ebx],eax` stores
  COUNT (raw int, not 0.0); grow reads `mov edi,[ebx]` as raw int oldCount; op==1 copies
  `mov eax,[ebx]; mov [ebx+8],eax` raw. No fld/fild/fstp on [ebx] anywhere. Changed
  RainGrowSys::head float->i32; op==0 store now `sys.head = count`; oldCount/f08 reads now
  raw int (no float<->int conversion). Updated test golden checks (50, 10, 3 as ints).

## Tests (build only the targets, no full rebuild)
- rain_grow_misc_recon_test  ... Passed
- rain_updatedrop_wave23_test ... Passed
- rain_weather_wave6_test     ... Passed
- snow_render_test            ... Passed
4/4 passed.

## Net changes
- src/render/rain.cpp: RainStreakDiffuse — drop the `(float)f` round before *96/*128 (x87 double-precision fidelity).
- src/render/rain_grow_misc_recon.h: RainGrowSys::head float -> i32 (+ doc); op==0 doc.
- src/render/rain_grow_misc_recon.cpp: head writes/reads as raw int at op==0 / grow / op==1.
- tests/unit/rain_grow_misc_recon_test.cpp: head golden checks updated to int literals.
